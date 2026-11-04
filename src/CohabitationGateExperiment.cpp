// 自动定位关系和种族条件，按 INI 覆盖结果；不修改游戏文件或存档数据。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include "ConditionTarget.h"
#include "MarriageCommit.h"
#include "BeautyPicker.h"
#include "PartnerManager.h"
#include "UiLanguage.h"

namespace {
// 引擎使用 0=满足、1=不满足、2=上下文不可用，不能使用普通 bool 代替。
using Evaluate = std::uint8_t(__fastcall*)(void*, void*, std::uint8_t);
constexpr unsigned conditionCount = static_cast<unsigned>(ConditionKind::Count);
constexpr const wchar_t* keys[] = {
    L"IgnoreFriendlyLevel", L"IgnoreMarriageable", L"IgnoreLivingTogether", L"IgnoreAffinityLevel", L"IgnoreTribe"
};
constexpr const char* labels[] = {
    "CheckFriendlyLevel", "IsMarriageable", "HasLivingTogether", "CheckAffinityLevel", "CheckTribe"
};
static_assert(sizeof(keys) / sizeof(keys[0]) == conditionCount);
static_assert(sizeof(labels) / sizeof(labels[0]) == conditionCount);
Evaluate originals[conditionCount] = {};
ConditionTarget targets[conditionCount] = {};
HMODULE self = nullptr;
volatile LONG activeConditions = 0;
wchar_t logPath[MAX_PATH] = {}, iniPath[MAX_PATH] = {};
SRWLOCK settingsLock=SRWLOCK_INIT;

struct Settings {
    bool enabled = false;
    bool beautyPicker = false;
    LONG requested = 0;
    bool ignoreExistingMarriageOnCommit = false;
    DWORD preferredPartnerKey = 0;
    bool spawnAllPartners = false;
    LONG active() const { return enabled ? requested : 0; }
};

Settings readSettings() {
    Settings settings;
    settings.beautyPicker = GetPrivateProfileIntW(L"Relationships", L"BeautyPartnerPicker", 0, iniPath) != 0;
    settings.enabled = GetPrivateProfileIntW(L"Experiment", L"Enabled", 0, iniPath) != 0;
    settings.ignoreExistingMarriageOnCommit = GetPrivateProfileIntW(
        L"Relationships", L"IgnoreExistingMarriageOnCommit", 0, iniPath) != 0;
    settings.preferredPartnerKey=GetPrivateProfileIntW(L"Relationships",L"PreferredPartnerKey",0,iniPath);
    settings.spawnAllPartners=GetPrivateProfileIntW(L"Relationships",L"SpawnAllPartners",0,iniPath)!=0;
    for (unsigned i = 0; i < conditionCount; ++i) {
        // 保持旧配置的行为：缺少子开关时只启用同居覆盖，其他条件不动。
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

bool savePreferredPartner(DWORD key) {
    AcquireSRWLockExclusive(&settingsLock);
    wchar_t value[16]{};swprintf_s(value,L"%lu",key);
    const bool saved=WritePrivateProfileStringW(L"Relationships",L"PreferredPartnerKey",value,iniPath)!=FALSE;
    if(saved)SetPreferredPartnerKey(key);
    ReleaseSRWLockExclusive(&settingsLock);
    if(saved){char line[128];sprintf_s(line,"PARTNER PREFERENCE SAVED: PreferredPartnerKey=%lu; applied immediately.",key);log(line);}
    return saved;
}

std::uint8_t transform(std::uint8_t value, ConditionKind kind, bool active,
    const ConditionTarget* target = nullptr, const void* object = nullptr) {
    if (!active || value > 1) return value;
    if (kind == ConditionKind::FriendlyLevel || kind == ConditionKind::AffinityLevel) {
        if (!target || !target->hasComparison || !object) return value;
        const auto bytes = static_cast<const std::uint8_t*>(object);
        const auto index = bytes[target->comparisonOffset];
        if (index >= 6) return value;
        const int threshold = static_cast<std::int8_t>(bytes[target->thresholdOffset]);
        // 使用有符号等级字段的最高值，仅用于比较，不写实际等级。
        // 这样 >=3 成功分支通过，<3 失败分支不通过，避免两个分支同时成立。
        constexpr int virtualLevel = 127;
        bool satisfied = false;
        switch (target->comparisonOps[index]) {
        case ComparisonOp::Equal: satisfied = virtualLevel == threshold; break;
        case ComparisonOp::NotEqual: satisfied = virtualLevel != threshold; break;
        case ComparisonOp::LessEqual: satisfied = virtualLevel <= threshold; break;
        case ComparisonOp::GreaterEqual: satisfied = virtualLevel >= threshold; break;
        case ComparisonOp::Less: satisfied = virtualLevel < threshold; break;
        case ComparisonOp::Greater: satisfied = virtualLevel > threshold; break;
        default: return value;
        }
        return satisfied ? 0 : 1;
    }
    // 仅修改有效的真/假结果，2 和未知值始终保留。
    if (kind == ConditionKind::LivingTogether) return value == 0 ? 1 : value;
    return value == 1 ? 0 : value;
}

template<unsigned index>
std::uint8_t __fastcall replacement(void* object, void* context, std::uint8_t negated) {
    // 始终先由原函数处理真实上下文，不增加好感、不写关系状态。
    const auto value = originals[index](object, context, negated);
    const auto mask = InterlockedCompareExchange(&activeConditions, 0, 0);
    // 原生 NOT 节点翻转第三参数，AND 节点继续传递。
    // 只覆盖取反的同居条件，保留美容等正向检查。
    const bool applicable = index != static_cast<unsigned>(ConditionKind::LivingTogether) || negated == 1;
    const auto result = transform(value, static_cast<ConditionKind>(index), applicable && (mask & (1L << index)) != 0,
        &targets[index], object);
    return result;
}

const Evaluate replacements[] = {replacement<0>, replacement<1>, replacement<2>, replacement<3>, replacement<4>};
static_assert(sizeof(replacements) / sizeof(replacements[0]) == conditionCount);

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
    ConfigurePartnerManager(iniPath,&savePreferredPartner,log);
    log("Plugin loaded. v0.1.3: F8 partner manager, persistent display names, beauty selection and bilingual dialogs; native relationship guards.");

    // 全部定位成功才安装；配置暂时关闭的条件也先定位，便于运行中切换。
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
    log("PASS: all five unique RTTI/vtable/dispatch targets and evaluator compatibility checks.");

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
    log("INSTALLED: five condition evaluator wrappers. INI reloads every 5 seconds.");
    // 提交检查是独立的可选挂钩。定位或安装失败不会强行放行婚姻提交。
    const bool commitHookReady = InstallMarriageCommitHook(GetModuleHandleW(nullptr),log,
        targets[static_cast<unsigned>(ConditionKind::LivingTogether)].evaluator);
    LONG oldRequested = -1;
    int oldEnabled = -1;
    int oldCommit = -1;
    DWORD oldPartnerKey=MAXDWORD;
    int oldSpawn=-1;
    ULONGLONG nextSettings=0;
    for (;;) {
        PollPartnerManager();
        const auto now=GetTickCount64();
        if(now<nextSettings){Sleep(50);continue;}
        nextSettings=now+5000;
        ReloadUiLanguage();
        AcquireSRWLockExclusive(&settingsLock);
        const auto settings = readSettings();
        SetBeautyPickerEnabled(settings.enabled && settings.beautyPicker);
        // 五项条件覆盖的总开关与子开关用一个原子位掩码发布。
        InterlockedExchange(&activeConditions, settings.active());
        const bool commitActive = settings.enabled && settings.ignoreExistingMarriageOnCommit && commitHookReady;
        const bool spawnActive=settings.enabled && settings.spawnAllPartners && IsPartnerSpawnHookReady();
        SetMarriageCommitOverride(commitActive,settings.preferredPartnerKey,spawnActive,settings.enabled);
        SetPartnerManagerEnabled(settings.enabled,commitActive);
        ReleaseSRWLockExclusive(&settingsLock);
        if (settings.requested != oldRequested || static_cast<int>(settings.enabled) != oldEnabled ||
            static_cast<int>(settings.ignoreExistingMarriageOnCommit) != oldCommit || settings.preferredPartnerKey!=oldPartnerKey ||
            static_cast<int>(settings.spawnAllPartners)!=oldSpawn) {
            char message[512];
            sprintf_s(message, "STATUS Enabled=%d IgnoreFriendlyLevel=%d IgnoreMarriageable=%d IgnoreLivingTogether=%d IgnoreAffinityLevel=%d IgnoreTribe=%d IgnoreExistingMarriageOnCommit=%d CommitOverrideActive=%d PreferredPartnerKey=%lu SpawnAllPartners=%d SpawnAllActive=%d RelationshipGuardsActive=%d PartnerRestoreActive=%d",
                settings.enabled, (settings.requested & 1) != 0, (settings.requested & 2) != 0,
                (settings.requested & 4) != 0, (settings.requested & 8) != 0,
                (settings.requested & 16) != 0, settings.ignoreExistingMarriageOnCommit, commitActive,settings.preferredPartnerKey,
                settings.spawnAllPartners,spawnActive,settings.enabled && IsPartnerRuntimeReady(),spawnActive && IsPartnerRestoreHookReady());
            log(message);
            oldRequested = settings.requested;
            oldEnabled = settings.enabled;
            oldCommit = settings.ignoreExistingMarriageOnCommit;
            oldPartnerKey=settings.preferredPartnerKey;
            oldSpawn=settings.spawnAllPartners;
        }
        Sleep(50);
    }
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // 扫描、文件访问和挂钩都在工作线程执行，避免占用 DLL 加载器锁。
        self = module;
        // 静态 CRT 与线程局部出场作用域需要正常线程通知，不禁用它们。
        HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
        // 关闭句柄不会终止线程；卸载插件应完整退出游戏。
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
