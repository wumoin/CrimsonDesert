#pragma once
#include <windows.h>

// 保持已有枚举的顺序，确保旧配置对应的覆盖位不变。
enum class ConditionKind : unsigned { FriendlyLevel, Marriageable, LivingTogether, AffinityLevel, Tribe, Count };

// 自动定位结果。RVA 仅用于日志，安装挂钩直接使用解析出的真实指针。
struct ConditionTarget {
    void** slot = nullptr;
    void* evaluator = nullptr;
    DWORD typeRva = 0;
    DWORD locatorRva = 0;
    DWORD vtableRva = 0;
    DWORD evaluatorRva = 0;
    unsigned slotIndex = 0;
};

// 只读取模块；不会执行游戏函数、改内存或安装挂钩。
// 类型、结构或分派模式不唯一 / 不兼容时返回 false，并给出静态错误文本。
bool FindConditionTarget(HMODULE module, ConditionTarget& target, const char*& error,
    ConditionKind kind = ConditionKind::LivingTogether);
