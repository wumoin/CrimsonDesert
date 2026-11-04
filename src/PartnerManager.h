#pragma once
#include "BeautyPicker.h"

using NativePartnerNameAssign = void(__fastcall*)(void*,const wchar_t*);
struct PartnerNameTarget {
    NativePartnerNameAssign assign=nullptr;
    void** clientWorld=nullptr;
    unsigned managerOffset=0, registryOffset=0;
    unsigned bucketCountOffset=0, bucketsOffset=0, entriesOffset=0;
    unsigned statusOffset=0, nameOffset=0;
};
bool FindPartnerNameTarget(HMODULE module,const MarriageCommitTarget& marriage,PartnerNameTarget& target,const char** error=nullptr);

using SavePreferredPartner = bool(*)(DWORD);
void ConfigurePartnerManager(const wchar_t* iniPath,SavePreferredPartner save,PluginLog log);
void InstallPartnerManager(HMODULE module,const MarriageCommitTarget& marriage);
void SetPartnerManagerEnabled(bool enabled,bool preferenceEnabled);
// 在插件工作线程轮询；窗口和名字更新由游戏窗口线程处理。
void PollPartnerManager();
std::wstring PartnerDisplayName(std::uint16_t playable,const BeautyPartnerOption& partner,unsigned index);
