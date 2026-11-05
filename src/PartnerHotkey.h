#pragma once
#include <windows.h>
#include <string>

enum PartnerHotkeyModifier : unsigned { PartnerCtrl=1,PartnerAlt=2,PartnerShift=4 };
struct PartnerHotkey {
    unsigned key=VK_F8,modifiers=0;
    bool operator==(const PartnerHotkey& other) const {return key==other.key && modifiers==other.modifiers;}
    bool operator!=(const PartnerHotkey& other) const {return !(*this==other);}
};
using PartnerKeyState=SHORT(WINAPI*)(int);
bool ParsePartnerHotkey(const std::wstring& text,PartnerHotkey& result);
PartnerHotkey GetPartnerHotkey();
std::wstring PartnerHotkeyLabel(PartnerHotkey hotkey=GetPartnerHotkey());
// 缺失或无效配置回退 F8；只读取 INI，不重写用户输入。
bool ReloadPartnerHotkey(const wchar_t* iniPath,bool* valid=nullptr);
bool PartnerHotkeyMatches(PartnerHotkey hotkey,UINT key,PartnerKeyState state=&GetAsyncKeyState);
bool PartnerHotkeyDown(PartnerHotkey hotkey,PartnerKeyState state=&GetAsyncKeyState);
