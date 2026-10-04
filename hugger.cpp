// hugger.cpp — играть за лицехвата.
// В его дереве (facehugger_behave.bml в data/BINARY_BEHAVIOR/_DIRECTORY_CONTENTS.BML) мод добавляет ветку SEL_HUGGER —
// сразу после CINEMATIC_BRANCH (урон, загрузка, смерть и катсцены по-прежнему главнее). В ней рубильники
// ConditionDebugMenuLinkTest: «Hugger Attack» (ActionMeleeAttack VENT — прыжок лицехвата, как в его ATTACK_BRANCH),
// «Hugger Next Target», «Hugger Path control» (ActionMoveToMostRecentSensedPosition — точку подставляем, как в режиме
// пути Чужого), «Hugger Arrow control» (ActionMoveInDirection, Distance = 124 — по этому числу мод узнаёт узел) и
// «Hugger Arrow idle» (стоять). Все выключены — лицехваты живут своим ИИ.
// Узлы дерева общие для всех лицехватов (каждая проверка получает персонажа и его состояние), поэтому сами рубильники
// не трогаем: перехватываем их проверку (слот 248 vtable узла, 0x6ef6f0: «cmp byte [ecx+0x14]») и отвечаем «да» только
// тому лицехвату, за которого играешь. Запрос движения у каждого свой — в его состоянии узла.
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include "mp.h"
#include "puppet.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

enum HugGate { HG_ATTACK, HG_NEXT, HG_PATH, HG_ARROW, HG_IDLE, HG_COUNT };
static const char* const kHugText[HG_COUNT] = { "Hugger Attack", "Hugger Next Target", "Hugger Path control",
                                                "Hugger Arrow control", "Hugger Arrow idle" };
static constexpr int kMaxGates = 64, kMaxMoves = 16, kSeen = 16;
static std::atomic<addr> s_gate[kMaxGates];      // узлы-рубильники лицехватов (все экземпляры дерева)
static std::atomic<int> s_gateKind[kMaxGates];
static std::atomic<int> s_gateN{ 0 };
static addr s_move[kMaxMoves];                    // узлы ActionMoveInDirection лицехватов (под общим замком)
static int s_moveN = 0;
static std::atomic<bool> s_want[HG_COUNT];        // что отвечают рубильники тому, за кого играешь
static std::atomic<addr> s_hugForce{ 0 };         // жертва прыжка: цель лицехвата перед стартом атаки (выбирает мод)
static std::atomic<addr> s_ctlHook{ 0 };          // он же — для хука без замка
static std::atomic<bool> s_ctlAndroid{ false };    // играешь за андроида (не лицехвата)
enum LeapPhase { LP_NONE, LP_WANT, LP_ACTIVE, LP_DONE };
static std::atomic<int> s_leapPhase{ LP_NONE };   // прыжок: заказан / идёт (состояние игры живо) / кончился
static void HugSetTarget(addr ch, addr v, bool create = false);  // цель; create — подставить запись цели (поток игры)
static bool LeapAlive(addr ch);                  // у него живое состояние прыжка, ещё не помеченное «кончено»
static std::atomic<addr> s_leapVictim{ 0 };
static std::atomic<addr> s_leapOwner{ 0 };         // лицехват, который прыгает (прыжок доводим и после B/Esc)
static std::atomic<bool> s_attached{ false };      // лицехват держится за лицо жертвы (состояние прыжка держим живым)
static std::atomic<bool> s_pinOn{ false };         // жертва легла: лицехват лежит на её лице (ставим каждый кадр)
struct Seen { std::atomic<addr> ch; std::atomic<uint32_t> at; };
static Seen s_seen[kSeen];                         // кто недавно проверял рубильники мода (значит, ветка у него есть)
// остальные лицехваты: что отвечать их рубильникам (биты HugGate) — считает HuggerTick, читает горячий хук
struct Other { std::atomic<addr> ch; std::atomic<uint32_t> want; };
static Other s_other[kSeen];
int HugAiNow() { return hugger && hugAi == 2 ? 3 : hugAi; }
int AndroidAiNow() { return hugger && androidAi == 2 ? 3 : androidAi; }  // играешь за NPC — к Аманде не лезут
static bool IsPlayableNpc(addr e);

// ---------------- отцепление мода: узлы — следующей DLL (reload.cpp) ----------------
void HugExport(std::vector<NodeRec>& out) {
  const int n = s_gateN;
  for (int i = 0; i < n; i++)
    if (const addr p = s_gate[i]; p && Alive(p, va::VT_GATE)) out.push_back({ NR_HUG_GATE + s_gateKind[i], p, 0 });
  for (int i = 0; i < s_moveN; i++)
    if (s_move[i] && Alive(s_move[i], va::VT_MOVE)) out.push_back({ NR_HUG_MOVE, s_move[i], 0 });
}
static void HugAddGate(int k, addr p);
void HugAdopt(const NodeRec& r) {
  if (r.kind >= NR_HUG_GATE && r.kind < NR_HUG_GATE + HG_COUNT && Alive(r.p, va::VT_GATE)) HugAddGate(r.kind - NR_HUG_GATE, r.p);
  else if (r.kind == NR_HUG_MOVE && Alive(r.p, va::VT_MOVE)) HugOnMoveNode(r.p);
}

// ---------------- хуки (control.cpp) ----------------
void HugForget(addr node) {  // по этому адресу создан новый узел — старый (прошлый уровень) мёртв
  const int n = s_gateN;
  for (int i = 0; i < n; i++)
    if (s_gate[i] == node) s_gate[i] = 0;
  for (int i = 0; i < s_moveN; i++)
    if (s_move[i] == node) s_move[i] = 0;
}
void HugNodeDestroyed(addr node) { HugForget(node); }
bool HugHasGates() {
  const int n = s_gateN;
  for (int i = 0; i < n; i++)
    if (s_gate[i] && Alive(s_gate[i], va::VT_GATE)) return true;
  return false;
}
void HugOnGate(const char* t, addr p) {  // «Hugger Attack» / «Android Attack» и т. д. — вид по второй части имени
  const char* suffix = strncmp(t, "Android ", 8) == 0 ? t + 8 : strncmp(t, "Hugger ", 7) == 0 ? t + 7 : nullptr;
  if (!suffix) return;
  int k = -1;
  for (int i = 0; i < HG_COUNT; i++)
    if (strcmp(suffix, kHugText[i] + 7) == 0) k = i;
  if (k >= 0) HugAddGate(k, p);
}
static void HugAddGate(int k, addr p) {
  const int n = s_gateN;
  int slot = -1;
  for (int i = 0; i < n && slot < 0; i++)
    if (!s_gate[i] || !Alive(s_gate[i], va::VT_GATE)) slot = i;
  if (slot < 0) {
    if (n >= kMaxGates) return;
    slot = n;
  }
  s_gate[slot] = 0;  // хук не должен увидеть узел с чужим видом
  s_gateKind[slot] = k;
  s_gate[slot] = p;
  if (slot == n) s_gateN = n + 1;
  if (k == HG_IDLE) Log("facehugger branch: gates ready (%08X)", unsigned(p));
}
void HugOnMoveNode(addr n) {
  for (int i = 0; i < s_moveN; i++)
    if (!s_move[i] || !Alive(s_move[i], va::VT_MOVE)) {
      s_move[i] = n;
      return;
    }
  if (s_moveN < kMaxMoves) s_move[s_moveN++] = n;
}
static bool IsHugMove(addr node);
bool HugIsMoveNode(addr node) { return IsHugMove(node); }
static bool IsHugMove(addr node) {
  for (int i = 0; i < s_moveN; i++)
    if (node && s_move[i] == node) return true;
  return false;
}

static void NoteSeen(addr ch) {  // горячий: запомнить, что у этого персонажа ветка мода есть
  const uint32_t now = uint32_t(NowMs());
  int free = -1;
  for (int i = 0; i < kSeen; i++) {
    const addr c = s_seen[i].ch.load(std::memory_order_relaxed);
    if (c == ch) {
      s_seen[i].at.store(now, std::memory_order_relaxed);
      return;
    }
    if (free < 0 && (!c || now - s_seen[i].at.load(std::memory_order_relaxed) > 5000)) free = i;
  }
  if (free >= 0) {
    s_seen[free].ch.store(ch, std::memory_order_relaxed);
    s_seen[free].at.store(now, std::memory_order_relaxed);
  }
}
static bool SeenRecently(addr ch) {
  const uint32_t now = uint32_t(NowMs());
  for (Seen& s : s_seen)
    if (s.ch.load() == ch && now - s.at.load() < 1500) return true;
  return false;
}
// проверка рубильника: thiscall(узел, персонаж, контекст), возврат — bool (горячий, без замка)
// пробы для поиска узлов в памяти (treescan.cpp): какие из найденных рубильников игра на самом деле проверяет
static constexpr int kProbes = 16;
static std::atomic<addr> s_probe[kProbes];
static std::atomic<uint32_t> s_probeAt[kProbes];
static std::atomic<int> s_probeN{ 0 };
void GateProbeSet(const std::vector<addr>& nodes) {
  s_probeN = 0;
  int n = 0;
  for (addr p : nodes)
    if (n < kProbes) s_probeAt[n] = 0, s_probe[n++] = p;
  s_probeN = n;
}
bool GateProbeSeen(addr node) {
  const int n = s_probeN;
  for (int i = 0; i < n; i++)
    if (s_probe[i] == node) return s_probeAt[i] != 0;
  return false;
}
static void OnGateTestLeave(Invocation& inv) {
  const addr node = inv.ecx;
  if (const int pn = s_probeN.load(std::memory_order_relaxed))
    for (int i = 0; i < pn; i++)
      if (s_probe[i].load(std::memory_order_relaxed) == node) s_probeAt[i].store(uint32_t(NowMs()) | 1, std::memory_order_relaxed);
  // Чужой бьёт андроида (actors.cpp): перед стартом атаки его цель — андроид (иначе игра поставит свою)
  if (const addr ft = forceTarget.load(std::memory_order_relaxed); ft && node == forceTargetGate.load(std::memory_order_relaxed)) {
    const addr ch = inv.arg[0];
    Safe([&] { SetTargetOf(ch, ft); });
  }
  const int n = s_gateN.load(std::memory_order_relaxed);
  if (!n) return;
  for (int i = 0; i < n; i++) {
    if (s_gate[i].load(std::memory_order_relaxed) != node) continue;
    const int k = s_gateKind[i].load(std::memory_order_relaxed);
    const addr ch = inv.arg[0], ctl = s_ctlHook.load(std::memory_order_relaxed);
    // узел на месте старого (другой уровень, чужое дерево) — не наш: отвечать только за лицехватов
    if (!(ctl && ch == ctl) && !IsPlayableNpc(ch)) return;  // узел на месте старого — не наш
    NoteSeen(ch);
    bool on;
    if (ctl && ch == ctl) {
      on = s_want[k].load(std::memory_order_relaxed);
      if (k == HG_ATTACK && s_ctlAndroid.load(std::memory_order_relaxed)) {  // андроид: удар, пока открыт мод
        if (on)
          if (const addr v = s_hugForce.load(std::memory_order_relaxed)) HugSetTarget(ch, v, true);
      } else if (k == HG_ATTACK) {  // открыт, пока прыжок заказан или идёт (дерево не прервёт его и не начнёт второй)
        const int ph = s_leapPhase.load(std::memory_order_relaxed);
        on = ch == s_leapOwner.load(std::memory_order_relaxed) &&
             (ph == LP_WANT || ((ph == LP_ACTIVE || s_attached.load(std::memory_order_relaxed)) && LeapAlive(ch)));
        if (on)  // перед стартом прыжка — цель, которую выбрал мод (любой персонаж)
          if (const addr v = s_hugForce.load(std::memory_order_relaxed)) HugSetTarget(ch, v, true);
      }
    } else if (ch == s_leapOwner.load(std::memory_order_relaxed) && s_attached.load(std::memory_order_relaxed) &&
               LeapAlive(ch)) {  // отпустил управление, а он на лице — пусть держится
      on = k == HG_ATTACK;
    } else {  // остальные — по режиму ИИ (HuggerTick); ещё не посчитан — ручное: стоят, иначе свой ИИ
      int j = 0;
      while (j < kSeen && s_other[j].ch.load(std::memory_order_relaxed) != ch) j++;
      on = j < kSeen ? (s_other[j].want.load(std::memory_order_relaxed) >> k) & 1
                     : k == HG_IDLE && (IsHugger(ch) ? HugAiNow() : AndroidAiNow()) == 0;
    }
    inv.replaceRet(on ? 1 : 0);
    return;
  }
}

// запрос движения того, за кого играешь: ловим на Start его узла (как у Чужого), сбрасываем на Shutdown
static addr s_req = 0, s_reqOwner = 0;
static bool s_starting = false;
static addr s_startCh = 0, s_pendingReq = 0;
bool HugOnMoveStart(addr node, addr ch) {
  if (!IsHugMove(node)) return false;
  s_starting = true;
  s_startCh = ch;
  s_pendingReq = 0;
  return true;
}
void HugOnReq(addr r) { s_pendingReq = r; }  // из горячего хука, тот же поток, что и Start
// тип запроса движения и объекта ходьбы — как у Чужого (проверены): у чужого типа мод ничего не пишет (вылет андроида
// 0x471aff при сбросе его дерева — искали, не пишет ли мод не туда)
static addr ReqVtRef() {  // запомненный тип: запрос у Чужого живёт, только пока он идёт
  static addr cached = 0;
  addr vt = 0;
  if (req && SafeRead(req, vt) && vt) cached = vt;
  return cached;
}
void HugOnMoveStartLeave() {
  if (s_starting && s_startCh && s_startCh == hugger) {
    addr vt = 0, ref = ReqVtRef();
    if (s_pendingReq) SafeRead(s_pendingReq, vt);
    const bool ok = s_pendingReq && (!ref || vt == ref);
    if (ok) s_req = s_pendingReq, s_reqOwner = s_startCh;
    static unsigned n = 0;
    if (++n <= 40 || !ok)
      Log("%s move start: request %08X vt %08X (alien's %08X)%s", s_ctlAndroid ? "android" : "facehugger", unsigned(s_pendingReq),
          unsigned(vt ? ToVA(vt) : 0), unsigned(ref ? ToVA(ref) : 0), ok ? "" : " — NOT a move request, left alone");
  }
  s_starting = false;
  s_pendingReq = 0;
}
void HugOnMoveShutdown(addr node, addr ch) {
  if (IsHugMove(node) && ch && ch == s_reqOwner) s_req = s_reqOwner = 0;
}

// у лицехвата, который никого ещё не заметил, нет текущей записи цели (+0x34a8 -> +0x10) — атака без неё не стартует.
// Подставляем его же запись (про Аманду, +0x14/+0x0c) — в неё SetTargetOf и запишет жертву
// Слоты +0x0c/+0x10/+0x14 — «умные указатели»: у записи счётчик ссылок в +0x14 (в +0x10 — vtable подобъекта-счётчика,
// на нуле игра зовёт его слот 1 — удаление). Подставляя запись в +0x10, счётчик увеличиваем: иначе игра потом удалит
// запись, на которую ещё смотрит другой слот, и следующий доступ — вызов «чистого виртуального» метода и abort (вылеты
// после атак лицехвата, проверено: _purecall из FUN_006e22e0). Подставлять — только в потоке игры (create), из тика — нет.
static bool EnsureTargetEntry(addr ch, bool create) {
  bool ok = false;
  Safe([&] {
    const addr x = rp(ch + 0x34a8);
    if (!x) return;
    if (rp(x + 0x10)) {
      ok = true;
      return;
    }
    for (addr o : { addr(0x14), addr(0x0c), addr(0x18) }) {
      const addr en = rp(x + o);
      if (en && rd<addr>(en) == VA(va::VT_TARGET_ENTRY)) {
        if (create) {
          wr<int32_t>(en + 0x14, rd<int32_t>(en + 0x14) + 1);  // ещё одна ссылка — наша
          wr<addr>(x + 0x10, en);
        }
        ok = true;
        return;
      }
    }
  });
  return ok;
}
static void HugSetTarget(addr ch, addr v, bool create) {
  if (!EnsureTargetEntry(ch, create)) return;
  Safe([&] {
    if (rp(rp(ch + 0x34a8) + 0x10)) SetTargetOf(ch, v);  // без записи — цель поставит рубильник атаки (поток игры)
  });
}

// ---------------- кто лицехват ----------------
// тип персонажа: +0x64 -> +4 (проверено на уровне с лицехватами: VentFacehugger = 5, андроид CorpseTrap = 2)
bool IsHugger(addr e) {
  if (!e || e == npc || e == player) return false;
  uint32_t type = 0;
  return Safe([&] { type = rd<uint32_t>(rp(e + 0x64) + 4); }) && type == 5;
}
// андроид (Working Joe, тип 2) или тяжёлый андроид (7) — за них тоже можно играть (ветка «Android …» в android_behave)
static bool IsPlayableNpc(addr e) { return IsHugger(e) || (e && e != npc && e != player && IsAndroid(e)); }
int NpcCtlKind() { return !hugger ? 0 : s_ctlAndroid ? 2 : 1; }
bool HugIsPlayable(addr e) { return IsPlayableNpc(e); }
// за мёртвого (DLC: лицехват, упавший с Кейна; убитый огнём) играть нельзя
static bool HugPlayable(addr e) {
  int16_t hp = 0;
  return DeathState(e) == 0 && SafeRead(e + 0x33c, hp) && hp > 0;
}
std::vector<HugRow> HuggerList() {  // ближе к Аманде — раньше
  std::vector<HugRow> out;
  V3 a{};
  const bool hasA = player && Safe([&] { a = rv3(player + 0x70); });
  for (addr e : ActiveChars()) {
    if (!IsPlayableNpc(e) || !HugPlayable(e)) continue;
    HugRow r{ e, CharName(e) + (IsHugger(e) ? "" : TR("  [андроид]", "  [android]")), -1.f, e == hugger, SeenRecently(e) };
    V3 p{};
    if (hasA && Safe([&] { p = rv3(e + 0x70); })) r.dist = std::hypot(p.x - a.x, p.z - a.z) + std::fabs(p.y - a.y);
    out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(), [](const HugRow& x, const HugRow& y) { return x.dist < y.dist; });
  return out;
}

// ---------------- выбор ----------------
static void WantNone() {
  for (auto& w : s_want) w = false;
}
static addr s_ctl = 0;  // за кого играли в прошлый тик
static ms_t s_ctlAt = 0;
void ActHugger(addr e) {
  if (e && (!IsPlayableNpc(e) || !HugPlayable(e))) return;
  if (e == hugger) return;
  if (e && camMode == 0 && CamCutsceneActive()) {  // катсцена: камеру не отдаём (camera.cpp)
    cutsceneRefusedAt = NowMs();
    Log("npc: the game's own camera is active (cutscene) — wait until it ends");
    return;
  }
  const bool wasCam = camMode != 0 || camWant == 1 || camWant == 2;
  WantNone();
  hugger = e;
  s_ctlHook = e;
  s_ctlAndroid = e && !IsHugger(e);
  s_req = s_reqOwner = 0;
  CamUseHuggerCfg(e != 0 && !s_ctlAndroid);  // у андроида — камера как у Чужого (он ростом с человека)
  if (e) {
    g_amandaInput = false;
    camWant = 2, camKeyAt = NowMs();  // камера за спиной — на него (CamDo в потоке игры)
    Log("playing as %s %08X (%s)", s_ctlAndroid ? "android" : "facehugger", unsigned(e), CharName(e).c_str());
  } else {
    if (wasCam) camWant = 3, camKeyAt = NowMs();
    Log("facehugger released");
  }
}
void ActHuggerNext() {
  const std::vector<HugRow> list = HuggerList();
  if (list.empty()) {
    Log("no facehuggers on this level");
    return;
  }
  size_t i = 0;
  for (size_t k = 0; k < list.size(); k++)
    if (list[k].e == hugger) i = k + 1;
  ActHugger(list[i % list.size()].e);
}
void HugOnDtor(addr ch) {  // поток игры, объект ещё цел: камеру — Аманде до того, как он исчезнет
  Log("facehugger %08X is being destroyed", unsigned(ch));
  dyingChar = ch;
  if (s_leapOwner == ch) s_leapOwner = 0, s_attached = false, s_pinOn = false;
  s_leapPhase = LP_NONE;
  CamKillCam(0);
  if (!player) {  // уровень выгружается (Аманды уже нет): камеры не трогаем — игра пересоздаст их сама
    camMode = 0, camWant = 0;
    ActHugger(0);
    camWant = 0;
    return;
  }
  if (camMode != 0) CamRestore("facehugger destroyed");
  ActHugger(0);
}
void HuggerReset() {  // смена уровня: узлы забудутся сами (фабрика создаст новые на их месте), управление — сразу
  WantNone();
  CamKillCam(0);
  s_leapPhase = LP_NONE;
  s_attached = false;
  s_pinOn = false;
  s_leapOwner = 0;
  s_leapVictim = 0;
  hugger = 0;
  s_ctlHook = 0;
  s_ctl = 0;
  s_req = s_reqOwner = 0;
  hugPathNow = hugNoBranch = false;
  for (Other& o : s_other) o.ch = 0;  // лицехваты прошлого уровня
  CamUseHuggerCfg(false);
}

// ---------------- движение «по навмешу»: точку подставляем в хуке PathMoveTo (control.cpp) ----------------
static bool s_anyInput = false;
static float s_moveYaw = 0;
static int s_speed = 1;
bool HugPathFor(addr ch) { return ch && ch == hugger && hugPathNow; }
bool HugPathTarget(addr ch, V3& out) {
  if (!HugPathFor(ch)) return false;
  V3 p{};
  if (!Safe([&] { p = rv3(ch + 0x70); })) return false;
  const float d = s_anyInput ? 2.5f : 0.f;
  out = { p.x + std::sin(s_moveYaw) * d, p.y, p.z + std::cos(s_moveYaw) * d };
  return true;
}
int HugPathSpeed() { return s_speed; }

// ---------------- жертвы лицехвата: любой живой персонаж рядом ----------------
static addr s_victim = 0;  // выбранная цель (ПКМ)
static bool VictimOk(addr e, const V3& from, float maxDist) {
  if (!e || e == hugger || e == npc || IsHugger(e) || DeathState(e) != 0) return false;
  uint32_t type = 0;
  if (!Safe([&] { type = rd<uint32_t>(rp(e + 0x64) + 4); }) || type == 1) return false;  // Чужой — не жертва (не умирает, команду не берёт)
  if (s_ctlAndroid && (type == 2 || type == 7)) return false;                              // андроид своих не душит
  if (e == player && !hugAttackAmanda) return false;
  const std::vector<addr> act = ActiveChars();
  if (std::find(act.begin(), act.end(), e) == act.end()) return false;
  V3 p{};
  if (!Safe([&] { p = rv3(e + 0x70); })) return false;
  return std::hypot(p.x - from.x, p.z - from.z) <= maxDist && std::fabs(p.y - from.y) < 4.f;
}
static std::vector<std::pair<float, addr>> Victims(const V3& from, float maxDist) {
  std::vector<std::pair<float, addr>> out;
  for (addr e : ActiveChars()) {
    if (!VictimOk(e, from, maxDist)) continue;
    V3 p{};
    Safe([&] { p = rv3(e + 0x70); });
    out.push_back({ std::hypot(p.x - from.x, p.z - from.z), e });
  }
  std::sort(out.begin(), out.end());
  return out;
}
static addr NextVictim(const V3& from, addr cur) {  // ПКМ: по кругу, ближние — раньше
  const auto v = Victims(from, 15.f);
  if (v.empty()) return 0;
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].second == cur) return v[(i + 1) % v.size()].second;
  return v.front().second;
}
static addr NearestVictim(const V3& from, float yaw) {  // ЛКМ без выбранной цели: ближний перед лицехватом (или совсем рядом)
  for (const auto& d : Victims(from, 6.f)) {
    V3 p{};
    Safe([&] { p = rv3(d.second + 0x70); });
    if (d.first < 2.f || std::fabs(WrapPi(std::atan2(p.x - from.x, p.z - from.z) - yaw)) < 1.75f) return d.second;
  }
  return 0;
}

// ---------------- прыжок: состояние игры (game.h, HugLeapSet) и «лицехват на лице» ----------------
// ЛКМ: цель дальше kReach — лицехват сначала подбегает (сам, бегом), потом рубильник «Hugger Attack» открыт, пока игра
// не заведёт состояние прыжка, и пока оно живо (иначе дерево прервёт прыжок или запустит второй). Конец анимации прыжка
// (End_Action) у цели-не-игрока мод забирает себе: сдачи лицехвату нет; жертва умирает обычной смертью (падает телом —
// «смерть от лицехвата» игры у NPC пустая, они стоят столбом); лицехват остаётся на лице: состояние прыжка держим живым,
// а его систему координат («прилип», персонаж+0x3f0+0xba0) ставим от кости головы жертвы — куда голова, туда и он.
// Пробел — отцепиться.
static constexpr float kReach = 1.8f;              // с какого расстояния прыгает (в игре — 1.7 м)
static std::atomic<bool> s_detachReq{ false };     // отцепиться (Пробел, жертва пропала)
static std::atomic<bool> s_forceEnd{ false };      // End_Action не пришёл — сделать то же самим (поток игры)
static ms_t s_leapAt = 0, s_leapSeenAt = 0;
static std::atomic<bool> s_endSeen{ false };
static addr s_approach = 0;                         // к кому подбегаем
static ms_t s_approachUntil = 0;
static bool s_approachKeys = false;

static addr HugStateOf(addr ch) {
  addr s = 0;
  if (ch) SafeRead(ch + 0x36c, s);
  return s;
}
static bool IsLeapState(addr s) {
  addr vt = 0;
  return s && SafeRead(s, vt) && vt == VA(va::VT_HUG_LEAP);
}
// состояние прыжка живо и игра ещё не пометила его законченным (+8: биты 2|4). Кончилось — рубильник атаки сразу
// закрыт, иначе дерево тут же начинает второй прыжок (на мёртвую жертву — отказ, и дерево лицехвата сбивается)
static bool LeapAlive(addr ch) {
  const addr s = HugStateOf(ch);
  uint8_t fl = 0xff;
  return IsLeapState(s) && SafeRead(s + 8, fl) && (fl & 6) == 0;
}
static uint8_t StateId(addr ch) {
  uint8_t id = 0;
  if (const addr s = HugStateOf(ch)) SafeRead(s + 0x1a, id);
  return id;
}
// состояние удара андроида (FUN_006596d0: id 0x42) живо и не помечено законченным (+8: биты 2|4)
static bool AndroidAttackAlive(addr ch) {
  const addr s = HugStateOf(ch);
  uint8_t id = 0, fl = 0xff;
  return s && SafeRead(s + 0x1a, id) && id == 0x42 && SafeRead(s + 8, fl) && (fl & 6) == 0;
}
static const char* NameOr(addr e) {  // для Log: несколько имён в одной строке — свои буферы
  static thread_local std::string ring[4];
  static thread_local int i = 0;
  std::string& s = ring[i++ & 3];
  s = e ? CharName(e) : std::string("-");
  return s.c_str();
}

// ---- кость головы жертвы (поток игры). Матрицы 4x4 Havok: m[столбец*4 + строка], перенос — m[12..14] ----
struct alignas(16) Mat { float m[16]; };
static Mat MatMul(const Mat& a, const Mat& b) {
  Mat r;
  for (int c = 0; c < 4; c++)
    for (int w = 0; w < 4; w++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[k * 4 + w] * b.m[c * 4 + k];
      r.m[c * 4 + w] = s;
    }
  return r;
}
static Mat RigidInv(const Mat& a) {  // [R t] -> [Rт, -Rт·t]
  Mat r{};
  for (int c = 0; c < 3; c++)
    for (int w = 0; w < 3; w++) r.m[c * 4 + w] = a.m[w * 4 + c];
  for (int w = 0; w < 3; w++) r.m[12 + w] = -(r.m[w] * a.m[12] + r.m[4 + w] * a.m[13] + r.m[8 + w] * a.m[14]);
  r.m[15] = 1;
  return r;
}
static int s_headBone = -1;
static std::atomic<float> s_devTilt{ 0 };
static std::atomic<int> s_boneLayer{ 0 };           // слой позы: 0 — итоговая (с ragdoll), 7 — анимация (камера BoneAttached)
static std::atomic<bool> s_devBones{ false };
static Mat s_rel;                                   // «прилип» лицехвата в системе кости головы
static bool s_relHave = false;
static bool AnimReady(addr ch) {
  uint8_t ok = 0;
  return SafeRead(ch + 0x3f0 + 0xb5c, ok) && ok;
}
static int FindHeadBone(addr v) {
  if (!AnimReady(v)) return -1;
  for (const char* n : { "Head", "Head1", "head", "HEAD", "Neck", "Neck1" }) {
    int i = -1;
    Safe([&] { i = Fn<BoneIndexFn>(va::BoneIndex)(v + 0x3f0, n, s_boneLayer.load()); });
    if (i >= 0) {
      Log("facehugger: victim head bone \"%s\" = %d", n, i);
      return i;
    }
  }
  return -1;
}
static bool HeadMatrix(addr v, Mat& out) {
  if (s_headBone < 0 || !v || !AnimReady(v)) return false;
  uint8_t ok = 0;
  return Safe([&] { ok = Fn<BoneMatrixFn>(va::BoneMatrix)(v + 0x3f0, s_headBone, s_boneLayer.load(), out.m); }) && ok;
}
// точка для кадра атаки (camera.cpp, поток игры): голова жертвы, пока лицехват прыгает/держится
bool HugAttackFocus(V3& out) {
  const addr v = s_leapVictim.load();
  if (!v || (s_leapPhase == LP_NONE && !s_attached && !s_pinOn)) return false;
  if (s_headBone < 0) s_headBone = FindHeadBone(v);
  Mat m;
  if (!HeadMatrix(v, m)) return false;
  out = { m.m[12], m.m[13], m.m[14] };
  return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
// Жертва легла. Персонаж в игре всегда вертикален (наклон из «прилип» игра отбрасывает — проверено), поэтому на лежащем
// лице прыжок отпускаем, а лицехват в своей позе «на полу» (плоский) каждый кадр ставим на голову жертвы, хвостом к шее
static int s_neckBone = -1;
static std::atomic<float> s_pinDy{ 0.02f };
static void OnPinTick(Invocation&) {  // поток игры (апдейт локомоции), горячий
  if (!s_pinOn.load(std::memory_order_relaxed)) return;
  static ms_t at = 0;
  const ms_t t = NowMs();
  if (t - at < 8) return;
  at = t;
  const addr h = s_leapOwner.load(), v = s_leapVictim.load();
  Mat head;
  if (!h || !HeadMatrix(v, head)) return;
  if (s_neckBone < 0)
    for (const char* n : { "Neck", "Neck1", "neck", "Spine2", "Spine1" }) {
      int i = -1;
      Safe([&] { i = Fn<BoneIndexFn>(va::BoneIndex)(v + 0x3f0, n, s_boneLayer.load()); });
      if (i >= 0) {
        s_neckBone = i;
        break;
      }
    }
  V3 from{};
  Mat neck;
  uint8_t ok = 0;
  if (s_neckBone >= 0 && Safe([&] { ok = Fn<BoneMatrixFn>(va::BoneMatrix)(v + 0x3f0, s_neckBone, s_boneLayer.load(), neck.m); }) && ok)
    from = { neck.m[12], neck.m[13], neck.m[14] };
  else
    Safe([&] { from = rv3(v + 0x70); });
  const float dx = head.m[12] - from.x, dz = head.m[14] - from.z;
  const float yaw = (dx * dx + dz * dz > 1e-4f) ? std::atan2(dx, dz) : rf(h + 0x174);
  TpMove(h, head.m[12], head.m[13] + s_pinDy.load(), head.m[14]);
  Safe([&] { wf(h + 0x174, yaw); });
}
bool HugAttackLying() { return s_pinOn.load(); }
// лицехват на лице: запомнить его «прилип» относительно головы жертвы, жертву — убить (падает телом)
static void AttachAndKill(addr ch, addr v) {
  s_relHave = false;
  s_headBone = FindHeadBone(v);
  Mat head, frame;
  if (HeadMatrix(v, head) && Safe([&] { memcpy(frame.m, reinterpret_cast<const void*>(ch + 0x3f0 + 0xba0), 64); })) {
    s_rel = MatMul(RigidInv(head), frame);
    s_relHave = true;
    Log("facehugger: on the face of %s (head %.2f %.2f %.2f, frame %.2f %.2f %.2f)", NameOr(v), head.m[12], head.m[13],
        head.m[14], frame.m[12], frame.m[13], frame.m[14]);
  } else {
    Log("facehugger: no head bone on %s — it stays where it landed", NameOr(v));
  }
  s_attached = true;
  if (DeathState(v) == 0) QueueKill(v, v, 0);  // «сам себя» — обычная смерть, без пустой смерти «от лицехвата»
}

// ---- хуки (поток игры) ----
// копии из одной сущности делят GUID: в коде прыжка «персонаж по ручке» — наша жертва, а не первая копия (горячий, без замка)
static bool InLeapCode(addr ra) {
  const addr v = ToVA(ra);
  return (v >= 0x0065d2e0 && v < 0x0065d70d) || (v >= 0x0065fb80 && v < 0x0065fc7d) || (v >= 0x0066fff0 && v < 0x006701db);
}
static void OnCharByHandleLeave(Invocation& inv) {
  const int ph = s_leapPhase.load(std::memory_order_relaxed);
  if ((ph != LP_WANT && ph != LP_ACTIVE && !s_attached.load(std::memory_order_relaxed)) || !InLeapCode(inv.ra)) return;
  const addr got = inv.retval(), v = s_leapVictim.load(std::memory_order_relaxed);
  if (!got || !v || got == v) return;
  uint32_t a0 = 0, a1 = 0, b0 = 1, b1 = 1;
  if (Safe([&] { a0 = rd<uint32_t>(got + 0x54), a1 = rd<uint32_t>(got + 0x58), b0 = rd<uint32_t>(v + 0x54), b1 = rd<uint32_t>(v + 0x58); }) &&
      a0 == b0 && a1 == b1)
    inv.replaceRet(v);
}
// «прилип» лицехвата: вместо корня жертвы — от кости её головы (FUN_006894c0 из апдейта прыжка, горячий, без замка)
static void OnAttachFrameEnter(Invocation& inv) {
  if (ToVA(inv.ra) != va::LeapFrameRet) return;
  if (!s_relHave || !s_attached.load(std::memory_order_relaxed)) return;
  if (inv.ecx - 0x3f0 != s_leapOwner.load(std::memory_order_relaxed)) return;
  Mat head;
  if (!HeadMatrix(s_leapVictim.load(std::memory_order_relaxed), head)) return;
  Mat f = MatMul(head, s_rel);
  if (const float tilt = s_devTilt.load(std::memory_order_relaxed)) {  // отладка: наклон «прилип» вокруг его X
    Mat r{};
    const float c = std::cos(tilt), sn = std::sin(tilt);
    r.m[0] = 1, r.m[5] = c, r.m[6] = sn, r.m[9] = -sn, r.m[10] = c, r.m[15] = 1;
    const V3 t{ f.m[12], f.m[13], f.m[14] };
    f = MatMul(f, r);
    f.m[12] = t.x, f.m[13] = t.y, f.m[14] = t.z;
  }
  Safe([&] { memcpy(reinterpret_cast<void*>(inv.arg[0]), f.m, 64); });
}
// андроид мода бьёт (окно удара): номер попытки для лога (дерево повторяет отказанный старт каждый кадр)
static std::atomic<bool> s_meleeOn{ false };
static std::atomic<int> s_meleeLogN{ 0 };
static std::atomic<bool> s_meleeGo{ false };  // игра начала анимацию удара андроида
static std::atomic<ms_t> s_meleeGoAt{ 0 };
static bool s_attackSeen = false;  // состояние удара андроида уже было живо (HuggerTick)
static bool MeleeLogOk() { return s_meleeLogN.fetch_add(1) < 6; }
static void OnHugMeleeEnter(Invocation& inv) {  // MeleeStart: thiscall(узел, персонаж, состояние узла, ...)
  const addr ch = inv.arg[0];
  if (!ch || ch != s_ctlHook.load()) return;
  // андроид: тип атаки узла 5 («любая») игра превращает в удар/захват по своему выбору. Ставим 1 — ветка
  // FUN_006dc310 (выбор атаки из ANDROID_ATTACK_DATA), где мод подставит убийство (OnAndroidChooseLeave)
  if (s_ctlAndroid.load() && s_meleeOn.load()) {
    uint32_t ty = 0;
    if (SafeRead(inv.ecx + 8, ty) && ty != 1) {
      inv.text[0] = 1;
      memcpy(inv.text + 4, &ty, 4);
      Safe([&] { wr<uint32_t>(inv.ecx + 8, 1); });
    }
  }
  if (s_ctlAndroid.load() && !MeleeLogOk()) return;
  addr t = 0;
  uint32_t id = 0, tid = 0, type = 0;
  Safe([&] {
    const addr lg = rp(rp(ch + 0x34a8) + 0x10);
    t = rp(lg + 0x42c), id = rd<uint32_t>(lg + 0x430), type = rd<uint32_t>(inv.ecx + 8);
    if (t) tid = rd<uint32_t>(t + 0x50);
  });
  Log("facehugger melee start: target %s id %u/%u attack type %u", NameOr(t), id, tid, type);
}
static void OnHugMeleeLeave(Invocation& inv) {
  const addr ch = inv.arg[0];
  if (!ch || ch != s_ctlHook.load()) return;
  if (inv.text[0] == 1) {  // тип атаки узла — назад
    uint32_t ty = 0;
    memcpy(&ty, inv.text + 4, 4);
    Safe([&] { wr<uint32_t>(inv.ecx + 8, ty); });
  }
  if (s_ctlAndroid.load() && s_meleeLogN.load() > 6) return;
  uint32_t st = 0;
  SafeRead(inv.arg[1] + 4, st);
  const addr s = HugStateOf(ch);
  Log("facehugger melee start -> node %u (8 = refused), state %02X%s", st, StateId(ch), IsLeapState(s) ? " = LEAP" : "");
}
static void OnLeapCheckLeave(Invocation& inv) {  // FUN_0065fb80: годится ли цель прыжка
  const addr ch = s_ctlHook.load();
  if (!ch || inv.ecx != HugStateOf(ch)) return;
  uint8_t fl = 0;
  addr tg = 0;
  Safe([&] { fl = rd<uint8_t>(inv.ecx + 8), tg = rp(inv.ecx + 0x44); });
  if (s_ctlAndroid.load()) {  // андроид: игра начала удар — смертельный удар мода откладываем, пока идёт анимация
    const bool ok = inv.cpu->eax & 0xff;
    if (ok && !s_meleeGo.exchange(true)) {
      s_meleeGoAt = NowMs();
      uint32_t id = 0;
      Safe([&] { id = rd<uint32_t>(rp(inv.ecx + 0x4c) + 4); });
      QueueKill(tg, ch, 15000);
      Log("android: the game started attack #%u on %s", id, NameOr(tg));
    }
    if (!ok && MeleeLogOk()) Log("android: attack check REFUSED (flags %02X, target %s)", fl, NameOr(tg));
    return;
  }
  Log("facehugger leap: target check %s (flags %02X, target %s, health %d)", (inv.cpu->eax & 0xff) ? "ok" : "REFUSED", fl,
      NameOr(tg), tg ? int(rd<int16_t>(tg + 0x33c)) : -1);
}
static void OnPairOkLeave(Invocation& inv) {  // FUN_006d4410(менеджер, нападающий, цель) — только старт прыжка лицехвата
  const addr ch = s_ctlHook.load();
  // андроиды: у менеджера пар андроидов (*(0x16f0c88)+0x5c, записи по 0x6c: +4 нападающий, +0x30 цель, +0x44 «можно»)
  // на «своих» людей +0x44 = 0 — игра отказывает. Андроиду мода его жертву разрешаем
  if (ch && s_ctlAndroid.load() && s_meleeOn.load() && inv.arg[0] == ch && inv.arg[1] &&
      inv.arg[1] == s_hugForce.load() && !(inv.cpu->eax & 0xff)) {
    inv.cpu->eax = (inv.cpu->eax & ~0xffu) | 1;
    if (MeleeLogOk()) Log("android: pair check for %s allowed by the mod", NameOr(inv.arg[1]));
    return;
  }
  if (!ch || inv.arg[0] != ch || s_ctlAndroid.load() || s_leapPhase.load() != LP_WANT) return;
  Log("facehugger leap: pair check %s (target %s)", (inv.cpu->eax & 0xff) ? "ok" : "REFUSED", NameOr(inv.arg[1]));
}
// выбор атаки андроида (слот +0x24, FUN_006d63d0): таблица ANDROID_ATTACK_DATA у выбора — +0x818 (записи по 0x74: +8 тип
// 0 hit / 1 grapple / 2 kill / 4 trap, +0xc маска целей: 1 игрок, 2 люди-НПС, 4 андроиды, 8 Чужой), число — байт +0x800.
// Игра выбирает по «уровню агрессии» (+0x81c: 2 — захват, 3 — убийство) и пропускает атаки, чья проверка места не прошла.
// Андроиду мода на его жертву — убийство (kill vs human / player), номер — один на удар
static std::atomic<int> s_killPick{ -1 };
static void OnAndroidChooseLeave(Invocation& inv) {
  const addr ch = s_ctlHook.load(std::memory_order_relaxed);
  if (!ch || !s_ctlAndroid.load(std::memory_order_relaxed) || !s_meleeOn.load(std::memory_order_relaxed)) return;
  const addr v = inv.arg[0];
  addr chooser = 0;
  if (!v || v != s_hugForce.load() || !SafeRead(ch + 0x34b4, chooser) || inv.ecx != chooser) return;
  int pick = s_killPick.load();
  if (pick < 0) {
    int cand[25], k = 0;
    Safe([&] {
      const uint32_t cls = rd<uint32_t>(rp(v + 0x64) + 4);
      const addr pl = rp(v + 0x31c);
      const uint32_t mask = (cls == 2 || cls == 7) ? 4u : (pl && rd<uint8_t>(pl + 0x48)) ? 1u : cls == 1 ? 8u : 2u;
      const int n = rd<uint8_t>(chooser + 0x800);
      const addr T = rp(chooser + 0x818);
      for (int i = 0; i < n && i < 25; i++)
        if (rd<uint32_t>(T + i * 0x74 + 8) == 2 && (rd<uint32_t>(T + i * 0x74 + 0xc) & mask)) cand[k++] = i;
    });
    if (!k) {
      if (MeleeLogOk()) Log("android: no kill animation for %s — the game's attack #%u", NameOr(v), unsigned(inv.cpu->eax));
      return;
    }
    pick = cand[std::rand() % k];
    s_killPick = pick;
    Log("android: kill animation #%d for %s (the game chose #%u)", pick, NameOr(v), unsigned(inv.cpu->eax));
  }
  inv.cpu->eax = uint32_t(pick);
}
static void OnLeapEndEnter(Invocation& inv) {  // End_Action: конец анимации прыжка (fastcall, без аргументов в стеке)
  const addr ch = s_leapOwner.load(), s = inv.ecx;
  if (!ch || s != HugStateOf(ch)) return;
  addr tg = 0;
  uint8_t done = 0;
  Safe([&] { tg = rp(s + 0x44), done = rd<uint8_t>(s + 0x84); });
  Log("facehugger leap: End_Action (target %s)", NameOr(tg));
  s_endSeen = true;
  if (!tg || tg == player || (done & 1)) return;  // Аманда — как в игре (это её смерть)
  inv.skip = true;
  inv.skipPop = 0;
  Safe([&] { wr<uint8_t>(s + 0x84, uint8_t(done | 1)); });
  AttachAndKill(ch, tg);
}
// апдейт прыжка: держим «прилип» (время прыжка не больше 4 с), отцепление, запасной End_Action
static void OnLeapUpdateEnter(Invocation& inv) {
  const addr ch = s_leapOwner.load(), s = inv.ecx;
  if (!ch || s != HugStateOf(ch)) return;
  if (s_devBones.exchange(false)) {  // отладка: голова жертвы во всех слоях позы
    const addr v = s_leapVictim.load();
    for (int layer = 0; layer < 12; layer++) {
      int i = -1;
      Mat m{};
      uint8_t ok = 0;
      Safe([&] { i = Fn<BoneIndexFn>(va::BoneIndex)(v + 0x3f0, "Head", layer); });
      if (i >= 0) Safe([&] { ok = Fn<BoneMatrixFn>(va::BoneMatrix)(v + 0x3f0, i, layer, m.m); });
      Log("bones: layer %d head %d ok %d at %.2f %.2f %.2f", layer, i, ok, m.m[12], m.m[13], m.m[14]);
    }
  }
  if (s_forceEnd.exchange(false)) {
    uint8_t done = 0;
    addr tg = 0;
    Safe([&] { tg = rp(s + 0x44), done = rd<uint8_t>(s + 0x84); });
    if (tg && tg != player && !(done & 1)) {
      Safe([&] { wr<uint8_t>(s + 0x84, uint8_t(done | 1)); });
      AttachAndKill(ch, tg);
    }
  }
  if (!s_attached.load()) return;
  Safe([&] {
    const float now = float(rd<double>(VA(va::GameTimeD)));
    if (s_detachReq.exchange(false)) {
      s_attached = false;
      s_relHave = false;
      wf(s + 0x78, now - 10.f);  // время прыжка вышло — игра сама его закончит
      Log("facehugger: let go of the face");
    } else if (now - rf(s + 0x78) > 4.f) {
      wf(s + 0x78, now - 4.f);
    }
  });
}
static void OnLeapDtorEnter(Invocation& inv) {
  const addr ch = s_leapOwner.load();
  if (!ch || inv.ecx != HugStateOf(ch)) return;
  if (s_leapPhase == LP_ACTIVE || s_leapPhase == LP_WANT || s_attached) Log("facehugger leap: state ended");
  s_leapPhase = LP_DONE;  // рубильник атаки — сразу закрыт: второго прыжка не будет
  s_attached = false;
  s_relHave = false;
}
// «смерть от лицехвата» игры (команда 28) нашей жертве не нужна: у NPC она пустая — стоят столбом
static void OnFhDeathEnter(Invocation& inv) {
  addr att = 0;
  SafeRead(inv.ecx, att);
  const addr v = inv.arg[0];
  if (v && v == s_leapVictim.load() && (s_leapPhase != LP_NONE || s_attached)) {
    inv.skip = true;
    inv.skipPop = 4;
    Log("death from facehugger for %s skipped — it dies the normal way", NameOr(v));
    return;
  }
  Log("death from facehugger: %s (attacker %s)", NameOr(v), NameOr(att));
}

// ---- андроид: удар/удушение — ActionMeleeAttack ANY (у андроидов есть убийства людей, ANDROID_ATTACK_DATA: kill vs human) ----
static constexpr float kReachAndroid = 1.4f;
static ms_t s_meleeUntil = 0;
static void StartMelee(addr v, ms_t t) {
  s_victim = v;
  s_approach = 0;
  s_hugForce = v;
  s_meleeUntil = t + 1500;
  s_meleeLogN = 0;
  s_meleeGo = false;
  s_killPick = -1;
  s_attackSeen = false;
  s_meleeOn = true;
  if (!EnsureTargetEntry(hugger, false)) Log("android: it has no target records — the game will refuse the attack");
  HugSetTarget(hugger, v);
  QueueKill(v, hugger, 4000);  // игра не убила за 4 с (нет парной анимации) — смертельный удар от андроида
  Log("android: attack %s", CharName(v).c_str());
}
static void StartLeap(addr v, ms_t t) {
  s_victim = v;
  s_approach = 0;
  s_hugForce = v;
  if (s_attached) s_detachReq = true;  // прошлый лицехват на лице — отпустит
  s_leapVictim = v;
  s_leapAt = t;
  s_leapSeenAt = 0;
  s_endSeen = false;
  s_forceEnd = false;
  s_leapPhase = LP_WANT;
  s_pinOn = false;
  s_leapOwner = hugger;
  s_headBone = s_neckBone = -1;  // кость головы — у новой жертвы своя (ищем при первом кадре атаки)
  if (!EnsureTargetEntry(hugger, false)) Log("facehugger: it has no target records — the game will refuse the leap");
  HugSetTarget(hugger, v);
  if (IsAndroid(v)) TempHumanClass(v, 12000);
  float d = -1;
  Safe([&] { const V3 a = rv3(hugger + 0x70), b = rv3(v + 0x70); d = std::hypot(a.x - b.x, a.z - b.z); });
  Log("facehugger: leap at %s (%.1f m)", CharName(v).c_str(), d);
}
// след в лог: состояния лицехвата и жертвы (id состояния, смерть, здоровье) — при каждой смене, 8 с после прыжка
static void LeapTrace(ms_t t, addr v) {
  static uint32_t last = 0xffffffff;
  if (!v || !hugger || t - s_leapAt > 8000) return;
  int16_t hp = 0;
  SafeRead(v + 0x33c, hp);
  const uint32_t key = uint32_t(StateId(hugger)) | uint32_t(StateId(v)) << 8 | DeathState(v) << 16 | uint32_t(hp != 0) << 24;
  if (key == last) return;
  last = key;
  Log("trace +%.2f: hugger %02X, %s state %02X death %u health %d", double(t - s_leapAt) / 1000.0, StateId(hugger),
      NameOr(v), StateId(v), DeathState(v), int(hp));
}
// раз в тик (за кого играешь): как идёт прыжок; запасные пути; отцепиться — Пробел
static void LeapTick(ms_t t) {
  const addr v = s_leapVictim.load();
  LeapTrace(t, v);
  const int ph = s_leapPhase;
  if (ph == LP_WANT) {
    if (IsLeapState(HugStateOf(hugger))) {
      s_leapPhase = LP_ACTIVE;
      s_leapSeenAt = t;
      Log("facehugger leap: started (state %02X)", StateId(hugger));
    } else if (t - s_leapAt > 1500) {
      s_leapPhase = LP_DONE;
      hugMsgAt = t;
      Log("facehugger leap: the game did not start it (state %02X)", StateId(hugger));
    }
  } else if (ph == LP_ACTIVE) {
    if (!LeapAlive(hugger)) {
      s_leapPhase = LP_DONE;
      Log("facehugger leap: finished");
    } else if (!s_endSeen && !s_attached && t - s_leapSeenAt > 2500) {  // End_Action так и не пришёл
      Log("facehugger leap: no End_Action in 2.5 s — holding on anyway");
      s_endSeen = true;
      s_forceEnd = true;
    }
  }
  if (ph == LP_DONE) {
    s_hugForce = 0;
    if (t - s_leapAt > 3000) s_leapPhase = LP_NONE;
  }
  // жертва мертва и лежит — лицехват ложится на её лицо (прыжок отпускаем, дальше ставим сами)
  static ms_t deadAt = 0;
  if (!s_attached || !v || hugger != s_leapOwner || DeathState(v) < 3) deadAt = 0;
  else if (!deadAt) deadAt = t;
  else if (t - deadAt > 600 && !s_pinOn) {
    s_pinOn = true;
    s_detachReq = true;
    Log("facehugger: the victim is down — it lies on the face");
  }
  // на лице: Пробел — отцепиться; жертва пропала (выгружена) — тоже
  static bool spWas = false;
  const bool sp = camMode != 0 && !photo.on && Down(VK_SPACE);
  if ((s_attached || s_pinOn) && hugger == s_leapOwner) {
    const std::vector<addr> act = ActiveChars();
    if ((sp && !spWas) || !v || std::find(act.begin(), act.end(), v) == act.end()) {
      if (s_attached) s_detachReq = true;
      if (s_pinOn) s_pinOn = false, Log("facehugger: let go of the face");
    }
  }
  spWas = sp;
}

void HugOnAnyDtor(addr ch) {  // поток игры: разрушают жертву, на которой висит лицехват, — отпустить
  if (ch && ch == s_leapVictim.load() && (s_attached || s_pinOn)) {
    s_pinOn = false;
    s_attached = false;
    s_relHave = false;
    s_detachReq = true;
    Log("facehugger: its victim is being destroyed — letting go");
  }
}

int NpcActionState() {
  if (!hugger) return 0;
  if (s_ctlAndroid) return (s_approach || NowMs() < s_meleeUntil) ? 1 : 0;
  if (hugger == s_leapOwner && s_pinOn) return 3;
  if (hugger == s_leapOwner && s_attached) return 2;
  return (s_approach || s_leapPhase == LP_WANT || s_leapPhase == LP_ACTIVE) ? 1 : 0;
}

// ---------------- голос андроида (G): «динамический диалог» игры (как CATHODE NPC_DynamicDialogue, FUN_004d79a0) ----------------
// Андроиды говорят не голосом Чужого, а событием Wwise «Android_Dynamic_Dialogue» (банк DIALOGUE_GENERIC) с 8 аргументами:
// Character_Voice (AN1/AN2/AN3/ANH — по персонажу), Target_Character, Action, Seen_Target, Android_Escalation,
// Suspicious_Item_Type, Last_Activated_Sense, Last_Hit_By_Weapon. Мод собирает путь аргументов так же, как игра, и ставит
// реплику в очередь речи (FUN_006bec90). Реплики — только те, что в дереве события есть у всех четырёх голосов
// (разбор банка — dev\ghidra_dumps\android_dialogue_paths.txt); "" — «любое значение» (*)
struct NpcLine { const char *action, *target, *seen, *esc, *item, *sense, *weapon, *ru, *en; };
static const NpcLine kAndroidLines[] = {
  { "Suspicious_Warning", "", "", "", "", "", "", "Предупреждение", "Warning" },
  { "Suspicious_Warning_Fail", "", "", "", "", "", "", "Последнее предупреждение", "Final warning" },
  { "Player_Observed", "Player", "", "", "", "", "", "Вижу человека", "Human spotted" },
  { "Detected_Enemy", "", "", "", "", "VISUAL", "", "Обнаружил", "Target detected" },
  { "Detected_Enemy", "", "", "", "", "MOVEMENT_SOUND", "", "Слышу движение", "Heard movement" },
  { "Search_Starts", "", "Unseen", "Aggressive", "", "", "", "Начинаю поиск", "Starts searching" },
  { "Search_Loop", "", "Unseen", "Aggressive", "", "", "", "Ищу", "Searching" },
  { "Search_Fail", "", "Unseen", "Aggressive", "", "", "", "Никого", "Search failed" },
  { "Idle_Chatter", "", "", "Passive", "", "", "", "Болтает", "Idle chatter" },
  { "Idle_Chatter", "", "", "Aggressive", "", "", "", "Болтает (враждебно)", "Hostile chatter" },
  { "Suspicious_Item_Initial", "", "Unseen", "", "DEAD_BODY", "", "", "Тело", "Dead body" },
  { "Suspicious_Item_Initial", "", "Unseen", "", "NOISE_MAKER", "", "", "Шумелка", "Noise maker" },
  { "Attack", "Player", "", "", "", "", "", "Атака", "Attack" },
  { "Enter_Grapple", "Player", "", "", "", "", "", "Захват", "Grapple" },
  { "Final_Hit", "Player", "", "", "", "", "", "Добивание", "Final hit" },
  { "Hit_By_Weapon", "", "", "", "", "", "IMPACT", "Получил удар", "Got hit" },
  { "Death", "", "", "", "", "", "", "Отключение", "Shutdown" },
};
static constexpr int kAndroidLineN = int(sizeof kAndroidLines / sizeof kAndroidLines[0]);
static std::atomic<int> s_npcLine{ -1 };
static std::atomic<addr> s_npcVoiceWho{ 0 };
static int s_voiceIdx = -1;
static ms_t s_voiceAt = 0;
const char* NpcVoiceName() {
  if (s_voiceIdx < 0 || NowMs() - s_voiceAt > 3000) return "";
  return TR(kAndroidLines[s_voiceIdx].ru, kAndroidLines[s_voiceIdx].en);
}
static void NpcVoiceSay(addr ch, int line) {
  s_npcVoiceWho = ch;
  s_npcLine = line;
}
// путь аргументов диалога (0xc4 байта, FUN_0049fbc0), шаблон персонажа — 0xd0; с запасом
struct alignas(16) DlgBuf { uint8_t b[0x100]; };
static bool SayNpcLine(addr ch, const NpcLine& L, const char** why) {
  const addr mgr = rd<addr>(VA(va::SoundMgrPtr));
  const addr names = mgr ? rp(mgr + 0xc4) : 0;
  if (!names) return *why = "no sound manager", false;
  addr ev = 0;
  if (!(Fn<DlgEventFindFn>(va::DlgEventFind)(names, "Android_Dynamic_Dialogue", &ev) & 0xff) || !ev)
    return *why = "the dialogue bank is not loaded", false;
  DlgBuf path{}, tmp{};
  Fn<DlgPathInitFn>(va::DlgPathInit)(path.b);
  Fn<DlgPathCopyFn>(va::DlgPathCopy)(path.b, Fn<DlgPathTemplateFn>(va::DlgPathTemplate)(ch + 0x11a0, tmp.b));
  path.b[0x21] = 1;
  const char* voice = Fn<VoiceActorNameFn>(va::VoiceActorName)(rd<int>(ch + 0x11a8));
  const char* args[8] = { voice ? voice : "", L.target, L.action, L.seen, L.esc, L.item, L.sense, L.weapon };
  for (const char* a : args) {
    uint32_t pair[2] = { 0, 0 };
    if (a && *a) Fn<DlgArgNameFn>(va::DlgArgName)(names, a, pair);  // (имя, хэш); не нашёл — «любое»
    Fn<DlgPathAddFn>(va::DlgPathAdd)(path.b, reinterpret_cast<const char*>(pair[0]), pair[1]);
  }
  const addr db = Fn<SoundDbFn>(va::SoundDb)(mgr);
  if (!db) return *why = "no speech queue", false;
  // приоритет 3 — как обычная реплика НПС (болтовню андроида, 1–2, перебивает). 5 — канал «рации» (мультиплеер): мимо
  // слотов речи, у андроида не звучит, а элемент очереди так и висит «играет» (было)
  const bool ok = Fn<SpeechQueueFn>(va::SpeechQueue)(db, 3, ev, path.b, rd<addr>(ch + 0x1320), ch, 2.f, 0, 0) & 0xff;
  *why = ok ? voice : "the speech queue refused (already talking?)";
  return ok;
}
// отладка: элементы очереди речи этого персонажа (база 0xb: +0x130 -> массив по 0x110, +0x134 число; элемент +0 приоритет,
// +8 звуковой объект, +0xe0 состояние: 1 ждёт, 3/4 играет; +0xe8 playing id)
static void LogSpeechItems(addr ch, const char* when) {
  Safe([&] {
    const addr mgr = rd<addr>(VA(va::SoundMgrPtr)), db = Fn<SoundDbFn>(va::SoundDb)(mgr), so = rd<addr>(ch + 0x1320);
    const addr arr = rp(rp(db + 0x130));
    const uint32_t n = std::min<uint32_t>(rd<uint32_t>(db + 0x134), 64);
    char b[200] = "";
    int o = 0;
    for (uint32_t i = 0; i < n && o < 180; i++) {
      const addr it = arr + i * 0x110;
      if (rp(it + 8) != so) continue;
      o += snprintf(b + o, sizeof b - o, "[p%u s%u id%X] ", rd<uint32_t>(it), rd<uint32_t>(it + 0xe0), rd<uint32_t>(it + 0xe8));
    }
    Log("speech items %s (%s): %s", NameOr(ch), when, o ? b : "none");
  });
}
static ms_t s_speechCheckAt = 0;
static addr s_speechCheckCh = 0;
static void OnNpcVoiceGameTick(Invocation&) {  // поток игры (апдейт локомоции)
  if (s_speechCheckAt && NowMs() > s_speechCheckAt) {
    s_speechCheckAt = 0;
    LogSpeechItems(s_speechCheckCh, "1 s later");
  }
  if (s_npcLine.load(std::memory_order_relaxed) < 0) return;
  const int line = s_npcLine.exchange(-1);
  const addr ch = s_npcVoiceWho.load();
  if (line < 0 || line >= kAndroidLineN || !ch) return;
  const char* why = "";
  bool ok = false;
  if (!Safe([&] { ok = SayNpcLine(ch, kAndroidLines[line], &why); })) why = "exception";
  Log("android line %s -> %s (%s%s)", kAndroidLines[line].action, NameOr(ch), ok ? "voice " : "", why ? why : "");
  LogSpeechItems(ch, "now");
  s_speechCheckAt = NowMs() + 1000, s_speechCheckCh = ch;
}
static void AndroidVoiceTick(ms_t t) {  // G — следующая реплика, Shift+G — случайная
  static ms_t keyAt = 0;
  if ((photo.on && PhotoActorNow() != PA_NPC) || !HotK(K_SOUND) || t - keyAt < 350) return;
  keyAt = t;
  s_voiceIdx = Down(VK_SHIFT) ? std::rand() % kAndroidLineN : (s_voiceIdx + 1) % kAndroidLineN;
  s_voiceAt = t;
  NpcVoiceSay(hugger, s_voiceIdx);
}

// ---------------- стрейф (как у Чужого, R): тело смотрит по камере, WASD — вбок/назад (у лицехвата есть
// WALK_STRAFE_*, WALK_DEFAULT_BACKWARDS). После апдейта локомоции пишем направление скорости L+0x1c ----------------
static std::atomic<bool> s_hugStrafe{ false }, s_hugMoving{ false };
static std::atomic<float> s_faceYawA{ 0 }, s_moveYawA{ 0 };
static void OnHugFaceLeave(Invocation&) {
  const addr h = s_ctlHook.load(std::memory_order_relaxed);
  if (!h || !s_hugStrafe.load(std::memory_order_relaxed)) return;
  // (была «сверка типа» объекта ходьбы с Чужим по первому полю — там не vtable, а число, и сверка всегда выключала
  // стрейф у андроида; вылет, ради которого её ставили, — запись узла движения, control.cpp OnMoveRecLeave)
  Safe([&] {
    const addr L = rp(h + va::CharLocoOff);
    if (!L) return;
    wf(L + 0x1c, s_hugMoving.load(std::memory_order_relaxed) ? s_moveYawA.load() : s_faceYawA.load());
  });
}

// ---------------- отладка: команды из файла dev\cmd.txt рядом с модом (строка — команда) ----------------
const std::wstring& ModBase();  // runtime.cpp
static bool s_devLeap = false, s_devRmb = false;
static addr s_devCam = 0;
extern std::atomic<addr> g_devCodeCam;  // camera.cpp
static ms_t s_devMoveUntil = 0;
static float s_devMoveRel = 0;  // направление относительно камеры (рад)
static int s_devSpeed = 1, s_devWhere = 1;
static ms_t s_devWhereAt = 0;
static void DevCmdTick() {
  static ms_t at = 0;
  const ms_t t = NowMs();
  if (s_devWhereAt && t > s_devWhereAt) spawnWhere = s_devWhere, s_devWhereAt = 0;
  if (t - at < 300) return;
  at = t;
  const std::wstring base = ModBase();
  const size_t sl = base.find_last_of(L'\\');
  const std::wstring path = (sl == std::wstring::npos ? std::wstring(L".") : base.substr(0, sl)) + L"\\dev\\cmd.txt";
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return;
  char buf[1024] = {};
  const size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  _wremove(path.c_str());
  buf[n] = 0;
  for (char* line = strtok(buf, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
    char cmd[32] = {};
    float a = 0, b = 0;
    const int k = sscanf(line, "%31s %f %f", cmd, &a, &b);
    if (k < 1) continue;
    Log("dev: %s", line);
    if (!strcmp(cmd, "hugger")) ActHuggerNext();
    else if (!strcmp(cmd, "release")) ActHugger(0);
    else if (!strcmp(cmd, "scan")) ActSpawnScan();
    else if (!strcmp(cmd, "spawn") && k >= 2) {
      if (!s_devWhereAt) s_devWhere = spawnWhere;
      spawnWhere = 2;  // перед тем, за кого играешь (точку игра считает позже — вернём через 2 с)
      s_devWhereAt = t + 2000;
      ActSpawnSelect(int(a) - 1);
      ActSpawn(true);
    } else if (!strcmp(cmd, "leap")) s_devLeap = true;
    else if (!strcmp(cmd, "rmb")) s_devRmb = true;
    else if (!strcmp(cmd, "strafe")) ActToggleStrafe();
    else if (!strcmp(cmd, "move") && k >= 2) {  // move <градусы от камеры> [мс]
      s_devMoveRel = a * 3.14159265f / 180.f;
      s_devMoveUntil = t + ms_t(k >= 3 ? b : 1500);
    } else if (!strcmp(cmd, "speed") && k >= 2) s_devSpeed = int(a);
    else if (!strcmp(cmd, "detach")) s_detachReq = true;
    else if (!strcmp(cmd, "voice") && k >= 2) {  // voice <номер реплики> [адрес] — реплика андроиду (по умолчанию — за кого играешь)
      char* sp2 = strchr(line + 6, ' ');
      const addr who = sp2 ? strtoul(sp2 + 1, nullptr, 16) : hugger;
      if (who) NpcVoiceSay(who, int(a));
    }
    else if (!strcmp(cmd, "pindy") && k >= 2) s_pinDy = a;
    else if (!strcmp(cmd, "mpself")) MpCmd(MPA_SELFTEST, 0, 0);  // мультиплеер: пакет самому себе через Steam
    else if (!strcmp(cmd, "mpcmd") && k >= 2) MpCmd(int(a), int(b), 0);  // mpcmd <MpAction> [arg] — как кнопка меню
    else if (!strcmp(cmd, "mptap") && k >= 2) MpCmd(MPA_TAP, int(a), 0);
    else if (!strcmp(cmd, "mpstat")) MpLogStats(k >= 2 ? int(a) : 25);
    else if (!strcmp(cmd, "mpclear")) MpCmd(MPA_CLEAR, 0, 0);
    else if (!strcmp(cmd, "mpdump")) {  // журнал выходов — в лог
      const MpSnap m = MpSnapshot();
      Log("mp: steam %d me %llu role %d state %d caught %u sent %u recv %u replayed %u muted %u missed %u odd-this %u noise %u (%u keys)",
          int(m.steam), static_cast<unsigned long long>(m.me), m.role, m.state, m.captured, m.sent, m.recv, m.replayed, m.suppressed,
          m.missed, m.ecxOdd, m.filtered, m.noisyKeys);
      for (size_t i = 0; i < m.trig.size(); i++)
        Log("mp: #%u %s %08X:%08X out %08X %s delay %.2f x%u%s", unsigned(i), m.trig[i].kind.c_str(), m.trig[i].ent, m.trig[i].inst,
            m.trig[i].out, MpOutName(m.trig[i].out) ? MpOutName(m.trig[i].out) : "?", double(m.trig[i].delay), m.trig[i].n,
            m.trig[i].remote ? " host" : "");
    }
    else if (!strcmp(cmd, "mpreplay") && k >= 2) {  // mpreplay <номер в журнале> — повторить выход у себя
      const MpSnap m = MpSnapshot();
      const size_t i = size_t(a);
      if (i < m.trig.size()) MpCmd(MPA_REPLAY, int(i), uint64_t(m.trig[i].ent) << 32 | m.trig[i].out);
    }
    else if (!strcmp(cmd, "camcode")) g_devCodeCam = strtoul(line + 7, nullptr, 16);
    else if (!strcmp(cmd, "amodel") && k >= 2) {  // amodel 0/1 — спрятать/вернуть, 2 — части в лог, 3/4 <имя> — одну часть
      const char* nm = strchr(line + 7, ' ');
      snprintf(devPartName, sizeof devPartName, "%s", nm ? nm + 1 : "");
      devAmandaModel = int(a);
    }
    else if (!strcmp(cmd, "parts")) devPartsOf = strtoul(line + 6, nullptr, 16);  // parts <адрес> — подмодели в лог
    else if (!strcmp(cmd, "cam") && k >= 2) devCamReq = int(a);          // cam 0..3 — как ActCamera (3 — Аманде)
    else if (!strcmp(cmd, "photo") && k >= 2) devPhotoReq = int(a);      // photo 0/1
    else if (!strcmp(cmd, "tilt") && k >= 2) s_devTilt = a * 3.14159265f / 180.f;
    else if (!strcmp(cmd, "camon")) s_devCam = strtoul(line + 5, nullptr, 16);  // camon <адрес> — камера за ним (0 — как обычно)
    else if (!strcmp(cmd, "bones")) s_devBones = true;  // слои позы головы жертвы — в лог (поток игры, апдейт прыжка)
    else if (!strcmp(cmd, "layer") && k >= 2) s_boneLayer = int(a);
    else if (!strcmp(cmd, "bonetest")) {  // bonetest — кости головы Чужого: индекс и мировая матрица (поток задач мода)
      for (int ly : { 0, 7 })
        for (const char* nm : { "Head1", "Head", "Neck1", "Neck", "Spine3" }) {
          int bi = -2;
          float m[16] = {};
          uint8_t ok = 0;
          const bool okCall = Safe([&] { bi = Fn<BoneIndexFn>(va::BoneIndex)(npc + 0x3f0, nm, ly); });
          if (okCall && bi >= 0) Safe([&] { ok = Fn<BoneMatrixFn>(va::BoneMatrix)(npc + 0x3f0, bi, ly, m); });
          Log("bonetest: layer %d %s -> %d (call %s) ok %u pos %.2f %.2f %.2f rowZ %.2f %.2f %.2f rowX %.2f %.2f %.2f", ly, nm, bi,
              okCall ? "ok" : "CRASH", unsigned(ok), m[12], m[13], m[14], m[8], m[9], m[10], m[0], m[1], m[2]);
        }
    }
    else if (!strcmp(cmd, "headscale") && k >= 3) {  // headscale <кость> <мс> [режим 0 — поза модели, 1 — локальная, 2 — обе]
      int mode = 0;
      sscanf(line, "%*s %*f %*f %d", &mode);
      devHeadBone = int(a), devHeadMode = mode, devHeadUntil = t + ms_t(b);
    }
    else if (!strcmp(cmd, "lookforce") && k >= 2) {  // lookforce <мс> [theta] [phi] — LOOK без клавиши, свои углы
      float th = -1.f, ph = 0.f;
      sscanf(line, "%*s %*f %f %f", &th, &ph);
      devLookTheta = th, devLookPhi = ph;
      devLookUntil = t + ms_t(a);
    }
    else if (!strcmp(cmd, "shot")) {  // shot [сколько] [мс] — кадры игры в dev\shot_N.bmp
      g_shotIdx = 0;
      g_shotEvery = k >= 3 ? int(b) : 300;
      g_shotLeft = k >= 2 ? int(a) : 1;
    }
    else if (!strcmp(cmd, "state")) {
      const addr h = hugger;
      V3 p{};
      float y = 0;
      addr tg = 0;
      if (h) Safe([&] { p = rv3(h + 0x70), y = rf(h + 0x174), tg = TargetOf(h); });
      Log("dev state: hugger %08X state %02X%s pos %.1f %.1f %.1f yaw %.2f target %s leap %d strafe %d", unsigned(h),
          StateId(h), IsLeapState(HugStateOf(h)) ? " LEAP" : "", p.x, p.y, p.z, y, NameOr(tg),
          int(s_leapPhase.load()), int(fmode != 0));
      for (addr e : ActiveChars()) {
        V3 q{};
        Safe([&] { q = rv3(e + 0x70); });
        uint32_t ty = 0;
        Safe([&] { ty = rd<uint32_t>(rp(e + 0x64) + 4); });
        Log("  %08X %-24s type %u death %u state %02X dist %.1f", unsigned(e), CharName(e).c_str(), ty, DeathState(e),
            StateId(e), std::hypot(q.x - p.x, q.z - p.z));
      }
    }
  }
}

// ---------------- тик ----------------
static int s_release = 0, s_atkRetries = 0;
static ms_t s_validAt = 0, s_holdAt = 0, s_atkFrom = 0, s_atkUntil = 0, s_nextUntil = 0, s_playerAt = 0;
static V3 s_holdPos{};
static bool s_autoPath = false;  // «само»: прямое движение не сработало — перешли на навмеш
static unsigned s_tick = 0;

// ИИ остальных лицехватов (раз в 100 мс): ручное — стоят; «не на Аманду» — кто нацелился на неё, стоит и ищет другую
// цель (импульс «следующая цель»), остальные живут сами; полный — свой ИИ
static void OthersTick(ms_t t) {  // лицехваты — по hugAi, андроиды — по androidAi
  int n = 0;
  for (addr e : ActiveChars()) {
    if (n >= kSeen) break;
    if (e == hugger || !IsPlayableNpc(e)) continue;
    const int mode = IsHugger(e) ? HugAiNow() : AndroidAiNow();
    uint32_t want = 0;
    if (PupIsBody(e)) {  // изображает другого игрока (puppet.cpp): стоит, его ведёт сеть
      want = 1u << HG_IDLE;
    } else if (mode == 0) {
      want = 1u << HG_IDLE;
    } else if (mode == 3 && player) {
      addr T = 0;
      Safe([&] { T = TargetOf(e); });
      if (T == player) want = 1u << HG_IDLE | ((t / 300) % 2 == 0 ? 1u << HG_NEXT : 0);
    }
    s_other[n].ch.store(0);  // хук не должен увидеть лицехвата с чужими битами
    s_other[n].want.store(want);
    s_other[n].ch.store(e);
    n++;
  }
  for (; n < kSeen; n++) s_other[n].ch.store(0);
}

static void HuggerTick() {  // 16 мс, под общим замком
  const ms_t t = NowMs();
  s_tick++;
  static ms_t othersAt = 0;
  if (t - othersAt >= 100) othersAt = t, OthersTick(t);
  if ((!player && t - s_playerAt > 500) || t - s_playerAt > 5000) s_playerAt = t, FindPlayer();
  // H — за следующего лицехвата (Esc / B / «Вернуться к Аманде» — назад к ней)
  static bool hWas = false, bWas = false, lWas = false, rWas = false;
  const bool h = !photo.on && HotK(K_HUGGER), hPress = h && !hWas;
  hWas = h;
  if (hPress) {
    if (Down(VK_CONTROL)) ActSpawnHugger();  // Ctrl+H — ещё один лицехват туда, куда смотрит камера
    else ActHuggerNext();
  }

  if (hugger != s_ctl) {
    s_ctl = hugger;
    s_ctlAt = t;
    s_release = 0;
    s_holdAt = 0;
    s_autoPath = false;
    s_atkFrom = s_atkUntil = s_nextUntil = 0;
    hugNoBranch = false;
  }
  if (!hugger) {
    hugPathNow = false;
    return;
  }
  if (t - s_validAt > 300) {  // жив ли ещё (убили, выгрузили)
    s_validAt = t;
    const std::vector<addr> act = ActiveChars();
    if (std::find(act.begin(), act.end(), hugger) == act.end() || !IsPlayableNpc(hugger) || DeathState(hugger) >= 2) {
      Log("facehugger %08X is gone (or dead)", unsigned(hugger));
      ActHugger(0);
      return;
    }
  }
  // камеру вернули Аманде (B, Esc, меню) — больше не играем за него
  if (camMode == 0 && camWant == 0 && t - s_ctlAt > 800) {
    ActHugger(0);
    return;
  }
  // уровень без Чужого: колесо камеры, B и запасной CamDo — здесь (иначе их делает MainTick Чужого)
  if (!gate) {
    CamOffsetKeys();
    if (camMode == 2 && s_tick % 2 == 0) ApplyFollow();
    if (camWant && t - camKeyAt > 300) CamDo();
    const bool b = !photo.on && HotK(K_SWAP), bPress = b && !bWas;
    bWas = b;
    if (bPress && t - camKeyAt > 500 && !MpAvatarLocked()) camWant = 3, camKeyAt = t;
  }
  const bool seen = SeenRecently(hugger);
  if (!seen && t - s_ctlAt > 2000 && !hugNoBranch) {
    hugNoBranch = true;
    Log("facehugger %08X does not check the mod branch — run AlienModSetup.exe -> Install and reload",
        unsigned(hugger));
  } else if (seen) {
    hugNoBranch = false;
  }
  s_want[HG_IDLE] = true;  // без клавиш — стоит (ветка мода главнее его ИИ)

  // ---- ввод: WASD относительно камеры (её курс — взгляд Аманды), Пробел — во весь опор, Shift — быстро, Ctrl — тихо ----
  const bool cam = camMode != 0 && !photo.on;
  const PhotoDrive pd = PhotoDriveFor(PA_NPC);  // фоторежим: стрелки у него — относительно взгляда фрикама
  const bool u = pd.on ? pd.dy > 0 : cam && Down('W'), dn = pd.on ? pd.dy < 0 : cam && Down('S');
  const bool l = pd.on ? pd.dx < 0 : cam && Down('A'), rt = pd.on ? pd.dx > 0 : cam && Down('D');
  const int dx = (rt ? 1 : 0) - (l ? 1 : 0), dy = (u ? 1 : 0) - (dn ? 1 : 0);
  float camYaw = 0, hy = 0;
  V3 pos{};
  if (player) Safe([&] { camYaw = rf(player + 0x174); });
  if (pd.on) camYaw = pd.yaw;
  if (!Safe([&] { hy = rf(hugger + 0x174), pos = rv3(hugger + 0x70); })) return;
  const bool keys = dx != 0 || dy != 0;
  bool any = keys || t < s_devMoveUntil;
  float moveYaw = keys ? WrapPi(camYaw + std::atan2(float(dx), float(dy))) : any ? WrapPi(camYaw + s_devMoveRel) : hy;
  int speed = pd.on ? pd.speed : Down(VK_SPACE) ? 3 : Down(VK_SHIFT) ? 2 : Down(VK_CONTROL) ? 0 : keys ? 1 : s_devSpeed;
  hugPathNow = hugMoveMode == 2 || (hugMoveMode == 0 && s_autoPath);
  static ms_t strafeKeyAt = 0;
  if (!photo.on && t - strafeKeyAt > 400 && HotK(K_STRAFE)) strafeKeyAt = t, ActToggleStrafe();  // R — стрейф, как у Чужого

  // ---- ЛКМ — прыжок, ПКМ — следующая цель. Цель выбирает мод: любой живой персонаж рядом — люди, андроиды, Чужой
  // (Аманда — только если разрешено, это её смерть). Андроид на время прыжка — «гражданский» (анимации человека);
  // жертва не умерла за 3 с — смертельный удар от лицехвата (как игра добивает жертву) ----
  const bool lmb = cam && Down(VK_LBUTTON), rmb = cam && Down(VK_RBUTTON);
  const bool lmbHit = (lmb && !lWas) || s_devLeap || PhotoTakeAttack(PA_NPC), rmbHit = (rmb && !rWas) || s_devRmb;
  s_devLeap = s_devRmb = false;
  if (rmbHit) {
    s_victim = NextVictim(pos, s_victim);
    if (s_victim) {
      HugSetTarget(hugger, s_victim);
      Log("facehugger target: %s", CharName(s_victim).c_str());
    } else {
      hugMsgAt = t;
    }
  }
  const int ph = s_leapPhase;
  if (lmbHit && ph != LP_WANT && ph != LP_ACTIVE) {
    const addr v = VictimOk(s_victim, pos, 16.f) ? s_victim : NearestVictim(pos, hy);
    if (!v) {
      hugMsgAt = t;
      Log(s_ctlAndroid ? "android: nobody to attack" : "facehugger: nobody to leap at");
    } else {
      V3 q{};
      Safe([&] { q = rv3(v + 0x70); });
      if (std::hypot(q.x - pos.x, q.z - pos.z) <= (s_ctlAndroid ? kReachAndroid : kReach)) {
        if (s_ctlAndroid) StartMelee(v, t);
        else StartLeap(v, t);
      } else {  // далеко — сначала подбежать
        s_victim = s_approach = v;
        s_approachUntil = t + 7000;
        s_approachKeys = keys;
        HugSetTarget(hugger, v);
        Log("facehugger: running at %s", CharName(v).c_str());
      }
    }
  }
  lWas = lmb, rWas = rmb;
  if (s_approach) {  // подбегаем: бегом прямо к жертве; WASD — отмена
    V3 q{};
    const bool ok = VictimOk(s_approach, pos, 20.f) && Safe([&] { q = rv3(s_approach + 0x70); });
    const float d = ok ? std::hypot(q.x - pos.x, q.z - pos.z) : 99.f;
    if (!keys) s_approachKeys = false;  // клавиши, зажатые при клике, не отменяют — только новое нажатие
    if (!ok || (keys && !s_approachKeys) || t > s_approachUntil) {
      Log("facehugger: run cancelled (%s)", !ok ? "target lost" : keys ? "WASD" : "too long");
      s_approach = 0;
    } else if (d <= (s_ctlAndroid ? kReachAndroid : kReach)) {
      if (s_ctlAndroid) StartMelee(s_approach, t);
      else StartLeap(s_approach, t);
      any = false;
    } else {
      any = true;
      moveYaw = std::atan2(q.x - pos.x, q.z - pos.z);
      speed = 3;
    }
  }
  if (!s_ctlAndroid) LeapTick(t);
  else AndroidVoiceTick(t);
  if (s_pinOn && hugger == s_leapOwner && keys) s_pinOn = false, Log("facehugger: crawls off the face");  // WASD — слезть
  const bool busy = !s_ctlAndroid && (s_leapPhase == LP_WANT || s_leapPhase == LP_ACTIVE || ((s_attached || s_pinOn) && hugger == s_leapOwner));
  if (s_ctlAndroid && t < s_meleeUntil) {  // андроид бьёт — стоит, цель держим
    any = false;
    if (s_hugForce) HugSetTarget(hugger, s_hugForce.load());
  }
  if ((s_leapPhase == LP_WANT || s_leapPhase == LP_ACTIVE) && s_hugForce) HugSetTarget(hugger, s_hugForce.load());
  if (busy) any = false;  // в прыжке и на лице не ходим (Пробел — отцепиться)
  CamKillCam(s_devCam ? s_devCam : busy ? s_leapVictim.load() : 0);  // камера — на жертву: у лицехвата в «прилип» игра не рисует уровень
  // удар андроида начался — рубильник держим, пока жива его анимация (иначе дерево завершит узел и оборвёт убийство)
  if (s_ctlAndroid && s_meleeGo && s_meleeUntil) {
    if (AndroidAttackAlive(hugger)) {
      s_attackSeen = true;
      if (t < s_meleeGoAt.load() + 15000) s_meleeUntil = t + 200;
    } else if (s_attackSeen) {  // анимация удара кончилась — жертва жива? добиваем (мёртвую QueueKill не трогает)
      s_attackSeen = false;
      if (const addr v = s_hugForce.load()) QueueKill(v, hugger, 400);
    }
  }
  s_want[HG_ATTACK] = s_ctlAndroid ? t < s_meleeUntil : s_leapPhase == LP_WANT || s_leapPhase == LP_ACTIVE;
  if (s_ctlAndroid && s_meleeUntil && t >= s_meleeUntil) s_meleeUntil = 0, s_hugForce = 0, s_meleeOn = false, s_meleeGo = false;
  s_want[HG_NEXT] = false;  // смену цели делает мод (ПКМ)

  // ---- стрейф: тело — по камере, движение — по WASD (направление скорости пишет OnHugFaceLeave) ----
  const bool strafe = fmode != 0 && !s_approach && !hugPathNow && !pd.on;  // в кадре — тело по ходу движения
  s_hugStrafe = strafe && player;
  s_hugMoving = any;
  s_moveYawA = moveYaw;
  s_faceYawA = camYaw;
  const float bodyYaw = strafe && player && any ? camYaw : moveYaw;
  s_anyInput = any;
  s_moveYaw = moveYaw;
  s_speed = speed;

  // ---- движение: узел ActionMoveInDirection (как у Чужого; узел общий — параметры пишем, пока он наш) или «по навмешу» ----
  for (int i = 0; i < s_moveN; i++)
    if (s_move[i]) Safe([&] {
        wr<uint32_t>(s_move[i] + 8, uint32_t(s_speed));
        wf(s_move[i] + 0xc, WrapPi(bodyYaw - hy));
      });
  const addr r = s_reqOwner == hugger ? s_req : 0;
  if (any) {
    s_release = 0;
    s_want[HG_PATH] = hugPathNow;
    s_want[HG_ARROW] = !hugPathNow;
    if (r) Safe([&] {
        wf(r + 0x1c, bodyYaw);
        wf(r + 0x24, 10.f);
        wr<uint8_t>(r + 0x34, 1);
      });
    // «само»: держат WASD полторы секунды, а лицехват ни с места и запроса движения нет — прямое движение ему не
    // подходит, переходим на навмеш
    if (hugMoveMode == 0 && !s_autoPath) {
      if (!s_holdAt) {
        s_holdAt = t, s_holdPos = pos;
      } else if (t - s_holdAt > 1500) {
        if (std::hypot(pos.x - s_holdPos.x, pos.z - s_holdPos.z) < 0.3f && !r) {
          s_autoPath = true;
          Log("facehugger: direct movement did not move it — switching to navmesh movement");
        }
        s_holdAt = t, s_holdPos = pos;
      }
    }
  } else {
    s_holdAt = 0;
    if (r) Safe([&] {
        if (rf(r + 0x24) > 0.3f) wf(r + 0x24, 0.3f), wr<uint8_t>(r + 0x34, 1);
      });
    if (++s_release > 25) s_want[HG_ARROW] = s_want[HG_PATH] = false;  // ~0.4 с после отпускания — стоит
  }
}

void InstallHugger() {
  using Interceptor::Attach;
  Attach(VA(va::GateTest), nullptr, OnGateTestLeave, false);  // горячий: без замка
  Attach(VA(va::MeleeStart), OnHugMeleeEnter, OnHugMeleeLeave);
  Attach(VA(va::HugLeapCheck), nullptr, OnLeapCheckLeave);
  Attach(VA(va::HugPairOk), nullptr, OnPairOkLeave);
  Attach(VA(va::AndroidChooseAttack), nullptr, OnAndroidChooseLeave);
  Attach(VA(va::HugLeapEnd), OnLeapEndEnter, nullptr);
  Attach(VA(va::HugLeapUpdate), OnLeapUpdateEnter, nullptr, false);
  Attach(VA(va::HugLeapDtor), OnLeapDtorEnter, nullptr);
  Attach(VA(va::FhDeathCmd), OnFhDeathEnter, nullptr);
  Attach(VA(va::CharByHandleA), nullptr, OnCharByHandleLeave, false);
  Attach(VA(va::CharByHandleB), nullptr, OnCharByHandleLeave, false);
  Attach(VA(va::AttachFrameSet), OnAttachFrameEnter, nullptr, false);
  Attach(VA(va::LocoUpdate), nullptr, OnPinTick, false);
  Attach(VA(va::LocoUpdate), nullptr, OnNpcVoiceGameTick);
  Attach(VA(va::LocoReqStart), nullptr, OnHugFaceLeave, false);
  Attach(VA(va::LocoReqUpdate), nullptr, OnHugFaceLeave, false);
  Attach(VA(va::LocoUpdate), nullptr, OnHugFaceLeave, false);
  Every(16, "hugger", HuggerTick);
  Every(100, "hugger-dev", DevCmdTick);
}
