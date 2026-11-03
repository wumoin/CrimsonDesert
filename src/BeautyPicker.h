#pragma once
#include "MarriageCommit.h"

using NativeBeautyEntry = void(__fastcall*)(void*,void*,void*,void*);
struct BeautyUiTarget {
    NativeBeautyEntry entry=nullptr;
    void** clientWorld=nullptr;
    void** characters=nullptr;
    unsigned worldManager=0, managerSession=0, sessionActor=0;
    unsigned modeOffset=0, nameOffset=0;
};
struct BeautyPartnerOption { DWORD key=0; std::uint16_t character=0xffff; };
bool FindBeautyUiTarget(HMODULE module,const MarriageCommitTarget& marriage,BeautyUiTarget& target);
bool ReadBeautyPartners(void* component,std::uint16_t playable,std::vector<BeautyPartnerOption>& options);
bool VerifyBeautyPartner(void* component,std::uint16_t playable,DWORD key);
void RunBeautyForPartner(NativeBeautyEntry original,void* self,void* a2,void* a3,void* payload,
    void* component,std::uint16_t playable,DWORD key);
bool InstallBeautyPicker(HMODULE module,const MarriageCommitTarget& marriage,PluginLog log);
void SetBeautyPickerEnabled(bool enabled);
