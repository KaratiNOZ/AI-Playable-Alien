// camera.cpp — камеры B/V, настройка камеры колесом мыши, флаг is_first_person, шахта (ближняя камера), столкновения камеры
// (навмеш + Havok), камера за кулисами, звук от Чужого, безопасный возврат камеры при уничтожении Чужого.
// Источник: alien.js (камера, столкновения, звук) + hot.js §8, §9, fpKeeper, bsCam/bsCam2/bsEnt/bsEnt2.
#include "game.h"
#include "interceptor.h"
#include "puppet.h"
#include "state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

static uint32_t HashName(const char* s) {  // hashName() из alien.js
  uint32_t h = 0;
  size_t n = strlen(s);
  for (size_t i = 0, j = 0; i < 256; i++, j++) {
    if (j >= n) j = 0;
    h += uint32_t(uint8_t(s[j])) << (i & 31);
  }
  return h;
}
static const uint32_t H_BONE = HashName("BoneAttached"), H_FOLLOW = HashName("FollowBehavior");
// кадр атаки лицехвата (ниже, CamKillCam)
static std::atomic<addr> s_killCamWant{ 0 };
static addr s_killCamOn = 0;
static V3 s_kcPos{};
static float s_kcYaw = 0, s_kcPitch = 0;
static bool s_kcHave = false;


addr CamG() { return rp(rp(VA(va::GameGlobals)) + 0x58); }
static void CamSetEntity(addr cam, addr ent) { Fn<CamSetEntityFn>(va::CamSetEntity)(cam, ent); }
static unsigned CamActivate(addr G, addr cam) { return Fn<CamActivateFn>(va::CamActivate)(G, cam); }

// поведения камеры: cam+0xbc массив, cam+0xc0 число; тип поведения = хэш имени в +8
template <class F> static void ForBehaviours(addr cam, uint32_t hash, F f) {
  uint32_t n = std::min<uint32_t>(rd<uint32_t>(cam + 0xc0), 256);
  addr arr = rp(cam + 0xbc);
  for (uint32_t i = 0; i < n; i++) {
    addr b = rp(arr + i * 4);
    if (b && rd<uint32_t>(b + 8) == hash) f(b);
  }
}
static addr FindBehaviour(addr cam, uint32_t hash) {
  addr r = 0;
  Safe([&] { ForBehaviours(cam, hash, [&](addr b) { if (!r) r = b; }); });
  return r;
}
addr FollowBehRaw() {
  addr G = 0, f = 0;
  if (!SafeRead(VA(va::GameGlobals), G) || !G || !Safe([&] { f = rp(CamG() + 0x1dc); }) || !f) return 0;
  return FindBehaviour(f, H_FOLLOW);
}
addr FollowBeh() { return camMode == 0 ? 0 : FollowBehRaw(); }  // с камерой Аманды в её FollowBehavior не пишем
static addr BoneBeh() {
  addr c = 0;
  if (!Safe([&] { c = rp(CamG() + 0x1e0); }) || !c) return 0;
  return FindBehaviour(c, H_BONE);
}
static void SetBoneTarget(addr code, addr ent) {
  ForBehaviours(code, H_BONE, [&](addr b) { wr<addr>(b + 0xe4, ent); });
}

void ApplyFollow() {
  addr b = FollowBeh();
  if (!b) return;
  for (int i = 0; i < 3; i++) {
    wf(b + 0x9c + i * 4, fOff[i]);
    wf(b + 0xa8 + i * 4, fTgt[i]);
  }
}

// сущность годится в цель камеры: активация камеры ищет у цели кость «Head1» (FUN_006b3100 читает скелет
// *(персонаж+0x3f0+0x328)). У Аманды, которую уже разбирает перезапуск уровня, там 0 — вылет 0xA2C011 (тестер: game over
// -> «начать заново» -> Чужой разрушен раньше Аманды -> мод вернул камеру ей -> вылет)
static bool CamEntityOk(addr e) {
  addr sk = 0;
  return e && SafeRead(e + 0x3f0 + 0x328, sk) && sk;
}
// Текущая камера — не follow-камера Аманды (в игре она и есть камера Аманды, is_first_person) и не камеры мода: идёт
// катсцена (или игра взяла свою камеру). Брать Чужого в такой момент нельзя: мод запомнил бы камеру катсцены как
// «камеру Аманды», а после катсцены Чужой исчезает — тестер: не вернуться к Аманде, Аманда проваливается
bool CamCutsceneActive() {
  bool r = false;
  Safe([&] {
    const addr G = CamG();
    if (!G) return;
    const addr cur = rp(G + 0x1d8);
    r = cur && cur != rp(G + 0x1dc) && cur != rp(G + 0x1e0) && cur != rp(G + 0x1f0);
  });
  return r;
}
void CamRestore(const char* why) {
  if (player && !CamEntityOk(player)) {  // уровень разбирается: камеры не трогаем, игра поставит свои
    camMode = 0;
    camSaved = 0;
    Log("cam restore skipped (%s): amanda is being unloaded", why);
    return;
  }
  bool ok = Safe([&] {
    addr G = CamG(), follow = rp(G + 0x1dc), code = rp(G + 0x1e0);
    if (player) {
      CamSetEntity(follow, player);
      CamSetEntity(code, player);
      SetBoneTarget(code, player);
    }
    if (camSaved) CamActivate(G, camSaved);
    camMode = 0;
    Log("cam restored: %s", why);
  });
  if (!ok) Log("err camRestore");
}

static CamCfg s_cur;
static bool s_curInit = false;
static void FollowFromCfg();
static void HeadFromCfg();

// V — code_controlled_camera (BoneAttached Head1) на Чужого, B — player_follow_camera с целью Чужой, N — вернуть.
// Зовётся из потока игры (хуки 0x660dc0/0x44d4f0) и запасным путём из таймера.
void CamDo() {
  const addr tgt = CamTarget();  // Чужой или лицехват, за которого играешь
  // вернуть камеру Аманде (3) можно и без цели: отпустил лицехвата/андроида на уровне без Чужого
  if (!camWant || !player || (!tgt && camWant != 3)) return;
  if ((camWant != 3 && !CamEntityOk(tgt)) || !CamEntityOk(player)) return;  // цель или Аманда разбираются (выгрузка уровня)
  if ((camWant == 1 || camWant == 2) && camMode == 0 && CamCutsceneActive()) {
    camWant = 0;
    cutsceneRefusedAt = NowMs();
    Log("cam: the game's own camera is active (cutscene) — wait until it ends");
    return;
  }
  int w = camWant;
  if (hugger && w == 1) w = 2;  // у лицехвата вида из головы нет — только за спиной
  camWant = 0;
  camMode = (w == 3) ? 0 : w;
  addr G = CamG(), cur = rp(G + 0x1d8), follow = rp(G + 0x1dc), code = rp(G + 0x1e0);
  if (!camSaved) camSaved = cur;
  if (w == 1) {
    SetBoneTarget(code, tgt);
    CamSetEntity(code, tgt);
    Safe([] { HeadFromCfg(); });
    CamActivate(G, code);
  }
  if (w == 2) {  // следование Аманды не включаем принудительно
    CamSetEntity(follow, tgt);
    CamActivate(G, follow);
    s_cur = camCfg, s_curInit = true;  // при включении — сразу на месте, без подъезда
    FollowFromCfg();
    ApplyFollow();
  }
  if (w == 3) CamRestore("N");
  Log("cam %d", w);
}
static void PickWorld();
static void KillCamTick() {
  if (camMode != 2) {  // камера у Аманды (или из головы): кадр атаки забыт, CamDo поставит цель заново
    s_killCamOn = 0;
    return;
  }
  const addr want = s_killCamWant.load();
  if (want == s_killCamOn) return;
  const bool ok = Safe([&] {
    const addr G = CamG(), follow = rp(G + 0x1dc), code = rp(G + 0x1e0);
    if (want) {  // камера «из кости» на жертву — позицию пишем сами (OnKcSubmitEnter)
      SetBoneTarget(code, want);
      CamSetEntity(code, want);
      CamActivate(G, code);
    } else if (const addr ent = CamTarget()) {
      CamSetEntity(follow, ent);
      CamActivate(G, follow);
    }
  });
  if (!ok) return;
  s_killCamOn = want;
  s_kcHave = false;
  FollowFromCfg();
  ApplyFollow();
  Log(want ? "camera: attack view on the victim" : "camera: back on the facehugger");
}
std::atomic<addr> g_devCodeCam{ 0 };  // отладка (dev\cmd.txt: camcode <адрес>): камера «из кости» на персонажа

// ---- подмодели Аманды: спрятать (флаги +7 = 0, запомнив прежние) и вернуть как было; поток игры ----
template <class F> static void ForSubmodels(addr ch, F f) {
  const addr anim = ch + 0x3f0;
  auto visit = [&](addr m) {
    if (!m) return;
    const uint32_t n = std::min<uint32_t>(rd<uint32_t>(rp(m + 200) + 0x54), 256);
    const addr arr = rp(m + 0x10c);
    for (uint32_t i = 0; i < n; i++)
      if (const addr sub = rp(arr + i * 4)) f(sub);
  };
  if (!rd<uint8_t>(anim + 0x3cd)) visit(Fn<ModelInstFn>(va::ModelInst)(anim));
  else
    for (uint32_t k = 0, n = std::min<uint32_t>(rd<uint32_t>(anim + 0x3d0), 16); k < n; k++)
      visit(Fn<ModelInstAtFn>(va::ModelInstAt)(anim, k));
}
static const char* SubName(addr sub) {
  const char* s = nullptr;
  Safe([&] { s = reinterpret_cast<const char*>(rp(rp(rp(sub + 0x38)) + 8)); });
  return s ? s : "?";
}
// ---- части Аманды: игра рисует её «от первого лица» — голова скрыта, кости рук у камеры (FP_ARMS, FP_ARMS_INTERACT,
// WATCH_INTERACT висят в воздухе и видны сквозь стены, плечи и манжеты тела тянутся к камере — тестер). Пока камера не
// у неё, вся её модель скрыта (голова — тоже подмодели, флаг sub+0x90); в фоторежиме с галочкой «Аманда в кадре» —
// только руки.
// Флаги берём КАЖДЫЙ РАЗ заново по списку частей: адрес флагов у части не постоянный — игра переиспользует эти ячейки,
// и запомненный адрес через время указывал на части других персонажей (было: у андроида остались одни глаза, у Чужого
// пропали купол и череп). Исходные флаги запоминаем по имени части.
static bool IsFpPart(const char* n) {
  if (!n) return false;
  const size_t l = strlen(n);
  return !strncmp(n, "FP_", 3) || (l > 9 && !strcmp(n + l - 9, "_INTERACT"));
}
static std::unordered_map<std::string, uint8_t> s_partOrig;  // имя части -> флаги до того, как мод её спрятал
static addr s_partsOf = 0;
static int s_partsMode = 0;   // 0 — все как были, 1 — скрыты руки, 2 — скрыта вся модель
static int s_devParts = -1;   // отладка (amodel 0/1): 2 — всё скрыть, -1 — как положено
static ms_t s_partsAt = 0;
static void ApplyParts(int mode) {  // поток игры
  ForSubmodels(player, [&](addr sub) {
    const char* n = SubName(sub);
    const bool hide = mode == 2 || (mode == 1 && IsFpPart(n));
    const addr fl = Fn<SubFlagsFn>(va::SubFlags)(sub);
    if (!fl) return;
    const auto it = s_partOrig.find(n);
    if (hide) {
      if (it == s_partOrig.end()) s_partOrig.emplace(n, rd<uint8_t>(fl + 7));
      if (rd<uint8_t>(fl + 7)) wr<uint8_t>(fl + 7, 0);
    } else if (it != s_partOrig.end()) {
      wr<uint8_t>(fl + 7, it->second);
      s_partOrig.erase(it);
    }
  });
  if (mode == 0) s_partOrig.clear();  // чего в модели уже нет — того и не вернуть
}
// ---- голова Чужого в виде из головы: череп (SKULL01), купол (SHELL01) и crusher — отдельные части модели, их прячем так
// же, как части Аманды (флаг +7, прежние флаги — по имени). Шея и пасть — в общей модели ALIEN, отдельно их не спрятать:
// их убирает сама камера — поправка «вперёд» в разделе «Камера» ----
static bool IsAlienHeadPart(const char* n) { return n && (strstr(n, "SKULL") || strstr(n, "SHELL") || !strcmp(n, "crusher")); }
static std::unordered_map<std::string, uint8_t> s_headOrig;
static addr s_headOf = 0;
static bool s_headHidden = false;
static void ApplyAlienHead(addr ch, bool hide) {  // поток игры
  ForSubmodels(ch, [&](addr sub) {
    const char* n = SubName(sub);
    if (!IsAlienHeadPart(n)) return;
    const addr fl = Fn<SubFlagsFn>(va::SubFlags)(sub);
    if (!fl) return;
    const auto it = s_headOrig.find(n);
    if (hide) {
      if (it == s_headOrig.end()) s_headOrig.emplace(n, rd<uint8_t>(fl + 7));
      if (rd<uint8_t>(fl + 7)) wr<uint8_t>(fl + 7, 0);
    } else if (it != s_headOrig.end()) {
      wr<uint8_t>(fl + 7, it->second);
      s_headOrig.erase(it);
    }
  });
  if (!hide) s_headOrig.clear();
}
static void AlienHeadTick() {  // поток игры, вместе с частями Аманды
  if (npc != s_headOf) s_headOrig.clear(), s_headOf = npc, s_headHidden = false;
  if (!npc || !CamEntityOk(npc)) return;
  const bool want = camMode == 1 && fpHideHead && !hugger && !photo.on;
  if (want == s_headHidden && !want) return;
  Safe([&] { ApplyAlienHead(npc, want); });  // пока спрятана — каждый раз: игра могла вернуть флаг
  if (want != s_headHidden) Log("alien head: %s", want ? "hidden (head view)" : "back");
  s_headHidden = want;
}
void FpArmsForget(addr ch) {  // Аманду (или Чужого) разрушают: их части больше не наши
  if (ch && ch == s_partsOf) s_partOrig.clear(), s_partsOf = 0, s_partsMode = 0;
  if (ch && ch == s_headOf) s_headOrig.clear(), s_headOf = 0, s_headHidden = false;
}
static void FpArmsTick() {  // поток игры, не чаще раза в кадр
  const ms_t t = NowMs();
  if (t - s_partsAt < 12) return;
  s_partsAt = t;
  if (player != s_partsOf) s_partOrig.clear(), s_partsOf = player, s_partsMode = 0;
  AlienHeadTick();
  if (!player || !CamEntityOk(player)) return;
  const bool want = camMode != 0 || photo.on || PupOwnsPlayer();
  int mode = !want ? 0 : (photo.on && photoPost.showAmanda) || PupOwnsPlayer() ? 1 : 2;  // кукла другого игрока — видна, без FP-рук
  if (s_devParts >= 0) mode = s_devParts;
  if (mode == 0 && s_partOrig.empty()) {
    s_partsMode = 0;
    return;
  }
  Safe([&] { ApplyParts(mode); });
  if (mode != s_partsMode) {
    s_partsMode = mode;
    Log("amanda: %s", mode == 2 ? "whole model hidden" : mode == 1 ? "first-person arms hidden" : "model back as it was");
  }
}
static void DevModelTick() {
  if (const addr who = devPartsOf.exchange(0)) Safe([who] {
      int i = 0;
      ForSubmodels(who, [&](addr sub) {
        const addr fl = Fn<SubFlagsFn>(va::SubFlags)(sub);
        uint8_t h = 0;
        SafeRead(sub + 0x90, h);
        Log("part %d of %08X: %s flags %02X head %X", i++, unsigned(who), SubName(sub), fl ? unsigned(rd<uint8_t>(fl + 7)) : 0xffu, h & 0xf);
      });
    });
  const int m = devAmandaModel.exchange(-1);
  if (m == 0 || m == 1) s_devParts = m == 0 ? 2 : -1;
  if ((m == 3 || m == 4) && player) Safe([m] {  // 3 <имя> — спрятать подмодель, 4 <имя> — показать (флаги 0 / 0x0B)
      ForSubmodels(player, [m](addr sub) {
        if (strcmp(SubName(sub), devPartName)) return;
        const addr fl = Fn<SubFlagsFn>(va::SubFlags)(sub);
        if (fl) wr<uint8_t>(fl + 7, m == 3 ? 0 : 0x0B);
        Log("amanda part %s -> %s", devPartName, m == 3 ? "hidden" : "shown");
      });
    });
  if (m == 2 && player) Safe([] {
      ForSubmodels(player, [](addr sub) {
        const addr fl = Fn<SubFlagsFn>(va::SubFlags)(sub);
        Log("amanda part %s flags %02X", SubName(sub), fl ? unsigned(rd<uint8_t>(fl + 7)) : 0xffu);
      });
    });
  const int c = devCamReq.exchange(-1);
  if (c >= 0) ActCamera(c);
  const int ph = devPhotoReq.exchange(-1);
  if (ph >= 0) ActPhoto(ph != 0);
}
static void OnCamTick(Invocation&) {
  g_gameTickAt = NowMs();  // игра идёт (на паузе эти апдейты не зовутся — EscTick в control.cpp)
  DevModelTick();
  FpArmsTick();
  CamDo();
  KillCamTick();
  if (const addr e = g_devCodeCam.exchange(0)) Safe([&] {
      const addr G = CamG(), code = rp(G + 0x1e0);
      SetBoneTarget(code, e);
      CamSetEntity(code, e);
      CamActivate(G, code);
      Log("dev: code camera on %08X", unsigned(e));
    });
  PickWorld();
}

// ---------------- настройка камеры: колесо мыши (и меню), сохраняется в .ini ----------------
// за спиной: колесо — ближе/дальше, Shift+колесо — выше/ниже, Ctrl+колесо — левее/правее, средняя кнопка — сброс;
// из головы: колесо — вперёд/назад, Shift — выше/ниже, Ctrl — левее/правее, средняя кнопка — сброс
// s_cur — то, что сейчас стоит в камере за спиной: плавно догоняет camCfg (колесо двигает цель, камера доезжает)
// «кадр атаки» лицехвата. Пока жив прыжок (и лицехват держится за жертву), follow-камера не рисует уровень — ни за
// лицехватом, ни за жертвой, ни за Амандой (проверено снимками кадра), а камера «из кости» рисует. Поэтому на время атаки —
// камера «из кости» (code_controlled_camera) на жертву, а её позицию и взгляд пишем сами, как фоторежим: вокруг
// лицехвата, курс — мышью (курс Аманды), луч Havok не пускает в стены
void CamKillCam(addr victim) { s_killCamWant = victim; }
// поток игры (деструктор персонажа): камера «из кости» не должна остаться на разрушаемом — при выгрузке уровня её
// поведение BoneAttached ищет кость у мёртвого объекта (вылет в FUN_006b3100, проверено)
// Персонаж разрушается: камеры игры, которые мод навёл на него (follow-камера Аманды, камера «из кости»), не должны
// держать висячий указатель. При выгрузке уровня игра разбирает массив объектов камер, и сброс FUN_0042c9f0 ищет кость
// у цели камеры (+0x148) — у уже разрушенного Чужого это вылет 0xA2C011 (01.10.2026: Esc -> загрузка чекпоинта/выход,
// стек 0042CC14 -> 006B3135 -> 00676EEA, дважды за вечер). Возвращаем им Аманду, как в игре — без активации (она тоже
// ищет кость), а если и Аманда уже разбирается — пустую цель (сброс пустую цель пропускает).
static void CamDropEntity(addr ch) {
  if (!ch || ch == player) return;
  Safe([&] {
    const addr G = CamG();
    if (!G) return;
    const addr back = player && CamEntityOk(player) ? player : 0;
    for (const addr cam : { rp(G + 0x1dc), rp(G + 0x1e0) }) {
      if (!cam) continue;
      ForBehaviours(cam, H_BONE, [&](addr b) { if (rp(b + 0xe4) == ch) wr<addr>(b + 0xe4, back); });
      if (rp(cam + 0x148) != ch) continue;
      CamSetEntity(cam, back);
      Log("camera %08X: its target %08X is destroyed — %s", unsigned(cam), unsigned(ch), back ? "back on Amanda" : "target cleared");
    }
  });
}
void CamOnAnyDtor(addr ch) {
  CamDropEntity(ch);
  if (!ch || (ch != s_killCamOn && ch != s_killCamWant.load())) return;
  s_killCamWant = 0;
  if (ch != s_killCamOn) return;
  Safe([&] {
    const addr G = CamG(), code = rp(G + 0x1e0), follow = rp(G + 0x1dc);
    SetBoneTarget(code, 0);
    const addr ent = hugger && hugger != ch ? hugger : player;
    if (ent && ent != ch && !GameStalled() && CamEntityOk(ent)) {  // выгрузка: активация ищет кость — не трогаем
      CamSetEntity(code, ent);
      CamSetEntity(follow, ent);
      CamActivate(G, follow);
    }
  });
  s_killCamOn = 0;
  s_kcHave = false;
  Log("camera: attack view target destroyed — back to the follow camera");
}
static void KillCamPose() {  // поток игры
  s_kcHave = false;
  const addr h = CamTarget();
  V3 t{};
  float yaw = 0;
  if (!h || !Safe([&] { t = rv3(h + 0x70); if (player) yaw = rf(player + 0x174); })) return;
  if (!HugAttackFocus(t)) t.y += 0.1f;  // голова жертвы (там лицехват), иначе — сам лицехват
  const bool lying = HugAttackLying();   // жертва лежит — смотрим сверху
  const float d = lying ? 1.0f : 1.8f, up = lying ? 1.35f : 0.55f;
  V3 c{ t.x - std::sin(yaw) * d, t.y + up, t.z - std::cos(yaw) * d };
  V3 hit{};
  const V3 from{ t.x, t.y + (lying ? 0.5f : 0.f), t.z };  // луч — не из самой головы (упрётся в тело)
  if (RayHit(from, c, hit)) {  // стена между — ближе, чуть не доходя
    const float fx = c.x - from.x, fy = c.y - from.y, fz = c.z - from.z, L = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float hx = hit.x - from.x, hy = hit.y - from.y, hz = hit.z - from.z, H = std::sqrt(hx * hx + hy * hy + hz * hz);
    const float k = std::max(0.25f, H - 0.2f) / std::max(L, 0.01f);
    c = { from.x + fx * k, from.y + fy * k, from.z + fz * k };
  }
  const float fx = t.x - c.x, fy = t.y - c.y, fz = t.z - c.z, L = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (!(L > 0.01f) || !std::isfinite(L)) return;
  s_kcPos = c;
  s_kcYaw = std::atan2(fx, fz);
  s_kcPitch = std::asin(std::max(-1.f, std::min(1.f, fy / L)));
  s_kcHave = true;

}
static void KillCamWrite(addr cam, int from, int to) {
  if (!s_kcHave) return;
  const float sy = std::sin(s_kcYaw / 2), cy = std::cos(s_kcYaw / 2), sp = std::sin(-s_kcPitch / 2), cp = std::cos(-s_kcPitch / 2);
  const float qx = cy * sp, qy = sy * cp, qz = -sy * sp, qw = cy * cp;  // курс (Y), потом тангаж (X) — как в фоторежиме
  for (int k = from; k <= to; k++) {
    const addr s = cam + k * 0x28;
    wf(s + 0x04, qx), wf(s + 0x08, qy), wf(s + 0x0c, qz), wf(s + 0x10, qw);
    wv3(s + 0x14, s_kcPos.x, s_kcPos.y, s_kcPos.z);
  }
}
// ---------------- свободный вид из головы ----------------
// Камера «из кости» (BoneAttached Head1) сама ставит кадр у головы Чужого и поворачивает его вместе с головой — «прибита».
// В свободном виде поворот наш: мышь (Raw Input, input_block.cpp) крутит fpYaw/fpPitch, как камеру за спиной; позиция —
// та, что посчитала кость (блок 1 перед расчётом скорости камеры), но сглаженная: голова качается при шаге. Пишем кадр там
// же, где кадр атаки лицехвата: перед скоростью камеры (FUN_00429ad0) и перед рендером (FUN_00432300).
bool FpFreeActive() { return camMode == 1 && fpFree.load() && !photo.on && !hugger && npc; }
static bool FpNearActive() { return camMode == 1 && fpHideHead && !photo.on && !hugger && npc; }  // вид из головы Чужого
float ViewYaw() {
  if (FpFreeActive() && fpHave) return fpYaw;
  float y = 0;
  if (player) Safe([&] { y = rf(player + 0x174); });
  return y;
}
static V3 s_fpPos{}, s_fpBone{};
static bool s_fpPosHave = false, s_fpFrameDone = false;
static int s_fpWasCam = 0;
// позиция: не сама кость Head1 (она качается при шаге, мотается при осмотре), а точка головы, привязанная к телу, — средняя
// позиция головы в осях тела, медленно подстраивается (~1.5 с · влияние) и стоит, пока Чужой осматривается; кость
// подмешивается с весом fpHeadInfluence (1 — прямо за костью, как раньше). При влиянии 0 точка не связана с телом вовсе:
// смещение головы замерло (как при входе в вид) и поворачивается по взгляду камеры, а не по курсу тела — тело под камерой
// разворачивается, качается, осматривается, а камера стоит; двигается только вместе с самим Чужим (его позиция).
// Между 0 и 1 — курс для смещения от взгляда к телу.
static V3 s_fpLocalAvg{};
static bool s_fpLocalHave = false;
static void FpFreeUpdate(addr cam) {  // раз в кадр: мышь -> взгляд, кость и тело -> позиция
  const int mx = fpMouseDX.exchange(0), my = fpMouseDY.exchange(0);
  const float body = rf(npc + 0x174);
  if (!fpHave) {  // вход в вид: смотрим туда же, куда смотрела камера за спиной (курс Аманды), иначе — куда смотрит Чужой
    fpYaw = player ? rf(player + 0x174) : body;
    fpPitch = 0;
    fpHave = true;
    s_fpPosHave = false;
    s_fpLocalHave = false;
  }
  fpYaw = WrapPi(fpYaw + mx * fpSens * 0.0022f);
  fpPitch = std::clamp(fpPitch - my * fpSens * 0.0022f, -1.35f, 1.35f);
  {  // резкий разворот на 180 (control.cpp): камера поворачивается вместе с телом
    static float last = 0.f;
    static bool have = false;
    if (turnFollowBody.load()) {
      if (have) fpYaw = WrapPi(fpYaw + WrapPi(body - last));
      last = body, have = true;
    } else {
      have = false;
    }
  }
  static ms_t s_flipUntil = 0;  // на ходу — камеру сразу назад; предел поворота снят, пока тело не догонит (до 2 с)
  if (fpFlipReq.exchange(false)) fpYaw = WrapPi(fpYaw + kPi), s_flipUntil = NowMs() + 2000;
  // как у Аманды: шея не крутится на 360 — дальше предела от курса тела взгляд не уходит, ждём, пока довернётся тело
  // (разворот на месте); при осмотре (LOOK) голова уходит почти назад
  const float lim = (alienLooking ? 170.f : std::clamp(fpYawLimit, 30.f, 180.f)) / 57.2958f;
  if (s_flipUntil && (NowMs() > s_flipUntil || std::fabs(WrapPi(fpYaw - body)) <= lim)) s_flipUntil = 0;
  if (lim < 3.1f && !s_flipUntil) {
    const float rel = WrapPi(fpYaw - body);
    if (rel > lim) fpYaw = WrapPi(body + lim);
    else if (rel < -lim) fpYaw = WrapPi(body - lim);
  }
  const V3 b = rv3(cam + 0x28 + 0x14);  // блок 1: позиция, которую только что поставила кость (с поправками из меню)
  const V3 p = rv3(npc + 0x70);
  if (!std::isfinite(b.x) || !std::isfinite(p.x)) return;
  s_fpBone = b;
  const float fx = std::sin(body), fz = std::cos(body), rx = std::cos(body), rz = -std::sin(body);  // вперёд / вправо
  const V3 d{ b.x - p.x, b.y - p.y, b.z - p.z };
  const V3 local{ d.x * rx + d.z * rz, d.y, d.x * fx + d.z * fz };  // голова в осях тела: вправо, вверх, вперёд
  const float w = std::clamp(fpHeadInfluence, 0.f, 1.f);
  static float lastHead[3] = { 1e9f, 1e9f, 1e9f };
  if (lastHead[0] != camCfg.head[0] || lastHead[1] != camCfg.head[1] || lastHead[2] != camCfg.head[2]) {
    for (int i = 0; i < 3; i++) lastHead[i] = camCfg.head[i];
    s_fpLocalHave = false;  // при влиянии 0 точка замерла — без этого ползунки «вперёд/вверх/вбок» ничего не меняли
  }
  if (!s_fpLocalHave) {
    s_fpLocalAvg = local;
    s_fpLocalHave = true;
  } else if (!alienLooking) {
    const float k = 0.012f * w;  // ~1.5 с при 60 кадрах и влиянии 1; при 0 — замерло
    s_fpLocalAvg.x += (local.x - s_fpLocalAvg.x) * k;
    s_fpLocalAvg.y += (local.y - s_fpLocalAvg.y) * k;
    s_fpLocalAvg.z += (local.z - s_fpLocalAvg.z) * k;
  }
  const float ay = WrapPi(fpYaw + WrapPi(body - fpYaw) * w);  // курс для смещения: 0 — взгляд камеры, 1 — тело
  const float afx = std::sin(ay), afz = std::cos(ay), arx = std::cos(ay), arz = -std::sin(ay);
  const V3 anchor{ p.x + arx * s_fpLocalAvg.x + afx * s_fpLocalAvg.z, p.y + s_fpLocalAvg.y, p.z + arz * s_fpLocalAvg.x + afz * s_fpLocalAvg.z };
  const V3 t{ anchor.x + (b.x - anchor.x) * w, anchor.y + (b.y - anchor.y) * w, anchor.z + (b.z - anchor.z) * w };
  const float dx = t.x - s_fpPos.x, dy = t.y - s_fpPos.y, dz = t.z - s_fpPos.z;
  if (!s_fpPosHave || dx * dx + dy * dy + dz * dz > 4.f) {
    s_fpPos = t;
    s_fpPosHave = true;
  } else {  // лёгкое сглаживание того, что пришло от кости
    s_fpPos.x += dx * 0.7f;
    s_fpPos.z += dz * 0.7f;
    s_fpPos.y += dy * 0.4f;
  }
}
static void FpFreeWrite(addr cam, int from, int to) {
  const float sy = std::sin(fpYaw / 2), cy = std::cos(fpYaw / 2), sp = std::sin(-fpPitch / 2), cp = std::cos(-fpPitch / 2);
  const float qx = cy * sp, qy = sy * cp, qz = -sy * sp, qw = cy * cp;  // курс (Y), потом тангаж (X) — как в фоторежиме
  for (int k = from; k <= to; k++) {
    const addr s = cam + k * 0x28;
    wf(s + 0x04, qx), wf(s + 0x08, qy), wf(s + 0x0c, qz), wf(s + 0x10, qw);
    if (s_fpPosHave) wv3(s + 0x14, s_fpPos.x, s_fpPos.y, s_fpPos.z);
  }
}
// точка и направление взгляда текущей камеры (LOOK по камере, alien_extra.cpp); поток игры
bool ViewDir(V3& from, V3& dir) {
  if (FpFreeActive() && fpHave && s_fpPosHave) {
    const float cp = std::cos(fpPitch);
    from = s_fpPos;
    dir = { std::sin(fpYaw) * cp, std::sin(fpPitch), std::cos(fpYaw) * cp };
    return true;
  }
  bool ok = false;
  Safe([&] {
    const addr cam = rp(CamG() + 0x1d8);
    if (!cam) return;
    const addr s = cam + 0x28;  // блок 1 — то, что ушло в рендер
    const float x = rf(s + 0x04), y = rf(s + 0x08), z = rf(s + 0x0c), w = rf(s + 0x10);
    dir = { 2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y) };  // вперёд = q · (0,0,1)
    from = rv3(s + 0x14);
    ok = std::isfinite(dir.x) && std::isfinite(from.x) && std::fabs(dir.x) + std::fabs(dir.y) + std::fabs(dir.z) > 0.5f;
  });
  return ok;
}
static void FpFreeTrack() {  // вышли из вида из головы — в следующий раз начать заново; за спиной — туда же, куда смотрели
  const int c = camMode;
  if (s_fpWasCam == 1 && c != 1 && fpHave && fpFree && player) Safe([] { wf(player + 0x174, fpYaw); });
  if (c != 1) fpHave = false, s_fpPosHave = false;
  s_fpWasCam = c;
}

static void OnKcVelocityEnter(Invocation& inv) {
  if (FpFreeActive() && !s_killCamOn) {
    Safe([&] {
      if (inv.ecx != rp(CamG() + 0x1d8)) return;
      FpFreeUpdate(inv.ecx);
      s_fpFrameDone = true;
      FpFreeWrite(inv.ecx, 0, 1);
    });
    return;
  }
  if (!s_killCamOn || photo.on) return;
  Safe([&] { if (inv.ecx == rp(CamG() + 0x1d8)) KillCamWrite(inv.ecx, 0, 1); });
}
// ---- голова Чужого по костям (Havok hkaPose слоя 0: персонаж+0x3f0+0x230 -> +0 скелет, +4 локальная поза, +0x10 поза
// модели, hkQsTransform по 0x30: сдвиг, поворот, масштаб +0x20; скелет +0xc — родители int16, +0x1c — число костей) ----
static void ScaleSubtree(addr ch, int root, int mode) {
  const addr pose = rp(ch + 0x3f0 + 0x230);
  if (!pose) return;
  const addr skel = rp(pose), par = rp(skel + 0xc), local = rp(pose + 4), model = rp(pose + 0x10);
  const int n = int(rd<uint32_t>(skel + 0x1c));
  if (!par || !local || !model || root < 0 || root >= n || n > 400) return;
  static uint8_t in[400];
  for (int i = 0; i < n; i++) {
    const int p = rd<int16_t>(par + i * 2);
    in[i] = i == root || (p >= 0 && p < i && in[p]);
  }
  for (int i = 0; i < n; i++) {
    if (!in[i]) continue;
    if (mode != 1) wf(model + i * 0x30 + 0x20, 0.f), wf(model + i * 0x30 + 0x24, 0.f), wf(model + i * 0x30 + 0x28, 0.f);
    if (mode != 0 && i == root) wf(local + i * 0x30 + 0x20, 0.f), wf(local + i * 0x30 + 0x24, 0.f), wf(local + i * 0x30 + 0x28, 0.f);
  }
}
// кость шеи Чужого: имён костей в данных игры нет (вырезаны), поэтому — по скелету «ALIEN» из 127 костей: 85 — от груди
// (55) цепочка 85 -> 86 -> 87 -> 88 и мелкие кости пасти 89..94 (высота 1.6–1.9 м, впереди) — проба dev\probe\skeleton.py
static int AlienNeckBone(addr ch) {
  const addr pose = rp(ch + 0x3f0 + 0x230);
  if (!pose) return -1;
  const addr skel = rp(pose), par = rp(skel + 0xc);
  if (rd<uint32_t>(skel + 0x1c) != 127 || !par || rd<int16_t>(par + 85 * 2) != 55 || rd<int16_t>(par + 86 * 2) != 85) return -1;
  return 85;
}
// ---- шея и голова Чужого из головы — костями: в палитре скиннинга (FUN_0068e9a0, поза слоя 7) масштаб 0 у кости шеи 85
// и всех её детей (86..94: шея, голова, челюсти, зубы). Вершины этих костей стягиваются в их начала — не видны. Массив —
// синхронизированная поза модели самой анимации: на входе обнуляем, на выходе возвращаем как было — камера «из кости» и
// всё остальное видят обычную позу. Зовётся и из рабочих потоков рендера/анимации: без замка, свои буферы только для
// вызова с позой Чужого. ----
static uint8_t s_headBone[400];
static int s_headBoneN = 0;
static addr s_headSkel = 0;
static float s_palSave[32][12];  // hkQsTransform целиком: сдвиг, поворот, масштаб
static int s_palIdx[32], s_palN = 0, s_headInto = -1;
static addr s_palArr = 0;
static void OnPaletteEnter(Invocation& inv) {
  if (!fpNeckScale || !FpNearActive()) return;
  const addr ch = npc;
  const addr pose = rp(ch + 0x3f0 + va::AnimFinalPoseOff);
  const addr arr = inv.args[0];
  if (!pose || !arr || rp(pose + 0x10) != arr) return;
  const addr skel = rp(pose);
  const int n = int(inv.args[1]);
  if (skel != s_headSkel) {  // поддерево шеи — один раз на скелет
    s_headSkel = skel, s_headBoneN = 0;
    const int root = AlienNeckBone(ch);
    const addr par = rp(skel + 0xc);
    const int nb = int(rd<uint32_t>(skel + 0x1c));
    s_headInto = root >= 0 && par ? rd<int16_t>(par + root * 2) : -1;  // родитель шеи — грудь (55): туда и стягиваем
    if (root >= 0 && par && nb > 0 && nb <= 400) {
      for (int i = 0; i < nb; i++) {
        const int pp = rd<int16_t>(par + i * 2);
        s_headBone[i] = i == root || (pp >= 0 && pp < i && s_headBone[pp]);
        if (s_headBone[i]) s_headBoneN++;
      }
    }
    Log("head view: %d neck/head bones hidden in the skin palette (root %d, into %d)", s_headBoneN, root, s_headInto);
  }
  if (!s_headBoneN || n <= 0 || n > 400 || s_headInto < 0 || s_headInto >= n) return;
  // каждую кость шеи/головы — в начало груди и масштаб 0: вершины уходят в одну точку внутри тела (раньше каждая кость
  // стягивалась в своё начало — из головы были видны сплющенные голова и шея)
  const addr into = arr + s_headInto * 0x30;
  const float tx = rf(into), ty = rf(into + 4), tz = rf(into + 8), tw = rf(into + 12);
  g_palEditSeq.fetch_add(1);  // нечётное: поза Чужого сейчас с «пустой» шеей — тело для сети её не берёт (puppet.cpp)
  s_palN = 0;
  for (int i = 0; i < n && s_palN < 32; i++) {
    if (!s_headBone[i]) continue;
    const addr s = arr + i * 0x30;
    s_palIdx[s_palN] = i;
    for (int k = 0; k < 12; k++) s_palSave[s_palN][k] = rf(s + k * 4);
    s_palN++;
    wf(s, tx), wf(s + 4, ty), wf(s + 8, tz), wf(s + 12, tw);
    wf(s + 0x20, 0.f), wf(s + 0x24, 0.f), wf(s + 0x28, 0.f);
  }
  s_palArr = arr;
  inv.text[0] = 1;
}
static void OnPaletteLeave(Invocation& inv) {
  if (inv.text[0] != 1 || !s_palArr) return;
  for (int k = 0; k < s_palN; k++) {
    const addr s = s_palArr + s_palIdx[k] * 0x30;
    for (int j = 0; j < 12; j++) wf(s + j * 4, s_palSave[k][j]);
  }
  s_palN = 0, s_palArr = 0;
  g_palEditSeq.fetch_add(1);
}
// ---- ближняя плоскость отсечения камеры: игра при отрисовке берёт её из объекта камеры +0x4c (FUN_00432300,
// ограничивает сверху [0x011bed34]); в виде из головы отодвигаем её — шея, пасть, купол у самой камеры не рисуются ----
static addr s_nearCam = 0;
static float s_nearOrig = 0.f;
static void NearClipTick(addr G) {
  const addr cam = rp(G + 0x1d8);
  const bool want = FpNearActive() && cam == rp(G + 0x1e0);
  if (s_nearCam && (!want || cam != s_nearCam)) {  // вернуть камере её плоскость
    Safe([] { wf(s_nearCam + 0x4c, s_nearOrig); });
    s_nearCam = 0;
  }
  if (!want) return;
  if (!s_nearCam) s_nearCam = cam, s_nearOrig = rf(cam + 0x4c);
  wf(cam + 0x4c, std::max(s_nearOrig, std::clamp(fpNearClip, 0.02f, 1.f)));
}
// Из головы (камера «из кости», у неё +0x155/+0x156 = 0) игра не берёт комнату у Чужого, а ищет зону запросом в физику от
// позиции камеры — а камера внутри его головы. За спиной (камера следования) комната — его (+0x1b0). Из головы звук был
// глухой (тестер): ставим ту же комнату, что за спиной, и пишем в лог, отличалась ли найденная зона.
static void OnKcSubmitLeave(Invocation& inv) {
  if (camMode != 1 || photo.on || hugger || !npc || s_killCamOn || inv.ecx != CamG()) return;
  const uint32_t room = rd<uint32_t>(npc + 0x1b0);
  if (!room || room == 0xFFFFFFFFu) return;
  static int s_last = -1;
  const addr mgr = rp(VA(va::PovManager)), pov = mgr ? rp(mgr + 0x8c) : 0;
  const uint32_t zone = pov ? rd<uint32_t>(pov + 0x330) : 0;
  const int same = zone == room;
  if (same != s_last) {
    s_last = same;
    Log("head view: camera zone %08X, alien room %08X — %s", unsigned(zone), unsigned(room), same ? "same" : "different, using the alien's room");
  }
  wr<uint32_t>(VA(va::RenderRoom), room);
  wr<uint8_t>(VA(va::RenderRoomForced), 1);
}
static void OnKcSubmitEnter(Invocation& inv) {
  FpFreeTrack();
  Safe([&] { if (inv.ecx == CamG()) NearClipTick(inv.ecx); });
  if (npc && NowMs() < devHeadUntil.load()) Safe([] { ScaleSubtree(npc, devHeadBone.load(), devHeadMode.load()); });
  if (FpFreeActive() && !s_killCamOn) {
    Safe([&] {
      const addr G = inv.ecx;
      if (G != CamG()) return;
      addr cam = rp(G + 0x1d8);
      if (rd<uint8_t>(G + 0x221) && rp(G + 0x1f0)) cam = rp(G + 0x1f0);
      if (!cam) return;
      if (!s_fpFrameDone) FpFreeUpdate(cam);  // скорость камеры в этом кадре не считалась — кость ещё в блоке 1
      s_fpFrameDone = false;
      FpFreeWrite(cam, 0, 2);
    });
    return;
  }
  if (!s_killCamOn || photo.on) return;
  Safe([&] {
    const addr G = inv.ecx;
    if (G != CamG()) return;
    KillCamPose();
    addr cam = rp(G + 0x1d8);
    if (rd<uint8_t>(G + 0x221) && rp(G + 0x1f0)) cam = rp(G + 0x1f0);
    if (cam) KillCamWrite(cam, 0, 2);
  });
}
static void FollowFromCfg() {  // вне шахты: fOff/fTgt из настройки игрока
  if (ductOn) return;
  if (!s_curInit) s_cur = camCfg, s_curInit = true;
  const CamCfg& c = s_cur;
  const float o[3] = { c.side, c.height, -c.dist }, t[3] = { 0.f, c.height - (camIsHugger ? 0.45f : 0.7f), 2.0f };
  memcpy(fOff, o, sizeof fOff);
  memcpy(fTgt, t, sizeof fTgt);
}
static void HeadFromCfg() {
  if (addr b = BoneBeh()) {
    wf(b + 0xac, camCfg.head[0]);
    wf(b + 0xb0, camCfg.head[1]);
    wf(b + 0xb4, camCfg.head[2]);
  }
}
void CamCfgApply() {
  CamCfg& c = camCfg;
  c.dist = std::clamp(c.dist, camIsHugger ? 0.5f : 1.f, 8.f);  // лицехват маленький — камера может быть ближе и ниже
  c.height = std::clamp(c.height, camIsHugger ? 0.1f : 0.3f, 4.5f);
  c.side = std::clamp(c.side, -2.f, 2.f);
  for (float& h : c.head) h = std::clamp(h, -1.5f, 1.5f);
  // за спиной — доедет плавно (SmoothFollow), из головы — сразу
  if (camMode == 1) {
    Safe([] { HeadFromCfg(); });
    camWant = 1, camKeyAt = NowMs();  // переактивировать, чтобы поведение перечитало параметры
  }
}
void ActCamCfg(const CamCfg& c) {
  camCfg = c;
  CamCfgApply();
}
// Лицехват <-> Чужой: у каждого своя настройка камеры (лицехват маленький) — меняем местами
void CamUseHuggerCfg(bool on) {
  if (camIsHugger == on) return;
  std::swap(camCfg, camCfgOther);
  camIsHugger = on;
  ductOn = false;  // у лицехвата шахт нет; вернёмся к Чужому в шахте — DuctUpdate включит ближнюю камеру заново
  s_cur = camCfg, s_curInit = true;
  FollowFromCfg();
  ApplyFollow();
}
void ActCamReset(int cam) {
  const CamCfg d = camIsHugger ? HuggerCamDefault() : CamCfg{};
  if (cam == 2) camCfg.dist = d.dist, camCfg.height = d.height, camCfg.side = d.side;
  else memcpy(camCfg.head, d.head, sizeof d.head);
  CamCfgApply();
  Log("camera %d reset", cam);
}
static void SmoothFollow() {  // 16 мс, из MainTick
  if (!s_curInit) s_cur = camCfg, s_curInit = true;
  bool moved = false;
  auto step = [&](float& cur, float want) {
    const float d = want - cur;
    if (d == 0) return;
    cur = std::fabs(d) < 0.005f ? want : cur + d * 0.2f;
    moved = true;
  };
  step(s_cur.dist, camCfg.dist);
  step(s_cur.height, camCfg.height);
  step(s_cur.side, camCfg.side);
  if (!moved) return;
  FollowFromCfg();
  if (camMode == 2) ApplyFollow();
}
void CamOffsetKeys() {
  SmoothFollow();
  const int w = camWheel.exchange(0);
  static bool mmbWas = false;
  const bool mmb = Down(VK_MBUTTON), click = mmb && !mmbWas;
  mmbWas = mmb;
  if (camMode == 0 || photo.on || !g_hotkeysOn) return;
  if (click) return ActCamReset(camMode);
  if (!w) return;
  const float steps = w / 120.f;  // один щелчок колеса = 120
  const bool sh = Down(VK_SHIFT), ct = Down(VK_CONTROL);
  CamCfg& c = camCfg;
  if (camMode == 2) {
    if (sh) c.height += steps * 0.1f;
    else if (ct) c.side += steps * 0.1f;
    else c.dist -= steps * 0.25f;  // колесо от себя — ближе
  } else {
    float& h = c.head[sh ? 1 : ct ? 0 : 2];
    h += steps * 0.03f;
  }
  CamCfgApply();
}

// ---------------- is_first_person = beh+0x9c+0x5a: 0 только для камеры за спиной Чужого, иначе вернуть исходное ОДИН раз ----------------
static int s_fpOrig = -1;
static bool s_fpDirty = false;
static void FpKeeperTick() {
  addr b = FollowBehRaw();
  if (!b) return;
  addr f = b + 0x9c + 0x5a;
  if (s_fpOrig < 0) s_fpOrig = rd<uint8_t>(f);
  if (camMode == 2 || PhotoActive()) {  // в фоторежиме тоже: Аманду в кадре видно целиком
    if (rd<uint8_t>(f) != 0) wr<uint8_t>(f, 0), s_fpDirty = true;
  } else if (s_fpDirty) {  // постоянная запись ломает руки/трекер Аманды и логику укрытий
    wr<uint8_t>(f, uint8_t(s_fpOrig));
    s_fpDirty = false;
  }
}

void CameraShutdown(bool mayCallGame) {  // отцепление мода; mayCallGame — поток игры стоит (пауза), функции игры звать можно
  if (mayCallGame && player && s_partsOf == player && !s_partOrig.empty()) Safe([] { ApplyParts(0); });  // части Аманде
  if (mayCallGame && npc && s_headOf == npc && !s_headOrig.empty()) Safe([] { ApplyAlienHead(npc, false); });  // голова Чужому
  if (s_nearCam) Safe([] { wf(s_nearCam + 0x4c, s_nearOrig); }), s_nearCam = 0;  // ближняя плоскость камеры — как была
  if (camMode != 0 || camWant) {
    camWant = 0;
    if (mayCallGame) CamRestore("mod unloaded");
    else Log("unload: camera left on the alien — the game thread didn't take it back");
  }
  if (s_fpDirty) {
    if (addr b = FollowBehRaw()) Safe([&] { wr<uint8_t>(b + 0x9c + 0x5a, uint8_t(s_fpOrig)); });
    s_fpDirty = false;
  }
}

// ---------------- лучи ----------------
alignas(16) static float s_nFrom[4], s_nTo[4], s_nHit[4];
alignas(16) static float s_rFrom[4], s_rTo[4], s_rPos[4], s_rNrm[4];
alignas(16) static uint8_t s_rHit[16];
static bool HkRayHitIn(addr world, float x0, float y0, float z0, float x1, float y1, float z1) {
  s_rFrom[0] = x0, s_rFrom[1] = y0, s_rFrom[2] = z0, s_rFrom[3] = 0;
  s_rTo[0] = x1, s_rTo[1] = y1, s_rTo[2] = z1, s_rTo[3] = 0;
  s_rHit[0] = 0;
  Fn<HkRayFn>(va::HkRay)(0, addr(s_rHit), world, addr(s_rFrom), addr(s_rTo), addr(s_rPos), addr(s_rNrm));
  return s_rHit[0] != 0;
}
static bool HkRayHit(float x0, float y0, float z0, float x1, float y1, float z1) {
  return HkRayHitIn(hkWorld, x0, y0, z0, x1, y1, z1);
}

// ---------------- шахта: низкий потолок над Чужим -> ближняя камера (с гистерезисом 5 проверок) ----------------
// В hot.js проверялось из потока Frida; здесь — в потоке игры, из апдейта follow-камеры, раз в 150 мс.
static bool s_ductState = false;
static int s_ductRun = 0;
static ms_t s_ductAt = 0;
static void DuctUpdate() {
  ms_t t = NowMs();
  if (t - s_ductAt < 150 || !hkWorld) return;
  s_ductAt = t;
  V3 a = rv3(npc + 0x70);
  bool raw = HkRayHit(a.x, a.y + 0.4f, a.z, a.x, a.y + 1.9f, a.z);
  if (raw == s_ductState) s_ductRun = 0;
  else if (++s_ductRun >= 5) s_ductState = raw, s_ductRun = 0;
  if (s_ductState == ductOn) return;
  ductOn = s_ductState;
  Log("duct %d", int(ductOn));
  const float inOff[3] = { 0, 0.35f, -0.9f }, inTgt[3] = { 0, 0.3f, 1.5f };
  if (ductOn) {
    memcpy(fOff, inOff, sizeof fOff);
    memcpy(fTgt, inTgt, sizeof fTgt);
  }
  FollowFromCfg();  // из шахты — обратно к настройке игрока
  ApplyFollow();
}

// ---------------- столкновения камеры ----------------
// camCollide: 0 — выкл; 1 — мягкая (по умолчанию): только физика (стены, потолки, предметы), три луча от головы
// персонажа к камере (по центру и по бокам на 0.3 м) — камера подъезжает по «среднему» из трёх, поэтому задетая краем
// труба или угол её не дёргает, и ближе ~1 м к голове (или 30% дистанции) она не подходит; 2 — строгая, как раньше:
// ещё и навмеш по горизонтали (край навмеша — не обязательно стена: у ограждений и обрывов камеру тянуло внутрь и вниз,
// «в ноги»), один луч, до 15% дистанции.
static float s_sNav = 1, s_sPhys = 1;
static ms_t s_colLog = 0;
static unsigned s_colN = 0, s_colNav = 0, s_colPhys = 0;
static void Collide(addr b) {
  if (camCollide == 0) {
    s_sNav = s_sPhys = 1;
    return;
  }
  const bool strict = camCollide == 2;
  addr ent = rp(rp(CamG() + 0x1dc) + 0x148);
  if (!ent) return;
  float cx = rf(b + 0x1c), cy = rf(b + 0x20), cz = rf(b + 0x24);        // позиция камеры
  float bx = rf(b + 0x1c0), by0 = rf(b + 0x1c4), bz = rf(b + 0x1c8);   // база (сущность камеры)
  s_colN++;
  float px = cx, py = cy, pz = cz;
  // 1) навмеш по горизонтали — только в строгом режиме
  if (strict) {
    addr nav = Fn<NavCtxOfFn>(va::NavCtxOf)(rp(rp(VA(va::GameGlobals)) + 0x24));
    int filt = Fn<NavFilterFn>(va::NavFilter)(rp(ent + 0x64) + 4);
    s_nFrom[0] = bx, s_nFrom[1] = by0, s_nFrom[2] = bz;
    s_nTo[0] = cx, s_nTo[1] = by0, s_nTo[2] = cz;
    float dFull = std::hypot(cx - bx, cz - bz), tNav = 1;
    if (!Fn<NavRayFn>(va::NavRay)(nav, addr(s_nFrom), addr(s_nTo), addr(s_nHit), filt, 0)) {
      float dHit = std::hypot(s_nHit[0] - bx, s_nHit[2] - bz);
      tNav = (dHit - 0.4f) / std::max(dFull, 0.001f);
      s_colNav++;
    }
    tNav = std::max((hugger ? 0.4f : 0.8f) / std::max(dFull, 0.001f), std::min(1.f, tNav));
    s_sNav = tNav < s_sNav ? s_sNav + (tNav - s_sNav) * 0.3f : s_sNav + (tNav - s_sNav) * 0.04f;
    if (std::fabs(tNav - s_sNav) < 0.02f) s_sNav = tNav;
    px = bx + (cx - bx) * s_sNav, pz = bz + (cz - bz) * s_sNav, py = cy - (cy - by0) * (1 - s_sNav) * 0.3f;
  } else {
    s_sNav = 1;
  }
  // 2) физика: от «головы» персонажа к камере
  if (hkWorld) {
    const float by = by0 + (hugger ? 0.3f : 1.2f), dx = px - bx, dy = py - by, dz = pz - bz;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(len > 0.05f)) len = 0.05f;
    const float ux = dx / len, uy = dy / len, uz = dz / len;
    auto rayT = [&](float ox, float oz) {  // доля пути до препятствия (1 — свободно); ox/oz — сдвиг конца луча вбок
      const float ex = px + ox + ux * 0.3f, ey = py + uy * 0.3f, ez = pz + oz + uz * 0.3f;
      if (!HkRayHit(bx, by, bz, ex, ey, ez)) return 1.f;
      const float hx = s_rPos[0] - bx, hy = s_rPos[1] - by, hz = s_rPos[2] - bz;
      return std::min(1.f, (std::sqrt(hx * hx + hy * hy + hz * hz) - 0.3f) / len);
    };
    float tPhys = rayT(0, 0);
    if (!strict) {  // медиана трёх лучей: одиночное задевание краем камеру не тянет
      const float hl = std::hypot(ux, uz);
      const float sx = hl > 0.01f ? uz / hl * 0.3f : 0.3f, sz = hl > 0.01f ? -ux / hl * 0.3f : 0.f;
      const float t1 = rayT(sx, sz), t2 = rayT(-sx, -sz);
      tPhys = std::max(std::min(tPhys, t1), std::min(std::max(tPhys, t1), t2));
    }
    if (tPhys < 0.999f) s_colPhys++;
    const float minT = strict ? 0.15f : std::max(0.3f, (hugger ? 0.4f : 1.f) / len);
    tPhys = std::max(minT, std::min(1.f, tPhys));
    const float in = strict ? 0.4f : 0.25f, out = strict ? 0.05f : 0.07f;
    s_sPhys = tPhys < s_sPhys ? s_sPhys + (tPhys - s_sPhys) * in : s_sPhys + (tPhys - s_sPhys) * out;
    if (std::fabs(tPhys - s_sPhys) < 0.02f) s_sPhys = tPhys;
    px = bx + dx * s_sPhys;
    py = by + dy * s_sPhys;
    pz = bz + dz * s_sPhys;
  }
  if (s_sNav < 0.999f || s_sPhys < 0.999f) wv3(b + 0x1c, px, py, pz);
  ms_t t = NowMs();
  if (t - s_colLog > 5000) {
    s_colLog = t;
    Log("col (%s) n=%u nav=%u phys=%u sNav=%.2f sPhys=%.2f", strict ? "strict" : "soft", s_colN, s_colNav, s_colPhys, s_sNav, s_sPhys);
  }
}

// ---------------- за кулисами ----------------
// Движок не обновляет камеру, которая следит за персонажем «вне мира» (картинка замирает). Поэтому, пока Чужой
// за кулисами, камера формально следит за Амандой (она в мире — движок камеру обновляет и считает орбиту по мыши),
// а мы каждый кадр переносим эту орбиту на Чужого. Сам Чужой за кулисами двигается по-настоящему (Havok-тело, vent.cpp),
// так что камера ездит вместе с ним. Вышел — камера снова следит за Чужим напрямую.
static bool Backstage() { return vent.stay || alienBackstage; }
static void BackstageCamera(addr b) {
  V3 a = rv3(npc + 0x70), base = rv3(b + 0x1c0), c = rv3(b + 0x1c);
  float ox = c.x - base.x, oy = c.y - base.y, oz = c.z - base.z;  // орбита, которую игра посчитала вокруг Аманды
  float r = std::sqrt(ox * ox + oy * oy + oz * oz);
  if (!(r > 0.1f && r < 15.f)) ox = 0, oy = 2.f, oz = -3.f;
  if (ox * ox + oz * oz > 0.01f) {  // курс камеры — для WASD в вентиляции
    bsViewYaw = std::atan2(-ox, -oz);
    bsViewValid = true;
  }
  float px = a.x + ox, py = a.y + oy, pz = a.z + oz;
  wv3(b + 0x1c0, a.x, a.y, a.z);
  wv3(b + 0x1c, px, py, pz);
  addr cam = rp(b + 0x34);  // объект камеры: позиция +0x14/+0x3c/+0x64, точка взгляда +0x10c
  if (!cam) return;
  for (addr o : { 0x14, 0x3c, 0x64 }) wv3(cam + o, px, py, pz);
  wv3(cam + 0x10c, a.x, a.y + 1.3f, a.z);
  wr<uint8_t>(cam + 0x153, 1);
}
static bool s_proxy = false;
static ms_t s_proxyAt = 0;
static void OnFollowUpdateEnter(Invocation& inv) {
  const addr tgt = CamTarget();
  if (camMode != 2 || !tgt || PhotoActive()) return;
  ms_t t = NowMs();
  if (t - s_proxyAt < 150) return;
  s_proxyAt = t;
  const bool back = !hugger && Backstage() && player;
  const addr want = back ? player : tgt;
  addr follow = rp(CamG() + 0x1dc), cam = rp(inv.ecx + 0x34);
  if (rp(follow + 0x148) != want) CamSetEntity(follow, want);
  if (cam && cam != follow && rp(cam + 0x148) != want) CamSetEntity(cam, want);
  if (back != s_proxy) {
    s_proxy = back;
    Log(back ? "backstage cam: on (orbit moved to the alien)" : "backstage cam: off");
  }
}
static void OnFollowUpdateLeave(Invocation& inv) {
  addr b = inv.ecx;
  if (b != FollowBehRaw()) return;
  if (PhotoActive()) {  // фоторежим (photo.cpp) — камера полностью наша
    if (!Safe([&] { PhotoCamera(b); })) Log("err photo cam");
    return;
  }
  if (camMode != 2 || !CamTarget()) return;
  if (!hugger && Backstage()) {
    if (!Safe([&] { BackstageCamera(b); })) Log("err backstage cam");
    return;
  }
  if (!hugger && !Safe([&] { DuctUpdate(); })) Log("err duct");  // шахты — только у Чужого
  if (!Safe([&] { Collide(b); })) Log("err col");
}

// ---------------- лучи для выхода из вентиляции (vent.cpp) — только из потока игры ----------------
bool RayDown(float x, float y, float z, float len, float& hitY) {
  if (!hkWorld) return false;
  if (!HkRayHit(x, y, z, x, y - len, z)) return false;
  hitY = s_rPos[1];
  return true;
}
bool RayHit(const V3& from, const V3& to, V3& hit) {  // фоторежим: точка фокуса
  if (!hkWorld) return false;
  if (!HkRayHit(from.x, from.y, from.z, to.x, to.y, to.z)) return false;
  hit = { s_rPos[0], s_rPos[1], s_rPos[2] };
  return true;
}
bool NavClear(const V3& from, const V3& to) {
  if (!player) return false;
  addr nav = Fn<NavCtxOfFn>(va::NavCtxOf)(rp(rp(VA(va::GameGlobals)) + 0x24));
  int filt = Fn<NavFilterFn>(va::NavFilter)(rp(player + 0x64) + 4);
  s_nFrom[0] = from.x, s_nFrom[1] = from.y, s_nFrom[2] = from.z;
  s_nTo[0] = to.x, s_nTo[1] = to.y, s_nTo[2] = to.z;
  return Fn<NavRayFn>(va::NavRay)(nav, addr(s_nFrom), addr(s_nTo), addr(s_nHit), filt, 0) != 0;
}

// Мир Havok пересоздаётся с каждым уровнем (старый — освобождён: лучи в него падали, «err duct / err col»), а миров
// бывает два, и игра пускает лучи то в один, то в другой (прошлая версия переключалась туда-сюда 60 раз в секунду).
// 0xb48890 — внутренний луч самой HkRay: мир виден, только когда игра (или мод) пускает лучи. Поэтому хук лишь
// запоминает миры, встреченные после загрузки уровня (без замка), а PickWorld в потоке игры раз в секунду выбирает:
// один — его; несколько — тот, где луч от Аманды вниз попадает в пол; выбранный держим, пока он годится.
static constexpr int kWorlds = 4;
static std::atomic<addr> s_world[kWorlds];
static std::atomic<uint32_t> s_worldSeen[kWorlds];
static void OnHkWorldEnter(Invocation& inv) {  // горячий: без замка
  const addr w = inv.ecx;
  if (!w) return;
  const uint32_t now = uint32_t(NowMs());
  int slot = -1;
  for (int i = 0; i < kWorlds; i++) {
    const addr c = s_world[i].load(std::memory_order_relaxed);
    if (c == w) {
      s_worldSeen[i].store(now, std::memory_order_relaxed);
      return;
    }
    if (slot < 0 && (!c || now - s_worldSeen[i].load(std::memory_order_relaxed) > 3000)) slot = i;
  }
  if (slot < 0) return;
  s_worldSeen[slot].store(now, std::memory_order_relaxed);
  s_world[slot].store(w, std::memory_order_relaxed);
}
static bool WorldLive(addr w) {  // встречался недавно (30 с): миры старого уровня отсеивает CameraWorldReset
  const uint32_t now = uint32_t(NowMs());
  for (int i = 0; i < kWorlds; i++)
    if (w && s_world[i].load() == w && now - s_worldSeen[i].load() < 30000) return true;
  return false;
}
void CameraWorldReset() {  // смена уровня: миры старого освобождаются
  for (int i = 0; i < kWorlds; i++) s_world[i] = 0, s_worldSeen[i] = 0;
  hkWorld = 0;
}
void CameraAdoptWorld(addr w) {  // мир от прошлой версии мода (reload.cpp)
  if (!w) return;
  hkWorld = w;
  s_worldSeen[0] = uint32_t(NowMs());
  s_world[0] = w;
  Log("hkpWorld %08X (from the previous version)", unsigned(w));
}
static void PickWorld() {  // поток игры, раз в секунду (и сразу, если мира нет)
  static ms_t at = 0;
  const ms_t t = NowMs();
  if (hkWorld && t - at < 1000) return;
  at = t;
  addr live[kWorlds];
  int n = 0;
  for (int i = 0; i < kWorlds; i++) {
    const addr w = s_world[i].load();
    if (w && WorldLive(w)) live[n++] = w;
  }
  if (!n) return;  // игра лучей ещё не пускала — оставляем, что есть
  addr pick = 0;
  if (n == 1) {
    pick = live[0];
  } else {  // несколько: где под Амандой пол
    V3 p{};
    const addr ref = player ? player : CamTarget();
    const bool hasP = ref && Safe([&] { p = rv3(ref + 0x70); });
    addr hit = 0;
    bool curHits = false;
    for (int i = 0; i < n && hasP; i++)
      Safe([&] {
        if (!HkRayHitIn(live[i], p.x, p.y + 1.f, p.z, p.x, p.y - 5.f, p.z)) return;
        if (!hit) hit = live[i];
        if (live[i] == hkWorld) curHits = true;
      });
    pick = curHits ? hkWorld : hit ? hit : (WorldLive(hkWorld) ? hkWorld : live[0]);
  }
  if (pick != hkWorld) {
    Log("hkpWorld %08X (%d live)", unsigned(pick), n);
    hkWorld = pick;
  }
}

// звук: комнатная система берёт позицию слушателя от Чужого (подмена this у 0x689230 в вызове из 0x6be31f)
static void OnSoundListenerEnter(Invocation& inv) {
  const addr tgt = CamTarget();
  if (camMode && tgt && inv.ra == VA(va::SoundListenerRet)) inv.cpu->ecx = tgt + 0x3f0;
}


// деструктор Чужого = vtable[0]: вернуть камеру Аманде до того, как объект исчезнет
static addr s_dtorVt = 0;
static void OnAlienDtor(Invocation& inv) {
  // Аманду разрушают — уровень выгружается: дальше камеры на неё не направлять (активация follow-камеры ищет кость
  // «Head1» у своей цели — у освобождённой Аманды это вылет в FUN_006b3100, проверено)
  if (player && inv.ecx == player) {
    FpArmsForget(player);
    player = 0;
    camSaved = 0;
    Log("amanda destroyed — the level is unloading, cameras are left to the game");
  }
  CamOnAnyDtor(inv.ecx);
  HugOnAnyDtor(inv.ecx);
  if (hugger && inv.ecx == hugger) return HugOnDtor(inv.ecx);  // лицехват, за которого играешь (hugger.cpp)
  if (!npc || inv.ecx != npc) return;
  FpArmsForget(npc);  // спрятанные части головы — больше не наши
  dyingChar = npc;  // его узлы движения при разрушении не завершаем (control.cpp, OnMoveShutdownEnter)
  // Игра стоит (пауза, загрузка чекпоинта, выгрузка уровня): камеры не трогаем — у разбираемых персонажей скелет уже
  // обнулён (анимация+0x328 = 0), активация камеры ищет кость и падала (FUN_006b3100 -> FUN_00a2c000, чтение 0+0x24:
  // тестер загрузил чекпоинт из меню паузы, 01.10.2026). Новый уровень поставит свои камеры.
  if (camMode != 0 && !hugger) {
    if (GameStalled()) {
      camMode = 0, camSaved = 0;
      Log("cam restore skipped (alien destroyed while the game stands — loading/unloading)");
    } else {
      CamRestore("alien destroyed");  // за лицехвата камера не у Чужого
    }
  }
  AlienReset("alien destroyed");  // деревья не трогаем — новый Чужой найдётся сам
}
static void DtorInstallTick() {  // деструктор общий у всех персонажей (vtable 0x012F5D80)
  const addr c = npc ? npc : hugger;
  if (s_dtorVt || !c) return;
  addr vt = rp(c), dtor = rp(vt);
  s_dtorVt = vt;
  if (Interceptor::AttachNow(dtor, OnAlienDtor, nullptr)) Log("dtor hook VA %08X", unsigned(ToVA(dtor)));
}

void InstallCamera() {
  using Interceptor::Attach;
  Attach(VA(va::AnimInputUpdate), OnCamTick, nullptr);
  Attach(VA(va::LocoUpdate), OnCamTick, nullptr);
  Attach(VA(va::FollowUpdate), OnFollowUpdateEnter, OnFollowUpdateLeave);
  Attach(VA(va::HkWorldUse), OnHkWorldEnter, nullptr, false);  // горячий: только атомики
  Attach(VA(va::SoundListener), OnSoundListenerEnter, nullptr);
  Attach(VA(va::CamVelocity), OnKcVelocityEnter, nullptr);  // кадр атаки лицехвата (как фоторежим)
  Attach(VA(va::CamSubmit), OnKcSubmitEnter, OnKcSubmitLeave);
  Attach(VA(va::SkinPalette), OnPaletteEnter, OnPaletteLeave, false);  // горячая (все модели, рабочие потоки): без замка
  Every(16, "fp-keeper", FpKeeperTick);
  Every(200, "dtor-hook", DtorInstallTick);
}
