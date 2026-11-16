#include "scene_config.h"
#include "scene_scope_order.h"
#include "scene_record_codec.h"
#include "tab5_navigation_profile.h"
#include <cassert>
#include <iostream>
#include <vector>

int main(){
  using namespace SceneSettings;
  auto s=SceneConfig::defaults();assert(SceneConfig::valid(s)&&s.active==1);
  assert(!std::strcmp(s.scenes[0].name,"Global")&&!SceneConfig::selectable(s,0));
  assert(!SceneConfig::remove(s,0)&&!SceneConfig::remove(s,1));
  Binding shared;shared.input=kKey3;shared.count=1;shared.actions[0].type=kTone;shared.actions[0].value=660;
  assert(SceneConfig::assign(s,shared)&&binding(s,0,kKey3,kSingle)->global);
  assert(effectiveBinding(s,1,kKey3,kSingle)->scene==0);
  shared.scene=1;shared.actions[0].value=880;assert(SceneConfig::assign(s,shared));
  assert(!binding(s,1,kKey3,kSingle)->global&&effectiveBinding(s,1,kKey3,kSingle)->actions[0].value==880);
  auto bad=s;bad.active=0;assert(!SceneConfig::valid(bad));
  bad=s;binding(bad,0,kKey3,kSingle)->global=0;assert(!SceneConfig::valid(bad));
  Action target;target.type=kSwitchScene;target.target=0;assert(!SceneConfig::validAction(s,target));
  target.target=1;assert(SceneConfig::validAction(s,target));
  assert(Tab5NavigationProfile::install(s,8)&&SceneConfig::valid(s));
  s.active=8;target.target=8;binding(s,0,kKey3,kSingle)->actions[0]=target;
  assert(SceneConfig::remove(s,8)&&s.active==1&&binding(s,0,kKey3,kSingle)->actions[0].target==1);

  // Preserve the complete old local Default, even when all eight slots are full.
  auto old=defaults();old.scenes[0].ring=Light{kSceneStatic,0x123456};old.scenes[0].levels[0].overrideGeneral=1;old.scenes[0].levels[0].percent=23;
  for(int id=1;id<kSceneCount;++id){old.scenes[id].used=1;std::snprintf(old.scenes[id].name,25,"Scene %d",id);}
  Binding global;global.global=1;global.input=kKey3;global.gesture=kDouble;global.count=1;global.actions[0].type=kNextScene;
  assert(assign(old,global)&&valid(old));
  SceneConfig::Stored upgraded;assert(SceneConfig::migrate(old,upgraded)&&SceneConfig::valid(upgraded));
  assert(upgraded.active==8&&upgraded.reserved[0]==8);
  assert(!std::memcmp(&old.scenes[0],&upgraded.scenes[8],sizeof(Scene)));
  for(int id=1;id<kSceneCount;++id)assert(!std::memcmp(&old.scenes[id],&upgraded.scenes[id],sizeof(Scene)));
  for(int i=0;i<kBindingCount;++i)if(old.bindings[i].count){
    auto expected=old.bindings[i];if(expected.scene==0&&!expected.global)expected.scene=8;
    for(int j=0;j<expected.count;++j)if(expected.actions[j].type==kSwitchScene&&expected.actions[j].target==0)expected.actions[j].target=8;
    assert(!std::memcmp(&expected,&upgraded.bindings[i],sizeof(Binding)));
  }
  auto unchanged=upgraded;old.version=9;assert(!SceneConfig::migrate(old,upgraded)&&!std::memcmp(&unchanged,&upgraded,sizeof(upgraded)));

  // A Default containing only shared bindings disappears from the real scene list.
  old=defaults();for(auto &b:old.bindings)b=Binding{};old.scenes[3].used=1;std::strcpy(old.scenes[3].name,"Music");old.active=3;
  assert(assign(old,global)&&SceneConfig::migrate(old,upgraded));
  assert(upgraded.active==3&&!upgraded.reserved[0]&&!exists(upgraded,1));
  assert(effectiveBinding(upgraded,3,kKey3,kDouble)->scene==0);
  old.scenes[4].used=1;std::strcpy(old.scenes[4].name,"Global");assert(SceneConfig::migrate(old,upgraded));
  assert(!std::strcmp(upgraded.scenes[4].name,"Global scene 1"));

  SceneScopeOrder::Stored order;uint8_t ids[SceneConfig::kScopeCount];
  assert(SceneScopeOrder::valid(order));assert(SceneScopeOrder::list(order,upgraded,ids)==2&&ids[0]==3&&ids[1]==4);
  assert(SceneScopeOrder::neighbor(order,upgraded,3,-1)==4&&SceneScopeOrder::neighbor(order,upgraded,4,1)==3);
  assert(SceneScopeOrder::neighbor(order,upgraded,0,1)==-1&&!SceneScopeOrder::move(order,upgraded,0,0));
  assert(SceneScopeOrder::move(order,upgraded,4,0)&&SceneScopeOrder::list(order,upgraded,ids)==2&&ids[0]==4);
  assert(SceneScopeOrder::valid(order)&&order.ids[0]==0);
  old=defaults();for(int id=1;id<kSceneCount;++id){old.scenes[id].used=1;std::snprintf(old.scenes[id].name,25,"Scene %d",id);}
  assert(SceneConfig::migrate(old,upgraded));
  SceneOrder::Stored legacyOrder;assert(SceneOrder::move(legacyOrder,old,0,4));
  assert(SceneScopeOrder::migrate(legacyOrder,upgraded,order));
  assert(SceneScopeOrder::list(order,upgraded,ids)==8&&ids[4]==8);
  auto repeat=SceneScopeOrder::Stored{};assert(SceneScopeOrder::migrate(legacyOrder,upgraded,repeat)&&!std::memcmp(&order,&repeat,sizeof(order)));
  for(int from=1;from<9;++from)for(int to=0;to<8;++to){
    auto moved=order;assert(SceneScopeOrder::move(moved,upgraded,from,to));
    assert(SceneScopeOrder::valid(moved)&&SceneScopeOrder::list(moved,upgraded,ids)==8&&ids[to]==from);
    for(int i=0;i<8;++i)assert(SceneScopeOrder::neighbor(moved,upgraded,ids[i],1)==ids[(i+1)%8]);
  }
  auto fresh=SceneConfig::defaults();assert(SceneScopeOrder::neighbor(order,fresh,1,1)==1);
  std::vector<uint8_t> packed(SceneRecordCodec::capacity(sizeof(upgraded)));
  const auto bytes=SceneRecordCodec::encode(reinterpret_cast<const uint8_t*>(&upgraded),sizeof(upgraded),packed.data(),packed.size());
  SceneConfig::Stored restored;assert(bytes&&SceneRecordCodec::decode(packed.data(),bytes,reinterpret_cast<uint8_t*>(&restored),sizeof(restored)));
  assert(!std::memcmp(&upgraded,&restored,sizeof(restored))&&SceneConfig::valid(restored));
  std::cout<<"PASS Global scope, eight real scenes, full-capacity lossless migration, shared/local resolution, protected scope, scene targets/order and compact persistence\n";
}
