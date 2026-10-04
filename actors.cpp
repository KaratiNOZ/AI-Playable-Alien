// actors.cpp — атака (ЛКМ/T), Аманда следует за Чужим (Havok-тело), режимы ИИ (7/9, 0 — сброс),
// телепорт через физику, меню персонажей (F1), строка статуса для оверлея.
// Источник: hot.js §4, §6, §7, ai6 + ai10 (+ исправления, см. README), panic, tp, cname, em5, §11.
#include "game.h"
#include "interceptor.h"
#include "puppet.h"
#include "state.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

// ---------------- атака: ЛКМ = сменить цель (0.25 с) -> атака; если цель Аманда — ещё раз (до 3); T = сменить цель ----------------
// ---- андроиды: тип +0x64 -> +4 = 2 / 7; жив — состояние смерти +0x2f90 == 0 (1 здоровье на нуле, 2 умирает, 3 мёртв) ----
bool IsAndroid(addr e) {
  uint32_t type = 0;
  return e && e != npc && e != player && Safe([&] { type = rd<uint32_t>(rp(e + 0x64) + 4); }) && (type == 2 || type == 7);
}
uint32_t DeathState(addr e) {
  uint32_t s = 0;
  SafeRead(e + 0x2f90, s);
  return s;
}
static addr AndroidInReach() {  // ближайший живой андроид в 4 м перед Чужим (±100°)
  V3 a{};
  float yaw = 0;
  if (!npc || !Safe([&] { a = rv3(npc + 0x70), yaw = rf(npc + 0x174); })) return 0;
  addr best = 0;
  float bd = 4.f;
  for (addr e : ActiveChars()) {
    if (!IsAndroid(e) || DeathState(e) != 0) continue;
    V3 p{};
    if (!Safe([&] { p = rv3(e + 0x70); })) continue;
    const float d = std::hypot(p.x - a.x, p.z - a.z);
    if (d >= bd || std::fabs(p.y - a.y) > 2.5f) continue;
    if (d > 1.f && std::fabs(WrapPi(std::atan2(p.x - a.x, p.z - a.z) - yaw)) > 1.75f) continue;
    best = e, bd = d;
  }
  return best;
}
static addr s_andT = 0;
static ms_t s_andUntil = 0, s_andCheckAt = 0, s_finishAt = 0;
// «как человека» (опыт): на время удара андроид — класс CIVILIAN (3), чтобы Чужой взял анимацию убийства человека,
// а андроид — свою половину; не умер за 3 с — добиваем прямым ударом. Класс — +0x64 -> +4, возвращаем через 4 с.
static addr s_classPatched = 0;
static uint32_t s_classWas = 0;
static ms_t s_classUntil = 0;
static void PatchClass(addr e, uint32_t cls) {
  Safe([&] {
    const addr info = rp(e + 0x64);
    s_classWas = rd<uint32_t>(info + 4);
    wr<uint32_t>(info + 4, cls);
  });
  s_classPatched = e;
}
static void RestoreClass();
void TempHumanClass(addr e, ms_t ms) {
  if (s_classPatched && s_classPatched != e) RestoreClass();
  if (s_classPatched != e) PatchClass(e, 3);
  s_classUntil = NowMs() + ms;
}
static void RestoreClass() {
  if (!s_classPatched) return;
  Safe([&] { wr<uint32_t>(rp(s_classPatched + 0x64) + 4, s_classWas); });
  Log("android %08X: class back to %u", unsigned(s_classPatched), s_classWas);
  s_classPatched = 0;
}
static void OnMeleeStartEnter(Invocation& inv) {  // диагностика: старт атаки Чужого
  if (!s_classPatched || inv.arg[0] != npc) return;
  addr lg = 0, t = 0;
  uint32_t id = 0, tid = 0, type = 0;
  Safe([&] {
    lg = rp(rp(npc + 0x34a8) + 0x10), t = rp(lg + 0x42c), id = rd<uint32_t>(lg + 0x430), type = rd<uint32_t>(inv.ecx + 8);
    if (t) tid = rd<uint32_t>(t + 0x50);
  });
  Log("melee start: target %08X id %u/%u attack type %u", unsigned(t), id, tid, type);
}
static void OnMeleeStartLeave(Invocation& inv) {
  if (!s_classPatched || inv.arg[0] != npc) return;
  uint32_t st = 0;
  SafeRead(inv.arg[1] + 4, st);
  Log("melee start -> state %u (8 = refused)", st);
}
static void OnVictimActLeave(Invocation& inv) {  // диагностика: половина жертвы
  if (!s_classPatched) return;
  addr victim = 0, attacker = 0;
  uint8_t flags = 0;
  Safe([&] { victim = rp(inv.ecx + 4), attacker = rp(inv.ecx + 0xd0), flags = rd<uint8_t>(inv.ecx + 8); });
  Log("victim act: victim %08X attacker %08X flags %02X%s", unsigned(victim), unsigned(attacker), flags,
      (flags & 0x40) ? " (refused)" : "");
}
// Анимации пары «Чужой — андроид» в игре нет: узел атаки с андроидом-целью ничего не делает. Поэтому удар — сами:
// Чужой рычит атакующим голосом, через 0.35 с андроид получает смертельный урон от Чужого (FUN_007d0c10 — так игра
// сама добивает жертву) — в потоке игры, из апдейта локомоции.
static std::atomic<addr> s_killReq{ 0 }, s_killBy{ 0 };
static std::atomic<uint32_t> s_killAt{ 0 };
static void OnKillGameTick(Invocation&) {
  const addr v = s_killReq.load();
  if (!v || uint32_t(NowMs()) - s_killAt.load() > 0x80000000u) return;  // ещё рано
  if (s_killReq.exchange(0) != v) return;
  const addr by = s_killBy.load() ? s_killBy.load() : npc;
  if (!by || DeathState(v) != 0) return;  // уже умирает — не трогаем
  uint8_t canDie = 0;
  Safe([&] { canDie = rd<uint8_t>(rp(v + 0x64) + 0x1f); });
  if (!canDie) return Log("%s %08X can't die here (the level made it invincible)", CharName(v).c_str(), unsigned(v));
  const bool ok = Safe([&] { Fn<CharKillFn>(va::CharKill)(v, by); });
  Log("%s %08X: struck by %s (%s)", CharName(v).c_str(), unsigned(v), CharName(by).c_str(), ok ? "ok" : "the game refused");
}
void QueueKill(addr victim, addr attacker, ms_t delayMs) {
  s_killBy = attacker;
  s_killAt = uint32_t(NowMs() + delayMs);
  s_killReq = victim;
}

void ActAttack() {
  ms_t t = NowMs();
  Attack& A = attack;
  A.keyAt = t;
  g_localAttackSeq.fetch_add(1);  // мультиплеер: клиент за Чужого — хост повторит удар (mp.cpp)
  if (alienKillsAndroids && npc) {  // андроид рядом — бьём его (цель подставит хук теста рубильника атаки)
    const addr an = AndroidInReach();
    if (an && androidKillMode == 1) {  // как человека: обычная атака Чужого, андроид на время — «гражданский»
      s_andT = an;
      PatchClass(an, 3);
      s_classUntil = t + 4000;
      s_andUntil = t + 1500;
      s_andCheckAt = t + 3200;
      s_finishAt = t + 3000;
      A.nextUntil = 0;
      A.startAt = t;
      A.until = t + 1200;
      A.retries = 0;
      forceTargetGate = A.gate;
      forceTarget = an;
      Log("attack android %08X (%s) as a human (class %u -> 3)", unsigned(an), CharName(an).c_str(), s_classWas);
      return;
    }
    if (an) {
      s_andT = an;
      s_andUntil = t + 400;  // цель — андроид (HUD, «следующая цель» не перебьёт), рубильник атаки не открываем
      s_andCheckAt = t + 2500;
      A.nextUntil = A.startAt = A.until = 0;
      A.retries = 0;
      forceTarget = 0;
      ActAlienVoice(1);  // атакующий рык
      QueueKill(an, npc, 350);
      V3 a{}, p{};
      Safe([&] { a = rv3(npc + 0x70), p = rv3(an + 0x70); });
      Log("attack android %08X (%s) %.1f m", unsigned(an), CharName(an).c_str(), std::hypot(p.x - a.x, p.z - a.z));
      return;
    }
  }
  A.nextUntil = A.startAt = t + 250;
  A.until = t + 900;
  A.retries = 0;
  Log("attack");
}
// мультиплеер (puppet.cpp): Чужой-кукла бьёт выбранную цель — Аманду хоста, когда другой игрок нажал удар рядом с ней.
// Как удар по андроиду: цель подставляет хук теста рубильника атаки (forceTarget), обычная проверка «цель — Аманда» молчит
static ms_t s_remoteUntil = 0;
void ActAttackTarget(addr target) {
  ms_t t = NowMs();
  Attack& A = attack;
  A.nextUntil = 0;
  A.startAt = t;
  A.until = t + 1200;
  A.retries = 0;
  forceTargetGate = A.gate;
  forceTarget = target;
  s_remoteUntil = t + 1500;
  Log("attack %s %08X (another player's Alien)", CharName(target).c_str(), unsigned(target));
}
void ActNextTarget() {
  ms_t t = NowMs();
  attack.keyAt = t;
  attack.nextUntil = t + 300;
  Log("next target");
}
static void AttackTick() {
  ms_t t = NowMs();
  Attack& A = attack;
  if (photo.on && PhotoTakeAttack(PA_ALIEN) && t - A.keyAt > 900) ActAttack();  // фоторежим: Enter
  if (camMode != 0 && !photo.on && !hugger && t - A.keyAt > 900) {  // за лицехвата мышь — его (hugger.cpp)
    if (Down(VK_LBUTTON)) ActAttack();
    else if (Down(VK_RBUTTON)) ActNextTarget();  // ПКМ — сменить цель
  }
  if (s_andUntil) {  // атака на андроида: цель держим, пока открыт рубильник, потом смотрим, что стало с андроидом
    if (t < s_andUntil) Safe([&] { SetTargetOf(npc, s_andT); });
    else s_andUntil = 0, forceTarget = 0;
  }
  if (s_remoteUntil) {  // удар другого игрока: цель держим, пока открыт рубильник
    if (t < s_remoteUntil) Safe([&] { if (const addr ft = forceTarget.load()) SetTargetOf(npc, ft); });
    else s_remoteUntil = 0, forceTarget = 0;
  }
  if (s_finishAt && t >= s_finishAt) {  // «как человека» не убило — добиваем прямым ударом
    s_finishAt = 0;
    if (s_andT && DeathState(s_andT) == 0) {
      Log("android %08X survived the human kill — finishing it directly", unsigned(s_andT));
      ActAlienVoice(1);
      QueueKill(s_andT, npc, 0);
    }
  }
  if (s_classPatched && (t >= s_classUntil || DeathState(s_classPatched) >= 2)) RestoreClass();
  if (s_andCheckAt && t >= s_andCheckAt) {
    addr T = 0;
    Safe([&] { T = TargetOf(npc); });
    Log("android %08X after the attack: death state %u, alien target %08X", unsigned(s_andT), DeathState(s_andT), unsigned(T));
    s_andCheckAt = 0, s_andT = 0;
  }
  if (!forceTarget && A.startAt && t >= A.startAt && t < A.until && npc && player) {
    addr T = 0;
    Safe([&] { T = TargetOf(npc); });
    if (T == player) {
      if (++A.retries <= 3) {
        A.nextUntil = A.startAt = t + 250;
        A.until = t + 900;
      } else {
        A.until = A.startAt = 0;
        Log("no NPC target");
      }
    }
  }
  SetGate(A.gate, A.until > t && t >= A.startAt);
  SetGate(A.nextGate, A.nextUntil > t);
}

// ---------------- Аманда следует за Чужим: пишем её Havok rigid body (без отскока) ----------------
// RB ловим в 0xb85150 (вызов из 0x8030xx..0x8034xx): ecx = объект персонажа, *(ecx+0x10) = RB, возврат -> позиция
static void OnCharPhysLeave(Invocation& inv) {
  if (!player) return;
  addr ra = ToVA(inv.ra);
  if (ra < va::CharPhysFrom || ra > va::CharPhysTo) return;
  addr p = inv.retval();
  if (std::fabs(rf(p) - rf(player + 0x70)) >= 0.3f || std::fabs(rf(p + 8) - rf(player + 0x78)) >= 0.3f) return;
  addr co = inv.ecx, rb = rp(co + 0x10);
  if (amandaRB == rb) return;
  charObj = co;
  amandaRB = rb;
  V3 pos = rv3(rb + 0x120), c1 = rv3(rb + 0x140);
  comOff[0] = c1.x - pos.x, comOff[1] = c1.y - pos.y, comOff[2] = c1.z - pos.z;
  Log("amanda RB %08X", unsigned(rb));
}
static void OnVentShutdownPause(Invocation&) { amandaPauseUntil = NowMs() + 6000; }  // смена сцены -> пауза следования
static ms_t s_followKeyAt = 0;
void ActSetFollow(bool on) {
  if (amandaFollow == on) return;
  amandaFollow = on;
  Log("amanda follow %d", int(on));
}
static void AmandaTick() {
  ms_t t = NowMs();
  // вентиляция и шахта: Аманду за Чужим не водим — в шахте она была в 1 м позади, а касание Чужого для игры — её смерть
  // (тестер: вылез из шахты — game over)
  if (VentBusy() || vent.stay || ductOn) amandaPauseUntil = std::max(amandaPauseUntil, t + 3000);
  if (AlienKeyAdvK(K_FOLLOW) && t - s_followKeyAt > 500) {  // F — вкл/выкл (нужно для зон, звука, триггеров уровня)
    s_followKeyAt = t;
    ActSetFollow(!amandaFollow);
  }
  if (!amandaFollow || !amandaRB || !npc || !player || camMode == 0 || hugger || t < amandaPauseUntil) return;
  if (amandaHidden || amandaStay) return;  // спрятана под/над картой — её держит alien_extra.cpp; пригнулась — стоит
  // Аманда позади Чужого: касание = смерть (логика игры); в шахте — 1 м, при атаке — 7 м
  float dist = ductOn ? 1.0f : (attack.until > t ? 7.0f : 3.5f);
  float yaw = rf(npc + 0x174);
  V3 a = rv3(npc + 0x70);
  float tx = a.x - std::sin(yaw) * dist, tz = a.z - std::cos(yaw) * dist;
  addr RB = amandaRB;
  wv3(RB + 0x120, tx, a.y, tz);
  wv3(RB + 0x130, tx + comOff[0], a.y + comOff[1], tz + comOff[2]);  // swept COM
  wv3(RB + 0x140, tx + comOff[0], a.y + comOff[1], tz + comOff[2]);
  wv3(RB + 0x1b0, 0, 0, 0);  // скорость
  wv3(player + 0x70, tx, a.y, tz);
}

// ---------------- режимы ИИ: 7 = охотится, но не на Аманду; 9 = полный ИИ; повторное нажатие — ручное; 0 — аварийный сброс ----------------
static std::vector<addr> GatesNear() {  // все рубильники дерева рядом с нашим (±16 КБ)
  std::vector<addr> out;
  if (!gate) return out;
  addr to = gate + 0x4000, vt = VA(va::VT_GATE);
  for (addr a = gate - 0x4000; a < to;) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof m)) break;
    addr end = std::min<addr>(addr(m.BaseAddress) + m.RegionSize, to);
    if (Readable(m))
      Safe([&] {
        for (addr p = a; p + 4 <= end; p += 4)
          if (rp(p) == vt) out.push_back(p);
      });
    a = end;
  }
  return out;
}
static void AiApply() {
  ms_t t = NowMs();
  if (aiMode != 0) {
    pathOn = false;
    vent.goUntil = vent.comeUntil = 0;
    vent.stay = false;
    attack.until = attack.startAt = 0;
    for (addr g : GatesNear())
      if (!(g == attack.nextGate && attack.nextUntil > t)) SetGate(g, false);  // импульс «следующая цель» не глушим
    aiZeroed = true;
  } else if (aiZeroed) {  // возвращаем ИСХОДНЫЕ значения (idle = 1 и т.д.)
    for (addr g : GatesNear()) {
      auto it = aiInit.find(g);
      SetGate(g, it != aiInit.end() && it->second);
    }
    aiZeroed = false;
    Log("AI off: gates restored");
  }
}
static ms_t s_aiKeyAt = 0, s_aiPulseAt = 0, s_aiLogAt = 0;
static void AiHunt() {
  if (!npc) return;
  addr hiding = npc + 0x423c + 8;  // флаг логики PLAYER_HIDING: Аманда — не добыча
  if (aiMode == 3) {
    wr<uint8_t>(hiding, 1);
    aiHid = true;
    ms_t t = NowMs();
    if (t - s_aiPulseAt > 400 && attack.nextGate && player) {
      addr T = 0;
      Safe([&] { T = TargetOf(npc); });
      if (T && T == player) {
        s_aiPulseAt = t;
        attack.nextUntil = t + 200;  // импульс держит цикл атаки (раньше его затирало через 20 мс)
        if (t - s_aiLogAt > 3000) s_aiLogAt = t, Log("retarget from Amanda");
      }
    }
  } else if (aiHid) {  // в hot.js флаг после выхода из режима 7 оставался
    wr<uint8_t>(hiding, 0);
    aiHid = false;
  }
}
void ActSetAi(int mode) {
  if (mode != 0 && mode != 2 && mode != 3) return;
  aiMode = mode;
  Log("AI mode %d (0 manual, 3 hunt others, 2 full)", aiMode);
}
static void AiTick() {
  ms_t t = NowMs();
  if (t - s_aiKeyAt > 400) {
    if (HotAdvK(K_AI)) {  // F4 — ИИ по кругу
      s_aiKeyAt = t;
      ActCycleAi();
    }
  }
  AiApply();
  Safe([] { AiHunt(); });
}
// «ИИ для всех»: один режим Чужому и лицехватам; F4 — по кругу ручное -> не на Аманду -> полный
void ActSetAiAll(int mode) {
  if (mode != 0 && mode != 2 && mode != 3) return;
  ActSetAi(mode);
  hugAi = mode;
  androidAi = mode;
  Log("AI mode for everyone: %d", mode);
}
void ActCycleAi() { ActSetAiAll(aiMode == 0 ? 3 : aiMode == 3 ? 2 : 0); }
void ActPanic() {
  aiMode = 0;
  AiApply();
  Safe([] { AiHunt(); });
  pathOn = false;
  vent.goUntil = vent.comeUntil = 0;
  vent.stay = false;
  Log("panic reset");
}
static void PanicTick() {
  if (HotAdvK(K_PANIC)) ActPanic();
  static ms_t unstuckAt = 0;
  if (AlienKeyK(K_UNSTUCK) && NowMs() - unstuckAt > 1000) unstuckAt = NowMs(), ActUnstuck();
}

// ---------------- телепорт через Havok-тело: контроллер (0x01707a48 + i*0xB0) +0xC -> +0x10 = RB ----------------
static addr RbOf(addr e) {
  addr act = VA(va::ActiveChars);
  for (int i = 0; i < 18; i++) {
    addr s = act + i * 0xB0;
    if (rp(s) != e) continue;
    addr p = rp(s + 0xc);
    return p ? rp(p + 0x10) : 0;
  }
  return 0;
}
bool TpMove(addr e, float x, float y, float z) {
  addr rb = 0;
  Safe([&] { rb = RbOf(e); });
  bool ok = Safe([&] {
    if (rb) {
      V3 p = rv3(rb + 0x120), c = rv3(rb + 0x140);
      float cx = c.x - p.x, cy = c.y - p.y, cz = c.z - p.z;
      wv3(rb + 0x120, x, y, z);
      wv3(rb + 0x130, x + cx, y + cy, z + cz);
      wv3(rb + 0x140, x + cx, y + cy, z + cz);
      wv3(rb + 0x1b0, 0, 0, 0);
    }
    wv3(e + 0x70, x, y, z);
  });
  if (!ok) Log("err tp");
  return rb != 0;
}

std::vector<addr> ActiveChars() {
  std::vector<addr> out;
  addr act = VA(va::ActiveChars);
  for (int i = 0; i < 18; i++) {
    addr e = 0;
    if (SafeRead(act + i * 0xB0, e) && e) out.push_back(e);
  }
  return out;
}
std::string CharName(addr e) {  // строка в объекте персонажа (+0x10, до 64 символов)
  std::string s;
  Safe([&] {
    for (int i = 0; i < 64; i++) {
      uint8_t c = rd<uint8_t>(e + 0x10 + i);
      if (!c) break;
      s += (c >= 32 && c < 127) ? char(c) : '?';
    }
  });
  return s.empty() ? "?" : s;
}

static bool IsActive(addr e) {
  std::vector<addr> A = ActiveChars();
  return std::find(A.begin(), A.end(), e) != A.end();
}
void ActTpAlienTo(addr e) {
  if (!npc || !e || !IsActive(e)) return;  // из ImGui адрес мог устареть за время до выполнения команды
  V3 p{};
  if (!Safe([&] { p = rv3(e + 0x70); })) return;
  TpMove(npc, p.x, p.y, p.z - 1.5f);
  Log("TP alien -> %s", CharName(e).c_str());
}
void ActTpBring(addr e) {
  if (!npc || !e || e == npc || !IsActive(e)) return;
  V3 a{};
  if (!Safe([&] { a = rv3(npc + 0x70); })) return;
  TpMove(e, a.x + 1.2f, a.y, a.z + 1.2f);
  Log("TP bring %s", CharName(e).c_str());
}

// «застрял»: за кулисами / в пути — вылезти у Аманды; в мире — встать в 3.5 м позади Аманды (ближе — касание её убивает)
void ActUnstuck() {
  if (!npc || !player) return;
  pathOn = false;
  if (vent.stay || alienBackstage) {
    Log("unstuck: come out near Amanda");
    return ActVentExitNearAmanda();
  }
  vent.goUntil = vent.comeUntil = 0;
  V3 p{};
  float yaw = 0;
  if (!Safe([&] { p = rv3(player + 0x70), yaw = rf(player + 0x174); })) return;
  TpMove(npc, p.x - std::sin(yaw) * 3.5f, p.y, p.z - std::cos(yaw) * 3.5f);
  Log("unstuck: behind Amanda");
}

// ---------------- меню F1: активные персонажи; ↑/↓ выбор, Enter — Чужой к нему, Backspace — притащить к Чужому ----------------
static size_t s_menuSel = 0;
static ms_t s_menuAt = 0, s_menuNavAt = 0;
static void MenuTick() {
  ms_t t = NowMs();
  if (HotAdvK(K_CHARS) && t - s_menuAt > 400) {
    s_menuAt = t;
    menuOpen = !menuOpen;
    Log("menu %d", int(menuOpen));
    if (!menuOpen) OverlaySet(2, L"");
  }
  if (!menuOpen) return;
  std::vector<addr> A = ActiveChars();
  if (A.empty()) return;
  if (s_menuSel >= A.size()) s_menuSel = 0;
  if (t - s_menuNavAt > 180) {
    addr e = A[s_menuSel];
    if (Down(VK_UP)) {
      s_menuNavAt = t;
      s_menuSel = (s_menuSel + A.size() - 1) % A.size();
    } else if (Down(VK_DOWN)) {
      s_menuNavAt = t;
      s_menuSel = (s_menuSel + 1) % A.size();
    } else if (Down(VK_RETURN) && npc) {
      s_menuNavAt = t;
      ActTpAlienTo(e);
    } else if (Down(VK_BACK) && npc) {
      s_menuNavAt = t;
      ActTpBring(e);
    }
  }
  addr ref = npc ? npc : player;
  wchar_t line[200];
  swprintf(line, 200, TRW(L"=== ПЕРСОНАЖИ (%u/18) — ↑/↓ выбор, Enter — Чужой к нему, Backspace — притащить к Чужому, F1 — закрыть ===",
                          L"=== CHARACTERS (%u/18) — ↑/↓ select, Enter — Alien to them, Backspace — bring to the Alien, F1 — close ==="),
           unsigned(A.size()));
  std::wstring s = line;
  for (size_t i = 0; i < A.size(); i++) {
    addr e = A[i];
    float d = 0;
    if (ref) Safe([&] { d = std::hypot(rf(e + 0x70) - rf(ref + 0x70), rf(e + 0x78) - rf(ref + 0x78)); });
    swprintf(line, 200, L"\n%ls%ls%ls%ls  %.1f %ls", i == s_menuSel ? L" > " : L"   ", Widen(CharName(e)).c_str(),
             e == player ? TRW(L" (ты)", L" (you)") : L"", e == npc ? TRW(L" (управляемый)", L" (controlled)") : L"", d, TRW(L"м", L"m"));
    s += line;
  }
  OverlaySet(2, s);
}

// ---------------- статус для оверлея (hot.js §11) ----------------
static void StatusTick() {
  if (!gate) {
    OverlaySet(0, L"AlienMod: хуки стоят. Загружай уровень с Чужим (инжект — из главного меню).");
    return;
  }
  ms_t now = NowMs();
  int cm = camMode;
  const wchar_t* cam = cm == 2 ? L"за спиной (V)" : cm == 1 ? L"в голове (V)" : L"Аманда (B)";
  const wchar_t* vs = vent.goUntil > now ? L"иду за кулисы (E)" : vent.comeUntil > now ? L"выхожу (E)" : vent.stay ? L"за кулисами (E / Q — выйти)" : L"—";
  const wchar_t* ai = aiMode == 3 ? L"ВКЛ, охотится не на Аманду" : aiMode == 2 ? L"ПОЛНЫЙ (охотится)" : L"выкл — ручное";
  bool sh = Down(VK_SHIFT), ct = Down(VK_CONTROL), spc = Down(VK_SPACE);
  const wchar_t* sp = spc ? L"бег" : sh ? L"быстро" : ct ? L"крадусь" : L"шаг";
  wchar_t buf[768];
  swprintf(buf, 768,
           L"Камера: %ls   Ввод: %ls\n"
           L"Режим пути [C]: %ls   Стрейф [R]: %ls   Аманда следует [F]: %ls\n"
           L"Вентиляция: %ls%ls\n"
           L"ИИ [F4, F5 — сброс]: %ls   Скорость: %ls   Атака: ЛКМ   Деревьев: %u",
           cam, cm ? L"WASD" : L"стрелки", pathOn ? L"ВКЛ" : L"выкл", !fmode ? L"выкл" : StrafeActive() ? L"вкл" : L"пауза",
           amandaFollow ? L"вкл" : L"выкл", vs, ductOn ? L"   [в шахте]" : L"",
           ai, sp, unsigned(multi[G_ARROW].size()));
  OverlaySet(0, buf);
}

void InstallActors() {
  using Interceptor::Attach;
  Attach(VA(va::CharPhysPos), nullptr, OnCharPhysLeave);
  Attach(VA(va::BackstageShutdown), OnVentShutdownPause, nullptr);
  Every(20, "attack", AttackTick);
  Attach(VA(va::LocoUpdate), nullptr, OnKillGameTick);  // удар по андроиду — в потоке игры
  Attach(VA(va::MeleeStart), OnMeleeStartEnter, OnMeleeStartLeave);
  // вылеты «abort»: кто вызвал — в лог, пока процесс ещё жив
  Attach(VA(va::CrtPurecall), [](Invocation& inv) { LogStackChain("PURE VIRTUAL CALL (a destroyed object)", inv.cpu->esp); }, nullptr, false);
  Attach(VA(va::CrtTerminate), [](Invocation& inv) { LogStackChain("TERMINATE (unhandled C++ exception)", inv.cpu->esp); }, nullptr, false);
  Attach(VA(va::VictimActStart), nullptr, OnVictimActLeave);
  Every(8, "amanda", AmandaTick);
  Every(40, "ai", AiTick);
  Every(200, "panic", PanicTick);
  Every(90, "menu", MenuTick);
  Every(300, "status", StatusTick);
}
