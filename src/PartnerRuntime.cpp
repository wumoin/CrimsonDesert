#define WIN32_LEAN_AND_MEAN
#include "MarriageCommit.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {
struct RuntimeArray { std::uintptr_t data; DWORD count, capacity; };
PluginLog runtimeLog = nullptr;
// 只保存 ID、地址和所属对象用于识别异步收回，不跨帧解引用保存的裸指针。
struct PendingRemoval { std::uintptr_t owner, address; DWORD actorId, key; };
std::vector<PendingRemoval> pendingRemovals;
SRWLOCK pendingLock = SRWLOCK_INIT;
struct PendingLockScope {
    bool exclusive;
    explicit PendingLockScope(bool write):exclusive(write){if(write)AcquireSRWLockExclusive(&pendingLock);else AcquireSRWLockShared(&pendingLock);}
    ~PendingLockScope(){if(exclusive)ReleaseSRWLockExclusive(&pendingLock);else ReleaseSRWLockShared(&pendingLock);}
};
thread_local DWORD preferredReuse = 0;

bool runtimeRead(void* out, const void* at, std::size_t bytes) {
    SIZE_T got = 0;
    return at && ReadProcessMemory(GetCurrentProcess(), at, out, bytes, &got) && got == bytes;
}
template<class T> bool runtimeField(std::uintptr_t at, unsigned offset, T& value) {
    return at && offset <= 4096 && runtimeRead(&value, reinterpret_cast<void*>(at + offset), sizeof(value));
}
bool ownerInfo(const MarriageCommitTarget& layout, void* component, std::uintptr_t& owner, DWORD& id) {
    return runtimeField(reinterpret_cast<std::uintptr_t>(component), 8, owner) &&
        runtimeField(owner, layout.runtime.actorIdOffset, id) && id;
}
bool actorMatches(const MarriageCommitTarget& layout, std::uintptr_t actor, std::uint16_t playable,
    DWORD ownerId, DWORD& key, DWORD& id) {
    std::uintptr_t components = 0, status = 0, save = 0;
    std::uint16_t actorPlayable = 0; DWORD actorOwner = 0;
    return runtimeField(actor, layout.runtime.actorIdOffset, id) && id &&
        runtimeField(actor, layout.npcKeyOffsets[0], components) &&
        runtimeField(components, layout.npcKeyOffsets[1], status) &&
        runtimeField(status, layout.runtime.companionPlayableOffset, actorPlayable) && actorPlayable == playable &&
        runtimeField(status, layout.runtime.companionOwnerOffset, actorOwner) && actorOwner == ownerId &&
        runtimeField(status, layout.npcKeyOffsets[2], save) &&
        runtimeField(save, layout.npcKeyOffsets[3], key) && key && key != MAXDWORD;
}
bool locateRecord(const MarriageCommitTarget& layout, void* component, std::uint16_t playable,
    DWORD key, std::uintptr_t& selected, DWORD& actorId) {
    RuntimeArray outer{}; selected = 0;
    if (!runtimeField(reinterpret_cast<std::uintptr_t>(component), layout.arrayOffset, outer) ||
        outer.count > 64 || outer.capacity < outer.count || layout.recordStride < 16 || layout.recordStride > 127)
        return false;
    if(layout.stateOffset>=layout.recordStride || layout.playableOffset+2>layout.recordStride)return false;
    for (unsigned i = 0; i < outer.count; ++i) {
        RuntimeArray inner{};
        if (!runtimeRead(&inner, reinterpret_cast<void*>(outer.data + i * sizeof(inner)), sizeof(inner)) ||
            inner.count > 4096 || inner.capacity < inner.count) return false;
        for (unsigned j = 0; j < inner.count; ++j) {
            std::uint8_t row[128]{};
            const auto at = inner.data + static_cast<std::size_t>(j) * layout.recordStride;
            if (!runtimeRead(row, reinterpret_cast<void*>(at), layout.recordStride)) return false;
            DWORD rowKey = 0; std::uint16_t player = 0;
            std::memcpy(&rowKey, row, 4); std::memcpy(&player, row + layout.playableOffset, 2);
            if (rowKey != key || player != playable || row[layout.stateOffset] != 2) continue;
            if (selected) return false;
            selected = at; std::memcpy(&actorId, row + 4, 4);
        }
    }
    return selected != 0;
}
bool bindInstance(const MarriageCommitTarget& layout, void* component, std::uint16_t playable,
    DWORD key, DWORD desired) {
    std::uintptr_t at = 0; DWORD previous = 0;
    if (!locateRecord(layout, component, playable, key, at, previous)) return false;
    MEMORY_BASIC_INFORMATION page{};
    if (!VirtualQuery(reinterpret_cast<void*>(at + 4), &page, sizeof(page)) || page.State != MEM_COMMIT ||
        (page.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !((page.Protect & 0xff) == PAGE_READWRITE || (page.Protect & 0xff) == PAGE_WRITECOPY)) return false;
    // 对应原版创建 / 收回路径的 DWORD 实例字段。只在原游戏调用线程修改关联，
    // 婚姻状态、对象键和持久关系均不改；数组地址每次重新查找，不缓存删除前的指针。
    return static_cast<DWORD>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at + 4),
        static_cast<LONG>(desired), static_cast<LONG>(previous))) == previous;
}
bool pending(const PartnerActor& actor, std::uintptr_t owner) {
    bool found = false;
    PendingLockScope lock(false);
    for (const auto& item : pendingRemovals)
        if (item.owner == owner && item.actorId == actor.actorId && item.address == actor.address && item.key == actor.key)
            found = true;
    return found;
}
void rememberRemoval(const PartnerActor& actor, std::uintptr_t owner) {
    PendingLockScope lock(true);
    if (pendingRemovals.size() < 4096) pendingRemovals.push_back({owner, actor.address, actor.actorId, actor.key});
}
void logObservation(DWORD key, bool spawn, const PartnerActionObservation& action) {
    if (!runtimeLog) return;
    char text[256];
    sprintf_s(text, "PARTNER INSTANCE: key=%lu spawn=%d before=%u after=%u removedRequests=%u pending=%d verified=%d result=0x%08lx",
        key, spawn, action.before, action.after, action.removed, action.pending, action.verified, action.result);
    runtimeLog(text);
}
}

void SetPartnerRuntimeLog(PluginLog log) { runtimeLog = log; }

bool CollectPartnerActors(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, std::vector<PartnerActor>& actors) {
    actors.clear(); const auto& api = layout.runtime;
    if (!api.worldSlot || !api.actorIdOffset || !api.companionOwnerOffset) return false;
    std::uintptr_t owner = 0, root = 0, manager = 0; DWORD ownerId = 0, bucketsCount = 0;
    std::uintptr_t buckets = 0, entries = 0;
    if (!ownerInfo(layout, component, owner, ownerId) || !runtimeRead(&root, api.worldSlot, sizeof(root)) ||
        !runtimeField(root, api.managerOffset, manager) ||
        !runtimeField(manager, api.bucketCountOffset, bucketsCount) || !bucketsCount || bucketsCount > 65536 ||
        !runtimeField(manager, api.bucketsOffset, buckets) || !runtimeField(manager, api.entriesOffset, entries)) return false;
    std::vector<std::uint8_t> table(static_cast<std::size_t>(bucketsCount) * 256);
    std::vector<std::pair<DWORD,std::uintptr_t>> liveIds;
    if (!runtimeRead(table.data(), reinterpret_cast<void*>(buckets), table.size())) return false;
    bool ownerFound = false;
    for (unsigned i = 0; i < bucketsCount; ++i) {
        DWORD count = 0; std::memcpy(&count, table.data() + i * 256, 4);
        if (count > 31) return false;
        for (unsigned j = 0; j < count; ++j) {
            DWORD key = 0, index = 0; std::uintptr_t entry = 0, actor = 0;
            std::memcpy(&key, table.data() + i * 256 + 8 + j * 8, 4);
            std::memcpy(&index, table.data() + i * 256 + 12 + j * 8, 4);
            if (index > 1000000 || !runtimeRead(&entry, reinterpret_cast<void*>(entries + index * 8ull), 8)) return false;
            DWORD actualKey = 0;
            if (!runtimeField(entry, 4, actualKey) || actualKey != key || !runtimeField(entry, 8, actor)) return false;
            DWORD liveId=0;
            if(runtimeField(actor,api.actorIdOffset,liveId))liveIds.push_back({liveId,actor});
            if (actor == owner) { ownerFound = true; continue; }
            DWORD npcKey = 0, actorId = 0;
            if (!actorMatches(layout, actor, playable, ownerId, npcKey, actorId) || (actorId & 0xfffff) != key) continue;
            actors.push_back({npcKey, actorId, actor});
        }
    }
    // 同一 ActorId 的客户端副本不能作为服务端原生动作的控制器。
    if (!ownerFound) { actors.clear(); return false; }
    std::sort(actors.begin(), actors.end(), [](const auto& a, const auto& b) { return a.actorId < b.actorId; });
    actors.erase(std::unique(actors.begin(), actors.end(), [](const auto& a, const auto& b) {
        return a.actorId == b.actorId && a.address == b.address;
    }), actors.end());
    PendingLockScope lock(true);
    pendingRemovals.erase(std::remove_if(pendingRemovals.begin(), pendingRemovals.end(), [&](const auto& item) {
        return std::none_of(liveIds.begin(), liveIds.end(), [&](const auto& a) {
            return a.first == item.actorId && a.second == item.address;
        });
    }), pendingRemovals.end());
    return true;
}

bool ReuseExistingPartnerActor(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, DWORD& actorId, DWORD& result) {
    std::uintptr_t record = 0, owner = 0; DWORD tracked = 0, ownerId = 0;
    if (!locateRecord(layout, component, playable, key, record, tracked)) return false;
    std::vector<PartnerActor> actors;
    if (!CollectPartnerActors(layout, component, playable, actors) || !ownerInfo(layout, component, owner, ownerId)) {
        result = layout.runtime.unavailableResult; return true;
    }
    const PartnerActor* selected = nullptr; bool hasPending = false;
    for (const auto& actor : actors) {
        if (actor.key != key) continue;
        if (pending(actor, owner)) { hasPending = true; continue; }
        if (!selected || actor.actorId == tracked || actor.actorId == preferredReuse) selected = &actor;
        if (actor.actorId == preferredReuse) break;
    }
    if (!selected) {
        if (hasPending) { result = layout.runtime.unavailableResult; return true; }
        return false; // 确实缺少角色时，交给完整原版创建路径。
    }
    result = bindInstance(layout, component, playable, key, selected->actorId) ? 0 : layout.runtime.unavailableResult;
    if (!result) actorId = selected->actorId;
    if (runtimeLog) {
        char message[160]; sprintf_s(message, "PARTNER REUSE: key=%lu actor=0x%08lx result=0x%08lx; no new actor created.", key, actorId, result);
        runtimeLog(message);
    }
    return true;
}

bool PreparePartnerDivorce(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, DWORD interactionActor, PartnerActionInvoker invoke, void* context) {
    std::uintptr_t record=0,owner=0;DWORD tracked=0,ownerId=0;
    std::vector<PartnerActor> actors;
    if(!locateRecord(layout,component,playable,key,record,tracked) ||
        !ownerInfo(layout,component,owner,ownerId) || !CollectPartnerActors(layout,component,playable,actors))return false;
    DWORD keep=0;
    for(const auto& actor:actors)if(actor.key==key && !pending(actor,owner)) {
        if(!keep || actor.actorId==tracked)keep=actor.actorId;
        if(interactionActor && actor.actorId==interactionActor){keep=interactionActor;break;}
    }
    if(interactionActor && keep!=interactionActor)return false;
    for(const auto& actor:actors) {
        if(actor.key!=key || actor.actorId==keep || pending(actor,owner))continue;
        if(!bindInstance(layout,component,playable,key,actor.actorId))return false;
        const DWORD result=invoke(context,key,false);DWORD now=0;std::uintptr_t current=0;
        if(result || !locateRecord(layout,component,playable,key,current,now) || now) {
            bindInstance(layout,component,playable,key,keep);return false;
        }
        rememberRemoval(actor,owner);
    }
    // 正在对话的那一份交给原版离婚代码清理；普通收回入口可能因对话状态而拒绝它。
    // 先恢复其关联，保证原版按键离婚处理的正是该实例，而不是刚收回的副本。
    return bindInstance(layout,component,playable,key,keep);
}

PartnerActionObservation ReconcilePartnerAction(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, bool spawn, PartnerActionInvoker invoke, void* context) {
    PartnerActionObservation out{}; out.result = layout.runtime.unavailableResult;
    std::vector<PartnerActor> actors; std::uintptr_t record = 0, owner = 0; DWORD tracked = 0, ownerId = 0;
    if (!invoke || !locateRecord(layout, component, playable, key, record, tracked) ||
        !ownerInfo(layout, component, owner, ownerId) || !CollectPartnerActors(layout, component, playable, actors)) return out;
    std::vector<PartnerActor> matching;
    for (const auto& actor : actors) if (actor.key == key) matching.push_back(actor);
    out.before = static_cast<unsigned>(matching.size());
    const auto observe=[&]() {
        std::vector<PartnerActor> current;
        if(!CollectPartnerActors(layout,component,playable,current))return false;
        out.after=0;out.pending=false;
        for(const auto& actor:current)if(actor.key==key){++out.after;if(pending(actor,owner))out.pending=true;}
        return true;
    };
    const PartnerActor* keep = nullptr;
    if (spawn) for (const auto& actor : matching)
        if (!pending(actor, owner) && (!keep || actor.actorId == tracked)) keep = &actor;

    for (const auto& actor : matching) {
        if ((keep && actor.actorId == keep->actorId) || pending(actor, owner)) continue;
        if (!bindInstance(layout, component, playable, key, actor.actorId)) {
            if(keep)bindInstance(layout,component,playable,key,keep->actorId);
            observe();logObservation(key,spawn,out);
            return out;
        }
        const DWORD result = invoke(context, key, false);
        DWORD nowTracked = 0; std::uintptr_t nowRecord = 0;
        if (result || !locateRecord(layout, component, playable, key, nowRecord, nowTracked) || nowTracked != 0) {
            out.result = result; // 返回 0 但关联未解除的原版分支也不能记作已收回。
            if (keep) bindInstance(layout, component, playable, key, keep->actorId);
            observe();
            logObservation(key, spawn, out); return out;
        }
        rememberRemoval(actor, owner); ++out.removed;
    }
    if (spawn) {
        // 清空的是实例关联，不是婚姻状态。原版控制器会进入创建分支，创建挂钩
        // 复用 keep，从而照常发送原版召唤回执，修复客户端与服务端关联。
        if (!bindInstance(layout, component, playable, key, 0)) return out;
        struct ReuseScope { DWORD before; explicit ReuseScope(DWORD id):before(preferredReuse){preferredReuse=id;} ~ReuseScope(){preferredReuse=before;} } scope(keep ? keep->actorId : 0);
        out.result = invoke(context, key, true);
        // 原版场景拒绝或空动作不能把仍存在的合法角色留成无关联实例。
        if(keep) {
            std::vector<PartnerActor> current;
            if(CollectPartnerActors(layout,component,playable,current) && std::any_of(current.begin(),current.end(),[&](const auto& actor){
                return actor.key==key && actor.actorId==keep->actorId && actor.address==keep->address;
            })) {
                DWORD boundId=0;std::uintptr_t boundRecord=0;
                if(locateRecord(layout,component,playable,key,boundRecord,boundId) && !boundId)
                    bindInstance(layout,component,playable,key,keep->actorId);
            }
        }
    } else {
        out.result = 0;
        if (matching.empty()) bindInstance(layout, component, playable, key, 0);
    }
    if(!observe())return out;
    DWORD afterId = 0; std::uintptr_t afterRecord = 0;
    const bool bound = locateRecord(layout, component, playable, key, afterRecord, afterId);
    out.verified = bound && !out.result && (spawn ? out.after == 1 && afterId != 0 && !out.pending : out.after == 0 && afterId == 0);
    logObservation(key, spawn, out); return out;
}
