// Per-character animation time: never change the world's clock or another NPC.
#include "game.h"
#include "state.h"
#include "interceptor.h"
#include "MinHook.h"
#include <cstring>

namespace {
using DeltaFn = double(__cdecl*)();
DeltaFn originalDelta = nullptr;
thread_local bool fastTurnPhase = false;
double __cdecl TurnDelta() {
  const double dt = originalDelta();
  return fastTurnPhase ? dt * 1.8 : dt;
}
void PhaseEnter(Invocation& inv) {
  inv.text[0] = fastTurnPhase ? 1 : 0;
  const addr actor = g_fastTurnActor.load();
  fastTurnPhase = actor && inv.ecx == actor + 0x3f0;
}
void PhaseLeave(Invocation& inv) { fastTurnPhase = inv.text[0] != 0; }
}
void InstallTurnRate() {
  const MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(VA(0x00772f80)),
      reinterpret_cast<void*>(&TurnDelta), reinterpret_cast<void**>(&originalDelta));
  if (st != MH_OK) { Log("turn rate hook failed: %s", MH_StatusToString(st)); return; }
  Interceptor::Attach(VA(0x006a25a0), PhaseEnter, PhaseLeave, false);
}
