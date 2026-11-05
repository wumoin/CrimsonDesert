#define WIN32_LEAN_AND_MEAN
#include "MarriageCommit.h"
#include "BeautyPicker.h"
#include "PartnerSummon.h"
#include "../vendor/minhook/include/MinHook.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <intrin.h>
#include <algorithm>

namespace {
struct RelationshipArray {
    std::uintptr_t data;
    std::uint32_t count, capacity;
};
static_assert(sizeof(RelationshipArray) == 16);

struct EligibilityView {
    std::vector<std::uint8_t> component;
    std::vector<RelationshipArray> arrays;
    std::vector<std::vector<std::uint8_t>> records;
};

MarriageCommitTarget installedTarget{};
MarriageEligibility originalEligibility = nullptr;
LivingPartnerLookup originalLookup = nullptr;
PartnerSpawnControl originalSpawnControl = nullptr;
NativePartnerCreate originalPartnerCreate = nullptr;
NativePartnerRestore originalPartnerRestore = nullptr;
NativeDivorceCurrent originalDivorceCurrent = nullptr;
NativeDivorceByKey originalDivorceByKey = nullptr;
NativeRequestHandler originalStepRequest = nullptr;
NativeRequestHandler originalResetRequest = nullptr, originalDialogueEndRequest = nullptr;
bool spawnHookReady = false;
bool runtimeHookReady = false;
bool restoreHookReady = false;
volatile LONG spawnAllEnabled = 0;
volatile LONG relationshipGuardsEnabled = 0;
struct BeautyRoute {void* component;std::uint16_t playable;DWORD key;};
thread_local BeautyRoute* beautyRoute=nullptr;
PluginLog logger = nullptr;
volatile LONG enabled = 0;
volatile LONG reported = 0;
volatile LONG routeReported = 0;
volatile LONG missingReported = 0;
volatile LONG lastPreflightKey = 0;
volatile LONG lastRejectedKey = 0;
volatile LONG configuredPartnerKey = 0;
SRWLOCK choiceLock = SRWLOCK_INIT;
struct PartnerChoice {
    std::uint16_t playable=0;
    DWORD key=0;
    ULONGLONG when=0;
};
PartnerChoice choices[64] = {};
struct SpawnRoute {
    std::uint16_t playable;
    void* component=nullptr;
    DWORD forcedKey=0, selectedKey=0;
    bool captured=false, selectionCaptured=false;
};
thread_local SpawnRoute* spawnRoute=nullptr;
struct SpawnRouteScope {
    SpawnRoute* previous;
    explicit SpawnRouteScope(SpawnRoute& route):previous(spawnRoute){spawnRoute=&route;}
    ~SpawnRouteScope(){spawnRoute=previous;}
};
// 每次恢复只临时指定一个对象。创建参数由原版读取存档后决定，
// 此处只记录它是否确实请求了同行，不能把“检查过”当成已恢复跟随。
struct RestorePass { bool followRequested = false, reused = false; };
thread_local RestorePass* restorePass = nullptr;
struct RestorePassScope {
    RestorePass* previous;
    explicit RestorePassScope(RestorePass& pass):previous(restorePass){restorePass=&pass;}
    ~RestorePassScope(){restorePass=previous;}
};
struct InteractionTarget { std::uintptr_t owner = 0; DWORD ownerId = 0, key = 0, actorId = 0; };
InteractionTarget interactionTargets[64]{};
SRWLOCK interactionLock = SRWLOCK_INIT;
struct DivorceRoute {
    void* component;
    std::uint16_t playable;
    DWORD key;
    alignas(16) std::uint8_t snapshot[128]{};
};
thread_local DivorceRoute* divorceRoute = nullptr;

bool copyReadable(void* destination, const void* source, std::size_t length) {
    SIZE_T copied = 0;
    return source && ReadProcessMemory(GetCurrentProcess(),source,destination,length,&copied) && copied == length;
}

DWORD readNpcKey(const MarriageCommitTarget& layout, void* npc) {
    std::uintptr_t at = reinterpret_cast<std::uintptr_t>(npc);
    for (unsigned i=0;i<3;++i) {
        if (!at || layout.npcKeyOffsets[i]>4096 ||
            !copyReadable(&at,reinterpret_cast<void*>(at+layout.npcKeyOffsets[i]),sizeof(at))) return 0;
    }
    DWORD key=0;
    if (!at || layout.npcKeyOffsets[3]>4096 ||
        !copyReadable(&key,reinterpret_cast<void*>(at+layout.npcKeyOffsets[3]),sizeof(key))) return 0;
    return key == MAXDWORD ? 0 : key;
}

DWORD wantedPartner(std::uint16_t playable) {
    const auto configured=static_cast<DWORD>(InterlockedCompareExchange(&configuredPartnerKey,0,0));
    if (configured) return configured;
    DWORD key=0;
    AcquireSRWLockShared(&choiceLock);
    for (const auto& choice:choices) if(choice.when && choice.playable==playable) {
        key=choice.key;
        break;
    }
    ReleaseSRWLockShared(&choiceLock);
    return key;
}

struct NativePartnerRecord { DWORD key;std::uintptr_t address; };
bool readMarriedRecords(const MarriageCommitTarget& layout, void* component, std::uint16_t playable,
    std::vector<NativePartnerRecord>& partners) {
    partners.clear();
    if (!component || layout.arrayOffset>4096 || layout.recordStride<16 || layout.recordStride>127 ||
        layout.stateOffset>=layout.recordStride || layout.playableOffset+2>layout.recordStride) return false;
    try {
        RelationshipArray outer{};
        if (!copyReadable(&outer,static_cast<std::uint8_t*>(component)+layout.arrayOffset,sizeof(outer)) ||
            outer.count>64 || outer.capacity<outer.count) return false;
        std::vector<RelationshipArray> arrays(outer.count);
        if (outer.count && !copyReadable(arrays.data(),reinterpret_cast<void*>(outer.data),arrays.size()*sizeof(RelationshipArray)))
            return false;
        std::size_t totalBytes=0;
        for (const auto& array:arrays) {
            if (array.count>4096 || array.capacity<array.count) return false;
            const auto bytes=static_cast<std::size_t>(array.count)*layout.recordStride;
            totalBytes+=bytes;
            if (totalBytes>8*1024*1024) return false;
            if (!bytes) continue;
            std::vector<std::uint8_t> records(bytes);
            if (!copyReadable(records.data(),reinterpret_cast<void*>(array.data),bytes)) return false;
            for (unsigned i=0;i<array.count;++i) {
                const auto record=records.data()+static_cast<std::size_t>(i)*layout.recordStride;
                std::uint16_t owner=0;DWORD key=0;
                std::memcpy(&owner,record+layout.playableOffset,2);
                std::memcpy(&key,record,4);
                if (owner!=playable) continue;
                if(record[layout.stateOffset]!=2)continue;
                if(!key || key==MAXDWORD)return false;
                partners.push_back({key,array.data+static_cast<std::size_t>(i)*layout.recordStride});
            }
        }
        std::sort(partners.begin(),partners.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        return std::adjacent_find(partners.begin(),partners.end(),
            [](const auto& a,const auto& b){return a.key==b.key;})==partners.end();
    } catch (...) { return false; }
}

void* marriedRecord(const MarriageCommitTarget& layout,void* component,std::uint16_t playable,
    DWORD wanted,unsigned& total) {
    total=0;
    std::vector<NativePartnerRecord> partners;
    if(!wanted || !readMarriedRecords(layout,component,playable,partners))return nullptr;
    total=static_cast<unsigned>(partners.size());
    for(const auto& partner:partners)if(partner.key==wanted)return reinterpret_cast<void*>(partner.address);
    return nullptr;
}

// 原版资格函数只读取组件前缀、关系数组与 owner 指针。构造同布局的私有只读视图，
// 让原版重新执行目标关系检查，不碰真实对象，也不自行发送或伪造婚姻成功消息。
std::unique_ptr<EligibilityView> makeView(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable) {
    if (layout.arrayOffset < 16 || layout.arrayOffset > 4096 || layout.recordStride < 16 ||
        layout.recordStride > 127 || layout.stateOffset >= layout.recordStride ||
        layout.playableOffset + sizeof(playable) > layout.recordStride) return nullptr;
    try {
        auto view = std::make_unique<EligibilityView>();
        view->component.resize(layout.arrayOffset + sizeof(RelationshipArray));
        if (!copyReadable(view->component.data(),component,view->component.size())) return nullptr;
        RelationshipArray outer{};
        std::memcpy(&outer,view->component.data()+layout.arrayOffset,sizeof(outer));
        if (!outer.count || outer.count > 64 || outer.capacity < outer.count) return nullptr;
        view->arrays.resize(outer.count);
        if (!copyReadable(view->arrays.data(),reinterpret_cast<void*>(outer.data),
            view->arrays.size()*sizeof(RelationshipArray))) return nullptr;
        view->records.resize(outer.count);
        std::size_t totalBytes = 0;
        bool changed = false;
        for (std::size_t i = 0; i < view->arrays.size(); ++i) {
            auto& array = view->arrays[i];
            if (array.count > 4096 || array.capacity < array.count) return nullptr;
            const auto bytes = static_cast<std::size_t>(array.count)*layout.recordStride;
            totalBytes += bytes;
            if (totalBytes > 8*1024*1024) return nullptr;
            if (!bytes) continue;
            auto& records = view->records[i];
            records.resize(bytes);
            if (!copyReadable(records.data(),reinterpret_cast<void*>(array.data),bytes)) return nullptr;
            for (unsigned j = 0; j < array.count; ++j) {
                auto record = records.data()+static_cast<std::size_t>(j)*layout.recordStride;
                std::uint16_t owner = 0;
                std::memcpy(&owner,record+layout.playableOffset,sizeof(owner));
                if (owner == playable && record[layout.stateOffset] == 2) {
                    // 3 是原检查会排除、并拒绝对同一目标重复提交的状态。
                    // 仅在副本中转换当前角色的现有状态 2；其他角色的关系保持原样。
                    record[layout.stateOffset] = 3;
                    changed = true;
                }
            }
            array.data = reinterpret_cast<std::uintptr_t>(records.data());
            array.capacity = array.count;
        }
        if (!changed) return nullptr; // 没有现有婚姻时，不接管其他原因造成的失败。
        outer.data = reinterpret_cast<std::uintptr_t>(view->arrays.data());
        outer.capacity = outer.count;
        std::memcpy(view->component.data()+layout.arrayOffset,&outer,sizeof(outer));
        return view;
    } catch (...) {
        return nullptr; // 快照失败或内存不足时保留原版的失败结果。
    }
}

bool __fastcall replacementEligibility(void* component, std::uint16_t playable, void* npc) {
    const auto caller=_ReturnAddress();
    const bool active = InterlockedCompareExchange(&enabled,0,0) != 0;
    const bool accepted=EvaluateMarriageEligibility(installedTarget,originalEligibility,component,playable,npc,active);
    if(active) {
        const DWORD key=readNpcKey(installedTarget,npc);
        if(accepted && caller==installedTarget.commitCallReturn) {
            RememberMarriageCommitTarget(installedTarget,component,playable,npc);
            InterlockedExchange(&lastRejectedKey,0);
        } else if(logger && key) {
            const bool explicitPath=caller==installedTarget.commitCallReturn;
            const auto previous=accepted ? InterlockedExchange(&lastPreflightKey,static_cast<LONG>(key)) :
                (explicitPath ? InterlockedExchange(&lastRejectedKey,static_cast<LONG>(key)) : static_cast<LONG>(key));
            if(previous!=static_cast<LONG>(key)) {
                char message[192];
                sprintf_s(message,"%s: playable=%u npcSaveKey=%lu; no partner selection change.",
                    accepted ? "COMMIT PREFLIGHT" : "COMMIT TARGET REJECTED",static_cast<unsigned>(playable),key);
                logger(message);
            }
        }
    }
    return accepted;
}

bool interactionOwner(void* requestContext, std::uintptr_t& owner, DWORD& ownerId, void*& contents, std::uint16_t& playable) {
    std::uintptr_t components = 0, status = 0, cache = 0;
    const auto& layout = installedTarget;
    if (!copyReadable(&owner, requestContext, 8) || !owner ||
        !copyReadable(&ownerId, reinterpret_cast<void*>(owner + layout.runtime.actorIdOffset), 4) ||
        !copyReadable(&components, reinterpret_cast<void*>(owner + layout.npcKeyOffsets[0]), 8) ||
        !copyReadable(&status, reinterpret_cast<void*>(components + layout.npcKeyOffsets[1]), 8) ||
        !copyReadable(&playable, reinterpret_cast<void*>(status + layout.runtime.playableIndexOffset), 2) ||
        !copyReadable(&cache, reinterpret_cast<void*>(components + layout.runtime.contentsComponentOffset), 8)) return false;
    contents = reinterpret_cast<void*>(cache); return ownerId && cache;
}
void clearInteractionTarget(std::uintptr_t owner) {
    AcquireSRWLockExclusive(&interactionLock);
    for (auto& target : interactionTargets) if (!owner || target.owner == owner) target = {};
    ReleaseSRWLockExclusive(&interactionLock);
}
// 从原版请求的显式目标 ActorId 获取对象，不能从最近结婚、战斗锁定或菜单预检猜测。
bool captureInteractionTarget(void* requestContext,const char* source="step") {
    std::uintptr_t owner = 0, packet = 0; DWORD ownerId = 0, actorId = 0,requester=0,interaction=0,step=0;
    void* contents = nullptr; std::uint16_t playable = 0, total = 0, payload = 0;
    if (!interactionOwner(requestContext, owner, ownerId, contents, playable)) return false;
    if (!copyReadable(&packet, static_cast<std::uint8_t*>(requestContext) + 0x18, 8) ||
        !copyReadable(&total, static_cast<std::uint8_t*>(requestContext) + 0x10, 2) ||
        !copyReadable(&payload, reinterpret_cast<void*>(packet + 3), 2) || payload != 16 ||
        total != payload + 5 || !copyReadable(&actorId, reinterpret_cast<void*>(packet + 5), 4) ||
        !copyReadable(&requester,reinterpret_cast<void*>(packet+9),4) ||
        !copyReadable(&interaction,reinterpret_cast<void*>(packet+13),4) ||
        !copyReadable(&step,reinterpret_cast<void*>(packet+17),4)) {clearInteractionTarget(owner);return false;}
    // 同一协议也用于 NPC 与 NPC 的自动交互。两端必须恰有一端是当前玩家，
    // 另一端才可作为伴侣目标；不同交互会交换两端顺序，不能固定把第二端当玩家。
    const DWORD first=actorId,second=requester;
    if(first==ownerId && second!=ownerId)actorId=second;
    else if(second!=ownerId || first==ownerId)return false;
    clearInteractionTarget(owner);
    std::vector<PartnerActor> actors;
    if (!CollectPartnerActors(installedTarget, contents, playable, actors)) return true;
    InteractionTarget selected{};
    for (const auto& actor : actors) if (actor.actorId == actorId) {
        unsigned count = 0;
        if (marriedRecord(installedTarget, contents, playable, actor.key, count)) selected = {owner, ownerId, actor.key, actorId};
    }
    if (!selected.key) {
        if(logger){
            char text[256];sprintf_s(text,"INTERACTION TARGET UNAVAILABLE: source=%s actor=0x%08lx playable=%u otherParticipant=0x%08lx interaction=%lu step=%lu; no verified owned married actor.",source,actorId,static_cast<unsigned>(playable),requester,interaction,step);logger(text);
        }
        return true;
    }
    AcquireSRWLockExclusive(&interactionLock);
    InteractionTarget* entry = &interactionTargets[0];
    for (auto& item : interactionTargets) if (!item.owner || item.owner == owner) { entry = &item; break; }
    *entry = selected;
    ReleaseSRWLockExclusive(&interactionLock);
    if (logger) {
        char text[272]; sprintf_s(text, "INTERACTION TARGET: source=%s owner=0x%08lx playable=%u key=%lu actor=0x%08lx participants=0x%08lx/0x%08lx interaction=%lu step=%lu; verified player/NPC pair.",
            source,ownerId, static_cast<unsigned>(playable), selected.key, actorId,first,second,interaction,step); logger(text);
    }
    return true;
}
void* handleTargetRequest(NativeRequestHandler original, void* request, DWORD* result, void* context, bool reset,const char* source) {
    const bool guard = InterlockedCompareExchange(&relationshipGuardsEnabled, 0, 0) != 0;
    bool applies=reset;
    if (guard && !reset) try { applies=captureInteractionTarget(context,source); } catch (...) { /* 未知目标不放行删除。 */ }
    void* returned = original(request, result, context);
    if (guard && applies && (reset || (result && *result))) {
        std::uintptr_t owner = 0;
        if (copyReadable(&owner, context, 8)) clearInteractionTarget(owner);
    }
    return returned;
}
void* __fastcall replacementStepRequest(void* request, DWORD* result, void* context) {
    return handleTargetRequest(originalStepRequest, request, result, context, false,"step");
}
void* __fastcall replacementResetRequest(void* request, DWORD* result, void* context) {
    return handleTargetRequest(originalResetRequest, request, result, context, true,"reset");
}
void* __fastcall replacementDialogueEndRequest(void* request, DWORD* result, void* context) {
    return handleTargetRequest(originalDialogueEndRequest, request, result, context, true,"end");
}

struct RuntimeActionContext { const MarriageCommitTarget* layout; PartnerSpawnControl original; void* component; std::uint16_t playable; };
DWORD invokeSinglePartner(void* raw, DWORD key, bool spawn) {
    const auto& call = *static_cast<RuntimeActionContext*>(raw);
    SpawnRoute route{call.playable, call.component, key}; SpawnRouteScope scope(route);
    DWORD result = call.layout->runtime.unavailableResult;
    call.original(call.component, &result, spawn, call.playable);
    return route.selectedKey == key ? result : call.layout->runtime.unavailableResult;
}

void* __fastcall replacementDivorceByKey(void* component, DWORD* result, std::uint16_t playable, const DWORD* npcKey) {
    DWORD key = 0; unsigned count = 0;
    if (InterlockedCompareExchange(&relationshipGuardsEnabled, 0, 0) && copyReadable(&key, npcKey, 4) &&
        marriedRecord(installedTarget, component, playable, key, count)) {
        if(logger){
            const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const auto host=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            char text[192];sprintf_s(text,"DIVORCE REQUEST: source=keyed playable=%u key=%lu callerRVA=0x%llx; preserving explicit native target.",
                static_cast<unsigned>(playable),key,static_cast<unsigned long long>(caller>=host ? caller-host : 0));logger(text);
        }
        RuntimeActionContext call{&installedTarget, originalSpawnControl, component, playable};
        DWORD interactionActor=0;
        if(divorceRoute && divorceRoute->key==key)std::memcpy(&interactionActor,divorceRoute->snapshot+4,4);
        if (!PreparePartnerDivorce(installedTarget, component, playable, key, interactionActor, invokeSinglePartner, &call)) {
            if (result) *result = installedTarget.runtime.unavailableResult;
            if (logger) logger("DIVORCE BLOCKED: partner instances could not be dismissed; relationship unchanged.");
            return result;
        }
    }
    void* returned = originalDivorceByKey(component, result, playable, npcKey);
    if (result && !*result && key) {
        AcquireSRWLockExclusive(&choiceLock);
        for (auto& choice : choices) if (choice.playable == playable && choice.key == key) choice = {};
        ReleaseSRWLockExclusive(&choiceLock);
    }
    if(logger && key && InterlockedCompareExchange(&relationshipGuardsEnabled,0,0)) {
        std::vector<NativePartnerRecord> remaining;
        const bool readable=readMarriedRecords(installedTarget,component,playable,remaining);
        char text[192];sprintf_s(text,"DIVORCE RESULT: key=%lu result=0x%08lx remainingMarried=%u snapshotValid=%d; native call returned.",
            key,result?*result:MAXDWORD,static_cast<unsigned>(remaining.size()),readable);logger(text);
    }
    return returned;
}

void* __fastcall replacementDivorceCurrent(void* component, DWORD* result) {
    if (!InterlockedCompareExchange(&relationshipGuardsEnabled, 0, 0)) return originalDivorceCurrent(component, result);
    std::uintptr_t owner = 0; DWORD ownerId = 0; void* contents = nullptr; std::uint16_t playable = 0;
    if (!copyReadable(&owner, static_cast<std::uint8_t*>(component) + 8, 8)) owner = 0;
    InteractionTarget selected{};
    if (owner && interactionOwner(&owner, owner, ownerId, contents, playable)) {
        AcquireSRWLockShared(&interactionLock);
        for (const auto& item : interactionTargets) if (item.owner == owner && item.ownerId == ownerId) selected = item;
        ReleaseSRWLockShared(&interactionLock);
    }
    unsigned count = 0;
    void* record = selected.key ? marriedRecord(installedTarget, contents, playable, selected.key, count) : nullptr;
    std::vector<PartnerActor> actors;
    bool targetAlive = record && CollectPartnerActors(installedTarget, contents, playable, actors) &&
        std::any_of(actors.begin(), actors.end(), [&](const auto& actor) { return actor.key == selected.key && actor.actorId == selected.actorId; });
    DivorceRoute route{contents, playable, selected.key};
    if (!targetAlive || !copyReadable(route.snapshot, record, installedTarget.recordStride)) {
        if (result) *result = installedTarget.runtime.unavailableResult;
        if (logger) logger("DIVORCE BLOCKED: no verified current interaction partner; reopen interaction. No latest/first-partner fallback.");
        return result;
    }
    std::memcpy(route.snapshot+4,&selected.actorId,4);
    // 原函数删除数组后还会读取查询结果。独立快照避免末项换位导致通知另一位伴侣。
    struct Scope { DivorceRoute* previous; explicit Scope(DivorceRoute* p):previous(divorceRoute){divorceRoute=p;} ~Scope(){divorceRoute=previous;} } scope(&route);
    if (logger) { char text[144]; sprintf_s(text, "DIVORCE TARGET: key=%lu actor=0x%08lx; interaction target and stable record snapshot.", selected.key, selected.actorId); logger(text); }
    void* returned = originalDivorceCurrent(component, result);
    if (result && !*result) clearInteractionTarget(owner);
    return returned;
}

void* __fastcall replacementPartnerCreate(void* component, DWORD* result, std::uint16_t playable,
    DWORD* actorId, const DWORD* npcKey, bool party) {
    if(HasSelectedPartnerSummon()) {
        DWORD selected=0,actual=0;std::uint16_t character=0xffff;
        if(!ReadSelectedPartnerSummon(component,playable,selected,character) || !copyReadable(&actual,npcKey,sizeof(actual)) || actual!=selected) {
            if(result)*result=installedTarget.runtime.unavailableResult;return result;
        }
        // 原版同行创建分支使用当前操控角色附近的位置；房屋分支使用住处出场点。
        party=true;
    }
    if (restorePass && party) restorePass->followRequested = true;
    DWORD key = 0, reused = 0, status = installedTarget.runtime.unavailableResult;
    if (InterlockedCompareExchange(&relationshipGuardsEnabled, 0, 0) && copyReadable(&key, npcKey, 4)) {
        try {
            if (ReuseExistingPartnerActor(installedTarget, component, playable, key, reused, status)) {
                if (restorePass && !status) restorePass->reused = true;
                if (result) *result = status; if (!status && actorId) *actorId = reused; return result;
            }
        } catch (...) {
            if (result) *result = installedTarget.runtime.unavailableResult; return result;
        }
    }
    return originalPartnerCreate(component, result, playable, actorId, npcKey, party);
}

void* __fastcall replacementLookup(void* component,std::uint16_t playable) {
    if (_ReturnAddress() == installedTarget.runtime.divorceLookupReturn && InterlockedCompareExchange(&relationshipGuardsEnabled,0,0))
        return divorceRoute && divorceRoute->component == component && divorceRoute->playable == playable ? divorceRoute->snapshot : nullptr;
    if (_ReturnAddress()==installedTarget.beautyLookupReturn && InterlockedCompareExchange(&relationshipGuardsEnabled,0,0)) {
        const auto key=beautyRoute && beautyRoute->component==component && beautyRoute->playable==playable ? beautyRoute->key : 0;
        if(key) {
            unsigned total=0;
            void* record=marriedRecord(installedTarget,component,playable,key,total);
            if(logger) {char message[192];sprintf_s(message,"BEAUTY TARGET: playable=%u key=%lu marriedCandidates=%u verified=%d; scoped native beauty query.",static_cast<unsigned>(playable),key,total,record!=nullptr);logger(message);}
            // 选择的对象失效时不能静默改为另一位伴侣。
            return record;
        }
    }
    return EvaluateLivingPartnerSelection(installedTarget,originalLookup,component,playable,
        InterlockedCompareExchange(&enabled,0,0)!=0);
}

void* __fastcall replacementSpawnControl(void* component,void* result,bool spawn,std::uint16_t playable) {
    try {
        if(HasSelectedPartnerSummon() && !spawnRoute) {
            DWORD key=0;std::uint16_t character=0xffff;PartnerActionObservation action{};action.result=installedTarget.runtime.unavailableResult;
            if(spawn && ReadSelectedPartnerSummon(component,playable,key,character))
                action=EvaluateSelectedPartnerSpawn(installedTarget,originalSpawnControl,component,playable,key,character);
            ReportSelectedPartnerSummon(action);if(result)std::memcpy(result,&action.result,sizeof(action.result));return result;
        }
        return EvaluatePartnerSpawnAction(installedTarget,originalSpawnControl,component,result,spawn,playable,
            InterlockedCompareExchange(&spawnAllEnabled,0,0)!=0);
    } catch (...) {
        // 部分动作可能已经执行，异常后不能再次运行整个原动作。
        const auto error=installedTarget.runtime.unavailableResult;
        if(result)std::memcpy(result,&error,4);
        if(logger)logger("PARTNER ACTION BLOCKED: reconciliation could not complete; no blind retry.");
        return result;
    }
}

void* __fastcall replacementPartnerRestore(void* component, DWORD* result) {
    // 恢复与手动召唤共用 SpawnAllPartners，但入口不同；不向工作线程投递游戏调用。
    if (!component || !result || !InterlockedCompareExchange(&spawnAllEnabled, 0, 0) || spawnRoute || restorePass)
        return originalPartnerRestore(component, result);
    void* primaryReturned = nullptr;
    bool primaryComplete = false;
    try {
        std::uintptr_t owner = 0; DWORD ownerId = 0; void* contents = nullptr; std::uint16_t playable = 0;
        if (!copyReadable(&owner, static_cast<std::uint8_t*>(component) + 8, 8) ||
            !interactionOwner(&owner, owner, ownerId, contents, playable) || contents != component)
            return originalPartnerRestore(component, result);
        std::vector<NativePartnerRecord> partners;
        if (!readMarriedRecords(installedTarget, component, playable, partners) || partners.empty())
            return originalPartnerRestore(component, result);

        DWORD primaryKey = 0; unsigned checked = 0, followRequests = 0, reused = 0, errors = 0;
        const auto restoreOne = [&](DWORD forcedKey, DWORD* output, DWORD& selectedKey) {
            SpawnRoute route{playable, component, forcedKey}; SpawnRouteScope routeScope(route);
            RestorePass pass{}; RestorePassScope passScope(pass);
            void* returned = originalPartnerRestore(component, output);
            selectedKey = route.selectedKey;
            ++checked; if (pass.followRequested) ++followRequests; if (pass.reused) ++reused;
            DWORD code = installedTarget.runtime.unavailableResult;
            copyReadable(&code, output, sizeof(code));
            if (code) ++errors;
            if (logger) {
                char text[256];
                sprintf_s(text, "PARTNER RESTORE CHECK: key=%lu requestedKey=%lu followRequested=%d reused=%d result=0x%08lx; native saved-party decision preserved.",
                    selectedKey, forcedKey, pass.followRequested, pass.reused, code); logger(text);
            }
            return returned;
        };
        // 首次调用保留原版/配置选中的对象、返回指针和错误码。
        // 后续只临时切换查询结果，仍由原版读取各自 _isParty；等待中的对象不会被强制同行。
        primaryReturned = restoreOne(0, result, primaryKey);
        primaryComplete = true;
        for (const auto& partner : partners) {
            if (partner.key == primaryKey) continue;
            // 原调用可能更新缓存，不能跨调用使用先前快照中的记录地址。
            unsigned count = 0;
            if (!marriedRecord(installedTarget, component, playable, partner.key, count)) continue;
            DWORD extraResult = installedTarget.runtime.unavailableResult, selectedKey = 0;
            restoreOne(partner.key, &extraResult, selectedKey);
        }
        if (logger) {
            char text[240];
            sprintf_s(text, "PARTNER RESTORE: playable=%u primaryKey=%lu marriedRecords=%u checked=%u followRequests=%u reused=%u errors=%u; native calls returned, following requires gameplay verification.",
                static_cast<unsigned>(playable), primaryKey, static_cast<unsigned>(partners.size()), checked, followRequests, reused, errors); logger(text);
        }
        return primaryReturned;
    } catch (...) {
        // 一部分对象可能已经恢复，异常后不能重跑首个原版动作而制造副本。
        if (!primaryComplete) *result = installedTarget.runtime.unavailableResult;
        if (logger) logger("PARTNER RESTORE INCOMPLETE: no blind retry after partial native work.");
        return primaryComplete ? primaryReturned : result;
    }
}
}

bool RememberMarriageCommitTarget(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, void* npc) {
    if (!component) return false;
    const DWORD key=readNpcKey(layout,npc);
    if (!key) return false;
    AcquireSRWLockExclusive(&choiceLock);
    PartnerChoice* selected=&choices[0];
    for(auto& choice:choices) {
        if(choice.when && choice.playable==playable) { selected=&choice;break; }
        if(!choice.when || choice.when<selected->when) selected=&choice;
    }
    *selected={playable,key,GetTickCount64()};
    ReleaseSRWLockExclusive(&choiceLock);
    InterlockedExchange(&routeReported,0);
    InterlockedExchange(&missingReported,0);
    if (logger) {
        char message[192];
        sprintf_s(message,"COMMIT TARGET: playable=%u npcSaveKey=%lu; awaiting native married record.",
            static_cast<unsigned>(playable),key);
        logger(message);
    }
    return true;
}

void* EvaluateLivingPartnerSelection(const MarriageCommitTarget& layout, LivingPartnerLookup original,
    void* component, std::uint16_t playable, bool active) {
    if (!original) return nullptr;
    void* native=original(component,playable);
    auto* scope=spawnRoute && spawnRoute->playable==playable ? spawnRoute : nullptr;
    if(scope && !scope->captured) {
        scope->captured=true;
        if(scope->component && scope->component!=component)return nullptr;
        scope->component=component;
    }
    auto capture=[&](void* selected){
        if(scope && scope->component==component && !scope->selectionCaptured) {
            scope->selectionCaptured=true;
            scope->selectedKey=0;
            if(selected)copyReadable(&scope->selectedKey,selected,sizeof(scope->selectedKey));
        }
        return selected;
    };
    if(!native)return capture(nullptr);
    if(scope && scope->component==component && scope->forcedKey) {
        // 出场扩展每次只临时指定一个真实已婚对象，不改全局当前对象偏好。
        unsigned ignored=0;
        return capture(marriedRecord(layout,component,playable,scope->forcedKey,ignored));
    }
    if (!active) return capture(native);
    const auto wanted=wantedPartner(playable);
    if (!wanted) return capture(native);
    unsigned total=0;
    void* selected=marriedRecord(layout,component,playable,wanted,total);
    if (!selected) {
        if(logger && InterlockedCompareExchange(&missingReported,1,0)==0) {
            char message[224];
            sprintf_s(message,"PARTNER ROUTE WAIT: playable=%u requestedKey=%lu observedMarried=%u; no unique verified match; keeping native selection.",
                static_cast<unsigned>(playable),wanted,total);
            logger(message);
        }
        return capture(native); // 未实际写入、不是本角色伴侣或快照无效时保留原选择。
    }
    if (logger && InterlockedCompareExchange(&routeReported,1,0)==0) {
        char message[224];
        DWORD nativeKey=0;copyReadable(&nativeKey,native,sizeof(nativeKey));
        sprintf_s(message,"PARTNER ROUTE: playable=%u nativeKey=%lu selectedKey=%lu state=2 marriedCandidates=%u; selected an existing native married record.",
            static_cast<unsigned>(playable),nativeKey,wanted,total);
        logger(message);
    }
    return capture(selected);
}

PartnerActionObservation EvaluateSelectedPartnerSpawn(const MarriageCommitTarget& layout,PartnerSpawnControl original,
    void* component,std::uint16_t playable,DWORD key,std::uint16_t character) {
    PartnerActionObservation denied{};denied.result=layout.runtime.unavailableResult;unsigned count=0;std::uint16_t actual=0xffff;
    const auto record=marriedRecord(layout,component,playable,key,count);
    if(!original || !record || !copyReadable(&actual,static_cast<const BYTE*>(record)+0xc,sizeof(actual)) || actual!=character)return denied;
    RuntimeActionContext call{&layout,original,component,playable};
    return ReconcilePartnerAction(layout,component,playable,key,true,invokeSinglePartner,&call);
}

void* EvaluatePartnerSpawnAction(const MarriageCommitTarget& layout,PartnerSpawnControl original,
    void* component,void* result,bool spawn,std::uint16_t playable,bool active) {
    if(!original)return result;
    if(!active || spawnRoute)return original(component,result,spawn,playable);
    std::vector<NativePartnerRecord> partners;
    if (!readMarriedRecords(layout,component,playable,partners) || partners.empty()) return original(component,result,spawn,playable);
    DWORD primaryKey = wantedPartner(playable);
    if (std::none_of(partners.begin(),partners.end(),[&](const auto& partner){return partner.key == primaryKey;}))
        primaryKey = partners.front().key;
    std::stable_sort(partners.begin(),partners.end(),[&](const auto& a,const auto& b){return a.key==primaryKey && b.key!=primaryKey;});
    RuntimeActionContext call{&layout,original,component,playable};
    DWORD primaryResult=layout.runtime.unavailableResult;
    unsigned attempted=0,accepted=0,verified=0,pending=0;
    for(const auto& partner:partners) {
        const auto action=ReconcilePartnerAction(layout,component,playable,partner.key,spawn,invokeSinglePartner,&call);
        if(partner.key==primaryKey)primaryResult=action.result;
        else {++attempted;if(!action.result)++accepted;}
        if(action.verified)++verified;if(action.pending)++pending;
        // 一位对象失败不再阻止其余伴侣执行各自的原版场景检查。
    }
    if(result)std::memcpy(result,&primaryResult,sizeof(primaryResult));
    if(logger) {
        char message[272];
        sprintf_s(message,"PARTNER ACTION: spawn=%d playable=%u primaryKey=%lu marriedRecords=%u extrasAttempted=%u extrasAccepted=%u verified=%u pending=%u; instance registry checked.",
            spawn,static_cast<unsigned>(playable),primaryKey,static_cast<unsigned>(partners.size()),attempted,accepted,verified,pending);
        logger(message);
    }
    return result;
}

bool EvaluateMarriageEligibility(const MarriageCommitTarget& layout, MarriageEligibility original,
    void* component, std::uint16_t playable, void* npc, bool active) {
    // 先执行完整原检查：本来允许、开关关闭或参数缺失时均不覆盖。
    if (!original) return false;
    if (original(component,playable,npc)) return true;
    if (!active || !component || !npc) return false;
    auto view = makeView(layout,component,playable);
    if (!view) return false;
    // 同一目标已婚时，在私有视图里会被原版的“当前目标状态 3”分支拒绝。
    // 目标属于其他角色、目标已处于排除状态等其他检查仍由原版执行。
    const bool accepted = original(view->component.data(),playable,npc);
    if (accepted && logger && InterlockedCompareExchange(&reported,1,0) == 0)
        logger("COMMIT RECHECK: native eligibility accepted a private view excluding existing partners; native state write still follows.");
    return accepted;
}

bool InstallMarriageCommitHook(HMODULE module, PluginLog writeLog, void* livingEvaluator) {
    logger = writeLog;
    SetPartnerRuntimeLog(writeLog);
    const char* error = nullptr;
    bool found = false;
    try { found = FindMarriageCommitTarget(module,installedTarget,error,livingEvaluator); }
    catch (...) { error = "marriage eligibility discovery failed"; }
    char message[256];
    if (!found) {
        sprintf_s(message,"COMMIT HOOK UNAVAILABLE: %s.",error ? error : "target not found");
        if (logger) logger(message);
        return false;
    }
    if (!FindPartnerRuntimeTarget(module,installedTarget,error)) {
        sprintf_s(message,"COMMIT HOOK UNAVAILABLE: relationship guards not compatible: %s.",error ? error : "unknown layout");
        if(logger)logger(message);
        return false;
    }
    if(FindBeautyPartnerLookup(module,installedTarget)) {
        if(logger)logger("BEAUTY ROUTE RESOLVED: isolated native beauty lookup caller.");
    } else if(logger)logger("BEAUTY ROUTE UNAVAILABLE: original beauty lookup caller not uniquely verified.");
    sprintf_s(message,"COMMIT RESOLVED: eligibility RVA=0x%08lx arrays=0x%x stride=0x%x state=0x%x playable=0x%x",
        installedTarget.functionRva,installedTarget.arrayOffset,installedTarget.recordStride,
        installedTarget.stateOffset,installedTarget.playableOffset);
    if (logger) logger(message);
    sprintf_s(message,"PARTNER ROUTE RESOLVED: lookup RVA=0x%08lx writer RVA=0x%08lx key offsets=0x%x/0x%x/0x%x/0x%x",
        installedTarget.lookupRva,installedTarget.writerRva,installedTarget.npcKeyOffsets[0],installedTarget.npcKeyOffsets[1],
        installedTarget.npcKeyOffsets[2],installedTarget.npcKeyOffsets[3]);
    if(logger)logger(message);
    struct Hook { void* target; void* replacement; void** original; };
    const auto& rt=installedTarget.runtime;
    const Hook hooks[]={
        {reinterpret_cast<void*>(installedTarget.function),reinterpret_cast<void*>(&replacementEligibility),reinterpret_cast<void**>(&originalEligibility)},
        {reinterpret_cast<void*>(installedTarget.lookup),reinterpret_cast<void*>(&replacementLookup),reinterpret_cast<void**>(&originalLookup)},
        {rt.divorceCurrent,reinterpret_cast<void*>(&replacementDivorceCurrent),reinterpret_cast<void**>(&originalDivorceCurrent)},
        {reinterpret_cast<void*>(rt.divorceByKey),reinterpret_cast<void*>(&replacementDivorceByKey),reinterpret_cast<void**>(&originalDivorceByKey)},
        {reinterpret_cast<void*>(rt.create),reinterpret_cast<void*>(&replacementPartnerCreate),reinterpret_cast<void**>(&originalPartnerCreate)},
        {reinterpret_cast<void*>(installedTarget.spawnControl),reinterpret_cast<void*>(&replacementSpawnControl),reinterpret_cast<void**>(&originalSpawnControl)},
        {reinterpret_cast<void*>(rt.stepRequest),reinterpret_cast<void*>(&replacementStepRequest),reinterpret_cast<void**>(&originalStepRequest)},
        {reinterpret_cast<void*>(rt.resetRequest),reinterpret_cast<void*>(&replacementResetRequest),reinterpret_cast<void**>(&originalResetRequest)},
        {reinterpret_cast<void*>(rt.dialogueEndRequest),reinterpret_cast<void*>(&replacementDialogueEndRequest),reinterpret_cast<void**>(&originalDialogueEndRequest)}
    };
    MH_STATUS status = MH_Initialize(); unsigned installed=0;
    if(status==MH_ERROR_ALREADY_INITIALIZED)status=MH_OK;
    if(status==MH_OK)for(const auto& hook:hooks) {
        status=MH_CreateHook(hook.target,hook.replacement,hook.original);
        if(status!=MH_OK)break;
        ++installed;status=MH_QueueEnableHook(hook.target);if(status!=MH_OK)break;
    }
    if(status==MH_OK)status=MH_ApplyQueued();
    if (status != MH_OK) {
        // 目标、上下文和实例保护必须成套发布，不留下已启用的半套对象路由。
        for(unsigned i=0;i<installed;++i){MH_DisableHook(hooks[i].target);MH_RemoveHook(hooks[i].target);}
        sprintf_s(message,"COMMIT HOOK UNAVAILABLE: MinHook %s.",MH_StatusToString(status));
        if (logger) logger(message);
        return false;
    }
    if (logger) logger("COMMIT HOOK INSTALLED: original eligibility plus private-view recheck; override defaults off.");
    if (logger) logger("PARTNER ROUTE INSTALLED: native write target is preferred only after its married record exists.");
    runtimeHookReady=true;spawnHookReady=true;
    if(logger){
        sprintf_s(message,"PARTNER SPAWN RESOLVED: control RVA=0x%08lx; actor registry and creation guard compatible.",installedTarget.spawnRva);logger(message);
        logger("PARTNER SPAWN HOOK INSTALLED: instance reconciliation, native creation reuse and per-partner actions.");
        sprintf_s(message,"DIVORCE GUARD INSTALLED: RVA=0x%08lx; explicit interaction target and stable result snapshot.",rt.divorceRva);logger(message);
    }
    // 恢复入口是独立的可选扩展。新版本无法识别该入口或安装失败时，
    // 原版恢复与此前已安装的九个挂钩保持可用，不撤销整组关系功能。
    bool restoreFound = false;
    try { restoreFound = FindPartnerRestoreTarget(module, installedTarget, error); }
    catch (...) { error = "partner restore discovery failed"; }
    if (restoreFound) {
        const auto address = reinterpret_cast<void*>(rt.restore);
        status = MH_CreateHook(address, reinterpret_cast<void*>(&replacementPartnerRestore), reinterpret_cast<void**>(&originalPartnerRestore));
        if (status == MH_OK) {
            status = MH_EnableHook(address);
            if (status != MH_OK) { MH_DisableHook(address); MH_RemoveHook(address); }
            else restoreHookReady = true;
        }
        if (!restoreHookReady) error = MH_StatusToString(status);
    }
    if (logger) {
        if (restoreHookReady)
            sprintf_s(message, "PARTNER RESTORE HOOK INSTALLED: RVA=0x%08lx; per-partner native saved-party restoration.", rt.restoreRva);
        else sprintf_s(message, "PARTNER RESTORE UNAVAILABLE: %s; native single-partner restore retained.", error ? error : "unknown layout");
        logger(message);
    }
    InstallPartnerSummon(module,installedTarget,logger);
    InstallBeautyPicker(module,installedTarget,logger);
    return true;
}

bool ReadBeautyPartners(void* component,std::uint16_t playable,std::vector<BeautyPartnerOption>& options) {
    options.clear();std::vector<NativePartnerRecord> records;
    if(!readMarriedRecords(installedTarget,component,playable,records))return false;
    for(const auto& record:records) {
        std::uint16_t character=0xffff;DWORD actorId=0;
        if(!copyReadable(&character,reinterpret_cast<void*>(record.address+0xc),2) ||
            !copyReadable(&actorId,reinterpret_cast<void*>(record.address+4),4))return false;
        options.push_back({record.key,character,actorId});
    }
    return true;
}
bool VerifyBeautyPartner(void* component,std::uint16_t playable,DWORD key) {
    unsigned total=0;return marriedRecord(installedTarget,component,playable,key,total)!=nullptr;
}
void RunBeautyForPartner(NativeBeautyEntry original,void* self,void* a2,void* a3,void* payload,
    void* component,std::uint16_t playable,DWORD key) {
    BeautyRoute route{component,playable,key};
    struct Guard {BeautyRoute* previous;explicit Guard(BeautyRoute& value):previous(beautyRoute){beautyRoute=&value;}~Guard(){beautyRoute=previous;}} guard(route);
    original(self,a2,a3,payload);
}

bool IsPartnerSpawnHookReady(){return spawnHookReady;}
bool IsPartnerRuntimeReady(){return runtimeHookReady;}
bool IsPartnerRestoreHookReady(){return restoreHookReady;}

void SetPreferredPartnerKey(DWORD key) {
    InterlockedExchange(&configuredPartnerKey,static_cast<LONG>(key));
    InterlockedExchange(&routeReported,0);
    InterlockedExchange(&missingReported,0);
}

void SetMarriageCommitOverride(bool active,DWORD preferredPartnerKey,bool spawnAll,bool relationshipGuards) {
    InterlockedExchange(&relationshipGuardsEnabled,relationshipGuards && runtimeHookReady ? 1 : 0);
    if(!relationshipGuards)clearInteractionTarget(0);
    InterlockedExchange(&spawnAllEnabled,spawnAll && spawnHookReady ? 1 : 0);
    const auto oldKey=InterlockedExchange(&configuredPartnerKey,static_cast<LONG>(preferredPartnerKey));
    const auto previous=InterlockedExchange(&enabled,active ? 1 : 0);
    if(oldKey!=static_cast<LONG>(preferredPartnerKey)) {
        InterlockedExchange(&routeReported,0);
        InterlockedExchange(&missingReported,0);
    }
    if (!active) {
        InterlockedExchange(&reported,0);
        if(previous) {
            AcquireSRWLockExclusive(&choiceLock);
            for(auto& choice:choices)choice={};
            ReleaseSRWLockExclusive(&choiceLock);
            InterlockedExchange(&routeReported,0);
            InterlockedExchange(&missingReported,0);
            InterlockedExchange(&lastPreflightKey,0);
            InterlockedExchange(&lastRejectedKey,0);
        }
    }
}
