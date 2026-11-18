#pragma once
#include <cstdint>

namespace LightingSettings {
enum Mode : uint32_t { kRainbow, kStatic };
constexpr uint32_t kTransitionTag = 0x534D5400;
constexpr uint32_t kTransitionMs = 300, kMaxTransitionMs = 5000;

// One NVS blob commits all lighting settings together. Brightness is percent.
struct Stored {
  uint32_t version = 1;
  uint32_t screen = 70, ring = 70, keys = 70;
  // Reserved words retain the old on-flash offsets. They are never applied.
  uint32_t reservedBeforeMode[3] = {};
  uint32_t mode = kRainbow, color = 0xFFFFFF;
  // Tagged transition flags/duration reuse retired sensor endpoints. Old
  // records without this tag use the new defaults; old firmware ignores them.
  uint32_t reservedTail[2] = {kTransitionTag | 1, kTransitionMs};
};
static_assert(sizeof(Stored) == 44, "lighting settings storage layout changed");

inline bool hasTransitions(const Stored &s) { return (s.reservedTail[0] & ~3u) == kTransitionTag; }
inline bool ringSmooth(const Stored &s) { return !hasTransitions(s) || (s.reservedTail[0] & 1); }
inline bool keysSmooth(const Stored &s) { return hasTransitions(s) && (s.reservedTail[0] & 2); }
inline uint32_t transitionMs(const Stored &s) { return hasTransitions(s) ? s.reservedTail[1] : kTransitionMs; }
inline void setTransitions(Stored &s, bool ring, bool keys, uint32_t ms) {
  s.reservedTail[0] = kTransitionTag | (ring ? 1 : 0) | (keys ? 2 : 0);
  s.reservedTail[1] = ms;
}

inline bool valid(const Stored &s) {
  return s.version == 1 && s.screen <= 100 && s.ring <= 100 && s.keys <= 100 &&
    s.mode <= kStatic && s.color <= 0xFFFFFF &&
    (!hasTransitions(s) || transitionMs(s) <= kMaxTransitionMs);
}

inline uint8_t pwm(int percent) { return (percent * 255 + 50) / 100; }
}  // namespace LightingSettings
