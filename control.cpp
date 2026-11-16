// control.cpp — управление Чужим: узлы дерева (рубильники и MoveInDirection), стрелки/WASD, стрейф, режим пути,
// валидация указателей и сброс при смене уровня.
// Источник: alien.js (основа) + hot.js §1 (настоящий игрок), §2 (кэш ввода), §5 (несколько деревьев), destroyGuard, lockCtl, lockLoco.
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include "mp.h"
#include "motion_math.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
const std::wstring& ModBase();
#include <sstream>
#include <string>

static const char* const kGateText[G_COUNT] = {
  "Alien Arrow control", "Alien Go Backstage", "Alien Come Frontstage", "Alien Stay Backstage", "Alien Path control",
  "Alien Attack",        "Alien Next Target",  "Alien Arrow idle",      "Alien Gamepad control" };

bool Alive(addr p, addr vtVa) {
  addr v = 0;
  return p && SafeRead(p, v) && v == VA(vtVa);
}
// рубильник проверяем по vtable прямо перед записью (docs/HOOKING.md, «Стабильность»)
void SetGate(addr p, bool on) {
  if (Alive(p, va::VT_GATE)) Safe([&] { wr<uint8_t>(p + 0x14, on ? 1 : 0); });
}
addr TargetOf(addr n) { return rp(rp(rp(n + 0x34a8) + 0x10) + 0x42c); }
// поставить цель: старт атаки (ActionMeleeAttack, 0x717d90) сверяет +0x430 с номером цели *(цель+0x50) — без него
// атака сразу проваливается (ни анимации, ни звука)
void SetTargetOf(addr n, addr t) {
  const addr lg = rp(rp(n + 0x34a8) + 0x10);
  if (!lg || !t) return;
  wr<uint32_t>(lg + 0x430, rd<uint32_t>(t + 0x50));
  wr<addr>(lg + 0x42c, t);
}

static void FindAlienReset();
static void ForgetVentCrawl();
static void ResetTurnState();

void HardReset(const char* why) {
  MpLevelReset();          // мультиплеер: ID экземпляров и TriggerInfo — у каждого уровня свои
  AlienExtraReset(false);  // уровень меняется: Аманду не двигаем, просто забываем
  CameraWorldReset();      // мир Havok у нового уровня свой (camera.cpp выберет тот, где под Амандой пол)
  gate = moveNode = npc = req = loco = player = 0;
  ForgetVentCrawl();
  InspectionReset();
  ResetTurnState();
  camMode = 0;
  HuggerReset();  // после camMode = 0: камеру уходящего уровня не трогаем
  camSaved = 0;
  AutoDuctReset();
  pathGate = pathNode = 0;
  vent.goUntil = vent.comeUntil = 0;
  vent.stay = false;
  alienBackstage = false;
  bsViewValid = false;
  ExitReset();
  PhotoReset();
  bsTrail.clear();
  vent.go = vent.come = vent.stayG = 0;
  amandaRB = charObj = 0;
  ductOn = false;
  attack.gate = attack.nextGate = 0;
  attack.until = attack.startAt = 0;
  for (auto& v : multi) v.clear();
  multiMoves.clear();
  locked = false;
  lockLocoNpc = 0;
  SpawnReset();
  FindAlienReset();
  Log("hard reset: %s", why);
}

// Чужой уничтожен / перезагрузка после смерти: сбрасываем только то, что относится к самому Чужому.
// Деревья (рубильники, узлы движения) НЕ трогаем: при перезагрузке игра сначала создаёт новые деревья (их уже поймала
// фабрика), а потом уничтожает старого Чужого — прежний полный сброс стирал и новые рубильники, и мод замолкал.
// Мёртвые узлы убирает OnTreeDestroy поштучно, нового Чужого находит FindAlienTick.
void AlienReset(const char* why) {
  InspectionReset();
  // играешь за лицехвата — камера и Аманда его, их не трогаем
  if (!hugger) AlienExtraReset(true);  // до обнуления player: спрятанную Аманду вернуть домой
  npc = req = loco = player = 0;
  ForgetVentCrawl();
  ResetTurnState();
  lockLocoNpc = 0;
  if (!hugger) {
    camMode = 0;
    camSaved = 0;  // объекты камер могли пересоздаться — старую не активируем
  }
  AutoDuctReset();
  vent.goUntil = vent.comeUntil = vent.comeAt = 0;
  vent.stay = false;
  alienBackstage = false;
  bsViewValid = false;
  ExitReset();
  PhotoReset();
  bsTrail.clear();
  amandaRB = charObj = 0;
  ductOn = false;
  attack.until = attack.startAt = 0;
  FindAlienReset();
  Log("alien reset: %s (trees kept: gate %08X move %08X)", why, unsigned(gate), unsigned(moveNode));
}

// основные указатели — на последний живой экземпляр из списков (после того как какие-то узлы умерли)
static addr LastAlive(const std::vector<addr>& v, addr vtVa) {
  for (auto it = v.rbegin(); it != v.rend(); ++it)
    if (Alive(*it, vtVa)) return *it;
  return 0;
}
static void RepickPrimaries() {
  gate = LastAlive(multi[G_ARROW], va::VT_GATE);
  vent.go = LastAlive(multi[G_GO], va::VT_GATE);
  vent.come = LastAlive(multi[G_COME], va::VT_GATE);
  vent.stayG = LastAlive(multi[G_STAY], va::VT_GATE);
  pathGate = LastAlive(multi[G_PATH], va::VT_GATE);
  attack.gate = LastAlive(multi[G_ATTACK], va::VT_GATE);
  attack.nextGate = LastAlive(multi[G_NEXT], va::VT_GATE);
  moveNode = LastAlive(multiMoves, va::VT_MOVE);
}

// ---------------- отцепление мода: отдать узлы следующей DLL и вернуть рубильники игре (reload.cpp) ----------------
void ControlExport(std::vector<NodeRec>& out) {
  for (int k = 0; k < G_COUNT; k++)
    for (addr p : multi[k])
      if (Alive(p, va::VT_GATE)) {
        auto it = aiInit.find(p);
        out.push_back({ k, p, it != aiInit.end() ? int(it->second) : 0 });
      }
  for (addr m : multiMoves)
    if (Alive(m, va::VT_MOVE)) out.push_back({ NR_ALIEN_MOVE, m, 0 });
}
void ControlAdopt(const NodeRec& r) {
  if (r.kind >= 0 && r.kind < G_COUNT && Alive(r.p, va::VT_GATE)) {
    multi[r.kind].push_back(r.p);
    aiInit[r.p] = uint8_t(r.init);
  } else if (r.kind == NR_ALIEN_MOVE && Alive(r.p, va::VT_MOVE)) {
    multiMoves.push_back(r.p);
  }
}
static ms_t s_treesAt = 0;
void ControlAdoptDone() {
  RepickPrimaries();
  s_treesAt = NowMs();
  if (gate) Log("reload: alien tree adopted (gate %08X move %08X)", unsigned(gate), unsigned(moveNode));
}
void ControlShutdown() {  // все рубильники Чужого — в исходные значения (как без мода: игра их не трогает)
  for (auto& v : multi)
    for (addr p : v) {
      auto it = aiInit.find(p);
      SetGate(p, it != aiInit.end() && it->second);
    }
  ActVentCrawl(false);
  ResetTurnState();
  AutoDuctReset();
  vent.goUntil = vent.comeUntil = 0;
  vent.stay = false;
  attack.until = attack.startAt = 0;
}

// ---------------- фабрика рубильников ----------------
// attrs(xml): xml+4 = число атрибутов (u8), xml+8+i*8 = { char* имя, char* значение }
static void ReadMenuText(addr xml, char* out, size_t cap) {
  out[0] = 0;
  uint8_t n = 0;
  if (!SafeRead(xml + 4, n)) return;
  for (unsigned i = 0; i < n; i++)
    Safe([&] {
      addr p = xml + 8 + i * 8;
      if (strcmp(reinterpret_cast<const char*>(rp(p)), "In_Game_Menu_Text") != 0) return;
      const char* v = reinterpret_cast<const char*>(rp(p + 4));
      size_t j = 0;
      for (; j + 1 < cap && v[j]; j++) out[j] = v[j];
      out[j] = 0;
    });
}
static int GateKindOf(const char* t) {
  for (int k = 0; k < G_COUNT; k++)
    if (strcmp(t, kGateText[k]) == 0) return k;
  return -1;
}

static void OnFactoryEnter(Invocation& inv) { ReadMenuText(inv.arg[0], inv.text, sizeof inv.text); }
static void OnFactoryLeave(Invocation& inv) {
  addr p = inv.retval();
  if (!p) return;
  Safe([&] { aiInit[p] = rd<uint8_t>(p + 0x14); });  // исходное значение — для выхода из режима ИИ
  const char* t = inv.text;
  HugForget(p);  // на месте старого узла (прошлый уровень) — новый: старое дерево лицехвата мертво
  if (strncmp(t, "Hugger ", 7) == 0 || strncmp(t, "Android ", 8) == 0) return HugOnGate(t, p);  // лицехват / андроид
  if (strncmp(t, "Alien ", 6) != 0) return;
  ms_t now = NowMs();
  if (now - s_treesAt > 5000) {  // первый наш узел за 5 с — новый уровень
    for (auto& v : multi) v.clear();
    multiMoves.clear();
    Log("new level: trees");
  }
  s_treesAt = now;
  int k = GateKindOf(t);
  if (k < 0 || locked) return;  // после первого спавна деревья клонов не трогаем
  multi[k].push_back(p);
  switch (k) {
    case G_ARROW: gate = p; Log("gate %08X", unsigned(p)); break;
    case G_GO: vent.go = p; break;
    case G_COME: vent.come = p; break;
    case G_STAY: vent.stayG = p; break;
    case G_PATH: pathGate = p; break;
    case G_ATTACK: attack.gate = p; break;
    case G_NEXT: attack.nextGate = p; break;
    default: break;
  }
}

// ---------------- узел ActionMoveInDirection с distance = 123 (так он помечен в BML) ----------------
static void OnMoveParamsLeave(Invocation& inv) {
  addr n = inv.ecx;
  HugForget(n);
  if (std::fabs(rf(n + 0x10) - 124.f) < 0.01f || std::fabs(rf(n + 0x10) - 125.f) < 0.01f)
    return HugOnMoveNode(n);  // узел лицехвата (124) или андроида (125) — hugger.cpp
  if (std::fabs(rf(n + 0x10) - 123.f) >= 0.01f || locked) return;
  moveNode = n;
  multiMoves.push_back(n);
  Log("move node %08X", unsigned(n));
}
static bool s_hugStart = false;  // идёт Start узла движения лицехвата (поток игры)
static void OnMoveStartEnter(Invocation& inv) {
  if (HugOnMoveStart(inv.ecx, inv.arg[0])) {
    s_hugStart = true;
    return;
  }
  if (!moveNode || inv.ecx != moveNode) return;
  if (!locked) npc = inv.arg[0];
  inStart = true;
}
static void OnMoveStartLeave(Invocation&) {
  if (s_hugStart) {
    s_hugStart = false;
    return HugOnMoveStartLeave();
  }
  if (!inStart) return;
  inStart = false;
  int type = -1;
  float yaw = 0;
  if (req) Safe([&] { type = int(rd<uint32_t>(req + 0x10)); yaw = rf(req + 0x1c); });
  Log("START npc %08X req %08X type %d yaw %.2f", unsigned(npc), unsigned(req), type, yaw);
}
static void OnSmartPtrCopyEnter(Invocation& inv) {  // горячая функция: без замка, только флаг
  if (s_hugStart) {
    const addr r = inv.arg[0];
    if (r && rd<uint32_t>(r + 0x10) == 9) HugOnReq(r);
    return;
  }
  if (!inStart) return;
  addr r = inv.arg[0];
  if (r && rd<uint32_t>(r + 0x10) == 9) req = r;
}
// Shutdown узла движения — thiscall(узел, персонаж, состояние), RET 8. Если персонаж уже разрушается (деспавн, выход из
// уровня, пока им управляешь), игра успевает освободить состояние узла раньше, чем завершает сам узел, и падает внутри
// (0x40f0f0 -> 0x6f8490 -> 0x471afb, чтение по «указателю» 0xBF800000 = -1.0f). Для узлов мода в таком случае
// завершение пропускаем: запрос движения просто не освобождается — это копейки при разрушении персонажа.
static bool OurMoveNode(addr n) {
  if (!n) return false;
  if (n == moveNode || HugIsMoveNode(n)) return true;
  return std::find(multiMoves.begin(), multiMoves.end(), n) != multiMoves.end();
}
static void OnMoveShutdownEnter(Invocation& inv) {
  const addr node = inv.ecx, ch = inv.arg[0];
  if (OurMoveNode(node)) {
    const std::vector<addr> act = ActiveChars();
    const bool gone = (ch && ch == dyingChar.load()) || std::find(act.begin(), act.end(), ch) == act.end();
    if (gone) {
      inv.skip = true;
      inv.skipPop = 8;
      inv.cpu->eax = 0;
      if (node == moveNode) req = 0;
      HugOnMoveShutdown(node, ch);
      Log("move node %08X shutdown skipped: its character %08X is being destroyed", unsigned(node), unsigned(ch));
      return;
    }
  }
  HugOnMoveShutdown(inv.ecx, inv.arg[0]);  // Shutdown(узел, персонаж, состояние)
  if (moveNode && inv.ecx == moveNode) {
    req = 0;
    Log("SHUTDOWN");
  }
}
// свежая запись узла движения: обнулить указатель на запрос (+0x34/+0x38), как делает сама игра у соседнего класса узлов.
// Иначе сброс дерева раньше Start (у андроида — при взятии под управление) завершает узел с мусором и игра падает
static void OnMoveRecLeave(Invocation& inv) {
  const addr r = inv.cpu->eax;
  if (!r) return;
  const uint32_t stale = rd<uint32_t>(r + 0x34);
  wr<uint32_t>(r + 0x34, 0);
  wr<uint32_t>(r + 0x38, 0);
  static std::atomic<unsigned> n{ 0 };
  if (stale && n.fetch_add(1) < 5) Log("move node record %08X: stale request pointer %08X cleared", unsigned(r), stale);
}
static void OnLocoGetLeave(Invocation& inv) {
  addr ra = ToVA(inv.ra);
  if (ra < va::LocoGetFrom || ra >= va::LocoGetTo) return;
  addr L = rp(inv.retval());
  if (!L || L == loco) return;
  // только объект самого Чужого (он же *(Чужой+0x42a8)): LocoGet зовут для всех персонажей, и раньше мод мог запомнить
  // объект ходьбы андроида или лицехвата — стрейф Чужого тогда писал направление им (тестер: стрейф не работает)
  if (!npc || L != rp(npc + va::CharLocoOff)) return;
  loco = L;
  Log("loco %08X", unsigned(L));
}

// ---------------- ввод ----------------
Input ReadInput() {
  Input r{};
  const bool cam = camMode != 0;
  bool u = Down(cam ? 'W' : VK_UP), dn = Down(cam ? 'S' : VK_DOWN);
  bool l = Down(cam ? 'A' : VK_LEFT), rt = Down(cam ? 'D' : VK_RIGHT);
  if (menuOpen && !cam) u = dn = false;  // ↑/↓ заняты меню F1
  const PhotoDrive pd = PhotoDriveFor(PA_ALIEN);  // фоторежим: стрелки у Чужого — относительно взгляда фрикама
  if (photo.on) u = pd.dy > 0, dn = pd.dy < 0, l = pd.dx < 0, rt = pd.dx > 0;
  bool shift = Down(VK_SHIFT), ctrl = Down(VK_CONTROL), space = Down(VK_SPACE);
  int dx = (rt ? 1 : 0) - (l ? 1 : 0), dy = (u ? 1 : 0) - (dn ? 1 : 0);
  r.rawAny = dx != 0 || dy != 0;
  bool busy = VentBusy() || autoDuctPath;  // ветки вентиляции/пути: стрелочная ветка и лицо не работают
  r.any = !busy && r.rawAny;
  r.camYaw = ViewYaw();  // курс камеры: свободный вид из головы — свой, иначе курс Аманды (её невидимый взгляд)
  if (pd.on) r.camYaw = pd.yaw;
  r.worldYaw = r.rawAny ? WrapPi(r.camYaw + std::atan2(float(dx), float(dy))) : 0.f;
  r.speed = pd.on ? pd.speed : space ? 3 : shift ? 2 : ctrl ? 0 : 1;  // Пробел — бег, Shift — быстро, Ctrl — тихо
  hasFace = player != 0 && !pd.on;  // в кадре — без стрейфа: тело по ходу движения
  faceYaw = r.camYaw;
  moveYaw = r.worldYaw;
  moving = r.any;
  // ИИ ведёт сам / фоторежим / Чужой осматривается / играешь за лицехвата — Чужому движение не запрашиваем
  if (aiMode != 0 || (photo.on && !pd.on) || alienLooking || InspectionActive() || (hugger && !pd.on)) {
    r.any = r.rawAny = false;
    moving = false;
  }
  lastInput = r;
  return r;
}

// ---------------- стрейф: включён игроком (fmode) и Чужой не в вентиляции ----------------
// В вентиляции (уход за кулисы, за кулисами, выход) и в шахте тело должно идти по ходу движения, а не по камере:
// стрейф там сам встаёт на паузу и сам возвращается, когда Чужой снова в мире.
const char* StrafePause() {
  if (VentBusy() || vent.stay || alienBackstage) return TR("вентиляция", "vents");
  if (ductOn) return TR("шахта", "duct");
  return nullptr;
}
bool StrafeActive() { return fmode != 0 && !StrafePause(); }

// ---------------- развороты на месте ----------------
// Стоит, а камера смотрит в сторону — Чужой доворачивается на месте анимацией IDLE_TURN_L/R_90/180 (основное действие
// AT_NPCLocomotionStandingTurn = 57; текущее основное действие — npc+0x36c -> +0x1a). Начинаем разворот так же, как сама
// игра из состояния «стоит» (FUN_00451040, когда поворачивается к цели): в объект ходьбы — режим 4 и курс цели, потом
// FUN_00657860 пересобирает текущее действие в разворот. Зовём на выходе интегратора движения (0x44d4f0) — там ни один
// метод действия не выполняется. Анимация сама сходится к нужному углу (ConvergeOrientation).
// Разворот закончился — у действия поднят бит +8 & 4 (так его проверяет FUN_00450940), но в «стоит» он сам не
// возвращается (нас не ведёт система запросов локомоции): переводим его в «стоит» сами (FUN_0065bdd0) или сразу
// начинаем следующий разворот, если камеру крутили дальше.
// Режим объекта ходьбы L+8 = 4 («разворот») игра сбрасывает в 0 сама, в обработчике «стоит» FUN_00451040, — у Чужого
// под нашим управлением он не вызывается, и после разворота режим 4 оставался: нажал W — интегратор всё ещё
// «разворачивает», Чужой стоит на месте (застревал). Сбрасываем сами — когда разворот закончен или прерван.
static bool s_turnOn = false;
static std::atomic<bool> s_locoTurnMode{ false };  // мы ставили L+8 = 4 — вернуть 0, когда разворот кончится
static std::atomic<int> s_gameReq{ 0 };  // поток игры (OnTurnGameTick): 1 — начать разворот, 2 — перевести в «стоит»
static float s_turnYaw = 0, s_turnFrom = 0;
static ms_t s_turnAt = 0, s_turnEndAt = 0;
static int s_turnPrim = -1, s_turnTries = 0;
static int PrimaryAct(bool* done = nullptr) {  // тип основного действия; done — оно закончилось (бит +8 & 4)
  int r = -1;
  if (npc) Safe([&] {
      if (const addr a = rp(npc + 0x36c)) {
        r = rd<uint8_t>(a + 0x1a);
        if (done) *done = (rd<uint8_t>(a + 8) & 4) != 0;
      }
    });
  return r;
}
static bool s_quickTurn = false;  // идёт резкий разворот на 180 (клавиша)
static ms_t s_quickReq = 0;        // нажали — ждём, пока он сможет начать (до 1.5 с)
static ms_t s_moveEndAt = 0;       // когда отпустили движение: 0.4 с после этого разворот не начинаем — игра ещё
                                   // дообрабатывает запрос остановки и тут же возвращает разворот в «стоит» (57 -> 1, 0°)
static void TurnEnd(const char* why) {
  if (turnFollowBody) CameraBodyTurnEnd();
  turnFollowBody = false;
  s_quickTurn = false;
  if (!s_turnOn) return;
  float y = 0;
  if (npc) Safe([&] { y = rf(npc + 0x174); });
  Log("turn: end (%s), body turned %.0f deg", why, WrapPi(y - s_turnFrom) * 57.3f);
  s_turnOn = false;
  g_fastTurnActor = 0;
  s_turnEndAt = NowMs();
}
static bool TurnWanted() { return !ventCrawl.load() && (camMode == 1 ? turnInPlaceFp : camMode == 2 ? turnInPlace3p : false); }
static void TurnTick(const Input& in) {
  const ms_t t = NowMs();
  const bool available = npc && loco && !photo.on && !hugger && aiMode == 0 && !VentBusy() && !vent.stay && !alienBackstage &&
                    !autoDuctPath && !alienLooking && !InspectionActive() && attack.until <= t;
  const bool crouch = ventCrawl.load() || ductOn;
  const bool base = available && (!crouch || (camMode == 2 && (s_quickReq || s_quickTurn)));
  // резкий разворот на 180 (клавиша): стоит — анимацией, на ходу — камера сразу назад, тело развернётся по ходу
  if (camMode != 0 && g_hotkeysOn && !g_rebinding && KeyHit(K_TURN180) && available) {
    if (crouch && camMode == 2 && !in.rawAny && !s_quickTurn) {
      s_quickReq = t;  // native action uses the active crouched/vent animation context
      return;
    }
    if (!base) return;
    if (in.rawAny) {
      if (camMode == 1) fpFlipReq = true;
      else if (player) Safe([] { wf(player + 0x174, WrapPi(rf(player + 0x174) + kPi)); });
      Log("turn 180: on the move — view flipped");
    } else {
      s_quickReq = t;
    }
  }
  if (s_quickReq && t - s_quickReq > 1500) s_quickReq = 0;
  {
    static bool wasMoving = false;
    if (wasMoving && !in.rawAny) s_moveEndAt = t;
    wasMoving = in.rawAny;
  }
  const bool settled = t - s_moveEndAt > 400;
  const bool can = (TurnWanted() || s_quickTurn || s_quickReq) && base && !in.rawAny;
  bool done = false;
  const int prim = PrimaryAct(&done);
  const bool turnOver = prim == 57 && done;  // разворот доигран, действие висит в последнем кадре
  // доигранная остановка (WALK/CHARGE_TO_IDLE, 59): без «удобного Чужого» он так и стоит в её последнем кадре — в «стоит»
  // его никто не переводит, поэтому разворот можно начать и отсюда (OnTurnGameTick сначала переведёт в «стоит»)
  const bool stopOver = prim == 59 && done;
  if (!can) {
    if (s_turnOn && s_gameReq.load() == 1) s_gameReq = 0;
    // не держать позу разворота; нажал движение посреди разворота — оборвать его сразу, иначе ходьба не начнётся
    if ((turnOver && !in.rawAny) || (prim == 57 && in.rawAny)) s_gameReq = 2;
    return TurnEnd("not now");
  }
  float body = 0;
  if (!Safe([&] { body = rf(npc + 0x174); })) return;
  const float want = ViewYaw(), diff = WrapPi(want - body);
  const bool ready = prim == 1 || turnOver || stopOver;  // из «стоит», доигранного разворота или остановки
  if (!s_turnOn && s_quickReq && ready && settled && t - s_turnEndAt >= 150) {  // резкий 180: цель — ровно за спиной
    s_quickReq = 0;
    s_quickTurn = true;
    turnFollowBody = true;  // камера поворачивается вместе с телом
    CameraBodyTurnBegin();
    s_turnOn = true, s_turnYaw = WrapPi(body + kPi), s_turnFrom = body, s_turnAt = t, s_turnPrim = prim, s_turnTries = 0;
    s_gameReq = 1;
    Log("turn 180: start (from action %d)", prim);
    return;
  }
  if (!s_turnOn) {
    if (!TurnWanted()) return;
    // из головы — как у Аманды: тело за взглядом уже с 30°; за спиной — с 75° (иначе отворачивается, стоит повернуть камеру)
    const float th = camMode == 1 ? 0.52f : 1.3f;
    if (std::fabs(diff) < th || t - s_turnEndAt < 150 || !ready || !settled) {
      if (turnOver && s_gameReq.load() == 0) s_gameReq = 2;
      return;
    }
    s_turnOn = true, s_turnYaw = want, s_turnFrom = body, s_turnAt = t, s_turnPrim = prim, s_turnTries = 0;
    s_gameReq = 1;
    Log("turn: start %.0f deg (from action %d)", diff * 57.3f, prim);
    return;
  }
  if (prim != s_turnPrim) Log("turn: primary action %d -> %d", s_turnPrim, prim), s_turnPrim = prim;
  if (s_gameReq.load() == 1) {  // ещё не начали (поток игры не дошёл)
    if (t - s_turnAt > 1000) s_gameReq = 0, TurnEnd("could not start");
    return;
  }
  if (prim == 57 && !done) {  // разворачивается
    if (t - s_turnAt > 4000) TurnEnd("timeout");
    return;
  }
  // доигран: камеру крутили дальше — следующий разворот, иначе — в «стоит»
  if (s_quickTurn) {  // резкий 180 доигран: камера шла вместе с телом — дальше как обычно (или ничего, если развороты выключены)
    const float turned = WrapPi(body - s_turnFrom);
    if (std::fabs(turned) < 0.5f && ready && ++s_turnTries < 3) {  // игра отменила разворот сразу — ещё раз
      s_turnYaw = WrapPi(s_turnFrom + kPi), s_turnAt = t;
      s_gameReq = 1;
      Log("turn 180: cancelled by the game (%.0f deg), again", turned * 57.3f);
      return;
    }
    s_quickTurn = false;
    CameraBodyTurnEnd();
    turnFollowBody = false;
    Log("turn 180: done, body turned %.0f deg", turned * 57.3f);
    if (!TurnWanted()) {
      if (turnOver) s_gameReq = 2;
      return TurnEnd("180 done");
    }
  }
  if (std::fabs(diff) > 0.35f && ready && ++s_turnTries < 4) {
    s_turnYaw = want, s_turnAt = t;
    s_gameReq = 1;
    Log("turn: again %.0f deg", diff * 57.3f);
    return;
  }
  if (turnOver) s_gameReq = 2;
  TurnEnd(std::fabs(diff) <= 0.35f ? "facing the camera" : "stopped");
}
// «удобный Чужой»: отпустил клавиши — анимацию остановки (WALK/CHARGE_TO_IDLE, действие 59) не доигрываем, а сразу
// переводим его в «стоит» (FUN_0065bdd0) — иначе остановка шагала дальше (с корневым движением) или замирала на полпути
static std::atomic<ms_t> s_snapIdleUntil{ 0 };
// Follow the native idle transition. In particular, do not clear the finished bit
// on arbitrary idle actions: the locomotion FSM needs it to advance to a new request.
static void ToIdle(addr act) { Fn<PrimToIdleFn>(va::PrimToIdle)(act); }
static void ResetTurnState() {
  g_fastTurnActor = 0;
  s_turnOn = false;
  s_locoTurnMode = false;
  s_gameReq = 0;
  s_quickTurn = false;
  s_quickReq = 0;
  s_moveEndAt = 0;
  s_turnAt = s_turnEndAt = 0;
  s_turnPrim = -1;
  s_turnTries = 0;
  s_snapIdleUntil = 0;
  turnFollowBody = false;
}
// 44d4f0 integrates one entry of a manager's MemoryPtr array: ECX is the
// manager and arg[0] the entry index. Other NPCs must not drive our transitions
// or advance the camera while their own locomotion is being updated.
static void OnTurnLocoEnter(Invocation& inv) {
  inv.text[0] = 0;
  if (!loco || !inv.ecx) return;
  const addr begin = rp(inv.ecx), end = rp(inv.ecx + 4);
  if (begin && end >= begin && inv.arg[0] < (end - begin) / 8 &&
      rp(begin + inv.arg[0] * 8) == loco) inv.text[0] = 1;
}
// поток игры: выход собственного интегратора движения — переходы основного действия
static void OnTurnGameTick(Invocation& inv) {
  if (!inv.text[0]) return;
  if (!npc || !loco) return;
  if (ForcedRetreatActive() || InspectionActive()) {
    // A queued turn/instant stop must not replace the retreat's native action.
    s_gameReq = 0;
    s_snapIdleUntil = 0;
    g_fastTurnActor = 0;
    return;
  }
  CameraBodyTurnTick();
  const int want = s_gameReq.exchange(0);
  const ms_t snapUntil = s_snapIdleUntil.load();
  if (!want && !snapUntil && !s_locoTurnMode.load() && !(alienSnappy && moving && req)) return;
  Safe([&] {
    addr act = rp(npc + 0x36c);
    if (!act) return;
    int prim = rd<uint8_t>(act + 0x1a);
    bool done = (rd<uint8_t>(act + 8) & 4) != 0;
    // Skip only our direct movement's native start, never duct navigation or AI.
    if (alienSnappy && moving && req && !VentBusy() && !autoDuctPath && !ductOn && !ventCrawl &&
        !alienLooking && aiMode == 0 && prim == 55) {
      Fn<void(__fastcall*)(addr)>(va::PrimToLocoCycle)(act);
      prim = 56;
      Log("snappy: native movement start skipped");
    }
    if (s_locoTurnMode.load() && prim == 57 && done) {
      ToIdle(act);
      prim = 1;
      Log("turn: native finished event accepted; returned to idle");
    }
    if (want == 1 && prim == 59 && done) {  // доигранная остановка — сначала в «стоит» (так разворот начинает и игра)
      ToIdle(act);
      act = rp(npc + 0x36c);
      if (!act) return;
      prim = rd<uint8_t>(act + 0x1a), done = (rd<uint8_t>(act + 8) & 4) != 0;
    }
    if (want == 1 && (prim == 1 || (prim == 57 && done))) {  // разворот — только из «стоит» / доигранного разворота
      const addr L = loco;
      wr<uint32_t>(L + 8, 4u);
      s_locoTurnMode = true;
      wf(L + 0x10, 0.f), wf(L + 0x14, 0.f), wf(L + 0x18, 0.f), wf(L + 0x20, 0.f);
      wr<uint32_t>(L + 0x24, rd<uint32_t>(VA(va::TurnConstA)));
      wr<uint32_t>(L + 0x28, rd<uint32_t>(VA(va::TurnConstB)));
      wf(L + 0x1c, s_turnYaw);
      Fn<PrimToStandingTurnFn>(va::PrimToStandingTurn)(act, 0, 1);
      g_fastTurnActor = npc;
      s_turnAt = NowMs();
      prim = 57;
    } else if (want == 2 && prim == 57) {
      ToIdle(act);  // разворот (доигранный или прерванный движением) — в «стоит»
      prim = 1;
    }
    // разворот кончился — режим ходьбы обратно в 0 (как FUN_00451040), иначе Чужой не может пойти
    if (s_locoTurnMode.load() && prim != 57 && rd<uint32_t>(loco + 8) == 4u) {
      g_fastTurnActor = 0;
      wr<uint32_t>(loco + 8, 0u);
      s_locoTurnMode = false;
      Log("turn: loco mode 4 -> 0 (action %d)", prim);
    } else if (s_locoTurnMode.load() && prim != 57) {
      g_fastTurnActor = 0;
      s_locoTurnMode = false;  // режим уже сменила сама игра (пошёл)
    }
    if (snapUntil) {
      if (NowMs() > snapUntil || prim == 1) {
        s_snapIdleUntil = 0;
      } else if (prim == 59 || ((prim == 55 || prim == 56) && !req)) {  // остановка / старт после конца узла
        ToIdle(act);
        wf(loco + 0x18, 0.f);
        s_snapIdleUntil = 0;
        Log("snappy: stop animation skipped (action %d -> idle)", prim);
      }
    }
  });
}

// лицо/стрейф: после апдейта локомоции пишем направление скорости L+0x1c (тело держит курс по камере)
static bool s_hasLastMove = false;
static float s_lastMoveYaw = 0;
// тело по камере: включён стрейф — или вид из головы (управление как у Аманды: тело всегда за взглядом)
static bool FaceByCamera() { return StrafeActive() || (camMode == 1 && !hugger && !photo.on && !StrafePause()); }
void ApplyFace() {
  if (!loco || s_turnOn) return;  // разворот на месте ведёт сама игра
  if (!hasFace || !FaceByCamera()) return;
  if (moving) {
    s_hasLastMove = true;
    s_lastMoveYaw = moveYaw;
    wf(loco + 0x1c, moveYaw);
  } else if (s_hasLastMove && (req || rf(loco + 0x18) > 0.15f)) {
    wf(loco + 0x1c, s_lastMoveYaw);  // тормозим в прежнем направлении
  } else {
    s_hasLastMove = false;
    // стоит: курс тела как есть — разворачивает только TurnTick (развороты на месте можно выключить); раньше здесь стоял
    // курс камеры, и стоящий Чужой сам доворачивался за камерой
    wf(loco + 0x1c, npc ? rf(npc + 0x174) : faceYaw);
  }
}
// «Удобный Чужой»: отпустил клавиши — стоит там, где отпустил (позиция и скорость Havok-тела — как телепорт), а анимацию
// остановки OnTurnGameTick сразу меняет на «стоит». После апдейта локомоции, в потоке игры; до нового нажатия или пока
// он не встал в «стоит», но не дольше 2 с
static std::atomic<bool> s_snapHold{ false };
static std::atomic<ms_t> s_snapUntil{ 0 };
static float s_snapX = 0, s_snapZ = 0;
static void SnapHoldTick() {
  if (!s_snapHold.load(std::memory_order_relaxed)) return;
  if (ForcedRetreatActive() || InspectionActive() || VentBusy() || autoDuctPath) {
    s_snapHold = false;
    return;
  }
  // держим, пока он не встал в «стоит» (действие 1) и ещё 0.15 с после — но не дольше 2 с
  static ms_t idleAt = 0;
  const ms_t now = NowMs();
  int prim = -1;
  if (npc) Safe([&] { if (const addr a = rp(npc + 0x36c)) prim = rd<uint8_t>(a + 0x1a); });
  if (prim == 1) {
    if (!idleAt) idleAt = now;
  } else {
    idleAt = 0;
  }
  if (!npc || now > s_snapUntil.load() || (idleAt && now - idleAt > 150)) {
    s_snapHold = false;
    idleAt = 0;
    return;
  }
  V3 p{};
  if (!Safe([&] { p = rv3(npc + 0x70); })) return;
  if (std::fabs(p.x - s_snapX) + std::fabs(p.z - s_snapZ) > 0.003f) TpMove(npc, s_snapX, p.y, s_snapZ);
}
// ---------- floor crawl: use the same posture transition as horizontal ducts ----------
// Verified in Ghidra (007cae20/007cae90) and live horizontal-duct captures:
// native locomotion is 7 while moving and 0 while idle. Do not force mode 5,
// rebuild primary actions, or retain old modes/speeds across posture changes.
struct FloorCrawlState {
  addr actor = 0;
  bool owned = false;
};
static FloorCrawlState s_floorCrawl;
void ActVentCrawl(bool on) {
  if (ForcedRetreatActive()) return;
  if (!on) testMoveForward.store(false);
  const bool was = ventCrawl.exchange(on);
  if (on && !was) {
    s_snapHold = false;
    s_snapIdleUntil = 0;
  }
  if (was != on) Log("floor-crawl: requested %s", on ? "on" : "off");
}
static void ForgetVentCrawl() {
  // The old character may already be destroyed. Never write into its memory.
  s_floorCrawl = {};
  ventCrawl.store(false);
  testMoveForward.store(false);
  moving = false;
}
bool VentCrawlPending() { return s_floorCrawl.owned; }
void RestoreVentCrawl() {
  // Game thread (or a stalled game during unload), with JsLock held.
  if (!s_floorCrawl.owned) return;
  if (!npc || s_floorCrawl.actor != npc) {
    s_floorCrawl = {};
    return;
  }
  bool standing = false;
  if (!Safe([&] {
    Fn<CharacterPostureFn>(va::CrouchExit)(npc);
    standing = rd<uint8_t>(npc + 0x2AF) == 0;
  }) || !standing) return;  // scripted action can defer the native transition
  s_floorCrawl = {};
  Log("floor-crawl: native standing posture restored");
}
static void ApplyVentCrawl() {
  if (s_floorCrawl.actor && s_floorCrawl.actor != npc) s_floorCrawl = {};
  if (!npc) return;
  if (!ventCrawl.load()) {
    RestoreVentCrawl();
    return;
  }
  // Never take ownership of posture inside a real duct or backstage transition.
  if (VentBusy() || vent.stay || alienBackstage || ductOn) return;
  uint8_t crouched = 0;
  if (!Safe([&] { crouched = rd<uint8_t>(npc + 0x2AF); })) return;
  if (crouched) return;  // native function owns the context; no duplicate pushes
  bool applied = false;
  if (!Safe([&] {
    Fn<CharacterPostureFn>(va::CrouchEnter)(npc);
    applied = rd<uint8_t>(npc + 0x2AF) != 0;
  }) || !applied) return;
  s_floorCrawl.actor = npc;
  s_floorCrawl.owned = true;
  Log("floor-crawl: native crouched posture applied");
}
static void OnFaceLeave(Invocation&) {
  AutoDuctTick();
  ApplyVentCrawl();
  ApplyFace();
  SnapHoldTick();
}

// настоящий игрок из массива игроков (цель Чужого может быть NPC) — hot.js §1
void FindPlayer() {
  Safe([] {
    addr g = rp(rp(VA(va::GameGlobals)) + 8);
    if (rd<uint32_t>(g + 0x84) == 0) return;
    addr p = rp(rp(g + 0x80));
    if (p) player = p;
  });
}

// ---------------- файловый тестовый драйвер (без фокуса игры) ----------------
// Внешний C++-инструмент пишет команды в этот файл, а мод выполняет их на игровом потоке.
// Это нужно для захвата кадров через PrintWindow, когда окно игры не в фокусе.
#ifdef ALIENMOD_DEV_TOOLS
static void TestDriverTick() {
  static ms_t lastRead = 0;
  static std::string prev;
  const ms_t now = NowMs();
  if (now - lastRead < 100) return;
  lastRead = now;
  std::ifstream f(std::filesystem::path(ModBase() + L".test.cmd"));
  if (!f) return;
  std::string all((std::istreambuf_iterator<char>(f)), {});
  if (all == prev) return;
  prev = all;
  std::istringstream input(all);
  std::string line;
  while (std::getline(input, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
      line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string cmd;
    ls >> cmd;
    if (cmd == "on") {
      ActVentCrawl(true);
      Log("test-cmd: on");
    } else if (cmd == "off") {
      ActVentCrawl(false);
      Log("test-cmd: off");
    } else if (cmd == "move") {
      std::string value;
      ls >> value;
      testMoveForward.store(value == "on", std::memory_order_relaxed);
      Log("test-cmd: move %s", value.c_str());
    } else {
      Log("test-cmd: unknown '%s'", line.c_str());
    }
  }
}

#endif

// ---------------- главный цикл (alien.js, setInterval 16 мс) ----------------
static int s_tick = 0, s_release = 0, s_startTicks = 0;
static bool s_wasAny = false, s_strafeWas = true;
static ms_t s_strafeKeyAt = 0;
static void MainTick() {
#ifdef ALIENMOD_DEV_TOOLS
  TestDriverTick();
#endif
  {  // F6: экспериментальный animation context (ползание в комнате, без входа в вентиляцию)
    static bool was = false;
    const bool down = HotAdv(VK_F6);
    if (down && !was) ActVentCrawl(!ventCrawl.load());
    was = down;
  }
  if (!gate || !moveNode || !Alive(moveNode, va::VT_MOVE)) {
    moving = false;
    return;
  }
  s_tick++;
  if (npc && s_tick % 30 == 0) FindPlayer();
  if (npc) Safe([] {  // объект ходьбы Чужого — прямо из персонажа (не ждать, пока он пойдёт)
      const addr L = rp(npc + va::CharLocoOff);
      if (L && L != loco) loco = L, Log("loco %08X (of the alien %08X)", unsigned(L), unsigned(npc));
    });
  if (npc && loco && lockLocoNpc != npc) lockLocoNpc = npc;
  CamOffsetKeys();
  if (camMode == 2 && s_tick % 2 == 0) ApplyFollow();
  ms_t t = NowMs();
  if (camWant && t - camKeyAt > 300) CamDo();  // запасной путь, если хуки не тикают (Чужой стоит)
  if (!photo.on && t - camKeyAt > 500) {  // B — Чужой <-> Аманда; V (за Чужого) — вид за спиной <-> из головы
    if (HotK(K_SWAP)) ActSwap();
    else if (AlienKeyK(K_VIEW)) ActSwitchView();
  }
  const bool strafe = FaceByCamera();
  if (strafe != s_strafeWas) {
    s_strafeWas = strafe;
    if (fmode) Log(strafe ? "strafe resumed" : "strafe paused: %s", StrafePause());
  }
  Input in = ReadInput();
  if (testMoveForward.load(std::memory_order_relaxed)) {
    // Синтетический W: движение вперёд без клавиатуры и без фокуса окна.
    in.rawAny = in.any = true;
    in.speed = 1;
    in.camYaw = ViewYaw();
    in.worldYaw = in.camYaw;
    lastInput = in;
    moving = true;
  }
  TurnTick(in);
  float reqYaw = (strafe && player && !photo.on) ? in.camYaw : in.worldYaw;  // курс тела: по камере в режиме стрейфа
  if (in.any != s_wasAny) {
    s_wasAny = in.any;
    Log("input any=%d speed=%d yaw=%.2f", int(in.any), in.speed, in.worldYaw);
  }
  // стрейф/назад на средней скорости: у Чужого нет IDLE_TO_STRAFE для неё — первые 20 тиков даём Slowest
  bool sideways = strafe && in.any && player && std::fabs(WrapPi(in.worldYaw - in.camYaw)) > 0.6f;
  if (in.any && sideways && s_startTicks < 20) s_startTicks++;
  if (!in.any) s_startTicks = 0;
  wr<uint32_t>(moveNode + 8, uint32_t(!alienSnappy && in.speed == 1 && sideways && s_startTicks < 20 ? 0 : in.speed));
  if (npc) wf(moveNode + 0xc, WrapPi(reqYaw - rf(npc + 0x174)));
  if (in.any) {
    s_release = 0;
    s_snapHold = false;
    s_snapIdleUntil = 0;
    SetGate(gate, true);
    if (req) {
      wf(req + 0x1c, reqYaw);
      wf(req + 0x24, 10.f);
      wr<uint8_t>(req + 0x34, 1);
    }
  } else if (alienSnappy && !ventCrawl.load()) {  // «Удобный Чужой»: сразу — запрос на месте, рубильник закрыт, узел завершается «срочно»
    if (req && rf(req + 0x24) > 0.f) {
      wf(req + 0x24, 0.f);
      wr<uint8_t>(req + 0x34, 1);
    }
    // только что отпустил (в мире, не вентиляция/шахта/путь) — встать как вкопанный
    if (s_release == 0 && npc && !VentBusy() && !vent.stay && !alienBackstage && !ductOn && !autoDuctPath)
      Safe([&] {
        const V3 p = rv3(npc + 0x70);
        s_snapX = p.x, s_snapZ = p.z;
        s_snapUntil = NowMs() + 2000;
        s_snapHold = true;
        s_snapIdleUntil = NowMs() + 2000;  // и сразу в «стоит», без анимации остановки (OnTurnGameTick)
      });
    if (++s_release > 1) SetGate(gate, false);
  } else {
    if (req && rf(req + 0x24) > 0.3f) {
      wf(req + 0x24, 0.3f);
      wr<uint8_t>(req + 0x34, 1);
    }
    if (++s_release > 25) SetGate(gate, false);  // ~0.4 с после отпускания — дерево уходит в idle
  }
  // скорость завершения узла движения (RequestShutDownSpeed, узел +4: 1 плавно, 2 быстро, 3 срочно): «плавно» — Чужой
  // доигрывает WALK/CHARGE_TO_IDLE, «срочно» — обрывает остановку
  {
    static uint32_t orig = 0;
    if (!orig) orig = rd<uint32_t>(moveNode + 4);
    const uint32_t want = ventCrawl.load() ? 1u : alienSnappy ? 3u : (orig ? orig : 1u);
    if (rd<uint32_t>(moveNode + 4) != want) wr<uint32_t>(moveNode + 4, want);
    for (addr m : multiMoves)
      if (m != moveNode && Alive(m, va::VT_MOVE)) Safe([&] { if (rd<uint32_t>(m + 4) != want) wr<uint32_t>(m + 4, want); });
  }
  if (t - s_strafeKeyAt > 400 && AlienKeyAdvK(K_STRAFE)) s_strafeKeyAt = t, ActToggleStrafe();  // R — стрейф вкл/выкл
  if (strafe && loco) ApplyFace();
  if (s_tick % 1800 == 0 && npc) {
    V3 p = rv3(npc + 0x70);
    Log("npc yaw %.2f pos %.1f %.1f %.1f", rf(npc + 0x174), p.x, p.y, p.z);
  }
}

// ---------------- валидация (hot.js §5, каждые 5 мс) и зеркалирование на другие деревья (16 мс) ----------------
static void ValidateTick() {
  if (npc) {
    addr vt = 0;
    if (!SafeRead(npc, vt)) return AlienReset("npc unreadable");
    if (!npcVt) npcVt = vt;
    else if (vt != npcVt) return AlienReset("npc vtable changed");
  }
  for (auto& v : multi) v.erase(std::remove_if(v.begin(), v.end(), [](addr g) { return !Alive(g, va::VT_GATE); }), v.end());
  multiMoves.erase(std::remove_if(multiMoves.begin(), multiMoves.end(), [](addr m) { return !Alive(m, va::VT_MOVE); }),
                   multiMoves.end());
  bool lost = (moveNode && !Alive(moveNode, va::VT_MOVE));
  for (addr g : { gate, vent.go, vent.come, vent.stayG, pathGate, attack.gate, attack.nextGate })
    if (g && !Alive(g, va::VT_GATE)) lost = true;
  if (lost) {  // узлы старого дерева умерли — берём последние живые (новое дерево), а не стираем всё
    RepickPrimaries();
    Log("tree nodes gone — repicked gate %08X move %08X", unsigned(gate), unsigned(moveNode));
  }
}
static void MirrorTick() {
  auto mirror = [](const std::vector<addr>& arr, addr src) {
    uint8_t f = 0;
    if (!src || !SafeRead(src + 0x14, f)) return;
    for (addr g : arr)
      if (g != src) SetGate(g, f != 0);
  };
  mirror(multi[G_ARROW], gate);
  mirror(multi[G_GO], vent.go);
  mirror(multi[G_COME], vent.come);
  mirror(multi[G_STAY], vent.stayG);
  mirror(multi[G_PATH], pathGate);
  mirror(multi[G_ATTACK], attack.gate);
  mirror(multi[G_NEXT], attack.nextGate);
  if (moveNode) {
    uint32_t sp = rd<uint32_t>(moveNode + 8);
    float dir = rf(moveNode + 0xc);
    for (addr m : multiMoves)
      if (m != moveNode && Alive(m, va::VT_MOVE)) Safe([&] { wr<uint32_t>(m + 8, sp); wf(m + 0xc, dir); });
  }
}

// ---------------- режим пути (3): ветка "Alien Path control" -> ActionMoveToMostRecentSensedPosition ----------------
static bool FromPathTick(addr ra) {
  addr v = ToVA(ra);
  return v >= va::PathTick && v <= va::PathTickEnd;
}
static void OnPathTickEnter(Invocation& inv) {  // thiscall(узел, персонаж, состояние)
  if (HugPathFor(inv.arg[0])) return wr<uint32_t>(inv.ecx + 8, uint32_t(HugPathSpeed()));
  if (inv.arg[0] != npc) return;
  if (AutoDuctCancelPath(inv.ecx, inv.arg[0], inv.arg[1])) {
    inv.skip = true;
    inv.skipPop = 8;  // Tick(character, state); native cleanup already marked it finished
    return;
  }
  pathNode = inv.ecx;
  if (autoDuctPath) wr<uint32_t>(pathNode + 8, uint32_t(lastInput.speed));
}
static void OnPathTargetLeave(Invocation& inv) {
  if (!(autoDuctPath || hugPathNow) || !FromPathTick(inv.ra)) return;
  addr T2 = rp(inv.arg[0]);
  if (T2 && rd<int32_t>(T2 + 0x438) == -1) wr<int32_t>(T2 + 0x438, 0);
}
static void OnPathMoveToEnter(Invocation& inv) {  // thiscall(узел, персонаж, состояние, точка, ...)
  if (!FromPathTick(inv.ra)) return;
  V3 hp;
  if (HugPathTarget(inv.arg[0], hp)) {  // лицехват «по навмешу»: точка = он + WASD * 2.5 м
    wv3(inv.arg[2], hp.x, hp.y, hp.z);
    inv.args[4] = 0;
    return;
  }
  if (!autoDuctPath || !npc || inv.arg[0] != npc) return;
  V3 target{};
  if (!AutoDuctTarget(target)) return;
  wv3(inv.arg[2], target.x, target.y, target.z);
  inv.args[4] = 0;  // ShouldAim off

}
static void DuctGateTick() { SetGate(pathGate, autoDuctPath); }

// ---------------- Esc: меню паузы игры должно получать ввод, даже если играешь за Чужого ----------------
// Блок ввода Аманды (input_block.cpp) пропускает только сам Esc — меню паузы потом не нажать. Поэтому по Esc камера
// возвращается Аманде (B — снова за Чужого), а g_amandaInput открывает ввод сразу, до смены камеры в потоке игры.
// Esc больше не отдаёт камеру Аманде: Чужой (или лицехват) остаётся твоим, а на время паузы ввод идёт игре
// (g_pausePass: блок ввода снят, клавиши мода молчат). Пауза — по родным битам паузы или по тому, что игра перестала
// звать апдейт персонажей; игра пошла дальше — блок ввода возвращается сам. Esc не открыл паузу — через 1.5 с тоже.
static uint32_t PauseBits() {
  uint32_t v = 0;
  SafeRead(VA(va::PauseBits), v);
  return v;
}
static int s_escState = 0;  // 0 — играем, 1 — нажали Esc, ждём паузу, 2 — игра на паузе
static uint32_t s_runBits = 0;
static std::atomic<uint32_t> s_bitsWhileRunning{ 0 };
bool GameMenuPaused() { return GameStalled() && PauseBits() != s_bitsWhileRunning.load(); }  // биты паузы, пока игра шла (пауза без Esc — alt-tab — сравнивается с ними)
static ms_t s_escAt = 0, s_runAt = 0;
static uint32_t s_pauseBase = 0;
static bool GameStopped() { return NowMs() - g_gameTickAt.load() > 250 || PauseBits() != s_pauseBase; }
static void EscEnd(const char* why) {
  s_escState = 0;
  g_pausePass = false;
  Log("esc: control back (%s)", why);
}
static void EscTick() {
  static bool was = false;
  if (!GameStalled()) s_bitsWhileRunning = PauseBits();  // биты паузы, пока игра идёт (GameMenuPaused)
  const ms_t t = NowMs();
  // Down на паузе молчит — Esc читаем напрямую (при переназначении клавиш Esc — только отмена)
  const bool esc = GameFocused() && !g_rebinding && !g_uiKeysBlocked && (GetAsyncKeyState(VK_ESCAPE) & 0x8000), press = esc && !was;
  was = esc;
  const bool playing = (camMode != 0 || camWant == 1 || camWant == 2) && !photo.on;  // фоторежим Esc не трогает (выход — F9)
  if (s_escState && !playing) return EscEnd("camera is not on the alien any more");
  switch (s_escState) {
    case 0:
      if (!GameStalled()) s_runBits = PauseBits();
      // игра встала сама (alt-tab: при потере фокуса она ставит паузу), меню паузы на экране — мышь ему, а не блоку ввода
      // (тестер: после alt-tab меню паузы не нажималось мышью)
      if (!press && playing && GameFocused() && NowMs() - g_gameTickAt.load() > 500 && g_gameTickAt.load()) {
        s_pauseBase = s_runBits;
        s_escState = 2;
        s_runAt = 0;
        g_pausePass = true;
        Log("esc: the game paused by itself (alt-tab?) — input goes to the game menu");
        break;
      }
      if (press && playing) {
        s_pauseBase = PauseBits();
        s_escAt = t;
        s_escState = 1;
        g_pausePass = true;
        Log("esc: input goes to the game menu, the %s stays yours", hugger ? "facehugger" : "alien");
      }
      break;
    case 1:
      if (GameStopped()) {
        s_escState = 2;
        s_runAt = 0;
        Log("esc: game paused (bits %08X -> %08X)", s_pauseBase, PauseBits());
      } else if (t - s_escAt > 1500) {
        EscEnd("no pause after esc");
      }
      break;
    case 2:
      if (GameStopped()) s_runAt = 0;
      else if (!s_runAt) s_runAt = t;
      else if (t - s_runAt > 200) EscEnd("game resumed");
      break;
  }
}

// ---------------- действия для клавиш и кнопок ImGui ----------------
static int s_alienView = 2;  // последний вид за Чужого (2 за спиной, 1 из головы) — B возвращает в него
void ActCamera(int want) {
  if (want == 3 && MpAvatarLocked()) {  // по сети играешь Чужим / лицехватом / андроидом — к Аманде не переключиться
    static ms_t saidAt = 0;
    if (NowMs() - saidAt > 3000) saidAt = NowMs(), Log("multiplayer: your role is not Amanda — switching to her is off");
    return;
  }
  if (want == 1 || want == 2) s_alienView = want, g_amandaInput = false;
  camWant = want;
  camKeyAt = NowMs();
}
void ActCameraToggle(int want) { ActCamera(camMode == want ? 3 : want); }
void ActSwap() { ActCamera(camMode == 0 ? s_alienView : 3); }
void ActSwitchView() { ActCamera(camMode == 2 ? 1 : 2); }
void ActSetStrafe(bool on) {
  if ((fmode != 0) == on) return;
  fmode = on ? 3 : 0;
  s_strafeWas = StrafeActive();
  Log("strafe %d", int(on));
}
void ActToggleStrafe() { ActSetStrafe(fmode == 0); }

// ---------------- Чужой сразу после загрузки уровня (без ожидания, пока он выйдет и начнёт двигаться) ----------------
// Каждое условие дерева проверяется через FUN_00411d80 (слот 16 vtable узла-условия): thiscall(узел, персонаж, контекст).
// Дерево Чужого каждый кадр проверяет наши рубильники: в мире — первым «Alien Go Backstage» (SEL_ARROWS),
// за кулисами — первым «Alien Come Frontstage» (SEL_BACKSTAGE). Отсюда сразу и сам Чужой, и где он.
// Хук горячий (все условия всех NPC) и без общего замка: только сравнение адреса и запись атомиков.
static std::atomic<addr> s_watchFront{ 0 }, s_watchBack{ 0 };   // наши рубильники (копия для хука)
static std::atomic<addr> s_candFront{ 0 }, s_candBack{ 0 };     // персонаж из последней проверки
static std::atomic<uint32_t> s_frontAt{ 0 }, s_backAt{ 0 };    // когда проверялись (мс, младшие 32 бита)
static bool s_placeKnown = false;
static ms_t s_autoNextAt = 0;
static int s_autoTries = 0;

static void OnConditionEval(Invocation& inv) {
  const addr n = inv.ecx;
  if (!n) return;
  if (n == s_watchFront.load(std::memory_order_relaxed)) {
    s_candFront = inv.arg[0];
    s_frontAt = uint32_t(NowMs());
  } else if (n == s_watchBack.load(std::memory_order_relaxed)) {
    s_candBack = inv.arg[0];
    s_backAt = uint32_t(NowMs());
  }
}
static void FindAlienReset() {
  s_watchFront = s_watchBack = 0;
  s_candFront = s_candBack = 0;
  s_placeKnown = false;
  autoOutPending = false;
  s_autoTries = 0;
}
// кандидат — живой персонаж: есть в списке активных или у него vtable персонажа
static bool IsCharacter(addr c) {
  if (!c) return false;
  addr vt = 0;
  if (!SafeRead(c, vt) || !vt) return false;
  for (addr e : ActiveChars()) {
    addr evt = 0;
    if (e == c || (SafeRead(e, evt) && evt == vt)) return true;
  }
  return false;
}
static void FindAlienTick() {  // под общим замком, 50 мс
  s_watchFront = vent.go ? vent.go : gate;
  s_watchBack = vent.come ? vent.come : vent.stayG;
  if (!gate) return;
  const uint32_t now = uint32_t(NowMs());
  const bool front = now - s_frontAt.load() < 400, back = now - s_backAt.load() < 400;
  if (!npc) {
    addr c = back && !front ? s_candBack.load() : front ? s_candFront.load() : 0;
    if (c && IsCharacter(c)) {
      npc = c;
      if (dyingChar == c) dyingChar = 0;  // новый Чужой на месте старого
      Log("alien found %08X (%s)", unsigned(c), back && !front ? "backstage" : "in the world");
    }
  }
  if (front != back) {  // реальное положение — всегда, даже пока идёт наш выход/уход
    if (alienBackstage != back) Log("alien %s", back ? "went backstage" : "is in the world");
    alienBackstage = back;
  }
  // выход из вентиляции не удался: узел выхода закончился (или вышло время), а Чужой всё ещё за кулисами. Пока узел
  // идёт, Чужой ползёт к вентиляции — это бывает долго, поэтому ждём его конца, а не фиксированные секунды.
  // Повторы через другие вентиляции — vent.cpp (ExitTick).
  if (vent.comeAt && back && !front) {
    const ms_t now = NowMs();
    if (!(vent.comeUntil > now) && now - std::max(vent.comeAt, vent.comeEnd) > 2500) {
      vent.comeUntil = 0;
      vent.comeAt = 0;
      ventComeFailedAt = now;
      Log("vent come failed: alien still backstage");
    }
  } else if (vent.comeAt && front && !back) {
    vent.comeAt = 0;  // вышел
  }
  if (!npc || aiMode != 0 || VentBusy()) return;
  // «за кулисами» — по дереву, а не только после нашей команды 1: так управление за кулисами работает и сразу после загрузки
  if (back && !front && !vent.stay) {
    vent.stay = true;
    Log("alien is backstage");
  } else if (front && !back && vent.stay) {
    vent.stay = false;
    Log("alien is in the world");
  }
  if (!s_placeKnown && (front || back)) {  // первый раз на уровне: за кулисами — вывести к Аманде, звать не нужно
    s_placeKnown = true;
    autoOutPending = back && !front && autoComeOut;
    s_autoNextAt = NowMs() + 2500;  // сразу после загрузки игра команду сбрасывает — даём уровню дойти
    s_autoTries = 0;
  }
  if (autoOutPending) {
    if (front && !back) {
      autoOutPending = false;  // вышел
    } else if (!autoComeOut || s_autoTries >= 4) {
      autoOutPending = false;
      if (s_autoTries >= 4) Log("auto: alien did not come out, stays backstage (1 — try again)");
    } else if (NowMs() >= s_autoNextAt && !ExitBusy()) {
      s_autoTries++;
      s_autoNextAt = NowMs() + 8000;  // выход занимает несколько секунд — следующая попытка после него
      ActVentExitNearAmanda();
      autoOutPending = true;  // ActVentExitNearAmanda сбрасывает — повторяем, пока не выйдет
      Log("auto: alien comes out (try %d)", s_autoTries);
    }
  }
}

// ---------------- смена уровня и разрушение деревьев ----------------
// queue_level / request_next_level перехватывает DevTools (Detours) — он и зовёт ModLevelChange (mod_main.cpp),
// поэтому здесь эти два адреса больше не перехватываем: двойной хук одного адреса двумя библиотеками не нужен.
static const char* const kLevelFn[] = { "GameMenu::LoadLevel", "LoadLevelUnknownFunc1", "LoadLevelUnknownFunc2" };
template <int I> static void OnLevel(Invocation&) { HardReset(kLevelFn[I]); }

static ms_t s_destroyAt = 0;
// 0x6d01b0 — слот 3 vtable узла (его разрушение), ecx = сам узел. Убираем ровно этот узел, пока память ещё не
// освобождена (иначе запись в мёртвый узел = порча кучи), и берём последний живой экземпляр. Остальное не трогаем.
static void OnTreeDestroy(Invocation& inv) {
  const addr n = inv.ecx;
  if (!n) return;
  HugNodeDestroyed(n);
  bool mine = false;
  for (auto& v : multi) {
    auto it = std::find(v.begin(), v.end(), n);
    if (it != v.end()) v.erase(it), mine = true;
  }
  auto it = std::find(multiMoves.begin(), multiMoves.end(), n);
  if (it != multiMoves.end()) multiMoves.erase(it), mine = true;
  for (addr g : { gate, moveNode, vent.go, vent.come, vent.stayG, pathGate, attack.gate, attack.nextGate })
    if (g == n) mine = true;
  if (!mine) return;
  RepickPrimaries();
  for (addr g : { gate, moveNode, vent.go, vent.come, vent.stayG, pathGate, attack.gate, attack.nextGate })
    if (g == n) {  // умирающий ещё «жив» по vtable — не берём его
      if (g == gate) gate = 0;
      if (g == moveNode) moveNode = 0;
      if (g == vent.go) vent.go = 0;
      if (g == vent.come) vent.come = 0;
      if (g == vent.stayG) vent.stayG = 0;
      if (g == pathGate) pathGate = 0;
      if (g == attack.gate) attack.gate = 0;
      if (g == attack.nextGate) attack.nextGate = 0;
    }
  ms_t t = NowMs();
  if (t - s_destroyAt > 1000) {
    s_destroyAt = t;
    Log("tree node destroyed — now gate %08X move %08X", unsigned(gate), unsigned(moveNode));
  }
}

void InstallControl() {
  InstallTurnRate();
  using Interceptor::Attach;
  Attach(VA(va::FactoryMenuLink), OnFactoryEnter, OnFactoryLeave);
  Attach(VA(va::MoveParams), nullptr, OnMoveParamsLeave);
  Attach(VA(va::MoveStart), OnMoveStartEnter, OnMoveStartLeave);
  Attach(VA(va::SmartPtrCopy), OnSmartPtrCopyEnter, nullptr, false);
  Attach(VA(va::MoveShutdown), OnMoveShutdownEnter, nullptr);
  Attach(VA(va::MoveRecCreate), nullptr, OnMoveRecLeave, false);  // горячий: без замка
  Attach(VA(va::LocoGet), nullptr, OnLocoGetLeave);
  Attach(VA(va::LocoReqStart), nullptr, OnFaceLeave);
  Attach(VA(va::LocoReqUpdate), nullptr, OnFaceLeave);
  Attach(VA(va::LocoUpdate), nullptr, OnFaceLeave);
  Attach(VA(va::LocoUpdate), OnTurnLocoEnter, OnTurnGameTick);
  Attach(VA(va::PathTick), OnPathTickEnter, nullptr);
  Attach(VA(va::PathTarget), nullptr, OnPathTargetLeave);
  Attach(VA(va::PathMoveTo), OnPathMoveToEnter, nullptr);
  Attach(VA(va::MenuLoadLevel), OnLevel<0>, nullptr);
  Attach(VA(va::LoadLevel1), OnLevel<1>, nullptr);
  Attach(VA(va::LoadLevel2), OnLevel<2>, nullptr);
  Attach(VA(va::TreeNodeDestroy), OnTreeDestroy, nullptr);
  Attach(VA(va::ConditionEval), OnConditionEval, nullptr, false);
  Every(50, "find-alien", FindAlienTick);
  Every(16, "main", MainTick);
  // Аманда — на любом уровне, и без Чужого (фоторежиму и лицехватам она нужна всегда)
  Every(250, "find-player", [] {
    static ms_t at = 0;
    if (player && NowMs() - at < 5000) return;
    at = NowMs();
    FindPlayer();
  });
  Every(10, "esc", EscTick);
  Every(5, "validate", ValidateTick);
  Every(16, "mirror", MirrorTick);
  Every(50, "duct-entry-gate", DuctGateTick);
}
