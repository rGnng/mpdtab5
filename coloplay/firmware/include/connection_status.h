#pragma once
#include "remote_feedback.h"
namespace ConnectionStatus {
struct State {bool usb=false,bluetooth=false,mpd=false;};
inline State from(const RemoteFeedback::Snapshot &selected,const RemoteFeedback::Snapshot &ble){
  State out;
  // USB means a validated serial session, not the SOF/DTR flag that can flicker.
  out.usb=selected.wired&&selected.connected;
  out.bluetooth=ble.enabled&&ble.ready&&ble.connected&&ble.secured;
  out.mpd=selected.fresh&&selected.available;
  return out;
}
}
