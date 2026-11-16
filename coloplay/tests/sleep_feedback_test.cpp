#include "sleep_feedback.h"
#include <cassert>
#include <iostream>
int main(){
  uint8_t p[20];cp_sleep_t s={CP_SLEEP_ON|CP_SLEEP_COUNTDOWN,30,1000};
  cp_sleep_packet(p,7,&s);assert(cp_sleep_valid(p,20));
  SleepFeedback feedback;assert(!feedback.accept(p,20,1));feedback.configure(true);
  assert(feedback.accept(p,20,0xFFFFFF00u));
  auto state=feedback.snapshot(0x100u,true);assert(state.fresh&&state.awake&&state.countdown&&state.remainingMs==488);
  state=feedback.snapshot(0x300u,true);assert(state.fresh&&state.awake&&!state.remainingMs); // Only host blanks.
  assert(!feedback.snapshot(0x800u,true).fresh&&!feedback.snapshot(0x100u,false).fresh);
  for(unsigned minutes:{0u,30u,60u}){
    s={CP_SLEEP_ON,static_cast<uint16_t>(minutes),minutes*60000u};cp_sleep_packet(p,7,&s);assert(cp_sleep_valid(p,20));
  }
  s={CP_SLEEP_MANUAL|CP_SLEEP_PLAYING,60,0};cp_sleep_packet(p,7,&s);assert(cp_sleep_valid(p,20));
  for(unsigned flags:{16,CP_SLEEP_ON|CP_SLEEP_MANUAL,CP_SLEEP_ON|CP_SLEEP_COUNTDOWN|CP_SLEEP_PLAYING}){
    p[8]=flags;assert(!cp_sleep_valid(p,20));
  }
  cp_sleep_packet(p,7,&s);cp_put16(p+10,20);assert(!cp_sleep_valid(p,20));
  cp_put16(p+10,60);cp_put32(p+12,1);assert(!cp_sleep_valid(p,20));
  s={CP_SLEEP_ON|CP_SLEEP_COUNTDOWN,30,1800001};cp_sleep_packet(p,7,&s);assert(!cp_sleep_valid(p,20));
  for(unsigned usage:{CP_SLEEP_ACTIVITY,CP_SLEEP_OFF,CP_SLEEP_WAKE}){
    cp_header(p,CP_SLEEP,1,7);cp_put16(p+8,usage);cp_put16(p+10,1);assert(cp_command_valid(p,20));
    p[12]=1;assert(!cp_command_valid(p,20));p[12]=0;p[16]=1;assert(!cp_command_valid(p,20));
  }
  cp_header(p,CP_SLEEP,1,7);cp_put16(p+8,4);cp_put16(p+10,1);assert(!cp_command_valid(p,20));
  feedback.configure(false);assert(!feedback.snapshot(1,true).supported);
  SleepFollower follower;SleepFeedback::State remote;remote.fresh=remote.supported=true;remote.awake=false;
  auto r=follower.update(remote,true,false,false,100);assert(r.linked&&!r.awake&&!r.notify&&!r.consume);
  r=follower.update(remote,false,true,true,101);assert(r.awake&&r.notify&&r.consume);
  r=follower.update(remote,true,false,true,200);assert(r.awake&&!r.notify&&r.consume);
  remote.awake=true;r=follower.update(remote,true,false,true,400);assert(r.awake&&r.notify&&r.consume);
  r=follower.update(remote,true,false,false,401);assert(r.consume&&r.reset);
  r=follower.update(remote,true,true,false,700);assert(!r.consume&&r.notify&&r.awake);
  r=follower.update(remote,true,true,false,701);assert(!r.notify); // Fast detents do not flood transport.
  remote.fresh=false;r=follower.update(remote,true,false,false,702);assert(!r.linked&&r.becameLocal&&r.awake);
  // A wake detent is consumed once; failed requests return to the host's real state.
  SleepFollower knob;remote.fresh=true;remote.awake=false;
  r=knob.update(remote,false,true,false,0xFFFFFF00u);assert(r.awake&&r.notify&&r.consume&&r.reset);
  r=knob.update(remote,true,false,false,0x100u);assert(r.awake&&!r.consume);
  r=knob.update(remote,true,false,false,0x600u);assert(!r.awake);
  // Issuing sleep from a held button must suppress periodic activity even
  // before the host's first blank-state reply, and stay asleep through release.
  SleepFollower sleeper;remote.fresh=true;remote.awake=true;
  r=sleeper.update(remote,true,true,true,1000);assert(r.notify);
  sleeper.sleepIssued();r=sleeper.update(remote,true,false,true,1500);assert(!r.notify);
  remote.awake=false;r=sleeper.update(remote,true,false,true,1800);assert(!r.awake&&!r.notify&&!r.consume);
  r=sleeper.update(remote,false,false,true,5000);assert(!r.awake&&!r.notify);
  r=sleeper.update(remote,false,false,false,5010);assert(!r.notify&&!r.awake);
  r=sleeper.update(remote,false,true,true,5100);assert(r.notify&&r.consume&&r.awake);
  std::cout<<"PASS negotiated sleep bounds, countdown/wrap, stale fallback, shared wake, gesture consumption and bounded activity\n";
}
