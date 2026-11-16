#pragma once
#include "coloplay_ble_protocol.h"
#include <cstring>

// Navigation is a separate, coherent stream; it never changes media/volume ACKs.
class NavigationFeedback {
 public:
  struct State {cp_navigation_t ui={};bool valid=false,fresh=false;uint16_t revision=0;uint32_t at=0;};
  void reset(){state_=State{};pending_=cp_navigation_t{};revision_=0;used_=length_=headingUsed_=headingLength_=0;modern_=false;}
  State snapshot(uint32_t now,bool connected)const {auto out=state_;out.fresh=connected&&out.valid&&uint32_t(now-out.at)<5000;return out;}
  bool accept(const uint8_t *p,size_t n,uint32_t now){
    if(!cp_packet(p,n)||!cp_u16(p+2))return false;
    const uint16_t revision=cp_u16(p+2);
    if(p[1]==CP_NAVIGATION||p[1]==CP_NAVIGATION_HEADING){
      const bool modern=p[1]==CP_NAVIGATION_HEADING;
      const unsigned length=modern?p[18]:cp_u16(p+18),heading=modern?p[19]:0;
      const uint32_t index=cp_u32(p+10),count=cp_u32(p+14);
      if(p[8]>6||(p[9]&~63)||length>95||heading>95||(!(p[9]&CP_NAV_FOCUS)&&length)||
         ((p[9]&CP_NAV_FOCUS)&&count&&index>=count))return false;
      if(state_.valid&&revision==state_.revision&&revision==revision_){state_.at=now;return true;}
      if(revision_&&int16_t(revision-revision_)<0)return false;
      pending_=cp_navigation_t{};pending_.view=p[8];pending_.flags=p[9];pending_.index=index;pending_.count=count;
      revision_=revision;length_=length;headingLength_=heading;used_=headingUsed_=0;modern_=modern;
      if(!length_&&!headingLength_)commit(now);
      return true;
    }
    const bool heading=p[1]==CP_NAV_HEADING_TEXT;
    if((p[1]!=CP_NAV_TEXT&&!heading)||(heading&&!modern_)||revision!=revision_)return false;
    unsigned &used=heading?headingUsed_:used_;const unsigned length=heading?headingLength_:length_;
    if(p[8]!=used||!p[9]||p[9]>10||used+p[9]>length)return false;
    std::memcpy((heading?pending_.heading:pending_.focus)+used,p+10,p[9]);used+=p[9];
    if(used_==length_&&headingUsed_==headingLength_)commit(now);
    return true;
  }
  static const char *viewName(const cp_navigation_t &ui){
    if(ui.flags&CP_NAV_CONTEXT)return "Context menu";
    if(ui.flags&CP_NAV_QUEUE)return "Queue";
    if(ui.heading[0])return ui.heading;
    static const char *names[]={"Now Playing","Albums","Artists","Songs","Playlists","Bluetooth","Settings"};
    return ui.view<7?names[ui.view]:"";
  }
 private:
  State state_;cp_navigation_t pending_={};uint16_t revision_=0;
  unsigned used_=0,length_=0,headingUsed_=0,headingLength_=0;bool modern_=false;
  void commit(uint32_t now){state_.ui=pending_;state_.revision=revision_;state_.valid=true;state_.at=now;}
};
