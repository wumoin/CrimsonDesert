#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

using MarriageEligibility = bool(__fastcall*)(void*, std::uint16_t, void*);
using LivingPartnerLookup = void*(__fastcall*)(void*, std::uint16_t);
using PartnerSpawnControl = void*(__fastcall*)(void*, void*, bool, std::uint16_t);
using PluginLog = void(*)(const char*);

using NativePartnerCreate = void*(__fastcall*)(void*, DWORD*, std::uint16_t, DWORD*, const DWORD*, bool);
using NativePartnerRestore = void*(__fastcall*)(void*, DWORD*);
using NativeRequestHandler = void*(__fastcall*)(void*, DWORD*, void*);
using NativeDivorceCurrent = void*(__fastcall*)(void*, DWORD*);
using NativeDivorceByKey = void*(__fastcall*)(void*, DWORD*, std::uint16_t, const DWORD*);

struct PartnerRuntimeTarget {
    void** worldSlot = nullptr;
    unsigned managerOffset = 0;
    unsigned bucketCountOffset = 0, bucketsOffset = 0, entriesOffset = 0;
    unsigned actorIdOffset = 0, companionPlayableOffset = 0, companionOwnerOffset = 0;
    NativePartnerCreate create = nullptr;
    void* divorceLookupReturn = nullptr;
    void* divorceCurrent = nullptr;
    DWORD divorceRva = 0;
    unsigned contentsComponentOffset = 0, playableIndexOffset = 0;
    DWORD unavailableResult = MAXDWORD;
    NativeRequestHandler stepRequest = nullptr;
    NativeRequestHandler resetRequest = nullptr, dialogueEndRequest = nullptr;
    NativeDivorceByKey divorceByKey = nullptr;
    NativePartnerRestore restore = nullptr;
    DWORD restoreRva = 0;
};

// 从原版函数恢复关系表与实例布局；不使用固定游戏 RVA。
struct MarriageCommitTarget {
    MarriageEligibility function = nullptr;
    DWORD functionRva = 0;
    unsigned arrayOffset = 0;
    unsigned recordStride = 0;
    unsigned stateOffset = 0;
    unsigned playableOffset = 0;
    LivingPartnerLookup lookup = nullptr;
    DWORD lookupRva = 0;
    void* beautyLookupReturn = nullptr;
    void* commitCallReturn = nullptr;
    DWORD writerRva = 0;
    unsigned npcKeyOffsets[4] = {};
    PartnerSpawnControl spawnControl = nullptr;
    DWORD spawnRva = 0;
    PartnerRuntimeTarget runtime{};
};

struct PartnerActor {
    DWORD key = 0, actorId = 0;
    std::uintptr_t address = 0;
};
// 快照只包含与当前玩家、可操作角色和 NPC 保存键匹配的伴侣实例。
bool CollectPartnerActors(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, std::vector<PartnerActor>& actors);
bool IsPartnerRuntimeReady();

// 回调保持原版场景检查和消息发送；运行时只修复已验证关系的实例关联，不改婚姻状态。
using PartnerActionInvoker = DWORD(*)(void*, DWORD, bool);
struct PartnerActionObservation {
    DWORD result = MAXDWORD;
    unsigned before = 0, after = 0, removed = 0;
    bool pending = false, verified = false;
};
PartnerActionObservation ReconcilePartnerAction(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, bool spawn, PartnerActionInvoker invoke, void* context);
bool ReuseExistingPartnerActor(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, DWORD& actorId, DWORD& result);
bool PreparePartnerDivorce(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, DWORD key, DWORD interactionActor, PartnerActionInvoker invoke, void* context);
void SetPartnerRuntimeLog(PluginLog log);

bool FindMarriageCommitTarget(HMODULE module, MarriageCommitTarget& target, const char*& error,
    void* livingEvaluator = nullptr);
bool FindBeautyPartnerLookup(HMODULE module, MarriageCommitTarget& target);
bool FindPartnerRuntimeTarget(HMODULE module, MarriageCommitTarget& target, const char*& error);
// 独立定位读档恢复入口；不兼容时只关闭恢复扩展，保留现有功能。
bool FindPartnerRestoreTarget(HMODULE module, MarriageCommitTarget& target, const char*& error);
bool IsPartnerRestoreHookReady();

// 先调用原检查；失败时只在私有副本中排除当前角色的现有伴侣后重新检查。
// 原组件、NPC、存档均不在此处写入，真正的状态更新仍由原版提交代码完成。
bool EvaluateMarriageEligibility(const MarriageCommitTarget& layout, MarriageEligibility original,
    void* component, std::uint16_t playable, void* npc, bool enabled);

// 只记录实际写入路径的目标键；后续选择必须在真实缓存找到其已婚记录。
bool RememberMarriageCommitTarget(const MarriageCommitTarget& layout, void* component,
    std::uint16_t playable, void* npc);
void* EvaluateLivingPartnerSelection(const MarriageCommitTarget& layout, LivingPartnerLookup original,
    void* component, std::uint16_t playable, bool enabled);

bool InstallMarriageCommitHook(HMODULE module, PluginLog writeLog, void* livingEvaluator = nullptr);
bool IsPartnerSpawnHookReady();
// 按真实关系核对每位伴侣的实例；一位失败不会跳过其他对象。
void* EvaluatePartnerSpawnAction(const MarriageCommitTarget& layout, PartnerSpawnControl original,
    void* component, void* result, bool spawn, std::uint16_t playable, bool enabled);
PartnerActionObservation EvaluateSelectedPartnerAction(const MarriageCommitTarget& layout,PartnerSpawnControl original,
    void* component,std::uint16_t playable,DWORD key,std::uint16_t character,bool spawn);
// preferredPartnerKey=0 时跟随本进程中本次写入目标；非 0 仅选择已属本角色的已婚记录。
void SetMarriageCommitOverride(bool enabled, DWORD preferredPartnerKey = 0, bool spawnAll = false,
    bool relationshipGuards = false);
void SetPreferredPartnerKey(DWORD key);
