// 用模拟的客户端发送与服务端派发验证请求关联；不连接或调用游戏。
#include "../src/PartnerSummon.cpp"
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
void check(bool value,const char* expression,int line){if(!value){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(1);}}
#define CHECK(value) check(static_cast<bool>(value),#value,__LINE__)
BYTE clientOwner[128]{},controlled[128]{},serverOwner[128]{},otherOwner[128]{},component[32]{},serverComponent[32]{},otherComponent[32]{},info[32]{};
BYTE follower[128]{},followerComponents[0x200]{},followerParty[0xc0]{},group[0xc0]{};
void* partyMethods[1]{};
std::vector<PartnerActor> liveActors;
bool actorsAvailable=true,leaveClears=true,leaveAlsoRecalls=false;DWORD leaveResult=0;unsigned leaves=0;
PartnerUiContext context{controlled,component,3};
std::vector<BeautyPartnerOption> partners{{100,50,0},{200,60,0}};
bool contextAvailable=true,throwSend=false,reachControl=true;
bool selectedSpawn=true;
unsigned sends=0,nativeCalls=0,selectedCalls=0,normalCalls=0;
BYTE sentToken=0;DWORD sentRole=0,selectedKey=0;std::uint16_t selectedCharacter=0;
template<class T>void put(void* at,unsigned offset,T value){std::memcpy(static_cast<BYTE*>(at)+offset,&value,sizeof(value));}
void* __fastcall fakeLeave(void* party,DWORD* result,bool first,bool second) {
    CHECK(party==followerParty && !first && !second);++leaves;*result=leaveResult;
    if(!leaveResult && leaveClears)put<BYTE>(followerParty,0x98,0);
    if(!leaveResult && leaveAlsoRecalls){partners[0].actorId=0;liveActors.clear();put<void*>(followerComponents,0x108,nullptr);}
    return result;
}
const void* __fastcall fakeInfo(void*,std::uint16_t playable){CHECK(playable==context.playable);return info;}
void __fastcall fakeSend(void* owner,unsigned,const BYTE* value,const DWORD* playable) {
    CHECK(owner==clientOwner);CHECK(!HasSelectedPartnerSummon());CHECK(*value>=0x80);
    if(throwSend)throw 1;
    ++sends;sentToken=*value;sentRole=*playable;
}
void __fastcall fakeNative(void*,void* raw,const BYTE* value,const DWORD*) {
    ++nativeCalls;
    if(!HasSelectedPartnerSummon()){++normalCalls;CHECK(*value<=1);return;}
    CHECK(*value<=1); // 原版业务只能收到规范的 true/false。
    CHECK(*static_cast<void**>(raw)==serverOwner);
    if(!reachControl)return;
    DWORD key=0;std::uint16_t character=0;
    CHECK(!ReadSelectedPartnerSummon(otherComponent,context.playable,key,character));
    CHECK(!ReadSelectedPartnerSummon(serverComponent,0,key,character));
    bool spawn=true;CHECK(ReadSelectedPartnerSummon(serverComponent,context.playable,key,character,&spawn));
    CHECK(*value==(spawn?1:0));selectedSpawn=spawn;
    ++selectedCalls;selectedKey=key;selectedCharacter=character;
    PartnerActionObservation observation;observation.result=0;observation.before=spawn?0:1;observation.after=spawn?1:0;observation.verified=true;
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
    CHECK(selectedSpawn);const auto spawnToken=sentToken;
    CHECK(RequestPartnerSummon(context,partners[1],false));CHECK(IsPartnerSummonBusy() && !RequestPartnerSummon(context,partners[0]));
    deliver(spawnToken,sentRole);CHECK(selectedCalls==2 && IsPartnerSummonBusy());
    deliver(sentToken,sentRole);CHECK(selectedCalls==3 && selectedKey==200 && !selectedSpawn && !IsPartnerSummonBusy());
    deliver(sentToken,sentRole);CHECK(selectedCalls==3);
    const auto before=normalCalls;deliver(0,sentRole);deliver(1,sentRole);CHECK(normalCalls==before+2 && selectedCalls==3);
    puts("PASS: native client send is deferred to server dispatch; selected summon/recall restore true/false, share busy guard, ordinary requests unchanged, wrong/late/duplicate tokens blocked.");
}
void testContextAndFailures(bool spawn) {
    const auto before=sends;auto stale=context;++stale.playable;CHECK(!RequestPartnerSummon(stale,partners[0],spawn));
    auto missing=partners[0];++missing.character;CHECK(!RequestPartnerSummon(context,missing,spawn));
    contextAvailable=false;CHECK(!RequestPartnerSummon(context,partners[0],spawn));contextAvailable=true;
    ready=false;CHECK(!RequestPartnerSummon(context,partners[0],spawn));ready=true;
    put<DWORD>(info,8,0);CHECK(!RequestPartnerSummon(context,partners[0],spawn));put<DWORD>(info,8,444);
    CHECK(sends==before);
    CHECK(RequestPartnerSummon(context,partners[0],spawn));const auto beforeSwitch=nativeCalls;
    context.playable=0;deliver(sentToken,sentRole);context.playable=3;
    CHECK(nativeCalls==beforeSwitch && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0],spawn));context.actor=otherOwner;deliver(sentToken,sentRole);context.actor=controlled;
    CHECK(nativeCalls==beforeSwitch && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0],spawn));const auto calls=nativeCalls;
    {Lock lock;pending.created=GetTickCount64()-requestLifetime-1;}
    deliver(sentToken,sentRole);CHECK(nativeCalls==calls && !IsPartnerSummonBusy());
    CHECK(RequestPartnerSummon(context,partners[0],spawn));SetPartnerSummonEnabled(false);deliver(sentToken,sentRole);
    CHECK(nativeCalls==calls && !IsPartnerSummonBusy());SetPartnerSummonEnabled(true);
    throwSend=true;CHECK(!RequestPartnerSummon(context,partners[0],spawn) && !IsPartnerSummonBusy());throwSend=false;
    reachControl=false;CHECK(RequestPartnerSummon(context,partners[0],spawn));const auto selected=selectedCalls;
    deliver(sentToken,sentRole);CHECK(selectedCalls==selected && !IsPartnerSummonBusy());reachControl=true;
    puts("PASS: stale role/partner, unavailable interface, invalid character key, timeout, disabled feature, send failure and native preflight rejection do not fall back to another partner.");
}
void testPrepareRecall() {
    const auto saved=partners;Selection active;active.owner=serverOwner;active.request.playable=3;active.request.key=100;active.request.character=50;active.request.spawn=false;selection=&active;
    const DWORD actorId=0xb0100099;
    const auto reset=[&] {
        partners=saved;partners[0].actorId=actorId;actorsAvailable=true;leaveClears=true;leaveAlsoRecalls=false;leaveResult=0;
        std::memset(follower,0,sizeof(follower));std::memset(followerComponents,0,sizeof(followerComponents));std::memset(followerParty,0,sizeof(followerParty));std::memset(group,0,sizeof(group));
        put<DWORD>(follower,0x60,actorId);put<void*>(follower,0x68,followerComponents);put<void*>(followerComponents,0x108,followerParty);
        put<void**>(followerParty,0,partyMethods);put<void*>(followerParty,8,follower);put<BYTE>(followerParty,0x98,1);put<void*>(followerParty,0x90,group);put<DWORD>(group,0xa0,0xa0100001);
        liveActors={{100,actorId,reinterpret_cast<std::uintptr_t>(follower)}};
    };
    bool already=false;reset();put<BYTE>(followerParty,0x98,0);
    CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==0 && !already && leaves==0);
    reset();CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==0 && !already && leaves==1);
    auto before=leaves;reset();put<DWORD>(group,0xa0,0xa0100002);CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && leaves==before);
    reset();put<DWORD>(group,0xa0,actorId);CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && leaves==before);
    reset();put<void*>(followerParty,8,otherOwner);CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && leaves==before);
    reset();put<void*>(followerParty,0,otherOwner);CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && leaves==before);
    reset();actorsAvailable=false;CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && leaves==before);
    reset();leaveResult=0x4567;CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==0x4567 && !already);
    reset();leaveClears=false;CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==MAXDWORD && !already);
    reset();leaveAlsoRecalls=true;CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==0 && already);
    before=leaves;reset();partners[0].actorId=0;CHECK(PrepareSelectedPartnerRecall(serverComponent,3,100,already)==0 && already && leaves==before);
    reset();native.leaveParty=nullptr;CHECK(IsPartnerSummonReady() && !IsPartnerRecallReady());CHECK(!RequestPartnerSummon(context,partners[0],false));native.leaveParty=fakeLeave;
    partners=saved;liveActors.clear();selection=nullptr;
    puts("PASS: native member leave only for an owned married follower, leader/foreign/component mismatch rejected, no-party no-op, leave errors/still-blocked result retained, already-recalled instance not dereferenced.");
}
}
bool ReadPartnerUiContext(PartnerUiContext& value){value=context;return contextAvailable;}
bool ReadBeautyPartners(void*,std::uint16_t,std::vector<BeautyPartnerOption>& values){values=partners;return contextAvailable;}
bool VerifyBeautyPartner(void*,std::uint16_t playable,DWORD key){return contextAvailable && playable==context.playable && (key==100 || key==200);}
bool FindPartnerSummonTarget(HMODULE,const MarriageCommitTarget&,PartnerSummonTarget&,const char*&){return false;}
bool FindPartnerRecallTarget(HMODULE,const MarriageCommitTarget&,PartnerSummonTarget&,const char*&){return false;}
bool CollectPartnerActors(const MarriageCommitTarget&,void*,std::uint16_t,std::vector<PartnerActor>& actors){actors=liveActors;return actorsAvailable;}
int main() {
    layout.runtime.actorIdOffset=0x60;
    layout.npcKeyOffsets[0]=0x68;
    put<DWORD>(clientOwner,0x60,0xa0100001);put<DWORD>(serverOwner,0x60,0xa0100001);put<DWORD>(otherOwner,0x60,0xa0100002);
    put<DWORD>(controlled,0x60,0xb0100003);
    put<void*>(component,8,clientOwner);put<void*>(serverComponent,8,serverOwner);put<void*>(otherComponent,8,otherOwner);put<DWORD>(info,8,444);
    native.send=fakeSend;native.playableInfo=fakeInfo;native.playableKeyOffset=8;originalDispatch=fakeNative;ready=true;enabled=true;
    native.leaveParty=fakeLeave;native.partyVtable=partyMethods;native.partyComponentOffset=0x108;native.partyActiveOffset=0x98;native.partyGroupOffset=0x90;native.partyLeaderOffset=0xa0;
    testRouting();
    testContextAndFailures(true);testContextAndFailures(false);
    testPrepareRecall();
    puts("All partner summon bridge tests passed.");return 0;
}
