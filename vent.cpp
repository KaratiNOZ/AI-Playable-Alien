// vent.cpp — вентиляция: E = уйти за кулисы / выйти здесь, Q = выйти у Аманды; движение за кулисами по узлам.
// Источник: alien.js (вентиляция) + hot.js §10 (задержка ветки), ventCancel, bs2 (навигация за кулисами v2).
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <vector>

bool VentBusy() {
  ms_t t = NowMs();
  return vent.goUntil > t || vent.comeUntil > t;
}

// менеджер вентиляций: +8 массив (шаг 0x30, +0x28 -> позиция), +0xC число
static void NearestVent(addr mgr, addr ref, int& best, float& bd) {
  best = -1;
  bd = 1e9f;
  addr arr = rp(mgr + 8);
  uint32_t cnt = rd<uint32_t>(mgr + 0xc);
  V3 a = rv3(ref + 0x70);
  if (cnt > 4096) return;
  for (uint32_t i = 0; i < cnt; i++)
    Safe([&] {
      V3 v = rv3(rp(arr + i * 0x30 + 0x28));
      float d = std::hypot(v.x - a.x, v.z - a.z) + std::fabs(v.y - a.y) * 0.7f;
      if (d < bd) bd = d, best = int(i);
    });
}

// геттер «выбранная вентиляция» для Чужого (индекс в mgr+0x40): подменяем выбор для ухода за кулисы и пишем
// индекс в слот менеджера, чтобы блокировки остались согласованными. Причин (VentLockReason) у менеджера две — 0 и 1.
// Подменяем всегда, а не только когда игра ничего не нашла: если игра выбрала свою (дальнюю) вентиляцию, Чужой шёл к
// ней, а через 10 с запрос кончался (тестер: «иногда отказывается лезть в вентиляцию»)
static void OnVentPickLeave(Invocation& inv) {
  ms_t now = NowMs();
  if (!npc || !(vent.goUntil > now)) return;
  const addr orig = inv.retval();  // выбор игры (0 — не нашла)
  addr mgr = inv.ecx;
  int reason = int(inv.arg[0]), best;
  float bd;
  NearestVent(mgr, npc, best, bd);
  if (best < 0) return;
  addr entry = rp(mgr + 8) + addr(best) * 0x30;
  if (orig && orig != entry) {  // игра выбрала свою — оставляем, если она почти так же близко (её выбор учитывает путь)
    float od = 1e9f;
    Safe([&] {
      const V3 v = rv3(rp(orig + 0x28)), a = rv3(npc + 0x70);
      od = std::hypot(v.x - a.x, v.z - a.z) + std::fabs(v.y - a.y) * 0.7f;
    });
    if (od <= bd + 3.f) return;
  }
  if (reason >= 0 && reason < 2) Safe([&] { wr<int32_t>(mgr + 0x40 + reason * 4, best); });
  inv.replaceRet(entry);
  if (best != vent.pickLast || now - vent.pickAt > 5000) {
    vent.pickLast = best;
    vent.pickAt = now;
    Log("vent pick go #%d dist %.2f%s", best, bd, orig && orig != entry ? " (the game had picked another one)" : "");
  }
}

// Shutdown узлов MoveToBackstage/FrontStage -> смена сцены. Выход: ветку включаем через 300 мс после клавиши,
// поэтому всё, что закончилось раньше 450 мс, — предыдущий узел; уход — как было (1.5 с).
static void OnBackstageShutdown(Invocation&) {
  ms_t now = NowMs();
  if (vent.goUntil > now && now - vent.keyAt > 1500) {
    vent.goUntil = 0;
    vent.stay = true;
    Log("backstage");
  } else if (vent.comeUntil > now && now - vent.keyAt > 450) {
    vent.comeUntil = 0;
    vent.comeEnd = now;
    Log("frontstage");
  }
}

static bool MoveKeys() {
  return Down('W') || Down('A') || Down('S') || Down('D') || Down(VK_UP) || Down(VK_DOWN) || Down(VK_LEFT) || Down(VK_RIGHT);
}

// ---------------- выход из вентиляции ----------------
// Как игра выбирает вентиляцию для выхода (код менеджера вентиляций, FUN_00414590 — каждый кадр):
//   * «вентиляция Чужого» mgr+0x40 — ближайшая по навмеш-пути от Чужого: путь короче 10 м и проходит дальше 8 м от
//     Аманды (FUN_00414520/FUN_00414440);
//   * «вентиляция выхода» mgr+0x48 — любая другая, до которой путь от Аманды уже посчитан (пути считаются по одному, по
//     кругу, и устаревают, когда Аманда сдвигается на 2 м), не длиннее 20 м, и последние 5 м пути подходят к Аманде
//     сзади/сбоку — ±100° от «назад» по её взгляду (FUN_004135b0/FUN_004134a0/FUN_00413330). Нет «его» вентиляции —
//     нет и выхода.
// Отсюда все отказы: «выйти здесь» ставил Аманду к вентиляции рядом с Чужим — а это как раз исключённая «его»
// вентиляция, и Аманда оказывалась ближе 8 м к его пути; сразу после переноса пути от Аманды к тому же ещё старые.
// Сам старт выхода (FUN_0040fb10) берёт вентиляцию из геттера FUN_00413310 и просто ведёт Чужого к её позиции. Поэтому,
// как и для ухода, подставляем её там — Аманду никуда не переносим:
//   E (играешь Чужим) — ближайшая к Чужому вентиляция;
//   Q / из тела Аманды — выбирает игра, а если не нашла — ближайшая к Аманде (к её «дому», если она спрятана).
// Не вышел — следующая по близости, всего 3 попытки.
enum { EXIT_NONE, EXIT_AMANDA, EXIT_HERE };
static std::atomic<int> s_exitMode{ EXIT_NONE };
static std::atomic<int> s_exitVent{ -1 };        // подставляемая вентиляция (индекс); -1 — выбирает игра
static std::atomic<bool> s_gameRefused{ false };  // геттер вернул «нет вентиляции» (поток игры -> ExitTick)
static std::vector<int> s_exitTried;               // уже опробованные в этом выходе (только под общим замком)
static int s_exitTries = 0;
static ms_t s_exitAt = 0, s_exitStartAt = 0;

static bool VentPos(addr arr, uint32_t i, V3& v) { return Safe([&] { v = rv3(rp(arr + i * 0x30 + 0x28)); }); }
static bool VentMgr(addr& mgr, addr& arr, uint32_t& cnt) {
  mgr = arr = 0;
  cnt = 0;
  return npc && Safe([&] { mgr = rp(rp(npc + 0x34f0) + 0x24), arr = rp(mgr + 8), cnt = rd<uint32_t>(mgr + 0xc); }) && mgr &&
         arr && cnt <= 4096;
}

// ближайшая к точке вентиляция (кроме уже опробованных)
static int NearestUntried(const V3& a, float& bd) {
  bd = 1e9f;
  addr mgr, arr;
  uint32_t cnt;
  if (!VentMgr(mgr, arr, cnt)) return -1;
  int best = -1;
  for (uint32_t i = 0; i < cnt; i++) {
    if (std::find(s_exitTried.begin(), s_exitTried.end(), int(i)) != s_exitTried.end()) continue;
    V3 v;
    if (!VentPos(arr, i, v) || !std::isfinite(v.x)) continue;
    const float d = std::hypot(v.x - a.x, v.z - a.z) + std::fabs(v.y - a.y) * 0.7f;
    if (d < bd) bd = d, best = int(i);
  }
  return best;
}
static bool PickExitVent() {
  const bool here = s_exitMode == EXIT_HERE;
  V3 a{};
  const bool ok = here ? Safe([&] { a = rv3(npc + 0x70); })
                       : (AmandaHome(a) || (player && Safe([&] { a = rv3(player + 0x70); })));
  if (!ok || !std::isfinite(a.x)) return false;
  float d;
  const int v = NearestUntried(a, d);
  if (v < 0) return false;
  s_exitTried.push_back(v);
  s_exitVent = v;
  Log("vent exit: via vent #%d, %.1f m from %s", v, d, here ? "the alien" : "Amanda");
  return true;
}
static int GameExitVent() {  // что игра выбрала сама (слоты двух причин)
  addr mgr, arr;
  uint32_t cnt;
  int v0 = -1, v1 = -1;
  if (!VentMgr(mgr, arr, cnt) || !Safe([&] { v0 = rd<int32_t>(mgr + 0x48), v1 = rd<int32_t>(mgr + 0x4c); })) return -1;
  return v0 >= 0 ? v0 : v1;
}

// геттер «вентиляция для выхода» (поток игры; зовёт только старт узла выхода)
static void OnVentPickExitLeave(Invocation& inv) {
  if (!npc || s_exitMode == EXIT_NONE || !(vent.comeUntil > NowMs())) return;
  const int v = s_exitVent;
  if (v < 0) {
    if (inv.retval() == 0) s_gameRefused = true;  // игра не нашла — ExitTick выберет сам
    return;
  }
  const addr mgr = inv.ecx;
  if (uint32_t(v) >= rd<uint32_t>(mgr + 0xc)) return;
  const addr entry = rp(mgr + 8) + addr(v) * 0x30;
  if (!rp(entry + 0x28)) return;
  const int reason = int(inv.arg[0]);
  if (reason >= 0 && reason < 2) wr<int32_t>(mgr + 0x48 + reason * 4, v);  // слот менеджера — как при уходе
  inv.replaceRet(entry);
  const ms_t now = NowMs();
  if (v != vent.pickLast || now - vent.pickAt > 5000) {
    vent.pickLast = v;
    vent.pickAt = now;
    Log("vent pick exit #%d", v);
  }
}

static void ComeStart(int mode, bool retry) {
  const ms_t t = NowMs();
  vent.keyAt = t;
  vent.comeAt = t;
  vent.comeEnd = 0;
  ventComeFailedAt = 0;
  vent.comeUntil = t + 25000;  // путь за кулисами до дальней вентиляции — секунды; узел закончится сам (Shutdown)
  vent.goUntil = 0;
  vent.stay = false;
  s_exitAt = t;
  s_gameRefused = false;
  if (!retry) {
    s_exitMode = mode;
    s_exitTried.clear();
    s_exitTries = 1;
    s_exitVent = -1;
    s_exitStartAt = t;
    if (mode == EXIT_HERE || GameExitVent() < 0) PickExitVent();  // Q: игра уже знает, что у Аманды выхода нет
  }
  Log("vent come (%s, try %d)", mode == EXIT_HERE ? "here" : "near Amanda", s_exitTries);
}
static void ExitEnd() {
  s_exitMode = EXIT_NONE;
  s_exitVent = -1;
  s_gameRefused = false;
}
static void CancelVent(const char* by) {
  const bool exiting = s_exitMode != EXIT_NONE;
  autoOutPending = false;
  ExitEnd();
  if (!VentBusy() && !exiting) return;
  vent.goUntil = vent.comeUntil = 0;
  vent.comeAt = vent.comeEnd = 0;  // отменили сами — не «не вышел»
  vent.keyAt = NowMs();
  Log("vent cancelled by %s", by);
}

void ActVentGo() {
  ms_t t = NowMs();
  autoOutPending = false;
  ExitEnd();
  vent.keyAt = t;
  vent.goUntil = t + 10000;
  vent.comeUntil = 0;
  vent.comeAt = vent.comeEnd = 0;
  Log("vent go");
}
void ActVentCome() { ComeStart(EXIT_AMANDA, false); }
void ActVentCancel() { CancelVent("menu"); }
void ActVentExitHere() {
  autoOutPending = false;
  ComeStart(EXIT_HERE, false);
}
void ActVentExitNearAmanda() {
  autoOutPending = false;
  ComeStart(EXIT_AMANDA, false);
}

static void ExitTick() {  // 50 мс, под общим замком
  if (s_exitMode == EXIT_NONE) return;
  const ms_t t = NowMs();
  if (!npc || aiMode != 0 || t - s_exitStartAt > 90000) {  // ИИ забрал Чужого / уровень сменился / что-то зависло
    ExitEnd();
    return;
  }
  if (s_gameRefused.exchange(false) && s_exitVent < 0 && vent.comeUntil > t) {
    Log("vent exit: the game found no flanked vent near Amanda");
    PickExitVent();
  }
  if (ventComeFailedAt >= s_exitAt) {  // узел закончился, а Чужой всё ещё за кулисами
    if (s_exitTries < 3 && PickExitVent()) {
      s_exitTries++;
      ComeStart(s_exitMode, true);
    } else {
      Log("vent exit: alien stayed backstage after %d tries", s_exitTries);
      ExitEnd();
    }
    return;
  }
  if (!vent.comeAt && !alienBackstage) {  // вышел (control.cpp видит его дерево в мире)
    Log("vent exit: alien is out (try %d)", s_exitTries);
    vent.comeUntil = 0;  // ветку выхода закрываем сразу, иначе узел начнётся снова уже в мире
    ExitEnd();
    return;
  }
  if (!vent.comeUntil && !vent.comeEnd) ExitEnd();  // сброс извне (смена ИИ, паника, «застрял»)
}

bool ExitBusy() { return s_exitMode != EXIT_NONE; }
void ExitReset() {  // смена уровня / новый Чужой
  ExitEnd();
  s_exitTried.clear();
  vent.comeAt = vent.comeEnd = 0;
}

// E — одна клавиша на всё: в мире -> уйти за кулисы, за кулисами -> выйти здесь, в пути -> отменить
void ActVentSmart() {
  if (VentBusy() || ExitBusy()) {
    CancelVent("key");
  } else if (vent.stay || alienBackstage) {
    if (camMode == 0) ActVentExitNearAmanda();  // играешь Амандой — Чужой выходит к ней
    else ActVentExitHere();                     // играешь Чужим — выходит там, где он
  } else {
    ActVentGo();
  }
}

static void VentTick() {
  ms_t t = NowMs();
  // E — вентиляция по ситуации, Q — вылезти у Аманды. Только по нажатию: пока клавишу держат, она больше не
  // отменяет выход и не запускает его заново (раньше удержание E гоняло «выход — отмена» по кругу)
  static bool s_e = false, s_q = false;
  const bool e = AlienKeyK(K_VENT), q = AlienKeyK(K_VENT_AMANDA);
  const bool k1 = e && !s_e, k2 = q && !s_q;
  s_e = e, s_q = q;
  if (VentBusy() && t - vent.keyAt >= 400) {
    // отмена: уход в вентиляцию — любой клавишей движения или E/Q; выход — только E/Q, чтобы WASD его не сбивали
    const bool mv = vent.goUntil > t && MoveKeys();
    if (mv || k1 || k2) CancelVent(mv ? "move" : "key");
  } else if (!VentBusy() && t - vent.keyAt > 300) {
    if (k1) ActVentSmart();
    else if (k2 && (vent.stay || alienBackstage || ExitBusy())) ExitBusy() ? CancelVent("key") : ActVentExitNearAmanda();
  }
  static bool s_goWas = false;  // запрос «в вентиляцию» кончился по времени, а Чужой не ушёл — в лог (разбор отказов)
  if (s_goWas && !(vent.goUntil > t) && !vent.stay && !alienBackstage && t - vent.keyAt > 9000)
    Log("vent go: timed out — the alien did not get in (picked #%d)", vent.pickLast);
  s_goWas = vent.goUntil > t;
  // первые 300 мс после нажатия ветку не включаем — сначала гаснет наш запрос движения
  bool settle = t - vent.keyAt < 300;
  SetGate(vent.go, !settle && vent.goUntil > t);
  SetGate(vent.come, !settle && vent.comeUntil > t);
  SetGate(vent.stayG, vent.stay);
}

// ---------------- за кулисами: WASD/стрелки ведут самого Чужого по вентиляции, камера едет за ним ----------------
// За кулисами Чужой «вне мира»: игра двигает его только своим ИИ (ActionBackstageAreaSweep), а npc+0x70 каждый кадр
// пересобирает из его Havok-тела (проверено по памяти: запись только в npc+0x70 держится до следующего кадра).
// Поэтому двигаем так же, как телепорт из docs/HOOKING.md: Havok-тело (позиция, swept COM, скорость 0) + npc+0x70 —
// TpMove. Направление — относительно камеры (её курс считает camera.cpp из орбиты follow-камеры), как в мире.
static const wchar_t* DirName(float a) {
  static const wchar_t* const ru[8] = { L"вперёд", L"вперёд-вправо", L"вправо", L"назад-вправо",
                                        L"назад",  L"назад-влево",   L"влево",  L"вперёд-влево" };
  static const wchar_t* const en[8] = { L"ahead", L"ahead-right", L"right", L"back-right", L"back", L"back-left", L"left", L"ahead-left" };
  const int k = int(std::lround(a / (kPi / 4))) & 7;
  return g_lang ? en[k] : ru[k];
}
static float BsViewYaw() {  // куда смотрит камера: за спиной — по орбите, иначе по взгляду Аманды
  if (camMode == 2 && bsViewValid) return bsViewYaw;
  float y = 0;
  if (player) Safe([&] { y = rf(player + 0x174); });
  return y;
}
static bool BackstageControl() { return npc && vent.stay && aiMode == 0 && !VentBusy() && !photo.on && !hugger; }

static ms_t s_bsLast = 0, s_bsLogAt = 0;
static int s_bsViaBody = -1;
static void BackstageMoveTick() {  // 16 мс
  ms_t t = NowMs();
  float dt = std::min(0.05f, float(t - (s_bsLast ? s_bsLast : t)) / 1000.f);
  s_bsLast = t;
  if (!BackstageControl() || photo.on) return;  // фоторежим: WASD — камере, Чужой стоит
  const bool cam = camMode != 0;
  bool u = Down(cam ? 'W' : VK_UP), dn = Down(cam ? 'S' : VK_DOWN), l = Down(cam ? 'A' : VK_LEFT), r = Down(cam ? 'D' : VK_RIGHT);
  int dx = (r ? 1 : 0) - (l ? 1 : 0), dy = (u ? 1 : 0) - (dn ? 1 : 0);
  if (!dx && !dy) return;
  const float speed = Down(VK_SPACE) ? 6.f : Down(VK_SHIFT) ? 4.f : Down(VK_CONTROL) ? 1.2f : 2.5f;  // м/с
  const float yaw = WrapPi(BsViewYaw() + std::atan2(float(dx), float(dy)));
  V3 a = rv3(npc + 0x70);
  const bool body = TpMove(npc, a.x + std::sin(yaw) * speed * dt, a.y, a.z + std::cos(yaw) * speed * dt);
  wf(npc + 0x174, yaw);
  if (int(body) != s_bsViaBody) {
    s_bsViaBody = int(body);
    Log("backstage move: %s", body ? "via Havok body" : "no Havok body found — position only (game may reset it)");
  }
  if (t - s_bsLogAt > 1000) {  // диагностика: держится ли позиция
    s_bsLogAt = t;
    Log("backstage pos %.2f %.2f %.2f", a.x, a.y, a.z);
  }
}

// вентиляции уровня (для карты в HUD)
std::vector<V3> VentPositions() {
  std::vector<V3> out;
  if (!npc) return out;
  Safe([&] {
    addr mgr = rp(rp(npc + 0x34f0) + 0x24), arr = rp(mgr + 8);
    uint32_t cnt = rd<uint32_t>(mgr + 0xc);
    if (cnt > 4096) return;
    for (uint32_t i = 0; i < cnt; i++) {
      V3 v;
      if (VentPos(arr, i, v)) out.push_back(v);
    }
  });
  return out;
}

// строка в HUD: ближайшие выходы из вентиляции и где они относительно камеры; заодно — след Чужого для карты
static void BackstageListTick() {  // 200 мс
  if (npc && (vent.stay || alienBackstage)) {
    V3 p{};
    if (Safe([&] { p = rv3(npc + 0x70); }) && std::isfinite(p.x)) {
      if (bsTrail.empty() || std::hypot(p.x - bsTrail.back().x, p.z - bsTrail.back().z) > 0.5f) bsTrail.push_back(p);
      if (bsTrail.size() > 2000) bsTrail.erase(bsTrail.begin(), bsTrail.begin() + 500);
    }
  }
  if (!npc || !(vent.stay || alienBackstage)) {
    OverlaySet(1, L"");
    return;
  }
  addr mgr = rp(rp(npc + 0x34f0) + 0x24), arr = rp(mgr + 8);
  uint32_t cnt = rd<uint32_t>(mgr + 0xc);
  if (cnt > 4096) cnt = 0;
  V3 a = rv3(npc + 0x70);
  const float view = BsViewYaw();
  struct Node { int i; float d, dy, ang; };
  std::vector<Node> list;
  for (uint32_t i = 0; i < cnt; i++)
    Safe([&] {
      V3 v = rv3(rp(arr + i * 0x30 + 0x28));
      float d = std::hypot(v.x - a.x, v.z - a.z);
      if (d <= 40) list.push_back({ int(i), d, v.y - a.y, WrapPi(std::atan2(v.x - a.x, v.z - a.z) - view) });
    });
  std::sort(list.begin(), list.end(), [](const Node& p, const Node& q) { return p.d < q.d; });
  std::wstring s = TRW(L"Выходы рядом:", L"Exits nearby:");
  for (size_t k = 0; k < list.size() && k < 4; k++) {
    const Node& n = list[k];
    wchar_t line[160];
    swprintf(line, 160, L"\n   %ls  %.1f %ls%ls", DirName(n.ang), n.d, TRW(L"м", L"m"),
             n.dy > 1 ? TRW(L" (выше)", L" (above)") : n.dy < -1 ? TRW(L" (ниже)", L" (below)") : L"");
    s += line;
  }
  OverlaySet(1, s);
}

void InstallVent() {
  using Interceptor::Attach;
  Attach(VA(va::VentPickAlien), nullptr, OnVentPickLeave);
  Attach(VA(va::BackstageShutdown), OnBackstageShutdown, nullptr);
  Attach(VA(va::VentPickExit), nullptr, OnVentPickExitLeave);
  Every(10, "vent", VentTick);
  Every(50, "vent-exit", ExitTick);
  Every(16, "backstage-move", BackstageMoveTick);
  Every(200, "backstage-list", BackstageListTick);
}
