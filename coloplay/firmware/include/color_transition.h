#pragma once
#include <cstdint>

// A small RGB tween, driven by the existing LED render cadence. Retarget from
// the current interpolated color, including during rainbow/scene changes.
class ColorTransition {
 public:
  uint32_t update(uint32_t target, uint32_t now, bool enabled, uint32_t duration) {
    target &= 0xffffff;
    if (!ready_ || !enabled || !duration) {
      ready_ = true; from_ = target_ = target; since_ = now; duration_ = duration;
      return target;
    }
    const uint32_t current = value(now);
    if (target != target_ || duration != duration_) {
      from_ = current; target_ = target; since_ = now; duration_ = duration;
    }
    return current;
  }
 private:
  uint32_t from_ = 0, target_ = 0, since_ = 0, duration_ = 0;
  bool ready_ = false;
  uint32_t value(uint32_t now) const {
    const uint32_t elapsed = now - since_;
    if (!duration_ || elapsed >= duration_) return target_;
    uint32_t rgb = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
      const uint32_t a = (from_ >> shift) & 255, b = (target_ >> shift) & 255;
      rgb |= ((a * (duration_ - elapsed) + b * elapsed + duration_ / 2) / duration_) << shift;
    }
    return rgb;
  }
};
