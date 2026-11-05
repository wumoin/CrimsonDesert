// 用模拟的客户端发送与服务端派发验证请求关联；不连接或调用游戏。
#include "../src/PartnerSummon.cpp"
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
void check(bool value,const char* expression,int line){if(!value){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(1);}}
#define CHECK(value) check(static_cast<bool>(value),#value,__LINE__)
BYTE clientOwner[128]{},controlled[128]{},serverOwner[128]{},otherOwner[128]{},component[32]{},serverComponent[32]{},otherComponent[32]{},info[32]{};
PartnerUiContext context{controlled,component,3};
std::vector<BeautyPartnerOption> partners{{100,50,0},{200,60,0}};
bool contextAvailable=true,throwSend=false,reachControl=true;
unsigned sends=0,nativeCalls=0,selectedCalls=0,normalCalls=0;
BYTE sentToken=0;DWORD sentRole=0,selectedKey=0;std::uint16_t selectedCharacter=0;
template<class T>void put(void* at,unsigned offset,T value){std::memcpy(static_cast<BYTE*>(at)+offset,&value,sizeof(value));}
const void* __fastcall fakeInfo(void*,std::uint16_t playable){CHECK(playable==context.playable);return info;}
void __fastcall fakeSend(void* owner,unsigned,const BYTE* value,const DWORD* playable) {
    CHECK(owner==clientOwner);CHECK(!HasSelectedPartnerSummon());CHECK(*value>=0x80);
    if(throwSend)throw 1;
    ++sends;sentToken=*value;sentRole=*playable;
}
void __fastcall fakeNative(void*,void* raw,const BYTE* value,const DWORD*) {
    ++nativeCalls;
    if(!HasSelectedPartnerSummon()){++normalCalls;CHECK(*value<=1);return;}
    CHECK(*value==1); // 保留字段宽度，但原版业务只能收到规范 true。
    CHECK(*static_cast<void**>(raw)==serverOwner);
    if(!reachControl)return;
    DWORD key=0;std::uint16_t character=0;
    CHECK(!ReadSelectedPartnerSummon(otherComponent,context.playable,key,character));
    CHECK(!ReadSelectedPartnerSummon(serverComponent,0,key,character));
    CHECK(ReadSelectedPartnerSummon(serverComponent,context.playable,key,character));
    ++selectedCalls;selectedKey=key;selectedCharacter=character;
    PartnerActionObservation observation;observation.result=0;observation.after=1;observation.verified=true;
    ReportSelectedPartnerSummon(observation);
}
void deliver(BYTE token,DWORD role,void* owner=serverOwner) {
    std::thread([=]{void* requestOwner=owner;dispatch(nullptr,&requestOwner,&token,&role);CHECK(!HasSelectedPartnerSummon());}).join();
    CHECK(!HasSelectedPartnerSummon());
}
void testRouting() {
    CHECK(RequestPartnerSummon(context,partners[1]));CHECK(sends==1 && selectedCalls==0 && IsPartnerSummonBusy());
    CHECK(!RequestPartnerSummon(context,partners[0]));
    const auto oldToken=sentToken;
    deliver(sentToken,sentRole,otherOwner);CHECK(selectedCalls==0 && IsPartnerSummonBusy());
    deliver(sentToken,sentRole+1);CHECK(selectedCalls==0 && IsPartnerSummonBusy());
    deliver(static_cast<BYTE>(sentToken+1),sentRole);CHECK(selectedCalls==0 && IsPartnerSummonBusy());
    deliver(sentToken,sentRole);CHECK(!IsPartnerSummonBusy() && selectedCalls==1 && selectedKey==200 && selectedCharacter==60);
    CHECK(RequestPartnerSummon(context,partners[0]));CHECK(sentToken!=oldToken);
    deliver(oldToken,sentRole);CHECK(selectedCalls==1 && IsPartnerSummonBusy());
    deliver(sentToken,sentRole);CHECK(selectedCalls==2 && selectedKey==100 && !IsPartnerSummonBusy());
    const auto before=normalCalls;deliver(0,sentRole);deliver(1,sentRole);CHECK(normalCalls==before+2 && selectedCalls==2);
    puts("PASS: native client send is deferred to server dispatch; exact selected partner, canonical bool, ordinary requests unchanged, wrong/late/duplicate tokens blocked.");
}
void testContextAndFailures() {
    const auto before=sends;auto stale=context;++stale.playable;CHECK(!RequestPartnerSummon(stale,partners[0]));
    auto missing=partners[0];++missing.character;CHECK(!RequestPartnerSummon(context,missing));
    contextAvailable=false;CHECK(!RequestPartnerSummon(context,partners[0]));contextAvailable=true;
    ready=false;CHECK(!RequestPartnerSummon(context,partners[0]));ready=true;
    put<DWORD>(info,8,0);CHECK(!RequestPartnerSummon(context,partners[0]));put<DWORD>(info,8,444);
    CHECK(sends==before);
    CHECK(RequestPartnerSummon(context,partners[0]));const auto beforeSwitch=nativeCalls;
    context.playable=0;deliver(sentToken,sentRole);context.playable=3;
    CHECK(nativeCalls==beforeSwitch && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0]));context.actor=otherOwner;deliver(sentToken,sentRole);context.actor=controlled;
    CHECK(nativeCalls==beforeSwitch && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0]));const auto calls=nativeCalls;
    {Lock lock;pending.created=GetTickCount64()-requestLifetime-1;}
    deliver(sentToken,sentRole);CHECK(nativeCalls==calls && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0]));SetPartnerSummonEnabled(false);deliver(sentToken,sentRole);
    CHECK(nativeCalls==calls && !IsPartnerSummonBusy());SetPartnerSummonEnabled(true);
    throwSend=true;CHECK(!RequestPartnerSummon(context,partners[0]) && !IsPartnerSummonBusy());throwSend=false;
    reachControl=false;CHECK(RequestPartnerSummon(context,partners[0]));const auto selected=selectedCalls;
    deliver(sentToken,sentRole);CHECK(selectedCalls==selected && !IsPartnerSummonBusy());reachControl=true;
    puts("PASS: stale role/partner, unavailable interface, invalid character key, timeout, disabled feature, send failure and native preflight rejection do not fall back to another partner.");
}
}
bool ReadPartnerUiContext(PartnerUiContext& value){value=context;return contextAvailable;}
bool ReadBeautyPartners(void*,std::uint16_t,std::vector<BeautyPartnerOption>& values){values=partners;return contextAvailable;}
bool VerifyBeautyPartner(void*,std::uint16_t playable,DWORD key){return contextAvailable && playable==context.playable && (key==100 || key==200);}
bool FindPartnerSummonTarget(HMODULE,const MarriageCommitTarget&,PartnerSummonTarget&,const char*&){return false;}
int main() {
    layout.runtime.actorIdOffset=0x60;
    put<DWORD>(clientOwner,0x60,0xa0100001);put<DWORD>(serverOwner,0x60,0xa0100001);put<DWORD>(otherOwner,0x60,0xa0100002);
    put<DWORD>(controlled,0x60,0xb0100003);
    put<void*>(component,8,clientOwner);put<void*>(serverComponent,8,serverOwner);put<void*>(otherComponent,8,otherOwner);put<DWORD>(info,8,444);
    native.send=fakeSend;native.playableInfo=fakeInfo;native.playableKeyOffset=8;originalDispatch=fakeNative;ready=true;enabled=true;
    testRouting();
    testContextAndFailures();
    puts("All partner summon bridge tests passed.");return 0;
}
