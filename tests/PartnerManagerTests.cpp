// 独立的 Windows 回归程序：使用模拟角色和临时 INI，不加载或操作游戏。
#include "../src/PartnerManager.cpp"
#include <cassert>
#include <cstring>
#include <memory>
#include <thread>

namespace {
PartnerUiContext testContext{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),0};
std::vector<BeautyPartnerOption> testPartners{{635,50,0x1001},{644,50,0x1002}};
DWORD savedPreference=0;
bool contextAvailable=true,saveAllowed=true;
unsigned assignments=0;
bool summonReady=true,recallReady=true,summonBusy=false;unsigned summonRequests=0;DWORD summonedKey=0;
WORD requestedAction=summonId;bool requestedSpawn=true;
SHORT hotkeyStates[256]{};
SHORT WINAPI readHotkeyState(int key){assert(key>=0 && key<256);return hotkeyStates[key];}
constexpr char configComment[]=u8"; 中文注释：保留原配置与编码\r\n";
void seedSettings(const std::wstring& path) {
    FILE* file=nullptr;assert(!_wfopen_s(&file,path.c_str(),L"wb") && file);
    assert(fputs(configComment,file)>=0);
    assert(fputs("[Experiment]\r\nEnabled=1\r\n[Conditions]\r\nIgnoreTribe=0\r\n[Relationships]\r\nPreferredPartnerKey=42\r\nBeautyPartnerPicker=0\r\n",file)>=0);
    assert(!fclose(file));
}
void verifySharedSettings() {
    assert(GetPrivateProfileIntW(L"Experiment",L"Enabled",0,settingsPath.c_str())==1);
    assert(GetPrivateProfileIntW(L"Conditions",L"IgnoreTribe",1,settingsPath.c_str())==0);
    assert(GetPrivateProfileIntW(L"Relationships",L"BeautyPartnerPicker",1,settingsPath.c_str())==0);
    FILE* file=nullptr;assert(!_wfopen_s(&file,settingsPath.c_str(),L"rb") && file);
    char buffer[8192]{};const auto count=fread(buffer,1,sizeof(buffer),file);fclose(file);
    assert(std::string(buffer,count).find(configComment)!=std::string::npos);
    const auto extra=settingsPath.substr(0,settingsPath.size()-4)+L".names.ini";
    assert(GetFileAttributesW(extra.c_str())==INVALID_FILE_ATTRIBUTES);
}
struct FakeString {
    const wchar_t* text=nullptr;DWORD length=0,capacity=MAXDWORD;LONG refs=1;BYTE flag=1;
    std::wstring storage;
    explicit FakeString(const std::wstring& v,bool uncached=false):storage(v){text=storage.c_str();length=uncached?MAXDWORD:static_cast<DWORD>(v.size());}
};
std::vector<std::unique_ptr<FakeString>> strings;
void setFakeName(void* at,const std::wstring& value,bool uncached=false) {
    strings.push_back(std::make_unique<FakeString>(value,uncached));
    *static_cast<void**>(at)=strings.back().get();
}
void __fastcall fakeAssign(void* at,const wchar_t* value){++assignments;setFakeName(at,value);}
bool fakePreference(DWORD key) {
    if(!saveAllowed)return false;
    const auto value=std::to_wstring(key);
    if(!WritePrivateProfileStringW(L"Relationships",L"PreferredPartnerKey",value.c_str(),settingsPath.c_str()))return false;
    savedPreference=key;return true;
}
struct FakeActor {BYTE bytes[256]{};};
struct FakeStatus {BYTE bytes[256]{};};
struct FakeEntry {DWORD unused=0,key=0;void* actor=nullptr;};
struct FakeRegistry {DWORD count=1,padding=0;void* buckets=nullptr;void* entries=nullptr;};
struct FakeManager {FakeRegistry* registry=nullptr;};
struct FakeWorld {FakeManager* manager=nullptr;};
FakeActor actors[2];FakeStatus statuses[2];BYTE components[2][128]{},saves[2][128]{};
BYTE bucket[256]{};FakeEntry entries[2];FakeEntry* pointers[2]{};
FakeRegistry registry;FakeManager testManager;FakeWorld testWorld;FakeWorld* worldPointer=nullptr;
template<class T>void writeField(void* at,unsigned offset,T value){std::memcpy(static_cast<BYTE*>(at)+offset,&value,sizeof(value));}
void initActors() {
    marriage={};marriage.npcKeyOffsets[0]=0x68;marriage.npcKeyOffsets[1]=0x20;
    marriage.npcKeyOffsets[2]=0x28;marriage.npcKeyOffsets[3]=0x28;marriage.runtime.actorIdOffset=0x60;
    nativeNames={};nativeNames.assign=&fakeAssign;nativeNames.clientWorld=reinterpret_cast<void**>(&worldPointer);
    nativeNames.managerOffset=offsetof(FakeWorld,manager);nativeNames.registryOffset=offsetof(FakeManager,registry);
    nativeNames.bucketCountOffset=offsetof(FakeRegistry,count);nativeNames.bucketsOffset=offsetof(FakeRegistry,buckets);
    nativeNames.entriesOffset=offsetof(FakeRegistry,entries);nativeNames.statusOffset=0x20;nativeNames.nameOffset=0x98;
    registry={1,0,bucket,pointers};testManager.registry=&registry;testWorld.manager=&testManager;worldPointer=&testWorld;
    writeField<DWORD>(bucket,0,2);
    for(unsigned i=0;i<2;++i) {
        const auto& p=testPartners[i];
        writeField<void*>(&actors[i],0x68,components[i]);writeField<DWORD>(&actors[i],0x60,p.actorId);
        writeField<void*>(components[i],0x20,&statuses[i]);writeField<void*>(&statuses[i],0x28,saves[i]);
        writeField<std::uint16_t>(&statuses[i],0x30,p.character);writeField<DWORD>(saves[i],0x28,p.key);
        // 来自实机快照的原生格式：独立 UTF-16 文本指针，长度和容量都尚未缓存。
        setFakeName(statuses[i].bytes+0x98,L"原生名字",true);
        entries[i]={0,p.actorId,&actors[i]};pointers[i]=&entries[i];
        writeField<DWORD>(bucket,8+i*8,p.actorId);writeField<DWORD>(bucket,12+i*8,i);
    }
}
std::wstring actualName(unsigned i){std::wstring v;assert(readNativeName(&statuses[i],v));return v;}
void testEncoding() {
    for(const auto* name:{L"小红",L"\"名字=分号;\"",L"A\U0001F431B"}) {
        std::wstring decoded;assert(decodeName(encodeName(name),decoded));assert(decoded==name);
    }
    for(const auto* bad:{L"",L" \t\n",L"a\nb",L"a\tb",L"a\x7f" L"b"}){std::wstring v=bad;assert(!normalizeName(v));}
    std::wstring longName(31,L'中');assert(!normalizeName(longName));
    std::wstring surrogate(1,static_cast<wchar_t>(0xd800));assert(!normalizeName(surrogate));
    std::wstring embeddedNull(L"a\0b",3);assert(!normalizeName(embeddedNull));
    std::wstring value=L" \u3000小红 \u3000";assert(normalizeName(value) && value==L"小红");
    for(const auto* bad:{L"bad",L"v1:0000",L"v1:00X0",L"v1:D800",L"v1:004"})assert(!decodeName(bad,value));
}
void testNativeNameFormats() {
    FakeStatus status;std::wstring result;
    const std::vector<std::wstring> values{L"",L"奥洛内尔",L"小美",L"A\U0001F431B",std::wstring(256,L'中')};
    for(const auto& value:values) {
        for(const bool uncached:{false,true}) {
            setFakeName(status.bytes+0x98,value,uncached);
            assert(readNativeName(&status,result) && result==value);
        }
    }
    setFakeName(status.bytes+0x98,std::wstring(257,L'中'),true);assert(!readNativeName(&status,result));
    strings.back()->length=257;assert(!readNativeName(&status,result));
    strings.back()->length=MAXDWORD-1;assert(!readNativeName(&status,result));
    setFakeName(status.bytes+0x98,L"abc");strings.back()->length=2;assert(!readNativeName(&status,result));
    strings.back()->text=nullptr;strings.back()->length=MAXDWORD;assert(!readNativeName(&status,result));
    strings.back()->text=reinterpret_cast<const wchar_t*>(1);assert(!readNativeName(&status,result));
    puts("PASS: native cached/uncached UTF-16 names, Chinese/surrogate pairs, bounded reads and malformed string guards.");
}
void testUiLanguageSettings() {
    assert(GetUiLanguage()==UiLanguage::Chinese);
    assert(WritePrivateProfileStringW(L"UI",L"Language",L"invalid",settingsPath.c_str()));ReloadUiLanguage();
    assert(GetUiLanguage()==UiLanguage::Chinese);
    assert(WritePrivateProfileStringW(L"UI",L"Language",L"EN-us",settingsPath.c_str()));ReloadUiLanguage();
    assert(GetUiLanguage()==UiLanguage::English);
    assert(SaveUiLanguage(UiLanguage::English));ConfigureUiLanguage(settingsPath.c_str());
    assert(GetUiLanguage()==UiLanguage::English);
    assert(WritePrivateProfileStringW(L"UI",L"Language",nullptr,settingsPath.c_str()));ReloadUiLanguage();
    assert(GetUiLanguage()==UiLanguage::Chinese);
    assert(SaveUiLanguage(UiLanguage::English));
    const auto attributes=GetFileAttributesW(settingsPath.c_str());assert(attributes!=INVALID_FILE_ATTRIBUTES);
    assert(SetFileAttributesW(settingsPath.c_str(),attributes|FILE_ATTRIBUTE_READONLY));
    assert(!SaveUiLanguage(UiLanguage::Chinese) && GetUiLanguage()==UiLanguage::English);
    assert(SetFileAttributesW(settingsPath.c_str(),attributes));
    assert(!SaveUiLanguage(static_cast<UiLanguage>(99)) && GetUiLanguage()==UiLanguage::English);
    for(unsigned i=0;i<static_cast<unsigned>(UiText::Count);++i) {
        const auto id=static_cast<UiText>(i);
        assert(*UiString(id,UiLanguage::Chinese) && *UiString(id,UiLanguage::English));
    }
    assert(SaveUiLanguage(UiLanguage::Chinese));verifySharedSettings();
    assert(GetPrivateProfileIntW(L"Relationships",L"PreferredPartnerKey",0,settingsPath.c_str())==42);
    puts("PASS: shared language persistence/reload, invalid/missing defaults, save failure and complete translations.");
}
void testHotkeys() {
    struct Sample {const wchar_t* text;unsigned key,modifiers;};
    for(const auto& sample:{Sample{L"F1",VK_F1,0},Sample{L"f24",VK_F24,0},Sample{L"Ctrl+F8",VK_F8,PartnerCtrl},
        Sample{L" alt + shift + m ",L'M',PartnerAlt|PartnerShift},Sample{L"9",L'9',0},
        Sample{L"Insert",VK_INSERT,0},Sample{L"PgUp",VK_PRIOR,0},Sample{L"Control+Alt+Shift+Home",VK_HOME,7}}) {
        PartnerHotkey parsed;assert(ParsePartnerHotkey(sample.text,parsed));
        assert(parsed.key==sample.key && parsed.modifiers==sample.modifiers);
        PartnerHotkey roundtrip;assert(ParsePartnerHotkey(PartnerHotkeyLabel(parsed),roundtrip) && roundtrip==parsed);
    }
    for(const auto* text:{L"",L" ",L"F0",L"F25",L"Ctrl",L"+F8",L"F8+",L"Ctrl++F8",L"Ctrl+Ctrl+F8",L"F8+F9",L"F8+Ctrl",L"无效键"}) {
        PartnerHotkey parsed{VK_F9,PartnerAlt};assert(!ParsePartnerHotkey(text,parsed));assert(parsed==PartnerHotkey({VK_F9,PartnerAlt}));
    }
    const PartnerHotkey combo{VK_F9,PartnerCtrl|PartnerShift};
    hotkeyStates[VK_F9]=static_cast<SHORT>(0x8000);hotkeyStates[VK_CONTROL]=static_cast<SHORT>(0x8000);
    assert(!PartnerHotkeyDown(combo,readHotkeyState));hotkeyStates[VK_SHIFT]=static_cast<SHORT>(0x8000);
    assert(PartnerHotkeyDown(combo,readHotkeyState) && PartnerHotkeyMatches(combo,VK_F9,readHotkeyState));
    assert(!PartnerHotkeyMatches(combo,VK_F8,readHotkeyState) && !PartnerHotkeyDown({VK_F9,0},readHotkeyState));
    hotkeyStates[VK_MENU]=static_cast<SHORT>(0x8000);assert(!PartnerHotkeyDown(combo,readHotkeyState));
    std::memset(hotkeyStates,0,sizeof(hotkeyStates));assert(!PartnerHotkeyDown(combo,readHotkeyState));
    assert(GetPartnerHotkey()==PartnerHotkey{});
    assert(WritePrivateProfileStringW(L"UI",L"ManagerHotkey",L"Ctrl+F9",settingsPath.c_str()));ReloadPartnerManagerHotkey();
    assert(GetPartnerHotkey()==PartnerHotkey({VK_F9,PartnerCtrl}) && PartnerHotkeyLabel()==L"Ctrl+F9");
    assert(WritePrivateProfileStringW(L"UI",L"ManagerHotkey",L"unknown",settingsPath.c_str()));ReloadPartnerManagerHotkey();
    assert(GetPartnerHotkey()==PartnerHotkey{});
    wchar_t stored[32]{};GetPrivateProfileStringW(L"UI",L"ManagerHotkey",L"",stored,32,settingsPath.c_str());assert(!wcscmp(stored,L"unknown"));
    assert(WritePrivateProfileStringW(L"UI",L"ManagerHotkey",nullptr,settingsPath.c_str()));ReloadPartnerManagerHotkey();
    assert(GetPartnerHotkey()==PartnerHotkey{});verifySharedSettings();
    puts("PASS: configurable hotkey parsing, aliases/modifiers, key matching, live INI reload, invalid/missing F8 fallback and preserved settings.");
}
void testStorageAndRuntime() {
    const auto& a=testPartners[0];const auto& b=testPartners[1];
    assert(saveName(0,a,L"小红"));assert(saveName(0,b,L"小蓝"));
    wchar_t stored[256]{};
    GetPrivateProfileStringW(L"PartnerNames",storedKey(0,a).c_str(),L"",stored,256,settingsPath.c_str());
    assert(std::wstring(stored)==encodeName(L"小红"));
    assert(GetPrivateProfileIntW(L"Relationships",L"PreferredPartnerKey",0,settingsPath.c_str())==42);
    verifySharedSettings();
    assert(customName(0,a)==L"小红" && customName(0,b)==L"小蓝");
    names.clear();loadNames();assert(customName(0,a)==L"小红" && customName(0,b)==L"小蓝");
    assert(customName(1,a).empty());auto otherCharacter=a;++otherCharacter.character;assert(customName(0,otherCharacter).empty());
    applyNames();assert(actualName(0)==L"小红" && actualName(1)==L"小蓝");
    const auto calls=assignments;applyNames();assert(assignments==calls);
    assert(saveName(0,a,L"小红二号"));applyNames();assert(actualName(0)==L"小红二号" && actualName(1)==L"小蓝");
    assert(saveName(0,a,L""));applyNames();assert(actualName(0)==L"原生名字" && actualName(1)==L"小蓝");
    GetPrivateProfileStringW(L"PartnerNames",storedKey(0,a).c_str(),L"missing",stored,256,settingsPath.c_str());
    assert(std::wstring(stored)==L"missing" && customName(0,b)==L"小蓝");verifySharedSettings();
    SetPartnerManagerEnabled(false,false);applyNames();assert(actualName(1)==L"原生名字");
    SetPartnerManagerEnabled(true,true);applyNames();assert(actualName(1)==L"小蓝");
    setFakeName(statuses[1].bytes+0x98,L"原版新名字");assert(saveName(0,b,L""));applyNames();assert(actualName(1)==L"原版新名字");
    assert(saveName(0,a,L"小红"));applyNames();testContext.playable=1;applyNames();assert(actualName(0)==L"原生名字");testContext.playable=0;
    // 对象键、模板或实例 ID 不再匹配时，不能写到复用的角色内存。
    writeField<DWORD>(saves[0],0x28,999);const auto previous=assignments;applyNames();assert(assignments==previous);
    writeField<DWORD>(saves[0],0x28,a.key);writeField<DWORD>(&actors[0],0x60,0x2001);applyNames();assert(assignments==previous);
    writeField<DWORD>(&actors[0],0x60,a.actorId);applyNames();assert(actualName(0)==L"小红");
    assert(saveName(0,a,L""));applyNames();assert(actualName(0)==L"原生名字");
    puts("PASS: single INI persistence, UTF-8 comments/settings preserved, per-partner rename/reset and actor/context guards.");
}
void CALLBACK driveDialog(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    assert(SendDlgItemMessageW(window,listId,LB_GETCOUNT,0,0)==2);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(preferenceId,BN_CLICKED),0);assert(savedPreference==635);
    SetDlgItemTextW(window,nameId,L"  中文新名  ");SetFocus(GetDlgItem(window,nameId));
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(saveNameId,BN_CLICKED),0);
    assert(customName(0,testPartners[0])==L"中文新名" && actualName(0)==L"中文新名");
    SendDlgItemMessageW(window,listId,LB_SETCURSEL,1,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(listId,LBN_SELCHANGE),0);
    wchar_t buffer[64]{};GetDlgItemTextW(window,nameId,buffer,64);assert(std::wstring(buffer)==L"原生名字");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(automaticId,BN_CLICKED),0);assert(savedPreference==0);
    assert(GetPrivateProfileIntW(L"Relationships",L"PreferredPartnerKey",42,settingsPath.c_str())==0);verifySharedSettings();
    SendDlgItemMessageW(window,listId,LB_SETCURSEL,0,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(listId,LBN_SELCHANGE),0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(resetNameId,BN_CLICKED),0);
    assert(customName(0,testPartners[0]).empty() && actualName(0)==L"原生名字");
    contextAvailable=false;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(preferenceId,BN_CLICKED),0);assert(savedPreference==0);
    assert(!IsWindowEnabled(GetDlgItem(window,preferenceId)) && !IsWindowEnabled(GetDlgItem(window,saveNameId)));
    contextAvailable=true;EndDialog(window,IDCANCEL);
}
INT_PTR CALLBACK testDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    const auto result=managerDialog(window,message,wp,lp);
    if(message==WM_INITDIALOG)SetTimer(window,2,50,&driveDialog);
    return result;
}
void testDialogFlow() {
    ManagerDialog data{testContext,testPartners,0,0,false};const auto layout=dialogLayout();
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),
        nullptr,testDialog,reinterpret_cast<LPARAM>(&data));
    assert(result==IDCANCEL);puts("PASS: native dialog selection, immediate preference, Chinese rename/reset and stale-context blocking.");
}
void changeManagerLanguage(HWND window,UiLanguage language);
void CALLBACK driveSummonDialog(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    auto* data=reinterpret_cast<ManagerDialog*>(GetWindowLongPtrW(window,DWLP_USER));assert(data);
    assert(IsWindowEnabled(GetDlgItem(window,summonId)) && IsWindowEnabled(GetDlgItem(window,recallId)));
    RECT summon{},recall{},name{};assert(GetWindowRect(GetDlgItem(window,summonId),&summon) && GetWindowRect(GetDlgItem(window,recallId),&recall) && GetWindowRect(GetDlgItem(window,originalNameId),&name));
    assert(summon.top==recall.top && summon.right<=recall.left && summon.bottom<=name.top);
    const auto requests=summonRequests;
    recallReady=false;updatePartnerActionState(window,*data);
    assert(IsWindowEnabled(GetDlgItem(window,summonId)) && !IsWindowEnabled(GetDlgItem(window,recallId)));
    if(requestedAction==recallId){SendMessageW(window,WM_COMMAND,MAKEWPARAM(recallId,BN_CLICKED),0);assert(IsWindow(window) && summonRequests==requests);}
    recallReady=true;
    summonReady=false;updatePartnerActionState(window,*data);assert(!IsWindowEnabled(GetDlgItem(window,summonId)) && !IsWindowEnabled(GetDlgItem(window,recallId)));
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(requestedAction,BN_CLICKED),0);assert(IsWindow(window) && summonRequests==requests);
    summonReady=true;summonBusy=true;updatePartnerActionState(window,*data);assert(!IsWindowEnabled(GetDlgItem(window,summonId)) && !IsWindowEnabled(GetDlgItem(window,recallId)));
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(requestedAction,BN_CLICKED),0);assert(IsWindow(window) && summonRequests==requests);
    summonBusy=false;
    SendDlgItemMessageW(window,listId,LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(listId,LBN_SELCHANGE),0);
    assert(data->selected==1 && IsWindowEnabled(GetDlgItem(window,summonId)) && IsWindowEnabled(GetDlgItem(window,recallId)));
    for(const auto language:{UiLanguage::English,UiLanguage::Chinese}) {
        changeManagerLanguage(window,language);
        wchar_t text[128]{};GetDlgItemTextW(window,summonId,text,128);assert(!wcscmp(text,UiString(UiText::SummonSelected)));
        GetDlgItemTextW(window,recallId,text,128);assert(!wcscmp(text,UiString(UiText::RecallSelected)));
    }
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(requestedAction,BN_CLICKED),0);
    assert(summonRequests==requests); // 对话框回调只结束模态窗口，不能提前发请求。
}
INT_PTR CALLBACK summonDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    const auto result=managerDialog(window,message,wp,lp);
    if(message==WM_INITDIALOG)SetTimer(window,8,30,driveSummonDialog);return result;
}
void testManagerSummon() {
    for(const WORD command:{summonId,recallId}) {
        requestedAction=command;const bool spawn=command==summonId;
        ManagerDialog data{testContext,testPartners,0,0,false};const auto layout=dialogLayout();
        const auto preference=savedPreference;const auto requests=summonRequests,assigns=assignments;
        assert(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),nullptr,summonDialog,reinterpret_cast<LPARAM>(&data))==command);
        assert(!managerWindow && summonRequests==requests && dispatchManagerPartnerAction(data,spawn));
        assert(summonRequests==requests+1 && summonedKey==testPartners[1].key && requestedSpawn==spawn && savedPreference==preference && assignments==assigns);
        data.stale=true;assert(!dispatchManagerPartnerAction(data,spawn));data.stale=false;data.selected=-1;assert(!dispatchManagerPartnerAction(data,spawn));
        assert(summonRequests==requests+1);
    }
    puts("PASS: localized summon/recall buttons and layout, unavailable/busy/stale/empty guards, correct action only after modal closes, no preference/name writes.");
}
std::wstring controlText(HWND window,int id) {
    wchar_t buffer[512]{};
    if(id)GetDlgItemTextW(window,id,buffer,512);else GetWindowTextW(window,buffer,512);
    return buffer;
}
void changeManagerLanguage(HWND window,UiLanguage language) {
    SendDlgItemMessageW(window,UiLanguageComboId,CB_SETCURSEL,language==UiLanguage::English?1:0,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(UiLanguageComboId,CBN_SELCHANGE),0);
}
void CALLBACK driveLanguageDialog(HWND window,UINT,UINT_PTR timer,DWORD) {
    KillTimer(window,timer);
    auto* data=reinterpret_cast<ManagerDialog*>(GetWindowLongPtrW(window,DWLP_USER));assert(data);
    SendDlgItemMessageW(window,listId,LB_SETCURSEL,20,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(listId,LBN_SELCHANGE),0);
    SendDlgItemMessageW(window,listId,LB_SETTOPINDEX,12,0);
    setStatus(window,UiText::PreferredSaved);
    const std::wstring draft=L"  小美 / unsaved  ";SetDlgItemTextW(window,nameId,draft.c_str());
    SendDlgItemMessageW(window,nameId,EM_SETSEL,2,6);
    const auto top=SendDlgItemMessageW(window,listId,LB_GETTOPINDEX,0,0);
    const auto calls=assignments;const auto preference=savedPreference;
    for(const auto language:{UiLanguage::English,UiLanguage::Chinese}) {
        changeManagerLanguage(window,language);
        assert(GetUiLanguage()==language && data->selected==20);
        assert(SendDlgItemMessageW(window,listId,LB_GETCURSEL,0,0)==20);
        assert(SendDlgItemMessageW(window,listId,LB_GETTOPINDEX,0,0)==top);
        assert(controlText(window,0)==managerTitle());
        assert(controlText(window,nameId)==draft && controlText(window,statusId)==UiString(UiText::PreferredSaved));
        DWORD start=0,end=0;SendDlgItemMessageW(window,nameId,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        assert(start==2 && end==6);
        assert(controlText(window,originalNameId)==UiString(UiText::OriginalNamePrefix)+std::wstring(L"原生名字"));
        assert(controlText(window,preferenceId)==UiString(UiText::SetPreferred));
        assert(assignments==calls && savedPreference==preference);
        SetFocus(GetDlgItem(window,UiLanguageComboId));
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(saveNameId,0),0);
        assert(customName(0,testPartners[20]).empty() && assignments==calls);
    }
    assert(WritePrivateProfileStringW(L"UI",L"Language",L"en-US",settingsPath.c_str()));
    std::thread([]{ReloadUiLanguage();}).join();
    MSG update{};while(PeekMessageW(&update,window,UiLanguageChangedMessage(),UiLanguageChangedMessage(),PM_REMOVE))DispatchMessageW(&update);
    assert(controlText(window,0)==managerTitle(UiLanguage::English) && controlText(window,nameId)==draft);
    const auto attributes=GetFileAttributesW(settingsPath.c_str());assert(SetFileAttributesW(settingsPath.c_str(),attributes|FILE_ATTRIBUTE_READONLY));
    changeManagerLanguage(window,UiLanguage::Chinese);
    assert(GetUiLanguage()==UiLanguage::English && SendDlgItemMessageW(window,UiLanguageComboId,CB_GETCURSEL,0,0)==1);
    assert(controlText(window,statusId)==UiString(UiText::LanguageSaveFailed,UiLanguage::English) && controlText(window,nameId)==draft);
    assert(SetFileAttributesW(settingsPath.c_str(),attributes));changeManagerLanguage(window,UiLanguage::Chinese);
    for(const auto* binding:{L"Ctrl+F9",L"F8"}) {
        assert(WritePrivateProfileStringW(L"UI",L"ManagerHotkey",binding,settingsPath.c_str()));
        std::thread([]{ReloadPartnerManagerHotkey();}).join();
        while(PeekMessageW(&update,window,dispatchMessage,dispatchMessage,PM_REMOVE))DispatchMessageW(&update);
        assert(controlText(window,0)==managerTitle() && controlText(window,nameId)==draft);
        assert(SendDlgItemMessageW(window,listId,LB_GETCURSEL,0,0)==20 && SendDlgItemMessageW(window,listId,LB_GETTOPINDEX,0,0)==top);
        assert(assignments==calls && savedPreference==preference);
    }
    assert(WritePrivateProfileStringW(L"UI",L"ManagerHotkey",nullptr,settingsPath.c_str()));
    verifySharedSettings();EndDialog(window,IDCANCEL);
}
INT_PTR CALLBACK languageDialog(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    const auto result=managerDialog(window,message,wp,lp);
    if(message==WM_INITDIALOG)SetTimer(window,3,50,&driveLanguageDialog);
    return result;
}
void testManagerLanguageFlow() {
    const auto original=testPartners;
    for(DWORD key=10000;key<10030;++key)testPartners.push_back({key,50,0});
    ManagerDialog data{testContext,testPartners,0,0,false};const auto layout=dialogLayout();
    assert(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),nullptr,
        languageDialog,reinterpret_cast<LPARAM>(&data))==IDCANCEL);
    testPartners=original;
    puts("PASS: manager language switch/reload, selection/scroll/draft/edit selection retained, no unintended writes, localized save failure.");
}
constexpr UINT chainProbe=WM_APP+77;
LRESULT CALLBACK bindingHostA(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==chainProbe)return 0x111;
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK bindingHostB(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==chainProbe)return 0x222;
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK anotherSubclass(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    return CallWindowProcW(&windowProc,window,message,wp,lp);
}
void pollFromWorker(){std::thread([]{PollPartnerManager();}).join();}
void testWindowRecreation() {
    WNDCLASSW a{};a.lpfnWndProc=&bindingHostA;a.hInstance=GetModuleHandleW(nullptr);a.lpszClassName=L"PartnerManagerBindingTestA";
    WNDCLASSW b=a;b.lpfnWndProc=&bindingHostB;b.lpszClassName=L"PartnerManagerBindingTestB";
    assert(RegisterClassW(&a) && RegisterClassW(&b));
    const auto create=[&](const wchar_t* type) {
        const auto window=CreateWindowW(type,L"Partner manager binding test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,a.hInstance,nullptr);
        assert(window);ShowWindow(window,SW_SHOWNOACTIVATE);return window;
    };
    uiReady=true;lastUpdate=GetTickCount64();
    const auto first=create(a.lpszClassName);pollFromWorker();
    assert(gameWindow==first && GetWindowLongPtrW(first,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(&windowProc));
    assert(SendMessageW(first,chainProbe,0,0)==0x111);
    // 复现真实游戏：另一个入口接管窗口并销毁，插件收不到 WM_NCDESTROY。
    SetWindowLongPtrW(first,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&bindingHostA));
    assert(DestroyWindow(first));assert(gameWindow==first && !IsWindow(first));
    const auto second=create(a.lpszClassName);pollFromWorker();
    assert(gameWindow==second && GetWindowLongPtrW(second,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(&windowProc));
    assert(SendMessageW(second,chainProbe,0,0)==0x111);
    // 隐藏的旧窗口仍可能通过另一插件回调本插件；它必须保留自己的前任入口。
    SetWindowLongPtrW(second,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&anotherSubclass));
    ShowWindow(second,SW_HIDE);
    const auto third=create(b.lpszClassName);pollFromWorker();
    assert(gameWindow==third && SendMessageW(third,chainProbe,0,0)==0x222);
    assert(SendMessageW(second,chainProbe,0,0)==0x111);
    assert(DestroyWindow(second));assert(gameWindow==third);
    assert(DestroyWindow(third));assert(!gameWindow);
    uiReady=false;assert(UnregisterClassW(a.lpszClassName,a.hInstance) && UnregisterClassW(b.lpszClassName,b.hInstance));
    puts("PASS: worker-thread binding, replaced/destroyed game window recovery, per-window subclass chains and delayed destruction.");
}
LRESULT CALLBACK previewHost(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
        const auto label=L"独立测试窗口：按 "+PartnerHotkeyLabel()+L" 打开伴侣管理。\n测试使用模拟对象和临时配置，不连接游戏。";
        DrawTextW(dc,label.c_str(),-1,&bounds,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void previewWithHotkey() {
    WNDCLASSW type{};type.lpfnWndProc=&previewHost;type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"CrimsonDesertPartnerTestHost";type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    assert(RegisterClassW(&type));
    const auto host=CreateWindowW(type.lpszClassName,L"伴侣管理测试宿主（F8）",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,900,700,nullptr,nullptr,type.hInstance,nullptr);
    assert(host);ShowWindow(host,SW_SHOWNORMAL);uiReady=true;
    SetTimer(host,99,50,[](HWND,UINT,UINT_PTR,DWORD){PollPartnerManager();});
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
}
}
bool ReadPartnerUiContext(PartnerUiContext& context){context=testContext;return contextAvailable;}
bool ReadBeautyPartners(void*,std::uint16_t,std::vector<BeautyPartnerOption>& partners){partners=testPartners;return contextAvailable;}
std::wstring ReadPartnerOriginalName(const BeautyPartnerOption&,unsigned){return L"原生名字";}
bool FindPartnerNameTarget(HMODULE,const MarriageCommitTarget&,PartnerNameTarget&,const char**){return false;}
bool IsPartnerSummonReady(){return summonReady;}
bool IsPartnerRecallReady(){return summonReady && recallReady;}
bool IsPartnerSummonBusy(){return summonBusy;}
bool RequestPartnerSummon(const PartnerUiContext&,const BeautyPartnerOption& partner,bool spawn){++summonRequests;summonedKey=partner.key;requestedSpawn=spawn;return true;}

int wmain(int argc,wchar_t** argv) {
    wchar_t temp[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp));
    const auto path=std::wstring(temp)+L"CrimsonDesertPartnerTests-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    seedSettings(path);
    ConfigurePartnerManager(path.c_str(),&fakePreference,nullptr);initActors();SetPartnerManagerEnabled(true,true);
#ifdef PARTNER_MANAGER_PREVIEW
    (void)argc;(void)argv;
    if(true) {
#else
    if(argc>1 && !wcscmp(argv[1],L"--preview")) {
#endif
        previewWithHotkey();
    } else {testEncoding();testNativeNameFormats();testUiLanguageSettings();testHotkeys();testStorageAndRuntime();testDialogFlow();testManagerLanguageFlow();testManagerSummon();testWindowRecreation();}
    verifySharedSettings();DeleteFileW(settingsPath.c_str());puts("All partner manager tests passed.");return 0;
}
