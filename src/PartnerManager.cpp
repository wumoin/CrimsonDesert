#define WIN32_LEAN_AND_MEAN
#include "PartnerManager.h"
#include "UiLanguage.h"
#include "PartnerHotkey.h"
#include "PartnerSummon.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr WORD listId=100, preferenceId=101, automaticId=102, nameId=103;
constexpr WORD saveNameId=104, resetNameId=105, statusId=106, preferenceTextId=107, originalNameId=108;
constexpr WORD summonId=109;
constexpr unsigned maxNameLength=30;
using NameId=std::tuple<std::uint16_t,DWORD,std::uint16_t>;
std::map<NameId,std::wstring> names;
SRWLOCK namesLock=SRWLOCK_INIT;
MarriageCommitTarget marriage{};
PartnerNameTarget nativeNames{};
PluginLog logger=nullptr;
SavePreferredPartner savePreference=nullptr;
std::wstring settingsPath;
volatile LONG enabled=0,preferenceEnabled=0;
std::atomic<HWND> gameWindow{nullptr},managerWindow{nullptr};
constexpr wchar_t previousProcProperty[]=L"CohabitationGateExperiment.PartnerManager.PreviousWndProc";
UINT dispatchMessage=0;
std::atomic<bool> uiReady{false};
bool keyWasDown=false;
PartnerHotkey polledHotkey{};
bool hotkeyConfigValid=true;
ULONGLONG lastUpdate=0;

std::wstring managerTitle(UiLanguage language=GetUiLanguage()) {
    return std::wstring(UiString(UiText::ManagerTitle,language))+L" · "+PartnerHotkeyLabel();
}
std::string hotkeyLogLabel() {
    const auto label=PartnerHotkeyLabel();std::string result;
    for(const auto character:label)result+=static_cast<char>(character); // 热键标签只包含 ASCII。
    return result;
}

bool read(void* out,const void* at,std::size_t size) {
    SIZE_T copied=0;
    return at && ReadProcessMemory(GetCurrentProcess(),at,out,size,&copied) && copied==size;
}
template<class T> bool field(const void* at,unsigned offset,T& out) {
    return at && offset<=65536 && read(&out,static_cast<const char*>(at)+offset,sizeof(out));
}
NameId nameIdFor(std::uint16_t playable,const BeautyPartnerOption& partner) {
    return {playable,partner.key,partner.character};
}
std::wstring customName(std::uint16_t playable,const BeautyPartnerOption& partner) {
    AcquireSRWLockShared(&namesLock);
    const auto it=names.find(nameIdFor(playable,partner));
    std::wstring value=it==names.end()?L"":it->second;
    ReleaseSRWLockShared(&namesLock);
    return value;
}
bool normalizeName(std::wstring& value) {
    const auto first=value.find_first_not_of(L" \t\r\n\u3000");
    if(first==std::wstring::npos)return false;
    const auto last=value.find_last_not_of(L" \t\r\n\u3000");
    value=value.substr(first,last-first+1);
    if(value.size()>maxNameLength)return false;
    for(std::size_t i=0;i<value.size();++i) {
        const auto c=static_cast<unsigned>(value[i]);
        if(c<32 || (c>=0x7f && c<=0x9f))return false;
        if(c>=0xd800 && c<=0xdbff) {
            if(++i>=value.size() || value[i]<0xdc00 || value[i]>0xdfff)return false;
        } else if(c>=0xdc00 && c<=0xdfff)return false;
    }
    return true;
}
// 十六进制保存 UTF-16，防止 INI 对引号、分号和前后空格的解释改变用户名字。
std::wstring encodeName(const std::wstring& value) {
    constexpr wchar_t digits[]=L"0123456789ABCDEF";std::wstring encoded=L"v1:";
    for(const auto c:value)for(int shift=12;shift>=0;shift-=4)encoded+=digits[(c>>shift)&15];
    return encoded;
}
bool decodeName(const std::wstring& encoded,std::wstring& value) {
    value.clear();
    if(encoded.compare(0,3,L"v1:") || encoded.size()<=3 || (encoded.size()-3)%4 || encoded.size()>3+maxNameLength*4)return false;
    for(std::size_t i=3;i<encoded.size();i+=4) {
        unsigned c=0;
        for(unsigned j=0;j<4;++j) {
            const auto digit=encoded[i+j];unsigned n=16;
            if(digit>=L'0' && digit<=L'9')n=digit-L'0';
            else if(digit>=L'A' && digit<=L'F')n=digit-L'A'+10;
            else if(digit>=L'a' && digit<=L'f')n=digit-L'a'+10;
            if(n==16)return false;c=(c<<4)|n;
        }
        value+=static_cast<wchar_t>(c);
    }
    auto normalized=value;
    return normalizeName(normalized) && normalized==value;
}
std::wstring storedKey(std::uint16_t playable,const BeautyPartnerOption& partner) {
    return std::to_wstring(playable)+L"."+std::to_wstring(partner.key)+L"."+std::to_wstring(partner.character);
}
void loadNames() {
    std::vector<wchar_t> section(65536);
    const auto length=GetPrivateProfileSectionW(L"PartnerNames",section.data(),static_cast<DWORD>(section.size()),settingsPath.c_str());
    if(length>=section.size()-2){if(logger)logger("PARTNER NAMES REFUSED: name section exceeds limit.");return;}
    for(const wchar_t* at=section.data();*at;at+=wcslen(at)+1) {
        unsigned playable=0,character=0;unsigned long key=0;int consumed=0;
        if(swscanf_s(at,L"%u.%lu.%u=%n",&playable,&key,&character,&consumed)!=3 || consumed<=0 ||
            playable>=64 || !key || key==MAXDWORD || character>=0xffff)continue;
        std::wstring value;
        if(decodeName(at+consumed,value))names[{static_cast<std::uint16_t>(playable),key,static_cast<std::uint16_t>(character)}]=value;
    }
}
bool saveName(std::uint16_t playable,const BeautyPartnerOption& partner,const std::wstring& value) {
    if(settingsPath.empty() || playable>=64 || !partner.key || partner.key==MAXDWORD || partner.character==0xffff)return false;
    // 名字编码只包含 ASCII，可写入现有 INI，不改变其编码和中文注释。
    const auto key=storedKey(playable,partner),encoded=encodeName(value);
    if(!WritePrivateProfileStringW(L"PartnerNames",key.c_str(),value.empty()?nullptr:encoded.c_str(),settingsPath.c_str()))return false;
    AcquireSRWLockExclusive(&namesLock);
    if(value.empty())names.erase(nameIdFor(playable,partner));else names[nameIdFor(playable,partner)]=value;
    ReleaseSRWLockExclusive(&namesLock);
    if(logger) {
        char line[160];sprintf_s(line,"PARTNER NAME %s: playable=%u key=%lu character=%u; plugin name configuration saved.",
            value.empty()?"RESET":"SAVED",static_cast<unsigned>(playable),partner.key,static_cast<unsigned>(partner.character));logger(line);
    }
    return true;
}

void* clientActor(DWORD id) {
    if(!id || !nativeNames.clientWorld)return nullptr;
    void* world=nullptr;void* manager=nullptr;void* registry=nullptr;void* buckets=nullptr;void* entries=nullptr;
    DWORD count=0;
    if(!read(&world,nativeNames.clientWorld,sizeof(world)) || !field(world,nativeNames.managerOffset,manager) ||
        !field(manager,nativeNames.registryOffset,registry) || !field(registry,nativeNames.bucketCountOffset,count) ||
        !count || count>65536 || !field(registry,nativeNames.bucketsOffset,buckets) || !buckets ||
        !field(registry,nativeNames.entriesOffset,entries) || !entries)return nullptr;
    const auto bucket=static_cast<const BYTE*>(buckets)+(id%count)*256ull;
    DWORD items=0;if(!field(bucket,0,items) || items>31)return nullptr;
    for(DWORD i=0;i<items;++i) {
        DWORD key=0,index=0;
        if(!field(bucket,8+i*8,key) || !field(bucket,12+i*8,index))return nullptr;
        if(key!=id)continue;
        void* entry=nullptr;void* actor=nullptr;DWORD actual=0;
        if(index>1000000 || !read(&entry,static_cast<const BYTE*>(entries)+index*8ull,8) ||
            !field(entry,4,actual) || actual!=id || !field(entry,8,actor) ||
            !field(actor,marriage.runtime.actorIdOffset,actual) || actual!=id)return nullptr;
        return actor;
    }
    return nullptr;
}
bool nameField(void* actor,const BeautyPartnerOption& partner,void*& status) {
    void* components=nullptr;void* save=nullptr;DWORD key=0;std::uint16_t character=0xffff;
    return field(actor,marriage.npcKeyOffsets[0],components) && field(components,nativeNames.statusOffset,status) &&
        field(status,0x30,character) && character==partner.character &&
        field(status,marriage.npcKeyOffsets[2],save) && field(save,marriage.npcKeyOffsets[3],key) && key==partner.key;
}
struct AppliedName {
    void* actor=nullptr;void* status=nullptr;
    std::uint16_t playable=0xffff;
    BeautyPartnerOption partner;
    std::wstring original,value;
};
std::vector<AppliedName> appliedNames;
bool readNativeName(void* status,std::wstring& value) {
    void* string=nullptr;const wchar_t* text=nullptr;DWORD length=0;
    if(!field(status,nativeNames.nameOffset,string) || !field(string,0,text) || !text || !field(string,8,length))return false;
    // 原生字符串以 MAXDWORD 表示长度尚未缓存，并非无效名字。
    // 普通 NPC 的默认名字经常使用这种格式，逐个字符读取到终止符并限制长度。
    if(length==MAXDWORD) {
        std::wstring observed;
        for(unsigned i=0;i<=256;++i) {
            wchar_t character=0;
            if(!read(&character,text+i,sizeof(character)))return false;
            if(!character){value=std::move(observed);return true;}
            if(i==256)return false;
            observed+=character;
        }
        return false;
    }
    if(length>256)return false;
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length)+1);
    if(!read(buffer.data(),text,buffer.size()*sizeof(wchar_t)) || buffer.back())return false;
    value.assign(buffer.data(),length);return true;
}
bool assignName(void* status,const std::wstring& value) {
    // 仅操作客户端实例的原生显示名；游戏字符串的分配和引用计数仍由原函数负责。
    nativeNames.assign(static_cast<BYTE*>(status)+nativeNames.nameOffset,value.c_str());
    std::wstring current;return readNativeName(status,current) && current==value;
}
void logAppliedName(const char* action,const AppliedName& item) {
    if(!logger)return;
    char line[192];sprintf_s(line,"PARTNER NAME %s: playable=%u key=%lu character=%u actor=0x%08lx; native client name verified.",
        action,static_cast<unsigned>(item.playable),item.partner.key,static_cast<unsigned>(item.partner.character),item.partner.actorId);
    logger(line);
}
void applyNames() {
    if(!nativeNames.assign)return;
    PartnerUiContext context{};std::vector<BeautyPartnerOption> partners;
    const bool contextValid=ReadPartnerUiContext(context) && ReadBeautyPartners(context.component,context.playable,partners);
    const bool apply=InterlockedCompareExchange(&enabled,0,0)!=0 && contextValid;
    for(auto it=appliedNames.begin();it!=appliedNames.end();) {
        void* status=nullptr;
        if(clientActor(it->partner.actorId)!=it->actor || !nameField(it->actor,it->partner,status) || status!=it->status) {
            it=appliedNames.erase(it);continue;
        }
        const auto found=std::find_if(partners.begin(),partners.end(),[&](const auto& partner) {
            return partner.key==it->partner.key && partner.character==it->partner.character && partner.actorId==it->partner.actorId;
        });
        const auto desired=apply && context.playable==it->playable && found!=partners.end()?customName(context.playable,*found):L"";
        std::wstring current;
        if(!readNativeName(status,current)){++it;continue;}
        if(desired.empty()) {
            // 原版已更新名字时保留其新值；只还原本插件最后一次写入的值。
            if(current==it->value) {
                if(!assignName(status,it->original)){++it;continue;}
                logAppliedName("RESTORED",*it);
            }
            it=appliedNames.erase(it);continue;
        }
        if(current!=desired) {
            if(!assignName(status,desired)){++it;continue;}
            logAppliedName("APPLIED",*it);
        }
        it->value=desired;++it;
    }
    if(!apply)return;
    for(const auto& partner:partners) {
        const auto value=customName(context.playable,partner);
        if(value.empty() || !partner.actorId)continue;
        void* actor=clientActor(partner.actorId);void* status=nullptr;
        if(!actor || !nameField(actor,partner,status))continue;
        if(std::any_of(appliedNames.begin(),appliedNames.end(),[&](const auto& item){return item.status==status;}))continue;
        AppliedName item{actor,status,context.playable,partner,L"",value};
        if(!readNativeName(status,item.original))continue;
        if(assignName(status,value)){logAppliedName("APPLIED",item);appliedNames.push_back(std::move(item));}
    }
}

void word(std::vector<BYTE>& out,WORD v){out.push_back(static_cast<BYTE>(v));out.push_back(static_cast<BYTE>(v>>8));}
void dword(std::vector<BYTE>& out,DWORD v){word(out,static_cast<WORD>(v));word(out,static_cast<WORD>(v>>16));}
void text(std::vector<BYTE>& out,const wchar_t* v){do{word(out,static_cast<WORD>(*v));}while(*v++);}
void control(std::vector<BYTE>& out,DWORD style,short x,short y,short w,short h,WORD id,WORD type,const wchar_t* title) {
    while(out.size()%4)out.push_back(0);
    dword(out,style|WS_CHILD|WS_VISIBLE);dword(out,0);word(out,x);word(out,y);word(out,w);word(out,h);word(out,id);
    word(out,0xffff);word(out,type);text(out,title);word(out,0);
}
std::vector<BYTE> dialogLayout() {
    std::vector<BYTE> layout;
    dword(layout,WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT);dword(layout,0);word(layout,15);
    word(layout,0);word(layout,0);word(layout,360);word(layout,339);word(layout,0);word(layout,0);text(layout,managerTitle().c_str());
    word(layout,10);text(layout,L"Microsoft YaHei UI");
    control(layout,0,152,10,90,14,UiLanguageLabelId,0x82,UiString(UiText::LanguageLabel));
    control(layout,WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,244,7,104,80,UiLanguageComboId,0x85,L"");
    control(layout,0,12,34,336,14,200,0x82,UiString(UiText::ManagerHelp));
    control(layout,WS_BORDER|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,12,53,336,105,listId,0x83,L"");
    control(layout,0,12,166,336,24,preferenceTextId,0x82,L"");
    control(layout,WS_TABSTOP,12,193,104,22,preferenceId,0x80,UiString(UiText::SetPreferred));
    control(layout,WS_TABSTOP,126,193,104,22,automaticId,0x80,UiString(UiText::Automatic));
    control(layout,WS_TABSTOP,240,193,108,22,summonId,0x80,UiString(UiText::SummonSelected));
    control(layout,0,12,225,336,14,originalNameId,0x82,L"");
    control(layout,0,12,246,42,14,201,0x82,UiString(UiText::NameLabel));
    control(layout,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,56,243,292,20,nameId,0x81,L"");
    control(layout,WS_TABSTOP|BS_DEFPUSHBUTTON,12,270,104,22,saveNameId,0x80,UiString(UiText::SaveName));
    control(layout,WS_TABSTOP,126,270,104,22,resetNameId,0x80,UiString(UiText::ResetName));
    control(layout,0,12,301,251,25,statusId,0x82,UiString(UiText::StatusDefault));
    control(layout,WS_TABSTOP,278,304,70,22,IDCANCEL,0x80,UiString(UiText::Close));
    return layout;
}
struct ManagerDialog {
    PartnerUiContext context;
    std::vector<BeautyPartnerOption> partners;
    int selected=-1;
    DWORD preferred=0;
    bool stale=false;
    UiText status=UiText::StatusDefault;
};
void setStatus(HWND window,UiText message) {
    if(auto* data=reinterpret_cast<ManagerDialog*>(GetWindowLongPtrW(window,DWLP_USER)))data->status=message;
    SetDlgItemTextW(window,statusId,UiString(message));
}
void updatePreference(HWND window,ManagerDialog& data) {
    const auto label=data.preferred?UiString(UiText::PreferredIdPrefix)+std::to_wstring(data.preferred):std::wstring(UiString(UiText::PreferredAutomatic));
    SetDlgItemTextW(window,preferenceTextId,label.c_str());
}
void fillList(HWND window,ManagerDialog& data) {
    const auto top=SendDlgItemMessageW(window,listId,LB_GETTOPINDEX,0,0);
    SendDlgItemMessageW(window,listId,WM_SETREDRAW,FALSE,0);
    SendDlgItemMessageW(window,listId,LB_RESETCONTENT,0,0);
    for(unsigned i=0;i<data.partners.size();++i) {
        const auto& partner=data.partners[i];
        auto label=PartnerDisplayName(data.context.playable,partner,i)+L"  ["+std::to_wstring(partner.key)+L"]";
        if(partner.key==data.preferred)label+=UiString(UiText::PreferredMark);
        SendDlgItemMessageW(window,listId,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendDlgItemMessageW(window,listId,LB_SETCURSEL,data.selected,0);
    SendDlgItemMessageW(window,listId,LB_SETTOPINDEX,top==LB_ERR?0:top,0);
    SendDlgItemMessageW(window,listId,WM_SETREDRAW,TRUE,0);InvalidateRect(GetDlgItem(window,listId),nullptr,TRUE);
    updatePreference(window,data);
}
void updateOriginalName(HWND window,const ManagerDialog& data) {
    if(data.selected<0){SetDlgItemTextW(window,originalNameId,UiString(UiText::EmptyPartners));return;}
    const auto& partner=data.partners[static_cast<std::size_t>(data.selected)];
    const auto original=ReadPartnerOriginalName(partner,static_cast<unsigned>(data.selected));
    SetDlgItemTextW(window,originalNameId,(UiString(UiText::OriginalNamePrefix)+original).c_str());
}
void refreshManagerLanguage(HWND window,ManagerDialog& data) {
    SetWindowTextW(window,managerTitle().c_str());
    struct Label{WORD id;UiText text;};
    for(const auto& label:{Label{UiLanguageLabelId,UiText::LanguageLabel},{200,UiText::ManagerHelp},
        {preferenceId,UiText::SetPreferred},{automaticId,UiText::Automatic},{201,UiText::NameLabel},
        {saveNameId,UiText::SaveName},{resetNameId,UiText::ResetName},{summonId,UiText::SummonSelected},{IDCANCEL,UiText::Close}})
        SetDlgItemTextW(window,label.id,UiString(label.text));
    RefreshUiLanguageSelector(window);fillList(window,data);updateOriginalName(window,data);
    SetDlgItemTextW(window,statusId,UiString(data.status));
    // 不调用 updateSelection：语言切换不重填名字框，保留未保存输入和编辑选区。
}
void updateSummonState(HWND window,const ManagerDialog& data) {
    const bool valid=data.selected>=0 && !data.stale && InterlockedCompareExchange(&enabled,0,0);
    EnableWindow(GetDlgItem(window,summonId),valid && IsPartnerSummonReady() && !IsPartnerSummonBusy());
}
void updateSelection(HWND window,ManagerDialog& data) {
    const auto selected=SendDlgItemMessageW(window,listId,LB_GETCURSEL,0,0);
    data.selected=selected>=0 && static_cast<std::size_t>(selected)<data.partners.size()?static_cast<int>(selected):-1;
    const bool valid=data.selected>=0 && !data.stale && InterlockedCompareExchange(&enabled,0,0);
    EnableWindow(GetDlgItem(window,preferenceId),valid && InterlockedCompareExchange(&preferenceEnabled,0,0));
    EnableWindow(GetDlgItem(window,automaticId),!data.stale && InterlockedCompareExchange(&preferenceEnabled,0,0));
    for(const auto id:{nameId,saveNameId,resetNameId})EnableWindow(GetDlgItem(window,id),valid && nativeNames.assign);
    updateSummonState(window,data);
    updateOriginalName(window,data);
    if(data.selected<0){SetDlgItemTextW(window,nameId,L"");return;}
    const auto& partner=data.partners[static_cast<std::size_t>(data.selected)];
    const auto original=ReadPartnerOriginalName(partner,static_cast<unsigned>(data.selected));
    const auto value=customName(data.context.playable,partner);
    SetDlgItemTextW(window,nameId,(value.empty()?original:value).c_str());
}
bool verifyContext(HWND window,ManagerDialog& data) {
    PartnerUiContext current{};std::vector<BeautyPartnerOption> partners;
    if(!InterlockedCompareExchange(&enabled,0,0) || !ReadPartnerUiContext(current) || current.actor!=data.context.actor ||
        current.component!=data.context.component || current.playable!=data.context.playable ||
        !ReadBeautyPartners(current.component,current.playable,partners) || partners.size()!=data.partners.size() ||
        !std::equal(partners.begin(),partners.end(),data.partners.begin(),[](const auto& a,const auto& b){return a.key==b.key && a.character==b.character;})) {
        data.stale=true;updateSelection(window,data);setStatus(window,UiText::ContextChanged);return false;
    }
    return true;
}
INT_PTR CALLBACK managerDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<ManagerDialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        data=reinterpret_cast<ManagerDialog*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);managerWindow=window;
        InitializeUiLanguageSelector(window);RegisterUiLanguageWindow(window);
        SendDlgItemMessageW(window,nameId,EM_SETLIMITTEXT,maxNameLength,0);
        refreshManagerLanguage(window,*data);updateSelection(window,*data);SetTimer(window,1,1000,nullptr);
        CenterUiDialog(window,gameWindow);
        if(!nativeNames.assign)setStatus(window,UiText::NamesUnavailable);
        else if(!InterlockedCompareExchange(&preferenceEnabled,0,0))setStatus(window,UiText::PreferenceDisabled);
        SetFocus(GetDlgItem(window,listId));return FALSE;
    }
    if(message==WM_DESTROY){UnregisterUiLanguageWindow(window);KillTimer(window,1);managerWindow=nullptr;return TRUE;}
    if(message==WM_CLOSE){EndDialog(window,IDCANCEL);return TRUE;}
    if(!data)return FALSE;
    if(message==dispatchMessage && wp==2){SetWindowTextW(window,managerTitle().c_str());return TRUE;}
    if(message==UiLanguageChangedMessage()){refreshManagerLanguage(window,*data);return TRUE;}
    if(message==WM_TIMER && wp==1){if(verifyContext(window,*data))updateSummonState(window,*data);return TRUE;}
    if(message!=WM_COMMAND)return FALSE;
    const auto id=LOWORD(wp);
    if(IsUiLanguageCommand(wp)) {
        if(ApplyUiLanguageSelection(window))refreshManagerLanguage(window,*data);
        else setStatus(window,UiText::LanguageSaveFailed);
        return TRUE;
    }
    if(id==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
    if(id==listId && HIWORD(wp)==LBN_SELCHANGE){updateSelection(window,*data);return TRUE;}
    // Enter 只在“保存名字”按钮或名字框上保存；列表中的 Enter 不会意外改名。
    if(id!=preferenceId && id!=automaticId && id!=saveNameId && id!=resetNameId && id!=summonId)return FALSE;
    if(id==saveNameId && HIWORD(wp)==0 &&
        (GetFocus()==GetDlgItem(window,listId) || GetFocus()==GetDlgItem(window,UiLanguageComboId)))return TRUE;
    if(!verifyContext(window,*data))return TRUE;
    if(id==summonId) {
        if(data->selected<0)return TRUE;
        if(!IsPartnerSummonReady()){setStatus(window,UiText::SummonUnavailable);return TRUE;}
        if(IsPartnerSummonBusy()){setStatus(window,UiText::SummonBusy);return TRUE;}
        EndDialog(window,summonId);return TRUE;
    }
    if(id==preferenceId || id==automaticId) {
        if(!InterlockedCompareExchange(&preferenceEnabled,0,0)){setStatus(window,UiText::PreferenceNeedsCommit);return TRUE;}
        if(id==preferenceId && data->selected<0)return TRUE;
        const DWORD key=id==automaticId?0:data->partners[static_cast<std::size_t>(data->selected)].key;
        if(!savePreference || !savePreference(key)){setStatus(window,UiText::ConfigSaveFailed);return TRUE;}
        data->preferred=key;fillList(window,*data);
        setStatus(window,key?UiText::PreferredSaved:UiText::AutomaticSaved);return TRUE;
    }
    if(data->selected<0 || !nativeNames.assign)return TRUE;
    const auto& partner=data->partners[static_cast<std::size_t>(data->selected)];
    wchar_t buffer[maxNameLength+1]{};GetDlgItemTextW(window,nameId,buffer,static_cast<int>(std::size(buffer)));
    std::wstring value=buffer;
    if(id==saveNameId && !normalizeName(value)) {
        setStatus(window,UiText::InvalidName);
        SetFocus(GetDlgItem(window,nameId));SendDlgItemMessageW(window,nameId,EM_SETSEL,0,-1);return TRUE;
    }
    if(id==resetNameId)value.clear();
    if(!saveName(data->context.playable,partner,value)){setStatus(window,UiText::ConfigSaveFailed);return TRUE;}
    applyNames();fillList(window,*data);updateSelection(window,*data);
    setStatus(window,id==resetNameId?UiText::NameReset:UiText::NameSaved);return TRUE;
}
bool dispatchManagerSummon(const ManagerDialog& data) {
    return !data.stale && InterlockedCompareExchange(&enabled,0,0) && data.selected>=0 &&
        static_cast<std::size_t>(data.selected)<data.partners.size() && IsPartnerSummonReady() && !IsPartnerSummonBusy() &&
        RequestPartnerSummon(data.context,data.partners[static_cast<std::size_t>(data.selected)]);
}
void openManager() {
    if(managerWindow){SetForegroundWindow(managerWindow);return;}
    if(!uiReady || !InterlockedCompareExchange(&enabled,0,0))return;
    ManagerDialog data;
    if(!ReadPartnerUiContext(data.context)) {
        if(logger)logger("PARTNER MANAGER BLOCKED: controlled character/main-player relationship context unavailable.");
        MessageBoxW(gameWindow,UiString(UiText::NotLoaded),managerTitle().c_str(),MB_OK|MB_ICONINFORMATION);return;
    }
    if(!ReadBeautyPartners(data.context.component,data.context.playable,data.partners)) {
        if(logger){char line[160];sprintf_s(line,"PARTNER MANAGER BLOCKED: playable=%u relationship records could not be verified.",static_cast<unsigned>(data.context.playable));logger(line);}
        MessageBoxW(gameWindow,UiString(UiText::NotLoaded),managerTitle().c_str(),MB_OK|MB_ICONINFORMATION);return;
    }
    data.preferred=GetPrivateProfileIntW(L"Relationships",L"PreferredPartnerKey",0,settingsPath.c_str());
    if(!data.partners.empty())data.selected=0;
    for(unsigned i=0;i<data.partners.size();++i)if(data.partners[i].key==data.preferred)data.selected=static_cast<int>(i);
    if(logger){char line[128];sprintf_s(line,"PARTNER MANAGER OPEN: playable=%u partners=%zu",static_cast<unsigned>(data.context.playable),data.partners.size());logger(line);}
    const auto layout=dialogLayout();
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),
        gameWindow,managerDialog,reinterpret_cast<LPARAM>(&data));
    if(logger)logger(result==-1?"PARTNER MANAGER FAILED: dialog creation failed.":"PARTNER MANAGER CLOSED.");
    if(result==summonId && !dispatchManagerSummon(data))
        MessageBoxW(gameWindow,UiString(UiText::SummonFailed),managerTitle().c_str(),MB_OK|MB_ICONINFORMATION);
}
bool ownedWindow(HWND window) {
    DWORD pid=0;
    return window && IsWindow(window) && GetWindowThreadProcessId(window,&pid) && pid==GetCurrentProcessId();
}
void requestOpen(HWND window,const char* source) {
    if(logger){char line[192];sprintf_s(line,"PARTNER MANAGER HOTKEY: source=%s key=%s hwnd=%p",source,hotkeyLogLabel().c_str(),static_cast<void*>(window));logger(line);}
    if(!PostMessageW(window,dispatchMessage,1,0) && logger)logger("PARTNER MANAGER FAILED: could not post hotkey window message.");
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    // 短按也由窗口消息捕获；工作线程轮询同时兼容游戏的原始输入路径。
    if((message==WM_KEYDOWN || message==WM_SYSKEYDOWN) && PartnerHotkeyMatches(GetPartnerHotkey(),static_cast<UINT>(wp)) && !(lp&(1ll<<30)) &&
        InterlockedCompareExchange(&enabled,0,0) && GetForegroundWindow()==window && gameWindow==window) {
        requestOpen(window,"window");return 0;
    }
    if(message==dispatchMessage) {
        if(gameWindow!=window)return 0;
        try {if(wp==1)openManager();else applyNames();}
        catch(...){if(logger)logger("PARTNER MANAGER FAILED: operation could not be completed.");}
        return 0;
    }
    // 每个窗口保留自己的前任入口，旧窗口的销毁通知不能使用新窗口的调用链。
    const auto previous=reinterpret_cast<WNDPROC>(GetPropW(window,previousProcProperty));
    if(message==WM_NCDESTROY) {
        if(previous && GetWindowLongPtrW(window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(&windowProc))
            SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(window,previousProcProperty);
        HWND expected=window;gameWindow.compare_exchange_strong(expected,nullptr);
    }
    return previous?CallWindowProcW(previous,window,message,wp,lp):DefWindowProcW(window,message,wp,lp);
}
BOOL CALLBACK findWindow(HWND window,LPARAM result) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window,GW_OWNER))return TRUE;
    wchar_t name[64]{};GetClassNameW(window,name,64);
    if(!wcscmp(name,L"#32770"))return TRUE;
    RECT area{};if(!GetClientRect(window,&area) || area.right<320 || area.bottom<200)return TRUE;
    *reinterpret_cast<HWND*>(result)=window;return FALSE;
}
}

void ConfigurePartnerManager(const wchar_t* iniPath,SavePreferredPartner save,PluginLog log) {
    settingsPath=iniPath;savePreference=save;logger=log;ConfigureUiLanguage(iniPath);loadNames();
    ReloadPartnerHotkey(iniPath,&hotkeyConfigValid);
    dispatchMessage=RegisterWindowMessageW(L"CrimsonDesert.CohabitationGateExperiment.PartnerManager.v1");
}
void InstallPartnerManager(HMODULE module,const MarriageCommitTarget& target) {
    marriage=target;
    const char* error=nullptr;
    if(!FindPartnerNameTarget(module,target,nativeNames,&error)) {
        nativeNames={};
        if(logger && error){std::string line="PARTNER NAMES UNAVAILABLE: ";line+=error;logger(line.c_str());}
    }
    uiReady=true;
    if(logger) {
        const auto line=std::string("PARTNER MANAGER READY: ")+hotkeyLogLabel()+(nativeNames.assign?
            "; native client display names and persistent partner preference.":"; native names unavailable, preference selection remains available.");
        logger(line.c_str());
        if(!hotkeyConfigValid)logger("PARTNER MANAGER HOTKEY CONFIG: invalid [UI] ManagerHotkey; using F8.");
    }
}
void ReloadPartnerManagerHotkey() {
    bool valid=true;const bool changed=ReloadPartnerHotkey(settingsPath.c_str(),&valid);
    if(logger && (changed || valid!=hotkeyConfigValid)) {
        const auto line=valid?std::string("PARTNER MANAGER HOTKEY CONFIG: ManagerHotkey=")+hotkeyLogLabel():
            std::string("PARTNER MANAGER HOTKEY CONFIG: invalid [UI] ManagerHotkey; using F8.");
        logger(line.c_str());
    }
    hotkeyConfigValid=valid;
    if(changed)if(const auto window=managerWindow.load())PostMessageW(window,dispatchMessage,2,0);
}
void SetPartnerManagerEnabled(bool active,bool preferenceActive) {
    InterlockedExchange(&preferenceEnabled,preferenceActive?1:0);InterlockedExchange(&enabled,active?1:0);
}
void PollPartnerManager() {
    if(!uiReady || !dispatchMessage)return;
    const auto cached=gameWindow.load();
    if(cached && (!ownedWindow(cached) || !IsWindowVisible(cached))) {
        // 游戏启动和切换显示模式时可能替换主窗口，且旧入口不一定转发 WM_NCDESTROY。
        // 轮询必须独立核对句柄，不能只依赖旧窗口的销毁回调。
        if(ownedWindow(cached) && GetWindowLongPtrW(cached,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(&windowProc)) {
            const auto previous=GetPropW(cached,previousProcProperty);
            if(previous && SetWindowLongPtrW(cached,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous)))
                RemovePropW(cached,previousProcProperty);
        }
        HWND expected=cached;
        if(gameWindow.compare_exchange_strong(expected,nullptr)) {
            keyWasDown=false;
            if(logger)logger("PARTNER MANAGER WINDOW LOST: cached game window is no longer active; rediscovering.");
        }
    }
    if(!gameWindow) {
        HWND found=nullptr;EnumWindows(findWindow,reinterpret_cast<LPARAM>(&found));
        if(!found)return;
        if(!GetPropW(found,previousProcProperty)) {
            const auto previous=GetWindowLongPtrW(found,GWLP_WNDPROC);
            if(!previous || previous==reinterpret_cast<LONG_PTR>(&windowProc) ||
                !SetPropW(found,previousProcProperty,reinterpret_cast<HANDLE>(previous)))return;
            const auto replaced=SetWindowLongPtrW(found,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&windowProc));
            if(!replaced){RemovePropW(found,previousProcProperty);return;}
            if(replaced!=previous)SetPropW(found,previousProcProperty,reinterpret_cast<HANDLE>(replaced));
        }
        gameWindow=found;
        if(logger){char line[192];sprintf_s(line,"PARTNER MANAGER WINDOW ATTACHED: hwnd=%p thread=%lu; %s input bound.",static_cast<void*>(found),GetWindowThreadProcessId(found,nullptr),hotkeyLogLabel().c_str());logger(line);}
    }
    const auto hotkey=GetPartnerHotkey();const bool down=PartnerHotkeyDown(hotkey);
    if(hotkey!=polledHotkey){polledHotkey=hotkey;keyWasDown=down;}
    if(down && !keyWasDown && GetForegroundWindow()==gameWindow && InterlockedCompareExchange(&enabled,0,0))
        requestOpen(gameWindow,"poll");
    keyWasDown=down;
    const auto now=GetTickCount64();
    if(now-lastUpdate>=1000){lastUpdate=now;PostMessageW(gameWindow,dispatchMessage,0,0);}
}
std::wstring PartnerDisplayName(std::uint16_t playable,const BeautyPartnerOption& partner,unsigned index) {
    const auto value=customName(playable,partner);
    return value.empty() || !InterlockedCompareExchange(&enabled,0,0)?ReadPartnerOriginalName(partner,index):value;
}
