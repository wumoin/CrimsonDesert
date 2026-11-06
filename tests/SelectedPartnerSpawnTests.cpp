// 核对选中召唤的真实关系/模板校验和作用域选人；原生动作与注册表通过回调模拟。
#include "../src/MarriageCommit.cpp"
#include <array>
#include <cstdlib>

namespace {
void check(bool value,const char* expression,int line){if(!value){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(1);}}
#define CHECK(value) check(static_cast<bool>(value),#value,__LINE__)
std::array<std::array<BYTE,0x38>,3> rows{};
std::array<BYTE,0x2a0> component{};
RelationshipArray inner{},outer{};
unsigned reconciles=0;std::vector<DWORD> actions;std::vector<bool> actionKinds;
bool manualScope=true,manualSpawn=true;DWORD manualKey=200,nativeResult=0;std::uint16_t manualCharacter=60;
PartnerActionObservation lastReport;
unsigned creations=0;bool lastParty=false;
unsigned preparations=0;DWORD preparationResult=0;bool preparationAlready=false;
void* __fastcall fakeCreate(void*,DWORD* result,std::uint16_t,DWORD*,const DWORD*,bool party){++creations;lastParty=party;*result=0;return result;}
void* __fastcall fakeLookup(void*,std::uint16_t playable) {
    for(auto& row:rows){std::uint16_t owner=0;std::memcpy(&owner,row.data()+0xa,2);if(owner==playable && row[0xe]==2)return row.data();}return nullptr;
}
void* __fastcall fakeSpawn(void* contents,void* result,bool spawn,std::uint16_t playable) {
    const auto selected=replacementLookup(contents,playable);CHECK(selected);
    DWORD key=0;std::memcpy(&key,selected,4);actions.push_back(key);actionKinds.push_back(spawn);*static_cast<DWORD*>(result)=nativeResult;return result;
}
}
PartnerActionObservation ReconcilePartnerAction(const MarriageCommitTarget&,void*,std::uint16_t,DWORD key,bool spawn,PartnerActionInvoker invoke,void* context) {
    ++reconciles;PartnerActionObservation result;result.result=invoke(context,key,spawn);result.before=spawn?0:1;result.after=spawn?1:0;result.verified=result.result==0;return result;
}
bool CollectPartnerActors(const MarriageCommitTarget&,void*,std::uint16_t,std::vector<PartnerActor>&){return false;}
bool ReuseExistingPartnerActor(const MarriageCommitTarget&,void*,std::uint16_t,DWORD,DWORD&,DWORD&){return false;}
bool PreparePartnerDivorce(const MarriageCommitTarget&,void*,std::uint16_t,DWORD,DWORD,PartnerActionInvoker,void*){return false;}
void SetPartnerRuntimeLog(PluginLog){}
bool InstallBeautyPicker(HMODULE,const MarriageCommitTarget&,PluginLog){return false;}
bool InstallPartnerSummon(HMODULE,const MarriageCommitTarget&,PluginLog){return false;}
bool HasSelectedPartnerSummon(){return manualScope;}
bool ReadSelectedPartnerSummon(void*,std::uint16_t,DWORD& key,std::uint16_t& character,bool* spawn){key=manualKey;character=manualCharacter;if(spawn)*spawn=manualSpawn;return true;}
void ReportSelectedPartnerSummon(const PartnerActionObservation& result){lastReport=result;}
DWORD PrepareSelectedPartnerRecall(void*,std::uint16_t,DWORD key,bool& already){CHECK(key==manualKey);++preparations;already=preparationAlready;return preparationResult;}
int main() {
    installedTarget.arrayOffset=0x290;installedTarget.recordStride=0x38;installedTarget.stateOffset=0xe;installedTarget.playableOffset=0xa;
    const DWORD keys[]{100,200,300};const std::uint16_t roles[]{0,0,3},characters[]{50,60,70};
    for(unsigned i=0;i<3;++i){std::memcpy(rows[i].data(),keys+i,4);std::memcpy(rows[i].data()+0xa,roles+i,2);std::memcpy(rows[i].data()+0xc,characters+i,2);rows[i][0xe]=2;}
    inner={reinterpret_cast<std::uintptr_t>(rows.data()),3,3};outer={reinterpret_cast<std::uintptr_t>(&inner),1,1};std::memcpy(component.data()+0x290,&outer,sizeof(outer));
    const auto before=rows;originalLookup=fakeLookup;originalSpawnControl=fakeSpawn;configuredPartnerKey=100;enabled=1;spawnAllEnabled=1;
    DWORD result=MAXDWORD;replacementSpawnControl(component.data(),&result,true,0);
    CHECK(!result && reconciles==1 && actions==std::vector<DWORD>({200}) && actionKinds==std::vector<bool>({true}) && lastReport.verified);
    CHECK(configuredPartnerKey==100 && rows==before); // 全员开关/全局偏好不能抢走窗口选中的对象。
    const auto count=reconciles;
    for(const bool spawn:{false,true}) {
        CHECK(EvaluateSelectedPartnerAction(installedTarget,fakeSpawn,component.data(),0,200,999,spawn).result==MAXDWORD);
        CHECK(EvaluateSelectedPartnerAction(installedTarget,fakeSpawn,component.data(),3,200,60,spawn).result==MAXDWORD);
        CHECK(EvaluateSelectedPartnerAction(installedTarget,fakeSpawn,component.data(),0,999,60,spawn).result==MAXDWORD);
    }
    CHECK(reconciles==count && rows==before);
    replacementSpawnControl(component.data(),&result,false,0);CHECK(result==MAXDWORD && reconciles==count);
    originalPartnerCreate=fakeCreate;DWORD actorId=0,key=200;
    replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(!result && creations==1 && lastParty);
    key=100;replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(result==MAXDWORD && creations==1);
    manualSpawn=false;actions.clear();actionKinds.clear();
    replacementSpawnControl(component.data(),&result,false,0);
    CHECK(!result && actions==std::vector<DWORD>({200}) && actionKinds==std::vector<bool>({false}));
    CHECK(preparations==1);
    CHECK(lastReport.verified && lastReport.before==1 && lastReport.after==0 && configuredPartnerKey==100 && rows==before);
    const auto recalled=reconciles;
    replacementSpawnControl(component.data(),&result,true,0);CHECK(result==MAXDWORD && reconciles==recalled);
    key=200;replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(result==MAXDWORD && creations==1);
    nativeResult=0x1234;replacementSpawnControl(component.data(),&result,false,0);CHECK(result==0x1234 && !lastReport.verified);nativeResult=0;
    const auto actionCount=actions.size();preparationResult=0x5678;
    replacementSpawnControl(component.data(),&result,false,0);CHECK(result==0x5678 && actions.size()==actionCount && !lastReport.verified);
    preparationResult=0;preparationAlready=true;
    replacementSpawnControl(component.data(),&result,false,0);CHECK(!result && actions.size()==actionCount);preparationAlready=false;
    manualScope=false;actions.clear();actionKinds.clear();EvaluatePartnerSpawnAction(installedTarget,fakeSpawn,component.data(),&result,true,0,true);
    CHECK(actions==std::vector<DWORD>({100,200}) && actionKinds==std::vector<bool>({true,true}) && configuredPartnerKey==100 && rows==before);
    actions.clear();actionKinds.clear();EvaluatePartnerSpawnAction(installedTarget,fakeSpawn,component.data(),&result,false,0,true);
    CHECK(actions==std::vector<DWORD>({100,200}) && actionKinds==std::vector<bool>({false,false}) && rows==before);
    replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(!result && creations==2 && !lastParty);
    puts("PASS: selected summon/recall ignores all-partner mode and another preference; recall never creates, action mismatch/invalid target rejected, native failure retained, ordinary all-partner actions unchanged.");
    return 0;
}
