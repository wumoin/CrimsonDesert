#define WIN32_LEAN_AND_MEAN
#include "MarriageCommit.h"
#include "../vendor/minhook/include/MinHook.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <intrin.h>

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

void* marriedRecord(const MarriageCommitTarget& layout, void* component, std::uint16_t playable,
    DWORD wanted, unsigned& total) {
    total=0;
    if (!component || !wanted || layout.arrayOffset>4096 || layout.recordStride<16 || layout.recordStride>127 ||
        layout.stateOffset>=layout.recordStride || layout.playableOffset+2>layout.recordStride) return nullptr;
    try {
        RelationshipArray outer{};
        if (!copyReadable(&outer,static_cast<std::uint8_t*>(component)+layout.arrayOffset,sizeof(outer)) ||
            outer.count>64 || outer.capacity<outer.count) return nullptr;
        std::vector<RelationshipArray> arrays(outer.count);
        if (outer.count && !copyReadable(arrays.data(),reinterpret_cast<void*>(outer.data),arrays.size()*sizeof(RelationshipArray)))
            return nullptr;
        std::size_t totalBytes=0;
        void* selected=nullptr;
        unsigned selectedCount=0;
        for (const auto& array:arrays) {
            if (array.count>4096 || array.capacity<array.count) return nullptr;
            const auto bytes=static_cast<std::size_t>(array.count)*layout.recordStride;
            totalBytes+=bytes;
            if (totalBytes>8*1024*1024) return nullptr;
            if (!bytes) continue;
            std::vector<std::uint8_t> records(bytes);
            if (!copyReadable(records.data(),reinterpret_cast<void*>(array.data),bytes)) return nullptr;
            for (unsigned i=0;i<array.count;++i) {
                const auto record=records.data()+static_cast<std::size_t>(i)*layout.recordStride;
                std::uint16_t owner=0;DWORD key=0;
                std::memcpy(&owner,record+layout.playableOffset,2);
                std::memcpy(&key,record,4);
                if (owner!=playable) continue;
                if(record[layout.stateOffset]!=2)continue;
                ++total;
                if (key==wanted) {
                    ++selectedCount;
                    // 返回真实缓存内的记录地址，绝不返回临时快照的地址。
                    selected=reinterpret_cast<void*>(array.data+static_cast<std::size_t>(i)*layout.recordStride);
                }
            }
        }
        return selectedCount==1 ? selected : nullptr;
    } catch (...) { return nullptr; }
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

void* __fastcall replacementLookup(void* component,std::uint16_t playable) {
    return EvaluateLivingPartnerSelection(installedTarget,originalLookup,component,playable,
        InterlockedCompareExchange(&enabled,0,0)!=0);
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
    if (!active || !native) return native;
    const auto wanted=wantedPartner(playable);
    if (!wanted) return native;
    unsigned total=0;
    void* selected=marriedRecord(layout,component,playable,wanted,total);
    if (!selected) {
        if(logger && InterlockedCompareExchange(&missingReported,1,0)==0) {
            char message[224];
            sprintf_s(message,"PARTNER ROUTE WAIT: playable=%u requestedKey=%lu observedMarried=%u; no unique verified match; keeping native selection.",
                static_cast<unsigned>(playable),wanted,total);
            logger(message);
        }
        return native; // 未实际写入、不是本角色伴侣或快照无效时保留原选择。
    }
    if (logger && InterlockedCompareExchange(&routeReported,1,0)==0) {
        char message[224];
        DWORD nativeKey=0;copyReadable(&nativeKey,native,sizeof(nativeKey));
        sprintf_s(message,"PARTNER ROUTE: playable=%u nativeKey=%lu selectedKey=%lu state=2 marriedCandidates=%u; selected an existing native married record.",
            static_cast<unsigned>(playable),nativeKey,wanted,total);
        logger(message);
    }
    return selected;
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
    sprintf_s(message,"COMMIT RESOLVED: eligibility RVA=0x%08lx arrays=0x%x stride=0x%x state=0x%x playable=0x%x",
        installedTarget.functionRva,installedTarget.arrayOffset,installedTarget.recordStride,
        installedTarget.stateOffset,installedTarget.playableOffset);
    if (logger) logger(message);
    sprintf_s(message,"PARTNER ROUTE RESOLVED: lookup RVA=0x%08lx writer RVA=0x%08lx key offsets=0x%x/0x%x/0x%x/0x%x",
        installedTarget.lookupRva,installedTarget.writerRva,installedTarget.npcKeyOffsets[0],installedTarget.npcKeyOffsets[1],
        installedTarget.npcKeyOffsets[2],installedTarget.npcKeyOffsets[3]);
    if(logger)logger(message);
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        status = MH_CreateHook(reinterpret_cast<void*>(installedTarget.function),
            reinterpret_cast<void*>(&replacementEligibility),reinterpret_cast<void**>(&originalEligibility));
        if (status == MH_OK) {
            status=MH_CreateHook(reinterpret_cast<void*>(installedTarget.lookup),
                reinterpret_cast<void*>(&replacementLookup),reinterpret_cast<void**>(&originalLookup));
            if(status==MH_OK)status=MH_QueueEnableHook(reinterpret_cast<void*>(installedTarget.function));
            if(status==MH_OK)status=MH_QueueEnableHook(reinterpret_cast<void*>(installedTarget.lookup));
            if(status==MH_OK)status=MH_ApplyQueued();
            if(status!=MH_OK) {
                MH_DisableHook(reinterpret_cast<void*>(installedTarget.lookup));
                MH_DisableHook(reinterpret_cast<void*>(installedTarget.function));
                MH_RemoveHook(reinterpret_cast<void*>(installedTarget.lookup));
                MH_RemoveHook(reinterpret_cast<void*>(installedTarget.function));
            }
        }
    }
    if (status != MH_OK) {
        sprintf_s(message,"COMMIT HOOK UNAVAILABLE: MinHook %s.",MH_StatusToString(status));
        if (logger) logger(message);
        return false;
    }
    if (logger) logger("COMMIT HOOK INSTALLED: original eligibility plus private-view recheck; override defaults off.");
    if (logger) logger("PARTNER ROUTE INSTALLED: native write target is preferred only after its married record exists.");
    return true;
}

void SetMarriageCommitOverride(bool active,DWORD preferredPartnerKey) {
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
