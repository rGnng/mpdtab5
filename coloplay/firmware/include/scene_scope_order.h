#pragma once
#include "scene_config.h"
#include "scene_order.h"
namespace SceneScopeOrder {
struct Stored {uint32_t version=2;uint8_t ids[SceneConfig::kScopeCount]={0,1,2,3,4,5,6,7,8};uint8_t reserved[3]={};};
static_assert(sizeof(Stored)==16,"Global scope order layout changed");
inline bool valid(const Stored &order){
  if(order.version!=2||order.ids[0]!=SceneConfig::kGlobal)return false;
  unsigned seen=0;for(auto id:order.ids){if(id>=SceneConfig::kScopeCount||(seen&(1u<<id)))return false;seen|=1u<<id;}
  return true;
}
inline int list(const Stored &order,const SceneConfig::Stored &settings,uint8_t *out){
  int n=0;for(auto id:order.ids)if(SceneConfig::selectable(settings,id))out[n++]=id;return n;
}
inline int position(const Stored &order,const SceneConfig::Stored &settings,int id){
  uint8_t ids[SceneConfig::kScopeCount];const int n=list(order,settings,ids);
  for(int i=0;i<n;++i)if(ids[i]==id)return i;
  return -1;
}
inline int neighbor(const Stored &order,const SceneConfig::Stored &settings,int id,int direction){
  uint8_t ids[SceneConfig::kScopeCount];const int n=list(order,settings,ids);
  for(int i=0;i<n;++i)if(ids[i]==id)return ids[(i+(direction>0?1:direction<0?-1:0)+n)%n];
  return -1;
}
inline bool append(Stored &order,int id){
  if(id<=0||id>=SceneConfig::kScopeCount)return false;
  for(int i=1;i<SceneConfig::kScopeCount;++i)if(order.ids[i]==id){
    for(int j=i+1;j<SceneConfig::kScopeCount;++j)order.ids[j-1]=order.ids[j];
    order.ids[SceneConfig::kScopeCount-1]=id;return true;
  }return false;
}
inline bool move(Stored &order,const SceneConfig::Stored &settings,int id,int destination){
  uint8_t ids[SceneConfig::kScopeCount];const int n=list(order,settings,ids),source=position(order,settings,id);
  if(source<0||destination<0||destination>=n)return false;
  if(source<destination)for(int i=source;i<destination;++i)ids[i]=ids[i+1];
  else for(int i=source;i>destination;--i)ids[i]=ids[i-1];
  ids[destination]=id;
  Stored next;for(int i=0;i<n;++i)next.ids[i+1]=ids[i];
  int at=n+1;for(auto unused:order.ids)if(unused&&!SceneConfig::selectable(settings,unused))next.ids[at++]=unused;
  order=next;return true;
}
inline bool migrate(const SceneOrder::Stored &old,const SceneConfig::Stored &settings,Stored &out){
  if(!SceneOrder::valid(old))return false;
  Stored next;unsigned seen=1;int at=1;
  for(auto original:old.ids){
    const int id=original?original:settings.reserved[0];
    if(id>0&&id<SceneConfig::kScopeCount&&!(seen&(1u<<id))){next.ids[at++]=id;seen|=1u<<id;}
  }
  for(int id=1;id<SceneConfig::kScopeCount;++id)if(!(seen&(1u<<id)))next.ids[at++]=id;
  if(!valid(next))return false;
  out=next;return true;
}
}
