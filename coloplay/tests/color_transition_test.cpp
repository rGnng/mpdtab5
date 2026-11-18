#include "color_transition.h"
#include "action_lighting.h"
#include "playback_settings.h"
#include <cassert>
#include <iostream>

int main() {
  ColorTransition ring,button;
  assert(ring.update(0xff0000,100,true,300)==0xff0000);
  assert(ring.update(0x0000ff,100,true,300)==0xff0000);
  assert(ring.update(0x0000ff,250,true,300)==0x800080);
  assert(ring.update(0x0000ff,400,true,300)==0x0000ff);
  assert(button.update(0xff0000,100,false,300)==0xff0000);
  assert(button.update(0x0000ff,101,false,300)==0x0000ff);
  // Changing the target mid-fade starts from the current interpolated color.
  assert(ring.update(0x00ff00,400,true,300)==0x0000ff);
  const auto middle=ring.update(0x00ff00,550,true,300);
  assert(middle==0x008080);
  assert(ring.update(0xffffff,550,true,300)==middle);
  assert(ring.update(0xffffff,850,true,300)==0xffffff);
  assert(ring.update(0,851,true,0)==0);
  assert(ring.update(0xffffff,852,false,300)==0xffffff);
  // Re-enabling, timer rollover, and sparse frame delivery finish exactly.
  assert(ring.update(0,0xffffff80,true,300)==0xffffff);
  assert(ring.update(0,0x16,true,300)==0x808080);
  assert(ring.update(0,0x100,true,300)==0);
  // Smoothing only the palette leaves changing volume coverage immediate.
  ColorTransition palette;
  palette.update(0xff0000,0,true,300);palette.update(0x0000ff,0,true,300);
  const auto color=palette.update(0x0000ff,150,true,300);
  ActionFeedback::Light cue;cue.led=SceneSettings::kProportional;cue.percent=0;
  auto off=ActionLighting::resolve(cue,color,color,color,70,true,0,9,false,-1);
  cue.percent=100;
  auto full=ActionLighting::resolve(cue,color,color,color,70,true,0,9,false,-1);
  assert(off.applied&&off.color==0&&full.applied&&full.color==color);
  assert(PlaybackSettings::ringColor(color,0,0,0)==0);
  assert(PlaybackSettings::ringColor(color,0,1000,0)==color);
  // Off bypasses/reset the palette tween, avoiding flashes from unfilled LEDs.
  assert(palette.update(0,151,false,300)==0);
  assert(palette.update(0x00ff00,152,true,300)==0);
  assert(palette.update(0x00ff00,452,true,300)==0x00ff00);
  // Sleep's final brightness gate immediately overrides any in-flight fade.
  assert(DemoLevels::scaleRgb(color,0)==0);
  std::cout<<"PASS 300 ms color tween, disabled channels, retargeting, rollover and immediate coverage/sleep\n";
}
