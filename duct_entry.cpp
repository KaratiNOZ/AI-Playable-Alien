// Short native navigation assistance at a horizontal duct threshold. No backstage commands.
#include "game.h"
#include "state.h"
#include <cmath>

namespace {
struct EntryAssist {
  addr actor = 0;
  bool exiting = false, closing = false;
  V3 anchor{}, goal{};
  float yaw = 0;
  ms_t since = 0, nextProbe = 0, until = 0, cooldown = 0;
};
EntryAssist s_entry;

void FinishEntry(const char* why) {
  if (autoDuctPath && !s_entry.closing) {
    // Keep direct input suspended until the native node acknowledges cleanup on its
    // own tick. Otherwise its late stop animation can override a fresh WASD request.
    s_entry.closing = true;
    Log("duct-entry: %s; finishing native navigation", why);
  }
  s_entry.since = 0;
  s_entry.cooldown = NowMs() + 600;
}

// A supported low opening ahead: clear at crawl height, blocked at standing height,
// with a reachable floor on the other side. Solid walls, pits and ceiling vents fail.
bool FindLowOpening(const V3& a, float yaw, V3& goal) {
  const float dx = std::sin(yaw), dz = std::cos(yaw);
  V3 hit{};
  const V3 low{a.x, a.y + 0.75f, a.z};
  const V3 high{a.x, a.y + 1.65f, a.z};
  if (RayHit(low, {a.x + dx * 1.4f, low.y, a.z + dz * 1.4f}, hit)) return false;
  if (!RayHit(high, {a.x + dx * 1.4f, high.y, a.z + dz * 1.4f}, hit)) return false;
  const float distance = std::hypot(hit.x - a.x, hit.z - a.z);
  if (distance < 0.05f || distance > 1.35f) return false;
  float floor = 0;
  const float px = a.x + dx * (distance + 0.7f), pz = a.z + dz * (distance + 0.7f);
  if (!RayDown(px, a.y + 0.85f, pz, 1.3f, floor)) return false;
  if (floor < a.y - 0.35f || floor > a.y + 0.55f) return false;
  goal = {a.x + dx * 2.8f, floor, a.z + dz * 2.8f};
  return true;
}

bool FindDuctExit(const V3& a, float yaw, V3& goal) {
  const float dx = std::sin(yaw), dz = std::cos(yaw);
  V3 hit{};
  // Duct camera detection has hysteresis. Confirm a low roof physically when it
  // has not caught up; crouching on an open floor must not count as a duct.
  if (!ductOn && !RayHit({a.x, a.y + 0.55f, a.z}, {a.x, a.y + 1.85f, a.z}, hit)) return false;
  const float distances[] = {1.4f, 2.0f, 2.6f};
  for (float distance : distances) {
    const float x = a.x + dx * distance, z = a.z + dz * distance;
    float floor = 0;
    if (!RayDown(x, a.y + 0.8f, z, 1.5f, floor)) continue;
    if (floor < a.y - 0.65f || floor > a.y + 0.55f) continue;
    // Clear crawl-height route, safe floor, and standing room beyond the lip.
    if (RayHit({a.x, a.y + 0.65f, a.z}, {x, floor + 0.65f, z}, hit)) continue;
    if (RayHit({x, floor + 0.55f, z}, {x, floor + 1.85f, z}, hit)) continue;
    goal = {x, floor, z};
    return true;
  }
  return false;
}
}  // namespace

void AutoDuctReset() {
  autoDuctPath = false;
  s_entry = {};
}

bool AutoDuctTarget(V3& target) {
  if (!autoDuctPath || s_entry.actor != npc) return false;
  target = s_entry.goal;
  return true;
}

bool AutoDuctCancelPath(addr node, addr character, addr state) {
  if (!autoDuctPath || !s_entry.closing || character != npc || s_entry.actor != npc) return false;
  // The record is live here: this is the node's own tick, before its body executes.
  Fn<PathCancelFn>(va::PathCancel)(node, character, state);
  autoDuctPath = false;
  s_entry.closing = false;
  SetGate(pathGate, false);
  Log("duct-entry: native path request released; direct control restored");
  return true;
}

void AutoDuctTick() {
  // Called only on the game thread under JsLock. Rays must never run on the scheduler.
  if (s_entry.actor != npc) AutoDuctReset(), s_entry.actor = npc;
  const ms_t now = NowMs();
  if (s_entry.closing) return;
  const bool allowed = npc && player && camMode && !hugger && aiMode == 0 && !photo.on &&
      !VentBusy() && !vent.stay && !alienBackstage && !alienLooking &&
      !g_uiMenuShown && !g_pausePass && GameFocused() && attack.until <= now &&
      pathGate && Alive(pathGate, va::VT_GATE);
  if (!allowed || !lastInput.rawAny) {
    if (autoDuctPath) FinishEntry("input released/cancelled");
    s_entry.since = 0;
    return;
  }
  const V3 a = rv3(npc + 0x70);
  const float yaw = lastInput.worldYaw;
  if (autoDuctPath) {
    const float alignment = std::cos(WrapPi(yaw - s_entry.yaw));
    const float progress = (a.x - s_entry.anchor.x) * std::sin(s_entry.yaw) +
                           (a.z - s_entry.anchor.z) * std::cos(s_entry.yaw);
    if (alignment < 0.7f) return FinishEntry("direction changed");
    const bool crouched = rd<uint8_t>(npc + 0x2AF) != 0;
    if (progress > 0.55f && (s_entry.exiting ? !crouched : crouched)) return FinishEntry("threshold crossed");
    const float remaining = std::hypot(a.x - s_entry.goal.x, a.z - s_entry.goal.z);
    if (now >= s_entry.until || remaining < 0.35f || (!s_entry.exiting && progress > 1.6f))
      return FinishEntry("assistance finished");
    return;
  }
  if (now < s_entry.cooldown) return;
  if (!s_entry.since || std::cos(WrapPi(yaw - s_entry.yaw)) < 0.85f) {
    s_entry.anchor = a;
    s_entry.yaw = yaw;
    s_entry.since = now;
    return;
  }
  const float progress = (a.x - s_entry.anchor.x) * std::sin(yaw) +
                         (a.z - s_entry.anchor.z) * std::cos(yaw);
  if (progress > 0.08f) {
    s_entry.anchor = a;
    s_entry.since = now;
    return;
  }
  if (now - s_entry.since < 300 || now < s_entry.nextProbe) return;
  s_entry.nextProbe = now + 120;
  V3 goal{};
  const bool inside = ductOn || rd<uint8_t>(npc + 0x2AF) != 0;
  const bool exiting = inside && FindDuctExit(a, yaw, goal);
  if (!exiting && (ductOn || !FindLowOpening(a, yaw, goal))) return;
  s_entry.anchor = a;
  s_entry.goal = goal;
  s_entry.yaw = yaw;
  s_entry.exiting = exiting;
  s_entry.until = now + 2200;
  autoDuctPath = true;
  SetGate(gate, false);
  SetGate(pathGate, true);
  Log("duct-entry: %s ahead; assisting threshold", exiting ? "horizontal exit" : "low opening");
}
