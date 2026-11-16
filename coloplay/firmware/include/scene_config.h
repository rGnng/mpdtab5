#pragma once
#include "scene_settings.h"
#include <cstdio>

namespace SceneConfig {
using namespace SceneSettings;
constexpr int kGlobal=0,kMaxScenes=8,kScopeCount=kMaxScenes+1;
struct Stored {
  uint32_t version=9;
  // reserved[0] remembers the old Default's replacement for interrupted order migration.
  uint8_t active=1,reserved[3]={};
  Scene scenes[kScopeCount];
  Binding bindings[kBindingCount];
};
static_assert(sizeof(Stored)==3364,"Global scene storage layout changed");
inline bool selectable(const Stored &s,int id){return id>kGlobal&&exists(s,id);}
inline int firstScene(const Stored &s,int except=-1){
  for(int id=1;id<kScopeCount;++id)if(id!=except&&selectable(s,id))return id;
  return -1;
}
inline bool validAction(const Stored &s,const Action &a){
  return SceneSettings::validAction(s,a)&&(a.type!=kSwitchScene||selectable(s,a.target));
}
inline bool valid(const Stored &s){
  if(s.version!=9||!exists(s,kGlobal)||!validName(s.scenes[kGlobal].name)||std::strcmp(s.scenes[kGlobal].name,"Global")||
     !selectable(s,s.active)||s.reserved[0]>=kScopeCount)return false;
  for(int id=0;id<kScopeCount;++id){
    const auto &scene=s.scenes[id];if(scene.used>1)return false;if(!scene.used)continue;
    if(!validName(scene.name)||scene.screen<kClock||scene.screen>=kScreenCount||
       !validLight(scene.ring)||!validLight(scene.keys)||scene.icon>=32||
       scene.ringDisplay>=kRingDisplayCount||scene.ringStyle>=RingFill::kStyleCount)return false;
    for(const auto &level:scene.levels)if(!validLevel(level))return false;
    for(int other=0;other<id;++other)if(s.scenes[other].used&&!std::strcmp(scene.name,s.scenes[other].name))return false;
  }
  for(int i=0;i<kBindingCount;++i){
    const auto &b=s.bindings[i];if(!b.count)continue;
    if(!exists(s,b.scene)||b.global!=(b.scene==kGlobal)||!validGesture(b.input,b.gesture)||b.count>kActionCount||
       b.reserved[0]>1||b.holdMs()<150||b.holdMs()>3000||(b.repeatHold()&&b.gesture!=kHold))return false;
    for(int j=0;j<b.count;++j)if(!validAction(s,b.actions[j]))return false;
    for(int j=0;j<i;++j){const auto &p=s.bindings[j];if(p.count&&p.scene==b.scene&&p.input==b.input&&p.gesture==b.gesture)return false;}
  }
  return true;
}
inline bool assign(Stored &s,Binding value){value.global=value.scene==kGlobal;return SceneSettings::assign(s,value);}
inline bool remove(Stored &s,int id,int fallback=-1){
  if(!selectable(s,id))return false;
  if(fallback==id||!selectable(s,fallback))fallback=firstScene(s,id);
  if(fallback<0)return false; // A physical scene always remains active.
  s.scenes[id]=Scene{};if(s.active==id)s.active=fallback;
  for(auto &b:s.bindings){
    if(b.scene==id)b=Binding{};
    else for(int i=0;i<b.count;++i)if(b.actions[i].type==kSwitchScene&&b.actions[i].target==id)b.actions[i].target=fallback;
  }
  return true;
}
inline Stored defaults(){
  Stored s;s.scenes[kGlobal].used=1;std::strcpy(s.scenes[kGlobal].name,"Global");
  s.scenes[1].used=1;std::strcpy(s.scenes[1].name,"Default");s.reserved[0]=1;
  auto add=[&](int input,int gesture,int type,int value,int led=kInherit){
    Binding b;b.scene=1;b.input=input;b.gesture=gesture;b.count=1;
    auto &a=b.actions[0];a.type=type;a.value=value;a.led=led;
    a.setSound(type==kTone||(type>=kNextScene&&type<=kSwitchScene));
    if(type==kSwitchScene)a.target=1;
    assign(s,b);
  };
  add(kKey1,kSingle,kNextScene,0);add(kKey2,kSingle,kPreviousScene,0);
  add(kKey4,kHold,kSwitchScene,0);add(kOpen,kSingle,kToggleAwake,0);add(kRec,kSingle,kTone,660);
  add(kKnob,kLeft,kVolumeChange,-2,kProportional);add(kKnob,kRight,kVolumeChange,2,kProportional);
  add(kKnob,kPressLeft,kScreenChange,-3);add(kKnob,kPressRight,kScreenChange,3);
  return s;
}
// Preserve old local Default behavior, all real scene IDs, and existing global
// bindings. The extra scope slot permits a lossless upgrade even at old capacity.
inline bool migrate(const SceneSettings::Stored &old,Stored &out){
  if(!SceneSettings::valid(old))return false;
  Stored next;next.active=old.active;
  for(int id=0;id<SceneSettings::kSceneCount;++id)next.scenes[id]=old.scenes[id];
  for(int i=0;i<kBindingCount;++i)next.bindings[i]=old.bindings[i];
  bool needDefault=old.active==0;
  for(const auto &b:old.bindings)if(b.count){
    if(b.scene==0&&!b.global)needDefault=true;
    for(int i=0;i<b.count;++i)if(b.actions[i].type==kSwitchScene&&!b.actions[i].target)needDefault=true;
  }
  int replacement=0;
  if(needDefault){
    for(int id=1;id<kScopeCount;++id)if(!exists(next,id)){replacement=id;break;}
    if(!replacement)return false;
    next.scenes[replacement]=old.scenes[0];next.reserved[0]=replacement;
    if(!next.active)next.active=replacement;
    for(auto &b:next.bindings)if(b.count){
      if(!b.scene&&!b.global)b.scene=replacement;
      for(int i=0;i<b.count;++i)if(b.actions[i].type==kSwitchScene&&!b.actions[i].target)b.actions[i].target=replacement;
    }
  }
  std::strcpy(next.scenes[kGlobal].name,"Global");
  // Global becomes a reserved scope name; preserve any old scene with that name.
  for(int id=1;id<kScopeCount;++id)if(exists(next,id)&&!std::strcmp(next.scenes[id].name,"Global")){
    for(int suffix=1;suffix<=kScopeCount;++suffix){
      char name[25];std::snprintf(name,sizeof(name),"Global scene %d",suffix);bool used=false;
      for(int other=1;other<kScopeCount;++other)if(exists(next,other)&&!std::strcmp(next.scenes[other].name,name))used=true;
      if(!used){std::strcpy(next.scenes[id].name,name);break;}
    }
  }
  if(!valid(next))return false;
  out=next;return true;
}
}
