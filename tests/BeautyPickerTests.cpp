// 仅测试原生选择窗口，使用模拟伴侣和临时 INI；不连接游戏。
#include "../src/BeautyPicker.cpp"
#include <cassert>

namespace {
std::wstring testIni;
int finishMode=0;
unsigned nativeEditorCalls=0;
Choice previewChoices() {
    Choice data;data.playable=0;
    data.partners={{435,3006,0},{1178,1036,0},{1179,2211,0},{1180,2211,0}};
    for(DWORD key=10000;key<10036;++key)data.partners.push_back({key,50,0});
    return data;
}
std::wstring windowText(HWND window,int id=0) {
    wchar_t value[256]{};
    if(id)GetDlgItemTextW(window,id,value,256);else GetWindowTextW(window,value,256);
    return value;
}
void changeLanguage(HWND window,UiLanguage language) {
    SendDlgItemMessageW(window,UiLanguageComboId,CB_SETCURSEL,language==UiLanguage::English?1:0,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(UiLanguageComboId,CBN_SELCHANGE),0);
}
void dispatchLanguage(HWND window) {
    MSG message{};
    while(PeekMessageW(&message,window,UiLanguageChangedMessage(),UiLanguageChangedMessage(),PM_REMOVE))DispatchMessageW(&message);
}
void CALLBACK driveBeauty(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    auto* data=reinterpret_cast<Choice*>(GetWindowLongPtrW(window,DWLP_USER));assert(data);
    assert(data->names[0]==L"同名 [435]" && data->names[1]==L"同名 [1178]");
    assert(data->names[2]==L"小美" && data->names[3]==L"Alice");
    SendDlgItemMessageW(window,100,LB_SETCURSEL,20,0);SendDlgItemMessageW(window,100,LB_SETTOPINDEX,12,0);
    const auto top=SendDlgItemMessageW(window,100,LB_GETTOPINDEX,0,0);
    Choice second=previewChoices();second.initial=3;const auto layout=beautyLayout();
    const auto other=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),
        window,dialog,reinterpret_cast<LPARAM>(&second));assert(other);
    ShowWindow(other,SW_SHOWNOACTIVATE);
    for(const auto language:{UiLanguage::English,UiLanguage::Chinese}) {
        changeLanguage(window,language);dispatchLanguage(other);
        assert(windowText(window)==UiString(UiText::BeautyTitle) && windowText(other)==UiString(UiText::BeautyTitle));
        assert(windowText(window,IDOK)==UiString(UiText::Confirm) && windowText(window,IDCANCEL)==UiString(UiText::Cancel));
        assert(SendDlgItemMessageW(window,100,LB_GETCURSEL,0,0)==20 && SendDlgItemMessageW(window,100,LB_GETTOPINDEX,0,0)==top);
        assert(SendDlgItemMessageW(other,100,LB_GETCURSEL,0,0)==3);
        assert(data->selected==-1 && second.selected==-1 && nativeEditorCalls==0);
        assert(data->names[2]==L"小美" && data->names[3]==L"Alice");
        assert(data->names[20]==UiString(UiText::PartnerFallbackPrefix)+std::to_wstring(21));
    }
    assert(DestroyWindow(other));
    const auto attributes=GetFileAttributesW(testIni.c_str());assert(SetFileAttributesW(testIni.c_str(),attributes|FILE_ATTRIBUTE_READONLY));
    SendDlgItemMessageW(window,UiLanguageComboId,CB_SETCURSEL,1,0);
    assert(!ApplyUiLanguageSelection(window));
    assert(GetUiLanguage()==UiLanguage::Chinese && SendDlgItemMessageW(window,UiLanguageComboId,CB_GETCURSEL,0,0)==0);
    assert(SetFileAttributesW(testIni.c_str(),attributes));
    assert(WritePrivateProfileStringW(L"UI",L"Language",L"en-US",testIni.c_str()));ReloadUiLanguage();dispatchLanguage(window);
    assert(windowText(window)==UiString(UiText::BeautyTitle,UiLanguage::English));
    assert(SendDlgItemMessageW(window,100,LB_GETCURSEL,0,0)==20 && data->selected==-1);
    SetFocus(GetDlgItem(window,UiLanguageComboId));SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,0),0);
    assert(IsWindow(window) && data->selected==-1 && nativeEditorCalls==0);
    SetFocus(GetDlgItem(window,100));
    if(finishMode==0)SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);
    else if(finishMode==1)SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_DBLCLK),0);
    else SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),0);
}
INT_PTR CALLBACK testBeautyDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    const auto handled=dialog(window,message,wp,lp);
    if(message==WM_INITDIALOG)SetTimer(window,9,50,&driveBeauty);
    return handled;
}
void testBeautyFlow() {
    for(finishMode=0;finishMode<3;++finishMode) {
        assert(SaveUiLanguage(UiLanguage::Chinese));Choice data=previewChoices();const auto layout=beautyLayout();
        const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),
            nullptr,testBeautyDialog,reinterpret_cast<LPARAM>(&data));
        assert(result==(finishMode==2?IDCANCEL:IDOK));assert(data.selected==(finishMode==2?-1:20));
        assert(nativeEditorCalls==0);
    }
    assert(SaveUiLanguage(UiLanguage::Chinese));
    assert(ReadPartnerOriginalName({},0)==L"伴侣 1");assert(SaveUiLanguage(UiLanguage::English));
    assert(ReadPartnerOriginalName({},0)==L"Partner 1");
    puts("PASS: beauty language sync/reload, names/duplicates/fallbacks, selection/scroll, failed-save rollback, confirm/double-click/cancel.");
}
}

std::wstring PartnerDisplayName(std::uint16_t,const BeautyPartnerOption& partner,unsigned index) {
    if(partner.key==435 || partner.key==1178)return L"同名";
    if(partner.key==1179)return L"小美";
    if(partner.key==1180)return L"Alice";
    return ReadPartnerOriginalName(partner,index);
}
bool ReadBeautyPartners(void*,std::uint16_t,std::vector<BeautyPartnerOption>&){return false;}
bool VerifyBeautyPartner(void*,std::uint16_t,DWORD){return false;}
void RunBeautyForPartner(NativeBeautyEntry,void*,void*,void*,void*,void*,std::uint16_t,DWORD){++nativeEditorCalls;}
bool FindBeautyUiTarget(HMODULE,const MarriageCommitTarget&,BeautyUiTarget&){return false;}
void InstallPartnerManager(HMODULE,const MarriageCommitTarget&){}

int wmain(int argc,wchar_t** argv) {
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    testIni=std::wstring(temp)+L"CrimsonDesertBeautyTests-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    ConfigureUiLanguage(testIni.c_str());
#ifdef PARTNER_BEAUTY_PREVIEW
    (void)argc;(void)argv;
    const bool preview=true;
#else
    const bool preview=argc>1 && !wcscmp(argv[1],L"--preview");
#endif
    if(preview) {
        Choice data=previewChoices();const auto layout=beautyLayout();
        assert(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),
            nullptr,dialog,reinterpret_cast<LPARAM>(&data))!=-1);
    } else testBeautyFlow();
    DeleteFileW(testIni.c_str());puts("All beauty picker tests passed.");return 0;
}
