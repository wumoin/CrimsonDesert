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
unsigned reconciles=0;std::vector<DWORD> actions;
bool manualScope=true;DWORD manualKey=200;std::uint16_t manualCharacter=60;
PartnerActionObservation lastReport;
unsigned creations=0;bool lastParty=false;
void* __fastcall fakeCreate(void*,DWORD* result,std::uint16_t,DWORD*,const DWORD*,bool party){++creations;lastParty=party;*result=0;return result;}
void* __fastcall fakeLookup(void*,std::uint16_t playable) {
    for(auto& row:rows){std::uint16_t owner=0;std::memcpy(&owner,row.data()+0xa,2);if(owner==playable && row[0xe]==2)return row.data();}return nullptr;
}
void* __fastcall fakeSpawn(void* contents,void* result,bool spawn,std::uint16_t playable) {
    CHECK(spawn);const auto selected=replacementLookup(contents,playable);CHECK(selected);
    DWORD key=0;std::memcpy(&key,selected,4);actions.push_back(key);*static_cast<DWORD*>(result)=0;return result;
}
}
PartnerActionObservation ReconcilePartnerAction(const MarriageCommitTarget&,void*,std::uint16_t,DWORD key,bool spawn,PartnerActionInvoker invoke,void* context) {
    ++reconciles;PartnerActionObservation result;result.result=invoke(context,key,spawn);result.after=1;result.verified=result.result==0;return result;
}
bool CollectPartnerActors(const MarriageCommitTarget&,void*,std::uint16_t,std::vector<PartnerActor>&){return false;}
bool ReuseExistingPartnerActor(const MarriageCommitTarget&,void*,std::uint16_t,DWORD,DWORD&,DWORD&){return false;}
bool PreparePartnerDivorce(const MarriageCommitTarget&,void*,std::uint16_t,DWORD,DWORD,PartnerActionInvoker,void*){return false;}
void SetPartnerRuntimeLog(PluginLog){}
bool InstallBeautyPicker(HMODULE,const MarriageCommitTarget&,PluginLog){return false;}
bool InstallPartnerSummon(HMODULE,const MarriageCommitTarget&,PluginLog){return false;}
bool HasSelectedPartnerSummon(){return manualScope;}
bool ReadSelectedPartnerSummon(void*,std::uint16_t,DWORD& key,std::uint16_t& character){key=manualKey;character=manualCharacter;return true;}
void ReportSelectedPartnerSummon(const PartnerActionObservation& result){lastReport=result;}
int main() {
    installedTarget.arrayOffset=0x290;installedTarget.recordStride=0x38;installedTarget.stateOffset=0xe;installedTarget.playableOffset=0xa;
    const DWORD keys[]{100,200,300};const std::uint16_t roles[]{0,0,3},characters[]{50,60,70};
    for(unsigned i=0;i<3;++i){std::memcpy(rows[i].data(),keys+i,4);std::memcpy(rows[i].data()+0xa,roles+i,2);std::memcpy(rows[i].data()+0xc,characters+i,2);rows[i][0xe]=2;}
    inner={reinterpret_cast<std::uintptr_t>(rows.data()),3,3};outer={reinterpret_cast<std::uintptr_t>(&inner),1,1};std::memcpy(component.data()+0x290,&outer,sizeof(outer));
    const auto before=rows;originalLookup=fakeLookup;originalSpawnControl=fakeSpawn;configuredPartnerKey=100;enabled=1;spawnAllEnabled=1;
    DWORD result=MAXDWORD;replacementSpawnControl(component.data(),&result,true,0);
    CHECK(!result && reconciles==1 && actions==std::vector<DWORD>({200}) && lastReport.verified);
    CHECK(configuredPartnerKey==100 && rows==before); // 全员开关/全局偏好不能抢走窗口选中的对象。
    const auto count=reconciles;
    CHECK(EvaluateSelectedPartnerSpawn(installedTarget,fakeSpawn,component.data(),0,200,999).result==MAXDWORD);
    CHECK(EvaluateSelectedPartnerSpawn(installedTarget,fakeSpawn,component.data(),3,200,60).result==MAXDWORD);
    CHECK(EvaluateSelectedPartnerSpawn(installedTarget,fakeSpawn,component.data(),0,999,60).result==MAXDWORD);
    CHECK(reconciles==count && rows==before);
    replacementSpawnControl(component.data(),&result,false,0);CHECK(result==MAXDWORD && reconciles==count);
    originalPartnerCreate=fakeCreate;DWORD actorId=0,key=200;
    replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(!result && creations==1 && lastParty);
    key=100;replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(result==MAXDWORD && creations==1);
    manualScope=false;actions.clear();EvaluatePartnerSpawnAction(installedTarget,fakeSpawn,component.data(),&result,true,0,true);
    CHECK(actions==std::vector<DWORD>({100,200}) && configuredPartnerKey==100 && rows==before);
    replacementPartnerCreate(component.data(),&result,0,&actorId,&key,false);CHECK(!result && creations==2 && !lastParty);
    puts("PASS: selected summon uses only the chosen married record despite all-partner mode and another preference; invalid target/template/role rejected; ordinary all-partner summon unchanged.");
    return 0;
}
