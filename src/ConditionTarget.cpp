#define WIN32_LEAN_AND_MEAN
#include "ConditionTarget.h"
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
    bool functionSize(DWORD rva, DWORD& length) const {
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
                if (rva != fn.BeginAddress || fn.EndAddress <= rva) return false;
                length = fn.EndAddress - rva;
                return length <= 4096 && contains(rva, length, true);
            }
        }
        return false;
    }
};

// 不比较函数完整机器码；保留与三态返回语义有关的两段特征。
// 忽略堆栈布局、context 字段偏移、相对调用地址和分支距离等易随构建变化的值。
bool compatibleEvaluator(const Image& image, DWORD rva, ConditionKind kind) {
    DWORD length = 0;
    if (!image.functionSize(rva, length)) return false;
    const auto code = image.base + rva;
    bool contextLoad = false, unavailable = false, predicate = false;
    unsigned inversions = 0;
    for (DWORD i = 0; i + 7 <= (std::min)(length, DWORD{96}); ++i)
        // mov r64,[rdx+disp32]，兼容当前三种评估器使用的 RCX / RSI 寄存器。
        if (code[i] == 0x48 && code[i+1] == 0x8b && (code[i+2] & 0xc7) == 0x82)
            contextLoad = true;
    constexpr unsigned char nullPredicate[] = {0x48,0x85,0xdb,0x0f,0x94,0xc3};
    constexpr unsigned char invertBoolean[] = {0x84,0xc0,0x0f,0x94,0xc0};
    for (DWORD i = 0; i + 2 <= length; ++i) {
        if (code[i] == 0xb0 && code[i+1] == 2) unavailable = true;
        if (i + sizeof(invertBoolean) <= length &&
            std::memcmp(code + i, invertBoolean, sizeof(invertBoolean)) == 0) ++inversions;
        if (i + sizeof(nullPredicate) <= length &&
            std::memcmp(code + i, nullPredicate, sizeof(nullPredicate)) == 0) predicate = true;
    }
    if (!contextLoad || !unavailable) return false;
    if (kind == ConditionKind::LivingTogether) return predicate;
    // 可结婚判断把普通布尔值取反为三态结果；好感比较有多个比较运算分支。
    return inversions >= (kind == ConditionKind::FriendlyLevel ? 2u : 1u);
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
