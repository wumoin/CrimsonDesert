#define WIN32_LEAN_AND_MEAN
#include "PartnerSummon.h"
#include "../vendor/minhook/include/MinHook.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace {
constexpr ULONGLONG requestLifetime=10000,tokenQuarantine=60000;
PartnerSummonTarget native{};
MarriageCommitTarget layout{};
NativePartnerSummonDispatch originalDispatch=nullptr;
PluginLog logger=nullptr;
std::atomic<bool> ready{false},enabled{false};
SRWLOCK requestLock=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&requestLock);}~Lock(){ReleaseSRWLockExclusive(&requestLock);}};
struct Request {
    ULONGLONG id=0,created=0;
    DWORD ownerId=0,actorId=0,playableKey=0,key=0;
    std::uintptr_t clientActor=0,clientComponent=0;
    std::uint16_t playable=0xffff,character=0xffff;
    BYTE token=0;
    bool queued=false,processing=false;
};
Request pending;
ULONGLONG nextId=0,usedTokens[128]{};
unsigned nextToken=0;
struct Selection {Request request;void* owner=nullptr;bool consumed=false;};
thread_local Selection* selection=nullptr;
bool read(void* out,const void* at,std::size_t size) {
    SIZE_T got=0;return at && ReadProcessMemory(GetCurrentProcess(),at,out,size,&got) && got==size;
}
template<class T> bool field(const void* at,unsigned offset,T& value) {
    return at && offset<=4096 && read(&value,static_cast<const BYTE*>(at)+offset,sizeof(value));
}
void expire(ULONGLONG now) {
    if((pending.queued || pending.processing) && now-pending.created>requestLifetime){pending.queued=false;pending.processing=false;}
}
void logRequest(const char* action,const Request& request) {
    if(!logger)return;
    char line[256];sprintf_s(line,"PARTNER MANAGER SUMMON %s: id=%llu owner=0x%08lx playable=%u key=%lu character=%u token=%u",
        action,request.id,request.ownerId,static_cast<unsigned>(request.playable),request.key,static_cast<unsigned>(request.character),static_cast<unsigned>(request.token));logger(line);
}
void __fastcall dispatch(void* self,void* context,const BYTE* spawn,const DWORD* playableKey) {
    BYTE value=0;
    if(!read(&value,spawn,sizeof(value)) || value<0x80){originalDispatch(self,context,spawn,playableKey);return;}
    // 原版请求只序列化 1 字节动作和 4 字节角色键。已验证的原生读写器按原字节复制；
    // 保留高位动作字节用于关联本插件的短期选择，进入原版业务函数前恢复为规范的 true。
    // 因此普通 0/1 请求不会误用窗口选择，迟到或未知令牌也不会回退为另一位伴侣。
    Request request;void* owner=nullptr;DWORD ownerId=0,characterKey=0;bool accepted=false;
    if(read(&owner,context,sizeof(owner)) && field(owner,layout.runtime.actorIdOffset,ownerId) && read(&characterKey,playableKey,sizeof(characterKey))) {
        Lock lock;expire(GetTickCount64());
        if(ready && enabled && pending.queued && pending.token==value && pending.ownerId==ownerId && pending.playableKey==characterKey) {
            pending.queued=false;pending.processing=true;request=pending;accepted=true;
        }
    }
    if(!accepted){if(logger)logger("PARTNER MANAGER SUMMON BLOCKED: expired, unknown or mismatched request token; no native action.");return;}
    PartnerUiContext current{};DWORD currentActorId=0;
    // 这里只读最新客户端上下文；保存的地址仅用于比较，不跨帧解引用旧对象。
    if(!ReadPartnerUiContext(current) || reinterpret_cast<std::uintptr_t>(current.actor)!=request.clientActor ||
        reinterpret_cast<std::uintptr_t>(current.component)!=request.clientComponent || current.playable!=request.playable ||
        !field(current.actor,layout.runtime.actorIdOffset,currentActorId) || currentActorId!=request.actorId) {
        logRequest("CANCELLED CONTEXT",request);Lock lock;if(pending.id==request.id)pending.processing=false;return;
    }
    Selection active{request,owner,false};
    struct Scope {Selection* previous;explicit Scope(Selection& value):previous(selection){selection=&value;}~Scope(){selection=previous;}} scope(active);
    const BYTE canonicalSpawn=1;
    try {originalDispatch(self,context,&canonicalSpawn,playableKey);}
    catch(...) {if(logger)logger("PARTNER MANAGER SUMMON BLOCKED: native dispatch interrupted; no retry.");}
    if(!active.consumed)logRequest("NOT EXECUTED",request);
    {Lock lock;if(pending.id==request.id){pending.processing=false;pending.queued=false;}}
}
}

bool InstallPartnerSummon(HMODULE module,const MarriageCommitTarget& marriage,PluginLog log) {
    logger=log;layout=marriage;const char* error=nullptr;
    if(!FindPartnerSummonTarget(module,marriage,native,error)) {
        if(logger){char line[240];sprintf_s(line,"PARTNER MANAGER SUMMON UNAVAILABLE: %s.",error?error:"native request path not verified");logger(line);}return false;
    }
    auto status=MH_CreateHook(reinterpret_cast<void*>(native.dispatch),reinterpret_cast<void*>(&dispatch),reinterpret_cast<void**>(&originalDispatch));
    if(status==MH_OK){status=MH_EnableHook(reinterpret_cast<void*>(native.dispatch));if(status!=MH_OK)MH_RemoveHook(reinterpret_cast<void*>(native.dispatch));}
    ready=status==MH_OK;
    if(logger){char line[224];sprintf_s(line,"PARTNER MANAGER SUMMON HOOK: status=%s sendRVA=0x%08lx dispatchRVA=0x%08lx; native client request and server-thread selected-partner action.",MH_StatusToString(status),native.sendRva,native.dispatchRva);logger(line);}
    return ready;
}
void SetPartnerSummonEnabled(bool value) {
    enabled=value;
    if(!value){Lock lock;pending.queued=false;pending.processing=false;}
}
bool IsPartnerSummonReady(){return ready && enabled;}
bool IsPartnerSummonBusy(){Lock lock;expire(GetTickCount64());return pending.queued || pending.processing;}
bool RequestPartnerSummon(const PartnerUiContext& context,const BeautyPartnerOption& partner) {
    if(!IsPartnerSummonReady() || !partner.key || partner.key==MAXDWORD || partner.character==0xffff)return false;
    PartnerUiContext current{};std::vector<BeautyPartnerOption> partners;
    if(!ReadPartnerUiContext(current) || current.actor!=context.actor || current.component!=context.component || current.playable!=context.playable ||
        !ReadBeautyPartners(current.component,current.playable,partners) || std::none_of(partners.begin(),partners.end(),[&](const auto& item){return item.key==partner.key && item.character==partner.character;}))return false;
    void* owner=nullptr;DWORD ownerId=0,actorId=0,playableKey=0;
    if(!field(current.component,8,owner) || !field(owner,layout.runtime.actorIdOffset,ownerId) || !ownerId ||
        !field(current.actor,layout.runtime.actorIdOffset,actorId) || !actorId)return false;
    const auto info=native.playableInfo(nullptr,current.playable);
    if(!field(info,native.playableKeyOffset,playableKey) || !playableKey || playableKey==MAXDWORD)return false;
    PartnerUiContext after{};
    if(!ReadPartnerUiContext(after) || after.actor!=current.actor || after.component!=current.component || after.playable!=current.playable ||
        !VerifyBeautyPartner(after.component,after.playable,partner.key))return false;
    Request request;
    {
        Lock lock;const auto now=GetTickCount64();expire(now);
        if(pending.queued || pending.processing || !ready || !enabled)return false;
        int token=-1;
        for(unsigned i=0;i<128;++i) {
            const auto candidate=(nextToken+i)%128;
            if(!usedTokens[candidate] || now-usedTokens[candidate]>=tokenQuarantine){token=static_cast<int>(candidate);break;}
        }
        if(token<0)return false;
        usedTokens[token]=now?now:1;nextToken=(static_cast<unsigned>(token)+1)%128;
        request.id=++nextId;request.created=now;request.ownerId=ownerId;request.actorId=actorId;request.playableKey=playableKey;
        request.clientActor=reinterpret_cast<std::uintptr_t>(current.actor);request.clientComponent=reinterpret_cast<std::uintptr_t>(current.component);
        request.key=partner.key;request.playable=current.playable;request.character=partner.character;
        request.token=static_cast<BYTE>(0x80+token);request.queued=true;pending=request;
    }
    logRequest("REQUEST",request);
    // 这里只调用原生客户端发包函数；不在 Windows 窗口线程调用服务端召唤控制器。
    try {native.send(owner,0,&request.token,&request.playableKey);}
    catch(...) {Lock lock;if(pending.id==request.id)pending.queued=false;return false;}
    return true;
}
bool HasSelectedPartnerSummon(){return selection!=nullptr;}
bool ReadSelectedPartnerSummon(void* component,std::uint16_t playable,DWORD& key,std::uint16_t& character) {
    key=0;character=0xffff;void* owner=nullptr;
    if(!selection || !enabled || selection->request.playable!=playable || !field(component,8,owner) || owner!=selection->owner)return false;
    key=selection->request.key;character=selection->request.character;return true;
}
void ReportSelectedPartnerSummon(const PartnerActionObservation& observation) {
    if(!selection)return;selection->consumed=true;
    if(logger){char line[272];sprintf_s(line,"PARTNER MANAGER SUMMON RESULT: id=%llu playable=%u key=%lu result=0x%08lx before=%u after=%u verified=%d pending=%d; original scene checks and native acknowledgements preserved.",
        selection->request.id,static_cast<unsigned>(selection->request.playable),selection->request.key,observation.result,observation.before,observation.after,observation.verified,observation.pending);logger(line);}
}
