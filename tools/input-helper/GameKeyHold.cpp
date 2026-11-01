// Local test helper. Never executes commands, changes files, or injects DLLs.
// Sends one bounded scan-code key hold ONLY while Crimson Desert is foreground.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { puts("Usage: GameKeyHold <space|m|tab|w|a|s|d|e|f|shift> <50..2000 ms>"); return 2; }
    struct Key { const wchar_t* name; WORD vk; };
    const Key keys[]={{L"space",VK_SPACE},{L"m",'M'},{L"tab",VK_TAB},
        {L"w",'W'},{L"a",'A'},{L"s",'S'},{L"d",'D'},{L"e",'E'},{L"f",'F'},
        {L"shift",VK_LSHIFT}};
    WORD vk=0;
    for(const auto& k:keys) if(wcscmp(argv[1],k.name)==0) vk=k.vk;
    wchar_t* end=nullptr; const long duration=wcstol(argv[2],&end,10);
    if(!vk || *end || duration<50 || duration>2000) { puts("REFUSED: invalid key/duration."); return 2; }
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    HANDLE proc=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    wchar_t path[32768]={}; DWORD length=32768;
    const bool valid=proc && QueryFullProcessImageNameW(proc,0,path,&length) &&
      _wcsicmp(path,L"D:\\gametool\\steam\\steamapps\\common\\Crimson Desert\\bin64\\CrimsonDesert.exe")==0;
    if(proc) CloseHandle(proc);
    if(!valid) { puts("REFUSED: foreground is not the expected game."); return 3; }
    INPUT key{}; key.type=INPUT_KEYBOARD;
    key.ki.wScan=static_cast<WORD>(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC));
    key.ki.dwFlags=KEYEVENTF_SCANCODE;
    if(!key.ki.wScan || SendInput(1,&key,sizeof(key))!=1) { puts("FAILED: key down."); return 4; }
    const ULONGLONG start=GetTickCount64(); bool changed=false;
    do {
        Sleep(10); DWORD current=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&current);
        if(current!=pid) { changed=true; break; }
    } while(GetTickCount64()-start<static_cast<ULONGLONG>(duration));
    key.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
    const bool released=SendInput(1,&key,sizeof(key))==1;
    printf("%s held_ms=%llu foreground_changed=%d released=%d\n",
      released?"DONE":"FAILED_RELEASE",GetTickCount64()-start,changed,released);
    return released?0:5;
}
