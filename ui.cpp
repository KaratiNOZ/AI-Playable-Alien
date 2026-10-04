// ui.cpp — окна мода в ImGui: кнопки вместо горячих клавиш, HUD с подсказками и картой вентиляции, персонажи, спавн,
// фоторежим, лог. Все строки — на двух языках: TR("рус", "eng"), язык — в меню (по умолчанию по языку Windows).
//
// Потоки: ImGui рисуется в потоке Present, а всё состояние мода живёт под общим замком (JsLock) в потоке планировщика
// и в хуках игры. Поэтому ImGui НИЧЕГО не пишет в состояние мода напрямую:
//   * кнопки кладут команду в очередь (свой маленький замок), задача "ui-cmd" выполняет её под общим замком;
//   * для отображения задача "ui-snap" раз в 150 мс собирает снимок состояния, ImGui читает только его.
// Так поток рендера никогда не ждёт общий замок и не читает указатели игры.
#include "ModApi.h"
#include "game.h"
#include "state.h"
#include "booster_ui.h"
#include "mp.h"
#include "puppet.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

std::wstring OverlayGet(int slot);
unsigned LogRingCopy(std::vector<std::string>& out, unsigned haveSeq);

// ============================ команды ============================
enum CmdId {
  C_CAM, C_PATH, C_STRAFE, C_VENT_GO, C_VENT_COME, C_VENT_CANCEL, C_ATTACK, C_NEXT, C_AI, C_PANIC,
  C_CAMCFG, C_CAMRESET, C_TP_TO, C_TP_BRING, C_SCAN, C_SEL, C_SPAWN, C_AMANDA, C_AUTOOUT, C_SPAWN_HUG, C_SPAWN_WHERE,
  C_VENT_EXIT, C_VENT_EXIT_AMANDA, C_PHOTO, C_PHOTO_PARAM, C_PHOTO_TO_PLAYER, C_FREEZE, C_PHOTO_VIEW, C_TIMESCALE,
  C_PHOTO_POST, C_AMANDA_HIDE, C_VOICE, C_VOICE_AUTO, C_VOICE_EVERY, C_VOICE_KEY, C_HIDE_GAME_HUD, C_KILL_ANDROIDS, C_ANDROID_MODE,
  C_HUGGER, C_HUG_NEXT, C_HUG_AI, C_HUG_ATK_AMANDA, C_HUG_MOVE, C_AI_ALL, C_ANDROID_AI, C_ALIEN_SNAPPY,
  C_PHOTO_ACTOR, C_PHOTO_FOLLOW, C_PHOTO_GEAR, C_FP_FREE, C_FP_SENS, C_TURN, C_LOOK_MODE, C_CAM_COLLIDE, C_FP_OPT,
  C_BREATH
};
struct Cmd { CmdId id; int i; addr a; float v[6]; };

static SRWLOCK s_cmdLock = SRWLOCK_INIT;
static std::vector<Cmd> s_cmds;

static void Push(CmdId id, int i = 0, addr a = 0, const float* v = nullptr, int nv = 0) {
  Cmd c{ id, i, a, {} };
  for (int k = 0; k < nv && k < 6; k++) c.v[k] = v[k];
  AcquireSRWLockExclusive(&s_cmdLock);
  if (s_cmds.size() < 64) s_cmds.push_back(c);
  ReleaseSRWLockExclusive(&s_cmdLock);
}

static void RunCmd(const Cmd& c) {
  switch (c.id) {
    case C_CAM: ActCamera(c.i); break;
    case C_PATH: ActTogglePath(); break;
    case C_STRAFE: ActSetStrafe(c.i != 0); break;
    case C_VENT_GO: ActVentGo(); break;
    case C_VENT_COME: ActVentCome(); break;
    case C_VENT_EXIT: ActVentExitHere(); break;
    case C_VENT_EXIT_AMANDA: ActVentExitNearAmanda(); break;
    case C_VENT_CANCEL: ActVentCancel(); break;
    case C_ATTACK: ActAttack(); break;
    case C_NEXT: ActNextTarget(); break;
    case C_AI: ActSetAi(c.i); break;
    case C_PANIC: ActPanic(); break;
    case C_CAMCFG: {
      CamCfg cc;
      cc.dist = c.v[0], cc.height = c.v[1], cc.side = c.v[2];
      for (int k = 0; k < 3; k++) cc.head[k] = c.v[3 + k];
      ActCamCfg(cc);
      break;
    }
    case C_CAMRESET: ActCamReset(c.i); break;
    case C_TP_TO: ActTpAlienTo(c.a); break;
    case C_TP_BRING: ActTpBring(c.a); break;
    case C_HUGGER: ActHugger(c.a); break;
    case C_HUG_NEXT: ActHuggerNext(); break;
    case C_HUG_AI: if (c.i == 0 || c.i == 2 || c.i == 3) hugAi = c.i, Log("facehugger AI mode %d", c.i); break;
    case C_AI_ALL: ActSetAiAll(c.i); break;
    case C_ANDROID_AI: if (c.i == 0 || c.i == 2 || c.i == 3) androidAi = c.i, Log("android AI mode %d", c.i); break;
    case C_ALIEN_SNAPPY: alienSnappy = c.i != 0, Log("snappy alien %d", int(alienSnappy)); break;
    case C_BREATH: alienBreath = c.i != 0, Log("drool and breath always %d", int(alienBreath)); break;
    case C_HUG_ATK_AMANDA: hugAttackAmanda = c.i != 0; break;
    case C_HUG_MOVE: hugMoveMode = std::clamp(c.i, 0, 2); break;
    case C_SCAN: ActSpawnScan(); break;
    case C_SEL: ActSpawnSelect(c.i); break;
    case C_SPAWN: ActSpawn(c.i != 0); break;
    case C_SPAWN_HUG: ActSpawnHugger(); break;
    case C_SPAWN_WHERE: spawnWhere = std::clamp(c.i, 0, 2); break;
    case C_AMANDA: ActSetFollow(c.i != 0); break;
    case C_AUTOOUT: autoComeOut = c.i != 0; Log("auto come out %d", c.i); break;
    case C_PHOTO: ActPhoto(c.i != 0); break;
    case C_PHOTO_PARAM: photo.speed = c.v[0], photo.sens = c.v[1]; break;
    case C_PHOTO_TO_PLAYER: ActPhotoToPlayer(); break;
    case C_FREEZE: ActFreezeTime(c.i != 0); break;
    case C_PHOTO_VIEW: ActPhotoParams(c.v[0], c.v[1]); break;
    case C_TIMESCALE: ActTimeScale(c.v[0]); break;
    case C_PHOTO_POST: ActPhotoPost(c.i, c.v[0]); break;
    case C_PHOTO_ACTOR: ActPhotoActor(c.i); break;
    case C_PHOTO_FOLLOW: ActPhotoFollow(c.i); break;
    case C_PHOTO_GEAR: photoGear = std::max(0, std::min(3, c.i)); break;
    case C_AMANDA_HIDE: ActSetAmandaHide(c.i); break;
    case C_VOICE: ActAlienVoice(VoiceType(c.i)); break;
    case C_VOICE_AUTO: alienAutoVoice = c.i != 0; Log("auto voice %d", c.i); break;
    case C_VOICE_EVERY: alienVoiceEvery = std::clamp(c.v[0], 3.f, 120.f); break;
    case C_VOICE_KEY: alienVoiceKey = std::clamp(c.i, -1, VOICE_COUNT - 1); break;
    case C_HIDE_GAME_HUD: hideGameHudAsAlien = c.i != 0; break;
    case C_KILL_ANDROIDS: alienKillsAndroids = c.i != 0; break;
    case C_ANDROID_MODE: androidKillMode = std::clamp(c.i, 0, 1); break;
    case C_FP_FREE: fpFree = c.i != 0, fpHave = false, Log("head view: %s", c.i ? "free (mouse)" : "attached to the bone"); break;
    case C_FP_SENS: fpSens = std::clamp(c.v[0], 0.2f, 3.f); break;
    case C_TURN: (c.i == 0 ? turnInPlaceFp : turnInPlace3p) = c.a != 0, Log("turn on the spot (%s): %d", c.i ? "behind" : "head", int(c.a)); break;
    case C_FP_OPT:  // i: 0 — прятать голову, 1 — влияние кости, 2 — предел поворота
      if (c.i == 0) fpHideHead = c.v[0] != 0, Log("head view: hide own head %d", int(fpHideHead));
      else if (c.i == 1) fpHeadInfluence = std::clamp(c.v[0], 0.f, 1.f);
      else if (c.i == 2) fpYawLimit = std::clamp(c.v[0], 30.f, 180.f);
      else if (c.i == 3) fpNearClip = std::clamp(c.v[0], 0.02f, 1.f);
      else if (c.i == 4) fpNeckScale = c.v[0] != 0, Log("head view: neck bones scaled to zero %d", int(fpNeckScale));
      break;
    case C_CAM_COLLIDE: camCollide = std::clamp(c.i, 0, 2), Log("camera collision %d", camCollide); break;
    case C_LOOK_MODE: lookMode = std::clamp(c.i, 0, 1), Log("look mode %d", lookMode); break;
  }
}

static void CmdTick() {  // под общим замком (планировщик)
  std::vector<Cmd> todo;
  AcquireSRWLockExclusive(&s_cmdLock);
  todo.swap(s_cmds);
  ReleaseSRWLockExclusive(&s_cmdLock);
  for (const Cmd& c : todo)
    if (!Safe([&] { RunCmd(c); })) Log("err: menu command %d", int(c.id));
}

// ============================ снимок ============================
struct CharRow { addr e; std::string name; float dist; bool isPlayer, isNpc; };
struct Radar {  // карта вентиляции: всё относительно Чужого
  bool on = false;
  V3 alien{}, amanda{};
  float view = 0;              // курс камеры: вверх на карте — куда смотрит камера
  std::vector<V3> vents, trail;
  int exitVent = -1;           // через какую вентиляцию он выйдет по E
};
struct Snap {
  bool ready = false, level = false, hasNpc = false, hasPlayer = false, hasLoco = false;
  int cam = 0, ai = 0, fmode = 3;
  bool strafe = true, follow = false, autoOut = true;
  const char* strafePause = nullptr;  // строковый литерал из control.cpp
  bool path = false, ventGo = false, ventCome = false, ventStay = false, duct = false, locked = false, f1menu = false;
  bool ventFail = false;
  bool moving = false, attacking = false;  // для обучения
  std::string target;                      // имя цели Чужого (пусто — нет)
  float targetDist = -1;
  bool targetAmanda = false;
  bool photoOn = false, freeze = false, freezeAvail = false;
  int photoActorSel = 1, photoActorNow = 0, photoGear = 1, photoFollow = 0;  // машинима: кого ведут стрелки
  std::string photoToast;
  V3 photoPos{};
  float photoSpeed = 4, photoSens = 1, photoFov = 0, photoRoll = 0, gameFov = 47, timeScale = 1, timeLive = 1;
  PhotoPost post{};
  // за Чужого (alien_extra.cpp)
  int speed = 1;                 // 0 тихо, 1 шаг, 2 быстро, 3 бег
  bool looking = false, autoVoice = false, amandaHidden = false, hideGameHud = true, killAndroids = true;
  int androidMode = 1;
  int amandaHide = 1, voiceKey = -1;
  float voiceEvery = 14.f;
  bool amandaDir = false;        // есть направление на Аманду (относительно взгляда камеры)
  float amandaAngle = 0, amandaDist = 0;
  unsigned trees = 0;
  CamCfg camCfg{};
  bool camHug = false;  // в camCfg — настройка камеры лицехвата
  int spawnWhere = 1;
  // лицехваты (hugger.cpp)
  std::vector<HugRow> hugs;
  bool hugCtl = false, hugAtkAmanda = false, hugNoBranch = false, hugPath = false, hugMsg = false;
  int hugAi = 2, hugAiNow = 2, hugMove = 0, androidAi = 2;
  int npcKind = 0, npcAct = 0;  // 1 лицехват / 2 андроид; действие (hugger.cpp NpcActionState)
  std::string npcVoice;         // последняя реплика андроида
  bool snappy = false, breath = true;
  bool fpFree = true, turnFp = true, turn3p = false;
  float fpSens = 1.f;
  int lookMode = 1, camCollide = 1;
  bool fpHideHead = true, fpNeckScale = false;
  float fpInfluence = 0.2f, fpYawLimit = 100.f, fpNearClip = 0.35f;
  std::string hugName;
  std::vector<CharRow> chars;
  SpawnInfo spawn{};
  std::string backstage;
  std::string f1text;  // меню персонажей F1 (стрелки/Enter/Backspace) — показываем в HUD
  Radar radar;
};
static SRWLOCK s_snapLock = SRWLOCK_INIT;
static Snap s_snap;

static std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(std::max(n, 0)), '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

static void SnapTick() {  // под общим замком (планировщик)
  Snap s;
  s.ready = true;
  s.level = gate != 0;
  s.hasNpc = npc != 0;
  s.hasPlayer = player != 0;
  s.hasLoco = loco != 0;
  s.cam = camMode;
  s.ai = aiMode;
  s.fmode = fmode;
  s.strafe = StrafeActive();
  s.strafePause = StrafePause();
  s.follow = amandaFollow;
  s.autoOut = autoComeOut;
  s.path = pathOn;
  ms_t now = NowMs();
  s.ventGo = vent.goUntil > now;
  s.ventCome = vent.comeUntil > now;
  s.ventStay = vent.stay || (alienBackstage && vent.comeUntil <= now);
  s.duct = ductOn;
  s.ventFail = ventComeFailedAt && now - ventComeFailedAt < 10000;
  s.moving = moving || (alienBackstage && lastInput.rawAny);
  s.attacking = attack.until > now;
  if (const addr who = CamTarget(); who && (hugger || npc)) {  // цель Чужого (или лицехвата): +0x34a8 -> +0x10 -> +0x42c
    addr T = 0;
    V3 a{}, b{};
    if (Safe([&] { T = TargetOf(who); }) && T && T != who && Safe([&] { a = rv3(who + 0x70), b = rv3(T + 0x70); })) {
      s.targetAmanda = T == player;
      s.target = s.targetAmanda ? std::string(TR("Аманда", "Amanda")) : CharName(T);
      s.targetDist = std::hypot(a.x - b.x, a.z - b.z);
    }
  }
  s.locked = locked;
  s.f1menu = menuOpen;
  s.trees = unsigned(multi[G_ARROW].size());
  s.photoOn = photo.on;
  s.photoActorSel = photoActor.load();
  s.photoActorNow = PhotoActorNow();
  s.photoGear = photoGear.load();
  s.photoFollow = photoFollow.load();
  s.photoToast = PhotoToast();
  s.photoPos = photo.pos;
  s.photoSpeed = photo.speed;
  s.photoSens = photo.sens;
  s.freeze = photo.freeze;
  s.photoFov = photo.fov;
  s.photoRoll = photo.roll * 180.f / kPi;
  s.gameFov = PhotoGameFov();
  s.timeScale = photo.timeScale;
  s.timeLive = s.hasPlayer ? PhotoTimeLive() : 1.f;
  s.post = photoPost;
  s.freezeAvail = FreezeAvailable();
  s.speed = lastInput.speed;
  s.looking = alienLooking;
  s.autoVoice = alienAutoVoice;
  s.voiceEvery = alienVoiceEvery;
  s.voiceKey = alienVoiceKey;
  s.amandaHide = amandaHide;
  s.amandaHidden = amandaHidden;
  s.hideGameHud = hideGameHudAsAlien;
  s.killAndroids = alienKillsAndroids;
  s.androidMode = androidKillMode;
  if (CamTarget() && player && camMode != 0) {  // стрелка на Аманду: от Чужого (лицехвата), относительно взгляда камеры
    Safe([&] {
      const V3 a = rv3(CamTarget() + 0x70), b = rv3(player + 0x70);
      const addr cam = rp(CamG() + 0x1d8);
      if (!cam) return;
      const float x = rf(cam + 0x2c), y = rf(cam + 0x30), z = rf(cam + 0x34), w = rf(cam + 0x38);
      const float fx = 2 * (x * z + w * y), fz = 1 - 2 * (x * x + y * y);  // вперёд = q · (0,0,1)
      const float camYaw = std::atan2(fx, fz);
      s.amandaAngle = WrapPi(std::atan2(b.x - a.x, b.z - a.z) - camYaw);
      s.amandaDist = std::hypot(b.x - a.x, b.z - a.z);
      s.amandaDir = true;
    });
  }
  s.camCfg = camCfg;
  s.camHug = camIsHugger;
  s.spawnWhere = spawnWhere;
  s.hugs = HuggerList();
  s.hugCtl = hugger != 0;
  if (hugger) s.hugName = CharName(hugger);
  s.hugAi = hugAi;
  s.hugAiNow = HugAiNow();
  s.androidAi = androidAi;
  s.npcKind = NpcCtlKind();
  s.npcAct = NpcActionState();
  s.npcVoice = NpcVoiceName();
  s.snappy = alienSnappy, s.breath = alienBreath;
  s.fpFree = fpFree, s.fpSens = fpSens, s.turnFp = turnInPlaceFp, s.turn3p = turnInPlace3p, s.lookMode = lookMode, s.camCollide = camCollide;
  s.fpHideHead = fpHideHead, s.fpInfluence = fpHeadInfluence, s.fpYawLimit = fpYawLimit;
  s.fpNearClip = fpNearClip, s.fpNeckScale = fpNeckScale;
  s.hugMove = hugMoveMode;
  s.hugAtkAmanda = hugAttackAmanda;
  s.hugNoBranch = hugNoBranch;
  s.hugPath = hugPathNow;
  s.hugMsg = hugMsgAt && now - hugMsgAt < 4000;
  if (hugger) s.moving = Down('W') || Down('A') || Down('S') || Down('D');
  addr ref = npc ? npc : player;
  V3 rp3{};
  bool hasRef = ref && Safe([&] { rp3 = rv3(ref + 0x70); });
  for (addr e : ActiveChars()) {
    CharRow r{ e, CharName(e), -1.f, e == player, e == npc };
    V3 p{};
    if (hasRef && Safe([&] { p = rv3(e + 0x70); })) r.dist = std::hypot(p.x - rp3.x, p.z - rp3.z);
    s.chars.push_back(std::move(r));
  }
  s.spawn = SpawnSnapshot();
  if (s.ventStay) s.backstage = Utf8(OverlayGet(1));
  if (s.f1menu) s.f1text = Utf8(OverlayGet(2));
  // карта вентиляции — пока Чужой за кулисами
  if (npc && (s.ventStay || s.ventCome || s.ventGo)) {
    Radar& r = s.radar;
    r.on = Safe([&] {
      r.alien = rv3(npc + 0x70);
      if (player) r.amanda = rv3(player + 0x70);
      r.view = photo.on ? photo.yaw : bsViewValid && camMode == 2 ? bsViewYaw : ViewYaw();
    });
    r.vents = VentPositions();
    r.trail = bsTrail;
    float best = 1e9f;
    for (size_t i = 0; i < r.vents.size(); i++) {  // та же мера, что при выходе (vent.cpp)
      const V3& v = r.vents[i];
      float d = std::hypot(v.x - r.alien.x, v.z - r.alien.z) + std::fabs(v.y - r.alien.y) * 0.7f;
      if (d < best) best = d, r.exitVent = int(i);
    }
  }
  AcquireSRWLockExclusive(&s_snapLock);
  s_snap = std::move(s);
  ReleaseSRWLockExclusive(&s_snapLock);
}

static Snap GetSnap() {
  AcquireSRWLockShared(&s_snapLock);
  Snap s = s_snap;
  ReleaseSRWLockShared(&s_snapLock);
  return s;
}

// ============================ что показывать на экране (сохраняется в .ini рядом с DLL) ============================
struct HudCfg {
  bool hud = true;        // вообще весь HUD (F2)
  bool status = true;     // короткий статус: где Чужой, какая камера
  bool details = false;   // подробный статус: стрейф, Аманда следует, режим пути, ИИ
  bool hints = true;      // подсказки «что нажимать сейчас»
  bool radar = false;     // карта вентиляции (F3) — по умолчанию выключена, чтобы не лезла на экран
  bool spawn = true;      // список спавна (F7/F8) и персонажей (F1)
  bool footer = true;     // строчка «F2 — спрятать · Delete — меню»
  bool target = true;     // имя цели Чужого
  bool alienHud = true;   // за Чужого — свой HUD внизу по центру (режим, цель, стрелка на Аманду)
  int tutorial = 0;       // шаг обучения; 99 — пройдено / пропущено
  int corner = 0;         // 0 слева снизу, 1 справа снизу, 2 слева сверху, 3 справа сверху
  float alpha = 0.55f;    // прозрачность фона
};
static HudCfg s_cfg;

const std::wstring& ModBase();  // runtime.cpp: папка и имя DLL (и для копии из папки live)
static std::wstring CfgPath() { return ModBase() + L".ini"; }  // AlienModUI.dll -> AlienModUI.ini
static int CfgGet(const std::wstring& f, const wchar_t* key, int def) { return int(GetPrivateProfileIntW(L"hud", key, def, f.c_str())); }
static void CfgSet(const std::wstring& f, const wchar_t* key, int v) {
  WritePrivateProfileStringW(L"hud", key, std::to_wstring(v).c_str(), f.c_str());
}
static float FGet(const std::wstring& f, const wchar_t* sec, const wchar_t* key, float def) {
  wchar_t buf[32] = {};
  GetPrivateProfileStringW(sec, key, L"", buf, 32, f.c_str());
  wchar_t* end = nullptr;
  const float v = wcstof(buf, &end);
  return end && end != buf ? v : def;
}
static void FSet(const std::wstring& f, const wchar_t* sec, const wchar_t* key, float v) {
  wchar_t buf[32];
  swprintf(buf, 32, L"%.3f", double(v));
  WritePrivateProfileStringW(sec, key, buf, f.c_str());
}
static float CamGet(const std::wstring& f, const wchar_t* key, float def) { return FGet(f, L"camera", key, def); }
static void CamSet(const std::wstring& f, const wchar_t* key, float v) { FSet(f, L"camera", key, v); }

// клавиши [keys]
static const wchar_t* const kKeyIds[K_COUNT] = { L"swap", L"view", L"vent", L"vent_amanda", L"unstuck", L"strafe", L"follow", L"path",
                                                 L"chars", L"hud", L"map", L"ai", L"panic", L"spawn_find", L"spawn", L"photo",
                                                 L"look", L"sound", L"hugger", L"menu", L"turn180" };
static void SaveKeys() {
  const std::wstring f = CfgPath();
  for (int i = 0; i < K_COUNT; i++) WritePrivateProfileStringW(L"keys", kKeyIds[i], std::to_wstring(g_keys[i].load()).c_str(), f.c_str());
}

// игровые настройки [game]: стрейф, Аманда следует, автовыход, фоторежим — сохраняются, когда секунду не менялись
struct GameVal { const wchar_t* key; float v; };
static std::vector<GameVal> GameValues(const Snap& s) {
  const PhotoPost& p = s.post;
  return { { L"strafe", float(s.fmode != 0) }, { L"amanda_follows", float(s.follow) }, { L"auto_come_out2", float(s.autoOut) },
           { L"photo_speed", s.photoSpeed }, { L"photo_sens", s.photoSens }, { L"post_on", float(p.on) },
           { L"exposure", p.exposure }, { L"brightness", p.brightness }, { L"contrast", p.contrast }, { L"saturation", p.saturation },
           { L"temperature", p.temperature }, { L"tint", p.tint }, { L"vignette", p.vignette }, { L"grain", p.grain },
           { L"aberration", p.aberration }, { L"sharpness", p.sharpness }, { L"blur", p.blur }, { L"bloom", p.bloom },
           { L"no_motion_blur", float(p.noMotionBlur) }, { L"bloom_threshold", p.bloomThreshold },
           { L"distortion", p.distortion }, { L"dof", float(p.dof) }, { L"dof_focus", p.focus }, { L"dof_focal", p.focal },
           { L"dof_fstop", p.fstop }, { L"letterbox", float(p.letterbox) }, { L"grid", float(p.grid) },
           { L"photo_hide_hud", float(p.hideGameHud) }, { L"photo_amanda", float(p.showAmanda) }, { L"amanda_hide", float(s.amandaHide) },
           { L"auto_voice", float(s.autoVoice) }, { L"voice_every", s.voiceEvery }, { L"voice_type", float(s.voiceKey < 0 ? -1 : VoiceType(s.voiceKey)) },
           { L"hide_game_hud", float(s.hideGameHud) }, { L"kill_androids", float(s.killAndroids) }, { L"spawn_where", float(s.spawnWhere) }, { L"android_kill_mode", float(s.androidMode) }, { L"hug_ai", float(s.hugAi) }, { L"android_ai", float(s.androidAi) }, { L"alien_snappy", float(s.snappy) }, { L"alien_breath", float(s.breath) },
           { L"hug_attack_amanda", float(s.hugAtkAmanda) }, { L"hug_move", float(s.hugMove) }, { L"fp_free", float(s.fpFree) },
           { L"fp_sens", s.fpSens }, { L"turn_fp", float(s.turnFp) }, { L"turn_3p", float(s.turn3p) }, { L"look_mode", float(s.lookMode) }, { L"cam_collide", float(s.camCollide) },
           { L"fp_hide_head", float(s.fpHideHead) }, { L"fp_head_influence", s.fpInfluence }, { L"fp_yaw_limit", s.fpYawLimit },
           { L"fp_near_clip", s.fpNearClip }, { L"fp_neck_scale", float(s.fpNeckScale) } };
}
static std::vector<float> s_gameSaved;
static void LoadGame(const std::wstring& f) {  // до первого уровня — общий замок ещё никто не держит
  auto g = [&](const wchar_t* k, float d) { return FGet(f, L"game", k, d); };
  fmode = g(L"strafe", 1) != 0 ? 3 : 0;
  amandaFollow = g(L"amanda_follows", 0) != 0;
  autoComeOut = g(L"auto_come_out2", 0) != 0;  // новое имя: старое «1» из .ini не включает автовыход обратно
  amandaHide = std::clamp(int(g(L"amanda_hide", 1)), 0, 2);
  alienAutoVoice = g(L"auto_voice", 0) != 0;
  alienVoiceEvery = std::clamp(g(L"voice_every", 14.f), 3.f, 120.f);
  {  // что играет клавиша звука: тип звука (раньше — номер в старом списке меню)
    const int ty = int(g(L"voice_type", -99));
    static const int kOld[VOICE_COUNT] = { 9, 7, 11, 10, 8, 3, 1, 6, 4, 0 };
    const int old = int(g(L"voice_key", -1));
    const int want = ty != -99 ? ty : old >= 0 && old < VOICE_COUNT ? kOld[old] : -1;
    alienVoiceKey = -1;
    for (int i = 0; i < VOICE_COUNT; i++)
      if (VoiceType(i) == want) alienVoiceKey = i;
  }
  fpFree = g(L"fp_free", 1) != 0;
  fpSens = std::clamp(g(L"fp_sens", 1.f), 0.2f, 3.f);
  turnInPlaceFp = g(L"turn_fp", 1) != 0;
  turnInPlace3p = g(L"turn_3p", 0) != 0;
  lookMode = std::clamp(int(g(L"look_mode", 1)), 0, 1);
  camCollide = std::clamp(int(g(L"cam_collide", 1)), 0, 2);
  fpHideHead = g(L"fp_hide_head", 1) != 0;
  fpHeadInfluence = std::clamp(g(L"fp_head_influence", 0.2f), 0.f, 1.f);
  fpYawLimit = std::clamp(g(L"fp_yaw_limit", 100.f), 30.f, 180.f);
  fpNearClip = std::clamp(g(L"fp_near_clip", 0.35f), 0.02f, 1.f);
  fpNeckScale = g(L"fp_neck_scale", 1) != 0;
  hideGameHudAsAlien = g(L"hide_game_hud", 1) != 0;
  alienKillsAndroids = g(L"kill_androids", 1) != 0;
  spawnWhere = std::clamp(int(g(L"spawn_where", 1)), 0, 2);
  androidKillMode = std::clamp(int(g(L"android_kill_mode", 1)), 0, 1);
  {  // раньше было hug_others: 0 — живут сами (полный ИИ), 1 — стоят (ручное)
    const int old = int(g(L"hug_others", 0));
    const int m = int(g(L"hug_ai", old == 1 ? 0.f : 2.f));
    hugAi = m == 0 || m == 3 ? m : 2;
  }
  hugAttackAmanda = g(L"hug_attack_amanda", 0) != 0;
  {
    const int m = int(g(L"android_ai", 2));
    androidAi = m == 0 || m == 3 ? m : 2;
  }
  alienSnappy = g(L"alien_snappy", 0) != 0;
  alienBreath = g(L"alien_breath", 1) != 0;
  hugMoveMode = std::clamp(int(g(L"hug_move", 0)), 0, 2);
  photo.speed = std::clamp(g(L"photo_speed", photo.speed), 0.3f, 25.f);
  photo.sens = std::clamp(g(L"photo_sens", photo.sens), 0.1f, 4.f);
  PhotoPost& p = photoPost;
  p.on = g(L"post_on", p.on) != 0;
  p.exposure = g(L"exposure", p.exposure), p.brightness = g(L"brightness", p.brightness);
  p.contrast = g(L"contrast", p.contrast), p.saturation = g(L"saturation", p.saturation);
  p.temperature = g(L"temperature", p.temperature), p.tint = g(L"tint", p.tint);
  p.vignette = g(L"vignette", p.vignette), p.grain = g(L"grain", p.grain);
  p.aberration = g(L"aberration", p.aberration), p.sharpness = g(L"sharpness", p.sharpness);
  p.blur = g(L"blur", p.blur), p.bloom = g(L"bloom", p.bloom);
  p.noMotionBlur = g(L"no_motion_blur", p.noMotionBlur) != 0;
  p.bloomThreshold = std::clamp(g(L"bloom_threshold", p.bloomThreshold), 0.1f, 3.f);
  p.distortion = std::clamp(g(L"distortion", p.distortion), -0.5f, 0.5f);
  p.dof = g(L"dof", p.dof) != 0;
  p.focus = std::clamp(g(L"dof_focus", p.focus), 0.1f, 200.f);
  p.focal = std::clamp(g(L"dof_focal", p.focal), 10.f, 400.f);
  p.fstop = std::clamp(g(L"dof_fstop", p.fstop), 0.7f, 32.f);
  p.letterbox = std::clamp(int(g(L"letterbox", 0)), 0, 5);
  p.grid = g(L"grid", 0) != 0;
  p.hideGameHud = g(L"photo_hide_hud", 1) != 0;
  p.showAmanda = g(L"photo_amanda", 0) != 0;
  p.grain = std::clamp(p.grain, 0.f, 3.f);
  for (int i = 0; i < K_COUNT; i++) {
    const int vk = int(GetPrivateProfileIntW(L"keys", kKeyIds[i], kKeyDefault[i], f.c_str()));
    g_keys[i] = vk > 0 && vk < 256 ? vk : kKeyDefault[i];
  }
}
static CamCfg s_camSaved, s_camSavedHug;  // что лежит в .ini — сохраняем, когда игрок перестал крутить колесо
static void SaveCam(const CamCfg& c, bool hug) {  // у лицехвата — свои ключи hug_*
  const std::wstring f = CfgPath();
  CamSet(f, hug ? L"hug_dist" : L"dist", c.dist);
  CamSet(f, hug ? L"hug_height" : L"height", c.height);
  CamSet(f, hug ? L"hug_side" : L"side", c.side);
  if (!hug) {
    CamSet(f, L"head_x", c.head[0]);
    CamSet(f, L"head_y", c.head[1]);
    CamSet(f, L"head_z", c.head[2]);
  }
  (hug ? s_camSavedHug : s_camSaved) = c;
}
static bool SameCam(const CamCfg& a, const CamCfg& b) { return !memcmp(&a, &b, sizeof a); }
static void LoadCfg() {
  const std::wstring f = CfgPath();
  HudCfg& c = s_cfg;
  c.hud = CfgGet(f, L"hud", c.hud) != 0;
  c.status = CfgGet(f, L"status", c.status) != 0;
  c.details = CfgGet(f, L"details", c.details) != 0;
  c.hints = CfgGet(f, L"hints", c.hints) != 0;
  c.radar = CfgGet(f, L"radar", c.radar) != 0;
  c.spawn = CfgGet(f, L"spawn", c.spawn) != 0;
  c.footer = CfgGet(f, L"footer", c.footer) != 0;
  c.corner = std::clamp(CfgGet(f, L"corner", c.corner), 0, 3);
  c.alpha = std::clamp(CfgGet(f, L"alpha", int(c.alpha * 100)) / 100.f, 0.f, 1.f);
  g_simpleKeys = CfgGet(f, L"simple", 1) != 0;
  g_hotkeysOn = CfgGet(f, L"hotkeys", 1) != 0;
  g_lang = CfgGet(f, L"lang", g_lang) ? 1 : 0;
  CamCfg cc;  // до первого уровня, общий замок ещё никто не держит
  cc.dist = CamGet(f, L"dist", cc.dist);
  cc.height = CamGet(f, L"height", cc.height);
  cc.side = CamGet(f, L"side", cc.side);
  cc.head[0] = CamGet(f, L"head_x", cc.head[0]);
  cc.head[1] = CamGet(f, L"head_y", cc.head[1]);
  cc.head[2] = CamGet(f, L"head_z", cc.head[2]);
  camCfg = cc;
  s_camSaved = cc;
  CamCfg hc = HuggerCamDefault();
  hc.dist = CamGet(f, L"hug_dist", hc.dist);
  hc.height = CamGet(f, L"hug_height", hc.height);
  hc.side = CamGet(f, L"hug_side", hc.side);
  camCfgOther = hc;
  s_camSavedHug = hc;
  c.target = CfgGet(f, L"target", c.target) != 0;
  c.alienHud = CfgGet(f, L"alien_hud", c.alienHud) != 0;
  c.tutorial = CfgGet(f, L"tutorial", 0);
  LoadGame(f);
}
static void SaveCfg() {
  const std::wstring f = CfgPath();
  const HudCfg& c = s_cfg;
  CfgSet(f, L"hud", c.hud);
  CfgSet(f, L"status", c.status);
  CfgSet(f, L"details", c.details);
  CfgSet(f, L"hints", c.hints);
  CfgSet(f, L"radar", c.radar);
  CfgSet(f, L"spawn", c.spawn);
  CfgSet(f, L"footer", c.footer);
  CfgSet(f, L"corner", c.corner);
  CfgSet(f, L"alpha", int(c.alpha * 100 + 0.5f));
  CfgSet(f, L"simple", g_simpleKeys);
  CfgSet(f, L"hotkeys", g_hotkeysOn);
  CfgSet(f, L"lang", g_lang);
  CfgSet(f, L"target", c.target);
  CfgSet(f, L"alien_hud", c.alienHud);
  CfgSet(f, L"tutorial", c.tutorial);
}

// опрос клавиш действий: фронт нажатия -> отметка времени (её забирает HotK)
static void KeyPollTick() {
  static bool was[K_COUNT];
  for (int i = 0; i < K_COUNT; i++) {
    const bool d = Down(g_keys[i]);
    if (d && !was[i] && !g_rebinding) g_keyHitAt[i] = NowMs();  // нажатие при переназначении — не действие
    was[i] = d;
  }
}

void InstallUi() {
  g_lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? 0 : 1;
  LoadCfg();
  Every(5, "keys", KeyPollTick);
  Every(10, "ui-cmd", CmdTick);
  Every(150, "ui-snap", SnapTick);
}

// ============================ ImGui ============================

static void Tip(const char* text) { BUI::Tooltip(text); }  // подсказка на последний виджет меню
static const ImVec4 kKeyCol(1.f, 0.75f, 0.28f, 1.f), kWarnCol(1.f, 0.5f, 0.25f, 1.f), kDimCol(0.42f, 0.66f, 0.48f, 1.f);
// клавиша в рамке, как клавиша на пульте терминала
static void KeyCap(const char* key) {
  const ImVec2 ts = ImGui::CalcTextSize(key), pad(5.f, 0.f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 q(p.x + ts.x + pad.x * 2, p.y + ts.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, q, IM_COL32(60, 40, 5, 150));
  dl->AddRect(p, q, IM_COL32(255, 185, 60, 200));
  dl->AddLine(ImVec2(p.x + 1, q.y - 1), ImVec2(q.x - 1, q.y - 1), IM_COL32(255, 185, 60, 110), 2.f);  // «нижняя грань»
  dl->AddText(ImVec2(p.x + pad.x, p.y), ImGui::GetColorU32(kKeyCol), key);
  ImGui::Dummy(ImVec2(q.x - p.x, q.y - p.y));
}
// строка подсказки: «клавиша — что делает»
static void Hint(const char* key, const char* what) {
  KeyCap(key);
  ImGui::SameLine();
  ImGui::TextUnformatted(what);
}
// ---- имена клавиш (переназначаются в меню) ----
static std::string KeyName(int vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
  if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::to_string(vk - VK_NUMPAD0);
  switch (vk) {
    case VK_MBUTTON: return TR("СКМ", "MMB");
    case VK_XBUTTON1: return TR("Мышь 4", "Mouse 4");
    case VK_XBUTTON2: return TR("Мышь 5", "Mouse 5");
    case VK_TAB: return "Tab";
    case VK_CAPITAL: return "Caps Lock";
    case VK_OEM_3: return "`";
  }
  UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
  switch (vk) {  // у этих «расширенный» скан-код, иначе имя будет от цифрового блока
    case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
      sc |= 0x100;
  }
  char buf[64] = {};
  if (sc && GetKeyNameTextA(LONG(sc << 16), buf, sizeof buf) > 0) return buf;
  char hex[16];
  snprintf(hex, sizeof hex, "0x%02X", vk);
  return hex;
}
static const char* KN(int act) {  // имя клавиши действия (кэш на кадр рендера: строки живут до смены клавиши/языка)
  static std::string cache[K_COUNT];
  static int cacheVk[K_COUNT], cacheLang[K_COUNT];
  const int vk = g_keys[act], lang = g_lang;
  if (cache[act].empty() || cacheVk[act] != vk || cacheLang[act] != lang) cache[act] = KeyName(vk), cacheVk[act] = vk, cacheLang[act] = lang;
  return cache[act].c_str();
}
// кольцо буферов: строка живёт, пока её использует ImGui в этом же вызове
static char* RingBuf() {
  static char bufs[32][192];
  static int i = 0;
  return bufs[i++ & 31];
}
static const char* KN3(int a, int b, int c) {
  char* r = RingBuf();
  snprintf(r, 192, "%s / %s / %s", KN(a), KN(b), KN(c));
  return r;
}
// «Текст   [клавиша]###id» (pre — модификатор, например "Ctrl+")
static const char* Lbl(const char* text, int act, const char* id, const char* pre = "") {
  char* r = RingBuf();
  snprintf(r, 192, "%s   [%s%s]###%s", text, pre, KN(act), id);
  return r;
}
static const char* OnOff(bool on) { return on ? TR("вкл", "on") : TR("выкл", "off"); }

static const char* CamName(int c) {
  return c == 2 ? TR("за спиной Чужого", "behind the Alien") : c == 1 ? TR("в голове Чужого", "Alien's head") : TR("Аманда", "Amanda");
}
static const char* AiName(int a) {
  return a == 3 ? TR("охотится, но не на Аманду", "hunts, not Amanda") : a == 2 ? TR("полный ИИ", "full AI") : TR("ручное", "manual");
}
static const char* VentName(const Snap& s) {
  return s.ventGo ? TR("идёт в вентиляцию", "entering a vent")
                  : s.ventCome ? TR("выходит из вентиляции", "leaving the vent")
                               : s.ventStay ? TR("в вентиляции", "in the vents") : TR("в мире", "in the world");
}
static std::string StrafeText(const Snap& s) {
  if (!s.fmode) return TR("выкл", "off");
  if (s.strafePause) return std::string(TR("пауза (", "paused (")) + s.strafePause + ")";
  return TR("вкл", "on");
}
static std::string SpawnName(const SpawnInfo& sp, size_t i) {
  std::string r = "#" + std::to_string(i + 1) + "  " + (sp.labels[i].empty() ? "?" : sp.labels[i]);
  if (i < sp.hugger.size() && sp.hugger[i]) r += TR("  [лицехват]", "  [facehugger]");
  if (sp.gone[i]) r += TR("  [исчез]", "  [gone]");
  else if (sp.spawned[i])
    r += std::string(TR("  [заспавнен", "  [spawned")) + (sp.spawned[i] > 1 ? " x" + std::to_string(sp.spawned[i]) : std::string()) + "]";
  return r;
}

// ---- карта вентиляции: сверху — куда смотрит камера, радиус 25 м ----
static void DrawRadar(const Radar& r) {
  const float size = 220.f, R = size * 0.5f, meters = 25.f, k = R / meters;
  ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c(o.x + R, o.y + R);
  const float fx = std::sin(r.view), fz = std::cos(r.view), rx = std::cos(r.view), rz = -std::sin(r.view);
  auto map = [&](const V3& p, bool clamp, bool* inside) {
    float dx = p.x - r.alien.x, dz = p.z - r.alien.z;
    float sx = (dx * rx + dz * rz) * k, sy = (dx * fx + dz * fz) * k;
    float len = std::sqrt(sx * sx + sy * sy);
    if (inside) *inside = len <= R - 4;
    if (clamp && len > R - 4) sx *= (R - 4) / len, sy *= (R - 4) / len;
    return ImVec2(c.x + sx, c.y - sy);
  };
  dl->AddCircleFilled(c, R, IM_COL32(10, 14, 12, 170), 48);
  dl->AddCircle(c, R, IM_COL32(120, 200, 140, 160), 48, 1.5f);
  dl->AddCircle(c, R * 0.4f, IM_COL32(120, 200, 140, 60), 32);  // 10 м
  // след — где Чужой уже прополз
  for (size_t i = 1; i < r.trail.size(); i++) {
    bool a, b;
    ImVec2 p0 = map(r.trail[i - 1], false, &a), p1 = map(r.trail[i], false, &b);
    if (a && b) dl->AddLine(p0, p1, IM_COL32(255, 150, 40, 200), 2.f);
  }
  // вентиляции: яркие — на его высоте, тусклые — на других этажах, зелёная — через неё он выйдет по E
  for (size_t i = 0; i < r.vents.size(); i++) {
    bool in;
    ImVec2 p = map(r.vents[i], false, &in);
    if (!in) continue;
    const bool sameFloor = std::fabs(r.vents[i].y - r.alien.y) < 3.f;
    dl->AddCircleFilled(p, 4.f, sameFloor ? IM_COL32(200, 220, 255, 230) : IM_COL32(120, 130, 150, 120));
    if (int(i) == r.exitVent) dl->AddCircle(p, 8.f, IM_COL32(80, 255, 110, 255), 16, 2.f);
  }
  // Аманда (на краю, если далеко)
  dl->AddCircleFilled(map(r.amanda, true, nullptr), 5.f, IM_COL32(80, 170, 255, 255));
  // Чужой в центре, нос — вперёд по камере
  dl->AddTriangleFilled(ImVec2(c.x, c.y - 9), ImVec2(c.x - 6, c.y + 6), ImVec2(c.x + 6, c.y + 6), IM_COL32(255, 80, 80, 255));
  dl->AddText(ImVec2(o.x + 4, o.y + 2), IM_COL32(200, 230, 210, 220), TR("25 м", "25 m"));
}
static void RadarLegend() {
  ImGui::TextColored(ImVec4(1.f, 0.6f, 0.15f, 1.f), "%s", TR("— след", "— trail"));
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.8f, 0.87f, 1.f, 1.f), "%s", TR("● вентиляции", "● vents"));
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.3f, 1.f, 0.45f, 1.f), "%s", TR("○ выход по E", "○ exit on E"));
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.3f, 0.67f, 1.f, 1.f), "%s", TR("● Аманда", "● Amanda"));
}

namespace AlienMod {

// ============================ шрифты игры ============================
// основной — Isolation (как в меню игры), заголовки — Jixellation (терминалы Seegson); оба из UI.PAK (gamefont.cpp)
static ImFont* s_titleFont = nullptr;
void SetFonts(ImFont* body, ImFont* title) { s_titleFont = title, BUI::SetFonts(body, title); }
static void TitleText(const ImVec4& col, const char* t) {
  if (s_titleFont) ImGui::PushFont(s_titleFont);
  ImGui::TextColored(col, "%s", t);
  if (s_titleFont) ImGui::PopFont();
}
// ============================ оформление: терминалы Seegson / датчик движения ============================
// зелёный фосфор на почти чёрном, квадратные рамки, амбровые клавиши, полосы развёртки ЭЛТ
void ApplyStyle() {
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = st.ChildRounding = st.FrameRounding = st.PopupRounding = 0.f;
  st.ScrollbarRounding = st.GrabRounding = st.TabRounding = 0.f;
  st.WindowBorderSize = 1.f;
  st.FrameBorderSize = 1.f;
  st.PopupBorderSize = 1.f;
  st.WindowPadding = ImVec2(12, 10);
  st.FramePadding = ImVec2(8, 4);
  st.ItemSpacing = ImVec2(8, 6);
  st.GrabMinSize = 8.f;
  st.ScrollbarSize = 12.f;
  st.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  auto rgb = [](int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); };
  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = rgb(150, 255, 175);
  c[ImGuiCol_TextDisabled] = rgb(70, 130, 88);
  c[ImGuiCol_WindowBg] = rgb(4, 12, 8, 0.94f);
  c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = rgb(5, 16, 10, 0.97f);
  c[ImGuiCol_Border] = rgb(60, 190, 100, 0.55f);
  c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = rgb(10, 34, 20);
  c[ImGuiCol_FrameBgHovered] = rgb(18, 62, 34);
  c[ImGuiCol_FrameBgActive] = rgb(28, 90, 48);
  c[ImGuiCol_TitleBg] = rgb(6, 22, 12);
  c[ImGuiCol_TitleBgActive] = rgb(12, 48, 24);
  c[ImGuiCol_TitleBgCollapsed] = rgb(4, 14, 8, 0.8f);
  c[ImGuiCol_MenuBarBg] = rgb(8, 26, 15);
  c[ImGuiCol_ScrollbarBg] = rgb(3, 10, 6, 0.6f);
  c[ImGuiCol_ScrollbarGrab] = rgb(40, 120, 64);
  c[ImGuiCol_ScrollbarGrabHovered] = rgb(60, 170, 90);
  c[ImGuiCol_ScrollbarGrabActive] = rgb(100, 230, 130);
  c[ImGuiCol_CheckMark] = rgb(140, 255, 165);
  c[ImGuiCol_SliderGrab] = rgb(90, 220, 120);
  c[ImGuiCol_SliderGrabActive] = rgb(170, 255, 190);
  c[ImGuiCol_Button] = rgb(14, 48, 26);
  c[ImGuiCol_ButtonHovered] = rgb(26, 88, 46);
  c[ImGuiCol_ButtonActive] = rgb(46, 140, 72);
  c[ImGuiCol_Header] = rgb(14, 48, 26);
  c[ImGuiCol_HeaderHovered] = rgb(26, 88, 46);
  c[ImGuiCol_HeaderActive] = rgb(46, 140, 72);
  c[ImGuiCol_Separator] = rgb(60, 190, 100, 0.45f);
  c[ImGuiCol_SeparatorHovered] = rgb(100, 230, 130, 0.8f);
  c[ImGuiCol_SeparatorActive] = rgb(150, 255, 175);
  c[ImGuiCol_ResizeGrip] = rgb(60, 190, 100, 0.25f);
  c[ImGuiCol_ResizeGripHovered] = rgb(100, 230, 130, 0.6f);
  c[ImGuiCol_ResizeGripActive] = rgb(150, 255, 175, 0.9f);
  c[ImGuiCol_Tab] = rgb(8, 30, 17);
  c[ImGuiCol_TabHovered] = rgb(30, 100, 52);
  c[ImGuiCol_TabActive] = rgb(22, 76, 40);
  c[ImGuiCol_TabUnfocused] = rgb(6, 22, 12);
  c[ImGuiCol_TabUnfocusedActive] = rgb(14, 50, 27);
  c[ImGuiCol_PlotLines] = rgb(100, 230, 130);
  c[ImGuiCol_PlotHistogram] = rgb(255, 185, 60);
  c[ImGuiCol_TableHeaderBg] = rgb(10, 36, 20);
  c[ImGuiCol_TableBorderStrong] = rgb(60, 190, 100, 0.5f);
  c[ImGuiCol_TableBorderLight] = rgb(60, 190, 100, 0.25f);
  c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = rgb(40, 140, 70, 0.07f);
  c[ImGuiCol_TextSelectedBg] = rgb(60, 190, 100, 0.35f);
  c[ImGuiCol_NavHighlight] = rgb(255, 185, 60);
  c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.6f);
}

// полосы развёртки и тусклая рамка-«кинескоп» поверх окна
void DrawCrt() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize(), b(a.x + sz.x, a.y + sz.y);
  dl->PushClipRect(a, b, false);
  for (float y = a.y + 1; y < b.y; y += 3) dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(0, 0, 0, 38));
  // бегущая светлая полоса, как у старого монитора
  const float period = 6.f, t = float(fmod(ImGui::GetTime(), double(period))) / period, y = a.y + t * (sz.y + 60) - 30;
  dl->AddRectFilledMultiColor(ImVec2(a.x, y - 30), ImVec2(b.x, y), IM_COL32(120, 255, 150, 0), IM_COL32(120, 255, 150, 0),
                              IM_COL32(120, 255, 150, 10), IM_COL32(120, 255, 150, 10));
  // уголки-скобы
  const float k = 10.f;
  const ImU32 col = IM_COL32(150, 255, 175, 170);
  dl->AddLine(a, ImVec2(a.x + k, a.y), col, 2.f), dl->AddLine(a, ImVec2(a.x, a.y + k), col, 2.f);
  dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - k, a.y), col, 2.f), dl->AddLine(ImVec2(b.x - 1, a.y), ImVec2(b.x - 1, a.y + k), col, 2.f);
  dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(a.x + k, b.y - 1), col, 2.f), dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - k), col, 2.f);
  dl->AddLine(ImVec2(b.x, b.y - 1), ImVec2(b.x - k, b.y - 1), col, 2.f), dl->AddLine(ImVec2(b.x - 1, b.y), ImVec2(b.x - 1, b.y - k), col, 2.f);
  dl->PopClipRect();
}

// курсор: угловатая стрелка зелёного фосфора с тёмной заливкой и «пингом» датчика движения при клике
void DrawCursor() {
  ImGuiIO& io = ImGui::GetIO();
  if (!ImGui::IsMousePosValid()) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 p = io.MousePos;
  const float s = 1.15f;
  auto P = [&](float x, float y) { return ImVec2(p.x + x * s, p.y + y * s); };
  const ImU32 fill = IM_COL32(6, 24, 12, 235), line = IM_COL32(150, 255, 175, 255), glow = IM_COL32(120, 255, 150, 60);
  if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput) {  // в поле ввода — тонкая «I»
    dl->AddLine(P(0, -8), P(0, 8), line, 2.f);
    dl->AddLine(P(-3, -8), P(3, -8), line, 2.f);
    dl->AddLine(P(-3, 8), P(3, 8), line, 2.f);
    return;
  }
  const ImVec2 pts[7] = { P(0, 0), P(0, 16), P(4.5f, 12.5f), P(7.5f, 19), P(10, 18), P(7, 11.5f), P(12, 11.5f) };
  dl->AddTriangleFilled(pts[0], pts[1], pts[6], fill);
  dl->AddQuadFilled(pts[2], pts[3], pts[4], pts[5], fill);
  dl->AddPolyline(pts, 7, glow, ImDrawFlags_Closed, 4.f);
  dl->AddPolyline(pts, 7, line, ImDrawFlags_Closed, 1.5f);
  // «пинг» после клика — расходящееся кольцо, как отметка на датчике движения
  static double clickAt = -10;
  static ImVec2 clickPos;
  if (ImGui::IsMouseClicked(0)) clickAt = ImGui::GetTime(), clickPos = p;
  const float e = float(ImGui::GetTime() - clickAt);
  if (e < 0.45f) {
    const int a = int(200 * (1.f - e / 0.45f));
    dl->AddCircle(clickPos, 4.f + e * 60.f, IM_COL32(150, 255, 175, a), 24, 1.5f);
  }
}

// всегда true: F2 (вкл/выкл HUD) читается внутри DrawHud, поэтому её надо звать каждый кадр
bool HudEnabled() { return true; }

// F2 — HUD вкл/выкл, F3 — карта вентиляции вкл/выкл (по фронту нажатия; работают и в простом управлении)
static void HudKeys() {
  static bool f2, f3;
  const bool k2 = HotK(K_HUD), k3 = HotK(K_MAP);
  if (k2 && !f2) s_cfg.hud = !s_cfg.hud, SaveCfg();
  if (k3 && !f3) s_cfg.radar = !s_cfg.radar, SaveCfg();
  f2 = k2, f3 = k3;
}

// подсказки «что нажимать сейчас» — зависят от того, где камера и где Чужой.
// В простом управлении — только главное, по 2–4 строчки.
static void DrawContextHints(const Snap& s) {
  const bool adv = !g_simpleKeys;
  if (NowMs() - cutsceneRefusedAt.load() < 3000)
    ImGui::TextColored(kWarnCol, "%s", TR("Идёт катсцена — дождись её конца", "A cutscene is playing — wait until it ends"));
  if (s.hugCtl && s.cam != 0) {  // играешь за лицехвата
    Hint("WASD", TR("— бежать   Ctrl тихо · Shift быстро · Пробел во весь опор", "— run   Ctrl sneak · Shift fast · Space full speed"));
    Hint(TR("ЛКМ / ПКМ", "LMB / RMB"), TR("— прыжок / сменить цель", "— leap / next target"));
    Hint(TR("колесо", "wheel"), TR("— камера ближе/дальше (Shift — выше, Ctrl — вбок)", "— camera closer/farther (Shift — height, Ctrl — sideways)"));
    Hint(KN(K_HUGGER), TR("— следующий лицехват", "— next facehugger"));
    Hint(KN(K_SWAP), TR("— вернуться к Аманде", "— back to Amanda"));
    return;
  }
  if (!s.level && !s.hugs.empty()) {
    Hint(KN(K_HUGGER), TR("— играть за лицехвата", "— play as a facehugger"));
    Hint(KN(K_PHOTO), TR("— фоторежим", "— photo mode"));
    return;
  }
  if (!s.level) {
    ImGui::TextColored(kDimCol, "%s", TR("Загрузи уровень с Чужим (меню мода — страница «Уровни»)",
                                         "Load a level with the Alien (mod menu — Levels page)"));
    return;
  }
  if (!s.hasNpc) {
    ImGui::TextColored(kWarnCol, "%s", TR("Ищу Чужого...", "Looking for the Alien..."));
    return;
  }
  if (s.ventGo) {
    ImGui::TextColored(kDimCol, "%s", TR("Чужой лезет в вентиляцию...", "The Alien is climbing into a vent..."));
    if (s.cam != 0) Hint(KN(K_VENT), TR("— передумал, остаться", "— never mind, stay"));
    return;
  }
  if (s.ventCome) {
    ImGui::TextColored(kDimCol, "%s", TR("Чужой вылезает из вентиляции...", "The Alien is coming out of the vent..."));
    if (s.cam != 0) Hint(KN(K_VENT), TR("— передумал, остаться внутри", "— never mind, stay inside"));
    return;
  }
  if (s.ventStay) {
    if (s.cam == 0) {
      Hint(KN(K_SWAP), TR("— управлять Чужим в вентиляции", "— control the Alien in the vents"));
      return;
    }
    Hint("WASD", TR("— ползти по вентиляции (Shift быстрее, Пробел бегом)", "— crawl through the vents (Shift faster, Space run)"));
    Hint(KN(K_VENT), TR("— вылезти здесь", "— come out here"));
    Hint(KN(K_VENT_AMANDA), TR("— вылезти рядом с Амандой", "— come out near Amanda"));
    Hint(KN(K_MAP), s_cfg.radar ? TR("— спрятать карту", "— hide the map") : TR("— показать карту", "— show the map"));
    return;
  }
  if (s.cam == 0) {
    Hint(KN(K_SWAP), TR("— играть за Чужого", "— play as the Alien"));
    if (!s.hugs.empty()) Hint(KN(K_HUGGER), TR("— играть за лицехвата", "— play as a facehugger"));
    if (adv) Hint(TR("стрелки", "arrows"), TR("— вести Чужого, не забирая камеру", "— steer the Alien without taking the camera"));
    Hint(KN(K_PHOTO), TR("— фоторежим", "— photo mode"));
    return;
  }
  Hint("WASD", TR("— идти   Ctrl тихо · Shift быстро · Пробел бег", "— walk   Ctrl sneak · Shift fast · Space run"));
  Hint(TR("ЛКМ / ПКМ", "LMB / RMB"), TR("— атака / сменить цель", "— attack / next target"));
  Hint(KN(K_VENT), TR("— спрятаться в вентиляцию", "— hide in a vent"));
  Hint(KN(K_VIEW), s.cam == 2 ? TR("— вид из головы", "— head view") : TR("— вид из-за спины", "— view from behind"));
  Hint(TR("колесо", "wheel"), s.cam == 2 ? TR("— камера ближе/дальше (Shift — выше, Ctrl — вбок)", "— camera closer/farther (Shift — height, Ctrl — sideways)")
                                         : TR("— сдвинуть камеру (Shift — выше, Ctrl — вбок)", "— move the camera (Shift — height, Ctrl — sideways)"));
  Hint(KN(K_UNSTUCK), TR("— застрял? встать рядом с Амандой", "— stuck? stand near Amanda"));
  if (adv) Hint(KN3(K_STRAFE, K_FOLLOW, K_PATH), TR("— стрейф / Аманда следует / в шахты", "— strafe / Amanda follows / into ducts"));
  Hint(KN(K_SWAP), TR("— вернуться к Аманде", "— back to Amanda"));
}

// настройка камеры поменялась (колесо или меню) — записать в .ini, когда секунду ничего не менялось
static void CamSaveTick(const Snap& s) {
  static CamCfg last;
  static ms_t changedAt = 0;
  if (!s.ready) return;
  static bool lastHug = false;
  if (!SameCam(s.camCfg, last) || s.camHug != lastHug) last = s.camCfg, lastHug = s.camHug, changedAt = NowMs();
  else if (!SameCam(last, lastHug ? s_camSavedHug : s_camSaved) && NowMs() - changedAt > 1000) SaveCam(last, lastHug);
  // то же для игровых настроек
  static std::vector<float> lastG;
  static ms_t changedG = 0;
  const std::vector<GameVal> gv = GameValues(s);
  std::vector<float> cur;
  for (const GameVal& g : gv) cur.push_back(g.v);
  if (s_gameSaved.empty()) s_gameSaved = cur;  // первый снимок — то, что загрузили
  if (cur != lastG) lastG = cur, changedG = NowMs();
  else if (cur != s_gameSaved && NowMs() - changedG > 1000) {
    const std::wstring f = CfgPath();
    for (const GameVal& g : gv) FSet(f, L"game", g.key, g.v);
    s_gameSaved = cur;
  }
}

// ---------------- обучение: по шагу за раз, следующий — когда игрок сделал то, что просили ----------------
// Шаг хранится в .ini (s_cfg.tutorial), 99 — пройдено или пропущено; заново — кнопкой в меню («Экран»).
enum { TUT_SWAP, TUT_MOVE, TUT_CAMERA, TUT_ATTACK, TUT_VENT, TUT_INSIDE, TUT_FINAL, TUT_COUNT, TUT_DONE = 99 };
static void TutorialNext(int to) {
  s_cfg.tutorial = to;
  SaveCfg();
}
static void DrawTutorial(const Snap& s) {
  int st = s_cfg.tutorial;
  if (st < 0 || st >= TUT_COUNT || !s.level || !s.hasNpc || s.photoOn) return;
  static int shown = -1;
  static float stepTime = 0, moveTime = 0;
  static CamCfg camAt;
  static bool wasInside = false;
  const float dt = ImGui::GetIO().DeltaTime;
  if (shown != st) shown = st, stepTime = 0, moveTime = 0, camAt = s.camCfg, wasInside = false;
  stepTime += dt;
  if (s.moving) moveTime += dt;
  // выполнено?
  switch (st) {
    case TUT_SWAP: if (s.cam != 0) return TutorialNext(TUT_MOVE); break;
    case TUT_MOVE: if (moveTime > 1.5f) return TutorialNext(TUT_CAMERA); break;
    case TUT_CAMERA: if (!SameCam(s.camCfg, camAt) || stepTime > 15.f) return TutorialNext(TUT_ATTACK); break;
    case TUT_ATTACK: if (s.attacking || stepTime > 30.f) return TutorialNext(TUT_VENT); break;
    case TUT_VENT: if (s.ventGo || s.ventStay) return TutorialNext(TUT_INSIDE); break;
    case TUT_INSIDE:
      if (s.ventStay) wasInside = true;
      if ((wasInside && !s.ventStay && !s.ventGo) || stepTime > 90.f) return TutorialNext(TUT_FINAL);
      break;
    case TUT_FINAL: if (stepTime > 10.f) return TutorialNext(TUT_DONE); break;
  }
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, 40), ImGuiCond_Always, ImVec2(0.5f, 0.f));
  ImGui::SetNextWindowBgAlpha(0.75f);
  const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##alientutorial", nullptr, f)) {
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(ImVec4(0.45f, 1.f, 0.45f, 1.f), TR("Обучение  %d / %d", "Tutorial  %d / %d"), st + 1, int(TUT_COUNT));
    const bool needAlien = st >= TUT_MOVE && st <= TUT_VENT && s.cam == 0;
    if (needAlien) {
      Hint(KN(K_SWAP), TR("— вернись за Чужого, чтобы продолжить", "— take the Alien back to continue"));
    } else {
      switch (st) {
        case TUT_SWAP: Hint(KN(K_SWAP), TR("— стань Чужим", "— become the Alien")); break;
        case TUT_MOVE:
          Hint("WASD", TR("— иди", "— walk"));
          Hint(TR("Ctrl / Shift / Пробел", "Ctrl / Shift / Space"), TR("— тихо / быстро / бег", "— sneak / fast / run"));
          break;
        case TUT_CAMERA:
          Hint(TR("мышь", "mouse"), TR("— крутить камеру", "— turn the camera"));
          Hint(TR("колесо", "wheel"), TR("— камера ближе / дальше (попробуй!)", "— camera closer / farther (try it!)"));
          break;
        case TUT_ATTACK:
          Hint(TR("ЛКМ", "LMB"), TR("— атаковать", "— attack"));
          Hint(TR("ПКМ", "RMB"), TR("— сменить цель (кто цель — видно внизу)", "— next target (shown at the bottom)"));
          break;
        case TUT_VENT: Hint(KN(K_VENT), TR("— спрятаться в вентиляцию", "— hide in a vent")); break;
        case TUT_INSIDE:
          if (!s.ventStay) {
            ImGui::TextUnformatted(TR("Чужой лезет в вентиляцию...", "The Alien is climbing into a vent..."));
            break;
          }
          if (s.cam == 0) {
            Hint(KN(K_SWAP), TR("— управлять Чужим в вентиляции", "— control the Alien in the vents"));
            break;
          }
          Hint("WASD", TR("— ползти по вентиляции", "— crawl through the vents"));
          Hint(KN(K_VENT), TR("— вылезти здесь", "— come out here"));
          Hint(KN(K_VENT_AMANDA), TR("— вылезти рядом с Амандой", "— come out near Amanda"));
          break;
        case TUT_FINAL:
          Hint(KN(K_UNSTUCK), TR("— если застрял", "— if you get stuck"));
          Hint(KN(K_SWAP), TR("— вернуться к Аманде", "— back to Amanda"));
          Hint(KN(K_MENU), TR("— меню: камера, экран, клавиши", "— menu: camera, screen, keys"));
          ImGui::TextColored(kKeyCol, "%s", TR("Всё! Удачной охоты.", "That's it! Happy hunting."));
          break;
      }
    }
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled("%s", TR("Пропустить: меню мода → «Экран» → «Пропустить обучение»", "Skip: mod menu → «Screen» → «Skip the tutorial»"));
    DrawCrt();
  }
  ImGui::End();
}

// ---------------- переназначение клавиш ----------------
static int s_capture = -1;          // какое действие ждёт новую клавишу
static bool s_captureReleased;      // сначала ждём, пока отпустят всё (клик мышью по кнопке)
static int s_keysFrame = -10;       // кадр, в котором рисовался список (закрыли меню — ожидание отменяется)
static std::string s_keysMsg;
static void CaptureCancel() {
  s_capture = -1;
  g_rebinding = false;
}
static bool ReservedKey(int vk) {  // постоянные клавиши: движение, мышь, меню, модификаторы
  switch (vk) {
    case VK_LBUTTON: case VK_RBUTTON: case VK_MBUTTON: case VK_ESCAPE: case VK_INSERT: case VK_SPACE:
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_RETURN: case VK_BACK: case 'W': case 'A': case 'S': case 'D':
      return true;
  }
  return false;
}
static void CaptureTick() {  // поток рендера, каждый кадр
  if (s_capture < 0) return;
  if (ImGui::GetFrameCount() - s_keysFrame > 2) return CaptureCancel();  // меню закрыли
  int pressed = 0;
  for (int vk = 1; vk < 255; vk++) {
    if (vk == VK_PACKET || vk == 0x07 || vk == 0x0A || vk == 0x0B) continue;
    if (GetAsyncKeyState(vk) & 0x8000) {
      pressed = vk;
      if (!ReservedKey(vk) || vk == VK_ESCAPE) break;  // модификатор (Shift) и сама клавиша: берём клавишу
    }
  }
  if (!s_captureReleased) {
    if (!pressed) s_captureReleased = true;
    return;
  }
  if (!pressed) return;
  if (pressed == VK_ESCAPE) {
    s_keysMsg.clear();
    return CaptureCancel();
  }
  if (ReservedKey(pressed)) {
    s_keysMsg = KeyName(pressed) + TR(" — постоянная клавиша, выбери другую", " is a fixed key, pick another one");
    s_captureReleased = false;
    return;
  }
  if (s_capture == K_MENU && (pressed == VK_XBUTTON1 || pressed == VK_XBUTTON2)) {  // меню ловит окно по WM_KEYUP — только клавиатура
    s_keysMsg = TR("Меню — только клавиша клавиатуры", "The menu needs a keyboard key");
    s_captureReleased = false;
    return;
  }
  const int old = g_keys[s_capture];
  for (int i = 0; i < K_COUNT; i++)  // клавиша уже занята — меняем местами
    if (i != s_capture && g_keys[i] == pressed) {
      g_keys[i] = old;
      s_keysMsg = std::string(TR("Поменял местами с другим действием: теперь там ", "Swapped with another action, which now uses ")) + KeyName(old);
    }
  g_keys[s_capture] = pressed;
  SaveKeys();
  Log("key %d -> 0x%02X", s_capture, pressed);
  CaptureCancel();
}

static void DrawPhotoFrame(const Snap& s);
static void DrawAlienHud(const Snap& s);
void DrawHud() {
  CaptureTick();
  HudKeys();
  Snap s = GetSnap();
  CamSaveTick(s);
  DrawPhotoFrame(s);  // каше / сетка / отметка фокуса — и при скрытом HUD мода
  if (GameStalled()) return;  // меню паузы / загрузка: ничего поверх (тестер: статус и карта закрывали меню паузы)
  if (s.photoOn && !s.photoToast.empty()) {  // фоторежим: короткая подпись после Tab / PgUp / PgDn / T (1.5 с)
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(s.photoToast.c_str());
    const ImVec2 p0(ds.x * 0.5f - ts.x * 0.5f - 10, 18), p1(ds.x * 0.5f + ts.x * 0.5f + 10, 18 + ts.y + 8);
    dl->AddRectFilled(p0, p1, IM_COL32(10, 12, 14, 170), 4.f);
    dl->AddText(ImVec2(p0.x + 10, p0.y + 4), IM_COL32(255, 214, 150, 235), s.photoToast.c_str());
  }
  if (s_cfg.hud) DrawTutorial(s);
  const HudCfg& c = s_cfg;
  if (!c.hud) return;
  if (s.photoOn) return;  // в кадре фоторежима — ничего лишнего
  DrawAlienHud(s);        // за Чужого — свой HUD внизу по центру
  const SpawnInfo& sp = s.spawn;
  const bool showStatus = s.level && c.status, showDetails = s.level && s.hasNpc && c.details, showHints = c.hints;
  const bool showRadar = c.radar && s.radar.on, showSpawn = c.spawn && s.level && (sp.recent || !sp.msg.empty());
  const bool showF1 = c.spawn && !s.f1text.empty();
  const bool showTarget = c.target && s.hasNpc && s.cam != 0 && !s.ventStay && !s.target.empty();
  if (!showStatus && !showDetails && !showHints && !showRadar && !showSpawn && !showF1 && !showTarget && !s.ventFail) return;
  // угол экрана — из настроек
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  const bool right = c.corner & 1, top = c.corner >= 2;
  ImGui::SetNextWindowPos(ImVec2(right ? ds.x - 12 : 12, top ? 12 : ds.y - 12), ImGuiCond_Always,
                          ImVec2(right ? 1.f : 0.f, top ? 0.f : 1.f));
  ImGui::SetNextWindowBgAlpha(c.alpha);
  const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##alienhud", nullptr, f)) {
    bool sep = false;  // разделитель перед следующим блоком, если выше уже что-то есть
    auto Block = [&] {
      if (sep) ImGui::Separator();
      sep = true;
    };
    if (showStatus) {
      Block();
      if (s.hasNpc) ImGui::Text("%s %s   ·   %s %s", TR("Чужой:", "Alien:"), VentName(s), TR("камера:", "camera:"), CamName(s.cam));
      else ImGui::TextColored(kWarnCol, "%s", TR("Чужой не найден", "Alien not found"));
    }
    if (showTarget) {
      Block();
      ImGui::Text("%s", TR("Цель:", "Target:"));
      ImGui::SameLine();
      ImGui::TextColored(s.targetAmanda ? kWarnCol : ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", s.target.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled(TR("%.0f м   (ПКМ — другая)", "%.0f m   (RMB — another)"), s.targetDist);
    }
    if (showDetails) {
      Block();
      ImGui::Text("%s %s%s   ·   %s %s", TR("ИИ:", "AI:"), AiName(s.ai), s.duct ? TR("   [шахта]", "   [duct]") : "",
                  TR("стрейф:", "strafe:"), StrafeText(s).c_str());
      ImGui::Text("%s %s   ·   %s %s", TR("Аманда следует:", "Amanda follows:"), OnOff(s.follow), TR("режим пути:", "path mode:"),
                  OnOff(s.path));
    }
    if (showHints) {
      Block();
      DrawContextHints(s);
    }
    if (s.ventFail) {
      Block();
      ImGui::TextColored(kWarnCol, "%s", TR("Не вылез: три ближайшие вентиляции не выпустили Чужого.",
                                            "Did not come out: the three nearest vents did not let the Alien out."));
      ImGui::TextColored(kWarnCol, "%s", TR("Подползи к другой вентиляции и жми E ещё раз.", "Crawl to another vent and press E again."));
    }
    if (showRadar) {
      Block();
      DrawRadar(s.radar);
      RadarLegend();
    }
    if (showSpawn) {  // после F7/F8 — список спавна
      Block();
      if (sp.scanning) {
        ImGui::Text("%s %d%%", TR("Спавн: ищу персонажей уровня...", "Spawn: looking for level characters..."), sp.pct);
      } else if (sp.found.empty()) {
        ImGui::TextColored(kDimCol, "%s",
                           sp.done ? TR("Спавн: ничего не нашёл (Ctrl+F7 — заново)", "Spawn: nothing found (Ctrl+F7 — search again)")
                                   : TR("Спавн: F7 — найти персонажей уровня", "Spawn: F7 — find level characters"));
      } else {
        ImGui::Text("%s %u/18) — %s", TR("Спавн (персонажей", "Spawn (characters"), unsigned(s.chars.size()),
                    TR("F7/Shift+F7 выбор, F8 спавн:", "F7/Shift+F7 select, F8 spawn:"));
        size_t n = sp.found.size(), sel = size_t(std::max(sp.sel, 0)), from = sel >= 5 ? sel - 5 : 0, to = std::min(n, from + 10);
        for (size_t i = from; i < to; i++)
          ImGui::TextColored(i == sel ? kKeyCol : kDimCol, "%s%s", i == sel ? "> " : "  ", SpawnName(sp, i).c_str());
      }
      if (!sp.msg.empty()) ImGui::TextColored(kWarnCol, "%s", sp.msg.c_str());
    }
    if (showF1) {
      Block();
      ImGui::TextUnformatted(s.f1text.c_str());
    }
    if (c.footer) ImGui::TextDisabled(TR("%s — спрятать   ·   %s — меню", "%s — hide   ·   %s — menu"), KN(K_HUD), KN(K_MENU));
    DrawCrt();
  }
  ImGui::End();
}

// ---- меню (booster_ui: оформление друга, палитра терминала Seegson) ----
// ширина кнопки, чтобы n штук встали в ряд (BUI::SameLine — 8 px между ними); считается от всей строки, а не от
// остатка после SameLine — поэтому одна и та же для каждой кнопки ряда
static float RowW(int n) {
  const float full = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
  return std::floor((full - 8.f * float(n - 1)) / float(n));
}
// строка «клавиша — что делает»
static void KeyHint(const char* key, const char* what) {
  BUI::KeyCap(key);
  BUI::SameLine(10.f);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
  BUI::TextDim("%s", what);
}

void DrawLogBody() {
  static std::vector<std::string> lines;
  static unsigned seq = 0;
  static bool autoScroll = true;
  seq = LogRingCopy(lines, seq);
  BUI::Toggle(TR("Автопрокрутка###as", "Auto-scroll###as"), &autoScroll);
  if (BUI::Button(TR("Копировать всё###cp", "Copy everything###cp"), BUI::BtnFilled)) {
    std::string all;
    for (const std::string& l : lines) all += l + "\n";
    ImGui::SetClipboardText(all.c_str());
  }
  BUI::SameLine();
  BUI::Hint("%s", TR("полный лог — AlienModUI.log рядом с модом (пришли его с описанием бага)",
                     "full log — AlienModUI.log next to the mod (send it with the bug report)"));
  BUI::Separator();
  ImGui::BeginChild("##log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
  for (const std::string& l : lines) BUI::TextDim("%s", l.c_str());
  if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

void DrawLog(bool* open) {
  static float t = 0.f;
  t = BUI::Anim(ImGui::GetID("##logwin"), *open ? 1.f : 0.f, 12.f);
  if (BUI::BeginWindow("###alienlog", TR("Лог мода", "Mod log"), "ALIEN PLAYABLE", ImVec2(680, 380), open, t)) {
    ImGui::Dummy(ImVec2(0.f, 0.f));  // BeginContent встаёт SameLine к последнему элементу — не к крестику
    if (BUI::BeginContent("##logc")) DrawLogBody();
    BUI::EndContent();
  }
  BUI::EndWindow();
}

void DrawPanel(bool* open) {
  static float t = 0.f;
  t = BUI::Anim(ImGui::GetID("##panelwin"), *open ? 1.f : 0.f, 12.f);
  if (BUI::BeginWindow("###alienpanel", TR("Управление", "Controls"), "ALIEN PLAYABLE", ImVec2(640, 700), open, t)) {
    ImGui::Dummy(ImVec2(0.f, 0.f));  // BeginContent встаёт SameLine к последнему элементу — не к крестику
    if (BUI::BeginContent("##panelc")) DrawPanelBody();
    BUI::EndContent();
  }
  BUI::EndWindow();
}

void DrawLanguage() {
  static const char* kLangs[] = { "Русский", "English" };
  int lang = g_lang;
  if (BUI::Segmented("##lang", kLangs, 2, &lang, 190.f)) g_lang = lang, SaveCfg();
}

// раздел: BUI::BeginSection; содержимое — внутри if, в конце SectionEnd(). Ключ раздела — хвост после ###, чтобы
// раскрытые разделы оставались раскрытыми и после смены языка
static bool Section(const char* label, bool defaultOpen = false, const char* hint = nullptr, BUI::Icon ico = BUI::ICO_NONE,
                    const char* badge = nullptr) {
  const char* id = std::strstr(label, "###");
  return BUI::BeginSection(id ? id + 3 : label, label, hint, defaultOpen, badge, ico);
}
static void SectionEnd() { BUI::EndSection(); }

// большая кнопка на полстроки
static bool BigBtn(const char* label, bool enabled, bool active = false) {
  const float w = RowW(2);
  return active ? BUI::ToggleButton(label, true, enabled, w) : BUI::Button(label, BUI::BtnFilled, enabled, w);
}
static bool CfgCheck(const char* label, bool* v, const char* tip = nullptr) {
  const bool r = BUI::Toggle(label, v, nullptr);
  if (tip) Tip(tip);
  if (r) SaveCfg();
  return r;
}
// режим ИИ: три кнопки (ручное / не на Аманду / полный), значения мода 0 / 3 / 2
static bool AiSegmented(const char* id, int* mode, const char* ru1, const char* en1) {
  const char* ru[] = { "Ручное", ru1, "Полный ИИ" };
  const char* en[] = { "Manual", en1, "Full AI" };
  const int kVal[] = { 0, 3, 2 };
  int sel = *mode == 3 ? 1 : *mode == 2 ? 2 : *mode < 0 ? -1 : 0;  // -1 — у типов разные режимы: не подсвечен ни один
  if (!BUI::Segmented(id, g_lang ? en : ru, 3, &sel, ImGui::GetContentRegionAvail().x)) return false;
  *mode = kVal[sel];
  return true;
}

static void DrawKeys() {
  s_keysFrame = ImGui::GetFrameCount();
  struct ActInfo { int act; const char* what; bool main; };
  const ActInfo kActs[] = {
    { K_SWAP, TR("Играть за Чужого / вернуться к Аманде", "Play as the Alien / back to Amanda"), true },
    { K_VENT, TR("Вентиляция: спрятаться / вылезти здесь / отменить", "Vents: hide / come out here / cancel"), true },
    { K_VENT_AMANDA, TR("Вылезти из вентиляции рядом с Амандой", "Come out of the vents near Amanda"), true },
    { K_VIEW, TR("Вид: из-за спины / из головы", "View: from behind / from the head"), true },
    { K_UNSTUCK, TR("Застрял — встать рядом с Амандой", "Stuck — stand near Amanda"), true },
    { K_HUD, TR("Спрятать / показать всё на экране", "Hide / show everything on screen"), true },
    { K_MAP, TR("Карта вентиляции", "Vent map"), true },
    { K_PHOTO, TR("Фоторежим", "Photo mode"), true },
    { K_STRAFE, TR("Стрейф вкл/выкл", "Strafe on/off"), false },
    { K_FOLLOW, TR("Аманда следует за Чужим", "Amanda follows the Alien"), false },
    { K_PATH, TR("Режим пути — заходить в шахты", "Path mode — enter ducts"), false },
    { K_CHARS, TR("Список персонажей", "Character list"), false },
    { K_AI, TR("ИИ для всех по кругу: ручное → не на Аманду → полный", "AI for everyone, cycle: manual → not Amanda → full"), false },
    { K_PANIC, TR("Аварийный сброс", "Panic reset"), false },
    { K_SPAWN_FIND, TR("Спавн: найти и листать (Shift — назад, Ctrl — искать заново)", "Spawn: find and cycle (Shift — back, Ctrl — search again)"), false },
    { K_SPAWN, TR("Спавн: заспавнить (Shift — тот же ещё раз)", "Spawn: spawn (Shift — same one again)"), false },
    { K_LOOK, TR("Держать — Чужой осматривается", "Hold — the Alien looks around"), true },
    { K_TURN180, TR("Резкий разворот на 180°", "Quick 180° turn"), true },
    { K_SOUND, TR("Голос (Чужой, андроид; Shift — случайный)", "Voice (Alien, android; Shift — random)"), true },
    { K_HUGGER, TR("Играть за лицехвата / андроида (ещё раз — следующий)", "Play as a facehugger / android (again — the next one)"), true },
    { K_MENU, TR("Меню мода (Insert открывает его всегда)", "Mod menu (Insert always opens it too)"), true },
  };
  BUI::TextWrapped("%s", TR("Нажми на клавишу справа, потом новую клавишу (Esc — отмена). Занятая — поменяется местами.",
                            "Click the key on the right, then press a new one (Esc — cancel). A taken key gets swapped."));
  if (!s_keysMsg.empty()) BUI::TextCol(BUI::P().warning, "%s", s_keysMsg.c_str());
  BUI::Space(4.f);
  for (const ActInfo& a : kActs) {
    ImGui::PushID(a.act);
    const bool waiting = s_capture == a.act;
    char id[224];
    snprintf(id, sizeof id, "%s###ka%d", a.what, a.act);
    const std::string def = std::string(TR("по умолчанию: ", "default: ")) + KeyName(kKeyDefault[a.act]);
    if (BUI::KeyRow(id, KN(a.act), waiting, g_keys[a.act] != kKeyDefault[a.act] ? def.c_str() : nullptr)) {
      s_capture = a.act, s_captureReleased = false, g_rebinding = true;
      s_keysMsg.clear();
    }
    ImGui::PopID();
  }
  BUI::Space(6.f);
  if (BUI::Button(TR("Вернуть все клавиши по умолчанию###kdef", "Reset all keys to defaults###kdef"), BUI::BtnDanger)) {
    CaptureCancel();
    for (int i = 0; i < K_COUNT; i++) g_keys[i] = kKeyDefault[i];
    SaveKeys();
    s_keysMsg.clear();
  }
  BUI::Separator(TR("Постоянные — не меняются", "Fixed — not rebindable"));
  KeyHint("WASD", TR("идти;  Ctrl тихо · Shift быстро · Пробел бег", "walk;  Ctrl sneak · Shift fast · Space run"));
  KeyHint(TR("ЛКМ / ПКМ", "LMB / RMB"), TR("атака / сменить цель", "attack / next target"));
  KeyHint(TR("мышь, колесо", "mouse, wheel"), TR("камера; Shift/Ctrl + колесо — выше / вбок; СКМ — сброс",
                                                 "camera; Shift/Ctrl + wheel — height / sideways; MMB — reset"));
  KeyHint("Esc", TR("пауза игры: ввод у игры, Чужой / лицехват остаётся твоим",
                    "game pause: input goes to the game, the alien / facehugger stays yours"));
  KeyHint(TR("стрелки", "arrows"), TR("за Аманду: вести Чужого, не забирая камеру", "as Amanda: steer the Alien without taking the camera"));
  KeyHint("Insert", TR("это меню (и клавиша меню из списка выше)", "this menu (and the menu key above)"));
  KeyHint(TR("фоторежим", "photo mode"), TR("WASD, Q/E, Z/C/R, X, колесо, стрелки, Tab, T", "WASD, Q/E, Z/C/R, X, wheel, arrows, Tab, T"));
}

static void PageMain(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  if (g_wrongGame)
    BUI::TextWrappedCol(pal.warning, "%s", TR("Не Steam-версия игры: мод сделан под Steam-сборку от 17.02.2015 — часть функций или весь мод может не работать.",
                                              "Not the Steam build: the mod is made for the Steam build of 17 Feb 2015 — some features or the whole mod may not work."));
  DrawLanguage();
  BUI::SameLine(14.f);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7.f);  // по центру таблетки языка
  if (!s.ready) BUI::TextCol(pal.warning, "%s", TR("мод запускается...", "mod is starting..."));
  else if (!lvl && !s.hugs.empty())
    BUI::TextWrappedCol(pal.success, TR("Чужого нет; лицехватов и андроидов: %d — %s, чтобы играть за них",
                                        "no Alien; facehuggers and androids: %d — %s to play as them"),
                        int(s.hugs.size()), KN(K_HUGGER));
  else if (!lvl) BUI::TextCol(pal.warning, "%s", TR("загрузи уровень с Чужим", "load a level with the Alien"));
  else if (!s.hasNpc) BUI::TextCol(pal.warning, "%s", TR("ищу Чужого...", "looking for the Alien..."));
  else BUI::TextCol(pal.success, "%s", TR("всё готово, можно играть", "ready to play"));
  if (s.ready && !lvl && s.hugs.empty())
    BUI::TextWrapped("%s", TR("Например SCI_HospitalUpper — в разделе «Уровни» или из меню игры. "
                              "Если Чужой так и не находится — AlienModSetup.exe → «Установить» (кладёт _DIRECTORY_CONTENTS.BML в игру).",
                              "E.g. SCI_HospitalUpper — in the Levels section or from the game menu. "
                              "If the Alien is never found — AlienModSetup.exe → Install (it puts _DIRECTORY_CONTENTS.BML into the game)."));
  bool simple = g_simpleKeys;
  if (BUI::Toggle(TR("Простое управление###simple", "Simple controls###simple"), &simple)) g_simpleKeys = simple, SaveCfg();
  Tip(TR("Подсказки на экране — только про главные клавиши. Все клавиши из раздела «Клавиши» работают всегда.",
         "On-screen hints cover only the main keys. Every key from the Keys section always works."));
  bool hk = g_hotkeysOn;
  if (BUI::Toggle(TR("Клавиши мода вообще###hk", "Mod hotkeys at all###hk"), &hk)) g_hotkeysOn = hk, SaveCfg();
  Tip(TR("Выключи, если клавиши мода мешают. Движение WASD/стрелками и атака мышью работают всегда.",
         "Turn off if the mod's keys get in the way. WASD/arrow movement and mouse attack always work."));
  BUI::Separator(TR("Главное", "Main"));

  // ---------------- главное — большими кнопками ----------------
  const bool asAlien = s.cam != 0;
  if (BigBtn(asAlien ? Lbl(TR("Вернуться к Аманде", "Back to Amanda"), K_SWAP, "bplay")
                     : Lbl(TR("Играть за Чужого", "Play as the Alien"), K_SWAP, "bplay"), alien, asAlien))
    Push(C_CAM, asAlien ? 3 : 2);
  BUI::SameLine();
  const char* ventLbl = busy ? Lbl(TR("Отменить", "Cancel"), K_VENT, "bvent")
                             : s.ventStay ? Lbl(TR("Вылезти здесь", "Come out here"), K_VENT, "bvent")
                                          : Lbl(TR("Спрятаться в вентиляцию", "Hide in a vent"), K_VENT, "bvent");
  if (BigBtn(ventLbl, alien, busy || s.ventStay)) Push(busy ? C_VENT_CANCEL : s.ventStay ? C_VENT_EXIT : C_VENT_GO);
  if (BigBtn(TR("Атаковать   [ЛКМ]###batk", "Attack   [LMB]###batk"), alien && !s.ventStay)) Push(C_ATTACK);
  BUI::SameLine();
  if (s.ventStay) {
    if (BigBtn(Lbl(TR("Вылезти у Аманды", "Come out near Amanda"), K_VENT_AMANDA, "bamanda"), alien && !s.ventCome)) Push(C_VENT_EXIT_AMANDA);
  } else if (BigBtn(Lbl(TR("Фоторежим", "Photo mode"), K_PHOTO, "bphoto"), s.hasPlayer, s.photoOn)) {  // и без Чужого
    Push(C_PHOTO, s.photoOn ? 0 : 1);
  }
  if (!s.hugs.empty()) {  // лицехваты и андроиды на уровне
    if (BigBtn(s.hugCtl ? Lbl(TR("Следующий", "Next one"), K_HUGGER, "bhug")
                        : Lbl(TR("Играть за лицехвата / андроида", "Play as a facehugger / android"), K_HUGGER, "bhug"), true, s.hugCtl))
      Push(C_HUG_NEXT);
    if (s.hugCtl && !alien) {  // с Чужим на уровне эта кнопка уже есть выше
      BUI::SameLine();
      if (BigBtn(Lbl(TR("Вернуться к Аманде", "Back to Amanda"), K_SWAP, "bhugb"), true)) Push(C_HUGGER, 0, 0);
    }
  }
  BUI::Hint("%s", TR("Дальше всё само: внизу экрана подсказка, какие клавиши работают прямо сейчас.",
                     "The rest is automatic: the hint on screen shows which keys work right now."));
}

static void PageScreen(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  HudCfg& c = s_cfg;
  CfgCheck(Lbl(TR("Показывать всё поверх игры", "Show anything over the game"), K_HUD, "hud"), &c.hud,
           TR("Главный выключатель: без него на экране ничего нет", "Master switch: without it nothing is drawn"));
  BUI::PushDisabled(!c.hud);
  CfgCheck(TR("Подсказки клавиш###hints", "Key hints###hints"), &c.hints, TR("Что нажимать прямо сейчас", "What to press right now"));
  CfgCheck(Lbl(TR("Карта вентиляции", "Vent map"), K_MAP, "radar"), &c.radar,
           TR("Пока Чужой в вентиляции: вентиляции, его след, Аманда и выход. По умолчанию выключена",
              "While the Alien is in the vents: vents, his trail, Amanda and the exit. Off by default"));
  CfgCheck(TR("Статус (где Чужой, камера)###status", "Status (where the Alien is, camera)###status"), &c.status);
  CfgCheck(TR("Подробный статус###details", "Detailed status###details"), &c.details,
           TR("ИИ, стрейф, Аманда следует, режим пути", "AI, strafe, Amanda follows, path mode"));
  CfgCheck(TR("Списки спавна и персонажей###spawnl", "Spawn and character lists###spawnl"), &c.spawn,
           TR("Появляются после F7/F8 и F1", "Show up after F7/F8 and F1"));
  CfgCheck(TR("Строчка «спрятать · меню»###footer", "The «hide · menu» line###footer"), &c.footer);
  CfgCheck(TR("HUD Чужого, лицехвата, андроида (внизу по центру)###ahud", "Alien / facehugger / android HUD (bottom centre)###ahud"),
           &c.alienHud, TR("Режим движения, цель, стрелка на Аманду, клавиши", "Movement mode, target, arrow to Amanda, keys"));
  CfgCheck(TR("Имя цели Чужого###target", "The Alien's target###target"), &c.target, TR("Кого Чужой сейчас атакует и как далеко", "Who the Alien is after and how far"));
  static const char* kCornersRu[] = { "Слева снизу", "Справа снизу", "Слева сверху", "Справа сверху" };
  static const char* kCornersEn[] = { "Bottom left", "Bottom right", "Top left", "Top right" };
  if (BUI::Combo(TR("Угол экрана###corner", "Screen corner###corner"), &c.corner, g_lang ? kCornersEn : kCornersRu, 4)) SaveCfg();
  if (BUI::SliderF(TR("Фон###alpha", "Background###alpha"), &c.alpha, 0.f, 1.f, "%.2f")) SaveCfg();
  BUI::PopDisabled();
  BUI::Space(2.f);
  if (c.tutorial < TUT_COUNT) {
    if (BUI::Button(TR("Пропустить обучение###tuts", "Skip the tutorial###tuts"), BUI::BtnGhost)) TutorialNext(TUT_DONE);
  } else if (BUI::Button(TR("Пройти обучение заново###tutr", "Replay the tutorial###tutr"), BUI::BtnGhost)) {
    TutorialNext(TUT_SWAP);
  }
}

static void PageCamera(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  const float w3 = RowW(3);
  if (BUI::ToggleButton(Lbl(TR("Аманда", "Amanda"), K_SWAP, "c0"), s.cam == 0, alien, w3)) Push(C_CAM, 3);
  BUI::SameLine();
  if (BUI::ToggleButton(Lbl(TR("За спиной", "Behind"), K_VIEW, "c2"), s.cam == 2, alien, w3)) Push(C_CAM, 2);
  BUI::SameLine();
  if (BUI::ToggleButton(Lbl(TR("Из головы", "Head"), K_VIEW, "c1"), s.cam == 1, alien, w3)) Push(C_CAM, 1);
  BUI::Hint("%s", TR("В игре: колесо — ближе/дальше, Shift+колесо — выше/ниже, Ctrl+колесо — левее/правее,",
                     "In game: wheel — closer/farther, Shift+wheel — up/down, Ctrl+wheel — left/right,"));
  BUI::Hint("%s", TR("средняя кнопка — сброс. Запоминается само.", "middle button — reset. Saved automatically."));
  // пока ползунок тянут — своё значение, иначе — то, что стоит в моде (его меняет и колесо)
  static CamCfg edit;
  static bool editing = false;
  if (!editing) edit = s.camCfg;
  auto Send = [](const CamCfg& c) {
    const float v[6] = { c.dist, c.height, c.side, c.head[0], c.head[1], c.head[2] };
    Push(C_CAMCFG, 0, 0, v, 6);
  };
  bool ch = false, act = false;
  if (s.camHug)
    BUI::TextCol(pal.warning, "%s",
                 TR("Сейчас — камера лицехвата (у Чужого своя, она не меняется)", "Now — the facehugger camera (the Alien keeps his own)"));
  BUI::Separator(TR("За спиной", "Behind"));
  struct Preset { const char* name; float dist, height, side; };
  const Preset kPresets[] = {
    { TR("Обычная###pr0", "Normal###pr0"), 3.f, 2.f, 0.5f },
    { TR("Ближе###pr1", "Closer###pr1"), 2.f, 1.6f, 0.45f },
    { TR("Дальше###pr2", "Farther###pr2"), 5.f, 2.6f, 0.4f },
    { TR("Сверху###pr3", "From above###pr3"), 6.f, 4.2f, 0.f },
  };
  const Preset kHugPresets[] = {  // лицехват маленький
    { TR("Обычная###pr0", "Normal###pr0"), 1.4f, 0.55f, 0.f },
    { TR("Ближе###pr1", "Closer###pr1"), 0.9f, 0.4f, 0.f },
    { TR("Дальше###pr2", "Farther###pr2"), 2.5f, 0.9f, 0.f },
    { TR("Сверху###pr3", "From above###pr3"), 3.f, 2.2f, 0.f },
  };
  {
    const float wp = RowW(4);
    int i = 0;
    for (const Preset& p : s.camHug ? kHugPresets : kPresets) {
      if (i++) BUI::SameLine();
      if (BUI::Button(p.name, BUI::BtnTinted, true, wp)) edit.dist = p.dist, edit.height = p.height, edit.side = p.side, ch = true;
    }
  }
  if (BUI::Button(TR("Другое плечо###prs", "Other shoulder###prs"), BUI::BtnGhost)) edit.side = -edit.side, ch = true;
  ch |= BUI::SliderF(TR("Расстояние, м###cd", "Distance, m###cd"), &edit.dist, s.camHug ? 0.5f : 1.f, 8.f, "%.1f");
  act |= ImGui::IsItemActive();
  ch |= BUI::SliderF(TR("Высота, м###ch", "Height, m###ch"), &edit.height, s.camHug ? 0.1f : 0.3f, 4.5f, "%.1f");
  act |= ImGui::IsItemActive();
  ch |= BUI::SliderF(TR("Вбок (плечо), м###cs", "Sideways (shoulder), m###cs"), &edit.side, -2.f, 2.f, "%+.2f",
                     TR("Плюс — камера над правым плечом, минус — над левым", "Plus — over the right shoulder, minus — over the left"));
  act |= ImGui::IsItemActive();
  if (s.duct) BUI::Hint("%s", TR("сейчас в шахте — там мод сам ставит ближнюю камеру", "in a duct now — the mod uses a close camera there"));
  {
    static const char* kColRu[] = { "Выкл", "Мягкие", "Строгие" };
    static const char* kColEn[] = { "Off", "Soft", "Strict" };
    int cc = s.camCollide;
    BUI::Title(TR("Столкновения камеры со стенами", "Camera collision with walls"));
    if (BUI::Segmented("##camcol", g_lang ? kColEn : kColRu, 3, &cc, ImGui::GetContentRegionAvail().x)) Push(C_CAM_COLLIDE, cc);
    Tip(TR("«Мягкие» — камера подъезжает только к настоящим стенам и потолкам, задетый краем угол её не дёргает, ближе метра к "
           "голове не подходит. «Строгие» — как раньше (ещё и край навмеша: у ограждений тянуло к ногам). «Выкл» — сквозь стены",
           "«Soft» — the camera only moves in for real walls and ceilings, a corner it just grazes doesn't jerk it, and it stays "
           "a metre from the head. «Strict» — as before (also the navmesh edge, which pulled it to the feet at railings). «Off» — through walls"));
  }
  BUI::Separator(TR("Из головы", "Head"));
  {
    static const char* kFpRu[] = { "Прибита к кости головы", "Свободная (мышь)" };
    static const char* kFpEn[] = { "Attached to the head bone", "Free (mouse)" };
    int fm = s.fpFree ? 1 : 0;
    if (BUI::Segmented("##fpm", g_lang ? kFpEn : kFpRu, 2, &fm, ImGui::GetContentRegionAvail().x)) Push(C_FP_FREE, fm);
    Tip(TR("«Свободная» — камера у головы Чужого, но крутится мышью, как за спиной; Чужой идёт туда, куда смотришь. "
           "«Прибита к кости» — как в игре: кадр поворачивается вместе с головой",
           "«Free» — the camera sits at the Alien's head but you turn it with the mouse, like the camera behind; the Alien "
           "walks where you look. «Attached to the bone» — like the game: the view turns with the head"));
    if (s.fpFree) {
      static float sens = 1.f, infl = 0.2f, lim = 100.f;
      static bool ed = false, ed2 = false, ed3 = false;
      if (!ed) sens = s.fpSens;
      if (BUI::SliderF(TR("Чувствительность мыши###fps", "Mouse sensitivity###fps"), &sens, 0.2f, 3.f, "%.2f")) Push(C_FP_SENS, 0, 0, &sens, 1);
      ed = ImGui::IsItemActive();
      if (!ed2) infl = s.fpInfluence;
      if (BUI::SliderF(TR("Влияние кости головы (Head1)###fpinf", "Head bone influence (Head1)###fpinf"), &infl, 0.f, 1.f, "%.2f",
                       TR("0 — камера привязана к телу (не качается при шаге и осмотре), 1 — прямо за костью головы, как в игре",
                          "0 — the camera is tied to the body (no bobbing on steps and looks), 1 — right on the head bone, like the game")))
        Push(C_FP_OPT, 1, 0, &infl, 1);
      ed2 = ImGui::IsItemActive();
      if (!ed3) lim = s.fpYawLimit;
      if (BUI::SliderF(TR("Предел поворота головы, °###fplim", "Head turn limit, °###fplim"), &lim, 30.f, 180.f, "%.0f",
                       TR("Как у Аманды: дальше этого угла от тела взгляд не повернуть — сначала довернётся тело (развороты на "
                          "месте на странице «Движение»). 180 — без предела. С зажатым осмотром — до 170°",
                          "Like Amanda: you can't look further than this from the body — the body turns first (turns on the spot on "
                          "the Movement page). 180 — no limit. With look held — up to 170°")))
        Push(C_FP_OPT, 2, 0, &lim, 1);
      ed3 = ImGui::IsItemActive();
    }
    {
      bool hh = s.fpHideHead;
      if (BUI::Toggle(TR("Не рисовать свою голову###fphh", "Don't draw own head###fphh"), &hh)) {
        const float v = hh ? 1.f : 0.f;
        Push(C_FP_OPT, 0, 0, &v, 1);
      }
      Tip(TR("Из головы прячет череп, купол и «дробилку» (отдельные части модели), а всё, что ближе «отсечения» к камере (шея, "
             "пасть, край купола), не рисуется. Слюни и дым изо рта — эффекты, не часть модели",
             "In the head view hides the skull, the dome and the crusher (separate model parts), and nothing closer to the camera "
             "than the clip distance is drawn (neck, jaws, the dome's edge). Drool and breath smoke are effects, not model parts"));
      if (s.fpHideHead) {
        static float nc = 0.35f;
        static bool edn = false;
        if (!edn) nc = s.fpNearClip;
        if (BUI::SliderF(TR("Отсечение у камеры, м###fpnc", "Clip near the camera, m###fpnc"), &nc, 0.02f, 1.f, "%.2f",
                         TR("Ближняя плоскость камеры из головы: всё ближе не рисуется. Больше — меньше видно своей головы, но у "
                            "стен и в шахтах можно заглянуть сквозь стену",
                            "Near plane of the head camera: nothing closer is drawn. Larger hides more of the own head, but at "
                            "walls and in ducts you may see through the wall")))
          Push(C_FP_OPT, 3, 0, &nc, 1);
        edn = ImGui::IsItemActive();
        bool ns = s.fpNeckScale;
        if (BUI::Toggle(TR("Прятать шею, пасть и зубы (кости)###fpns", "Hide neck, jaws and teeth (bones)###fpns"), &ns)) {
          const float v = ns ? 1.f : 0.f;
          Push(C_FP_OPT, 4, 0, &v, 1);
        }
        Tip(TR("Кости шеи, головы и челюстей при отрисовке сжимаются в точку — шеи, пасти и зубов не видно, и отсечение у "
               "камеры можно поставить совсем маленьким (тогда не режет плечи и руки). Слюни и дым изо рта из головы снимаются",
               "The neck, head and jaw bones collapse to a point when drawn — no neck, jaws or teeth, and the clip near the "
               "camera can be very small (then it doesn't cut the shoulders and arms). Drool and breath are removed in the head view"));
      }
    }
  }
  ch |= BUI::SliderF(TR("Вперёд/назад###hz", "Forward/back###hz"), &edit.head[2], -1.5f, 1.5f, "%+.2f");
  act |= ImGui::IsItemActive();
  ch |= BUI::SliderF(TR("Вверх/вниз###hy", "Up/down###hy"), &edit.head[1], -1.5f, 1.5f, "%+.2f");
  act |= ImGui::IsItemActive();
  ch |= BUI::SliderF(TR("Влево/вправо###hx", "Left/right###hx"), &edit.head[0], -1.5f, 1.5f, "%+.2f");
  act |= ImGui::IsItemActive();
  editing = act;
  if (ch) Send(edit);
  const float w2 = RowW(2);
  if (BUI::Button(TR("Сбросить «за спиной»###crb", "Reset behind###crb"), BUI::BtnGhost, true, w2)) Push(C_CAMRESET, 2);
  BUI::SameLine();
  if (BUI::Button(TR("Сбросить «из головы»###crh", "Reset head###crh"), BUI::BtnGhost, true, w2)) Push(C_CAMRESET, 1);
}

static void PageMove(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  BUI::PushDisabled(!alien);
  bool strafe = s.fmode != 0;
  if (BUI::Toggle(Lbl(TR("Стрейф", "Strafe"), K_STRAFE, "st"), &strafe) && alien) Push(C_STRAFE, strafe ? 1 : 0);
  Tip(TR("Тело держится по камере: A/D — боком, S — спиной. В вентиляции и в шахте сам встаёт на паузу и возвращается при выходе. "
         "Работает и у лицехвата с андроидом",
         "Body keeps facing the camera: A/D sideways, S backwards. Pauses by itself in vents and ducts, comes back when out. "
         "Works for the facehugger and the android too"));
  if (s.fmode && s.strafePause) BUI::TextCol(pal.warning, "%s %s", TR("пауза:", "paused:"), s.strafePause);
  BUI::PopDisabled();
  BUI::Separator(TR("Развороты на месте", "Turning on the spot"));
  {
    bool tf = s.turnFp, t3 = s.turn3p;
    if (BUI::Toggle(TR("Из головы###tfp", "Head view###tfp"), &tf)) Push(C_TURN, 0, tf ? 1 : 0);
    if (BUI::Toggle(TR("За спиной###t3p", "Behind view###t3p"), &t3)) Push(C_TURN, 1, t3 ? 1 : 0);
    BUI::Hint("%s", TR("Стоит, а камера смотрит в сторону — доворачивается на месте (IDLE_TURN_L/R_90/180)",
                       "Standing while the camera looks aside — turns on the spot (IDLE_TURN_L/R_90/180)"));
    BUI::Row(TR("Резкий разворот на 180°", "Quick 180° turn"), KN(K_TURN180), pal.warning);
    BUI::Hint("%s", TR("стоит — разворот анимацией IDLE_TURN_180, камера поворачивается вместе с телом; на ходу — камера "
                       "сразу назад, тело разворачивается за ней. Работает, даже если развороты на месте выключены",
                       "standing — the IDLE_TURN_180 animation, the camera turns with the body; on the move — the camera "
                       "flips at once and the body follows. Works even with turning on the spot switched off"));
  }
  BUI::Separator();
  bool snappy = s.snappy;
  if (BUI::Toggle(TR("Удобный Чужой###snappy", "Snappy Alien###snappy"), &snappy)) Push(C_ALIEN_SNAPPY, snappy ? 1 : 0);
  Tip(TR("Отпустил клавиши — Чужой встаёт как вкопанный, где отпустил (анимация остановки доигрывает на месте). "
         "Выключено — как в игре: доигрывает остановку с проходом",
         "Let go of the keys and the Alien stops dead where you let go (the stop animation plays out in place). "
         "Off — as in the game: he walks the stop out"));
  BUI::PushDisabled(!alien);
  bool path = s.path;
  if (BUI::Toggle(Lbl(TR("Режим пути (шахты)", "Path mode (ducts)"), K_PATH, "pm"), &path) && alien) Push(C_PATH);
  Tip(TR("WASD задают точку впереди, Чужой идёт по навигации — так он заходит в горизонтальные шахты",
         "WASD set a point ahead and the Alien walks there by navigation — that's how he enters horizontal ducts"));
  bool follow = s.follow;
  if (BUI::Toggle(Lbl(TR("Аманда следует за Чужим", "Amanda follows the Alien"), K_FOLLOW, "af"), &follow) && alien)
    Push(C_AMANDA, follow ? 1 : 0);
  Tip(TR("Аманда держится в 3.5 м позади Чужого — нужно, чтобы работали зоны уровня, звук и триггеры. В шахтах и "
         "вентиляции — на паузе. По умолчанию выключено",
         "Amanda stays 3.5 m behind the Alien — needed for level zones, sound and triggers. Paused in ducts and vents. "
         "Off by default"));
  BUI::PopDisabled();
  BUI::Hint("%s", TR("WASD (камера у Чужого) или стрелки (камера у Аманды). Shift — бег, Ctrl — тихо",
                     "WASD (camera on the Alien) or arrows (camera on Amanda). Shift — run, Ctrl — sneak"));
}

static void PageAlien(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  BUI::Row(TR("Осмотреться (держать)", "Look around (hold)"), KN(K_LOOK), pal.warning);
  {
    static const char* kLkRu[] = { "Как раньше", "LOOK по камере" };
    static const char* kLkEn[] = { "As before", "LOOK by camera" };
    int lm = s.lookMode;
    if (BUI::Segmented("##lookm", g_lang ? kLkEn : kLkRu, 2, &lm, ImGui::GetContentRegionAvail().x)) Push(C_LOOK_MODE, lm);
    Tip(TR("«LOOK по камере» — как осмотр ИИ: Чужой встаёт и смотрит головой и корпусом (до 180°) туда, куда смотрит камера. "
           "«Как раньше» — сам крутит головой (аддитивный осмотр)",
           "«LOOK by camera» — like the AI's look: the Alien stops and turns his head and body (up to 180°) where the camera "
           "looks. «As before» — he looks around by himself (additive look-around)"));
  }
  BUI::Hint("%s", TR("держи — Чужой встаёт и осматривается", "hold — the Alien stops and looks around"));
  if (s.looking) BUI::TextCol(pal.success, "%s", TR("осматривается", "looking"));
  {
    bool br = s.breath;
    if (BUI::Toggle(TR("Слюни и дым изо рта всегда###breath", "Drool and breath always###breath"), &br)) Push(C_BREATH, br ? 1 : 0);
    Tip(TR("Игра показывает слюни и дым изо рта Чужого, только когда Аманда ближе 6 м. Включено — всегда, пока камера на Чужом",
           "The game shows the Alien's drool and breath only when Amanda is within 6 m. On — always while the camera is on the Alien"));
  }
  BUI::Separator(TR("Голос", "Voice"));
  BUI::Row(TR("Клавиша голоса", "Voice key"), KN(K_SOUND), pal.warning);
  {
    std::vector<const char*> items{ TR("случайный (поиск)", "random (searching)") };
    for (int i = 0; i < VOICE_COUNT; i++) items.push_back(VoiceName(i));
    int sel = s.voiceKey + 1;
    if (BUI::Combo(TR("Что играет по клавише###vkey", "What the key plays###vkey"), &sel, items.data(), int(items.size())))
      Push(C_VOICE_KEY, sel - 1);
  }
  {
    const float wv = RowW(3);
    for (int i = 0; i < VOICE_COUNT; i++) {  // кнопки — послушать каждый: поиск, «заметил», громкие
      if (i == 3) BUI::Separator(TR("заметил человека — сами по себе, только если рядом человек", "noticed a human — by themselves only with a human nearby"));
      if (i == 5) BUI::Separator(TR("громкие — только по выбору", "loud ones — only when you pick them"));
      const int k = i < 3 ? i : i < 5 ? i - 3 : i - 5;
      if (k % 3) BUI::SameLine();
      char lbl[96];
      std::snprintf(lbl, sizeof lbl, "%s###vc%d", VoiceName(i), i);
      if (BUI::Button(lbl, BUI::BtnTinted, alien && s.cam != 0, wv)) Push(C_VOICE, i);
    }
  }
  if (!(alien && s.cam != 0)) BUI::Hint("%s", TR("звуки — когда играешь за Чужого", "voices work while you play as the Alien"));
  bool av = s.autoVoice;
  if (BUI::Toggle(TR("Автозвуки###vauto", "Auto voice###vauto"), &av)) Push(C_VOICE_AUTO, av ? 1 : 0);
  Tip(TR("Сам иногда подаёт голос. Обычно — звуки поиска: ищет, начинает поиск, потерял след. «Заметил» и «бросок» — "
         "только когда цель — человек рядом (не андроид, не Аманда). Крики боли сам не издаёт",
         "Makes sounds by himself now and then. Normally searching sounds: searching, starts a search, lost the trail. "
         "«Noticed» and «charge» — only when the target is a human nearby (not an android, not Amanda). Never pain screams by itself"));
  {
    static float every = 14.f;
    static bool ed = false;
    if (!ed) every = s.voiceEvery;
    if (BUI::SliderF(TR("Автозвук примерно раз в, с###vev", "Auto voice about every, s###vev"), &every, 3.f, 60.f, "%.0f"))
      Push(C_VOICE_EVERY, 0, 0, &every, 1);
    ed = ImGui::IsItemActive();
  }
  BUI::Separator(TR("Аманда, пока играешь за Чужого", "Amanda while you play as the Alien"));
  {
    static const char* kAhRu[] = { "на месте", "под картой", "над картой" };
    static const char* kAhEn[] = { "stays put", "under the map", "above the map" };
    int ah = s.amandaHide;
    if (BUI::Segmented("##ah", g_lang ? kAhEn : kAhRu, 3, &ah, ImGui::GetContentRegionAvail().x)) Push(C_AMANDA_HIDE, ah);
    Tip(TR("Где Аманда, пока ты не за неё (её модель в это время не видна). Возвращается туда же, где стояла. "
           "Если часть уровня пропадает — «над картой» или «на месте»",
           "Where Amanda is while you are not her (her model is hidden meanwhile). She returns to the same spot. "
           "If parts of the level vanish — «above the map» or «stays put»"));
  }
  if (s.amandaHidden) BUI::TextCol(pal.success, "%s", TR("спрятана", "hidden"));
  bool hh = s.hideGameHud;
  if (BUI::Toggle(TR("За Чужого прятать интерфейс Рипли###hgh", "Hide Ripley's HUD as the Alien###hgh"), &hh)) Push(C_HIDE_GAME_HUD, hh ? 1 : 0);
  Tip(TR("Патроны, фонарь и подсказки «взять» — это интерфейс Аманды; за Чужого его не видно, вместо него HUD Чужого",
         "Ammo, torch and pickup prompts belong to Amanda; as the Alien they are hidden and the Alien HUD is shown"));
}

static void PageHuggers(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  if (s.hugs.empty()) {
    BUI::TextWrapped("%s", TR("На этом уровне нет лицехватов и андроидов (или мод их ещё не нашёл).",
                              "No facehuggers or androids on this level (or the mod has not found them yet)."));
    return;
  }
  BUI::TextWrapped(TR("%s — за следующего, %s — к Аманде. WASD, Пробел/Shift/Ctrl — скорость, ЛКМ — прыжок / удар, ПКМ — другая "
                      "цель, %s — голос андроида",
                      "%s — the next one, %s — back to Amanda. WASD, Space/Shift/Ctrl — speed, LMB — leap / strike, RMB — another "
                      "target, %s — android voice"),
                   KN(K_HUGGER), KN(K_SWAP), KN(K_SOUND));
  if (s.hugNoBranch)
    BUI::TextWrappedCol(pal.warning, "%s", TR("Нет ветки мода: закрой игру, AlienModSetup.exe → «Установить», загрузи уровень заново.",
                                       "No mod branch: close the game, AlienModSetup.exe → Install, load the level again."));
  for (size_t i = 0; i < s.hugs.size(); i++) {
    const HugRow& r = s.hugs[i];
    char left[160], right[64], dist[32] = "";
    if (r.dist >= 0) std::snprintf(dist, sizeof dist, TR("%.0f м", "%.0f m"), r.dist);
    std::snprintf(left, sizeof left, "%d. %s%s", int(i + 1), r.name.c_str(), r.hasBranch ? "" : TR(" (нет ветки)", " (no branch)"));
    ImGui::PushID(int(i));
    BUI::Row(left, dist);
    std::snprintf(right, sizeof right, "%s###hugp", r.controlled ? TR("Играю", "Playing") : TR("Играть", "Play"));
    if (BUI::ToggleButton(right, r.controlled, true, 140.f)) Push(C_HUGGER, 0, r.controlled ? 0 : r.e);
    ImGui::PopID();
  }
  BUI::Separator(TR("ИИ остальных лицехватов", "AI of the other facehuggers"));
  int ha = s.hugAi;
  if (AiSegmented("##hai", &ha, "Не на Аманду", "Not Amanda")) Push(C_HUG_AI, ha);
  Tip(TR("Как у Чужого. «Не на Аманду» — кто нацелился на неё, замирает и ищет другую цель. Лицехват, которого ты отпустил, "
         "тоже переходит в этот режим. На того, за кого играешь, не влияет",
         "Like the Alien's. «Not Amanda» — whoever targets her freezes and looks for another target. The facehugger you "
         "let go switches to this mode too. Does not affect the one you play"));
  if (s.hugCtl && s.hugAi == 2) BUI::Hint("%s", TR("пока играешь за лицехвата, остальные к Рипли не лезут (полный = не на Аманду)",
                                                     "while you play a facehugger the others leave Ripley alone (full = not Amanda)"));
  BUI::Separator(TR("ИИ остальных андроидов", "AI of the other androids"));
  int na = s.androidAi;
  if (AiSegmented("##nai", &na, "Игнор Аманды", "Ignore Amanda")) Push(C_ANDROID_AI, na);
  Tip(TR("Андроиды, за которых не играешь. «Игнор Аманды» — кто нацелился на Аманду, стоит и ищет другую цель. "
         "Пока играешь за лицехвата или андроида, «полный» тоже не трогает Аманду",
         "Androids you do not play. «Ignore Amanda» — whoever targets Amanda stands and looks for another target. "
         "While you play a facehugger or an android, «full» leaves Amanda alone too"));
  BUI::Separator();
  bool aa = s.hugAtkAmanda;
  if (BUI::Toggle(TR("Можно прыгать на Аманду (это её смерть)###hatk", "May leap at Amanda (that kills her)###hatk"), &aa))
    Push(C_HUG_ATK_AMANDA, aa ? 1 : 0);
  Tip(TR("Выключено — ЛКМ сначала ищет другую цель, а если кроме Аманды никого, прыжка не будет",
         "Off — LMB first looks for another target; if there is nobody but Amanda, no leap"));
  {
    static const char* kMvRu[] = { "Само", "Напрямую", "По навмешу" };
    static const char* kMvEn[] = { "Auto", "Direct", "Along the navmesh" };
    int mm = s.hugMove;
    if (BUI::Combo(TR("Движение лицехвата###hmv", "Facehugger movement###hmv"), &mm, g_lang ? kMvEn : kMvRu, 3)) Push(C_HUG_MOVE, mm);
    Tip(TR("«Напрямую» — как Чужой, сразу туда, куда жмёшь. «По навмешу» — бежит к точке перед собой по сетке уровня "
           "(плавнее, но с задержкой). «Само» — напрямую, а если так лицехват не сдвинется — по навмешу",
           "«Direct» — like the Alien, right where you press. «Along the navmesh» — runs to a point ahead along the level's "
           "mesh (smoother, with a delay). «Auto» — direct, and if that does not move him — navmesh"));
  }
  if (s.hugCtl && s.hugPath) BUI::Hint("%s", TR("сейчас по навмешу", "navmesh now"));
  BUI::Hint("%s", TR("Камера лицехвата — своя: настраивается в разделе «Камера», пока играешь за него.",
                     "The facehugger has its own camera: set it in the Camera section while you play him."));
}

static void PageVents(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  const std::string now = s.duct ? std::string(VentName(s)) + TR("  (в шахте)", "  (in a duct)") : std::string(VentName(s));
  BUI::Row(TR("Сейчас", "Now"), now.c_str());
  const char* smart = busy ? Lbl(TR("Отменить", "Cancel"), K_VENT, "v1")
                           : s.ventStay ? Lbl(TR("Выйти здесь", "Come out here"), K_VENT, "v1") : Lbl(TR("Уйти в вентиляцию", "Go into a vent"), K_VENT, "v1");
  const float w2 = RowW(2);
  if (BUI::ToggleButton(smart, busy, alien, w2)) Push(busy ? C_VENT_CANCEL : s.ventStay ? C_VENT_EXIT : C_VENT_GO);
  Tip(TR("Одна клавиша: в мире — уйти в ближайшую вертикальную вентиляцию; в вентиляции — выйти через ближайшую к Чужому; "
         "в пути — отменить",
         "One key: in the world — go into the nearest vertical vent; in the vents — come out through the one nearest the Alien; "
         "on the way — cancel"));
  BUI::SameLine();
  if (BUI::Button(Lbl(TR("Выйти у Аманды", "Come out near Amanda"), K_VENT_AMANDA, "v2"), BUI::BtnTinted,
                  alien && s.ventStay && !s.ventCome, w2))
    Push(C_VENT_EXIT_AMANDA);
  bool autoOut = s.autoOut;
  if (BUI::Toggle(TR("Сразу выводить Чужого после загрузки уровня###ao", "Bring the Alien out right after a level loads###ao"), &autoOut))
    Push(C_AUTOOUT, autoOut ? 1 : 0);
  if (!s.backstage.empty()) BUI::TextDim("%s", s.backstage.c_str());
}

static void PageAi(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  const float w2 = RowW(2);
  if (BUI::Button(TR("Атаковать  [ЛКМ]###at", "Attack  [LMB]###at"), BUI::BtnFilled, alien, w2)) Push(C_ATTACK);
  Tip(TR("Сменить цель, затем атака (если цель — Аманда, ещё до 3 попыток)", "Switch target, then attack (up to 3 retries if the target is Amanda)"));
  BUI::SameLine();
  if (BUI::Button(TR("Сменить цель  [ПКМ]###nt", "Next target  [RMB]###nt"), BUI::BtnTinted, alien, w2)) Push(C_NEXT);
  bool ka = s.killAndroids;
  if (BUI::Toggle(TR("Чужой атакует андроидов###kand", "The Alien attacks androids###kand"), &ka)) Push(C_KILL_ANDROIDS, ka ? 1 : 0);
  Tip(TR("Игра андроидов Чужому в цели не даёт, и анимации «Чужой убивает андроида» в ней нет. С галочкой: ЛКМ, когда "
         "андроид в 4 м перед Чужим, — Чужой бьёт его",
         "The game never lets the Alien target androids and has no «Alien kills android» animation. With this on: LMB with "
         "an android within 4 m in front of the Alien — the Alien strikes it"));
  {
    static const char* kAmRu[] = { "Прямой удар", "Как человека (опыт)" };
    static const char* kAmEn[] = { "Direct blow", "Like a human (experimental)" };
    int am = s.androidMode;
    BUI::PushDisabled(!s.killAndroids);
    if (BUI::Combo(TR("Как бить андроида###kandm", "How to hit an android###kandm"), &am, g_lang ? kAmEn : kAmRu, 2)) Push(C_ANDROID_MODE, am);
    BUI::PopDisabled();
    Tip(TR("«Как человека» — на время удара андроид считается гражданским: Чужой берёт анимацию убийства человека. Не умер за 3 с — "
           "добивается прямым ударом",
           "«Like a human» — for the blow the android counts as a civilian so the Alien uses its human kill animation. If it's "
           "still alive after 3 s it's finished with a direct blow"));
  }
  BUI::Separator(TR("ИИ для всех", "AI for everyone"));
  {
    const bool mixed = !(s.ai == s.hugAi && s.ai == s.androidAi);
    int all = mixed ? -1 : s.ai;
    if (AiSegmented("##aiall", &all, "Не на Аманду", "Not Amanda")) Push(C_AI_ALL, all);
    Tip(TR("Сразу Чужому, лицехватам и андроидам (кроме того, за кого играешь). По отдельности — ниже и в разделе "
           "«Лицехваты и андроиды». F4 — по кругу для всех",
           "The Alien, facehuggers and androids at once (except the one you play). Separately — below and in the "
           "Facehuggers & androids section. F4 cycles it for everyone"));
    if (mixed) BUI::Hint("%s", TR("сейчас у типов разные режимы — задано по отдельности", "the types have different modes now — set separately"));
  }
  BUI::Separator(TR("ИИ Чужого", "Alien's AI"));
  {
    int ai = s.ai;
    if (AiSegmented("##ai", &ai, "Не на Аманду", "Not Amanda") && alien) Push(C_AI, ai);
    Tip(TR("«Не на Аманду» — охотится сам, но на Аманду не нападает", "«Not Amanda» — hunts by himself but never attacks Amanda"));
  }
  BUI::Space(4.f);
  if (BUI::Button(Lbl(TR("Аварийный сброс", "Panic reset"), K_PANIC, "pn"), BUI::BtnDanger, lvl)) Push(C_PANIC);
  Tip(TR("Ручное управление, выключить режим пути и вентиляцию", "Manual control, turn off path mode and vents"));
}

static void PageSpawn(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  BUI::Title(TR("Персонажи на уровне", "Characters on the level"));
  {
    if (s.chars.empty()) BUI::Hint("%s", TR("нет активных персонажей", "no active characters"));
    const float w2 = RowW(2);
    for (const CharRow& r : s.chars) {
      ImGui::PushID(reinterpret_cast<void*>(r.e));
      char left[192], dist[32] = "";
      if (r.dist >= 0) std::snprintf(dist, sizeof dist, TR("%.1f м", "%.1f m"), r.dist);
      std::snprintf(left, sizeof left, "%s%s", r.name.c_str(),
                    r.isPlayer ? TR("  (Аманда)", "  (Amanda)") : r.isNpc ? TR("  (управляемый)", "  (controlled)") : "");
      BUI::Row(left, dist);
      if (BUI::Button(TR("Чужой сюда###tp", "Alien here###tp"), BUI::BtnTinted, alien && !r.isNpc, w2)) Push(C_TP_TO, 0, r.e);
      BUI::SameLine();
      if (BUI::Button(TR("Притащить###br", "Bring###br"), BUI::BtnTinted, alien && !r.isNpc, w2)) Push(C_TP_BRING, 0, r.e);
      ImGui::PopID();
    }
  }
  BUI::Separator(TR("Спавн", "Spawn"));
  const SpawnInfo& sp = s.spawn;
  unsigned act = unsigned(s.chars.size());
  const char* scanLbl = sp.scanning ? TR("Ищу...###sc", "Searching...###sc")
                                    : sp.done ? Lbl(TR("Искать заново", "Search again"), K_SPAWN_FIND, "sc", "Ctrl+")
                                              : Lbl(TR("Найти персонажей уровня", "Find level characters"), K_SPAWN_FIND, "sc");
  if (BUI::Button(scanLbl, BUI::BtnFilled, lvl && !sp.scanning)) Push(C_SCAN);
  if (sp.scanning) {
    BUI::ProgressBar(float(sp.pct) / 100.f, nullptr);
    BUI::Hint("%s %d%%", TR("сканирую память...", "scanning memory..."), sp.pct);
  } else if (sp.done) {
    BUI::Row(TR("найдено", "found"), std::to_string(sp.found.size()).c_str());
  }
  BUI::Hint("%s", TR("Имя становится известно после первого спавна объекта.", "A name becomes known after the object's first spawn."));
  for (size_t i = 0; i < sp.found.size(); i++) {
    std::string line = SpawnName(sp, i) + "###sp" + std::to_string(i);
    if (BUI::ToggleButton(line.c_str(), int(i) == sp.sel, true, ImGui::GetContentRegionAvail().x)) Push(C_SEL, int(i));
  }
  {
    static const char* kSwRu[] = { "как задумано уровнем", "куда смотрит камера", "перед персонажем" };
    static const char* kSwEn[] = { "where the level puts it", "where the camera looks", "in front of the character" };
    int sw = s.spawnWhere;
    if (BUI::Combo(TR("Куда ставить###sw", "Place at###sw"), &sw, g_lang ? kSwEn : kSwRu, 3)) Push(C_SPAWN_WHERE, sw);
    Tip(TR("«Куда смотрит камера» — точка в центре экрана. В фоторежиме так можно поставить кого угодно куда угодно: "
           "долети фрикамом и жми спавн",
           "«Where the camera looks» — the point in the middle of the screen. In photo mode that puts anyone anywhere: "
           "fly there with the free camera and spawn"));
  }
  const bool selOk = lvl && sp.sel >= 0 && size_t(sp.sel) < sp.found.size() && act < 16 && !sp.gone[size_t(sp.sel)];
  const bool again = selOk && sp.spawned[size_t(sp.sel)] > 0;
  const float w2 = RowW(2);
  if (BUI::Button(Lbl(TR("Заспавнить", "Spawn"), K_SPAWN, "sp1"), BUI::BtnSuccess, selOk && !again, w2)) Push(C_SPAWN, 0);
  BUI::SameLine();
  if (BUI::Button(Lbl(TR("Ещё раз", "Again"), K_SPAWN, "sp2", "Shift+"), BUI::BtnDanger, again, w2)) Push(C_SPAWN, 1);
  Tip(TR("Тот же объект — та же сущность. Повторный спавн может дать копию (два Чужих) и уронить игру",
         "Same object — same entity. Spawning it again may create a copy (two Aliens) and crash the game"));
  if (BUI::Button(TR("Лицехвата сюда  [Ctrl+H]###sphug", "A facehugger here  [Ctrl+H]###sphug"), BUI::BtnTinted, s.hasPlayer)) Push(C_SPAWN_HUG);
  Tip(TR("Копия лицехвата — туда, куда смотрит камера (в фоторежиме — клавиша H). Мод узнаёт лицехвата в списке после его "
         "первого спавна (F7/F8) — дальше хоть сколько",
         "A copy of a facehugger where the camera looks (photo mode — the H key). The mod learns which entry is a facehugger "
         "after its first spawn (F7/F8) — then as many as you like"));
  if (act >= 16) BUI::TextCol(pal.danger, "%s", TR("лимит: при 16+ персонажах игра падает", "limit: the game crashes with 16+ characters"));
  if (!sp.msg.empty()) BUI::TextCol(pal.warning, "%s", sp.msg.c_str());
  if (s.locked) BUI::Hint("%s", TR("управление закреплено за исходным Чужим (клоны не перехватывают)",
                                   "control is locked to the original Alien (clones can't take it)"));
}

static void PageKeys(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  DrawKeys();
}

static void PageMod(const Snap& s) {
  const bool lvl = s.level, alien = s.level && s.hasNpc;
  const BUI::Palette& pal = BUI::P();
  const bool busy = s.ventGo || s.ventCome;
  (void)lvl, (void)alien, (void)pal, (void)busy;
  BUI::TextWrapped("%s", TR("Просто запусти AlienInjectorUI.exe (или runUI.bat) ещё раз: он сам отцепит старую версию и подцепит новую, "
                            "а новая продолжит с того же места — тот же Чужой или лицехват, та же камера.",
                            "Just run AlienInjectorUI.exe (or runUI.bat) again: it detaches the old version and attaches the new one, "
                            "and the new one carries on from the same spot — same alien or facehugger, same camera."));
  BUI::Hint("%s", TR("Или отцепи вручную — игра станет как без мода (камера у Аманды, всё вернётся), меню пропадёт.",
                     "Or detach it by hand — the game goes back to vanilla (camera on Amanda, everything restored), the menu disappears."));
  static double armedAt = -100;
  const bool armed = ImGui::GetTime() - armedAt < 4.0;
  if (BUI::Button(armed ? TR("Точно отцепить? Нажми ещё раз###unload", "Really detach? Click again###unload")
                        : TR("Отцепить мод###unload", "Detach the mod###unload"),
                  BUI::BtnDanger)) {
    if (armed) RequestUnload();
    else armedAt = ImGui::GetTime();
  }
}

// ---------------- мультиплеер (mp.cpp) ----------------
// у мультиплеера своё состояние под своим маленьким замком — меню читает и командует им прямо отсюда
static void MpCopyId(const MpSnap& m, const char* id) {
  char b[24];
  snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(m.me));
  if (BUI::Button(id, BUI::BtnGhost)) ImGui::SetClipboardText(b);
}

static void MpAvatarCombo(const MpSnap& m) {  // за кого играю — и хост, и клиент
  const char* av[AV_COUNT];
  for (int i = 0; i < AV_COUNT; i++) av[i] = MpAvatarName(i);
  int a = m.avatar;
  if (BUI::Combo(TR("Играть за###mpav", "Play as###mpav"), &a, av, AV_COUNT)) MpCmd(MPA_AVATAR, a, 0);
  Tip(TR("Когда игра начнётся и уровень загрузится, мод сам отдаст тебе управление: Чужим или ближайшим андроидом (если их "
         "ещё нет — дождётся). Человек — играешь своим человеком, мод ничего не перехватывает",
         "Once the game starts and the level loads, the mod hands you control: the Alien or the nearest android (if there are "
         "none yet, it waits). Human — you play your own human, the mod takes nothing over"));
}
static bool HasMission(uint32_t mask, int n) { return n >= 1 && n <= kMpMissionCount && ((mask >> n) & 1); }
static void MpMyCharCombo(const MpSnap& m) {  // выживание: свой персонаж — у каждого свой, как экран выбора персонажа DLC
  const char* chars[kMpCharCount];
  for (int i = 0; i < kMpCharCount; i++) chars[i] = MpCharName(i + 1);
  int ch = m.myChar - 1;
  if (BUI::Combo(TR("Мой персонаж###mpchr", "My character###mpchr"), &ch, chars, kMpCharCount)) MpCmd(MPA_MYCHAR, ch + 1, 0);
  Tip(TR("Кем ты начинаешь выживание, если играешь человеком (у каждого игрока свой, как экран выбора персонажа DLC)",
         "Who you start Survivor as when playing a human (each player picks their own, like the DLC character screen)"));
}

static void MpHost(const MpSnap& m) {
  const BUI::Palette& pal = BUI::P();
  char b[256];
  MpAvatarCombo(m);
  if (!m.lobbyOpen) {
    BUI::Hint("%s", TR("Создай лобби: уровень пока не грузится. Друзья подключатся по твоему адресу (Steam ID), ты выберешь режим "
                       "(кампания, миссия, DLC, выживание) и нажмёшь «Начать» — тогда он запустится у всех.",
                       "Create a lobby: no level loads yet. Friends join by your address (Steam ID), you pick a mode (campaign, a "
                       "mission, DLC, Survivor) and press Start — then it launches for everyone."));
    if (BUI::Button(TR("Создать лобби###mpopen", "Create a lobby###mpopen"), BUI::BtnFilled, m.steam)) MpCmd(MPA_LOBBY_OPEN, 0, 0);
    return;
  }
  if (m.started) snprintf(b, sizeof b, TR("идёт игра: %s", "game running: %s"), m.lobbyLevel.c_str());
  else snprintf(b, sizeof b, "%s", TR("открыто, ждём игроков", "open, waiting for players"));
  BUI::Row(TR("Лобби", "Lobby"), b, m.started ? pal.success : pal.primary);
  snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(m.me));
  BUI::Row(TR("Твой адрес", "Your address"), b);
  MpCopyId(m, TR("Скопировать адрес###mpcopyh", "Copy the address###mpcopyh"));
  Tip(TR("Друг вписывает его в «Адрес хоста» (подойдёт и ссылка на твой профиль Steam) или жмёт на тебя в своём списке "
         "«Друзья в Alien: Isolation»",
         "A friend types it into «Host address» (a link to your Steam profile works too) or clicks you in their "
         "«Friends in Alien: Isolation» list"));

  BUI::Separator(TR("Что запускаем", "What to launch"));
  {
    const char* modes[kMpModeCount];
    for (int i = 0; i < kMpModeCount; i++) modes[i] = MpModeName(i);
    int md = m.lobbyMode;
    if (BUI::Combo(TR("Режим###mpmode", "Mode###mpmode"), &md, modes, kMpModeCount)) MpCmd(MPA_MODE, md, 0);
    Tip(TR("Запускается так же, как из меню игры: сброс игры и первый уровень режима — со всеми сущностями. DLC и карты "
           "выживания должны быть у всех",
           "Launched the same way as from the game's menu: the game resets and the mode's first level loads — with all its "
           "entities. Everyone needs the DLC / Survivor maps"));
    if (m.lobbyMode == MPM_SURVIVOR) {  // карта и персонаж — как экран выбора карты в игре
      static std::vector<MpMap> maps;
      static double at = -100;
      if (ImGui::GetTime() - at > 3.0) maps = MpSurvivorMaps(), at = ImGui::GetTime();
      if (maps.empty()) {
        BUI::Hint("%s", TR("карт выживания не видно (DLC не установлены?)", "no Survivor maps found (DLC not installed?)"));
      } else {
        std::vector<const char*> items;
        int cur = -1;
        for (size_t i = 0; i < maps.size(); i++) {
          items.push_back(maps[i].name.c_str());
          if (maps[i].id == m.lobbyMap) cur = int(i);
        }
        int sel = cur < 0 ? 0 : cur;
        if (BUI::Combo(TR("Карта###mpmap", "Map###mpmap"), &sel, items.data(), int(items.size())) || cur < 0) MpSetSurvivorMap(maps[size_t(sel)].id);
      }
      MpMyCharCombo(m);
    } else if (m.lobbyMode == MPM_MISSION) {  // миссия кампании — сохранение её начала (MSSVnn), у каждого своё
      const char* ms[kMpMissionCount];
      std::string names[kMpMissionCount];
      for (int i = 0; i < kMpMissionCount; i++) {
        names[i] = MpMissionName(i + 1);
        if (!HasMission(m.myMissions, i + 1)) names[i] += TR("  (у тебя нет)", "  (you don't have it)");
        ms[i] = names[i].c_str();
      }
      int mi = m.lobbyMission - 1;
      if (BUI::Combo(TR("Миссия###mpmis", "Mission###mpmis"), &mi, ms, kMpMissionCount)) MpCmd(MPA_MISSION, mi + 1, 0);
      Tip(TR("Как «Загрузить → миссия» в меню игры: начало миссии из сохранения MSSVnn. У каждого грузится своё — миссию надо хоть "
             "раз начать в своей кампании. Сложность — та, что в сохранении",
             "Like «Load → mission» in the game's menu: the mission start from the MSSVnn save. Everyone loads their own — the "
             "mission must have been started once in your campaign. Difficulty is the one in the save"));
      if (!HasMission(m.myMissions, m.lobbyMission))
        BUI::TextWrappedCol(pal.warning, "%s", TR("У тебя нет сохранения этой миссии — начни её в кампании или выбери другую",
                                                  "You have no save of this mission — start it in the campaign or pick another"));
    } else {
      const char* diffs[kMpDiffCount];
      for (int i = 0; i < kMpDiffCount; i++) diffs[i] = MpDiffName(i);
      int df = m.lobbyDiff;
      if (BUI::Combo(TR("Сложность###mpdiff", "Difficulty###mpdiff"), &df, diffs, kMpDiffCount)) MpCmd(MPA_DIFF, df, 0);
    }
    if (!m.lobbyLevel.empty()) BUI::Row(TR("Уровень хоста", "Host's level"), m.lobbyLevel.c_str());
  }

  snprintf(b, sizeof b, TR("Игроки (%d из %d)", "Players (%d of %d)"), int(m.members.size()), kMpMaxClients);
  BUI::Separator(b);
  if (m.members.empty()) BUI::Hint("%s", TR("пока никого — ждём", "nobody yet — waiting"));
  for (size_t i = 0; i < m.members.size(); i++) {
    const MpMember& p = m.members[i];
    ImGui::PushID(int(i) + 3000);
    char ping[32] = "";
    if (p.pingMs >= 0) snprintf(ping, sizeof ping, TR(", пинг %d мс", ", ping %d ms"), p.pingMs);
    snprintf(b, sizeof b, "%s — %s%s", p.name.empty() ? "?" : p.name.c_str(), MpAvatarName(p.avatar), ping);
    BUI::Text("%s", b);
    if (m.lobbyMode == MPM_MISSION && !HasMission(p.missions, m.lobbyMission))
      BUI::TextCol(pal.warning, "%s", TR("  нет сохранения этой миссии — у него она не запустится", "  no save of this mission — it won't start for them"));
    if (BUI::Button(TR("Выгнать###k", "Kick###k"), BUI::BtnGhost)) MpCmd(MPA_KICK, 0, p.id);
    BUI::SameLine();
    if (BUI::Button(TR("Бан###bn", "Ban###bn"), BUI::BtnDanger)) MpCmd(MPA_BAN, 0, p.id);
    Tip(TR("Выгнать и больше не пускать (список банов сохраняется)", "Kick and never let back in (the ban list is saved)"));
    ImGui::PopID();
  }
  if (!m.bans.empty()) {
    BUI::Separator(TR("Баны", "Bans"));
    for (size_t i = 0; i < m.bans.size(); i++) {
      const MpBan& bn = m.bans[i];
      ImGui::PushID(int(i) + 4000);
      if (BUI::Button(TR("Разбанить###ub", "Unban###ub"), BUI::BtnGhost)) MpCmd(MPA_UNBAN, 0, bn.id);
      BUI::SameLine();
      if (bn.name.empty()) BUI::Text("%llu", static_cast<unsigned long long>(bn.id));
      else BUI::Text("%s (%llu)", bn.name.c_str(), static_cast<unsigned long long>(bn.id));
      ImGui::PopID();
    }
  }
  BUI::Space(6.f);
  const float w2 = RowW(2);
  const bool canStart = m.lobbyMode == MPM_SURVIVOR ? !m.lobbyMap.empty() : m.lobbyMode == MPM_MISSION ? HasMission(m.myMissions, m.lobbyMission) : true;
  if (BUI::Button(m.started ? TR("Запустить заново###mpst", "Launch again###mpst") : TR("Начать###mpst", "Start###mpst"),
                  BUI::BtnSuccess, canStart, w2))
    MpCmd(MPA_START, 0, 0);
  Tip(TR("У тебя и у всех в лобби запустится выбранный режим; кто подключится позже — запустит его сразу",
         "The picked mode launches for you and everyone in the lobby; whoever joins later launches it right away"));
  BUI::SameLine();
  if (BUI::Button(TR("Закрыть лобби###mpcl", "Close the lobby###mpcl"), BUI::BtnDanger, true, w2)) MpCmd(MPA_LOBBY_CLOSE, 0, 0);
}

static void MpClient(const MpSnap& m) {
  const BUI::Palette& pal = BUI::P();
  char b[256];
  MpAvatarCombo(m);
  static char idBuf[96] = "";
  static bool idInit = false;
  if (!idInit) {
    idInit = true;
    if (m.peer) snprintf(idBuf, sizeof idBuf, "%llu", static_cast<unsigned long long>(m.peer));
  }
  if (m.state == MPS_IDLE) {
    if (!m.friends.empty()) {
      BUI::Title(TR("Друзья в Alien: Isolation", "Friends in Alien: Isolation"));
      for (size_t i = 0; i < m.friends.size(); i++) {
        const MpFriend& f = m.friends[i];
        ImGui::PushID(int(i) + 1000);
        if (BUI::Button(TR("Войти в лобби###fc", "Join###fc"), BUI::BtnFilled, m.steam)) {
          snprintf(idBuf, sizeof idBuf, "%llu", static_cast<unsigned long long>(f.id));
          MpCmd(MPA_CONNECT, 0, f.id);
        }
        BUI::SameLine();
        BUI::Text("%s", f.name.empty() ? "?" : f.name.c_str());
        ImGui::PopID();
      }
    }
    BUI::InputText("##mpid", idBuf, sizeof idBuf, TR("адрес хоста: Steam ID, ссылка на профиль или код друга", "host address: Steam ID, profile link or friend code"));
    const uint64_t want = MpParseId(idBuf);
    if (idBuf[0] && !want) BUI::Hint("%s", TR("это не похоже на Steam ID", "that doesn't look like a Steam ID"));
    if (BUI::Button(TR("Подключиться###mpcon", "Connect###mpcon"), BUI::BtnFilled, m.steam && want && want != m.me)) MpCmd(MPA_CONNECT, 0, want);
    BUI::Hint("%s", TR("Связь — через Steam, порты открывать не нужно. Хост должен сначала создать лобби.",
                       "It goes through Steam, no ports to open. The host has to create a lobby first."));
    return;
  }
  const char* st = m.state == MPS_CONNECTING ? TR("подключаюсь...", "connecting...")
                   : m.state == MPS_LOST     ? TR("связь пропала, переподключаюсь", "connection lost, reconnecting")
                   : m.started               ? TR("игра идёт", "game running")
                                             : TR("в лобби — ждём, когда хост начнёт", "in the lobby — waiting for the host to start");
  BUI::Row(TR("Состояние", "State"), st, m.state == MPS_CONNECTED ? pal.success : pal.warning);
  snprintf(b, sizeof b, "%s (%llu)", m.peerName.empty() ? "?" : m.peerName.c_str(), static_cast<unsigned long long>(m.peer));
  BUI::Row(TR("Хост", "Host"), b);
  if (m.pingMs >= 0) {
    snprintf(b, sizeof b, TR("%d мс", "%d ms"), m.pingMs);
    BUI::Row(TR("Пинг", "Ping"), b);
  }
  if (m.lobbyMode == MPM_SURVIVOR)
    snprintf(b, sizeof b, "%s: %s", MpModeName(m.lobbyMode), m.lobbyMap.empty() ? "?" : MpMapName(m.lobbyMap).c_str());
  else if (m.lobbyMode == MPM_MISSION)
    snprintf(b, sizeof b, TR("Кампания: %s", "Campaign: %s"), MpMissionName(m.lobbyMission));
  else
    snprintf(b, sizeof b, "%s, %s", MpModeName(m.lobbyMode), MpDiffName(m.lobbyDiff));
  BUI::Row(TR("Режим", "Mode"), b);
  if (m.lobbyMode == MPM_SURVIVOR && m.avatar == AV_HUMAN) MpMyCharCombo(m);
  if (m.lobbyMode == MPM_MISSION && !HasMission(m.myMissions, m.lobbyMission))
    BUI::TextWrappedCol(pal.warning, "%s", TR("У тебя нет сохранения этой миссии — у тебя она не запустится. Начни её в своей кампании.",
                                              "You have no save of this mission — it won't start for you. Start it in your campaign."));
  if (!m.lobbyLevel.empty()) BUI::Row(TR("Уровень хоста", "Host's level"), m.lobbyLevel.c_str());
  if (!m.members.empty()) {
    snprintf(b, sizeof b, TR("В лобби (%d)", "In the lobby (%d)"), int(m.members.size()));
    BUI::Title(b);
    for (const MpMember& p : m.members)
      BUI::Text("%s%s%s — %s", p.name.empty() ? "?" : p.name.c_str(), p.host ? TR(" (хост)", " (host)") : "",
                p.id == m.me ? TR(" (ты)", " (you)") : "", MpAvatarName(p.avatar));
  }
  if (BUI::Button(TR("Выйти из лобби###mpdis", "Leave the lobby###mpdis"), BUI::BtnDanger)) MpCmd(MPA_DISCONNECT, 0, 0);
}

// тела игроков (puppet.cpp): кто кого изображает в этом мире
static void MpBodies(const MpSnap& m) {
  const BUI::Palette& pal = BUI::P();
  char b[256];
  BUI::Separator(TR("Игроки в мире", "Players in the world"));
  const std::vector<PupRow> rows = PupSnapshot();
  if (rows.empty())
    BUI::Hint("%s", TR("пока никого: тела появятся, когда игра пойдёт и вы будете на одном уровне",
                       "nobody yet: bodies appear once the game is running and you're on the same level"));
  for (const PupRow& r : rows) {
    std::string who;
    if (r.ghost) who = TR("зеркало (ты)", "mirror (you)");
    for (const MpMember& p : m.members)
      if (p.id == r.who) who = p.name;
    if (who.empty() && r.who == m.peer) who = m.peerName;
    if (who.empty()) who = "?";
    char id[24] = "";
    if (!r.ident.empty()) snprintf(id, sizeof id, " (%s)", r.ident.c_str());
    snprintf(b, sizeof b, "%s — %s%s  →  %s", who.c_str(), MpAvatarName(r.avatar), id,
             r.body.empty() ? TR("некому изобразить", "nobody to show them") : r.body.c_str());
    BUI::TextCol(r.body.empty() ? pal.warning : pal.text, "%s", b);
    char d[160];
    snprintf(d, sizeof d, TR("%s%s%.1f м, %d пак/с, %s%s", "%s%s%.1f m, %d pk/s, %s%s"), r.attacking ? TR("бьёт! ", "attacking! ") : "",
             r.hidden ? TR("в вентиляции, ", "in the vents, ") : "",
             double(r.dist < 0 ? 0.f : r.dist), r.fps, r.pose ? TR("поза передаётся", "pose synced") : TR("без позы", "no pose"),
             r.ageMs > 1000 ? TR(", молчит", ", silent") : "");
    BUI::Hint("%s", d);
  }
  const char* bodies[PB_COUNT];
  for (int i = 0; i < PB_COUNT; i++) bodies[i] = PupHumanBodyName(i);
  int hb = PupHumanBody();
  if (BUI::Combo(TR("Другой человек здесь — это###mphb", "Another human here is###mphb"), &hb, bodies, PB_COUNT)) MpCmd(MPA_HUMANBODY, hb, 0);
  Tip(TR("Сначала — тот же персонаж в третьем лице, если его модель есть на уровне (DLC: экипаж «Ностромо» в «Экипаж — "
         "расходный материал», Ренсом и Лингард на части карт выживания). Целой модели Аманды в игре нет (у игрока она без головы), "
         "тогда — то, что выбрано здесь: целый персонаж уровня повторяет его движения и позу. Твоё тело у тебя — как в игре, от "
         "первого лица",
         "First the same character in third person, if its model is on the level (DLC: the Nostromo crew in Crew Expendable, "
         "Ransome and Lingard on some Survivor maps). The game has no whole Amanda model (the player's one has no head) — then "
         "what is picked here: a whole character of the level repeats their moves and pose. Your own body stays as in the game, "
         "first person"));
  bool gh = PupGhostOn();
  if (BUI::Toggle(TR("Зеркало: проверить куклу на себе###mpgh", "Mirror: test a puppet on yourself###mpgh"), &gh)) MpCmd(MPA_GHOST, gh ? 1 : 0, 0);
  Tip(TR("Без второго игрока: твоё тело повторяет кукла в 2.5 м перед тобой — так видно, как тебя увидят другие. Нужен загруженный "
         "уровень, где есть андроид или NPC",
         "No partner needed: a puppet 2.5 m ahead of you repeats your body — this is how others will see you. Needs a loaded level "
         "with an android or an NPC"));
}

static void PageMp(const Snap&) {
  const BUI::Palette& pal = BUI::P();
  const MpSnap m = MpSnapshot();
  char b[192];
  if (!m.steam) {
    BUI::TextWrappedCol(pal.warning, "%s", TR("Steam не найден: мультиплеер работает только в Steam-версии игры (через её steam_api.dll). "
                                              "Если игра из Steam — подожди пару секунд.",
                                              "Steam not found: multiplayer needs the Steam version of the game (its steam_api.dll). "
                                              "If the game is from Steam — wait a couple of seconds."));
  } else {
    BUI::Row(TR("Ты", "You"), m.name.empty() ? "?" : m.name.c_str());
  }
  {
    const float w2 = RowW(2);
    if (BUI::ToggleButton(TR("Хост — создать лобби###mprh", "Host — create a lobby###mprh"), m.role == MP_HOST, m.steam, w2) && m.role != MP_HOST)
      MpCmd(MPA_ROLE, MP_HOST, 0);
    BUI::SameLine();
    if (BUI::ToggleButton(TR("Клиент — подключиться###mprc", "Client — join###mprc"), m.role == MP_CLIENT, m.steam, w2) && m.role != MP_CLIENT)
      MpCmd(MPA_ROLE, MP_CLIENT, 0);
  }
  BUI::Space(4.f);
  if (m.role == MP_HOST) MpHost(m);
  else if (m.role == MP_CLIENT) MpClient(m);
  else BUI::Hint("%s", TR("Хост создаёт лобби, выбирает карту и начинает игру; клиенты подключаются к нему по адресу.",
                          "The host creates a lobby, picks a map and starts the game; clients join by the host's address."));
  BUI::Row(TR("Твой уровень", "Your level"), m.myLevel.empty() ? TR("меню / загрузка", "menu / loading") : m.myLevel.c_str());
  if (!m.status.empty()) BUI::TextWrappedCol(pal.textDim, "%s", m.status.c_str());
  BUI::Hint("%s", TR("Всё о подключении пишется в AlienModUI.mp.log рядом с модом — пришли его, если что-то не так",
                     "Everything about connecting goes to AlienModUI.mp.log next to the mod — send it if something's wrong"));
  MpBodies(m);

  BUI::Separator(TR("Ещё", "More"));
  if (BUI::Button(TR("Самопроверка Steam###mpself", "Steam self-test###mpself"), BUI::BtnGhost, m.steam && m.selfTest != 1)) MpCmd(MPA_SELFTEST, 0, 0);
  Tip(TR("Пакет самому себе: проверяет, что мод правильно зовёт сеть Steam. Если не вернулся — это ещё не беда, "
         "Steam может не пускать пакеты себе; надёжная проверка — вдвоём",
         "A packet to yourself: checks that the mod calls Steam networking right. If it doesn't come back that's not fatal — "
         "Steam may refuse packets to yourself; the real test is with a partner"));
  if (m.selfTest >= 2) {
    BUI::SameLine();
    BUI::TextCol(m.selfTest == 2 ? pal.success : pal.warning, "%s", m.selfTest == 2 ? TR("ок", "ok") : TR("не вернулся", "didn't come back"));
  }
  {
    bool gm = m.gameMenu;
    if (BUI::Toggle(TR("«Совместная игра» в меню игры###mpgm", "«Co-op» in the game menu###mpgm"), &gm))
      MpCmd(MPA_GAMEMENU, gm ? 1 : 0, 0);
    Tip(TR("Всё мультиплеерное прямо в меню игры, без окна мода: создать лобби, зайти к другу, режим, миссия, карта, роль "
           "(экран выбора персонажа DLC с портретами), игроки, «Начать игру». В меню паузы — состояние, игроки, выйти. Новое — если "
           "что-то пойдёт не так, выключи и пришли AlienModUI.log",
           "All of multiplayer right in the game's menu, no mod window needed: create a lobby, join a friend, mode, mission, map, "
           "role (the DLC character screen with portraits), players, «Start game». In the pause menu — status, players, leave. "
           "New — if anything goes wrong, turn it off and send AlienModUI.log"));
  }

  BUI::Separator(TR("Выходы сущностей", "Entity outputs"));
  BUI::Hint("%s", TR("«Выход X у сущности Y»: дверь открылась, триггер сработал, скрипт дошёл до шага. Хост шлёт свои, клиенты "
                     "повторяют их у себя тем же вызовом игры и гасят свои такие же. Только корневые: каскад повторится сам.",
                     "«Output X of entity Y»: a door opened, a trigger fired, a script reached a step. The host sends its own, clients "
                     "repeat them with the same game call and mute their own matching ones. Root ones only: the cascade repeats by itself."));
  bool tap = m.tap, sup = m.suppress;
  if (BUI::Toggle(TR("Журнал выходов###mptap", "Output journal###mptap"), &tap)) MpCmd(MPA_TAP, tap ? 1 : 0, 0);
  if (BUI::Toggle(TR("Клиент гасит свои такие же (экспериментально)###mpsup", "Client mutes its own matching ones (experimental)###mpsup"), &sup))
    MpCmd(MPA_SUPPRESS, sup ? 1 : 0, 0);
  Tip(TR("Мир ведёт хост: клиент не даёт сработать своим событиям, которые присылал хост. Может остановить сценарий у "
         "клиента — по умолчанию выключено",
         "The host runs the world: the client blocks its own events that the host has sent. It can stall the client's "
         "scripts — off by default"));
  snprintf(b, sizeof b, "%u / %u / %u", m.captured, m.sent, m.recv);
  BUI::Row(TR("поймано / отправлено / получено", "caught / sent / received"), b);
  snprintf(b, sizeof b, "%u / %u / %u", m.replayed, m.suppressed, m.missed);
  BUI::Row(TR("повторено / погашено / не найдено", "replayed / muted / not found"), b, m.missed ? pal.warning : 0);
  snprintf(b, sizeof b, TR("%u (ключей %u)", "%u (%u keys)"), m.filtered, m.noisyKeys);
  BUI::Row(TR("отсеяно как шум", "filtered as noise"), b);
  if (m.ecxOdd) {  // повтор ищет this (интерфейс сущности) сам; так было у всех пойманных — если нет, повтор неточен
    snprintf(b, sizeof b, "%u", unsigned(m.ecxOdd));
    BUI::Row(TR("выходов с необычным this", "outputs with an unusual this"), b, pal.warning);
  }
  if (BUI::Button(TR("Очистить###mpclr", "Clear###mpclr"), BUI::BtnGhost)) MpCmd(MPA_CLEAR, 0, 0);
  if (!m.tap) {
    BUI::Hint("%s", TR("включи журнал, чтобы видеть выходы", "turn the journal on to see outputs"));
  } else if (m.trig.empty()) {
    BUI::Hint("%s", TR("пока пусто — походи по уровню: двери, триггеры, скрипты", "empty so far — walk around: doors, triggers, scripts"));
  }
  for (size_t i = 0; i < m.trig.size(); i++) {
    const MpTrig& t = m.trig[i];
    ImGui::PushID(int(i));
    if (BUI::Button(TR("Повторить###rp", "Repeat###rp"), BUI::BtnGhost, true)) MpCmd(MPA_REPLAY, int(i), uint64_t(t.ent) << 32 | t.out);
    BUI::SameLine();
    char dl[24] = "";
    if (t.delay > 0.f) snprintf(dl, sizeof dl, " +%.2fs", double(t.delay));
    char outName[16];
    const char* on = MpOutName(t.out);
    if (!on) snprintf(outName, sizeof outName, "%08X", unsigned(t.out)), on = outName;
    snprintf(b, sizeof b, "%s%s  %08X:%08X  %s%s  x%u", t.remote ? TR("[хост] ", "[host] ") : "", t.kind.empty() ? "" : t.kind.c_str(),
             unsigned(t.ent), unsigned(t.inst), on, dl, unsigned(t.n));
    BUI::TextCol(t.remote ? pal.warning : pal.text, "%s", b);
    ImGui::PopID();
  }
}

// страницы меню (standalone.cpp — боковое меню): имя, подпись, иконка
struct PageDef { void (*draw)(const Snap&); const char* ru; const char* en; const char* subRu; const char* subEn; BUI::Icon ico; };
static const PageDef kPages[PP_COUNT] = {
  { PageMain, "Главное", "Main", "играть за Чужого, лицехвата, андроида", "play as the Alien, a facehugger, an android", BUI::ICO_GAMEPAD },
  { PageCamera, "Камера", "Camera", "за спиной, из головы, свободный взгляд", "behind, head, free look", BUI::ICO_CAMERA },
  { PageMove, "Движение", "Movement", "стрейф, развороты, «удобный Чужой»", "strafe, turns, snappy Alien", BUI::ICO_MOVE },
  { PageAlien, "Чужой", "Alien", "осмотр, голос, Аманда", "look, voice, Amanda", BUI::ICO_ALIEN },
  { PageAi, "Атака и ИИ", "Attack and AI", "цели, андроиды, режимы ИИ", "targets, androids, AI modes", BUI::ICO_TARGET },
  { PageHuggers, "Лицехваты", "Facehuggers", "лицехваты и андроиды", "facehuggers and androids", BUI::ICO_BUG },
  { PageVents, "Вентиляция", "Vents", "спрятаться, вылезти", "hide, come out", BUI::ICO_WIND },
  { PageSpawn, "Спавн", "Spawn", "персонажи уровня, спавн", "level characters, spawn", BUI::ICO_SPAWN },
  { PageMp, "Мультиплеер", "Multiplayer", "лобби, карта, кто за кого играет", "lobby, map, who plays whom", BUI::ICO_NET },
  { PageScreen, "Экран", "Screen", "подсказки, статус, карта", "hints, status, map", BUI::ICO_EYE },
  { PageKeys, "Клавиши", "Keys", "переназначить", "rebind", BUI::ICO_KEY },
  { PageMod, "Мод", "Mod", "обновить, отцепить", "update, detach", BUI::ICO_GEAR },
};
int MenuKey() { return g_keys[K_MENU]; }
PanelPageInfo PanelPage(int i) {
  if (i < 0 || i >= PP_COUNT) return {};
  const PageDef& d = kPages[i];
  PanelPageInfo r{ TR(d.ru, d.en), TR(d.subRu, d.subEn), int(d.ico), nullptr };
  if (i == PP_HUGGERS) {
    static char badge[16];
    const Snap s = GetSnap();
    if (!s.hugs.empty()) {
      snprintf(badge, sizeof badge, "%d", int(s.hugs.size()));
      r.badge = badge;
    }
  }
  return r;
}
void DrawPanelPage(int i) {
  if (i < 0 || i >= PP_COUNT) return;
  BUI::SetFilter("");
  const Snap s = GetSnap();
  kPages[i].draw(s);
}
// всё подряд (сборка с DevTools: одно окно без страниц)
void DrawPanelBody() {
  BUI::SetFilter("");
  const Snap s = GetSnap();
  for (int i = 0; i < PP_COUNT; i++) {
    if (i) BUI::Separator(TR(kPages[i].ru, kPages[i].en));
    kPages[i].draw(s);
  }
}

// ---------------- фоторежим ----------------
// ползунок постобработки: пока его тянут — своё значение, иначе — то, что стоит в моде
static void PostSlider(const char* label, int field, float cur, float lo, float hi, const char* fmt, const char* tip) {
  static std::unordered_map<int, float> editing;
  auto it = editing.find(field);
  float v = it != editing.end() ? it->second : cur;
  if (BUI::SliderF(label, &v, lo, hi, fmt, tip)) Push(C_PHOTO_POST, field, 0, &v, 1);
  if (ImGui::IsItemActive()) editing[field] = v;
  else editing.erase(field);
}
static bool PostCheck(const char* label, int field, bool cur, const char* tip) {
  bool v = cur;
  const bool ch = BUI::Toggle(label, &v);
  if (tip) Tip(tip);
  if (ch) {
    const float f = v ? 1.f : 0.f;
    Push(C_PHOTO_POST, field, 0, &f, 1);
  }
  return ch;
}
// клик по кадру (меню открыто — курсор свободен): навести фокус DOF в эту точку
static bool s_clickFocus = false;
static double s_focusMarkAt = -10;
static ImVec2 s_focusMarkPos;
static void FocusClickTick() {
  if (!s_clickFocus) return;
  ImGuiIO& io = ImGui::GetIO();
  if (!ImGui::IsMouseClicked(0) || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemHovered()) return;
  const ImVec2 ds = io.DisplaySize;
  if (ds.x < 1 || ds.y < 1) return;
  photoFocusUV[0] = io.MousePos.x / ds.x, photoFocusUV[1] = io.MousePos.y / ds.y, photoFocusUV[2] = ds.x / ds.y;
  photoFocusPick = 2;
  s_clickFocus = false;
  s_focusMarkAt = ImGui::GetTime(), s_focusMarkPos = io.MousePos;
}
static void DrawFocusMark() {  // рамка там, куда навели фокус
  const float e = float(ImGui::GetTime() - s_focusMarkAt);
  if (e > 1.5f) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const int a = int(255 * (1.f - e / 1.5f));
  const ImVec2 p = s_focusMarkPos;
  dl->AddRect(ImVec2(p.x - 14, p.y - 14), ImVec2(p.x + 14, p.y + 14), IM_COL32(255, 185, 60, a), 0.f, 0, 1.5f);
  dl->AddLine(ImVec2(p.x - 5, p.y), ImVec2(p.x + 5, p.y), IM_COL32(255, 185, 60, a), 1.5f);
  dl->AddLine(ImVec2(p.x, p.y - 5), ImVec2(p.x, p.y + 5), IM_COL32(255, 185, 60, a), 1.5f);
}
// рамка кадра фоторежима: каше под соотношение сторон и сетка третей (рисуется поверх игры и попадает в скриншот)
static void DrawPhotoFrame(const Snap& s) {
  DrawFocusMark();
  if (!s.photoOn || !s.post.on) return;
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  float x0 = 0, y0 = 0, x1 = ds.x, y1 = ds.y;
  static const float kAspect[] = { 0.f, 2.39f, 1.85f, 4.f / 3.f, 1.f, 9.f / 16.f };
  const int lb = std::clamp(s.post.letterbox, 0, 5);
  if (lb) {
    const float want = kAspect[lb], have = ds.x / std::max(1.f, ds.y);
    if (want > have) {  // шире экрана — полосы сверху и снизу
      const float h = ds.x / want, pad = (ds.y - h) * 0.5f;
      y0 = pad, y1 = ds.y - pad;
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, y0), IM_COL32(0, 0, 0, 255));
      dl->AddRectFilled(ImVec2(0, y1), ds, IM_COL32(0, 0, 0, 255));
    } else {  // уже — полосы по бокам
      const float w = ds.y * want, pad = (ds.x - w) * 0.5f;
      x0 = pad, x1 = ds.x - pad;
      dl->AddRectFilled(ImVec2(0, 0), ImVec2(x0, ds.y), IM_COL32(0, 0, 0, 255));
      dl->AddRectFilled(ImVec2(x1, 0), ds, IM_COL32(0, 0, 0, 255));
    }
  }
  if (s.post.grid) {
    const ImU32 c = IM_COL32(255, 255, 255, 70);
    for (int i = 1; i < 3; i++) {
      const float x = x0 + (x1 - x0) * i / 3.f, y = y0 + (y1 - y0) * i / 3.f;
      dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), c, 1.f);
      dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), c, 1.f);
    }
  }
}
// ============================ HUD Чужого (за Чужого, внизу по центру) ============================
// Свой вид — «кость и янтарь» вместо зелёного терминала Аманды: режим движения, где он, цель, стрелка на Аманду,
// клавиши осмотра и голоса. Интерфейс Рипли в это время спрятан (alien_extra.cpp).
static void DrawHuggerHud(const Snap& s);
static void DrawAndroidHud(const Snap& s);
static void DrawAlienHud(const Snap& s) {
  if (!s_cfg.alienHud || s.cam == 0) return;
  if (s.hugCtl) return s.npcKind == 2 ? DrawAndroidHud(s) : DrawHuggerHud(s);  // у каждого типа свой HUD
  if (!s.hasNpc) return;
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  const ImVec4 bone(0.93f, 0.86f, 0.72f, 1.f), amber(1.f, 0.66f, 0.25f, 1.f), dim(0.62f, 0.55f, 0.45f, 1.f), red(1.f, 0.42f, 0.35f, 1.f);
  // стрелка на Аманду по кругу вокруг центра экрана
  if (s.amandaDir && s.amandaDist > 2.f) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float r = std::min(ds.x, ds.y) * 0.33f, a = s.amandaAngle;
    const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f), p(c.x + std::sin(a) * r, c.y - std::cos(a) * r);
    const ImVec2 dir(std::sin(a), -std::cos(a)), side(-dir.y, dir.x);
    const ImVec2 tip(p.x + dir.x * 12, p.y + dir.y * 12), l(p.x + side.x * 7, p.y + side.y * 7), rr(p.x - side.x * 7, p.y - side.y * 7);
    const int alpha = s.amandaDist < 10.f ? 200 : 120;
    dl->AddTriangleFilled(tip, l, rr, IM_COL32(80, 170, 255, alpha));
    dl->AddTriangle(tip, l, rr, IM_COL32(200, 230, 255, alpha), 1.5f);
  }
  ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y - 14), ImGuiCond_Always, ImVec2(0.5f, 1.f));
  ImGui::SetNextWindowBgAlpha(0.45f);
  const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.55f, 0.25f, 0.45f));
  if (ImGui::Begin("##alienhud2", nullptr, f)) {
    // первая строка: ЧУЖОЙ · где · как идёт
    static const char* kSpeedRu[] = { "тихо", "шаг", "быстро", "бег" };
    static const char* kSpeedEn[] = { "sneak", "walk", "fast", "run" };
    const int sp = std::clamp(s.speed, 0, 3);
    TitleText(amber, TR("ЧУЖОЙ", "XENOMORPH"));
    ImGui::SameLine();
    ImGui::TextColored(dim, "|");
    ImGui::SameLine();
    const char* where = s.ventStay ? TR("в вентиляции", "in the vents") : s.ventGo ? TR("уходит в вентиляцию", "going into a vent")
                        : s.ventCome ? TR("вылезает", "coming out") : s.duct ? TR("в шахте", "in a duct") : TR("на охоте", "hunting");
    ImGui::TextColored(bone, "%s", where);
    ImGui::SameLine();
    ImGui::TextColored(dim, "|");
    ImGui::SameLine();
    if (s.looking) ImGui::TextColored(amber, "%s", TR("осматривается", "looking around"));
    else if (s.attacking) ImGui::TextColored(red, "%s", TR("атакует", "attacking"));
    else ImGui::TextColored(s.moving ? bone : dim, "%s", s.moving ? (g_lang ? kSpeedEn[sp] : kSpeedRu[sp]) : TR("стоит", "still"));
    // цель
    if (!s.target.empty() && !s.ventStay) {
      ImGui::TextColored(dim, "%s", TR("цель", "prey"));
      ImGui::SameLine();
      ImGui::TextColored(s.targetAmanda ? amber : red, "%s", s.target.c_str());
      ImGui::SameLine();
      ImGui::TextColored(dim, TR("%.0f м", "%.0f m"), s.targetDist);
    }
    // Аманда
    if (s.amandaDir) {
      ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.f, 1.f), "%s", TR("Аманда", "Amanda"));
      ImGui::SameLine();
      ImGui::TextColored(dim, TR("%.0f м%s", "%.0f m%s"), s.amandaDist,
                         s.amandaHidden ? (s.amandaHide == 2 ? TR("  · над картой", "  · above the map") : TR("  · под картой", "  · under the map")) : "");
    }
    // клавиши
    ImGui::TextColored(dim, "%s %s   %s %s   %s %s", KN(K_LOOK), TR("осмотр", "look"), KN(K_SOUND), TR("голос", "voice"), KN(K_VENT),
                       TR("вентиляция", "vents"));
  }
  ImGui::End();
  ImGui::PopStyleColor();
}

// ---- HUD лицехвата и андроида: у каждого свой вид (у Чужого — «кость и янтарь» выше) ----
struct HudPal { ImVec4 title, text, dim, warn, border; };
static void HudAmandaArrow(const Snap& s, ImU32 fill, ImU32 edge) {  // стрелка на Аманду по кругу вокруг центра экрана
  if (!s.amandaDir || s.amandaDist <= 2.f) return;
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float r = std::min(ds.x, ds.y) * 0.33f, a = s.amandaAngle;
  const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f), p(c.x + std::sin(a) * r, c.y - std::cos(a) * r);
  const ImVec2 dir(std::sin(a), -std::cos(a)), side(-dir.y, dir.x);
  const ImVec2 tip(p.x + dir.x * 12, p.y + dir.y * 12), l(p.x + side.x * 7, p.y + side.y * 7), rr(p.x - side.x * 7, p.y - side.y * 7);
  dl->AddTriangleFilled(tip, l, rr, fill);
  dl->AddTriangle(tip, l, rr, edge, 1.5f);
}
static bool HudBegin(const char* id, const HudPal& pal) {
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y - 14), ImGuiCond_Always, ImVec2(0.5f, 1.f));
  ImGui::SetNextWindowBgAlpha(0.45f);
  const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  ImGui::PushStyleColor(ImGuiCol_Border, pal.border);
  return ImGui::Begin(id, nullptr, f);
}
static void HudEnd() {
  ImGui::End();
  ImGui::PopStyleColor();
}
static void HudSep(const HudPal& pal) {
  ImGui::SameLine();
  ImGui::TextColored(pal.dim, "|");
  ImGui::SameLine();
}
static void HudTargetAndAmanda(const Snap& s, const HudPal& pal, const char* preyRu, const char* preyEn) {
  if (!s.target.empty()) {
    ImGui::TextColored(pal.dim, "%s", TR(preyRu, preyEn));
    ImGui::SameLine();
    ImGui::TextColored(s.targetAmanda ? pal.title : pal.warn, "%s", s.target.c_str());
    ImGui::SameLine();
    ImGui::TextColored(pal.dim, TR("%.0f м", "%.0f m"), s.targetDist);
  }
  if (s.amandaDir) {
    ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.f, 1.f), "%s", TR("Аманда", "Amanda"));
    ImGui::SameLine();
    ImGui::TextColored(pal.dim, TR("%.0f м%s", "%.0f m%s"), s.amandaDist,
                       s.amandaHidden ? (s.amandaHide == 2 ? TR("  · над картой", "  · above the map") : TR("  · под картой", "  · under the map")) : "");
  }
}

// лицехват: живое, бледно-розовое, «паучье»
static void DrawHuggerHud(const Snap& s) {
  const HudPal pal{ ImVec4(0.98f, 0.62f, 0.64f, 1.f), ImVec4(0.96f, 0.88f, 0.84f, 1.f), ImVec4(0.64f, 0.5f, 0.5f, 1.f),
                    ImVec4(1.f, 0.38f, 0.42f, 1.f), ImVec4(0.8f, 0.36f, 0.42f, 0.5f) };
  HudAmandaArrow(s, IM_COL32(80, 170, 255, s.amandaDist < 10.f ? 200 : 120), IM_COL32(200, 230, 255, 200));
  if (HudBegin("##huggerhud", pal)) {
    static const char* kSpeedRu[] = { "крадётся", "бежит", "быстро", "во весь опор" };
    static const char* kSpeedEn[] = { "sneaking", "running", "fast", "full speed" };
    const int sp = std::clamp(s.speed, 0, 3);
    TitleText(pal.title, TR("ЛИЦЕХВАТ", "FACEHUGGER"));
    HudSep(pal);
    ImGui::TextColored(pal.text, "%s", s.hugName.c_str());
    HudSep(pal);
    switch (s.npcAct) {
      case 1: ImGui::TextColored(pal.warn, "%s", TR("прыжок", "leaping")); break;
      case 2: ImGui::TextColored(pal.warn, "%s", TR("на лице жертвы", "on the victim's face")); break;
      case 3: ImGui::TextColored(pal.warn, "%s", TR("на лице — жертва лежит", "on the face — the victim is down")); break;
      default:
        ImGui::TextColored(s.moving ? pal.text : pal.dim, "%s", s.moving ? (g_lang ? kSpeedEn[sp] : kSpeedRu[sp]) : TR("затаился", "lurking"));
    }
    if (s.hugNoBranch)
      ImGui::TextColored(pal.warn, "%s", TR("нет ветки мода: AlienModSetup.exe и загрузить уровень заново", "no mod branch: run AlienModSetup.exe and reload the level"));
    if (s.hugMsg) ImGui::TextColored(pal.warn, "%s", TR("рядом некого атаковать (ПКМ — выбрать цель)", "nobody to leap at nearby (RMB — pick a target)"));
    HudTargetAndAmanda(s, pal, "жертва", "prey");
    if (s.npcAct >= 2)
      ImGui::TextColored(pal.dim, TR("Пробел — отцепиться   WASD — слезть   %s Аманда", "Space — let go   WASD — crawl off   %s Amanda"), KN(K_SWAP));
    else
      ImGui::TextColored(pal.dim, TR("ЛКМ прыжок   ПКМ цель   %s стрейф   %s следующий   %s Аманда", "LMB leap   RMB target   %s strafe   %s next   %s Amanda"),
                         KN(K_STRAFE), KN(K_HUGGER), KN(K_SWAP));
  }
  HudEnd();
}

// андроид Seegson: чистый «медицинский» бело-бирюзовый, как экраны Севастополя
static void DrawAndroidHud(const Snap& s) {
  const HudPal pal{ ImVec4(0.45f, 0.95f, 0.88f, 1.f), ImVec4(0.9f, 0.96f, 0.95f, 1.f), ImVec4(0.5f, 0.64f, 0.63f, 1.f),
                    ImVec4(1.f, 0.55f, 0.35f, 1.f), ImVec4(0.35f, 0.82f, 0.76f, 0.5f) };
  HudAmandaArrow(s, IM_COL32(80, 200, 190, s.amandaDist < 10.f ? 200 : 120), IM_COL32(220, 255, 250, 200));
  if (HudBegin("##androidhud", pal)) {
    static const char* kSpeedRu[] = { "тихо", "идёт", "быстро", "бегом" };
    static const char* kSpeedEn[] = { "quietly", "walking", "fast", "running" };
    const int sp = std::clamp(s.speed, 0, 3);
    TitleText(pal.title, TR("СИНТЕТИК SEEGSON", "SEEGSON SYNTHETIC"));
    HudSep(pal);
    ImGui::TextColored(pal.text, "%s", s.hugName.c_str());
    HudSep(pal);
    if (s.npcAct == 1) ImGui::TextColored(pal.warn, "%s", TR("устраняет угрозу", "neutralising"));
    else if (!s.npcVoice.empty()) ImGui::TextColored(pal.title, "%s", s.npcVoice.c_str());
    else ImGui::TextColored(s.moving ? pal.text : pal.dim, "%s", s.moving ? (g_lang ? kSpeedEn[sp] : kSpeedRu[sp]) : TR("ожидает", "standing by"));
    if (s.hugNoBranch)
      ImGui::TextColored(pal.warn, "%s", TR("нет ветки мода: AlienModSetup.exe и перезапустить игру", "no mod branch: run AlienModSetup.exe and restart the game"));
    if (s.hugMsg) ImGui::TextColored(pal.warn, "%s", TR("рядом некого атаковать (ПКМ — выбрать цель)", "nobody to attack nearby (RMB — pick a target)"));
    HudTargetAndAmanda(s, pal, "цель", "target");
    ImGui::TextColored(pal.dim, TR("ЛКМ удар   ПКМ цель   %s голос   %s стрейф   %s следующий   %s Аманда",
                                   "LMB strike   RMB target   %s voice   %s strafe   %s next   %s Amanda"),
                       KN(K_SOUND), KN(K_STRAFE), KN(K_HUGGER), KN(K_SWAP));
  }
  HudEnd();
}

static void DrawPhotoPost(const Snap& s) {
  const PhotoPost& p = s.post;
  const BUI::Palette& pal = BUI::P();
  FocusClickTick();
  if (Section(TR("Постобработка###pph0", "Post-processing###pph0"), true, TR("пресеты, вкл/выкл", "presets, on/off"), BUI::ICO_EYE)) {
    PostCheck(TR("Применять в фоторежиме###ppon", "Apply in photo mode###ppon"), PF_ON, p.on,
              TR("Поверх настроек уровня. Выходишь из фоторежима — всё как было.",
                 "On top of the level's own settings. Leave photo mode and it is all back."));
    static const char* kPresetRu[] = { "без фильтра", "нуар", "тёплый", "холодный", "плёнка 1979", "сочный", "терминал" };
    static const char* kPresetEn[] = { "no filter", "noir", "warm", "cold", "1979 film", "vivid", "terminal" };
    static int preset = 0;
    if (BUI::Combo(TR("Пресет###pppr", "Preset###pppr"), &preset, g_lang ? kPresetEn : kPresetRu, 7,
                   TR("Меняет цвет, свет и зерно; объектив и кадр не трогает", "Sets colour, light and grain; lens and frame stay"))) {
      const float v = float(preset);
      Push(C_PHOTO_POST, PF_PRESET, 0, &v, 1);
    }
    if (BUI::Button(TR("Сбросить всё###ppreset", "Reset all###ppreset"), BUI::BtnGhost)) Push(C_PHOTO_POST, PF_RESET);
    SectionEnd();
  }
  if (Section(TR("Цвет и свет###pph1", "Colour and light###pph1"), true, TR("экспозиция, контраст, bloom", "exposure, contrast, bloom"),
              BUI::ICO_APERTURE)) {
    PostSlider(TR("Экспозиция, EV###ppev", "Exposure, EV###ppev"), PF_EXPOSURE, p.exposure, -4.f, 4.f, "%+.2f",
               TR("+1 — вдвое светлее, −1 — вдвое темнее", "+1 — twice as bright, −1 — half as bright"));
    PostSlider(TR("Яркость###ppbr", "Brightness###ppbr"), PF_BRIGHTNESS, p.brightness, -1.f, 1.f, "%+.2f", nullptr);
    PostSlider(TR("Контраст###ppco", "Contrast###ppco"), PF_CONTRAST, p.contrast, 0.f, 3.f, "x%.2f", nullptr);
    PostSlider(TR("Насыщенность###ppsa", "Saturation###ppsa"), PF_SATURATION, p.saturation, 0.f, 3.f, "x%.2f",
               TR("0 — чёрно-белое", "0 — black and white"));
    PostSlider(TR("Температура###ppte", "Temperature###ppte"), PF_TEMPERATURE, p.temperature, -1.f, 1.f, "%+.2f",
               TR("− холоднее (синее), + теплее (желтее)", "− colder (bluer), + warmer (yellower)"));
    PostSlider(TR("Оттенок###ppti", "Tint###ppti"), PF_TINT, p.tint, -1.f, 1.f, "%+.2f", TR("− зеленее, + пурпурнее", "− greener, + more magenta"));
    PostSlider(TR("Свечение (bloom)###ppbl", "Bloom###ppbl"), PF_BLOOM, p.bloom, 0.f, 20.f, "x%.2f",
               TR("Сила ореола вокруг ярких мест", "Strength of the glow around bright spots"));
    PostSlider(TR("Порог свечения###ppbt", "Bloom threshold###ppbt"), PF_BLOOM_THRESHOLD, p.bloomThreshold, 0.1f, 3.f, "x%.2f",
               TR("Меньше — светится больше всего кадра", "Lower — more of the frame glows"));
    SectionEnd();
  }
  if (Section(TR("Объектив###pph2", "Lens###pph2"), true, TR("глубина резкости, искажения", "depth of field, distortion"), BUI::ICO_CAMERA)) {
    PostCheck(TR("Глубина резкости (DOF)###ppdof", "Depth of field (DOF)###ppdof"), PF_DOF, p.dof,
              TR("Размытие всего, что ближе и дальше точки фокуса (bokeh игры)",
                 "Blurs everything nearer and farther than the focus point (the game's bokeh)"));
    const float w2 = RowW(2);
    if (BUI::ToggleButton(TR("Фокус кликом по кадру###ppfc", "Focus: click the frame###ppfc"), s_clickFocus, s.photoOn, w2))
      s_clickFocus = !s_clickFocus;
    Tip(TR("Нажми, потом кликни в кадре по тому, что должно быть резким", "Press, then click on what should be sharp"));
    BUI::SameLine();
    if (BUI::Button(TR("По центру###ppfcc", "Centre###ppfcc"), BUI::BtnTinted, s.photoOn, w2, "F")) photoFocusPick = 1;
    if (s_clickFocus) BUI::TextCol(pal.warning, "%s", TR("кликни в кадре...", "click in the frame..."));
    else if (photoFocusHit.load() == 0.f) BUI::TextCol(pal.warning, "%s", TR("луч никуда не попал", "the ray hit nothing"));
    PostSlider(TR("Фокус, м###ppfo", "Focus, m###ppfo"), PF_FOCUS, p.focus, 0.1f, 200.f, "%.2f",
               TR("Дистанция, на которой всё резко", "Distance that stays sharp"));
    PostSlider(TR("Фокусное, мм###ppfl", "Focal length, mm###ppfl"), PF_FOCAL, p.focal, 10.f, 400.f, "%.0f",
               TR("Длиннее объектив — сильнее размытие фона", "Longer lens — more background blur"));
    PostSlider(TR("Диафрагма f/###ppfs", "Aperture f/###ppfs"), PF_FSTOP, p.fstop, 0.7f, 32.f, "%.1f",
               TR("Меньше число — сильнее размытие (f/1.4 — очень мягко, f/16 — почти всё резко)",
                  "Smaller number — more blur (f/1.4 very soft, f/16 nearly all sharp)"));
    PostSlider(TR("Искажение линзы###ppdi", "Lens distortion###ppdi"), PF_DISTORTION, p.distortion, -0.5f, 0.5f, "%+.2f",
               TR("+ бочка / «рыбий глаз», − подушка", "+ barrel / fisheye, − pincushion"));
    PostSlider(TR("Хроматическая аберрация###ppab", "Chromatic aberration###ppab"), PF_ABERRATION, p.aberration, 0.f, 5.f, "+%.2f",
               TR("Цветная кайма по краям кадра", "Colour fringing towards the frame edges"));
    PostSlider(TR("Виньетка###ppvi", "Vignette###ppvi"), PF_VIGNETTE, p.vignette, 0.f, 5.f, "+%.2f",
               TR("Затемнение углов кадра", "Darker frame corners"));
    SectionEnd();
  }
  if (Section(TR("Плёнка###pph3", "Film###pph3"), false, TR("зерно, резкость, размытие", "grain, sharpness, blur"), BUI::ICO_LAYERS)) {
    PostSlider(TR("Зерно###ppgr", "Grain###ppgr"), PF_GRAIN, p.grain, 0.f, 3.f, "%.2f",
               TR("В фоторежиме зерно уровня выключено — здесь своё (0 — чистый кадр)",
                  "In photo mode the level's grain is off — this is its own (0 — clean frame)"));
    PostSlider(TR("Резкость###ppsh", "Sharpness###ppsh"), PF_SHARPNESS, p.sharpness, 0.f, 3.f, "%.2f",
               TR("Своя повышалка резкости поверх кадра", "The mod's own sharpening over the frame"));
    PostSlider(TR("Размытие кадра###ppbu", "Frame blur###ppbu"), PF_BLUR, p.blur, 0.f, 1.f, "%.2f",
               TR("Весь кадр; выборочно — глубина резкости выше", "Whole frame; selective — depth of field above"));
    PostCheck(TR("Без motion blur###ppmb", "No motion blur###ppmb"), PF_NO_MOTION_BLUR, p.noMotionBlur,
              TR("Кадр не смазывается, пока камера летит", "The frame does not smear while the camera moves"));
    SectionEnd();
  }
  if (Section(TR("Кадр###pph4", "Frame###pph4"), false, TR("каше, сетка, интерфейс, Аманда", "letterbox, grid, HUD, Amanda"), BUI::ICO_EYE)) {
    static const char* kLbRu[] = { "нет", "2.39:1 (кино)", "1.85:1", "4:3", "1:1", "9:16 (телефон)" };
    static const char* kLbEn[] = { "none", "2.39:1 (cinema)", "1.85:1", "4:3", "1:1", "9:16 (phone)" };
    int lb = p.letterbox;
    if (BUI::Combo(TR("Каше###pplb", "Letterbox###pplb"), &lb, g_lang ? kLbEn : kLbRu, 6)) {
      const float v = float(lb);
      Push(C_PHOTO_POST, PF_LETTERBOX, 0, &v, 1);
    }
    PostCheck(TR("Сетка третей###ppgd", "Rule of thirds###ppgd"), PF_GRID, p.grid, nullptr);
    PostCheck(TR("Спрятать интерфейс игры###pphh", "Hide game HUD###pphh"), PF_HIDE_HUD, p.hideGameHud,
              TR("Патроны, фонарь, подсказки «взять»", "Ammo, torch, pickup prompts"));
    PostCheck(TR("Аманда в кадре###ppam", "Amanda in the shot###ppam"), PF_SHOW_AMANDA, p.showAmanda,
              TR("Игра рисует Аманду «от первого лица»: без головы, плечи и манжеты тянутся к камере. Выключено — её не видно",
                 "The game draws Amanda for first person: no head, shoulders and cuffs stretch towards the camera. Off — she's hidden"));
    SectionEnd();
  }
}

void DrawPhotoBody() {
  Snap s = GetSnap();
  const BUI::Palette& pal = BUI::P();
  BUI::SetFilter("");  // поиск — только на странице «Управление»
  bool on = s.photoOn;
  if (BUI::Toggle(TR("Фоторежим###ph", "Photo mode###ph"), &on, nullptr, KN(K_PHOTO))) Push(C_PHOTO, on ? 1 : 0);
  Tip(TR("Свободная кино-камера. Ввод в игру не идёт, HUD мода скрыт. Стрелки ведут персонажа (машинима).",
         "Free cinematic camera. Game input is blocked, the mod HUD is hidden. Arrows steer a character (machinima)."));
  if (!s.hasPlayer) BUI::TextCol(pal.warning, "%s", TR("Нужен загруженный уровень.", "Needs a loaded level."));

  // ---- камера: скорость, мышь, FOV, наклон ----
  if (Section(TR("Камера###pcam", "Camera###pcam"), true, TR("скорость, FOV, наклон", "speed, FOV, roll"), BUI::ICO_CAMERA)) {
    static float speed = 4.f, sens = 1.f;
    static bool editing = false;
    if (!editing) speed = s.photoSpeed, sens = s.photoSens;
    bool ch = BUI::SliderF(TR("Скорость, м/с###pspd", "Speed, m/s###pspd"), &speed, 0.3f, 25.f, "%.1f",
                           TR("Shift при перетаскивании — точнее", "Hold Shift while dragging — finer"));
    const bool a1 = ImGui::IsItemActive();
    ch |= BUI::SliderF(TR("Чувствительность мыши###psens", "Mouse sensitivity###psens"), &sens, 0.1f, 4.f, "%.2f");
    editing = a1 || ImGui::IsItemActive();
    if (ch) {
      const float v[2] = { speed, sens };
      Push(C_PHOTO_PARAM, 0, 0, v, 2);
    }
    static float fov = 47.f, roll = 0.f;
    static bool editView = false;
    if (!editView) fov = s.photoFov > 0 ? s.photoFov : s.gameFov, roll = s.photoRoll;
    bool chv = BUI::SliderF(TR("FOV, °###pfov", "FOV, °###pfov"), &fov, 5.f, 140.f, "%.1f",
                            TR("Колесо в фоторежиме — плавно, Ctrl+колесо — совсем точно, Shift+колесо — крупно",
                               "Wheel in photo mode — smooth, Ctrl+wheel — very fine, Shift+wheel — coarse"));
    const bool av = ImGui::IsItemActive();
    chv |= BUI::SliderF(TR("Наклон по Z, °  [Z / C]###proll", "Roll (Z), °  [Z / C]###proll"), &roll, -180.f, 180.f, "%.0f");
    editView = av || ImGui::IsItemActive();
    if (chv) {
      const float v[2] = { fov, roll };
      Push(C_PHOTO_VIEW, 0, 0, v, 2);
    }
    const float w3 = RowW(3);
    if (BUI::Button(TR("FOV как в игре###pfr0", "Game FOV###pfr0"), BUI::BtnTinted, s.photoOn, w3)) {
      const float v[2] = { 0.f, s.photoRoll };
      Push(C_PHOTO_VIEW, 0, 0, v, 2);
    }
    BUI::SameLine();
    if (BUI::Button(TR("Сбросить наклон###prr", "Reset roll###prr"), BUI::BtnTinted, s.photoOn, w3, "R")) {
      const float v[2] = { s.photoFov, 0.f };
      Push(C_PHOTO_VIEW, 0, 0, v, 2);
    }
    BUI::SameLine();
    if (BUI::Button(TR("Камеру к Аманде###pto", "Camera to Amanda###pto"), BUI::BtnTinted, s.photoOn, w3)) Push(C_PHOTO_TO_PLAYER);
    if (s.photoOn) BUI::Hint("%.1f  %.1f  %.1f", s.photoPos.x, s.photoPos.y, s.photoPos.z);
    SectionEnd();
  }

  // ---- машинима: персонаж в кадре ----
  if (Section(TR("Персонаж в кадре (машинима)###pact", "Actor in the shot (machinima)###pact"), true,
              TR("стрелки ведут Чужого или лицехвата", "arrows steer the Alien or a facehugger"), BUI::ICO_ALIEN)) {
    BUI::TextWrapped("%s", TR("Стрелки ведут выбранного персонажа относительно взгляда камеры: ↑ — от камеры, ← — влево в кадре. "
                              "Камера при этом летает на WASD и мыши. Время должно идти (X — стоп-время).",
                              "Arrows steer the chosen character relative to the camera view: ↑ — away from the camera, ← — screen left. "
                              "The camera keeps flying on WASD and the mouse. Time must be running (X — freeze)."));
    const float segW = ImGui::GetContentRegionAvail().x;
    BUI::Title(TR("Стрелки ведут  [Tab]", "Arrows drive  [Tab]"));
    {
      static const char* kRu[] = { "Камеру", "Чужого", "Лицехвата / андроида" };
      static const char* kEn[] = { "Camera", "Alien", "Facehugger / android" };
      const int kVal[] = { PA_CAMERA, PA_ALIEN, PA_NPC };
      int sel = s.photoActorSel == PA_ALIEN ? 1 : s.photoActorSel == PA_NPC ? 2 : 0;
      if (BUI::Segmented("##pact", g_lang ? kEn : kRu, 3, &sel, segW)) Push(C_PHOTO_ACTOR, kVal[sel]);
    }
    if (s.photoOn && s.photoActorSel != PA_CAMERA && s.photoActorNow == PA_CAMERA)
      BUI::TextCol(pal.warning, "%s", s.photoActorSel == PA_ALIEN ? TR("Чужого на уровне нет", "No Alien on this level")
                                                                 : TR("сначала возьми лицехвата/андроида (H)", "take a facehugger/android first (H)"));
    BUI::Title(TR("Скорость персонажа  [PgUp / PgDn]", "Actor speed  [PgUp / PgDn]"));
    {
      static const char* kGearRu[] = { "тихо", "шагом", "быстро", "бегом" };
      static const char* kGearEn[] = { "sneak", "walk", "fast", "run" };
      int g = s.photoGear;
      if (BUI::Segmented("##pgear", g_lang ? kGearEn : kGearRu, 4, &g, segW)) Push(C_PHOTO_GEAR, g);
    }
    BUI::Title(TR("Камера за персонажем  [T]", "Camera and actor  [T]"));
    {
      static const char* kRu[] = { "стоит", "едет за ним", "едет и смотрит" };
      static const char* kEn[] = { "static", "tracks", "tracks + aims" };
      int f = s.photoFollow;
      if (BUI::Segmented("##pfollow", g_lang ? kEn : kRu, 3, &f, segW)) Push(C_PHOTO_FOLLOW, f);
      Tip(TR("«Едет за ним» — камера держит расстояние до персонажа (WASD его меняет). «Едет и смотрит» — ещё и "
             "наводится на него, мышь сдвигает кадр относительно персонажа.",
             "\"Tracks\" keeps the camera's offset to the actor (WASD changes it). \"Tracks + aims\" also points at the "
             "actor, the mouse shifts the framing around it."));
    }
    SectionEnd();
  }

  // ---- время ----
  if (Section(TR("Время###ptime", "Time###ptime"), true, TR("стоп-кадр и слоу-мо", "freeze and slow motion"), BUI::ICO_GEAR)) {
    bool fr = s.freeze;
    if (BUI::Toggle(TR("Остановить время###pfr", "Freeze time###pfr"), &fr, nullptr, "X")) Push(C_FREEZE, fr ? 1 : 0);
    Tip(TR("Анимации, ИИ и физика замирают, камера летает. Работает, пока включён фоторежим.",
           "Animations, AI and physics freeze, the camera still flies. Works while photo mode is on."));
    if (s.photoOn) {
      if (s.timeLive < 0.001f) BUI::TextCol(pal.warning, "%s", TR("время стоит", "time stopped"));
      else BUI::Hint(TR("время ×%.2f", "time ×%.2f"), s.timeLive);
    }
    static float ts = 1.f;
    static bool editTs = false;
    if (!editTs) ts = s.timeScale;
    if (BUI::SliderF(TR("Скорость времени###pts", "Time speed###pts"), &ts, 0.f, 2.f, "%.2f",
                     TR("Слоу-мо: 1 — нормально, 0.25 — в 4 раза медленнее", "Slow motion: 1 — normal, 0.25 — 4x slower")))
      Push(C_TIMESCALE, 0, 0, &ts, 1);
    editTs = ImGui::IsItemActive();
    SectionEnd();
  }

  DrawPhotoPost(s);

  // ---- клавиши ----
  if (Section(TR("Клавиши фоторежима###pkeys", "Photo mode keys###pkeys"), false, nullptr, BUI::ICO_KEY)) {
    BUI::Title(TR("Камера", "Camera"));
    KeyHint("WASD", TR("лететь", "fly"));
    KeyHint("Q / E", TR("вниз / вверх", "down / up"));
    KeyHint(TR("мышь", "mouse"), TR("смотреть (стрелки — тоже, если они у камеры)", "look (arrows too when they drive the camera)"));
    KeyHint(TR("колесо", "wheel"), TR("FOV (зум)", "FOV (zoom)"));
    KeyHint("Z / C / R", TR("наклон влево / вправо / сброс", "roll left / right / reset"));
    KeyHint("Shift / Ctrl", TR("быстрее / медленнее", "faster / slower"));
    BUI::Title(TR("Персонаж в кадре", "Actor"));
    KeyHint(TR("стрелки", "arrows"), TR("вести персонажа (относительно взгляда камеры)", "steer the actor (relative to the view)"));
    KeyHint("Tab", TR("кого ведут стрелки: камера / Чужой / лицехват-андроид", "who the arrows drive: camera / Alien / facehugger-android"));
    KeyHint("PgUp / PgDn", TR("скорость; правый Shift — бегом, правый Ctrl — тихо", "speed; right Shift — run, right Ctrl — sneak"));
    KeyHint("Enter", TR("атака (Чужой, прыжок лицехвата, удар андроида)", "attack (Alien, facehugger leap, android strike)"));
    KeyHint(KN(K_SOUND), TR("голос (Чужой, андроид)", "voice (Alien, android)"));
    KeyHint("T", TR("камера: стоит / едет за персонажем / едет и смотрит", "camera: static / tracks the actor / tracks + aims"));
    BUI::Title(TR("Прочее", "Other"));
    KeyHint("X", TR("остановить / запустить время", "freeze / unfreeze time"));
    KeyHint(KN(K_PHOTO), TR("выйти из фоторежима", "leave photo mode"));
    KeyHint(KN(K_MENU), TR("меню: курсор свободен, камера стоит — можно крутить ползунки", "menu: free cursor, the camera holds still — tweak the sliders"));
    BUI::Hint("%s", TR("Esc фоторежим не сбрасывает, другие клавиши мода (F1–F8 и т. д.) в нём молчат.",
                       "Esc doesn't leave photo mode; the mod's other keys (F1–F8 etc.) are muted in it."));
    SectionEnd();
  }
}

}  // namespace AlienMod
