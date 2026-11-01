// A deliberately narrow, build-specific experiment. No archive/save writes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
using Evaluate = std::uint8_t(__fastcall*)(void*, void*);
Evaluate original = nullptr;
HMODULE self = nullptr;
volatile LONG enabled = 0;
volatile LONG64 calls[3] = {}, unexpected = 0;
wchar_t logPath[MAX_PATH] = {}, iniPath[MAX_PATH] = {};
constexpr DWORD buildTimestamp = 0x6ae58024;
constexpr DWORD buildImageSize = 0x13ee0000;
constexpr std::uintptr_t vtableRva = 0x05cfad38;
constexpr std::uintptr_t evaluateRva = 0x024896d0;
constexpr std::uintptr_t typeNameRva = 0x06fbb5d0;
constexpr unsigned slotNumber = 21;

// This engine uses 0 = satisfied, 1 = unsatisfied, 2 = unavailable.
// Preserve unavailable/unknown results rather than manufacturing valid context.
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
    const auto value = original(object, context);
    if (value < 3) InterlockedIncrement64(&calls[value]);
    else InterlockedIncrement64(&unexpected);
    return transform(value, InterlockedCompareExchange(&enabled,0,0) != 0);
}
DWORD WINAPI run(void*) {
    GetModuleFileNameW(self, logPath, MAX_PATH);
    wchar_t* dot = wcsrchr(logPath,L'.'); if (!dot) return 1;
    wcscpy_s(dot, MAX_PATH - (dot-logPath), L".log");
    wcscpy_s(iniPath,logPath); dot = wcsrchr(iniPath,L'.');
    wcscpy_s(dot, MAX_PATH - (dot-iniPath), L".ini");
    log("Plugin loaded. Experimental cohabitation gate only; full multi-marriage is unverified.");
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
    for (unsigned value=0; value<256; ++value) {
        if (transform(static_cast<std::uint8_t>(value),false) != value ||
            transform(static_cast<std::uint8_t>(value),true) != (value==0?1:value)) {
            log("REFUSED: result mapping self-test failed."); return 1;
        }
    }
    log("PASS: build/RTTI/evaluator/slot fingerprints and 512 result mapping checks.");
    original = reinterpret_cast<Evaluate>(*slot);
    InterlockedExchange(&enabled,GetPrivateProfileIntW(L"Experiment",L"Enabled",0,iniPath)!=0);
    DWORD prior = 0;
    if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&prior)) {
        log("REFUSED: could not make vtable slot writable."); return 1;
    }
    void* found = InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&replacement),
        reinterpret_cast<void*>(original));
    DWORD ignored = 0; VirtualProtect(slot,sizeof(void*),prior,&ignored);
    if (found != reinterpret_cast<void*>(original)) {
        log("REFUSED: slot changed concurrently."); return 1;
    }
    log("INSTALLED: HasLivingTogether evaluator wrapper. Enabled reloads from INI every 5 seconds.");
    LONG oldEnabled = -1;
    LONG64 previous[3] = {-1,-1,-1};
    for (;;) {
        const LONG active = GetPrivateProfileIntW(L"Experiment",L"Enabled",0,iniPath)!=0;
        InterlockedExchange(&enabled,active);
        LONG64 snapshot[3];
        for(int i=0;i<3;++i) snapshot[i]=InterlockedCompareExchange64(&calls[i],0,0);
        if (active!=oldEnabled || memcmp(snapshot,previous,sizeof(snapshot))!=0) {
            char text[256];
            sprintf_s(text,"STATUS Enabled=%ld calls(original satisfied/unsatisfied/unavailable)=%lld/%lld/%lld unexpected=%lld",
                active,snapshot[0],snapshot[1],snapshot[2],InterlockedCompareExchange64(&unexpected,0,0));
            log(text); oldEnabled=active; memcpy(previous,snapshot,sizeof(snapshot));
        }
        Sleep(5000);
    }
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        self=module; DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,run,nullptr,0,nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
