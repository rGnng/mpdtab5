#pragma once
#include "coloplay_ble_protocol.h"

class SleepFeedback {
 public:
  struct State {bool supported=false,fresh=false,awake=true,countdown=false,playing=false,manual=false;uint16_t minutes=0;uint32_t remainingMs=0;};
  void reset(){state_=State{};have_=false;received_=0;}
  void configure(bool enabled){if(!enabled)reset();state_.supported=enabled;}
  bool accept(const uint8_t *p,size_t size,uint32_t now){
    if(!state_.supported||!cp_sleep_valid(p,size))return false;
    state_.awake=p[8]&CP_SLEEP_ON;state_.countdown=p[8]&CP_SLEEP_COUNTDOWN;
    state_.playing=p[8]&CP_SLEEP_PLAYING;state_.manual=p[8]&CP_SLEEP_MANUAL;
    state_.minutes=cp_u16(p+10);state_.remainingMs=cp_u32(p+12);received_=now;have_=true;return true;
  }
  State snapshot(uint32_t now,bool linked)const{
    auto out=state_;out.fresh=linked&&have_&&uint32_t(now-received_)<2000;
    if(out.fresh&&out.countdown){const uint32_t elapsed=now-received_;out.remainingMs=elapsed>=out.remainingMs?0:out.remainingMs-elapsed;}
    return out;
  }
 private:
  State state_;uint32_t received_=0;bool have_=false;
};

/* A wake gesture is consumed through its release, including delayed hold/click
 * events. A short local preview bridges the host's next state update. */
class SleepFollower {
 public:
  void sleepIssued(){suppressHeld_=true;pending_=false;}
  struct Result {bool linked=false,awake=true,notify=false,consume=false,reset=false,becameLocal=false;};
  Result update(const SleepFeedback::State &remote,bool awake,bool activity,bool held,uint32_t now){
    Result out;out.linked=remote.fresh;out.becameLocal=linked_&&!out.linked;linked_=out.linked;
    if(!out.linked)pending_=false;
    if(pending_&&(remote.awake||uint32_t(now-wakeAt_)>=1500))pending_=false;
    out.awake=out.linked?(pending_||remote.awake):awake;
    if(!held)suppressHeld_=false;
    if(out.linked&&(activity||(held&&out.awake&&!suppressHeld_))&&(!notified_||!out.awake||uint32_t(now-notifyAt_)>=250)){out.notify=true;notifyAt_=now;notified_=true;}
    if(out.linked&&activity&&!out.awake){pending_=true;wakeAt_=now;out.awake=true;block_=true;}
    if(block_){out.consume=true;if(!held){block_=false;out.reset=true;}}
    return out;
  }
 private:
  bool linked_=false,pending_=false,block_=false,notified_=false,suppressHeld_=false;uint32_t wakeAt_=0,notifyAt_=0;
};
