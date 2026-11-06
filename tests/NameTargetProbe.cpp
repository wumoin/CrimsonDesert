#define WIN32_LEAN_AND_MEAN
#include "../src/ConditionTarget.h"
#include "../src/PartnerManager.h"
#include "../src/PartnerSummon.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iterator>

// 仅把磁盘 PE 映射为数据用于定位检查，不执行游戏入口或原生函数。
int wmain(int argc,wchar_t** argv) {
    if(argc!=2){puts("Usage: NameTargetProbe.exe path-to-CrimsonDesert.exe");return 2;}
    FILE* file=nullptr;if(_wfopen_s(&file,argv[1],L"rb") || !file)return 3;
    _fseeki64(file,0,SEEK_END);const auto size=_ftelli64(file);_fseeki64(file,0,SEEK_SET);
    if(size<=0 || size>1024ll*1024*1024){fclose(file);return 4;}
    std::vector<BYTE> bytes(static_cast<std::size_t>(size));
    const auto read=fread(bytes.data(),1,bytes.size(),file);fclose(file);if(read!=bytes.size())return 5;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);
    auto* image=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(nt->OptionalHeader.ImageBase),
        nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image){puts("Could not reserve preferred image base (compile probe with /BASE:0x180000000).");return 6;}
    std::memcpy(image,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        const auto& s=sections[i];
        if(s.SizeOfRawData && static_cast<std::size_t>(s.PointerToRawData)+s.SizeOfRawData<=bytes.size())
            std::memcpy(image+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    const auto module=reinterpret_cast<HMODULE>(image);
    ConditionTarget living{};MarriageCommitTarget marriage{};PartnerNameTarget names{};BeautyUiTarget ui{};PartnerSummonTarget summon{};
    const char* error=nullptr;
    bool ok=FindConditionTarget(module,living,error,ConditionKind::LivingTogether) &&
        FindMarriageCommitTarget(module,marriage,error,living.evaluator) && FindPartnerRuntimeTarget(module,marriage,error) &&
        FindBeautyPartnerLookup(module,marriage) && FindBeautyUiTarget(module,marriage,ui) && FindPartnerNameTarget(module,marriage,names,&error) &&
        FindPartnerSummonTarget(module,marriage,summon,error) && FindPartnerRecallTarget(module,marriage,summon,error);
    printf("Name target: %s (%s)\n",ok?"PASS":"FAIL",error?error:"no layout error");
    if(ok) {
        printf("UI world RVA=%llx offsets=%x/%x/%x player=%x; records=%x/%x/%x/%x keys=%x/%x/%x/%x actorId=%x contents=%x playable=%x\n",
            reinterpret_cast<const BYTE*>(ui.clientWorld)-image,ui.worldManager,ui.managerSession,ui.sessionActor,
            ui.managerPlayer,
            marriage.arrayOffset,marriage.recordStride,marriage.stateOffset,marriage.playableOffset,
            marriage.npcKeyOffsets[0],marriage.npcKeyOffsets[1],marriage.npcKeyOffsets[2],marriage.npcKeyOffsets[3],
            marriage.runtime.actorIdOffset,marriage.runtime.contentsComponentOffset,marriage.runtime.playableIndexOffset);
        printf("assign RVA=%llx world RVA=%llx manager=%x registry=%x buckets=%x/%x/%x status=%x name=%x\n",
            reinterpret_cast<const BYTE*>(names.assign)-image,reinterpret_cast<const BYTE*>(names.clientWorld)-image,
            names.managerOffset,names.registryOffset,names.bucketCountOffset,names.bucketsOffset,names.entriesOffset,names.statusOffset,names.nameOffset);
        printf("Summon send RVA=%lx dispatch RVA=%lx playable-key offset=%x\n",summon.sendRva,summon.dispatchRva,summon.playableKeyOffset);
        printf("Recall leave RVA=%lx party offsets=%x/%x/%x/%x\n",summon.leavePartyRva,summon.partyComponentOffset,summon.partyActiveOffset,summon.partyGroupOffset,summon.partyLeaderOffset);
        auto* leave=reinterpret_cast<BYTE*>(summon.leaveParty);const auto leaveByte=*leave;*leave=0xcc;
        PartnerSummonTarget changedRecall{};
        if(FindPartnerRecallTarget(module,marriage,changedRecall,error)){puts("Changed party leave ABI was incorrectly accepted.");ok=false;}
        *leave=leaveByte;
        if(!FindPartnerRecallTarget(module,marriage,changedRecall,error)){puts("Restored party leave ABI was incorrectly rejected.");ok=false;}
        // 发包字段宽度发生变化时关闭按钮，不在未知协议上发送选中请求。
        auto* sender=reinterpret_cast<BYTE*>(summon.send);constexpr BYTE keyWidth[]={0x41,0xb8,4,0,0,0};
        const auto width=std::search(sender,sender+512,std::begin(keyWidth),std::end(keyWidth));
        if(width==sender+512){puts("Summon key-width fixture unavailable.");ok=false;}
        else {
            width[2]=5;PartnerSummonTarget altered{};
            if(FindPartnerSummonTarget(module,marriage,altered,error)){puts("Changed summon ABI was incorrectly accepted.");ok=false;}
            width[2]=4;
            if(!FindPartnerSummonTarget(module,marriage,altered,error)){puts("Restored summon ABI was incorrectly rejected.");ok=false;}
        }
        // 已核对赋值入口的 ABI 特征改变时，必须关闭名字功能。
        auto* assign=reinterpret_cast<BYTE*>(names.assign);const auto original=*assign;*assign=0xcc;
        PartnerNameTarget changed{};if(FindPartnerNameTarget(module,marriage,changed)){puts("Changed ABI was incorrectly accepted.");ok=false;}
        *assign=original;
        if(!FindPartnerNameTarget(module,marriage,changed)){puts("Restored ABI was incorrectly rejected.");ok=false;}
    }
    VirtualFree(image,0,MEM_RELEASE);return ok?0:1;
}
