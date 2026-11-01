#define WIN32_LEAN_AND_MEAN
#include "MarriageCommit.h"
#include "../vendor/minhook/include/MinHook.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

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
PluginLog logger = nullptr;
volatile LONG enabled = 0;
volatile LONG reported = 0;

bool copyReadable(void* destination, const void* source, std::size_t length) {
    SIZE_T copied = 0;
    return source && ReadProcessMemory(GetCurrentProcess(),source,destination,length,&copied) && copied == length;
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
    const bool active = InterlockedCompareExchange(&enabled,0,0) != 0;
    return EvaluateMarriageEligibility(installedTarget,originalEligibility,component,playable,npc,active);
}
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

bool InstallMarriageCommitHook(HMODULE module, PluginLog writeLog) {
    logger = writeLog;
    const char* error = nullptr;
    bool found = false;
    try { found = FindMarriageCommitTarget(module,installedTarget,error); }
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
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        status = MH_CreateHook(reinterpret_cast<void*>(installedTarget.function),
            reinterpret_cast<void*>(&replacementEligibility),reinterpret_cast<void**>(&originalEligibility));
        if (status == MH_OK) {
            status = MH_EnableHook(reinterpret_cast<void*>(installedTarget.function));
            if (status != MH_OK) MH_RemoveHook(reinterpret_cast<void*>(installedTarget.function));
        }
    }
    if (status != MH_OK) {
        sprintf_s(message,"COMMIT HOOK UNAVAILABLE: MinHook %s.",MH_StatusToString(status));
        if (logger) logger(message);
        return false;
    }
    if (logger) logger("COMMIT HOOK INSTALLED: original eligibility plus private-view recheck; override defaults off.");
    return true;
}

void SetMarriageCommitOverride(bool active) {
    InterlockedExchange(&enabled,active ? 1 : 0);
    if (!active) InterlockedExchange(&reported,0);
}
