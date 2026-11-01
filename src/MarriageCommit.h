#pragma once
#include <windows.h>
#include <cstdint>

using MarriageEligibility = bool(__fastcall*)(void*, std::uint16_t, void*);
using PluginLog = void(*)(const char*);

// 从原版资格函数恢复的只读关系表布局；不使用固定游戏 RVA。
struct MarriageCommitTarget {
    MarriageEligibility function = nullptr;
    DWORD functionRva = 0;
    unsigned arrayOffset = 0;
    unsigned recordStride = 0;
    unsigned stateOffset = 0;
    unsigned playableOffset = 0;
};

bool FindMarriageCommitTarget(HMODULE module, MarriageCommitTarget& target, const char*& error);

// 先调用原检查；失败时只在私有副本中排除当前角色的现有伴侣后重新检查。
// 原组件、NPC、存档均不在此处写入，真正的状态更新仍由原版提交代码完成。
bool EvaluateMarriageEligibility(const MarriageCommitTarget& layout, MarriageEligibility original,
    void* component, std::uint16_t playable, void* npc, bool enabled);

bool InstallMarriageCommitHook(HMODULE module, PluginLog writeLog);
void SetMarriageCommitOverride(bool enabled);
