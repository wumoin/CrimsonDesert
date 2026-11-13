#define WIN32_LEAN_AND_MEAN
#include "BeautyPicker.h"
#include "PartnerManager.h"
#include "UiLanguage.h"
#include "../vendor/minhook/include/MinHook.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace {
BeautyUiTarget ui{};
MarriageCommitTarget marriage{};
NativeBeautyEntry original=nullptr;
PluginLog logger=nullptr;
volatile LONG active=0,lastChoice=0;
thread_local bool showing=false;
bool read(void* out,const void* at,std::size_t bytes) {
    SIZE_T got=0;return at && ReadProcessMemory(GetCurrentProcess(),at,out,bytes,&got) && got==bytes;
}
template<class T> bool field(const void* at,unsigned offset,T& out) {
    return at && offset<=65536 && read(&out,static_cast<const char*>(at)+offset,sizeof(out));
}
bool isPartnerBeauty(void* payload) {
    BYTE tag=0;DWORD count=0;void* outer=nullptr;
    if(!field(payload,0,tag) || tag!=0x15 || !field(payload,8,outer) || !field(payload,0x10,count) || count>32)return false;
    for(DWORD i=0;i<count;++i) {
        auto* item=static_cast<char*>(outer)+i*24;void* inner=nullptr;DWORD size=0;
        if(!field(item,0,tag) || tag!=0xe || !field(item,8,inner) || !field(item,0x10,size) || size>32)continue;
        for(DWORD j=0;j<size;++j) {
            auto* value=static_cast<char*>(inner)+j*16;void* string=nullptr;void* text=nullptr;
            char name[sizeof("BeautyLivingTogether")]{};
            if(field(value,0,tag) && tag==5 && field(value,8,string) && field(string,0,text) &&
                read(name,text,sizeof(name)) && !std::memcmp(name,"BeautyLivingTogether",sizeof(name)))return true;
        }
    }
    return false;
}
bool player(void*& actor,void*& component,std::uint16_t& playable) {
    void* world=nullptr;void* manager=nullptr;void* session=nullptr;void* controlled=nullptr;
    void* components=nullptr;void* status=nullptr;void* owner=nullptr;void* contents=nullptr;void* contentsOwner=nullptr;
    void* current=nullptr;std::uint16_t role=0xffff;
    // 原生美容分别读取当前操控实体与主玩家；切换主角后关系组件仍属于主玩家。
    if(!ui.managerPlayer || !read(&world,ui.clientWorld,sizeof(world)) || !field(world,ui.worldManager,manager) ||
        !field(manager,ui.managerSession,session) || !field(session,ui.sessionActor,controlled) || !controlled ||
        !field(controlled,marriage.npcKeyOffsets[0],components) || !field(components,marriage.npcKeyOffsets[1],status) ||
        !field(status,marriage.runtime.playableIndexOffset,role) || role==0xffff ||
        !field(manager,ui.managerPlayer,owner) || !owner || !field(owner,marriage.npcKeyOffsets[0],components) ||
        !field(components,marriage.runtime.contentsComponentOffset,contents) || !contents ||
        !field(contents,8,contentsOwner) || contentsOwner!=owner ||
        !field(session,ui.sessionActor,current) || current!=controlled ||
        !field(manager,ui.managerPlayer,current) || current!=owner)return false;
    actor=controlled;component=contents;playable=role;return true;
}
std::wstring partnerName(const BeautyPartnerOption& partner,unsigned index) {
    void* manager=nullptr;void* cache=nullptr;void* info=nullptr;void* name=nullptr;void* vt=nullptr;void* method=nullptr;
    DWORD count=0;
    if(ui.characterInfo && partner.character!=0xffff)info=ui.characterInfo(&partner.character);
    else if(ui.characters && read(&manager,ui.characters,8) && field(manager,8,count) && count<=65536 && partner.character<count &&
        field(manager,0x58,cache))field(cache,static_cast<unsigned>(partner.character)*8,info);
    if(field(info,ui.nameOffset,name) && field(name,0,vt) && field(vt,8,method)) {
        MEMORY_BASIC_INFORMATION page{};
        if(VirtualQuery(method,&page,sizeof(page)) && page.State==MEM_COMMIT &&
            (page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {
            using GetText=const char*(__fastcall*)(void*,bool);
            const char* text=reinterpret_cast<GetText>(method)(name,false);
            char bytes[256]{};unsigned n=0;
            if(text)for(;n+1<sizeof(bytes);++n)if(!read(bytes+n,text+n,1) || !bytes[n])break;
            bytes[n]=0;
            if(n) {
                const auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,-1,nullptr,0);
                if(length>1) {std::wstring value(static_cast<std::size_t>(length),L'\0');MultiByteToWideChar(CP_UTF8,0,bytes,-1,&value[0],length);value.resize(length-1);return value;}
            }
        }
    }
    return UiString(UiText::PartnerFallbackPrefix)+std::to_wstring(index+1);
}
struct Choice {
    std::vector<BeautyPartnerOption> partners;
    std::uint16_t playable=0xffff;
    std::vector<std::wstring> names;
    int initial=0,selected=-1;
};
void fillBeautyList(HWND window,Choice& data) {
    const auto previous=SendDlgItemMessageW(window,100,LB_GETCURSEL,0,0);
    const auto top=SendDlgItemMessageW(window,100,LB_GETTOPINDEX,0,0);
    data.names.clear();
    for(unsigned i=0;i<data.partners.size();++i)data.names.push_back(PartnerDisplayName(data.playable,data.partners[i],i));
    const auto originalNames=data.names;
    for(std::size_t i=0;i<data.names.size();++i) {
        bool duplicate=false;
        for(std::size_t j=0;j<data.names.size();++j)if(i!=j && originalNames[i]==originalNames[j])duplicate=true;
        if(duplicate)data.names[i]+=L" ["+std::to_wstring(data.partners[i].key)+L"]";
    }
    SendDlgItemMessageW(window,100,WM_SETREDRAW,FALSE,0);
    SendDlgItemMessageW(window,100,LB_RESETCONTENT,0,0);
    for(const auto& name:data.names)SendDlgItemMessageW(window,100,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
    SendDlgItemMessageW(window,100,LB_SETCURSEL,previous==LB_ERR?data.initial:previous,0);
    SendDlgItemMessageW(window,100,LB_SETTOPINDEX,top==LB_ERR?0:top,0);
    SendDlgItemMessageW(window,100,WM_SETREDRAW,TRUE,0);InvalidateRect(GetDlgItem(window,100),nullptr,TRUE);
}
void refreshBeautyLanguage(HWND window,Choice& data) {
    SetWindowTextW(window,UiString(UiText::BeautyTitle));
    SetDlgItemTextW(window,UiLanguageLabelId,UiString(UiText::LanguageLabel));
    SetDlgItemTextW(window,101,UiString(UiText::BeautyHelp));
    SetDlgItemTextW(window,IDOK,UiString(UiText::Confirm));
    SetDlgItemTextW(window,IDCANCEL,UiString(UiText::Cancel));
    RefreshUiLanguageSelector(window);fillBeautyList(window,data);
}
INT_PTR CALLBACK dialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<Choice*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        data=reinterpret_cast<Choice*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);
        InitializeUiLanguageSelector(window);RegisterUiLanguageWindow(window);refreshBeautyLanguage(window,*data);
        CenterUiDialog(window,GetParent(window));
        SetFocus(GetDlgItem(window,100));return FALSE;
    }
    if(message==WM_DESTROY){UnregisterUiLanguageWindow(window);return TRUE;}
    if(message==UiLanguageChangedMessage() && data){refreshBeautyLanguage(window,*data);return TRUE;}
    if(message==WM_CLOSE) {EndDialog(window,IDCANCEL);return TRUE;}
    if(message==WM_COMMAND && data) {
        const auto id=LOWORD(wp);
        if(IsUiLanguageCommand(wp)) {
            if(ApplyUiLanguageSelection(window))refreshBeautyLanguage(window,*data);
            else MessageBoxW(window,UiString(UiText::LanguageSaveFailed),UiString(UiText::LanguageErrorTitle),MB_OK|MB_ICONWARNING);
            return TRUE;
        }
        if(id==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
        if(id==IDOK && GetFocus()==GetDlgItem(window,UiLanguageComboId))return TRUE;
        if(id==IDOK || (id==100 && HIWORD(wp)==LBN_DBLCLK)) {
            const auto selected=SendDlgItemMessageW(window,100,LB_GETCURSEL,0,0);
            if(selected!=LB_ERR && selected>=0 && static_cast<std::size_t>(selected)<data->names.size()) {
                data->selected=static_cast<int>(selected);EndDialog(window,IDOK);
            }
            return TRUE;
        }
    }
    return FALSE;
}
void word(std::vector<BYTE>& out,WORD value){out.push_back(static_cast<BYTE>(value));out.push_back(static_cast<BYTE>(value>>8));}
void dword(std::vector<BYTE>& out,DWORD value){word(out,static_cast<WORD>(value));word(out,static_cast<WORD>(value>>16));}
void text(std::vector<BYTE>& out,const wchar_t* value){do{word(out,static_cast<WORD>(*value));}while(*value++);}
void control(std::vector<BYTE>& out,DWORD style,short x,short y,short w,short h,WORD id,WORD type,const wchar_t* title) {
    while(out.size()%4)out.push_back(0);
    dword(out,style|WS_CHILD|WS_VISIBLE);dword(out,0);word(out,x);word(out,y);word(out,w);word(out,h);word(out,id);
    word(out,0xffff);word(out,type);text(out,title);word(out,0);
}
std::vector<BYTE> beautyLayout() {
    std::vector<BYTE> layout;
    dword(layout,WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT);dword(layout,0);word(layout,6);
    word(layout,0);word(layout,0);word(layout,290);word(layout,249);word(layout,0);word(layout,0);text(layout,UiString(UiText::BeautyTitle));
    word(layout,9);text(layout,L"Microsoft YaHei UI");
    control(layout,0,90,10,90,14,UiLanguageLabelId,0x82,UiString(UiText::LanguageLabel));
    control(layout,WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,180,7,100,80,UiLanguageComboId,0x85,L"");
    control(layout,0,10,33,270,16,101,0x82,UiString(UiText::BeautyHelp));
    control(layout,WS_BORDER|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,10,52,270,158,100,0x83,L"");
    control(layout,WS_TABSTOP|BS_DEFPUSHBUTTON,160,222,55,18,IDOK,0x80,UiString(UiText::Confirm));
    control(layout,WS_TABSTOP,225,222,55,18,IDCANCEL,0x80,UiString(UiText::Cancel));
    return layout;
}
DWORD choose(const std::vector<BeautyPartnerOption>& partners,std::uint16_t playable) {
    Choice data;data.partners=partners;data.playable=playable;
    const auto previous=static_cast<DWORD>(InterlockedCompareExchange(&lastChoice,0,0));
    for(unsigned i=0;i<partners.size();++i)if(partners[i].key==previous)data.initial=static_cast<int>(i);
    const auto layout=beautyLayout();
    HWND owner=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(owner,&pid);
    if(pid!=GetCurrentProcessId())owner=nullptr;
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),owner,dialog,reinterpret_cast<LPARAM>(&data));
    if(result==IDOK && data.selected>=0) {
        const auto key=partners[static_cast<std::size_t>(data.selected)].key;
        InterlockedExchange(&lastChoice,static_cast<LONG>(key));return key;
    }
    return 0;
}
void __fastcall entry(void* self,void* a2,void* a3,void* payload) {
    if(showing || !InterlockedCompareExchange(&active,0,0) || !isPartnerBeauty(payload)) {original(self,a2,a3,payload);return;}
    struct Showing {Showing(){showing=true;}~Showing(){showing=false;}} showingScope;
    void* actor=nullptr;void* component=nullptr;std::uint16_t playable=0;
    std::vector<BeautyPartnerOption> partners;
    if(!player(actor,component,playable) || !ReadBeautyPartners(component,playable,partners) || partners.empty()) {
        if(logger)logger("BEAUTY PICKER BLOCKED: no verified current player/partner list.");return;
    }
    if(logger){char line[128];sprintf_s(line,"BEAUTY PICKER OPEN: playable=%u partners=%zu",static_cast<unsigned>(playable),partners.size());logger(line);}
    const auto key=choose(partners,playable);
    if(!key){if(logger)logger("BEAUTY PICKER CANCELLED: native editor was not opened.");return;}
    void* currentActor=nullptr;void* currentComponent=nullptr;std::uint16_t currentPlayable=0;
    if(!player(currentActor,currentComponent,currentPlayable) || currentActor!=actor || currentComponent!=component || currentPlayable!=playable ||
        !VerifyBeautyPartner(component,playable,key)) {if(logger)logger("BEAUTY PICKER BLOCKED: selected partner/context changed.");return;}
    const BYTE reset=0;SIZE_T written=0;
    if(!WriteProcessMemory(GetCurrentProcess(),static_cast<char*>(self)+ui.modeOffset,&reset,1,&written) || written!=1)return;
    if(logger){char line[128];sprintf_s(line,"BEAUTY PICKER SELECTED: key=%lu; entering original editor.",key);logger(line);}
    RunBeautyForPartner(original,self,a2,a3,payload,component,playable,key);
}
}
bool ReadPartnerUiContext(PartnerUiContext& context) {
    context={};return player(context.actor,context.component,context.playable);
}
std::wstring ReadPartnerOriginalName(const BeautyPartnerOption& partner,unsigned index) {return partnerName(partner,index);}
void SetBeautyPickerEnabled(bool enabled){InterlockedExchange(&active,enabled?1:0);}
bool InstallBeautyPicker(HMODULE module,const MarriageCommitTarget& target,PluginLog log) {
    logger=log;marriage=target;
    if(!FindBeautyUiTarget(module,target,ui)) {if(logger)logger("BEAUTY PICKER UNAVAILABLE: native entry/player ABI not uniquely verified.");return false;}
    InstallPartnerManager(module,target);
    auto status=MH_CreateHook(reinterpret_cast<void*>(ui.entry),reinterpret_cast<void*>(&entry),reinterpret_cast<void**>(&original));
    if(status==MH_OK) {status=MH_EnableHook(reinterpret_cast<void*>(ui.entry));if(status!=MH_OK)MH_RemoveHook(reinterpret_cast<void*>(ui.entry));}
    if(logger){char line[192];sprintf_s(line,"BEAUTY PICKER HOOK: status=%s names=%d; native editor/target routing, plugin selection window.",MH_StatusToString(status),ui.characters!=nullptr || ui.characterInfo!=nullptr);logger(line);}
    return status==MH_OK;
}
