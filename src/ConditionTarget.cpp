#define WIN32_LEAN_AND_MEAN
#include "ConditionTarget.h"
#include "MarriageCommit.h"
#include "BeautyPicker.h"
#include "PartnerManager.h"
#include "PartnerSummon.h"
#include "../vendor/minhook/src/hde/hde64.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
const char* typeNameFor(ConditionKind kind) {
    switch (kind) {
    case ConditionKind::FriendlyLevel: return ".?AVConditionData_CheckFriendlyLevel@pa@@";
    case ConditionKind::Marriageable: return ".?AVConditionData_IsMarriageable@pa@@";
    case ConditionKind::LivingTogether: return ".?AVConditionData_HasLivingTogether@pa@@";
    case ConditionKind::AffinityLevel: return ".?AVConditionData_CheckAffinityLevel@pa@@";
    case ConditionKind::Tribe: return ".?AVConditionData_CheckTribe@pa@@";
    case ConditionKind::Count: return nullptr;
    }
    return nullptr;
}
struct Span { DWORD begin, end; bool executable; };
struct Locator {
    DWORD signature, offset, constructorOffset, type, hierarchy, self;
};
static_assert(sizeof(Locator) == 24);

// VirtualQuery 检查实际页状态，避免扫描 PE 中尚未提交、被保护或不可读的区域。
bool readable(const MEMORY_BASIC_INFORMATION& page) {
    if (page.State != MEM_COMMIT || (page.Protect & PAGE_GUARD)) return false;
    switch (page.Protect & 0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}
bool memoryReadable(const void* address, std::size_t size) {
    auto at = reinterpret_cast<std::uintptr_t>(address);
    if (size > UINTPTR_MAX - at) return false;
    const auto end = at + size;
    while (at < end) {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &page, sizeof(page)) || !readable(page))
            return false;
        const auto next = reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize;
        if (next <= at) return false;
        at = (std::min)(next, end);
    }
    return true;
}

class Image {
public:
    std::uint8_t* base = nullptr;
    DWORD size = 0;
    IMAGE_DATA_DIRECTORY exceptions{};
    std::vector<Span> spans;

    bool open(HMODULE module) {
        base = reinterpret_cast<std::uint8_t*>(module);
        if (!base || !memoryReadable(base, sizeof(IMAGE_DOS_HEADER))) return false;
        IMAGE_DOS_HEADER dos{};
        std::memcpy(&dos, base, sizeof(dos));
        // 这是 PE 格式边界检查，不比较游戏版本、时间戳或固定映像大小。
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) ||
            dos.e_lfanew > 0x100000) return false;
        const auto ntAt = base + dos.e_lfanew;
        if (!memoryReadable(ntAt, sizeof(IMAGE_NT_HEADERS64))) return false;
        IMAGE_NT_HEADERS64 nt{};
        std::memcpy(&nt, ntAt, sizeof(nt));
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96) return false;
        size = nt.OptionalHeader.SizeOfImage;
        const auto sectionOffset = static_cast<std::size_t>(dos.e_lfanew) +
            sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        const auto sectionBytes = nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
        if (sectionOffset > size || sectionBytes > size - sectionOffset ||
            !memoryReadable(base + sectionOffset, sectionBytes)) return false;
        if (nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION)
            exceptions = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];

        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            std::memcpy(&section, base + sectionOffset + i * sizeof(section), sizeof(section));
            if (!(section.Characteristics & IMAGE_SCN_MEM_READ)) continue;
            const auto begin = section.VirtualAddress;
            const auto length = section.Misc.VirtualSize;
            if (!length || begin > size || length > size - begin) return false;
            const DWORD end = begin + length;
            auto at = static_cast<std::uintptr_t>(begin);
            while (at < end) {
                MEMORY_BASIC_INFORMATION page{};
                if (!VirtualQuery(base + at, &page, sizeof(page))) return false;
                const auto pageEnd = reinterpret_cast<std::uintptr_t>(page.BaseAddress) +
                    page.RegionSize - reinterpret_cast<std::uintptr_t>(base);
                const auto next = (std::min)(pageEnd, static_cast<std::uintptr_t>(end));
                if (next <= at) return false;
                if (readable(page)) {
                    const auto protection = page.Protect & 0xff;
                    const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                        (protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                         protection == PAGE_EXECUTE_WRITECOPY);
                    // 合并同一节内权限一致的相邻页，允许搜索跨页但完整可读的数据。
                    if (!spans.empty() && spans.back().end == at &&
                        spans.back().executable == executable && at != begin)
                        spans.back().end = static_cast<DWORD>(next);
                    else spans.push_back({static_cast<DWORD>(at), static_cast<DWORD>(next), executable});
                }
                at = next;
            }
        }
        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
        return !spans.empty();
    }
    bool contains(DWORD rva, std::size_t length, bool code = false) const {
        for (const auto& span : spans)
            if (rva >= span.begin && rva < span.end && length <= span.end - rva &&
                (!code || span.executable)) return true;
        return false;
    }
    template<class T> bool read(DWORD rva, T& value) const {
        if (!contains(rva, sizeof(value))) return false;
        std::memcpy(&value, base + rva, sizeof(value));
        return true;
    }
    bool pointerRva(std::uintptr_t pointer, DWORD& rva) const {
        const auto begin = reinterpret_cast<std::uintptr_t>(base);
        if (pointer < begin || pointer - begin >= size) return false;
        rva = static_cast<DWORD>(pointer - begin);
        return true;
    }
    // PE 的 x64 异常目录提供非叶函数的边界，避免把邻近函数的指令当成目标特征。
    bool functionRange(DWORD rva, DWORD& begin, DWORD& length) const {
        const auto directory = exceptions;
        if (!directory.Size || directory.Size % sizeof(RUNTIME_FUNCTION) ||
            directory.VirtualAddress >= size || directory.Size > size - directory.VirtualAddress)
            return false;
        std::size_t low = 0, high = directory.Size / sizeof(RUNTIME_FUNCTION);
        while (low < high) {
            const auto mid = low + (high - low) / 2;
            RUNTIME_FUNCTION fn{};
            if (!read(directory.VirtualAddress + static_cast<DWORD>(mid * sizeof(fn)), fn)) return false;
            if (rva < fn.BeginAddress) high = mid;
            else if (rva >= fn.EndAddress) low = mid + 1;
            else {
                if (fn.EndAddress <= fn.BeginAddress) return false;
                begin = fn.BeginAddress;
                length = fn.EndAddress - begin;
                return length <= 4096 && contains(begin, length, true);
            }
        }
        return false;
    }
    bool functionSize(DWORD rva, DWORD& length) const {
        DWORD begin = 0;
        return functionRange(rva, begin, length) && begin == rva;
    }
    bool functionRoot(DWORD rva,DWORD& root) const {
        RUNTIME_FUNCTION fn{};bool found=false;
        for(DWORD at=0;at+sizeof(fn)<=exceptions.Size;at+=sizeof(fn)) {
            if(!read(exceptions.VirtualAddress+at,fn))return false;
            if(rva>=fn.BeginAddress && rva<fn.EndAddress){found=true;break;}
            if(fn.BeginAddress>rva)break;
        }
        if(!found)return false;
        for(unsigned depth=0;depth<8;++depth) {
            BYTE header[4]{};
            if(!contains(fn.UnwindData,sizeof(header)))return false;
            std::memcpy(header,base+fn.UnwindData,sizeof(header));
            if((header[0]&7)!=1)return false;
            if(!(header[0]&0x20)){root=fn.BeginAddress;return contains(root,1,true);}
            const DWORD chained=fn.UnwindData+4+((static_cast<DWORD>(header[2])+1)&~1u)*2;
            RUNTIME_FUNCTION parent{};if(!read(chained,parent) || parent.BeginAddress>=fn.BeginAddress)return false;
            fn=parent;
        }
        return false;
    }
};

// 不比较完整机器码；按条件类型检查上下文读取与有效返回值模式。
// 忽略堆栈布局、context 字段偏移、相对调用地址和分支距离等易随构建变化的值。
bool compatibleEvaluator(const Image& image, DWORD rva, ConditionKind kind) {
    DWORD length = 0;
    if (!image.functionSize(rva, length)) return false;
    const auto code = image.base + rva;
    bool contextLoad = false, unavailable = false, predicate = false;
    bool tribeFailure = false, tribeSuccess = false, byteReturn = false;
    unsigned inversions = 0;
    for (DWORD i = 0; i + 7 <= (std::min)(length, DWORD{96}); ++i)
        // 关系评估器从 RDX 读取上下文；种族评估器先把 RDX 保存到 RAX。
        if (code[i] == 0x48 && code[i+1] == 0x8b &&
            (code[i+2] & 0xc7) == (kind == ConditionKind::Tribe ? 0x80 : 0x82))
            contextLoad = true;
    constexpr unsigned char nullPredicate[] = {0x48,0x85,0xdb,0x0f,0x94,0xc3};
    constexpr unsigned char invertBoolean[] = {0x84,0xc0,0x0f,0x94,0xc0};
    for (DWORD i = 0; i + 2 <= length; ++i) {
        if (code[i] == 0xb0 && code[i+1] == 2) unavailable = true;
        if (code[i] == 0xb3 && code[i+1] == 1) tribeFailure = true; // mov bl,1
        if ((code[i] == 0x32 || code[i] == 0x30) && code[i+1] == 0xdb) tribeSuccess = true; // xor bl,bl
        if (i + 3 <= length && code[i] == 0x0f && code[i+1] == 0xb6 && code[i+2] == 0xc3)
            byteReturn = true; // movzx eax,bl
        if (i + sizeof(invertBoolean) <= length &&
            std::memcmp(code + i, invertBoolean, sizeof(invertBoolean)) == 0) ++inversions;
        if (i + sizeof(nullPredicate) <= length &&
            std::memcmp(code + i, nullPredicate, sizeof(nullPredicate)) == 0) predicate = true;
    }
    if (!contextLoad) return false;
    // CheckTribe 的不匹配路径返回 1、匹配路径返回 0，没有单独的返回 2 分支。
    if (kind == ConditionKind::Tribe) return tribeFailure && tribeSuccess && byteReturn;
    if (!unavailable) return false;
    if (kind == ConditionKind::LivingTogether) return predicate;
    // 可结婚判断把普通布尔值取反为三态结果；好感和亲密度有多个比较分支。
    const bool comparison = kind == ConditionKind::FriendlyLevel || kind == ConditionKind::AffinityLevel;
    return inversions >= (comparison ? 2u : 1u);
}

// 在虚表各入口中识别“小分派函数”，从其跳转指令读取真正评估槽号。
// 不固定分派函数自身的槽号，也不固定目标槽号 21。
bool dispatchSlot(const Image& image, DWORD rva, unsigned& index) {
    if (!image.contains(rva, 32, true)) return false;
    const auto p = image.base + rva;
    constexpr unsigned char forwardContext[] = {0x48,0x8b,0x02,0x48,0x8b,0xd1,0x49,0x8b,0xc9};
    if (p[0] != 0x48 || p[1] != 0x83 || p[2] != 0x79 || p[4] != 0 ||
        p[5] != 0x4c || p[6] != 0x8b || p[7] != 0xca || p[8] != 0x74 ||
        std::memcmp(p + 10, forwardContext, sizeof(forwardContext)) != 0) return false;
    const int destination = 10 + static_cast<std::int8_t>(p[9]);
    if (destination < 19 || destination > 55 || !image.contains(rva, destination + 10, true)) return false;
    const auto jump = p + destination;
    if (jump[0] != 0x48 || jump[1] != 0x8b || jump[2] != 0x01) return false;
    auto opcode = jump + 3;
    if (*opcode == 0x48) ++opcode; // MSVC 可在间接 jmp 前保留 REX.W 前缀。
    if (opcode[0] != 0xff) return false;
    std::int32_t displacement = 0;
    if (opcode[1] == 0xa0) std::memcpy(&displacement, opcode + 2, sizeof(displacement));
    else if (opcode[1] == 0x60) displacement = static_cast<std::int8_t>(opcode[2]);
    else return false;
    if (displacement < 0 || displacement % sizeof(void*) || displacement / sizeof(void*) >= 128) return false;
    index = static_cast<unsigned>(displacement / sizeof(void*));
    return true;
}

// 从等级评估器恢复运算字段、阈值字段及 switch 表的实际比较语义。
// 同一个函数用于“>=3 成功”和“<3 失败”，两者必须按同一虚拟等级互斥求值。
bool comparisonLayout(const Image& image, DWORD rva, ConditionKind kind, ConditionTarget& target) {
    DWORD length = 0;
    if (!image.functionSize(rva, length)) return false;
    const auto code = image.base + rva;
    unsigned objectRegister = 0, switches = 0;
    DWORD tableAt = 0;
    for (DWORD i = 0; i + 8 <= length; ++i) {
        auto p = code + i;
        unsigned prefix = 0, rex = 0;
        if (p[0] >= 0x40 && p[0] <= 0x4f) { rex = p[0]; prefix = 1; ++p; }
        // movzx eax,byte ptr [object+disp8]; cmp eax,5
        if (p[0] != 0x0f || p[1] != 0xb6 || (p[2] & 0xf8) != 0x40 ||
            (p[2] & 7) == 4 || (rex & 4) || p[3] >= 0x80 ||
            p[4] != 0x83 || p[5] != 0xf8 || p[6] != 5) continue;
        if (++switches != 1) return false;
        objectRegister = (p[2] & 7) + ((rex & 1) ? 8u : 0u);
        target.comparisonOffset = p[3];
        tableAt = i + prefix + 7;
        i += prefix + 6;
    }
    if (switches != 1) return false;

    // lea rdx,[rip+base]; mov ecx,[rdx+rax*4+table]; add rcx,rdx; jmp rcx
    DWORD tableRva = 0, baseRva = 0;
    unsigned tables = 0;
    const DWORD searchEnd = (std::min)(length, tableAt + 64);
    for (DWORD i = tableAt; i + 19 <= searchEnd; ++i) {
        const auto p = code + i;
        constexpr unsigned char indexLoad[] = {0x8b,0x8c,0x82};
        constexpr unsigned char tail[] = {0x48,0x03,0xca,0xff,0xe1};
        if (p[0] != 0x48 || p[1] != 0x8d || p[2] != 0x15 ||
            std::memcmp(p+7,indexLoad,sizeof(indexLoad)) || std::memcmp(p+14,tail,sizeof(tail))) continue;
        std::int32_t relativeBase = 0, relativeTable = 0;
        std::memcpy(&relativeBase,p+3,4);
        std::memcpy(&relativeTable,p+10,4);
        const std::int64_t base = static_cast<std::int64_t>(rva) + i + 7 + relativeBase;
        const std::int64_t table = base + relativeTable;
        if (++tables != 1 || base < 0 || base >= image.size || table < 0 || table >= image.size ||
            !image.contains(static_cast<DWORD>(table),6*sizeof(DWORD))) return false;
        baseRva = static_cast<DWORD>(base);
        tableRva = static_cast<DWORD>(table);
    }
    if (tables != 1) return false;

    bool haveThreshold = false;
    for (unsigned index = 0; index < 6; ++index) {
        std::int32_t relativeCase = 0;
        if (!image.read(tableRva + index*sizeof(DWORD),relativeCase)) return false;
        const std::int64_t branch = static_cast<std::int64_t>(baseRva) + relativeCase;
        if (branch < rva || branch + 16 > static_cast<std::int64_t>(rva) + length) return false;
        auto p = image.base + branch;
        unsigned rex = 0;
        if (p[0] >= 0x40 && p[0] <= 0x4f) { rex = p[0]; ++p; }
        unsigned threshold = 0;
        const unsigned char* result = nullptr;
        if (kind == ConditionKind::FriendlyLevel) {
            // cmp level8,[object+disp8]
            if (p[0] != 0x3a || (p[1] & 0xc0) != 0x40 || (p[1] & 7) == 4 || p[2] >= 0x80 ||
                (p[1] & 7) + ((rex & 1) ? 8u : 0u) != objectRegister) return false;
            threshold = p[2];
            result = p + 3;
        } else {
            // movsx eax,byte ptr [object+disp8]; cmp level32,eax
            if (p[0] != 0x0f || p[1] != 0xbe || (p[2] & 0xf8) != 0x40 ||
                (p[2] & 7) == 4 || (rex & 4) || p[3] >= 0x80 ||
                (p[2] & 7) + ((rex & 1) ? 8u : 0u) != objectRegister ||
                p[4] != 0x3b || (p[5] & 0xc7) != 0xc0) return false;
            threshold = p[3];
            result = p + 6;
        }
        if (haveThreshold && target.thresholdOffset != threshold) return false;
        target.thresholdOffset = threshold;
        haveThreshold = true;
        // setcc al; test al,al; sete al，把普通布尔比较转成引擎 0=满足的结果。
        if (result[0] != 0x0f || result[2] != 0xc0 || result[3] != 0x84 ||
            result[4] != 0xc0 || result[5] != 0x0f || result[6] != 0x94 || result[7] != 0xc0) return false;
        switch (result[1]) {
        case 0x94: target.comparisonOps[index]=ComparisonOp::Equal; break;
        case 0x95: target.comparisonOps[index]=ComparisonOp::NotEqual; break;
        case 0x9e: target.comparisonOps[index]=ComparisonOp::LessEqual; break;
        case 0x9d: target.comparisonOps[index]=ComparisonOp::GreaterEqual; break;
        case 0x9c: target.comparisonOps[index]=ComparisonOp::Less; break;
        case 0x9f: target.comparisonOps[index]=ComparisonOp::Greater; break;
        default: return false;
        }
    }
    target.hasComparison = true;
    return true;
}

bool evaluateVtable(const Image& image, DWORD vtable, ConditionTarget& result, ConditionKind kind) {
    std::vector<DWORD> entries;
    for (unsigned i = 0; i < 128; ++i) {
        std::uintptr_t pointer = 0;
        DWORD code = 0;
        if (!image.read(vtable + i * sizeof(void*), pointer) ||
            !image.pointerRva(pointer, code) || !image.contains(code, 1, true)) break;
        entries.push_back(code);
    }
    unsigned selected = 0, matches = 0;
    for (const auto entry : entries) {
        unsigned index = 0;
        if (!dispatchSlot(image, entry, index)) continue;
        if (index >= entries.size() || !compatibleEvaluator(image, entries[index], kind)) continue;
        if (!matches || selected != index) { selected = index; ++matches; }
    }
    if (matches != 1) return false;
    if ((kind == ConditionKind::FriendlyLevel || kind == ConditionKind::AffinityLevel) &&
        !comparisonLayout(image,entries[selected],kind,result)) return false;
    result.vtableRva = vtable;
    result.slotIndex = selected;
    result.evaluatorRva = entries[selected];
    result.slot = reinterpret_cast<void**>(image.base + vtable) + selected;
    result.evaluator = image.base + result.evaluatorRva;
    return true;
}
}

bool FindConditionTarget(HMODULE module, ConditionTarget& target, const char*& error, ConditionKind kind) {
    target = {};
    Image image;
    if (!image.open(module)) { error = "invalid or unreadable PE64 image"; return false; }
    const auto typeName = typeNameFor(kind);
    if (!typeName) { error = "unknown condition kind"; return false; }
    const auto nameSize = std::strlen(typeName) + 1;

    std::vector<DWORD> types;
    for (const auto& span : image.spans) {
        // 名称位置前有两个 64 位字段，构成 MSVC TypeDescriptor。
        auto at = span.begin;
        while (at < span.end && nameSize <= span.end - at) {
            const auto hit = static_cast<const std::uint8_t*>(
                std::memchr(image.base + at, typeName[0], span.end - at - nameSize + 1));
            if (!hit) break;
            at = static_cast<DWORD>(hit - image.base);
            if (at >= 16 && image.contains(at - 16, 16 + nameSize) &&
                std::memcmp(hit, typeName, nameSize) == 0)
                types.push_back(at - 16);
            ++at;
        }
    }
    if (types.empty()) { error = "target RTTI type not found in this host"; return false; }

    std::vector<DWORD> locators;
    for (const auto& span : image.spans) {
        for (std::uint64_t at = (span.begin + 3ull) & ~3ull; at + sizeof(Locator) <= span.end; at += 4) {
            Locator col{};
            std::memcpy(&col, image.base + at, sizeof(col));
            // x64 COL 使用映像相对地址，且 self 必须指回自身；只接受主虚表。
            if (col.signature != 1 || col.offset || col.constructorOffset || col.self != at ||
                std::find(types.begin(), types.end(), col.type) == types.end()) continue;
            DWORD hierarchy[4]{}, firstBase = 0, firstType = 0;
            if (!image.read(col.hierarchy, hierarchy) || hierarchy[0] != 0 ||
                !hierarchy[2] || hierarchy[2] > 128 ||
                !image.contains(hierarchy[3], hierarchy[2] * sizeof(DWORD)) ||
                !image.read(hierarchy[3], firstBase) || !image.read(firstBase, firstType) ||
                firstType != col.type) continue;
            locators.push_back(static_cast<DWORD>(at));
        }
    }
    if (locators.empty()) { error = "target RTTI locator not found"; return false; }

    unsigned matches = 0;
    for (const auto& span : image.spans) {
        for (std::uint64_t at = (span.begin + 7ull) & ~7ull; at + 2 * sizeof(void*) <= span.end; at += 8) {
            std::uintptr_t pointer = 0;
            std::memcpy(&pointer, image.base + at, sizeof(pointer));
            DWORD colRva = 0;
            if (!image.pointerRva(pointer, colRva) ||
                std::find(locators.begin(), locators.end(), colRva) == locators.end()) continue;
            ConditionTarget candidate{};
            if (!evaluateVtable(image, static_cast<DWORD>(at + sizeof(void*)), candidate, kind)) continue;
            Locator col{};
            image.read(colRva, col);
            candidate.locatorRva = colRva;
            candidate.typeRva = col.type;
            if (++matches > 1) { error = "multiple compatible target vtables; refusing ambiguous hook"; return false; }
            target = candidate;
        }
    }
    if (matches != 1) { error = "target vtable or evaluator dispatch is incompatible"; return false; }
    error = nullptr;
    return true;
}

namespace {
// 私有组件只包含原函数已确认会读取的前缀，必须拒绝访问其他组件字段的变体。
// 此处接受当前“RCX this 保存到 RSI”的只读函数族；遇到别名转存或未知访问即拒绝。
bool supportsPrivateMarriageView(const Image& image, DWORD begin, DWORD length,
    const MarriageCommitTarget& layout) {
    bool selfSaved = false;
    unsigned saves = 0;
    for (DWORD at = 0; at < length;) {
        unsigned char bytes[16] = {};
        std::memcpy(bytes,image.base+begin+at,(std::min)(DWORD{15},length-at));
        hde64s instruction{};
        const auto count = hde64_disasm(bytes,&instruction);
        if (!count || (instruction.flags & F_ERROR) || count > length-at) return false;
        const auto& ins = instruction;
        const unsigned reg = ins.modrm_reg + (ins.rex_r ? 8u : 0u);
        const unsigned rm = ins.modrm_rm + (ins.rex_b ? 8u : 0u);
        const bool modrm = (ins.flags & F_MODRM) != 0;
        const bool saveSelf = modrm && ins.modrm_mod == 3 && ins.opcode == 0x8b &&
            ins.rex_w && reg == 6 && rm == 1;
        if (saveSelf) {
            if (++saves != 1) return false;
            selfSaved = true;
            at += count;
            continue;
        }
        if (!selfSaved) {
            // 禁止把初始 this 指针存到其他寄存器、内存或交给辅助函数。
            if (ins.opcode == 0xe8 || ins.opcode == 0xe9 || ins.opcode == 0xeb || ins.opcode == 0xff ||
                ins.opcode == 0x51 || ins.opcode == 0x59 || (ins.opcode == 0xb9 && !ins.rex_b)) return false;
            if (modrm && ((ins.modrm_mod == 3 && (reg == 1 || rm == 1)) ||
                (ins.modrm_mod != 3 && (reg == 1 || rm == 1)))) return false;
        } else {
            if ((ins.opcode == 0x56 && !ins.rex_b) || (ins.opcode == 0xbe && !ins.rex_b)) return false;
            if ((ins.opcode >= 0xa4 && ins.opcode <= 0xaf) || (ins.opcode == 0x96 && !ins.rex_b)) return false;
            if (ins.opcode == 0x5e && !ins.rex_b && (at+count >= length || image.base[begin+at+count] != 0xc3))
                return false;
            if (modrm && ins.modrm_mod == 3 && (reg == 6 || rm == 6)) return false;
            if (modrm && ins.modrm_mod != 3) {
                // 不接受把 RSI 当作数据写出，也不接受以 RSI 为索引的复杂别名。
                if (reg == 6) return false;
                const bool sib = (ins.flags & F_SIB) != 0;
                if (sib && ins.sib_index + (ins.rex_x ? 8u : 0u) == 6) return false;
                const unsigned base = sib ? ins.sib_base + (ins.rex_b ? 8u : 0u) : rm;
                if (base == 6) {
                    if (sib || ins.p_67 || ins.p_seg ||
                        (ins.opcode != 0x8b && ins.opcode != 0x03 && ins.opcode != 0x39)) return false;
                    std::int64_t offset = 0;
                    if (ins.flags & F_DISP8) offset = static_cast<std::int8_t>(ins.disp.disp8);
                    else if (ins.flags & F_DISP32) offset = static_cast<std::int32_t>(ins.disp.disp32);
                    if (offset != 8 && offset != layout.arrayOffset && offset != layout.arrayOffset+8)
                        return false;
                }
            }
        }
        at += count;
    }
    return selfSaved && saves == 1;
}
}

namespace {
// 控制入口的返回结构当前只有一个 DWORD。拒绝把输出指针转存到未知辅助函数、
// 访问额外字段或已有入口跳转的变体，确保扩展调用的临时结果缓冲区契约明确。
bool spawnOutputIsDword(const Image& image,DWORD begin,DWORD length) {
    bool saved=false,returned=false;
    unsigned saves=0;
    for(DWORD at=0;at<length;) {
        unsigned char bytes[16]={};std::memcpy(bytes,image.base+begin+at,(std::min)(DWORD{15},length-at));
        hde64s ins{};const auto count=hde64_disasm(bytes,&ins);
        if(!count || (ins.flags&F_ERROR) || count>length-at)return false;
        const unsigned reg=ins.modrm_reg+(ins.rex_r?8u:0u),rm=ins.modrm_rm+(ins.rex_b?8u:0u);
        const bool modrm=(ins.flags&F_MODRM)!=0;
        if(modrm && ins.modrm_mod==3 && ins.opcode==0x8b && ins.rex_w && reg==6 && rm==2) {
            if(++saves!=1)return false;saved=true;at+=count;continue;
        }
        if(!saved) {
            if(ins.opcode==0xe8 || ins.opcode==0xe9 || ins.opcode==0xeb || ins.opcode==0xff)return false;
            if(modrm && ins.opcode==0x89 && ins.modrm_mod!=3 && reg==2)return false;
        } else {
            if((ins.opcode>=0xa4 && ins.opcode<=0xaf) || (ins.opcode==0x56 && !ins.rex_b))return false;
            if(modrm && ins.modrm_mod==3 && (reg==6 || rm==6)) {
                if(ins.opcode==0x8b && ins.rex_w && reg==0 && rm==6 && at+96>=length)returned=true;
                else return false;
            }
            if(modrm && ins.modrm_mod!=3) {
                const bool sib=(ins.flags&F_SIB)!=0;
                const unsigned base=sib?ins.sib_base+(ins.rex_b?8u:0u):rm;
                if(reg==6 && !(returned && ins.opcode==0x8b && base==11))return false;
                if(sib && ins.sib_index+(ins.rex_x?8u:0u)==6)return false;
                if(base==6) {
                    std::int64_t offset=0;
                    if(ins.flags&F_DISP8)offset=static_cast<std::int8_t>(ins.disp.disp8);
                    else if(ins.flags&F_DISP32)offset=static_cast<std::int32_t>(ins.disp.disp32);
                    if(sib || ins.p_seg || ins.p_67 || ins.rex_w || ins.opcode!=0x89 || offset!=0)return false;
                }
            }
            if(returned && (ins.opcode==0xe8 || ins.opcode==0xff))return false;
        }
        if(ins.opcode==0xc3)return saved && returned && saves==1;
        at+=count;
    }
    return false;
}

void findPartnerSpawnControl(const Image& image,MarriageCommitTarget& target) {
    unsigned matches=0;
    for(const auto& span:image.spans) {
        if(!span.executable)continue;
        DWORD at=span.begin;
        while(at<span.end && span.end-at>=5) {
            const auto p=static_cast<const std::uint8_t*>(std::memchr(image.base+at,0xe8,span.end-at-4));
            if(!p)break;at=static_cast<DWORD>(p-image.base)+1;
            std::int32_t relative=0;std::memcpy(&relative,p+1,4);
            if(static_cast<std::int64_t>(at-1)+5+relative!=target.lookupRva)continue;
            DWORD begin=0,length=0;if(!image.functionRange(at-1,begin,length))continue;
            bool arguments=false,record=false;
            constexpr unsigned char args[]={0x41,0x0f,0xb7,0xd9,0x45,0x0f,0xb6,0xf8,0x48,0x8b,0xf2,
                0x4c,0x8b,0xf1,0x48,0x8b,0x79,0x08,0x48,0x8d,0x57,0x08};
            constexpr unsigned char keyAndActor[]={0x8b,0x78,0x04,0x8b,0x00};
            for(DWORD i=0;i+sizeof(keyAndActor)<=length;++i) {
                const auto q=image.base+begin+i;
                if(i+sizeof(args)<=length && !std::memcmp(q,args,sizeof(args)))arguments=true;
                if(!std::memcmp(q,keyAndActor,sizeof(keyAndActor)))record=true;
            }
            if(!arguments || !record || !spawnOutputIsDword(image,begin,length))continue;
            if(matches && target.spawnRva==begin)continue;
            if(++matches>1){target.spawnControl=nullptr;target.spawnRva=0;return;}
            target.spawnRva=begin;target.spawnControl=reinterpret_cast<PartnerSpawnControl>(image.base+begin);
        }
    }
}

bool findMarriageRouting(const Image& image, MarriageCommitTarget& target, DWORD livingEvaluator,
    const char*& error) {
    DWORD guardLength = 0;
    if (!image.functionSize(target.functionRva,guardLength)) return false;
    const auto guard = image.base+target.functionRva;
    unsigned keyHeads = 0, keyTails = 0;
    DWORD headAt = 0, tailAt = 0;
    bool npcAlias = false;
    for (DWORD i = 0; i+3 <= guardLength; ++i) {
        const auto p = guard+i;
        if (p[0] == 0x49 && p[1] == 0x8b && p[2] == 0xe8) npcAlias = true; // rbp = NPC 参数 r8
        if (i+8 <= guardLength && p[0] == 0x48 && p[1] == 0x8b && p[2] == 0x45 && p[3] < 0x80 &&
            p[4] == 0x48 && p[5] == 0x8b && p[6] == 0x58 && p[7] < 0x80) {
            ++keyHeads; headAt=i; target.npcKeyOffsets[0]=p[3]; target.npcKeyOffsets[1]=p[7];
        }
        if (i+7 <= guardLength && p[0] == 0x48 && p[1] == 0x8b && p[2] == 0x5b && p[3] < 0x80 &&
            p[4] == 0x8b && p[5] == 0x5b && p[6] < 0x80) {
            ++keyTails; tailAt=i; target.npcKeyOffsets[2]=p[3]; target.npcKeyOffsets[3]=p[6];
        }
    }
    if (!npcAlias || keyHeads != 1 || keyTails != 1 || headAt >= tailAt) {
        error="NPC relationship key access is incompatible"; return false;
    }

    // 只在真正写入状态 2 的调用路径记住目标，不用预检 / 菜单扫描替换当前对象。
    unsigned writers = 0;
    for (const auto& span : image.spans) {
        if (!span.executable) continue;
        DWORD at = span.begin;
        while (at < span.end && span.end-at >= 5) {
            const auto p = static_cast<const std::uint8_t*>(std::memchr(image.base+at,0xe8,span.end-at-4));
            if (!p) break;
            at=static_cast<DWORD>(p-image.base)+1;
            std::int32_t relative = 0; std::memcpy(&relative,p+1,4);
            if (static_cast<std::int64_t>(at-1)+5+relative != target.functionRva) continue;
            DWORD begin=0,length=0;
            if (!image.functionRange(at-1,begin,length)) continue;
            bool savedState=false, runtimeState=false;
            for (DWORD i=0;i+10<=length;++i) {
                const auto q=image.base+begin+i;
                constexpr unsigned char save[] = {0xc6,0x44,0xf8,0x2c,0x02};
                constexpr unsigned char append[] = {0x41,0xb1,0x02,0x44,0x8b,0x40,0x08,0x48,0x8b,0xd6};
                if (!std::memcmp(q,save,sizeof(save))) savedState=true;
                if (!std::memcmp(q,append,sizeof(append))) runtimeState=true;
            }
            if (!savedState || !runtimeState) continue;
            if (++writers != 1) { error="multiple marriage write callers"; return false; }
            target.writerRva=begin;
            target.commitCallReturn=image.base+at+4;
        }
    }
    if (writers != 1) { error="marriage write caller not found"; return false; }

    DWORD evalLength=0;
    if (!image.functionSize(livingEvaluator,evalLength)) return false;
    unsigned lookups=0;
    for (DWORD i=0;i+5<=evalLength;++i) {
        const auto p=image.base+livingEvaluator+i;
        if (*p!=0xe8) continue;
        std::int32_t relative=0; std::memcpy(&relative,p+1,4);
        const std::int64_t candidate=static_cast<std::int64_t>(livingEvaluator)+i+5+relative;
        if (candidate<0 || candidate>=image.size || !image.contains(static_cast<DWORD>(candidate),192,true)) continue;
        const auto code=image.base+candidate;
        constexpr unsigned char start[] = {0x65,0x48,0x8b,0x04,0x25,0x58,0,0,0,0x4c,0x8b,0xd1};
        if (std::memcmp(code,start,sizeof(start))) continue;
        bool loop=false,array=false,count=false;
        for (unsigned j=0;j+18<=192;++j) {
            const auto q=code+j;
            // 原查询按记录顺序返回首个状态非 3 的对象。
            if (q[0]==0x80 && q[1]==0x78 && q[2]==target.stateOffset && q[3]==3 &&
                q[4]==0x75 && q[5]==11 && q[6]==0x48 && q[7]==0x83 && q[8]==0xc0 &&
                q[9]==target.recordStride && q[10]==0x48 && q[11]==0x3b && q[12]==0xc2 &&
                q[13]==0x75 && static_cast<std::int8_t>(q[14])==-15 &&
                q[15]==0x33 && q[16]==0xc0 && q[17]==0xc3) loop=true;
            DWORD offset=0;
            if (q[0]==0x49 && q[1]==0x03 && q[2]==0x8a) {
                std::memcpy(&offset,q+3,4); if(offset==target.arrayOffset)array=true;
            }
            if (q[0]==0x41 && q[1]==0x39 && q[2]==0x82) {
                std::memcpy(&offset,q+3,4); if(offset==target.arrayOffset+8)count=true;
            }
        }
        if (!loop || !array || !count) continue;
        if (++lookups!=1) { error="multiple living partner lookups"; return false; }
        target.lookupRva=static_cast<DWORD>(candidate);
        target.lookup=reinterpret_cast<LivingPartnerLookup>(image.base+candidate);
    }
    if (lookups!=1) { error="living partner lookup not found"; return false; }
    findPartnerSpawnControl(image,target);
    return true;
}
}

bool FindMarriageCommitTarget(HMODULE module, MarriageCommitTarget& target, const char*& error,
    void* livingEvaluator) {
    target = {};
    Image image;
    if (!image.open(module)) { error = "invalid or unreadable PE64 image"; return false; }
    unsigned matches = 0;
    for (const auto& span : image.spans) {
        if (!span.executable) continue;
        DWORD at = span.begin;
        while (at < span.end && span.end - at >= 19) {
            const auto hit = static_cast<const std::uint8_t*>(
                std::memchr(image.base + at, 0x80, span.end - at - 18));
            if (!hit) break;
            at = static_cast<DWORD>(hit - image.base);
            const auto p = hit;
            ++at;
            // cmp byte ptr [record+state],2; je reject; add record,stride; cmp end; jne loop
            if (p[1] != 0x79 || p[3] != 2 || p[4] != 0x0f || p[5] != 0x84 ||
                p[10] != 0x48 || p[11] != 0x83 || p[12] != 0xc1 ||
                p[14] != 0x48 || p[15] != 0x3b || p[16] != 0xca || p[17] != 0x75 ||
                static_cast<std::int8_t>(p[18]) != -19 || p[13] < 16 || p[13] > 127 ||
                p[2] >= p[13]) continue;
            DWORD begin = 0, length = 0;
            if (!image.functionRange(at - 1, begin, length)) continue;
            const auto code = image.base + begin;
            std::int32_t relativeReject = 0;
            std::memcpy(&relativeReject,p+6,4);
            const std::int64_t reject = static_cast<std::int64_t>(at - 1) + 10 + relativeReject;
            if (reject < begin || reject + 2 > static_cast<std::int64_t>(begin) + length) continue;

            MarriageCommitTarget candidate{};
            candidate.functionRva = begin;
            candidate.function = reinterpret_cast<MarriageEligibility>(image.base + begin);
            candidate.stateOffset = p[2];
            candidate.recordStride = p[13];
            bool selfAlias = false, ownerRead = false, booleanSuccess = false, booleanReject = false;
            unsigned arrays = 0, crossChecks = 0;
            for (DWORD i = 0; i + 3 <= length; ++i) {
                const auto q = code + i;
                if (q[0] == 0x48 && q[1] == 0x8b && q[2] == 0xf1) selfAlias = true;
                if (i + 4 <= length && q[0] == 0x48 && q[1] == 0x8b && q[2] == 0x46 && q[3] == 8)
                    ownerRead = true;
                if (q[0] == 0xb0 && q[1] == 1) booleanSuccess = true;
                // 二维关系数组：外层每个角色一项，内层记录步长由指令恢复。
                if (i + 22 <= length && q[0] == 0x48 && q[1] == 0x8b && q[2] == 0x86 &&
                    q[7] == 0x48 && q[8] == 0x8b && q[9] == 0x0c && q[10] == 0xd8 &&
                    q[11] == 0x8b && q[12] == 0x44 && q[13] == 0xd8 && q[14] == 8 &&
                    q[15] == 0x48 && q[16] == 0x6b && q[17] == 0xd0 &&
                    q[18] == candidate.recordStride && q[19] == 0x48 && q[20] == 0x03 && q[21] == 0xd1) {
                    std::memcpy(&candidate.arrayOffset,q+3,4);
                    ++arrays;
                }
                // 原版还检查：目标已属于其他角色，或当前角色对应关系处于排除状态。
                if (i + 20 <= length && q[0] == 0x0f && q[1] == 0xb6 && q[2] == 0x48 &&
                    q[3] == candidate.stateOffset && q[4] == 0x66 && q[5] == 0x3b && q[6] == 0x78 &&
                    q[8] == 0x74 && q[10] == 0x80 && q[11] == 0xf9 && q[12] == 2 &&
                    q[13] == 0xeb && q[15] == 0x80 && q[16] == 0xf9 && q[17] == 3 && q[18] == 0x74) {
                    candidate.playableOffset = q[7];
                    ++crossChecks;
                }
            }
            for (DWORD rva = static_cast<DWORD>(reject);
                rva + 2 <= begin + length && rva < reject + 64; ++rva) {
                const auto q = image.base + rva;
                if ((q[0] == 0x32 || q[0] == 0x30) && q[1] == 0xc0) booleanReject = true;
            }
            if (!selfAlias || !ownerRead || !booleanSuccess || !booleanReject || arrays != 1 || crossChecks != 1 ||
                candidate.arrayOffset < 16 || candidate.arrayOffset > 4096 ||
                candidate.playableOffset + sizeof(std::uint16_t) > candidate.recordStride ||
                (candidate.stateOffset >= candidate.playableOffset &&
                 candidate.stateOffset < candidate.playableOffset + sizeof(std::uint16_t))) continue;
            bool arrayCount = false;
            for (DWORD i = 0; i + 6 <= length; ++i) {
                const auto q = code + i;
                DWORD displacement = 0;
                if (q[0] == 0x39 && q[1] == 0x86) {
                    std::memcpy(&displacement,q+2,4);
                    if (displacement == candidate.arrayOffset + 8) arrayCount = true;
                }
            }
            if (!arrayCount || !supportsPrivateMarriageView(image,begin,length,candidate)) continue;
            if (matches && target.functionRva == begin) continue;
            if (++matches > 1) { error = "multiple marriage eligibility functions; refusing ambiguous hook"; return false; }
            target = candidate;
        }
    }
    if (matches != 1) { error = "marriage eligibility function or layout not found"; return false; }
    DWORD evaluatorRva=0;
    if (livingEvaluator) {
        if (!image.pointerRva(reinterpret_cast<std::uintptr_t>(livingEvaluator),evaluatorRva)) {
            error="living condition evaluator outside host"; return false;
        }
    } else {
        ConditionTarget condition{};
        if (!FindConditionTarget(module,condition,error,ConditionKind::LivingTogether)) return false;
        evaluatorRva=condition.evaluatorRva;
    }
    if (!findMarriageRouting(image,target,evaluatorRva,error)) return false;
    error = nullptr;
    return true;
}

namespace {
bool codeHas(const Image& image, DWORD begin, DWORD length, const unsigned char* needle, std::size_t size) {
    return length >= size && std::search(image.base + begin, image.base + begin + length, needle, needle + size) != image.base + begin + length;
}
std::vector<std::pair<DWORD, DWORD>> callsIn(const Image& image, DWORD begin, DWORD length,bool stopAtReturn=true) {
    std::vector<std::pair<DWORD, DWORD>> calls;
    for (DWORD at = 0; at < length;) {
        hde64s ins{}; unsigned char bytes[16]{};
        std::memcpy(bytes, image.base + begin + at, (std::min)(DWORD{15}, length - at));
        const auto size = hde64_disasm(bytes, &ins);
        if (!size || ins.flags & F_ERROR || size > length - at) break;
        if (ins.opcode == 0xe8) {
            const auto dest = static_cast<std::int64_t>(begin) + at + size + static_cast<std::int32_t>(ins.imm.imm32);
            if (dest > 0 && dest < image.size && image.contains(static_cast<DWORD>(dest), 1, true))
                calls.push_back({begin + at, static_cast<DWORD>(dest)});
        }
        if (ins.opcode == 0xc3 && stopAtReturn) break; // 后面的跳转表不是指令。
        at += size;
    }
    return calls;
}
// 请求类型的主虚表由 RTTI 识别；这里只接受已验证的三参数请求处理方法布局。
bool requestHandler(const Image& image, const char* name, NativeRequestHandler& handler,DWORD* vtable=nullptr) {
    const auto size = std::strlen(name) + 1;
    std::vector<DWORD> types, cols;
    for (const auto& span : image.spans) {
        auto at = span.begin;
        while (at < span.end && size <= span.end - at) {
            auto hit = static_cast<const unsigned char*>(std::memchr(image.base + at, '.', span.end - at - size + 1));
            if (!hit) break; at = static_cast<DWORD>(hit - image.base) + 1;
            if (at > 16 && !std::memcmp(hit, name, size)) types.push_back(at - 17);
        }
    }
    for (const auto& span : image.spans) for (std::uint64_t at = (span.begin + 3ull) & ~3ull; at + sizeof(Locator) <= span.end; at += 4) {
        Locator col{}; std::memcpy(&col, image.base + at, sizeof(col));
        if (col.signature == 1 && !col.offset && !col.constructorOffset && col.self == at &&
            std::find(types.begin(), types.end(), col.type) != types.end()) cols.push_back(static_cast<DWORD>(at));
    }
    unsigned matches = 0;
    for (const auto& span : image.spans) for (std::uint64_t at = (span.begin + 7ull) & ~7ull; at + 32 <= span.end; at += 8) {
        std::uintptr_t ptr = 0, method = 0; DWORD colRva = 0, rva = 0, length = 0;
        std::memcpy(&ptr, image.base + at, 8);
        if (!image.pointerRva(ptr, colRva) || std::find(cols.begin(), cols.end(), colRva) == cols.end()) continue;
        if (!image.read(static_cast<DWORD>(at + 24), method) || !image.pointerRva(method, rva) || !image.functionSize(rva, length)) continue;
        // 请求上下文的包地址、长度，以及包头长度校验均须出现。
        bool payload = false, sizeCheck = false;
        for (DWORD i = 0; i + 4 <= (std::min)(length, DWORD{128}); ++i) {
            const auto q = image.base + rva + i;
            if ((q[0] == 0x49 || q[0] == 0x4d) && q[1] == 0x8b && (q[2] & 0xc7) == 0x40 && q[3] == 0x18) payload = true;
            if (i + 4 <= length && q[0] == 0x48 && q[1] == 0x83 && (q[2] & 0xf8) == 0xe8 && q[3] == 5) sizeCheck = true;
        }
        if (!payload || !sizeCheck) continue;
        handler = reinterpret_cast<NativeRequestHandler>(method);if(vtable)*vtable=static_cast<DWORD>(at+8);++matches;
    }
    return matches == 1;
}
}

bool FindPartnerRuntimeTarget(HMODULE module, MarriageCommitTarget& target, const char*& error) {
    Image image; auto& rt = target.runtime; rt = {};
    if (!image.open(module) || !target.spawnControl) { error = "native spawn controller unavailable"; return false; }
    DWORD length = 0;
    if (!image.functionSize(target.spawnRva, length)) return false;
    unsigned registries = 0, creators = 0, contents = 0;
    for (DWORD i = 0; i + 16 <= length; ++i) {
        const auto q = image.base + target.spawnRva + i;
        if (q[0] == 0x48 && q[1] == 0x8b && q[2] == 0x0d && q[7] == 0x48 && q[8] == 0x8b && q[9] == 0x49 && q[11] == 0xe8) {
            std::int32_t relative = 0, call = 0; std::memcpy(&relative, q + 3, 4); std::memcpy(&call, q + 12, 4);
            const auto slot = static_cast<std::int64_t>(target.spawnRva) + i + 7 + relative;
            const auto lookup = static_cast<std::int64_t>(target.spawnRva) + i + 16 + call;
            DWORD lookupLength = 0;
            if (slot < 0 || slot >= image.size || !image.contains(static_cast<DWORD>(slot), 8) || lookup < 0 || lookup >= image.size ||
                !image.functionSize(static_cast<DWORD>(lookup), lookupLength)) continue;
            const auto bytes = image.base + lookup;
            constexpr unsigned char mask[] = {0x41,0x81,0xe0,0xff,0xff,0x0f,0};
            if (!codeHas(image, static_cast<DWORD>(lookup), lookupLength, mask, sizeof(mask))) continue;
            unsigned fields = 0;
            for (DWORD j = 0; j + 7 <= lookupLength; ++j) {
                const auto p = bytes + j;
                if (p[0] == 0x8b && p[1] == 0x8e) { std::memcpy(&rt.bucketCountOffset, p + 2, 4); ++fields; }
                if (p[0] == 0x4c && p[1] == 0x03 && p[2] == 0x96) { std::memcpy(&rt.bucketsOffset, p + 3, 4); ++fields; }
                if (p[0] == 0x48 && p[1] == 0x8b && p[2] == 0x86) { std::memcpy(&rt.entriesOffset, p + 3, 4); ++fields; }
            }
            if (fields != 3 || rt.bucketCountOffset > 4096 || rt.bucketsOffset > 4096 || rt.entriesOffset > 4096) continue;
            rt.worldSlot = reinterpret_cast<void**>(image.base + slot); rt.managerOffset = q[10];
            ++registries;
        }
        if (q[0] == 0x48 && q[1] == 0x8b && q[2] == 0x89) {
            unsigned offset = 0; std::memcpy(&offset, q + 3, 4);
            if (offset == target.arrayOffset) continue;
            // 控制器早期从 owner 的组件表取回公共关系组件。
            if (i < 96 && offset > 0 && offset < 4096) { rt.contentsComponentOffset = offset; ++contents; }
        }
        if (!rt.unavailableResult || rt.unavailableResult == MAXDWORD) if (q[0] == 0x8b && q[1] == 5) {
            std::int32_t rel = 0; std::memcpy(&rel, q + 2, 4);
            const auto value = static_cast<std::int64_t>(target.spawnRva) + i + 6 + rel;
            if (value > 0 && value < image.size) image.read(static_cast<DWORD>(value), rt.unavailableResult);
        }
    }
    for (const auto& call : callsIn(image, target.spawnRva, length)) {
        DWORD size = 0; if (!image.functionSize(call.second, size)) continue;
        const auto code = image.base + call.second;
        constexpr unsigned char head[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x4c,0x89,0x48,0x20,0x48,0x89,0x50,0x10};
        if (size < sizeof(head) || std::memcmp(code, head, sizeof(head))) continue;
        unsigned ownerFields = 0;
        for (DWORD j = 0; j + 20 <= size; ++j) {
            const auto p = code + j;
            if (p[0] == 0x66 && p[1] == 0x89 && p[2] == 0xb1 && p[7] == 0x48 && p[8] == 0x8b &&
                p[9] == 0x45 && p[11] == 0x8b && p[12] == 0x40 && p[14] == 0x89 && p[15] == 0x81) {
                std::memcpy(&rt.companionPlayableOffset, p + 3, 4); rt.actorIdOffset = p[13];
                std::memcpy(&rt.companionOwnerOffset, p + 16, 4); ++ownerFields;
            }
        }
        if (ownerFields != 1 || rt.companionOwnerOffset > 4096 || rt.companionPlayableOffset > 4096 || !rt.actorIdOffset) continue;
        rt.create = reinterpret_cast<NativePartnerCreate>(image.base + call.second); ++creators;
    }
    if (registries != 1 || creators != 1 || contents != 1) { error = "actor registry or creation layout incompatible"; rt = {}; return false; }
    // 某些错误标识由游戏启动时初始化，离线映像里可能仍为 0；拒绝路径不能误报成功。
    if(!rt.unavailableResult)rt.unavailableResult=MAXDWORD;

    // 先找到按键离婚的真实写入函数，再以调用关系定位公共离婚入口，避免固定 RVA。
    DWORD byKey = 0; unsigned byKeys = 0, divorces = 0;
    constexpr unsigned char divorced[] = {0xc6,0x43,0x2c,0x03};
    for (const auto& span : image.spans) if (span.executable) {
        for (DWORD at = span.begin; at + sizeof(divorced) <= span.end; ++at) {
            if (std::memcmp(image.base + at, divorced, sizeof(divorced))) continue;
            DWORD begin = 0, size = 0; if (!image.functionRange(at, begin, size)) continue;
            constexpr unsigned char args[] = {0x4c,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08};
            if (size < sizeof(args) || std::memcmp(image.base + begin, args, sizeof(args))) continue;
            if (byKey != begin) { byKey = begin; ++byKeys; }
        }
    }
    if (byKeys != 1) { error = "keyed divorce writer not unique"; return false; }
    for (const auto& span : image.spans) if (span.executable) {
        auto at = span.begin;
        while (at + 5 <= span.end) {
            auto hit = static_cast<const unsigned char*>(std::memchr(image.base + at, 0xe8, span.end - at - 4));
            if (!hit) break; at = static_cast<DWORD>(hit - image.base) + 1;
            std::int32_t rel = 0; std::memcpy(&rel, hit + 1, 4);
            if (static_cast<std::int64_t>(at) + 4 + rel != target.lookupRva) continue;
            DWORD begin = 0, size = 0; if (!image.functionRange(at - 1, begin, size)) continue;
            const auto calls = callsIn(image, begin, size);
            const auto keyedCall=std::find_if(calls.begin(),calls.end(),[&](const auto& call){return call.second==byKey;});
            if(keyedCall==calls.end())continue;
            DWORD auditEnd=0;
            constexpr unsigned char reuseR14[]={0x41,0xbe,1,0,0,0};
            for(DWORD j=keyedCall->first+5-begin;j+sizeof(reuseR14)<=size;++j)
                if(!std::memcmp(image.base+begin+j,reuseR14,sizeof(reuseR14))){auditEnd=j;break;}
            if(!auditEnd)continue;
            DWORD successAt=0;
            const DWORD callReturn=keyedCall->first+5-begin;
            for(DWORD j=callReturn;j+8<size && j<callReturn+24;++j) {
                const auto p=image.base+begin+j;
                if(p[0]!=0x85 || p[1]!=0xc0)continue;
                std::int64_t destination=-1;
                if(p[2]==0x74)destination=static_cast<std::int64_t>(j)+4+static_cast<std::int8_t>(p[3]);
                else if(p[2]==0x0f && p[3]==0x84){std::int32_t branch=0;std::memcpy(&branch,p+4,4);destination=static_cast<std::int64_t>(j)+8+branch;}
                if(destination>callReturn && destination<auditEnd){successAt=static_cast<DWORD>(destination);break;}
            }
            if(!successAt)continue;
            // R14 保存的查询记录在删除后仍用于通知。只允许对该记录读取的函数族。
            DWORD auditBegin=0;
            constexpr unsigned char readKey[]={0x41,0x8b,0x06};
            for(DWORD j=at+4-begin;j+sizeof(readKey)<=keyedCall->first-begin;++j)
                if(!std::memcmp(image.base+begin+j,readKey,sizeof(readKey))){auditBegin=j;break;}
            if(!auditBegin)continue;
            bool readonly = true;
            for (DWORD j = auditBegin; j < auditEnd;) {
                // 失败分支会把 R14 改作原生 Actor 引用并退出，不再读取关系记录。
                // 分别审核删除前及成功通知分支，不能把失败分支的寄存器复用当成写关系。
                if(j==callReturn){j=successAt;continue;}
                hde64s ins{}; unsigned char bytes[16]{}; std::memcpy(bytes, image.base + begin + j, (std::min)(DWORD{15}, size - j));
                auto n = hde64_disasm(bytes, &ins);
                if (!n || (ins.flags & F_ERROR) || n > size - j) { readonly = false; break; }
                if(ins.flags&F_MODRM) {
                    const unsigned reg=ins.modrm_reg+(ins.rex_r?8u:0u),rm=ins.modrm_rm+(ins.rex_b?8u:0u);
                    if(ins.modrm_mod==3 && (reg==14 || rm==14) && (ins.opcode==0x8b || ins.opcode==0x89))readonly=false;
                    if(ins.modrm_mod!=3 && rm==14) {
                        const unsigned width=ins.opcode==0x0f && ins.opcode2==0xb6 ? 1u :
                            (ins.opcode==0x0f && ins.opcode2==0xb7 ? 2u : (ins.rex_w?8u:4u));
                        std::int64_t offset=0;
                        if(ins.flags&F_DISP8)offset=static_cast<std::int8_t>(ins.disp.disp8);
                        else if(ins.flags&F_DISP32)offset=static_cast<std::int32_t>(ins.disp.disp32);
                        if((ins.opcode!=0x8b && !(ins.opcode==0x0f && (ins.opcode2==0xb6 || ins.opcode2==0xb7))) ||
                            offset<0 || offset+width>target.recordStride)readonly=false;
                    }
                }
                if (ins.opcode == 0xc3) break; j += n;
            }
            if (!readonly) continue;
            if (rt.divorceRva == begin) continue;
            rt.divorceRva = begin; rt.divorceCurrent = image.base + begin; rt.divorceLookupReturn = image.base + at + 4; ++divorces;
        }
    }
    rt.divorceByKey = reinterpret_cast<NativeDivorceByKey>(image.base + byKey);
    // 只解析离婚入口传给伴侣查询的角色索引读取，不把 NPC 模板等其他 WORD 字段混入。
    unsigned indexFields = 0; DWORD returnRva=0;
    if(image.pointerRva(reinterpret_cast<std::uintptr_t>(rt.divorceLookupReturn),returnRva) && returnRva>=32)
        for(DWORD i=returnRva-32;i+4<=returnRva-5;++i) {
            const auto q=image.base+i;
            if(q[0]==0x0f && q[1]==0xb7 && q[2]==0x52 && q[3]<0x80){rt.playableIndexOffset=q[3];++indexFields;}
        }
    if (divorces != 1) { error = "current-partner divorce caller not unique or not read-only"; return false; }
    if (indexFields!=1 || !rt.playableIndexOffset) { error = "player index field not unique"; return false; }
    struct Request { const char* name; NativeRequestHandler* destination; const char* reason; };
    const Request requests[]={
        {".?AVTrocTrInteractionDoStepReq@pa@@",&rt.stepRequest,"interaction step request incompatible"},
        {".?AVTrocTrInteractionResetReq@pa@@",&rt.resetRequest,"interaction reset request incompatible"},
        {".?AVTrocTrQuestDialogEndReq@pa@@",&rt.dialogueEndRequest,"dialogue end request incompatible"}
    };
    for(const auto& request:requests)if(!requestHandler(image,request.name,*request.destination)){error=request.reason;return false;}
    error = nullptr; return true;
}

namespace {
// 恢复入口为 (组件, DWORD* 结果)。当前函数族把结果指针保存在 R14，
// 这里只接受 DWORD 写入与最终返回，不能把未知大小的结果结构放进栈上。
bool restoreOutputIsDword(const Image& image, DWORD begin, DWORD length) {
    bool saved = false, returned = false;
    unsigned saves = 0;
    for (DWORD at = 0; at < length;) {
        unsigned char bytes[16]{};
        std::memcpy(bytes, image.base + begin + at, (std::min)(DWORD{15}, length - at));
        hde64s ins{}; const auto count = hde64_disasm(bytes, &ins);
        if (!count || (ins.flags & F_ERROR) || count > length - at) return false;
        const unsigned reg = ins.modrm_reg + (ins.rex_r ? 8u : 0u);
        const unsigned rm = ins.modrm_rm + (ins.rex_b ? 8u : 0u);
        const bool modrm = (ins.flags & F_MODRM) != 0;
        if (modrm && ins.modrm_mod == 3 && ins.opcode == 0x8b && ins.rex_w && reg == 14 && rm == 2) {
            if (++saves != 1) return false;
            saved = true; at += count; continue;
        }
        if (!saved) {
            if (ins.opcode == 0xe8 || ins.opcode == 0xe9 || ins.opcode == 0xeb || ins.opcode == 0xff) return false;
            if (modrm && ins.modrm_mod != 3 && ins.opcode == 0x89 && reg == 2) return false;
        } else {
            if (ins.opcode >= 0xa4 && ins.opcode <= 0xaf) return false;
            if (modrm && ins.modrm_mod == 3 && (reg == 14 || rm == 14)) {
                if (ins.opcode == 0x8b && ins.rex_w && reg == 0 && rm == 14 && at + 96 >= length) returned = true;
                else return false;
            }
            if (modrm && ins.modrm_mod != 3) {
                const bool sib = (ins.flags & F_SIB) != 0;
                const unsigned base = sib ? ins.sib_base + (ins.rex_b ? 8u : 0u) : rm;
                if (reg == 14 && !(returned && ins.opcode == 0x8b && base == 11)) return false;
                if (sib && ins.sib_index + (ins.rex_x ? 8u : 0u) == 14) return false;
                if (base == 14) {
                    std::int64_t offset = 0;
                    if (ins.flags & F_DISP8) offset = static_cast<std::int8_t>(ins.disp.disp8);
                    else if (ins.flags & F_DISP32) offset = static_cast<std::int32_t>(ins.disp.disp32);
                    if (sib || ins.p_seg || ins.p_67 || ins.rex_w || ins.opcode != 0x89 || offset != 0) return false;
                }
            }
            if (returned && (ins.opcode == 0xe8 || ins.opcode == 0xff)) return false;
        }
        if (ins.opcode == 0xc3) return saved && returned && saves == 1;
        at += count;
    }
    return false;
}
}

bool FindPartnerRestoreTarget(HMODULE module, MarriageCommitTarget& target, const char*& error) {
    auto& rt = target.runtime; rt.restore = nullptr; rt.restoreRva = 0;
    Image image; DWORD createRva = 0;
    if (!image.open(module) || !target.lookup || !rt.create ||
        !image.pointerRva(reinterpret_cast<std::uintptr_t>(rt.create), createRva)) {
        error = "partner restore dependencies unavailable"; return false;
    }
    // 从已验证的创建入口反查直接调用者，排除手动召唤控制器。
    // 恢复分支必须同时查询当前伴侣、读取保存的同行标记，并把该标记传入创建函数。
    // 不硬编码入口 RVA 或存档字段偏移；寄存器/调用约定变化时停止这一项扩展。
    std::vector<DWORD> visited;
    unsigned matches = 0;
    for (const auto& span : image.spans) {
        if (!span.executable) continue;
        DWORD at = span.begin;
        while (at < span.end && span.end - at >= 5) {
            const auto hit = static_cast<const unsigned char*>(std::memchr(image.base + at, 0xe8, span.end - at - 4));
            if (!hit) break;
            at = static_cast<DWORD>(hit - image.base) + 1;
            std::int32_t relative = 0; std::memcpy(&relative, hit + 1, 4);
            if (static_cast<std::int64_t>(at) + 4 + relative != createRva) continue;
            DWORD begin = 0, size = 0;
            if (!image.functionRange(at - 1, begin, size) || begin == target.spawnRva ||
                std::find(visited.begin(), visited.end(), begin) != visited.end()) continue;
            visited.push_back(begin);
            const auto calls = callsIn(image, begin, size);
            if (std::count_if(calls.begin(), calls.end(), [&](const auto& c) { return c.second == target.lookupRva; }) != 1 ||
                std::count_if(calls.begin(), calls.end(), [&](const auto& c) { return c.second == createRva; }) != 1) continue;
            const DWORD lookupCall = std::find_if(calls.begin(), calls.end(), [&](const auto& c) { return c.second == target.lookupRva; })->first;
            const DWORD createCall = std::find_if(calls.begin(), calls.end(), [&](const auto& c) { return c.second == createRva; })->first;
            if (lookupCall >= createCall) continue;
            constexpr unsigned char args[] = {0x4c,0x8b,0xf2,0x4c,0x8b,0xe9,0x33,0xf6,0x48,0x8b,0x59,0x08};
            constexpr unsigned char flagRead[] = {0x44,0x0f,0xb6,0x7b};
            constexpr unsigned char flagTest[] = {0x45,0x84,0xff};
            constexpr unsigned char partyArg[] = {0x44,0x88,0x7c,0x24,0x28};
            unsigned flagReads = 0, flagTests = 0, partyArgs = 0;
            bool skipsCreation = false;
            for (DWORD i = 0; i < size;) {
                unsigned char bytes[16]{}; hde64s ins{};
                std::memcpy(bytes, image.base + begin + i, (std::min)(DWORD{15}, size - i));
                const auto n = hde64_disasm(bytes, &ins);
                if (!n || (ins.flags & F_ERROR) || n > size - i) break;
                const DWORD site = begin + i;
                if (site > lookupCall && site < createCall) {
                    if (n == 5 && !std::memcmp(bytes, flagRead, sizeof(flagRead)) && bytes[4] < 128) ++flagReads;
                    if (flagReads == 1 && n == sizeof(flagTest) && !std::memcmp(bytes, flagTest, sizeof(flagTest))) {
                        ++flagTests;
                        // 保存标记为 false 时，JE 必须跳过创建调用，而非只影响其他逻辑。
                        const DWORD next = i + n;
                        std::int64_t destination = -1;
                        if (next + 2 <= size && image.base[begin + next] == 0x74)
                            destination = static_cast<std::int64_t>(begin) + next + 2 + static_cast<std::int8_t>(image.base[begin + next + 1]);
                        else if (next + 6 <= size && image.base[begin + next] == 0x0f && image.base[begin + next + 1] == 0x84) {
                            std::int32_t rel = 0; std::memcpy(&rel, image.base + begin + next + 2, 4);
                            destination = static_cast<std::int64_t>(begin) + next + 6 + rel;
                        }
                        skipsCreation = destination >= createCall + 5 && destination < static_cast<std::int64_t>(begin) + size;
                    }
                    if (flagTests == 1 && n == sizeof(partyArg) && !std::memcmp(bytes, partyArg, sizeof(partyArg)) &&
                        createCall - site <= 64) ++partyArgs;
                }
                if (ins.opcode == 0xc3) break;
                i += n;
            }
            if (!codeHas(image, begin, (std::min)(size, DWORD{64}), args, sizeof(args)) ||
                flagReads != 1 || flagTests != 1 || partyArgs != 1 || !skipsCreation ||
                !restoreOutputIsDword(image, begin, size)) continue;
            if (++matches > 1) {
                rt.restore = nullptr; rt.restoreRva = 0;
                error = "partner restore caller ambiguous"; return false;
            }
            rt.restore = reinterpret_cast<NativePartnerRestore>(image.base + begin); rt.restoreRva = begin;
        }
    }
    if (matches != 1) { error = "partner restore caller or saved-party contract incompatible"; return false; }
    error = nullptr; return true;
}

bool FindBeautyPartnerLookup(HMODULE module, MarriageCommitTarget& target) {
    target.beautyLookupReturn=nullptr;
    Image image;
    if (!image.open(module) || !target.lookupRva) return false;
    constexpr char name[]="BeautyLivingTogether";
    std::vector<DWORD> strings;
    for (const auto& span:image.spans) {
        for(DWORD at=span.begin;at+sizeof(name)<=span.end;++at)
            if(image.base[at]=='B' && !std::memcmp(image.base+at,name,sizeof(name))) strings.push_back(at);
    }
    unsigned matches=0;
    for(const auto& span:image.spans) if(span.executable) for(DWORD at=span.begin;at+7<=span.end;++at) {
        const auto q=image.base+at;
        if(q[0]!=0x48 || q[1]!=0x8d || q[2]!=0x15)continue;
        std::int32_t rel=0;std::memcpy(&rel,q+3,4);
        const auto string=static_cast<std::int64_t>(at)+7+rel;
        if(std::find(strings.begin(),strings.end(),string)==strings.end())continue;
        DWORD begin=0,length=0;
        if(!image.functionRange(at,begin,length))continue;
        for(const auto& call:callsIn(image,begin,length)) if(call.second==target.lookupRva) {
            target.beautyLookupReturn=image.base+call.first+5;++matches;
        }
    }
    if(matches==1)return true;
    target.beautyLookupReturn=nullptr;return false;
}

bool FindBeautyUiTarget(HMODULE module,const MarriageCommitTarget& marriage,BeautyUiTarget& target) {
    target={};Image image;DWORD ret=0,begin=0,length=0;
    if(!image.open(module) || !image.pointerRva(reinterpret_cast<std::uintptr_t>(marriage.beautyLookupReturn),ret) ||
        !image.functionRange(ret,begin,length))return false;
    target.entry=reinterpret_cast<NativeBeautyEntry>(image.base+begin);
    for(DWORD i=0;i+8<=length;++i) {
        const auto q=image.base+begin+i;
        if(q[0]==0x41 && q[1]==0xc6 && q[2]==0x86 && q[7]==1) {
            unsigned offset=0;std::memcpy(&offset,q+3,4);
            if(offset>0x100 && offset<4096)target.modeOffset=offset;
        }
    }
    unsigned worlds=0,players=0,playerWorldManager=0;
    void** playerWorld=nullptr;
    for(const auto& call:callsIn(image,begin,length)) {
        DWORD size=0;
        if(!image.functionSize(call.second,size) || size>128)continue;
        for(DWORD j=0;j+16<=size;++j) {
            const auto q=image.base+call.second+j;
            if(q[0]!=0x48 || q[1]!=0x8b || q[2]!=0x0d || q[7]!=0x48 || q[8]!=0x8b || q[9]!=0x49 || q[11]!=0xe8)continue;
            std::int32_t rel=0;std::memcpy(&rel,q+12,4);
            const auto helper=static_cast<std::int64_t>(call.second)+j+16+rel;
            DWORD hs=0;
            if(helper<=0 || helper>=image.size || !image.functionSize(static_cast<DWORD>(helper),hs))continue;
            std::memcpy(&rel,q+3,4);
            const auto slot=static_cast<std::int64_t>(call.second)+j+7+rel;
            if(slot<=0 || slot>=image.size || !image.contains(static_cast<DWORD>(slot),8))continue;
            auto* ptr=reinterpret_cast<void**>(image.base+slot);
            // 原生美容还调用主玩家 getter：直接读取 manager 字段，验证对象后写入返回包装。
            constexpr unsigned char playerResult[]={0x48,0x89,0x7b,8};
            constexpr unsigned char validateObject[]={0xff,0x90,0xc0,0,0,0};
            if(hs<=256 && codeHas(image,static_cast<DWORD>(helper),hs,playerResult,sizeof(playerResult)) &&
                codeHas(image,static_cast<DWORD>(helper),hs,validateObject,sizeof(validateObject))) {
                unsigned offset=0,fields=0;
                for(DWORD k=0;k+7<=hs;++k) {
                    const auto h=image.base+helper+k;
                    if(h[0]==0x48 && h[1]==0x8b && h[2]==0x79 && h[3] &&
                        h[4]==0x48 && h[5]==0x85 && h[6]==0xff){offset=h[3];++fields;}
                }
                if(fields==1) {
                    if(!playerWorld){playerWorld=ptr;playerWorldManager=q[10];target.managerPlayer=offset;++players;}
                    else if(playerWorld!=ptr || playerWorldManager!=q[10] || target.managerPlayer!=offset)++players;
                }
            }
            unsigned session=0,actor=0;
            for(DWORD k=0;k+7<=hs;++k) {
                const auto h=image.base+helper+k;
                if(h[0]==0x48 && h[1]==0x8b && h[2]==0x79)session=h[3];
                if(h[0]==0x4c && h[1]==0x8b && h[2]==0xb0)std::memcpy(&actor,h+3,4);
            }
            if(!session || !actor || actor>4096)continue;
            if(!target.clientWorld) {target.clientWorld=ptr;target.worldManager=q[10];target.managerSession=session;target.sessionActor=actor;++worlds;}
            else if(target.clientWorld!=ptr || target.sessionActor!=actor || target.managerSession!=session)++worlds;
        }
    }
    // 从原生 getCharacterName 绑定恢复名字管理器和本地化文本字段。
    constexpr char name[]="getCharacterName";
    std::vector<DWORD> strings;
    for(const auto& span:image.spans)for(DWORD at=span.begin;at+sizeof(name)<=span.end;++at)
        if(image.base[at]=='g' && !std::memcmp(image.base+at,name,sizeof(name)))strings.push_back(at);
    for(const auto& span:image.spans)if(span.executable)for(DWORD at=span.begin+96;at+7<=span.end;++at) {
        const auto q=image.base+at;
        if(q[0]!=0x4c || q[1]!=0x8d || q[2]!=5)continue;
        std::int32_t rel=0;std::memcpy(&rel,q+3,4);
        if(std::find(strings.begin(),strings.end(),static_cast<std::int64_t>(at)+7+rel)==strings.end())continue;
        for(DWORD j=at-96;j+7<=at;++j) {
            const auto p=image.base+j;
            if(p[0]!=0x48 || p[1]!=0x8d || p[2]!=0x15)continue;
            std::memcpy(&rel,p+3,4);const auto method=static_cast<std::int64_t>(j)+7+rel;
            DWORD ms=0;if(method<=0 || method>=image.size || !image.functionSize(static_cast<DWORD>(method),ms))continue;
            constexpr unsigned char textField[]={0x48,0x8b,0x88,0x10,1,0,0};
            if(!codeHas(image,static_cast<DWORD>(method),ms,textField,sizeof(textField)))continue;
            for(DWORD k=0;k+7<=(std::min)(ms,DWORD{128});++k) {
                const auto m=image.base+method+k;
                if(m[0]!=0x4c || m[1]!=0x8b || m[2]!=0x1d)continue;
                std::memcpy(&rel,m+3,4);const auto slot=method+k+7+rel;
                if(slot>0 && slot<image.size && image.contains(static_cast<DWORD>(slot),8)) {
                    target.characters=reinterpret_cast<void**>(image.base+slot);target.nameOffset=0x110;
                }
            }
        }
    }
    return target.modeOffset && worlds==1 && players==1 && target.clientWorld &&
        playerWorld==target.clientWorld && playerWorldManager==target.worldManager && target.managerPlayer!=target.managerSession;
}

bool FindPartnerNameTarget(HMODULE module,const MarriageCommitTarget& marriage,PartnerNameTarget& target,const char** error) {
    target={};Image image;NativeRequestHandler ack=nullptr;
    if(error)*error="native character-name acknowledgement not uniquely verified";
    if(!image.open(module) || !requestHandler(image,".?AVTrocTrChangeCharacterNameAck@pa@@",ack))return false;
    DWORD begin=0,size=0;
    if(!image.pointerRva(reinterpret_cast<std::uintptr_t>(ack),begin) || !image.functionSize(begin,size))return false;
    PartnerNameTarget found{};unsigned assignments=0,worlds=0,registries=0;
    std::vector<DWORD> seenRegistries;
    // 回执从角色组件表取得状态组件，再把 UTF-16 名字交给原生字符串赋值函数。
    for(DWORD i=0;i+15<=size;++i) {
        const auto p=image.base+begin+i;
        if(p[0]==0x48 && p[1]==0x8d && p[2]==0x8b && p[7]==0x49 && p[8]==0x8b && p[9]==0xd7 && p[10]==0xe8) {
            unsigned offset=0;std::int32_t rel=0;
            std::memcpy(&offset,p+3,4);std::memcpy(&rel,p+11,4);
            const auto dest=static_cast<std::int64_t>(begin)+i+15+rel;
            constexpr BYTE assignHead[]={0x40,0x53,0x55,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0x31,0x48,0x8b,0xea,0x48,0x8b,0xd9};
            if(offset<16 || offset>4096 || dest<=0 || dest>=image.size ||
                !image.contains(static_cast<DWORD>(dest),sizeof(assignHead),true) ||
                std::memcmp(image.base+dest,assignHead,sizeof(assignHead)))continue;
            // 同一回执的实例路径必须与既有 NPC 状态组件布局一致。
            constexpr BYTE statusLoad[]={0x48,0x8b,0x40,0x68,0x48,0x8b,0x58};
            if(i<48 || !codeHas(image,begin+i-48,48,statusLoad,sizeof(statusLoad)))continue;
            const auto load=std::search(image.base+begin+i-48,image.base+begin+i,statusLoad,statusLoad+sizeof(statusLoad));
            if(load+sizeof(statusLoad)>=image.base+begin+i || load[sizeof(statusLoad)]!=marriage.npcKeyOffsets[1])continue;
            found.assign=reinterpret_cast<NativePartnerNameAssign>(image.base+dest);
            found.nameOffset=offset;found.statusOffset=marriage.npcKeyOffsets[1];++assignments;
        }
        if(p[0]==0x48 && p[1]==0x8b && p[2]==5 && p[7]==0x4c && p[8]==0x8b && p[9]==0x60) {
            std::int32_t rel=0;std::memcpy(&rel,p+3,4);
            const auto slot=static_cast<std::int64_t>(begin)+i+7+rel;
            if(slot>0 && slot<image.size && image.contains(static_cast<DWORD>(slot),8)) {
                found.clientWorld=reinterpret_cast<void**>(image.base+slot);found.managerOffset=p[10];++worlds;
            }
        }
    }
    // 由回执的实例查找链恢复客户端注册表；完整 ActorId 查找不能使用服务端的掩码键。
    for(const auto& a:callsIn(image,begin,size)) {
        DWORD as=0;if(!image.functionSize(a.second,as) || as>128)continue;
        for(const auto& b:callsIn(image,a.second,as)) {
            DWORD bs=0;if(!image.functionSize(b.second,bs) || bs>1024)continue;
            for(const auto& c:callsIn(image,b.second,bs)) {
                if(std::find(seenRegistries.begin(),seenRegistries.end(),c.second)!=seenRegistries.end())continue;
                DWORD cs=0;if(!image.functionSize(c.second,cs) || cs>256)continue;
                constexpr BYTE fullId[]={0x44,0x8b,0xc2};
                constexpr BYTE entryCheck[]={0x45,0x39,0x41,4};
                if(!codeHas(image,c.second,cs,fullId,sizeof(fullId)) || !codeHas(image,c.second,cs,entryCheck,sizeof(entryCheck)))continue;
                unsigned count=0,buckets=0,entries=0;
                const auto code=image.base+c.second;
                for(DWORD j=0;j+7<=cs;++j) {
                    const auto q=code+j;
                    if(q[0]==0x8b && q[1]==0x89)std::memcpy(&count,q+2,4);
                    if(q[0]==0x4d && q[1]==3 && q[2]==0x9a)std::memcpy(&buckets,q+3,4);
                    if(q[0]==0x49 && q[1]==0x8b && q[2]==0x82)std::memcpy(&entries,q+3,4);
                }
                // 调用者以 manager+8 为注册表对象；同时核对参数保持和原生表查找调用。
                const auto before=c.first-b.second;
                if(before<6 || std::memcmp(image.base+c.first-6,"\x8b\xd7\x48\x8b\x4d",5))continue;
                const unsigned registry=image.base[c.first-1];
                if(!registry || !count || !buckets || !entries || count>4096 || buckets>4096 || entries>4096)continue;
                found.registryOffset=registry;found.bucketCountOffset=count;
                found.bucketsOffset=buckets;found.entriesOffset=entries;seenRegistries.push_back(c.second);++registries;
            }
        }
    }
    if(assignments!=1){if(error)*error="native UTF-16 name assignment ABI not uniquely verified";return false;}
    if(worlds!=1){if(error)*error="native client actor manager not uniquely verified";return false;}
    if(registries!=1){if(error)*error=registries?"native client actor registry ambiguous":"native client actor registry not verified";return false;}
    if(error)*error=nullptr;
    target=found;return true;
}

bool FindPartnerSummonTarget(HMODULE module,const MarriageCommitTarget& marriage,PartnerSummonTarget& target,const char*& error) {
    target={};Image image;NativeRequestHandler handler=nullptr;DWORD vtable=0;
    error="native summon request ABI not verified";
    if(!image.open(module) || !marriage.lookupRva || !marriage.spawnRva ||
        !requestHandler(image,".?AVTrocTrSpawnOrUnspawnLivingTogetherNpcReq@pa@@",handler,&vtable))return false;
    DWORD packet=0;unsigned descriptors=0;
    for(const auto& span:image.spans)if(span.executable)for(DWORD at=span.begin;at+14<=span.end;++at) {
        const auto p=image.base+at;
        if(p[0]!=0x48 || p[1]!=0x8d || p[2]!=5 || p[7]!=0x48 || p[8]!=0x89 || p[9]!=5)continue;
        std::int32_t relative=0;std::memcpy(&relative,p+3,4);
        if(static_cast<std::int64_t>(at)+7+relative!=vtable)continue;
        std::memcpy(&relative,p+10,4);const auto metadata=static_cast<std::int64_t>(at)+14+relative;
        DWORD identifier=0;
        if(metadata>0 && metadata+16<image.size && image.read(static_cast<DWORD>(metadata)+12,identifier) && identifier && identifier<0xffff) {
            packet=identifier;++descriptors;
        }
    }
    if(descriptors!=1){error="native summon packet descriptor not unique";return false;}
    std::vector<DWORD> dispatchRoots;
    for(const auto& span:image.spans)if(span.executable)for(DWORD at=span.begin;at+5<=span.end;++at) {
        if(image.base[at]!=0xe8)continue;std::int32_t relative=0;std::memcpy(&relative,image.base+at+1,4);
        if(static_cast<std::int64_t>(at)+5+relative!=marriage.spawnRva)continue;
        DWORD root=0;if(image.functionRoot(at,root))dispatchRoots.push_back(root);
    }
    DWORD handlerRva=0,handlerSize=0;
    if(!image.pointerRva(reinterpret_cast<std::uintptr_t>(handler),handlerRva) || !image.functionSize(handlerRva,handlerSize))return false;
    unsigned dispatches=0;bool rawReader=false;
    std::vector<std::pair<DWORD,BYTE>> booleanReads;
    constexpr BYTE readByte[]={0x0f,0xb6,0x0c,0x01,0x88,0x4a,0xff};
    constexpr BYTE readOwner[]={0x48,0x8b,0x3a};
    for(const auto& call:callsIn(image,handlerRva,handlerSize)) {
        DWORD size=0;const bool sized=image.functionSize(call.second,size);
        // 原版逐字节读取器是无栈帧叶函数，可能没有 x64 异常表项。
        constexpr BYTE readerHead[]={0x4c,0x8b,0xc9,0x8b,0x49,0x2c};
        if(!sized && image.contains(call.second,96,true) && !std::memcmp(image.base+call.second,readerHead,sizeof(readerHead)))size=96;
        if(size && codeHas(image,call.second,size,readByte,sizeof(readByte))) {
            rawReader=true;
            for(DWORD at=call.first>(handlerRva+40)?call.first-40:handlerRva;at+4<=call.first;++at) {
                const auto p=image.base+at;
                if(p[0]==0x48 && p[1]==0x8d && p[2]==0x55)booleanReads.push_back({call.first,p[3]});
            }
        }
        if(!sized)continue;
        if(std::find(dispatchRoots.begin(),dispatchRoots.end(),call.second)==dispatchRoots.end() ||
            !codeHas(image,call.second,size,readOwner,sizeof(readOwner)))continue;
        if(call.first<handlerRva+14)continue;
        const auto arguments=image.base+call.first-14;
        constexpr BYTE keyAddress[]={0x4c,0x8d,0x4d},booleanAddress[]={0x4c,0x8d,0x45};
        if(std::memcmp(arguments,keyAddress,sizeof(keyAddress)) || std::memcmp(arguments+4,booleanAddress,sizeof(booleanAddress)))continue;
        bool rawArgument=false;
        for(const auto& boolean:booleanReads)if(boolean.first<call.first && boolean.second==arguments[7]) {
            bool untouched=true;
            for(DWORD at=boolean.first+5;at<call.first;) {
                hde64s instruction{};BYTE bytes[16]{};std::memcpy(bytes,image.base+at,(std::min)(DWORD{15},call.first-at));
                const auto count=hde64_disasm(bytes,&instruction);
                if(!count || (instruction.flags&F_ERROR) || count>call.first-at){untouched=false;break;}
                if((instruction.flags&F_MODRM) && instruction.modrm_mod==1 && instruction.modrm_rm==5 && !instruction.rex_b &&
                    instruction.disp.disp8==boolean.second && instruction.opcode!=0x8d && instruction.opcode!=0x8a && instruction.opcode!=0x8b &&
                    !(instruction.opcode==0x0f && (instruction.opcode2==0xb6 || instruction.opcode2==0xb7))){untouched=false;break;}
                at+=count;
            }
            if(untouched)rawArgument=true;
        }
        if(!rawArgument)continue;
        if(!target.dispatch){target.dispatchRva=call.second;target.dispatch=reinterpret_cast<NativePartnerSummonDispatch>(image.base+call.second);++dispatches;}
        else if(target.dispatchRva!=call.second)++dispatches;
    }
    if(dispatches!=1 || !rawReader){error=rawReader?"native summon server dispatcher not unique":"native summon byte reader not compatible";return false;}
    unsigned senders=0;
    constexpr BYTE oneByte[]={0x41,0xb8,1,0,0,0},fourBytes[]={0x41,0xb8,4,0,0,0};
    constexpr BYTE peerSlot[]={0x48,0x8b,3,0x48,0x8b,0xcb,0xff,0x90,0x68,1,0,0};
    constexpr BYTE rawWrite[]={0x44,0x8b,0xc7,0x48,0x03,0x4b,0x10};
    for(const auto& span:image.spans)if(span.executable)for(DWORD at=span.begin;at+5<=span.end;++at) {
        if(image.base[at]!=0xe8)continue;std::int32_t relative=0;std::memcpy(&relative,image.base+at+1,4);
        if(static_cast<std::int64_t>(at)+5+relative!=marriage.lookupRva)continue;
        DWORD begin=0,length=0;if(!image.functionRange(at,begin,length) || length>512)continue;
        const auto calls=callsIn(image,begin,length,false);
        for(std::size_t i=0;i+1<calls.size();++i) {
            const auto info=calls[i],send=calls[i+1];const auto q=image.base+info.first+5;
            if(info.first<begin+2 || info.first+9>begin+length || image.base[info.first-2]!=0x8b ||
                (image.base[info.first-1]&0xf8)!=0xd0 || q[0]!=0x4c || q[1]!=0x8d || q[2]!=0x48 || !q[3] || q[3]>=128)continue;
            DWORD size=0;if(!image.functionSize(send.second,size) || size>1024)continue;
            const auto peer=std::search(image.base+send.second,image.base+send.second+size,std::begin(peerSlot),std::end(peerSlot));
            if(peer==image.base+send.second+size)continue;
            const auto payloadSize=static_cast<DWORD>(peer-(image.base+send.second));
            if(!codeHas(image,send.second,payloadSize,oneByte,sizeof(oneByte)) || !codeHas(image,send.second,payloadSize,fourBytes,sizeof(fourBytes)))continue;
            bool samePacket=false,rawWriter=false;
            for(DWORD j=0;j+5<=size;++j)if(image.base[send.second+j]==0xb9) {
                DWORD identifier=0;std::memcpy(&identifier,image.base+send.second+j+1,4);if(identifier==packet)samePacket=true;
            }
            for(const auto& writer:callsIn(image,send.second,size)) {
                DWORD writerSize=0;if(image.functionSize(writer.second,writerSize) && codeHas(image,writer.second,writerSize,rawWrite,sizeof(rawWrite)))rawWriter=true;
            }
            if(!samePacket || !rawWriter)continue;
            if(!target.send) {
                target.sendRva=send.second;target.send=reinterpret_cast<NativePartnerSummonSend>(image.base+send.second);
                target.playableInfo=reinterpret_cast<NativePlayableInfo>(image.base+info.second);target.playableKeyOffset=q[3];++senders;
            } else if(target.sendRva!=send.second || reinterpret_cast<const BYTE*>(target.playableInfo)!=image.base+info.second || target.playableKeyOffset!=q[3])++senders;
        }
    }
    if(senders!=1){error="native summon client sender/playable key ABI not unique";target={};return false;}
    error=nullptr;return true;
}
