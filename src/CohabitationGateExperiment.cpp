// 仅覆盖“已有同居对象”的条件结果；不修改婚姻数据、游戏文件或存档。
// 地址只适用于已确认的游戏版本，任一指纹不符时停止安装挂钩。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
// 保留原评估函数的调用约定和单字节返回类型，不能改成普通 bool。
using Evaluate = std::uint8_t(__fastcall*)(void*, void*);
Evaluate original = nullptr;
HMODULE self = nullptr;
volatile LONG enabled = 0;
wchar_t logPath[MAX_PATH] = {}, iniPath[MAX_PATH] = {};
// RVA 是相对于游戏模块基址的偏移，进程加载地址变化时仍可定位。
constexpr DWORD buildTimestamp = 0x6ae58024;
constexpr DWORD buildImageSize = 0x13ee0000;
constexpr std::uintptr_t vtableRva = 0x05cfad38;
constexpr std::uintptr_t evaluateRva = 0x024896d0;
constexpr std::uintptr_t typeNameRva = 0x06fbb5d0;
constexpr unsigned slotNumber = 21;

// 引擎语义：0 = 条件满足，1 = 不满足，2 = 上下文不可用。
// 开启时仅将“已有同居对象”映射为不满足，使外层 !HasLivingTogether() 可通过。
// 不可用和未知结果必须原样保留，避免缺少上下文时错误地放行。
std::uint8_t transform(std::uint8_t value, bool active) {
    return active && value == 0 ? 1 : value;
}
void log(const char* text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, logPath, L"ab") == 0 && f) {
        SYSTEMTIME t{}; GetLocalTime(&t);
        fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u %s\n", t.wYear,t.wMonth,t.wDay,
            t.wHour,t.wMinute,t.wSecond,text);
        fclose(f);
    }
}
std::uint8_t __fastcall replacement(void* object, void* context) {
    // 先执行原函数，由游戏负责解析真实上下文；此处只覆盖最终结果。
    const auto value = original(object, context);
    // 工作线程会更新开关，评估回调可能在其他线程上运行，使用原子读取。
    return transform(value, InterlockedCompareExchange(&enabled,0,0) != 0);
}
DWORD WINAPI run(void*) {
    // 配置与日志始终放在插件旁边，不依赖启动游戏时的工作目录。
    GetModuleFileNameW(self, logPath, MAX_PATH);
    wchar_t* dot = wcsrchr(logPath,L'.'); if (!dot) return 1;
    wcscpy_s(dot, MAX_PATH - (dot-logPath), L".log");
    wcscpy_s(iniPath,logPath); dot = wcsrchr(iniPath,L'.');
    wcscpy_s(dot, MAX_PATH - (dot-iniPath), L".ini");
    log("Plugin loaded. Experimental cohabitation gate only; full multi-marriage is unverified.");
    // 先确认主模块版本，再读取硬编码 RVA，防止更新后访问错误位置。
    const auto image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { log("REFUSED: DOS header."); return 1; }
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != buildTimestamp ||
        nt->OptionalHeader.SizeOfImage != buildImageSize) {
        log("REFUSED: unsupported game build; no patch applied."); return 1;
    }
    if (strcmp(reinterpret_cast<char*>(image+typeNameRva),
        ".?AVConditionData_HasLivingTogether@pa@@") != 0) {
        log("REFUSED: RTTI fingerprint mismatch; no patch applied."); return 1;
    }
    // RTTI、函数机器码和原虚函数槽共同确认目标，避免仅凭版本号误挂钩。
    const unsigned char prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x57,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x33,0xf6,0x89,0x74,0x24,0x20,
        0x48,0x8b,0x8a,0x28,0x08,0x00,0x00,0x48};
    if (memcmp(image+evaluateRva,prefix,sizeof(prefix)) != 0) {
        log("REFUSED: evaluator fingerprint mismatch; no patch applied."); return 1;
    }
    auto slot = reinterpret_cast<void**>(image+vtableRva) + slotNumber;
    if (*slot != image+evaluateRva) {
        log("REFUSED: vtable slot changed; no patch applied."); return 1;
    }
    log("PASS: build/RTTI/evaluator/slot fingerprints.");
    // 必须在发布回调指针前保存原入口，并初始化开关；配置缺失时默认关闭。
    original = reinterpret_cast<Evaluate>(*slot);
    InterlockedExchange(&enabled,GetPrivateProfileIntW(L"Experiment",L"Enabled",0,iniPath)!=0);
    DWORD prior = 0;
    // 只临时开放虚函数槽的写权限，不改原评估函数的机器码。
    if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&prior)) {
        log("REFUSED: could not make vtable slot writable."); return 1;
    }
    // 若其他插件在检查之后修改了该槽，原子比较交换会拒绝覆盖它。
    void* found = InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&replacement),
        reinterpret_cast<void*>(original));
    DWORD ignored = 0; VirtualProtect(slot,sizeof(void*),prior,&ignored);
    if (found != reinterpret_cast<void*>(original)) {
        log("REFUSED: slot changed concurrently."); return 1;
    }
    log("INSTALLED: HasLivingTogether evaluator wrapper. Enabled reloads from INI every 5 seconds.");
    LONG oldEnabled = -1;
    for (;;) {
        // 每 5 秒读取安装目录中的 INI，实现运行中切换；仅在开关变化时记日志。
        const LONG active = GetPrivateProfileIntW(L"Experiment",L"Enabled",0,iniPath)!=0;
        InterlockedExchange(&enabled,active);
        if (active!=oldEnabled) {
            log(active ? "STATUS Enabled=1" : "STATUS Enabled=0");
            oldEnabled=active;
        }
        Sleep(5000);
    }
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        // 将文件读写、版本检查与挂钩留给工作线程，避免在 DLL 加载器锁内执行。
        self=module; DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,run,nullptr,0,nullptr);
        // 关闭句柄不会终止工作线程；线程随游戏进程退出，不支持运行中卸载 DLL。
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
