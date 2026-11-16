#include "scene_service.h"
#include "scene_config.h"
#include "scene_scope_order.h"
#include "scene_record_codec.h"
#include "Preferences.h"
#include <cassert>
#include <iostream>

uint32_t gFakeNow=100;
namespace FakeNvs {
std::map<std::string,std::vector<uint8_t>> saved,pending;
bool open=true,failSettings=false,failOrder=false;
int settingsWrites=0,orderWrites=0;
}
template<class T>void seed(const char *key,const T &s,bool compact=false){
  const auto *p=reinterpret_cast<const uint8_t*>(&s);
  if(!compact){FakeNvs::saved[key].assign(p,p+sizeof(s));return;}
  auto &bytes=FakeNvs::saved[key];bytes.resize(SceneRecordCodec::capacity(sizeof(s)));
  auto n=SceneRecordCodec::encode(p,sizeof(s),bytes.data(),bytes.size());assert(n);bytes.resize(n);
}
static int handled=0,lastValue=0;
static int handle(const SceneSettings::Action &a,int){++handled;lastValue=a.value;return 0;}
static bool request(std::initializer_list<std::pair<std::string,std::string>> fields){
  WebServer server;server.fields=fields;String error;return SceneService::configure(server,error);
}
static SceneConfig::Stored stored(){
  SceneConfig::Stored s;const auto &bytes=FakeNvs::saved.at("settings");
  assert(SceneRecordCodec::decode(bytes.data(),bytes.size(),reinterpret_cast<uint8_t*>(&s),sizeof(s)));
  assert(SceneConfig::valid(s));return s;
}
int main(int argc,char **argv){
  using namespace SceneSettings;
  const std::string scenario=argc>1?argv[1]:"factory";
  auto old=defaults();old.scenes[3].used=1;std::strcpy(old.scenes[3].name,"Work");old.active=3;
  Binding shared;shared.global=1;shared.input=kKey3;shared.count=1;shared.actions[0].type=kTone;shared.actions[0].value=660;
  assert(assign(old,shared));SceneOrder::Stored oldOrder;assert(SceneOrder::move(oldOrder,old,0,1));
  if(scenario=="legacy"||scenario=="compact"||scenario=="save-failure"||scenario=="order-failure"){
    seed("settings",old,scenario!="legacy");seed("order",oldOrder);
  }else if(scenario=="v1"||scenario=="v2"){
    for(auto &b:old.bindings)for(auto &a:b.actions){a.setSound(false);a.setLedBrightness(0);}
    if(scenario=="v1"){
      StoredV1 early;early.active=old.active;
      for(int i=0;i<kSceneCount;++i)early.scenes[i]=old.scenes[i];
      for(int i=0;i<kBindingCount;++i)early.bindings[i]=old.bindings[i];seed("settings",early);
    }else{
      StoredV2 early;early.active=old.active;
      for(int i=0;i<kSceneCount;++i)early.scenes[i]=old.scenes[i];
      for(int i=0;i<kBindingCount;++i)early.bindings[i]=old.bindings[i];seed("settings",early);
    }
  }else if(scenario.size()==2&&scenario[0]=='v'){
    old.version=scenario[1]-'0';if(old.version<=6)for(auto &b:old.bindings)for(auto &a:b.actions)a.setLedBrightness(0);
    if(old.version<=5)for(auto &b:old.bindings)for(auto &a:b.actions)a.setSound(false);
    seed("settings",old);
  }else if(scenario=="corrupt")FakeNvs::saved["settings"]={1,2,3};
  else if(scenario=="checksum"){seed("settings",old,true);FakeNvs::saved["settings"].back()^=1;}
  const auto before=FakeNvs::saved;
  if(scenario=="save-failure")FakeNvs::failSettings=true;
  if(scenario=="order-failure")FakeNvs::failOrder=true;
  SceneService::begin(handle);
  assert(SceneService::activeScene()>0);
  if(scenario=="corrupt"||scenario=="checksum"){
    assert(FakeNvs::saved==before&&FakeNvs::settingsWrites==0);
  }else if(scenario=="save-failure"){
    assert(FakeNvs::saved==before&&SceneService::activeScene()==3);
    FakeNvs::failSettings=false;
    assert(request({{"op","binding"},{"scene_id","0"},{"input","key4"},{"gesture","single"},{"count","1"},{"a0_type","none"}}));
    assert(stored().version==9);SceneService::begin(handle);assert(SceneService::activeScene()==3);
  }else if(scenario=="order-failure"){
    assert(stored().version==9&&FakeNvs::saved.at("order")==before.at("order"));
    FakeNvs::failOrder=false;SceneService::begin(handle);assert(FakeNvs::saved.at("order").size()==sizeof(SceneScopeOrder::Stored));
  }else if(scenario=="legacy"||scenario=="compact"||(scenario.size()==2&&scenario[0]=='v')){
    const auto saved=stored();assert(saved.active==3&&saved.scenes[0].used&&saved.scenes[1].used);
    const int writes=FakeNvs::settingsWrites;SceneService::begin(handle);assert(FakeNvs::settingsWrites==writes);
    if(scenario=="legacy"||scenario=="compact"){
      SceneScopeOrder::Stored order;std::memcpy(&order,FakeNvs::saved["order"].data(),sizeof(order));
      uint8_t ids[9];assert(SceneScopeOrder::list(order,saved,ids)==2&&ids[0]==3&&ids[1]==1);
      SceneService::event(kKey3,kSingle);assert(lastValue==660);
    }
  }else if(scenario=="api"){
    const auto record=FakeNvs::saved;
    for(const char *op:{"activate","update","delete","reorder"}){
      assert(!request({{"op",op},{"scene_id","0"},{"name","Global"},{"position","0"}}));
      assert(FakeNvs::saved==record&&SceneService::activeScene()==1);
    }
    assert(!request({{"op","delete"},{"scene_id","1"}}));
    assert(request({{"op","create"},{"name","Music"}}));
    assert(request({{"op","binding"},{"scene_id","0"},{"input","key3"},{"gesture","single"},{"count","1"},{"a0_type","tone"},{"a0_value","660"}}));
    auto saved=stored();assert(binding(saved,0,kKey3,kSingle)->global);SceneService::event(kKey3,kSingle);assert(lastValue==660);
    assert(request({{"op","binding"},{"scene_id","1"},{"input","key3"},{"gesture","single"},{"count","1"},{"a0_type","tone"},{"a0_value","880"}}));
    SceneService::event(kKey3,kSingle);assert(lastValue==880);
    assert(request({{"op","run"},{"scene_id","0"},{"input","key3"},{"gesture","single"}})&&lastValue==660);
    assert(!request({{"op","binding"},{"scene_id","0"},{"input","key4"},{"gesture","single"},{"count","1"},{"a0_type","switch_scene"},{"a0_target","0"}}));
    assert(!request({{"op","binding"},{"scene_id","1"},{"input","key4"},{"gesture","single"},{"count","1"},{"global","1"},{"a0_type","none"}}));
    assert(request({{"op","binding"},{"scene_id","0"},{"input","key4"},{"gesture","single"},{"count","1"},{"a0_type","switch_scene"},{"a0_target","2"}}));
    SceneService::event(kKey4,kSingle);assert(SceneService::activeScene()==2);
    assert(request({{"op","delete"},{"scene_id","2"}})&&SceneService::activeScene()==1);
    saved=stored();assert(binding(saved,0,kKey4,kSingle)->actions[0].target==1);
    assert(!request({{"op","delete"},{"scene_id","1"}}));
  }else assert(stored().active==1);
  std::cout<<SceneService::configJson().c_str()<<"\n";
}
