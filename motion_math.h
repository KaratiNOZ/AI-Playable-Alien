#pragma once
#include <algorithm>
#include <cmath>

// CSS ease-in-out: cubic-bezier(.42, 0, .58, 1). Solve x before evaluating y.
inline float TurnEase(float progress) {
  const float x = std::clamp(progress, 0.f, 1.f);
  float lo = 0.f, hi = 1.f;
  for (int i = 0; i < 18; ++i) {
    const float t = (lo + hi) * .5f, u = 1.f - t;
    const float bx = 3.f*u*u*t*.42f + 3.f*u*t*t*.58f + t*t*t;
    if (bx < x) lo = t; else hi = t;
  }
  const float t = (lo + hi) * .5f;
  return x == 0.f || x == 1.f ? x : t*t*(3.f - 2.f*t);
}
