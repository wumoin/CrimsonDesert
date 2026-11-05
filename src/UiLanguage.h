#pragma once
#include <windows.h>

enum class UiLanguage : unsigned { Chinese,English };
enum class UiText : unsigned {
    LanguageLabel,ChineseOption,EnglishOption,ManagerTitle,BeautyTitle,ManagerHelp,BeautyHelp,
    SetPreferred,Automatic,NameLabel,SaveName,ResetName,Close,Confirm,Cancel,
    PreferredAutomatic,PreferredIdPrefix,PreferredMark,OriginalNamePrefix,EmptyPartners,
    StatusDefault,NamesUnavailable,PreferenceDisabled,ContextChanged,PreferenceNeedsCommit,
    ConfigSaveFailed,PreferredSaved,AutomaticSaved,InvalidName,NameReset,NameSaved,NotLoaded,
    LanguageSaveFailed,LanguageErrorTitle,PartnerFallbackPrefix,
    SummonSelected,SummonUnavailable,SummonBusy,SummonFailed,Count
};
constexpr WORD UiLanguageLabelId=500,UiLanguageComboId=501;
void ConfigureUiLanguage(const wchar_t* iniPath);
void ReloadUiLanguage();
UiLanguage GetUiLanguage();
const wchar_t* UiString(UiText text,UiLanguage language=GetUiLanguage());
bool SaveUiLanguage(UiLanguage language,HWND source=nullptr);
UINT UiLanguageChangedMessage();
void RegisterUiLanguageWindow(HWND window);
void UnregisterUiLanguageWindow(HWND window);
void InitializeUiLanguageSelector(HWND window);
void RefreshUiLanguageSelector(HWND window);
bool IsUiLanguageCommand(WPARAM command);
// 保存成功后由调用者刷新自身；其他已打开的弹窗通过消息刷新。
bool ApplyUiLanguageSelection(HWND window);
void CenterUiDialog(HWND window,HWND owner);
