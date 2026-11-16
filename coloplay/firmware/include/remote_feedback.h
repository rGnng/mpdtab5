#pragma once
#include "navigation_feedback.h"
#include "volume_prediction.h"
#include "playback_clock.h"

// No I/O or allocations. The transport serializes access to this state.
class RemoteFeedback {
 public:
  struct Snapshot {
    NavigationFeedback::State navigation;
    bool navigationSupported=false;
    bool starSupported=false,starKnown=false,starred=false;
    bool wired=false,floatVolume=false,enabled=false,ready=false,connected=false,secured=false,extension=false,fresh=false,available=false,pending=false,positionValid=false;
    int playback=255,seek=0,result=0;
    float volume=-1.0f;
    bool volumePrediction=false,predictionReady=false,predicting=false;
    int volumeSteps=0;
    unsigned volumePending=0;
    uint16_t volumeCompleted=0;
    float gridTarget=-1.0f,preview=-1.0f,displayVolume=-1.0f;
    uint16_t request=0,usage=0;
    uint32_t elapsedMs=0,durationMs=0,receivedAt=0,positionMs=0;
    uint64_t track=0;
    char title[64]={},artist[64]={};
  } state;
  VolumePrediction prediction;
  PlaybackClock clock;
  NavigationFeedback navigation;
  void clear(){
    const bool enabled=state.enabled,ready=state.ready;
    state=Snapshot();state.enabled=enabled;state.ready=ready;
    prediction.reset();clock.reset();navigation.reset();memset(text_,0,sizeof(text_));
  }
  bool accept(const uint8_t *p,size_t size,uint32_t session,uint32_t now){
    if(!session||!cp_packet(p,size)||cp_u32(p+4)!=session)return false;
    if(p[1]==CP_NAVIGATION||p[1]==CP_NAV_TEXT)return state.navigationSupported&&navigation.accept(p,size,now);
    if(p[1]==CP_TEXT){
      const unsigned field=p[2]&0x7f,offset=p[3],n=p[8];
      if((field!=1&&field!=2)||offset>=63||n>11||offset+n>63)return false;
      char *target=text_[field-1];if(!offset)memset(target,0,64);
      memcpy(target+offset,p+9,n);target[offset+n]=0;
      if(p[2]&0x80)memcpy(field==1?state.title:state.artist,target,64);
      return true;
    }
    if(p[1]==CP_TRACK){
      state.track=uint64_t(cp_u32(p+8))|(uint64_t(cp_u32(p+12))<<32);clock.track(state.track);
      state.title[0]=state.artist[0]=0;memset(text_,0,sizeof(text_));return true;
    }
    if(!cp_state_valid(p,size))return false;
    state.extension=true;
    if(p[1]==CP_HELLO){
      state.starSupported=(p[17]&CP_FEATURE_STAR)!=0;
      if(!state.starSupported)state.starKnown=state.starred=false;
      state.navigationSupported=(p[16]&CP_CAP_NAVIGATION)!=0;
      state.floatVolume=(p[16]&CP_CAP_VOLUME_FLOAT)!=0;
      state.volumePrediction=state.floatVolume&&(p[16]&CP_CAP_VOLUME_PREDICT)&&(p[16]&CP_CAP_VOLUME_SCALE);
      prediction.configure(state.volumePrediction,p[8]);
    }
    if(p[1]==CP_STATE){
      state.receivedAt=now;state.available=(p[10]&0x80)!=0;
      if(!state.floatVolume)state.volume=(int8_t)p[8]<0?-1.0f:(int8_t)p[8]/100.0f;
      state.playback=p[9];state.seek=p[10]&3;state.elapsedMs=cp_u32(p+12);state.durationMs=cp_u32(p+16);
      state.starKnown=state.starSupported&&(p[10]&CP_STATE_STAR_KNOWN);
      state.starred=state.starKnown&&(p[10]&CP_STATE_STARRED);
      clock.update(state.available,state.playback,state.seek,state.elapsedMs,state.durationMs,now);
      if(!state.floatVolume)result(cp_u16(p+2),p[11]);
    }
    if(p[1]==CP_VOLUME_STATE&&state.floatVolume){
      const uint16_t request=cp_u16(p+2);
      if(!state.volumePrediction||prediction.observe(cp_float(p+8),cp_float(p+16),p[13],cp_u16(p+14),request,p[12],now)){
        state.volume=cp_float(p+8);state.gridTarget=state.volumePrediction?cp_float(p+16):-1.0f;
      }
      result(request,p[12]);
    }
    return true;
  }
  Snapshot snapshot(uint32_t now)const{
    auto out=state;const bool linked=out.connected&&(out.wired||out.secured);
    out.navigation=navigation.snapshot(now,linked);
    out.positionMs=clock.position(now);out.positionValid=clock.valid(now);
    out.volumeSteps=prediction.steps();out.volumePending=prediction.pending();out.volumeCompleted=prediction.completed();
    out.predictionReady=prediction.ready();out.predicting=prediction.active(now);
    out.preview=out.predicting?prediction.predicted():-1.0f;out.displayVolume=prediction.display(now,out.volume);
    out.fresh=linked&&out.receivedAt&&uint32_t(now-out.receivedAt)<PlaybackClock::kFreshMs;
    if(!out.fresh||!out.available||!out.track)out.starKnown=out.starred=false;
    if(!out.fresh){out.available=out.positionValid=false;out.volume=out.preview=out.displayVolume=-1;out.predicting=out.predictionReady=false;out.playback=255;}
    return out;
  }
 private:
  char text_[2][64]={};
  void result(uint16_t request,uint8_t value){if(request&&request==state.request){state.result=value;state.pending=value==CP_PENDING||value==CP_ACCEPTED;}}
};
