#pragma once
#include "BeautyPicker.h"

using NativePartnerSummonSend=void(__fastcall*)(void*,unsigned,const BYTE*,const DWORD*);
using NativePlayableInfo=const void*(__fastcall*)(void*,std::uint16_t);
using NativePlayableInfoByPointer=const void*(__fastcall*)(const std::uint16_t*);
using NativePartnerSummonDispatch=void(__fastcall*)(void*,void*,const BYTE*,const DWORD*);
using NativePartnerPartyLeave=void*(__fastcall*)(void*,DWORD*,bool,bool);
struct PartnerSummonTarget {
    NativePartnerSummonSend send=nullptr;
    NativePlayableInfo playableInfo=nullptr;
    NativePlayableInfoByPointer playableInfoByPointer=nullptr;
    NativePartnerSummonDispatch dispatch=nullptr;
    unsigned playableKeyOffset=0;
    DWORD sendRva=0,dispatchRva=0;
    NativePartnerPartyLeave leaveParty=nullptr;
    void** partyVtable=nullptr;
    unsigned partyComponentOffset=0,partyActiveOffset=0,partyGroupOffset=0,partyLeaderOffset=0;
    DWORD leavePartyRva=0;
};
bool FindPartnerSummonTarget(HMODULE module,const MarriageCommitTarget& marriage,PartnerSummonTarget& target,const char*& error);
bool FindPartnerRecallTarget(HMODULE module,const MarriageCommitTarget& marriage,PartnerSummonTarget& target,const char*& error);
bool InstallPartnerSummon(HMODULE module,const MarriageCommitTarget& marriage,PluginLog log);
void SetPartnerSummonEnabled(bool enabled);
bool IsPartnerSummonReady();
bool IsPartnerRecallReady();
bool IsPartnerSummonBusy();
// 在管理窗口关闭后从原窗口线程发送原版请求；服务端处理时才调用选定对象的原版动作。
bool RequestPartnerSummon(const PartnerUiContext& context,const BeautyPartnerOption& partner,bool spawn=true);
bool HasSelectedPartnerSummon();
bool ReadSelectedPartnerSummon(void* component,std::uint16_t playable,DWORD& key,std::uint16_t& character,bool* spawn=nullptr);
void ReportSelectedPartnerSummon(const PartnerActionObservation& observation);
DWORD PrepareSelectedPartnerRecall(void* component,std::uint16_t playable,DWORD key,bool& alreadyRecalled);
