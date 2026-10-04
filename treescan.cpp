// treescan.cpp — мод внедрили посреди уровня (не из главного меню и без прошлой версии, которая передала бы узлы):
// узлы дерева поведения уже созданы, фабрика их не покажет. Ищем их в памяти.
// Узел-рубильник (ConditionDebugMenuLinkTest, vtable VT_GATE) своей подписи не хранит: только значение (+0x14) и детей
// (+0xC -> [0] ветка «нет», [1] ветка «да»). Поэтому: находим узлы ActionMoveInDirection (VT_MOVE) с Distance (+0x10)
// 123 (Чужой) или 124 (лицехват), рядом — рубильник, чей ребёнок «да» — этот узел, а соседние рубильники узнаём по их
// детям (узлы одной ветки создаются подряд, в порядке BML):
//   Чужой:    Go (LockBestVents) · Attack (MeleeAttack) · Next (SwitchToNextTarget) · Path (MoveToMostRecentSensed) ·
//             [Arrow] · Idle (ActionIdle) · Come (LockBestVents) · Stay (ActionIdle)
//   лицехват: Attack · Next · Path · [Arrow] · Idle
// Старые узлы прошлого уровня могут лежать в памяти с той же vtable — оставляем экземпляры, которые игра проверяет.
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include <algorithm>
#include <cmath>
#include <vector>

void ControlAdoptDone();  // control.cpp

namespace {
struct Inst {
  bool hug;
  addr move;
  std::vector<NodeRec> recs;
  std::vector<addr> probes;
};

addr ChildYes(addr g) {
  addr c = 0;
  Safe([&] {
    const addr kids = rp(g + 0xc);
    if (kids) c = rp(kids + 4);
  });
  return c;
}
addr VtOf(addr p) {
  addr v = 0;
  if (p) SafeRead(p, v);
  return v;
}

// рубильники в окне вокруг узла движения, по адресу
std::vector<addr> GatesAround(addr m) {
  std::vector<addr> out;
  const addr vt = VA(va::VT_GATE);
  const addr from = m > 0x1000 ? m - 0x1000 : 0, to = m + 0x1000;
  for (addr p = from & ~addr(3); p < to; p += 4) {
    addr v = 0;
    if (SafeRead(p, v) && v == vt) out.push_back(p);
  }
  return out;
}

bool Build(addr m, bool hug, Inst& I) {
  const std::vector<addr> g = GatesAround(m);
  int a = -1;
  for (size_t i = 0; i < g.size(); i++)
    if (ChildYes(g[i]) == m) a = int(i);
  if (a < 0) return false;
  I.hug = hug, I.move = m;
  auto at = [&](int i) { return i >= 0 && i < int(g.size()) ? g[size_t(i)] : addr(0); };
  auto is = [&](addr gate, addr vtVa) { return gate && VtOf(ChildYes(gate)) == VA(vtVa); };
  auto add = [&](int kind, addr gate, int init) { I.recs.push_back({ kind, gate, init }); };
  const addr arrow = g[size_t(a)], path = at(a - 1), next = at(a - 2), atk = at(a - 3), idle = at(a + 1);
  if (hug) {
    add(NR_HUG_GATE + 3, arrow, 0);  // HG_ARROW
    if (is(path, va::VT_MOVE_SENSED)) add(NR_HUG_GATE + 2, path, 0);
    if (is(next, va::VT_NEXT_TARGET)) add(NR_HUG_GATE + 1, next, 0);
    if (is(atk, va::VT_MELEE)) add(NR_HUG_GATE + 0, atk, 0), I.probes.push_back(atk);
    if (is(idle, va::VT_IDLE)) add(NR_HUG_GATE + 4, idle, 0);
    I.recs.push_back({ NR_HUG_MOVE, m, 0 });
  } else {
    const addr go = at(a - 4), come = at(a + 2), stay = at(a + 3);
    add(G_ARROW, arrow, 0);
    if (is(path, va::VT_MOVE_SENSED)) add(G_PATH, path, 0);
    if (is(next, va::VT_NEXT_TARGET)) add(G_NEXT, next, 0);
    if (is(atk, va::VT_MELEE)) add(G_ATTACK, atk, 0);
    if (is(go, va::VT_LOCK_VENTS)) add(G_GO, go, 0), I.probes.push_back(go);  // первый в SEL_ARROWS (в мире)
    if (is(idle, va::VT_IDLE)) add(G_IDLE, idle, 1);                           // DefaultBehaviour="True"
    if (is(come, va::VT_LOCK_VENTS)) add(G_COME, come, 0), I.probes.push_back(come);  // первый в SEL_BACKSTAGE
    if (is(stay, va::VT_IDLE)) add(G_STAY, stay, 0);
    I.recs.push_back({ NR_ALIEN_MOVE, m, 0 });
  }
  return true;
}

DWORD WINAPI ScanThread(void*) {
  Interceptor::IgnoreCurrentThread();
  const ms_t t0 = NowMs();
  const addr vtMove = VA(va::VT_MOVE);
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  // адреса — в 64 битах: у игры с большим адресным пространством есть области у самого верха 4 ГБ, и 32-битный
  // счётчик переполнялся в 0 — поиск шёл заново бесконечно (поток ел ядро процессора, Чужой не находился)
  const uint64_t lo = uintptr_t(si.lpMinimumApplicationAddress), hi = uintptr_t(si.lpMaximumApplicationAddress);
  std::vector<std::pair<addr, bool>> moves;  // узел движения, лицехват ли
  uint64_t bytes = 0;
  for (uint64_t a = lo; a < hi;) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(reinterpret_cast<void*>(uintptr_t(a)), &mi, sizeof mi)) break;
    const uint64_t base = uintptr_t(mi.BaseAddress), end = base + mi.RegionSize;
    if (mi.Type == MEM_PRIVATE && Readable(mi) && (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
      for (uint64_t c = base; c < end; c += 0x400000) {
        const uint64_t ce = std::min<uint64_t>(c + 0x400000, end);
        Safe([&] {  // память может освободиться на ходу — тогда просто пропускаем кусок
          for (uint64_t pp = c; pp + 0x14 <= end && pp < ce; pp += 4) {
            const addr p = addr(pp);
            if (rd<uint32_t>(p) != vtMove) continue;
            const float d = rf(p + 0x10);
            if (std::fabs(d - 123.f) < 0.01f) moves.push_back({ p, false });
            else if (std::fabs(d - 124.f) < 0.01f || std::fabs(d - 125.f) < 0.01f) moves.push_back({ p, true });  // лицехват / андроид
          }
        });
        bytes += ce - c;
        Sleep(0);
      }
    }
    if (end <= a) break;
    a = end;
  }
  std::vector<Inst> insts;
  for (const auto& mv : moves) {
    Inst I;
    if (Build(mv.first, mv.second, I)) insts.push_back(std::move(I));
  }
  Log("treescan: %u move nodes, %u trees in %.0f MB, %.1f s", unsigned(moves.size()), unsigned(insts.size()),
      double(bytes) / 1048576.0, double(NowMs() - t0) / 1000.0);
  if (insts.empty()) return 0;
  // какие экземпляры живые: игра проверяет их первые рубильники (2 с)
  std::vector<addr> probes;
  for (const Inst& I : insts) probes.insert(probes.end(), I.probes.begin(), I.probes.end());
  GateProbeSet(probes);
  Sleep(2000);
  std::vector<bool> live(insts.size());
  bool anyAlien = false, anyHug = false;
  for (size_t i = 0; i < insts.size(); i++) {
    for (addr p : insts[i].probes) live[i] = live[i] || GateProbeSeen(p);
    if (live[i]) (insts[i].hug ? anyHug : anyAlien) = true;
  }
  GateProbeSet({});
  JsLock();
  Safe([&] {
    const bool needAlien = !gate && multi[G_ARROW].empty(), needHug = !HugHasGates();
    unsigned na = 0, nh = 0;
    for (size_t i = 0; i < insts.size(); i++) {
      const Inst& I = insts[i];
      // живой, а если живых такого вида нет (Чужой ещё не проверял ветку) — все найденные
      if (!(live[i] || !(I.hug ? anyHug : anyAlien))) continue;
      if (I.hug ? !needHug : !needAlien) continue;
      for (const NodeRec& r : I.recs) {
        if (I.hug) HugAdopt(r);
        else ControlAdopt(r);
      }
      (I.hug ? nh : na)++;
    }
    if (na) ControlAdoptDone();
    if (na || nh) Log("treescan: took %u alien and %u facehugger trees (the mod was injected mid-level)", na, nh);
    else if (!needAlien && !needHug) Log("treescan: trees were already known — nothing to take");
  });
  JsUnlock();
  return 0;
}
}  // namespace

void ScanExistingTrees() {
  HANDLE t = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
  if (t) CloseHandle(t);
}
