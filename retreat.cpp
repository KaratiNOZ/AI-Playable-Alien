// Multiplayer fire response uses the game's own calibrated retreat gauge.
#include "game.h"
#include "state.h"
#include "mp.h"
#include "puppet.h"

namespace {
addr actor = 0;
bool gaugeLatched = false;
ms_t started = 0, lastTick = 0;

void Tick() {
  if (actor != npc) {
    actor = npc;
    gaugeLatched = false;
    g_forcedRetreat = false;
    g_forceRetreatRequested = false;
  }
  if (!npc || !MpMatchActive()) {
    if (g_forcedRetreat.exchange(false)) ActVentCancel();
    g_forceRetreatRequested = false;
    gaugeLatched = false;
    return;
  }
  // ConditionIsGaugeAmountAbove (6d2c80), RETREAT_GAUGE=0,
  // GAUGE_ACTIVATED=3, as used by alien_behave.bml. This preserves difficulty
  // and damage tuning, rather than counting arbitrary flamethrower hits.
  const addr gauge = rp(npc + 0x31f0);
  const bool high = gauge && rd<int>(gauge + 0x14) >= 3;
  if (!high) gaugeLatched = false;
  if (high && !gaugeLatched && !alienBackstage && !vent.stay) {
    gaugeLatched = true;
    if (MpAlienGameplay()) g_forceRetreatRequested = true;
    else if (const uint64_t owner = PupAlienOwner(npc)) MpRequestRetreat(owner);
  }
  if (g_forceRetreatRequested.exchange(false) && MpAlienGameplay() && !ForcedRetreatActive() &&
      !alienBackstage && !vent.stay && vent.go && Alive(vent.go, va::VT_GATE)) {
    ActVentCrawl(false);
    AutoDuctReset();
    attack.until = attack.startAt = attack.nextUntil = 0;
    SetGate(attack.gate, false);
    ActVentGo();
    started = lastTick = NowMs();
    g_forcedRetreat = true;
    Log("fire retreat: native gauge activated; input cancellation locked");
  }
  if (!ForcedRetreatActive()) return;
  const ms_t now = NowMs();
  if (GameMenuPaused()) started += now - lastTick;
  lastTick = now;
  if (vent.stay || (alienBackstage && !VentBusy())) {
    g_forcedRetreat = false;
    Log("fire retreat: backstage reached; control restored");
  } else if (now - started > 45000) {
    // A missing/unreachable vent must not permanently trap a player's controls.
    g_forcedRetreat = false;
    ActVentCancel();
    vent.goUntil = vent.comeUntil = 0;
    Log("fire retreat: no reachable vent after 45s; control restored");
  } else {
    vent.goUntil = NowMs() + 10000;  // no ordinary command timeout midway to a vent
  }
}
}
void InstallRetreat() { Every(50, "network-fire-retreat", Tick); }
