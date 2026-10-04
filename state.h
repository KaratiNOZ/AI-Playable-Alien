// state.h — общее состояние мода (бывшие глобалы alien.js и поля globalThis.H из hot.js).
// Всё меняется только под общим замком (JsLock): из задач планировщика или из хуков с lock = true.
#pragma once
#include "common.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

// ---------- рубильники дерева (узлы ConditionDebugMenuLinkTest по In_Game_Menu_Text) ----------
enum GateKind { G_ARROW, G_GO, G_COME, G_STAY, G_PATH, G_ATTACK, G_NEXT, G_IDLE, G_PAD, G_COUNT };

inline addr gate, moveNode, npc, req, loco, player;  // управляемый Чужой и всё, что к нему относится
inline addr pathGate, pathNode;
inline bool inStart, pathOn;
inline std::vector<addr> multi[G_COUNT];  // все экземпляры рубильников (несколько деревьев) — зеркалим им значения
inline std::vector<addr> multiMoves;
inline bool locked;       // lockCtl: после первого спавна новые деревья (клонов) не перехватывают управление
inline addr lockLocoNpc;  // lockLoco: объект локомоции закреплён за этим Чужим
inline addr npcVt;
// лицехват, за которого сейчас играешь (hugger.cpp); 0 — нет. Камера, Аманда и WASD тогда у него, клавиши Чужого молчат
inline addr hugger;
inline addr CamTarget() { return hugger ? hugger : npc; }  // за кем камера и чей HUD

// ---------- ввод ----------
struct Input { bool any, rawAny; float camYaw, worldYaw; int speed; };
inline Input lastInput;  // из хуков (поток игры) — последний опрос таймера, как кэш в hot.js §2
inline int fmode = 3;    // 3 = стрейф включён игроком (8 — вкл/выкл, 5 — выкл)
// стрейф действует, только пока Чужой в мире: в вентиляции / за кулисами / в шахте он сам на паузе (control.cpp)
bool StrafeActive();
const char* StrafePause();  // почему стрейф на паузе (nullptr — не на паузе)
inline bool hasFace, moving;
inline float faceYaw, moveYaw;

// ---------- камера ----------
inline std::atomic<int> camMode{ 0 };  // 0 Аманда, 1 голова Чужого (V), 2 за спиной (B); читается и без замка (блок ввода)
inline std::atomic<ms_t> g_gameTickAt{ 0 };  // последний кадр игры (хуки апдейта персонажей): на паузе игра их не зовёт
// игра стоит (меню паузы, alt-tab, загрузка): апдейты персонажей давно не звались
inline bool GameStalled() { const ms_t t = g_gameTickAt.load(); return !t || NowMs() - t > 300; }
bool GameInputBlocked();
bool GameMenuPaused();    // control.cpp: игра на паузе (стоит и биты паузы не те, что на ходу) — стоп-время фоторежима не пауза  // input_block.cpp: ввод сейчас не для Аманды (за Чужого / фоторежим, не пауза)
inline int camWant;
inline ms_t camKeyAt;
inline addr camSaved;
inline float fOff[3] = { 0.5f, 2.0f, -3.0f }, fTgt[3] = { 0.f, 1.3f, 2.0f };  // то, что сейчас стоит в follow-камере
// настройка камеры игроком (колесо мыши / меню), сохраняется в .ini (ui.cpp). Вне шахты fOff/fTgt считаются из неё:
// fOff = { side, height, -dist }, fTgt = { 0, height - 0.7, 2 }; в шахте мод ставит свою ближнюю камеру.
struct CamCfg {
  float dist = 3.f, height = 2.f, side = 0.5f;  // за спиной: расстояние, высота, сдвиг вбок (плечо)
  float head[3] = { 0, 0, 0 };                  // из головы: вбок, вверх, вперёд
};
inline CamCfg camCfg;
// у лицехвата своя настройка камеры (он маленький): пока играешь за него, в camCfg — она, а настройка Чужого лежит
// в camCfgOther (и наоборот). Меняет местами camera.cpp (CamUseHuggerCfg), сохраняет ui.cpp — каждую под своими ключами
inline CamCfg camCfgOther{ 1.4f, 0.55f, 0.f, { 0, 0, 0 } };
inline bool camIsHugger = false;
inline CamCfg HuggerCamDefault() { return { 1.4f, 0.55f, 0.f, { 0, 0, 0 } }; }
void CamUseHuggerCfg(bool on);
inline std::atomic<int> camWheel{ 0 };  // колесо мыши, пока камера у Чужого (input_block.cpp, Raw Input)
void CamCfgApply();                     // пересчитать и записать в камеру (под общим замком)
inline bool ductOn;
inline int camCollide = 1;  // столкновения камеры за спиной: 0 выкл, 1 мягкие (физика, 3 луча), 2 строгие (как раньше + навмеш)
inline addr hkWorld;

// ---------- вентиляция ----------
// comeEnd — когда узел выхода закончился (его Shutdown): «не вышел» считаем только через 2.5 с после этого
struct Vent { ms_t goUntil, comeUntil, keyAt, comeAt, comeEnd; bool stay; addr go, come, stayG; int pickLast = -1; ms_t pickAt; };
// выход не удался (Чужой так и остался за кулисами после всех попыток) — время для HUD
inline ms_t ventComeFailedAt;
inline Vent vent;
// Чужой сейчас за кулисами — по его дереву поведения (control.cpp), независимо от наших команд 1/2:
// так камера за кулисами работает и сразу после загрузки уровня, и пока он выходит к Аманде
inline bool alienBackstage;
inline float bsViewYaw;     // курс камеры за кулисами (camera.cpp, из орбиты follow-камеры) — для WASD в вентиляции
inline bool bsViewValid;

// ---------- атака ----------
struct Attack { addr gate, nextGate; ms_t until, nextUntil, startAt, keyAt; int retries; };
inline Attack attack;
// Чужой против андроидов (actors.cpp): игра андроидов Чужому в цели не даёт (альянсы), поэтому при ЛКМ рядом с андроидом
// мод подставляет его целью — в хуке теста рубильника атаки (hugger.cpp), прямо перед стартом ActionMeleeAttack
inline bool alienKillsAndroids = true;
inline int androidKillMode = 1;  // 0 — прямой удар; 1 — убийство, как человека (андроид на время удара — класс CIVILIAN)
inline std::atomic<addr> forceTarget{ 0 }, forceTargetGate{ 0 };
bool IsAndroid(addr e);  // тип персонажа 2 (андроид) или 7 (тяжёлый андроид)
void SetTargetOf(addr n, addr t);  // control.cpp: цель NPC (+0x42c) вместе с её номером (+0x430 = *(цель+0x50))
// персонаж разрушается (деструктор Чужого) / уровень выгружается — узлы движения мода не завершаем (control.cpp)
inline std::atomic<addr> dyingChar{ 0 };
bool HugIsMoveNode(addr node);  // hugger.cpp
// actors.cpp: общее для Чужого и лицехвата
uint32_t DeathState(addr e);                        // +0x2f90: 0 жив, 1 здоровье на нуле, 2 умирает, 3 мёртв
void TempHumanClass(addr e, ms_t ms);               // андроид на время — класс CIVILIAN (анимации убийства человека)
void QueueKill(addr victim, addr attacker, ms_t delayMs);  // смертельный удар в потоке игры, если жертва ещё жива

// ---------- Аманда следует за Чужим (Havok rigid body) ----------
inline bool amandaFollow = false;  // 6 — вкл/выкл, по умолчанию выключено
inline addr amandaRB, charObj;
inline float comOff[3];
inline ms_t amandaPauseUntil;

// ---------- режимы ИИ ----------
inline int aiMode;  // 0 ручное, 2 полный ИИ (9), 3 охотится, но не на Аманду (7)
inline bool aiZeroed, aiHid;
inline std::unordered_map<addr, uint8_t> aiInit;  // исходные значения всех рубильников

inline bool menuOpen;

// ---------- фоторежим (photo.cpp) ----------
struct PhotoState {
  bool on;
  V3 pos;
  float yaw, pitch, roll;     // рад; roll — наклон по Z
  float fov = 0;              // градусы, 0 — как у игры
  float speed = 4.f, sens = 1.f;
  bool freeze;                // остановить время
  float timeScale = 1.f;      // слоу-мо (1 — норма)
};
inline PhotoState photo;
// клавиша «за Чужого»: только пока камера у Чужого — тогда ввод до Аманды не доходит (input_block.cpp) и с клавишами игры
// ничего не пересекается (E, Q, R, F, C свободны); в фоторежиме молчит — там WASD/Q/E/C/R у свободной камеры
inline bool AlienKey(int vk) { return camMode != 0 && !photo.on && !hugger && Hot(vk); }
inline bool AlienKeyAdv(int vk) { return AlienKey(vk); }
inline bool AlienKeyK(int act) { return camMode != 0 && !photo.on && !hugger && HotK(act); }
inline bool AlienKeyAdvK(int act) { return AlienKeyK(act); }
inline std::atomic<int> photoMouseDX{ 0 }, photoMouseDY{ 0 }, photoWheel{ 0 };  // сырая мышь (input_block.cpp)
// фоторежим для машинимы: стрелки ведут персонажа относительно взгляда камеры (photo.cpp)
enum PhotoActorKind { PA_CAMERA = 0, PA_ALIEN = 1, PA_NPC = 2 };  // PA_NPC — лицехват/андроид, за которого играешь
struct PhotoDrive { bool on; int dx, dy, speed; float yaw; };     // on — стрелки сейчас у этого персонажа
inline std::atomic<int> photoActor{ PA_ALIEN };  // кого ведут стрелки (Tab), PA_CAMERA — стрелки крутят камеру
inline std::atomic<int> photoGear{ 1 };          // скорость персонажа 0 тихо .. 3 бегом (PgUp/PgDn)
inline std::atomic<int> photoFollow{ 0 };        // камера: 0 стоит, 1 едет за персонажем, 2 едет и смотрит на него
int PhotoActorNow();                             // кого реально ведут (с учётом того, кто есть на уровне)
PhotoDrive PhotoDriveFor(int kind);              // стрелки для Чужого (PA_ALIEN) / НПС (PA_NPC), если ведут его
bool PhotoTakeAttack(int kind);                  // Enter в фоторежиме — атака этого персонажа (забирает один раз)
std::string PhotoToast();                        // подпись на 1.5 с после Tab/PgUp/PgDn/T, "" — нет
void ActPhotoActor(int kind);                    // меню: кого ведут стрелки
void ActPhotoFollow(int mode);                   // меню: слежение камеры
bool PhotoActive();
void PhotoCamera(addr followBehaviour);  // поток игры, после апдейта follow-камеры
void ActPhoto(bool on);                  // F9
void PhotoOffForUnload();                // отцепление: без вызовов камер игры (камеру вернёт поток игры)
void ActPhotoToPlayer();
void PhotoReset();
void InstallPhoto();
bool FreezeAvailable();                  // остановка времени найдена в движке
void ActFreezeTime(bool on);
void ActTimeScale(float s);              // слоу-мо, 1 — норма
void ActPhotoParams(float fov, float rollDeg);
float PhotoGameFov();                    // FOV игры в момент входа в фоторежим
float PhotoTimeLive();                   // множитель игрового времени, который сейчас стоит в игре
// постобработка фоторежима — поверх того, что смешала игра из CATHODE-сущностей уровня (0 / 1 — «как в игре»)
struct PhotoPost {
  bool on = true;                        // применять, пока фоторежим включён
  float exposure = 0;                    // EV: цвет × 2^EV
  float brightness = 0;                  // + к яркости
  float contrast = 1, saturation = 1;    // × к игровым
  float temperature = 0, tint = 0;       // −1..1: холодно/тепло, зелёный/пурпурный
  float vignette = 0;                    // + к игровой виньетке
  float grain = 0;                       // зерно фоторежима ЦЕЛИКОМ: зерно уровня в фоторежиме выключается, 0 — без зерна
  float aberration = 0, sharpness = 0;   // аберрация + к игровой; резкость — свой проход (unsharp mask) перед ImGui
  float blur = 0;                        // размытие всего кадра, 0..1
  float bloom = 1;                       // × к силе свечения (bloom_gather_scale)
  float bloomThreshold = 1;              // × к порогу свечения (bloom_gather_exponent): меньше — светится больше
  float distortion = 0;                  // линза: + к радиальному искажению (бочка / «рыбий глаз»)
  bool noMotionBlur = true;              // убрать motion blur (кадр при полёте камеры не смазан)
  // глубина резкости (bokeh игры): фокус, объектив, диафрагма
  bool dof = false;
  float focus = 3.f;                     // м — дистанция резкости («точка» — клик по кадру или F)
  float focal = 50.f;                    // мм — фокусное расстояние объектива
  float fstop = 2.8f;                    // диафрагма f/…: меньше — сильнее размытие
  // кадр
  int letterbox = 0;                     // 0 нет, 1 — 2.39:1, 2 — 1.85:1, 3 — 4:3, 4 — 1:1, 5 — 9:16
  bool grid = false;                     // сетка третей
  bool hideGameHud = true;               // спрятать интерфейс игры (патроны, подсказки) в фоторежиме
  bool showAmanda = false;               // Аманда в кадре фоторежима (игра рисует её «от первого лица»: без головы)
};
inline PhotoPost photoPost;
enum PostField { PF_ON, PF_EXPOSURE, PF_BRIGHTNESS, PF_CONTRAST, PF_SATURATION, PF_TEMPERATURE, PF_TINT, PF_VIGNETTE,
                 PF_GRAIN, PF_ABERRATION, PF_SHARPNESS, PF_BLUR, PF_BLOOM, PF_NO_MOTION_BLUR, PF_RESET,
                 PF_BLOOM_THRESHOLD, PF_DISTORTION, PF_DOF, PF_FOCUS, PF_FOCAL, PF_FSTOP, PF_LETTERBOX, PF_GRID, PF_HIDE_HUD,
                 PF_PRESET, PF_SHOW_AMANDA };
inline std::atomic<int> photoFocusPick{ 0 };  // 1 — навести фокус по центру кадра, 2 — по точке photoFocusUV (клик)
inline float photoFocusUV[3] = { 0.5f, 0.5f, 16.f / 9.f };  // точка клика (0..1) и соотношение сторон кадра
inline std::atomic<float> photoFocusHit{ -1.f };  // последняя дистанция, найденная лучом (для подсказки в меню)
float PhotoSharpen();                             // сила своего прохода резкости (0 — выкл), поток рендера
void ActPhotoPost(int field, float v);

// ---------- путь Чужого за кулисами (для карты в HUD) ----------
inline std::vector<V3> bsTrail;
std::vector<V3> VentPositions();
inline bool autoComeOut = false;  // Чужой за кулисами после загрузки — сразу вывести к Аманде (галочка в меню; по умолчанию выкл)

// ---------- за Чужого: Аманда прячется, осмотр, голос, интерфейс игры (alien_extra.cpp) ----------
inline int amandaHide = 1;         // пока играешь за Чужого Аманда: 0 — стоит где стояла, 1 — под картой, 2 — над картой
inline bool amandaHidden;          // сейчас спрятана (дом запомнен — вернётся туда же)
inline bool amandaStay;            // пригнулась (вентиляция, укрытие) — не прячем и не водим: телепорт уронил бы её сквозь пол
inline std::atomic<ms_t> cutsceneRefusedAt{ 0 };  // не дали взять Чужого/НПС: камера игры занята катсценой (HUD)
bool CamCutsceneActive();
inline std::atomic<bool> g_wrongGame{ false };  // AI.exe не той сборки — мод не включился (mod_main.cpp)          // camera.cpp: текущая камера — не камера Аманды и не камеры мода (катсцена)
inline char devPartName[48];
inline std::atomic<addr> devPartsOf{ 0 };       // отладка: подмодели этого персонажа — в лог                    // отладка: имя подмодели для amodel 3/4
inline std::atomic<int> devAmandaModel{ -1 };  // отладка: 0 — спрятать модель Аманды, 1 — вернуть, 2 — подмодели в лог
inline std::atomic<int> devCamReq{ -1 }, devPhotoReq{ -1 };  // отладка: камера (ActCamera) / фоторежим
inline std::atomic<bool> alienLooking{ false };  // держат клавишу осмотра: Чужой стоит и осматривается
// осмотр: 1 — LOOK по камере (поза взгляда AUTONOMIC_LOOK_AT: голова и корпус туда, куда смотрит камера), 0 — как раньше
// (аддитивный осмотр SecondaryActLookAround)
inline int lookMode = 1;
// отладка (dev\cmd.txt: lookforce <мс> [theta] [phi]): LOOK без клавиши на время; theta >= 0 — свои углы позы взгляда
inline std::atomic<ms_t> devLookUntil{ 0 };
inline float devLookTheta = -1.f, devLookPhi = 0.f;
// отладка (dev\cmd.txt: headscale <кость> <мс> [режим]): масштаб 0 поддереву кости Чужого в позе Havok (camera.cpp)
inline std::atomic<int> devHeadBone{ -1 }, devHeadMode{ 0 };
inline std::atomic<ms_t> devHeadUntil{ 0 };
// ---------- вид из головы и развороты (camera.cpp, control.cpp) ----------
// вид из головы: true — свободный (мышь крутит взгляд, камера у головы, но не повёрнута вместе с ней), false — прибит к кости
inline std::atomic<bool> fpFree{ true };
inline std::atomic<int> fpMouseDX{ 0 }, fpMouseDY{ 0 };  // сырая мышь для свободного вида из головы (input_block.cpp)
inline float fpYaw, fpPitch;       // куда смотрит свободная камера из головы (рад; тангаж вверх — плюс)
inline bool fpHave;                // fpYaw/fpPitch уже заданы (при входе в вид — по курсу Чужого)
inline float fpSens = 1.f;         // чувствительность мыши в свободном виде из головы
inline float fpHeadInfluence = 0.2f;  // 0..1: насколько позиция камеры из головы следует за костью Head1 (0 — привязана к телу)
// резкий разворот на 180 (control.cpp): пока идёт, камера поворачивается вместе с телом (из головы — fpYaw в camera.cpp,
// за спиной — курс Аманды в OnTurnGameTick); на ходу из головы — камеру сразу назад, предел поворота головы на время снят
inline std::atomic<bool> turnFollowBody{ false }, fpFlipReq{ false };
inline float fpYawLimit = 100.f;      // градусов: дальше этого от курса тела взгляд не повернуть — сначала довернётся тело
inline bool fpHideHead = true;        // из головы не рисовать свою голову: череп SKULL01, купол SHELL01, crusher
inline float fpNearClip = 0.35f;      // из головы: ближняя плоскость отсечения, м (шея и пасть у самой камеры не рисуются)
inline bool fpNeckScale = true;       // из головы: шея, пасть, зубы — масштаб 0 костям шеи и головы при сборке палитры
bool FpFreeActive();               // сейчас свободный вид из головы (Чужой, не фоторежим, не кадр атаки)
// развороты на месте (IDLE_TURN_L/R_90/180): стоит, а камера смотрит в сторону — Чужой доворачивается на месте
inline bool turnInPlaceFp = true, turnInPlace3p = false;
float ViewYaw();                   // курс камеры, по которому идёт Чужой: свободный вид из головы — свой, иначе курс Аманды
bool ViewDir(V3& from, V3& dir);   // точка и направление взгляда текущей камеры (для LOOK); false — не знаем
inline bool alienAutoVoice = false;  // сам иногда рычит / шипит / принюхивается
inline float alienVoiceEvery = 14.f; // с — средний интервал автозвуков
inline int alienVoiceKey = -1;       // что играет клавиша звука: -1 — случайный из набора, иначе тип
inline bool hideGameHudAsAlien = true;  // за Чужого интерфейс Рипли (патроны, подсказки) спрятан
enum { VOICE_COUNT = 10 };
int VoiceType(int i);              // i-й звук в списке меню -> тип ActionTriggerSound
const char* VoiceName(int i);
void ActAlienVoice(int type);      // сыграть (в потоке игры, ближайший кадр)
void ActSetAmandaHide(int mode);
void AlienExtraReset(bool restoreAmanda);  // смена уровня (false) / Чужой уничтожен (true)
bool AmandaHome(V3& home);                 // Аманда спрятана — где она «на самом деле» стоит
void InstallAlienExtra();
inline bool autoOutPending;      // автовыход ещё не удался (повторяем); сбрасывается отменой 1 или уходом за кулисы

// ---------- действия (одни и те же для клавиш и для кнопок ImGui) — зовутся под общим замком ----------
// Раскладка: B — Чужой <-> Аманда; за Чужого: WASD, Shift бег, Ctrl тихо, ЛКМ атака, ПКМ цель, E вентиляция,
// Q вылезти у Аманды, V вид, R стрейф, F Аманда следует, C шахты (режим пути); F1 персонажи, F4 ИИ, F5 сброс, F7/F8 спавн
void ActCamera(int want);        // 1 голова, 2 за спиной, 3 вернуть Аманде
void ActCameraToggle(int want);  // включить эту камеру, повторно — вернуть Аманде
void ActSwap();                  // B: играть за Чужого (последний вид) / вернуться к Аманде
void ActSwitchView();            // V: вид за спиной <-> из головы
void ActTogglePath();            // C
void ActSetStrafe(bool on);
void ActToggleStrafe();          // R — вкл/выкл
void ActSetFollow(bool on);      // F — Аманда следует за Чужим
void ActVentGo();                // уйти за кулисы
void ActVentCome();              // выйти у Аманды
void ActVentCancel();
void ActVentSmart();             // E — по ситуации: в мире -> уйти, за кулисами -> выйти здесь, в пути -> отменить
void ActVentExitHere();          // выйти через ближайшую к Чужому вентиляцию
void ActVentExitNearAmanda();    // Q: выйти у Аманды (вентиляцию выбирает игра; не нашла — ближайшая к Аманде)
void ExitReset();
bool ExitBusy();                 // идёт выход из вентиляции (с повторами через другие вентиляции)
void ActAttack();                // ЛКМ
void ActAttackTarget(addr target);  // мультиплеер: Чужой-кукла бьёт эту цель (Аманду хоста), как удар по андроиду
void ActNextTarget();            // ПКМ
void ActSetAi(int mode);         // 0 ручное, 3 охотится не на Аманду, 2 полный ИИ
void ActCycleAi();               // F4: ручное -> охотится не на Аманду -> полный ИИ -> ручное
void ActPanic();                 // F5
void ActCamCfg(const CamCfg& c);  // из меню
void ActCamReset(int cam);       // средняя кнопка мыши: 2 — за спиной, 1 — из головы — по умолчанию
void ActTpAlienTo(addr e);       // Enter в меню F1
void ActTpBring(addr e);         // Backspace в меню F1
void ActUnstuck();               // X: «застрял» — за кулисами вылезти у Аманды, в мире — встать позади Аманды
void ActSpawnScan();             // F7 (первое нажатие)
void ActSpawnSelect(int i);      // F7 (листать)
void ActSpawn(bool again);       // F8 (again — Shift+F8: тот же объект ещё раз)
// ---------- снимок для ImGui (spawn.cpp / camera.cpp) ----------
struct SpawnInfo {
  bool scanning, done;
  int pct, sel;
  std::vector<addr> found;
  std::vector<std::string> labels;  // кто появился после спавна
  std::vector<int> spawned;         // сколько раз заспавнили
  std::vector<bool> gone;           // объект исчез (уровень выгрузил сущность)
  std::string msg;                  // последнее сообщение (лимит, повтор...)
  bool recent;                      // только что жали F7/F8 — показать список в HUD
  std::vector<bool> hugger;         // после спавна из этой сущности появился лицехват
};
// куда ставить заспавненного: 0 — где задумано уровнем, 1 — куда смотрит камера (центр экрана; во фрикаме фоторежима —
// куда угодно), 2 — перед тем, за кого играешь
inline int spawnWhere = 1;
void ActSpawnHugger();  // Ctrl+H (в фоторежиме — H): ещё один лицехват туда, куда смотрит камера
SpawnInfo SpawnSnapshot();

// ---------- control.cpp ----------
bool Alive(addr p, addr vtVa);
void SetGate(addr p, bool on);
addr TargetOf(addr n);  // цель NPC: npc+0x34a8 -> +0x10 -> +0x42c
void HardReset(const char* why);   // смена уровня: всё
void AlienReset(const char* why);  // Чужой уничтожен / перезагрузка после смерти: только Чужой
Input ReadInput();
void ApplyFace();
// ---------- camera.cpp ----------
addr CamG();
addr FollowBehRaw();
addr FollowBeh();  // null, пока камера у Аманды
void ApplyFollow();
void CamDo();
void CamRestore(const char* why);
void CamOffsetKeys();
// ---------- vent.cpp ----------
bool VentBusy();
// ---------- camera.cpp: лучи для выхода из вентиляции (только поток игры) ----------
bool RayDown(float x, float y, float z, float len, float& hitY);  // Havok-луч вниз: высота пола
bool NavClear(const V3& from, const V3& to);                      // по навмешу Аманды путь свободен
bool RayHit(const V3& from, const V3& to, V3& hit);                // Havok-луч: первая точка попадания (поток игры)
// ---------- actors.cpp ----------
bool TpMove(addr e, float x, float y, float z);
std::vector<addr> ActiveChars();
std::string CharName(addr e);
// ---------- spawn.cpp ----------
void SpawnReset();
// ---------- overlay.cpp ----------
void OverlaySet(int slot, const std::wstring& text);  // 0 статус, 1 кулисы, 2 меню/спавн
void StartOverlay();

// ---------- hugger.cpp: играть за лицехвата ----------
struct HugRow { addr e; std::string name; float dist; bool controlled, hasBranch; };
// ИИ лицехватов, за которых не играешь (и того, которого отпустил) — как у Чужого: 0 ручное (стоят), 3 охотятся, но не на
// Аманду, 2 полный ИИ. Пока играешь за лицехвата, «полный» у остальных = «не на Аманду»: к Рипли они теряют интерес.
inline int hugAi = 2;
int HugAiNow();                       // режим, который сейчас действует на остальных лицехватов
inline int androidAi = 2;             // ИИ андроидов, за которых не играешь: 0 ручное (стоят), 3 не на Аманду, 2 полный
int AndroidAiNow();
// за кого играешь (не Чужой): 0 — никто, 1 — лицехват, 2 — андроид; что он делает: 0 — ничего, 1 — бежит к цели /
// прыгает / бьёт, 2 — на лице жертвы, 3 — на лице лежащей жертвы (hugger.cpp)
int NpcCtlKind();
bool HugIsPlayable(addr e);  // hugger.cpp: лицехват или андроид (за него можно играть)
int NpcActionState();
const char* NpcVoiceName();  // hugger.cpp: последняя реплика андроида (для HUD), пусто — давно не говорил
// «Удобный Чужой»: отпустил клавиши — останавливается почти сразу, без долгих WALK/CHARGE_TO_IDLE (control.cpp)
inline bool alienSnappy = false;
// слюни и дым изо рта Чужого — всегда, пока камера на нём (иначе игра показывает их, только если Аманда в 6 м, alien_extra.cpp)
inline bool alienBreath = true;
void ActSetAiAll(int mode);           // «ИИ для всех»: Чужому и лицехватам один режим (F4 — по кругу)
inline bool hugAttackAmanda = false;  // прыгать на Аманду (это её смерть) — только если разрешено
inline int hugMoveMode = 0;           // 0 — само (напрямую, не идёт — по навмешу), 1 — напрямую, 2 — по навмешу
inline bool hugPathNow = false;       // сейчас лицехват ходит «по навмешу»
inline bool hugNoBranch = false;      // у лицехвата нет ветки мода (не установлен BML)
inline ms_t hugMsgAt = 0;             // «прыгнуть не на кого, кроме Аманды» — время для HUD
std::vector<HugRow> HuggerList();
bool IsHugger(addr e);
void ActHugger(addr e);   // играть за этого лицехвата (0 — вернуться к Аманде)
void ActHuggerNext();     // H: следующий лицехват (ближе к Аманде — раньше)
void HuggerReset();       // смена уровня
void HugOnDtor(addr ch);
void HugOnAnyDtor(addr ch);  // hugger.cpp: разрушают любого персонажа (жертва лицехвата — отпустить)
bool HugAttackFocus(V3& out);
bool HugAttackLying();        // hugger.cpp: жертва легла, лицехват лежит на лице  // hugger.cpp: голова жертвы лицехвата (поток игры)
void CamKillCam(addr victim);
void CamOnAnyDtor(addr ch);   // camera.cpp: персонаж разрушается — кадр атаки с него снять  // camera.cpp: камера следует за жертвой лицехвата (0 — обратно на него)  // игра разрушает лицехвата, за которого играешь: камеру — Аманде сразу (camera.cpp, деструктор)
// из хуков control.cpp
void HugOnGate(const char* text, addr node);
void HugForget(addr node);                // новый узел на месте старого — забыть старый
void HugOnMoveNode(addr node);
bool HugOnMoveStart(addr node, addr ch);  // true — это узел лицехвата
void HugOnMoveStartLeave();
void HugOnReq(addr r);
void HugOnMoveShutdown(addr node, addr ch);
bool HugPathFor(addr ch);                 // этот персонаж — наш лицехват и ходит «по навмешу»
bool HugPathTarget(addr ch, V3& out);
int HugPathSpeed();
void HugNodeDestroyed(addr node);
void FindPlayer();

void InstallControl();
void InstallHugger();
void InstallCamera();
void InstallVent();
void InstallActors();
void InstallSpawn();
void InstallInputBlock();
void InstallGameMenu();   // gamemenu.cpp: пункт «Мультиплеер» в главном меню и меню паузы игры
void MenuCursor(bool menuShown);

// ---------- отцепление мода и передача узлов следующей DLL (reload.cpp) ----------
// Узлы дерева мод узнаёт, только когда игра их создаёт (загрузка уровня). Чтобы новую DLL можно было внедрить посреди
// уровня, старая при отцеплении передаёт ей найденные узлы.
struct NodeRec { int kind; addr p; int init; };  // kind: 0..G_COUNT-1 — рубильник Чужого, 100 — его узел движения,
                                                 // 200+k — рубильник лицехвата, 300 — узел движения лицехвата
enum { NR_ALIEN_MOVE = 100, NR_HUG_GATE = 200, NR_HUG_MOVE = 300, NR_HK_WORLD = 400 };  // 400 — мир Havok (p)
void CameraWorldReset();        // camera.cpp
void CameraAdoptWorld(addr w);
void ControlExport(std::vector<NodeRec>& out);  // control.cpp
void ControlAdopt(const NodeRec& r);
void ControlAdoptDone();
void ControlShutdown();                          // рубильники Чужого — как их создала игра
void HugExport(std::vector<NodeRec>& out);       // hugger.cpp
void GateProbeSet(const std::vector<addr>& nodes);  // hugger.cpp: следить, проверяет ли игра эти рубильники
bool GateProbeSeen(addr node);
bool HugHasGates();                               // hugger.cpp: ветка лицехвата уже известна
void ScanExistingTrees();                         // treescan.cpp: внедрили посреди уровня — найти узлы в памяти
void HugAdopt(const NodeRec& r);
void CameraShutdown(bool mayCallGame);          // camera.cpp: камера — Аманде, флаг вида от первого лица — как был (false — без функций камер игры)
void AlienExtraShutdown();                       // alien_extra.cpp: Аманда домой, осмотр
bool GameHudHidden();                            // alien_extra.cpp: HUD игры ещё спрятан модом
void InstallReload();                            // reload.cpp: принять узлы от прошлой DLL (до Interceptor::Commit)  // input_block.cpp: меню открыли — курсор свободен, закрыли — как было у игры
