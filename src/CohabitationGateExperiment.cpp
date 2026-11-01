// 自动定位三个关系条件，按 INI 覆盖结果；不修改游戏文件或存档数据。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include "ConditionTarget.h"

namespace {
// 引擎使用 0=满足、1=不满足、2=上下文不可用，不能使用普通 bool 代替。
using Evaluate = std::uint8_t(__fastcall*)(void*, void*);
constexpr unsigned conditionCount = 3;
constexpr const wchar_t* keys[] = {L"IgnoreFriendlyLevel", L"IgnoreMarriageable", L"IgnoreLivingTogether"};
constexpr const char* labels[] = {"CheckFriendlyLevel", "IsMarriageable", "HasLivingTogether"};
Evaluate originals[conditionCount] = {};
HMODULE self = nullptr;
volatile LONG activeConditions = 0;
wchar_t logPath[MAX_PATH] = {}, iniPath[MAX_PATH] = {};

struct Settings {
    bool enabled = false;
    LONG requested = 0;
    LONG active() const { return enabled ? requested : 0; }
};

Settings readSettings() {
    Settings settings;
    settings.enabled = GetPrivateProfileIntW(L"Experiment", L"Enabled", 0, iniPath) != 0;
    for (unsigned i = 0; i < conditionCount; ++i) {
        // 保持旧配置的行为：缺少新键时只启用同居限制覆盖，好感和可结婚判断不动。
        const int fallback = i == static_cast<unsigned>(ConditionKind::LivingTogether) ? 1 : 0;
        if (GetPrivateProfileIntW(L"Conditions", keys[i], fallback, iniPath) != 0)
            settings.requested |= 1L << i;
    }
    return settings;
}

void log(const char* text) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, logPath, L"ab") == 0 && file) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        // 多个进程可能加载 ASI；用 PID 区分各自的定位和挂钩结果。
        fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u [pid=%lu] %s\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
            GetCurrentProcessId(), text);
        fclose(file);
    }
}

std::uint8_t transform(std::uint8_t value, ConditionKind kind, bool active) {
    if (!active) return value;
    // 仅修改有效的真/假结果，2 和未知值始终保留。
    if (kind == ConditionKind::LivingTogether) return value == 0 ? 1 : value;
    return value == 1 ? 0 : value;
}

template<unsigned index>
std::uint8_t __fastcall replacement(void* object, void* context) {
    // 始终先由原函数处理真实上下文，不增加好感、不写关系状态。
    const auto value = originals[index](object, context);
    const auto mask = InterlockedCompareExchange(&activeConditions, 0, 0);
    return transform(value, static_cast<ConditionKind>(index), (mask & (1L << index)) != 0);
}

const Evaluate replacements[] = {replacement<0>, replacement<1>, replacement<2>};

bool exchangeSlot(const ConditionTarget& target, void* expected, void* replacementPointer) {
    DWORD prior = 0;
    if (!VirtualProtect(target.slot, sizeof(void*), PAGE_READWRITE, &prior)) return false;
    // 其他插件在定位后改过入口时不覆盖它。
    void* found = InterlockedCompareExchangePointer(target.slot, replacementPointer, expected);
    DWORD ignored = 0;
    if (!VirtualProtect(target.slot, sizeof(void*), prior, &ignored))
        log("WARNING: could not restore vtable page protection.");
    return found == expected;
}

DWORD WINAPI run(void*) {
    // 配置与日志位于插件旁边，不依赖当前工作目录。
    const DWORD length = GetModuleFileNameW(self, logPath, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    wchar_t* dot = wcsrchr(logPath, L'.');
    if (!dot) return 1;
    wcscpy_s(dot, MAX_PATH - (dot - logPath), L".log");
    wcscpy_s(iniPath, logPath);
    dot = wcsrchr(iniPath, L'.');
    wcscpy_s(dot, MAX_PATH - (dot - iniPath), L".ini");
    log("Plugin loaded. Automatic condition discovery; full multi-marriage is unverified.");

    // 全部定位成功才安装；配置暂时关闭的条件也先定位，便于运行中切换。
    ConditionTarget targets[conditionCount] = {};
    for (unsigned i = 0; i < conditionCount; ++i) {
        const char* error = nullptr;
        bool resolved = false;
        try {
            resolved = FindConditionTarget(GetModuleHandleW(nullptr), targets[i], error,
                static_cast<ConditionKind>(i));
        } catch (...) { error = "target discovery failed"; }
        char message[320];
        if (!resolved) {
            sprintf_s(message, "REFUSED: %s: %s; no hooks installed.",
                labels[i], error ? error : "target discovery failed");
            log(message);
            return 1;
        }
        const auto& target = targets[i];
        sprintf_s(message, "RESOLVED: %s type RVA=0x%08lx locator RVA=0x%08lx vtable RVA=0x%08lx slot=%u evaluator RVA=0x%08lx",
            labels[i], target.typeRva, target.locatorRva, target.vtableRva, target.slotIndex, target.evaluatorRva);
        log(message);
        originals[i] = reinterpret_cast<Evaluate>(target.evaluator);
    }
    log("PASS: all three unique RTTI/vtable/dispatch targets and evaluator compatibility checks.");

    // 安装期间维持原版行为；任一槽安装失败时撤回之前安装的槽。
    unsigned installed = 0;
    for (; installed < conditionCount; ++installed)
        if (!exchangeSlot(targets[installed], targets[installed].evaluator,
            reinterpret_cast<void*>(replacements[installed]))) break;
    if (installed != conditionCount) {
        while (installed > 0) {
            --installed;
            if (!exchangeSlot(targets[installed], reinterpret_cast<void*>(replacements[installed]),
                targets[installed].evaluator))
                log("WARNING: hook rollback was not completed; condition overrides remain disabled.");
        }
        log("REFUSED: could not install all condition hooks; overrides disabled.");
        return 1;
    }
    log("INSTALLED: three condition evaluator wrappers. INI reloads every 5 seconds.");
    LONG oldRequested = -1;
    int oldEnabled = -1;
    for (;;) {
        const auto settings = readSettings();
        // 一个原子位掩码同时发布总开关与三个子开关，回调不会读取半更新配置。
        InterlockedExchange(&activeConditions, settings.active());
        if (settings.requested != oldRequested || static_cast<int>(settings.enabled) != oldEnabled) {
            char message[200];
            sprintf_s(message, "STATUS Enabled=%d IgnoreFriendlyLevel=%d IgnoreMarriageable=%d IgnoreLivingTogether=%d",
                settings.enabled, (settings.requested & 1) != 0, (settings.requested & 2) != 0,
                (settings.requested & 4) != 0);
            log(message);
            oldRequested = settings.requested;
            oldEnabled = settings.enabled;
        }
        Sleep(5000);
    }
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // 扫描、文件访问和挂钩都在工作线程执行，避免占用 DLL 加载器锁。
        self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
        // 关闭句柄不会终止线程；卸载插件应完整退出游戏。
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
