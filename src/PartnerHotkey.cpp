#define WIN32_LEAN_AND_MEAN
#include "PartnerHotkey.h"
#include <atomic>
#include <cwctype>

namespace {
std::atomic<unsigned> configured{VK_F8};
struct NamedKey {const wchar_t* name;unsigned key;};
constexpr NamedKey namedKeys[]={
    {L"INSERT",VK_INSERT},{L"DELETE",VK_DELETE},{L"HOME",VK_HOME},{L"END",VK_END},
    {L"PAGEUP",VK_PRIOR},{L"PAGEDOWN",VK_NEXT},{L"PAUSE",VK_PAUSE},{L"SCROLLLOCK",VK_SCROLL},
    {L"NUMLOCK",VK_NUMLOCK},{L"SPACE",VK_SPACE},{L"TAB",VK_TAB},{L"BACKSPACE",VK_BACK},{L"ESCAPE",VK_ESCAPE},
    {L"INS",VK_INSERT},{L"DEL",VK_DELETE},{L"PGUP",VK_PRIOR},{L"PGDN",VK_NEXT},{L"ESC",VK_ESCAPE}
};
std::wstring token(std::wstring value) {
    const auto begin=value.find_first_not_of(L" \t\r\n"),end=value.find_last_not_of(L" \t\r\n");
    if(begin==std::wstring::npos)return L"";
    value=value.substr(begin,end-begin+1);
    for(auto& character:value)character=static_cast<wchar_t>(towupper(character));
    return value;
}
unsigned keyFor(const std::wstring& value) {
    if(value.size()==1 && ((value[0]>=L'A' && value[0]<=L'Z') || (value[0]>=L'0' && value[0]<=L'9')))return value[0];
    if(value.size()>=2 && value.size()<=3 && value[0]==L'F') {
        unsigned number=0;
        for(std::size_t i=1;i<value.size();++i){if(value[i]<L'0' || value[i]>L'9')return 0;number=number*10+value[i]-L'0';}
        if(number>=1 && number<=24)return VK_F1+number-1;
    }
    for(const auto& item:namedKeys)if(value==item.name)return item.key;
    return 0;
}
unsigned heldModifiers(PartnerKeyState state) {
    return ((state(VK_CONTROL)&0x8000)?PartnerCtrl:0) | ((state(VK_MENU)&0x8000)?PartnerAlt:0) |
        ((state(VK_SHIFT)&0x8000)?PartnerShift:0);
}
}
bool ParsePartnerHotkey(const std::wstring& text,PartnerHotkey& result) {
    if(text.empty() || text.size()>127)return false;
    PartnerHotkey parsed;parsed.key=0;
    for(std::size_t at=0;;) {
        const auto end=text.find(L'+',at);const auto value=token(text.substr(at,end==std::wstring::npos?end:end-at));
        if(value.empty())return false;
        const unsigned modifier=value==L"CTRL" || value==L"CONTROL"?PartnerCtrl:value==L"ALT"?PartnerAlt:value==L"SHIFT"?PartnerShift:0;
        if(modifier) {if(parsed.key || (parsed.modifiers&modifier))return false;parsed.modifiers|=modifier;}
        else {if(parsed.key || !(parsed.key=keyFor(value)))return false;}
        if(end==std::wstring::npos)break;
        at=end+1;
    }
    if(!parsed.key)return false;result=parsed;return true;
}
PartnerHotkey GetPartnerHotkey() {const auto value=configured.load();return {value&255,value>>8};}
std::wstring PartnerHotkeyLabel(PartnerHotkey hotkey) {
    std::wstring label;
    if(hotkey.modifiers&PartnerCtrl)label+=L"Ctrl+";
    if(hotkey.modifiers&PartnerAlt)label+=L"Alt+";
    if(hotkey.modifiers&PartnerShift)label+=L"Shift+";
    if(hotkey.key>=VK_F1 && hotkey.key<=VK_F24)return label+L"F"+std::to_wstring(hotkey.key-VK_F1+1);
    if((hotkey.key>=L'A' && hotkey.key<=L'Z') || (hotkey.key>=L'0' && hotkey.key<=L'9'))return label+static_cast<wchar_t>(hotkey.key);
    for(const auto& item:namedKeys)if(hotkey.key==item.key)return label+item.name;
    return label+L"F8";
}
bool ReloadPartnerHotkey(const wchar_t* iniPath,bool* valid) {
    wchar_t value[128]{};
    const auto length=GetPrivateProfileStringW(L"UI",L"ManagerHotkey",L"F8",value,128,iniPath);
    PartnerHotkey hotkey;const bool accepted=length<127 && ParsePartnerHotkey(value,hotkey);
    if(valid)*valid=accepted;
    const auto packed=hotkey.key|(hotkey.modifiers<<8);
    return configured.exchange(packed)!=packed;
}
bool PartnerHotkeyMatches(PartnerHotkey hotkey,UINT key,PartnerKeyState state) {
    return state && key==hotkey.key && heldModifiers(state)==hotkey.modifiers;
}
bool PartnerHotkeyDown(PartnerHotkey hotkey,PartnerKeyState state) {
    return state && (state(static_cast<int>(hotkey.key))&0x8000) && PartnerHotkeyMatches(hotkey,hotkey.key,state);
}
