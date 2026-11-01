#define WIN32_LEAN_AND_MEAN
#include "ConditionTarget.h"
#include "MarriageCommit.h"
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
