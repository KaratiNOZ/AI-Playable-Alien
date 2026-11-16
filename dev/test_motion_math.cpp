#include "motion_math.h"
#include <cassert>
#include <cstdio>

int main() {
  assert(TurnEase(0.f) == 0.f && TurnEase(1.f) == 1.f);
  assert(TurnEase(-1.f) == 0.f && TurnEase(2.f) == 1.f);
  float last = 0.f;
  for (int i = 0; i <= 1000; ++i) {
    const float t = float(i) / 1000.f, y = TurnEase(t);
    assert(y >= last && y >= 0.f && y <= 1.f);
    assert(std::fabs(y + TurnEase(1.f-t) - 1.f) < .00002f);
    last = y;
  }
  assert(std::fabs(TurnEase(.5f) - .5f) < .00001f);
  assert(TurnEase(.25f) < .25f && TurnEase(.75f) > .75f);
  assert(TurnEase(.001f) < .00001f && 1.f-TurnEase(.999f) < .00001f);
  std::puts("Bezier turn easing: endpoints, monotonicity, symmetry and ease-in/out pass");
}
