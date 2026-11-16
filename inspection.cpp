// Authored hiding-place inspection. Native specs own alignment and animation cleanup.
#include "game.h"
#include "state.h"
#include "interceptor.h"
#include "mp.h"
#include <map>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>

std::string LevelCurrentName();

namespace {
struct InspectionPoint { const char* level; uint32_t top, entity, instance; V3 position; int high; };
#include "data/inspection_points.inc"
using Spec = std::array<uint32_t, 22>;
using PlayFn = void(__thiscall*)(addr, const uint32_t*, uint32_t);
struct GameInput {
  addr npc=0, loco=0;
  int camera=0;
  bool available=false, photo=false, paused=false, forced=false, moving=false, rawAny=false, network=false;
};
GameInput input;
std::atomic<unsigned> resetEpoch{0};
unsigned seenEpoch=0;
std::atomic<bool> unloading{false};
std::atomic<bool> allowActions{false};
addr overrideRef[2]{};
addr actor = 0, ownedAction = 0;
uint8_t ownedSerial = 0;
int phase = -1, candidate = -1;
ms_t phaseAt = 0, tickAt = 0, scanAt = 0;
bool cancel = false;
bool armed = true;  // после завершения нужно отпустить F, иначе сразу повтор
std::array<Spec,3> specs{};
// Catalog indices identify level + entity + instance; never share a timeout
// between neighbouring hiding places. Owned exclusively by the game callback.
std::map<int,ms_t> inspectedUntil;
constexpr ms_t kInspectionCooldownMs=120000;
int CooldownSeconds(int point, ms_t now) {
  if (!input.network || point<0) return 0;
  const auto it=inspectedUntil.find(point);
  return it==inspectedUntil.end() || it->second<=now ? 0 : int((it->second-now+999)/1000);
}

void Release(addr ref) {
  volatile LONG* rc = reinterpret_cast<volatile LONG*>(ref + 4);
  for (LONG v = *rc; v > 1; v = *rc)
    if (InterlockedCompareExchange(rc, v-1, v) == v) return;
}
bool ReadSpec(uint32_t entity, uint32_t instance, Spec& out, const char** reason=nullptr) {
  addr ref = 0;
  bool found = false;
  if (reason) *reason="entity not loaded";
  Safe([&] {
    const uint32_t key[2] = { entity, instance };
    if (const addr em = rp(VA(va::EntityManagerPtr))) Fn<FindEntityFn>(va::FindEntity)(em, &ref, key);
    if (!ref) return;
    if (reason) *reason="animation spec unavailable";
    const addr data = Fn<addr(__cdecl*)(const addr*)>(0x008b57e0)(&ref);
    if (!data) return;
    std::memcpy(out.data(), reinterpret_cast<const void*>(data), sizeof out);
    if (reason) *reason=(!out[0] || !out[1]) ? "marker unavailable" : "not an ALIEN animation set";
    found = out[0] && out[1] && out[9] == 0xEFA665ACu;  // actual marker and ALIEN set
  });
  if (ref) Release(ref);
  return found;
}
bool OwnAction() {
  return actor && actor == input.npc && ownedAction && rp(actor + 0x36c) == ownedAction &&
      rd<uint8_t>(ownedAction + 0x1a) == 18 && rd<uint8_t>(ownedAction + 0x19) == ownedSerial;
}
void EndOverride() {
  if (!overrideRef[0]) return;
  // 6799f0: byte +20 would cancel the current primary on removal. We restore
  // only our owned action ourselves; another action must not be cancelled.
  const uint32_t type=rd<uint32_t>(overrideRef[0]+0x10);
  const uint32_t state=rd<uint32_t>(overrideRef[0]+0xc);
  if (type==5 && state!=5 && state!=6) {
    wr<uint8_t>(overrideRef[0]+0x20,0);
    Fn<void(__fastcall*)(addr)>(0x00683db0)(overrideRef[0]);
  }
  Fn<void(__fastcall*)(addr*)>(0x006f58d0)(overrideRef);
}
bool BeginOverride() {
  if (overrideRef[0]) return true;
  Fn<addr*(__thiscall*)(addr,addr*)>(0x00682410)(actor+0x2cd0,overrideRef);
  if (!overrideRef[0]) return false;
  if (rd<uint32_t>(overrideRef[0]+0x10)!=5) { EndOverride(); return false; }
  // Exactly the token setup in native PlayAnimationRequest::Start (6860f2).
  wr<uint32_t>(overrideRef[0]+0x1c,18);
  wr<uint8_t>(overrideRef[0]+0x22,1);
  wr<uint8_t>(overrideRef[0]+0x18,1);
  Fn<void(__fastcall*)(addr)>(0x00683ce0)(overrideRef[0]);
  return true;
}
void Forget() {
  EndOverride();
  phase = -1;
  ownedAction = 0;
  cancel = false;
  armed = false;
  g_inspecting = false;
  g_inspectionHint = 0;
  g_inspectionProgress = 0;
}
void Restore() {
  if (OwnAction()) Fn<PrimToIdleFn>(va::PrimToIdle)(ownedAction);
  Forget();
}
void Play(int next) {
  if (!BeginOverride()) return;
  const addr action = rp(actor + 0x36c);
  Fn<PlayFn>(0x006579f0)(action, specs[next].data(), 0);
  ownedAction = rp(actor + 0x36c);
  ownedSerial = rd<uint8_t>(ownedAction + 0x19);
  phase = next;
  phaseAt = tickAt = NowMs();
  // These are completed stages, not an invented wall-clock countdown.
  g_inspectionProgress = next == 0 ? 0.f : next == 1 ? .25f : .75f;
  Log("inspection: native phase %d started", phase);
}
bool Available() {
  return input.available;
}
// CATHODE acquires its own entity-manager locks. This function must only run
// on the game thread: the scheduler holds JsLock while invoking its tasks,
// and querying entities there deadlocks against game callbacks needing JsLock.
void InspectionGameTick() {
  if (!input.network) inspectedUntil.clear();
  g_inspectionCooldown=0;
  const unsigned epoch=resetEpoch.load();
  if (actor != input.npc || seenEpoch!=epoch) {
    Forget(); actor=input.npc; seenEpoch=epoch; candidate=-1; scanAt=0;
  }
  const bool held = HeldK(K_SEARCH);
  if (!held) armed = true;
  if (g_inspecting) {
    const bool walk = Down('W') || Down('A') || Down('S') || Down('D') ||
        Down(VK_UP) || Down(VK_DOWN) || Down(VK_LEFT) || Down(VK_RIGHT);
    if ((!held || walk || !allowActions) && !input.photo && !input.paused) cancel = true;
    g_inspectionHint = 3;
    return;
  }
  if (!Available() || unloading) { candidate = -1; g_inspectionHint = 0; return; }
  const ms_t now = NowMs();
  if (now - scanAt >= 200) {
    scanAt = now;
    candidate = -1;
    const V3 pos = rv3(actor + 0x70);
    std::string level = LevelCurrentName();
    if (_strnicmp(level.c_str(), "Production\\", 11) == 0) level.erase(0,11);
    float best = 2.6f;
    for (size_t i=0; i<std::size(kInspectionPoints); ++i) {
      const auto& p = kInspectionPoints[i];
      if (_stricmp(level.c_str(), p.level) != 0) continue;
      const float d = std::hypot(pos.x-p.position.x, pos.z-p.position.z);
      if (d >= best || std::fabs(pos.y-p.position.y) > 1.2f) continue;
      Spec value{};
      const char* reason=nullptr;
      if (!ReadSpec(p.entity, p.instance, value, &reason)) {
        static ms_t failAt = 0;
        if (now - failAt >= 2000) {
          failAt = now;
          Log("inspection: spec unreadable %08X:%08X (%s): %s, distance %.2f", p.entity, p.instance, p.level, reason, d);
        }
        continue;
      }
      candidate = int(i); best = d;
    }
    // Revalidation evidence: one line per candidate transition, not per scan.
    static int logged = -1;
    if (candidate != logged) {
      logged = candidate;
      if (candidate < 0) Log("inspection: nothing nearby (level=%s)", level.c_str());
      else {
        const auto& p = kInspectionPoints[candidate];
        Log("inspection: %s nearby entity=%08X:%08X dist=%.2f (level=%s)",
            p.high ? "locker" : "low hiding place", p.entity, p.instance, best, level.c_str());
      }
    }
  }
  g_inspectionHint = candidate < 0 ? 0 : kInspectionPoints[candidate].high ? 1 : 2;
  g_inspectionCooldown=CooldownSeconds(candidate,now);
  if (g_inspectionCooldown>0) { g_inspectionHint=4; return; }
  if (!allowActions || !held || !armed || candidate < 0 || input.moving || input.rawAny || input.paused) return;
  const auto& point = kInspectionPoints[candidate];
  if (!ReadSpec(point.entity, point.instance, specs[0])) return;
  if (point.high && (!ReadSpec(0x52D1908Fu, point.instance, specs[1]) ||
                     !ReadSpec(0x8BC98221u, point.instance, specs[2]))) return;
  cancel = false;
  g_inspectionProgress = 0;
  phase = -2;  // let our movement request settle before rebuilding the primary action
  phaseAt = tickAt = now;
  g_inspecting = true;
  g_inspectionHint = 3;
  Log("inspection: requested %s at %.2f %.2f %.2f", point.high ? "locker" : "low hiding place",
      point.position.x, point.position.y, point.position.z);
}
void AdvanceInspection() {
  if (!g_inspecting) return;
  if (!actor || actor != input.npc) return Forget();
  const ms_t now = NowMs();
  if (input.photo || input.paused) { phaseAt += now-tickAt; tickAt=now; return; }
  tickAt = now;
  if (unloading || input.forced || !Available() || cancel || !allowActions) {
    Log("inspection: cancelled; progress discarded");
    return Restore();
  }
  const addr action = rp(actor+0x36c);
  if (!action) return Forget();
  if (phase == -2) {
    if (now-phaseAt>1500) return Forget();
    const int type=rd<uint8_t>(action+0x1a);
    if (type==1 || (type==59 && (rd<uint8_t>(action+8)&4))) Play(0);
    return;
  }
  if (!OwnAction()) {
    Log("inspection: native action changed; releasing our request");
    return Forget();  // another native action took over; never destroy it
  }
  const bool high = candidate>=0 && kInspectionPoints[candidate].high;
  const bool done = (rd<uint8_t>(action+8)&4)!=0;
  const ms_t elapsed=now-phaseAt;
  if (elapsed>30000) { Log("inspection: native phase timed out; restoring idle"); return Restore(); }
  // Preserve the native spec's PlayCount and finished signal. Never restart
  // a finished loop indefinitely or count a timer expiration as success.
  if (done) {
    if (!high || phase==2) {
      if (input.network && candidate>=0) inspectedUntil[candidate]=now+kInspectionCooldownMs;
      Log("inspection: completed native sequence while F held");
      Restore();
    } else Play(phase+1);
  }
}
void OnGameFrame(Invocation&) {
  static DWORD owner=0;
  const DWORD thread=GetCurrentThreadId();
  if (!owner) { owner=thread; Log("inspection: main-loop owner thread %lu, actions %d",owner,int(allowActions)); }
  if (thread!=owner) return;
  static bool busy=false;
  if (busy) return;
  busy=true;
  // RunCallbacks is a main-loop boundary, outside the locomotion worker jobs.
  // Only copy shared mod input under JsLock. Every native call below runs
  // after releasing it, including entity queries, action creation and cleanup.
  JsLock();
  const bool copied=Safe([] {
    input.npc=npc; input.loco=loco; input.camera=camMode;
    input.photo=photo.on; input.paused=GameMenuPaused();
    input.forced=ForcedRetreatActive(); input.moving=moving; input.rawAny=lastInput.rawAny;
    input.network=MpAlienGameplay();
    input.available=npc && loco && camMode!=0 && !hugger && aiMode==0 && !photo.on &&
        !input.paused && !input.forced && !VentBusy() && !vent.stay && !alienBackstage &&
        !ductOn && !ventCrawl && !autoDuctPath && !turnFollowBody && attack.until<=NowMs();
  });
  JsUnlock();
  if (copied) {
    const bool ok=Safe([] {
      if (unloading) { Restore(); return; }
      InspectionGameTick();
      AdvanceInspection();
    });
    if (!ok) { unloading=true; Log("inspection: native call failed; feature stopped"); }
  }
  busy=false;
}
}
void InspectionReset() { ++resetEpoch; g_inspectionHint=0; g_inspectionProgress=0; }
void InspectionSetActions(bool on) {
  allowActions=on;
  g_inspectionActions=on;
  Log("inspection: actions %d", int(on));
}
void InspectionPrepareUnload() { unloading=true; }
void InspectionRestorePaused() { unloading=true; }
void InstallInspection(bool actions) {
  allowActions=actions;
  g_inspectionActions=actions;
  if (const HMODULE dll=GetModuleHandleA("steam_api.dll"))
    if (const addr callbacks=addr(GetProcAddress(dll,"SteamAPI_RunCallbacks")))
      Interceptor::Attach(callbacks,nullptr,OnGameFrame,false);
}
