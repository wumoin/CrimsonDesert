#define WIN32_LEAN_AND_MEAN
#include "UiLanguage.h"
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace {
struct Translation { const wchar_t* chinese;const wchar_t* english; };
constexpr Translation translations[]={
    {L"语言 / &Language",L"语言 / &Language"},
    {L"简体中文",L"简体中文"},{L"English",L"English"},
    {L"伴侣管理",L"Partner Manager"},{L"伴侣美容",L"Partner Appearance"},
    {L"已婚对象（选择后管理名字、优先伴侣或召唤）",L"Select a married partner to manage or summon."},
    {L"选择要美容的伴侣：",L"Choose a partner to customize:"},
    {L"设为优先伴侣(&P)",L"Set &preferred"},{L"恢复自动选择(&A)",L"&Automatic"},
    {L"名字(&N)：",L"&Name:"},{L"保存名字(&S)",L"&Save name"},{L"重置名字(&R)",L"&Reset name"},
    {L"关闭(&C)",L"&Close"},{L"确定(&O)",L"&OK"},{L"取消(&C)",L"&Cancel"},
    {L"当前优先伴侣：自动选择",L"Preferred partner: Automatic"},
    {L"当前优先伴侣编号：",L"Preferred partner ID: "},{L"  · 优先",L"  · Preferred"},
    {L"原名：",L"Original name: "},{L"暂无已婚对象，请读档后重新打开。",L"No married partners. Load a save and reopen this window."},
    {L"修改会立即保存；召唤会关闭此窗口。",L"Changes are saved immediately. Summoning closes this window."},
    {L"名字接口不兼容，改名功能暂不可用。",L"Name editing is unavailable for this game version."},
    {L"优先选择需要开启婚姻提交覆盖；改名仍可用。",L"Enable marriage override to set a preference. Names can still be edited."},
    {L"角色或婚姻关系已变化，请关闭后重新打开。",L"Character or relationships changed. Close and reopen this window."},
    {L"请先开启 IgnoreExistingMarriageOnCommit。",L"Enable IgnoreExistingMarriageOnCommit first."},
    {L"保存失败，请检查插件配置文件是否可写。",L"Could not save. Check that the plugin INI is writable."},
    {L"已保存并设为优先伴侣。",L"Preferred partner saved and applied."},
    {L"已恢复自动选择（PreferredPartnerKey=0）。",L"Automatic selection restored (PreferredPartnerKey=0)."},
    {L"请输入 1～30 个字符，不能包含换行或控制字符。",L"Enter 1–30 UTF-16 units, without line breaks or control characters."},
    {L"已重置名字；重新打开游戏中的交互可刷新显示。",L"Name reset. Reopen the in-game interaction to refresh."},
    {L"名字已保存；重新打开游戏中的交互可刷新显示。",L"Name saved. Reopen the in-game interaction to refresh."},
    {L"尚未读取到当前角色，请进入存档后再使用管理热键。",L"Load a save and use the manager hotkey again."},
    {L"语言保存失败，请检查插件 INI 是否可写。",L"Could not save the language. Check that the plugin INI is writable."},
    {L"语言设置",L"Language settings"},{L"伴侣 ",L"Partner "},
    {L"召唤所选伴侣(&U)",L"S&ummon selected"},
    {L"当前游戏版本的召唤入口暂不可用。",L"Summoning is unavailable for this game version."},
    {L"上一条召唤请求仍在处理中，请稍后重试。",L"A summon request is still pending. Please wait."},
    {L"未能提交召唤请求。请确认角色与婚姻关系后重试。",L"Could not submit the summon request. Reopen the manager and try again."}
};
static_assert(sizeof(translations)/sizeof(translations[0])==static_cast<unsigned>(UiText::Count));
std::atomic<UiLanguage> current{UiLanguage::Chinese};
SRWLOCK languageLock=SRWLOCK_INIT;
std::wstring settingsPath;
std::vector<HWND> windows;
struct Lock {
    Lock(){AcquireSRWLockExclusive(&languageLock);}
    ~Lock(){ReleaseSRWLockExclusive(&languageLock);}
};
UiLanguage readLanguage() {
    wchar_t value[32]{};
    if(!settingsPath.empty())GetPrivateProfileStringW(L"UI",L"Language",L"zh-CN",value,32,settingsPath.c_str());
    return _wcsicmp(value,L"en-US")==0?UiLanguage::English:UiLanguage::Chinese;
}
void publish(UiLanguage language,HWND source=nullptr) {
    if(current.exchange(language)==language)return;
    for(const auto window:windows)if(window!=source)PostMessageW(window,UiLanguageChangedMessage(),0,0);
}
}

void ConfigureUiLanguage(const wchar_t* iniPath) {
    Lock lock;settingsPath=iniPath?iniPath:L"";publish(readLanguage());
}
void ReloadUiLanguage(){Lock lock;publish(readLanguage());}
UiLanguage GetUiLanguage(){return current.load();}
const wchar_t* UiString(UiText text,UiLanguage language) {
    const auto index=static_cast<unsigned>(text);
    if(index>=static_cast<unsigned>(UiText::Count))return L"";
    return language==UiLanguage::English?translations[index].english:translations[index].chinese;
}
bool SaveUiLanguage(UiLanguage language,HWND source) {
    if(language!=UiLanguage::Chinese && language!=UiLanguage::English)return false;
    Lock lock;
    if(settingsPath.empty() || !WritePrivateProfileStringW(L"UI",L"Language",
        language==UiLanguage::English?L"en-US":L"zh-CN",settingsPath.c_str()))return false;
    publish(language,source);return true;
}
UINT UiLanguageChangedMessage() {
    static const UINT message=RegisterWindowMessageW(L"CrimsonDesert.CohabitationGateExperiment.UiLanguage.v1");return message;
}
void RegisterUiLanguageWindow(HWND window) {
    Lock lock;if(std::find(windows.begin(),windows.end(),window)==windows.end())windows.push_back(window);
}
void UnregisterUiLanguageWindow(HWND window) {
    Lock lock;windows.erase(std::remove(windows.begin(),windows.end(),window),windows.end());
}
void RefreshUiLanguageSelector(HWND window) {
    SendDlgItemMessageW(window,UiLanguageComboId,CB_SETCURSEL,GetUiLanguage()==UiLanguage::English?1:0,0);
}
void InitializeUiLanguageSelector(HWND window) {
    SendDlgItemMessageW(window,UiLanguageComboId,CB_RESETCONTENT,0,0);
    for(const auto text:{UiText::ChineseOption,UiText::EnglishOption})
        SendDlgItemMessageW(window,UiLanguageComboId,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(UiString(text)));
    RefreshUiLanguageSelector(window);
}
bool IsUiLanguageCommand(WPARAM command){return LOWORD(command)==UiLanguageComboId && HIWORD(command)==CBN_SELCHANGE;}
bool ApplyUiLanguageSelection(HWND window) {
    const auto selected=SendDlgItemMessageW(window,UiLanguageComboId,CB_GETCURSEL,0,0);
    const bool saved=(selected==0 || selected==1) && SaveUiLanguage(selected==1?UiLanguage::English:UiLanguage::Chinese,window);
    if(!saved)RefreshUiLanguageSelector(window);
    return saved;
}
void CenterUiDialog(HWND window,HWND owner) {
    RECT bounds{},anchor{};MONITORINFO monitor{sizeof(monitor)};
    if(!GetWindowRect(window,&bounds) || !GetMonitorInfoW(MonitorFromWindow(owner?owner:window,MONITOR_DEFAULTTONEAREST),&monitor))return;
    const auto& work=monitor.rcWork;anchor=work;
    if(owner)GetWindowRect(owner,&anchor);
    const auto width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    const auto x=(std::max)(work.left,(std::min)(anchor.left+(anchor.right-anchor.left-width)/2,work.right-width));
    const auto y=(std::max)(work.top,(std::min)(anchor.top+(anchor.bottom-anchor.top-height)/2,work.bottom-height));
    SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
}
