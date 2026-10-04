// mp.cpp — мультиплеер: лобби через Steam P2P (ISteamNetworking из steam_api.dll самой игры) и «выход X у сущности Y» —
// перехват CallTriggers на хосте и повтор у клиентов.
//
// Лобби (MULTIPLAYER.md, «Лобби»):
//   * хост «создаёт лобби» — уровень не грузится; принимает всех, кто стучится (кроме забаненных), до kMpMaxClients.
//     Выбирает карту (уровень); «Начать» — у хоста и у всех в лобби грузится эта карта. Может выгнать или забанить.
//   * клиент выбирает, за кого играть (человек / Чужой / андроид), вписывает адрес хоста (его SteamID64, ссылку на
//     профиль или код друга) и ждёт в лобби. После «Начать» грузит карту хоста; мод отдаёт ему выбранного персонажа.
//     Подключился, когда игра уже идёт, — сразу грузит текущую карту. Хост сам перешёл на другой уровень — клиенты за ним.
//   * провод (канал 3, 'AIMP', версия 6): HELLO клиент->хост (персонаж, имя; вход и «я тут»), LOBBY хост->клиент
//     (идёт ли игра, карта, все в лобби), KICK хост->клиент (выгнан / забанен / лобби закрыто / мест нет), BYE клиент->хост,
//     PING/PONG в обе стороны, TRIG/FORGET хост->все клиенты, STATE — тело игрока (puppet.cpp) 20 раз в секунду без
//     гарантии доставки: клиент -> хосту, хост -> всем; чужие STATE хост пересылает остальным как есть. ACT клиент->хост —
//     удар его Чужого по кнопке (Чужой-кукла у хоста бьёт; сам по себе, касанием, не бьёт).
//
// Выходы сущностей (MULTIPLAYER.md, «Выходы сущностей»):
//   * через CallTriggers (0x008bd920) сущности CATHODE сообщают свои выходы ("on_opened", "finished_spawning"...).
//     На входе берём сущность -> её постоянную ссылку EntityHandle {GUID сущности, ID экземпляра составной}
//     (FUN_008c3360; одинакова у всех копий игры на одном уровне) + GUID выхода + задержку.
//   * только «корневые» вызовы — не внутри другого CallTriggers этого потока (Interceptor::ActiveDepth): каскад, который
//     выход запускает сам, у клиента повторится сам, иначе он сработал бы дважды.
//   * шум не шлём: корневых выходов ~6000 в секунду, почти все — тиканье (on_think, refreshed) и логические узлы, которые
//     пересчитываются каждый кадр (triggered, on_true/on_false). Они и так считаются у каждого сами. Отсев: on_think и
//     refreshed — всегда; любой ключ чаще 3 раз в секунду, 8 раз за 10 с или 12 раз за минуту — «шумный» навсегда
//     (до «Очистить»). Дверь столько раз подряд не открывают, а мигающая лампа — да. Хост шлёт выход через
//     150 мс после него — успевает понять, что ключ шумный; ключ, ставший шумным после отправки, клиенты «забывают»
//     (W_FORGET) — и снова считают его сами.
//   * this у CallTriggers — интерфейс самой сущности: её подобъект (+0x14 или +0x1c — по раскладке класса), у чьей
//     vtable слот +0x394 — диспетчер связей FUN_008be0d0 (у экземпляров составных — FUN_00965920) (IfaceOf).
//   * клиент находит ту же сущность (find_entity 0x009236a0) и зовёт CallTriggers(её интерфейс, ...) в потоке игры. Свои
//     корневые выходы с теми же ключами клиент глушит (inv.skip) — мир ведёт хост.
//   * повтор зовётся из нашего слушателя LocoUpdate, а внутри слушателя вложенные хуки не срабатывают — поэтому повтор
//     не попадает ни в журнал, ни под глушилку.
// Потоки: сеть — свой поток без общего замка (Steam не ждёт игру и наоборот); общее состояние — под s_mx (маленький
// замок: внутри ни Steam, ни функций игры, ни чтения памяти игры). Порядок: общий замок -> s_mx, никогда наоборот.
// Меню зовёт MpCmd/MpSnapshot прямо из потока рендера — им общий замок не нужен. Обратный вызов Steam регистрируется
// и уровень грузится только в главном потоке игры (слушатель SteamAPI_RunCallbacks).
#include "mp.h"
#include "game.h"
#include "interceptor.h"
#include "mp_names.h"
#include "puppet.h"
#include "state.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <tlhelp32.h>
#include <unordered_map>
#include <unordered_set>

const std::wstring& ModBase();  // runtime.cpp
std::string LevelCurrentName();               // standalone.cpp: «Production\Frontend» — главное меню
namespace AlienMod { void OnLevelChange(const char* why); }  // mod_main.cpp: сброс мода перед сменой уровня
void GameMenuReleaseNow(const char* why);  // gamemenu.cpp: отпустить свои объекты меню игры (до закрытия экрана)

static const char* const kSkinRu[kMpSkinCount] = { "Аманда Рипли", "Эллен Рипли", "Ренсом", "Рикардо", "Сэмюэлс", "Тейлор", "Марлоу",
                                                   "Уэйтс", "Аксель", "Хьюз", "Даллас", "Паркер", "Ламберт" };
static const char* const kSkinEn[kMpSkinCount] = { "Amanda Ripley", "Ellen Ripley", "Ransome", "Ricardo", "Samuels", "Taylor", "Marlow",
                                                   "Waits", "Axel", "Hughes", "Dallas", "Parker", "Lambert" };
static const char* const kAvatarRu[AV_COUNT] = { "человек", "Чужой", "андроид", "лицехват" };
static const char* const kAvatarEn[AV_COUNT] = { "a human", "the Alien", "an android", "a facehugger" };
const char* MpAvatarName(int a) { return a >= 0 && a < AV_COUNT ? TR(kAvatarRu[a], kAvatarEn[a]) : "?"; }
// миссии кампании (по уровням, где они идут — standalone.cpp, «Уровни»)
static const char* const kMissionRu[kMpMissionCount] = {
  "«Торренс»", "космопорт", "космопорт, путь к Лоренц", "башня Лоренц, центр связи", "башня Лоренц", "госпиталь Сан-Кристобаль",
  "Сигсон синтетикс", "галерея Соломонс", "LV-426", "R&D Лоренц", "галерея и космопорт", "Сигсон синтетикс, Сан-Кристобаль",
  "ядро APOLLO", "реактор и гнездо", "«Анесидора»", "центр связи Сигсон", "корпоративный пентхаус", "буксировочная платформа",
  "«Торренс», финал" };
static const char* const kMissionEn[kMpMissionCount] = {
  "the Torrens", "spaceflight terminal", "spaceflight terminal, to Lorenz", "Lorenz spire, comms", "Lorenz SysTech spire",
  "San Cristobal Medical", "Seegson Synthetics", "Solomons Galleria", "LV-426", "Lorenz R&D", "Galleria and terminal",
  "Seegson Synthetics, San Cristobal", "APOLLO core", "reactor and the nest", "the Anesidora", "Seegson Communications",
  "corporate penthouse", "tow platform", "the Torrens, finale" };
const char* MpMissionName(int n) {
  static char b[2][96];
  static int k = 0;
  if (n < 1 || n > kMpMissionCount) return "?";
  k ^= 1;
  std::snprintf(b[k], sizeof b[k], TR("миссия %d — %s", "mission %d — %s"), n, TR(kMissionRu[n - 1], kMissionEn[n - 1]));
  return b[k];
}

static const char* const kModeRu[kMpModeCount] = { "Кампания (с начала)", "DLC «Ностромо»: Последний выживший",
                                                     "DLC «Ностромо»: Экипаж — расходный материал", "Выживание (карты DLC)",
                                                     "Кампания: миссия…" };
static const char* const kModeEn[kMpModeCount] = { "Campaign (from the start)", "Nostromo DLC: Last Survivor", "Nostromo DLC: Crew Expendable",
                                                     "Survivor (DLC maps)", "Campaign: a mission…" };
// персонажи выживания: Character1..9 (иконки amanda, axel, dallas, ellen, hughes, lingard, parker, ransome, ricardo)
static const char* const kCharRu[kMpCharCount] = { "Аманда Рипли", "Аксель", "Даллас", "Эллен Рипли", "Хьюз", "Лингард", "Паркер", "Ренсом", "Рикардо" };
static const char* const kCharEn[kMpCharCount] = { "Amanda Ripley", "Axel", "Dallas", "Ellen Ripley", "Hughes", "Lingard", "Parker", "Ransome", "Ricardo" };
const char* MpCharName(int c) { return c >= 1 && c <= kMpCharCount ? TR(kCharRu[c - 1], kCharEn[c - 1]) : "?"; }
static const char* const kDiffRu[kMpDiffCount] = { "новичок", "лёгкая", "средняя", "тяжёлая", "кошмар" };
static const char* const kDiffEn[kMpDiffCount] = { "novice", "easy", "medium", "hard", "nightmare" };
const char* MpModeName(int m) { return m >= 0 && m < kMpModeCount ? TR(kModeRu[m], kModeEn[m]) : "?"; }
const char* MpDiffName(int d) { return d >= 0 && d < kMpDiffCount ? TR(kDiffRu[d], kDiffEn[d]) : "?"; }

const char* MpSkinName(int i) {
  if (i < 0 || i >= kMpSkinCount) return "?";
  return TR(kSkinRu[i], kSkinEn[i]);
}

namespace {

// ---------------- Steam (SDK ~1.26: ISteamNetworking005, ISteamUser017, ISteamFriends014) ----------------
// методы интерфейсов — thiscall, CSteamID по значению = 8 байт на стеке; GetSteamID возвращает класс через скрытый указатель
typedef addr(__cdecl* SteamGetFn)();
typedef bool(__thiscall* SendP2PFn)(addr self, uint64_t to, const void* data, uint32_t size, int sendType, int channel);
typedef bool(__thiscall* AvailP2PFn)(addr self, uint32_t* size, int channel);
typedef bool(__thiscall* ReadP2PFn)(addr self, void* dest, uint32_t cap, uint32_t* size, uint64_t* from, int channel);
typedef bool(__thiscall* SessionFn)(addr self, uint64_t who);
typedef bool(__thiscall* RelayFn)(addr self, bool allow);
typedef uint64_t*(__thiscall* GetSteamIdFn)(addr self, uint64_t* out);
typedef const char*(__thiscall* PersonaNameFn)(addr self);
typedef bool(__thiscall* SessionStateFn)(addr self, uint64_t who, void* state);
typedef int(__thiscall* FriendCountFn)(addr self, int flags);
typedef uint64_t*(__thiscall* FriendByIndexFn)(addr self, uint64_t* out, int index, int flags);
typedef const char*(__thiscall* FriendNameFn)(addr self, uint64_t who);
typedef bool(__thiscall* FriendGameFn)(addr self, uint64_t who, void* info);
typedef void(__cdecl* RegCbFn)(void* cb, int id);
typedef void(__cdecl* UnregCbFn)(void* cb);
enum { NET_SEND = 0, NET_AVAIL = 1, NET_READ = 2, NET_ACCEPT = 3, NET_CLOSE = 4, NET_STATE = 6, NET_RELAY = 7 };
enum { FR_COUNT = 3, FR_BY_INDEX = 4, FR_NAME = 7, FR_GAME = 8 };  // ISteamFriends014
constexpr int kFriendImmediate = 4;
constexpr uint32_t kAppId = 214490;  // Alien: Isolation
// P2PSessionState_t (pack 8): активна, соединяется, ошибка, через relay, байт/пакетов в очереди, IP, порт
struct P2PState { uint8_t active, connecting, error, relay; int32_t bytesQueued, packetsQueued; uint32_t ip; uint16_t port, pad; };
const char* P2PError(int e) {
  switch (e) {
    case 0: return "none";
    case 1: return "partner is not running the game";
    case 2: return "partner has no rights to the game";
    case 3: return "partner is not logged in to Steam";
    case 4: return "timeout (host didn't accept / NAT)";
    default: return "?";
  }
}
enum { P2P_UNRELIABLE = 0, P2P_RELIABLE = 2 };
constexpr int kCbSessionRequest = 1202;  // k_iSteamNetworkingCallbacks + 2: P2PSessionRequest_t { CSteamID }
constexpr uint64_t kSteamId0 = 76561197960265728ull;
template <class F> F VSlot(addr iface, int i) { return reinterpret_cast<F>(rp(rp(iface) + addr(i) * 4)); }

addr s_net = 0, s_user = 0, s_friends = 0;
HMODULE s_steamDll = nullptr;

// ---------------- провод ----------------
#pragma pack(push, 1)
struct WHdr { uint32_t magic; uint8_t ver, type; uint16_t n; };
struct WTrig { uint32_t ent, inst, out; float delay; };
#pragma pack(pop)
enum { W_HELLO = 1, W_PING = 2, W_PONG = 3, W_TRIG = 4, W_BYE = 5, W_SELF = 6, W_FORGET = 7, W_LOBBY = 8, W_KICK = 9, W_STATE = 10, W_ACT = 11 };
enum { ACT_ATTACK = 1 };  // W_ACT клиент -> хост: [что]; удар Чужого по кнопке — Чужой-кукла хоста бьёт (puppet.cpp)
enum { KICK_KICKED = 0, KICK_BANNED = 1, KICK_CLOSED = 2, KICK_FULL = 3 };
constexpr uint32_t kMagic = 0x504D4941;  // "AIMP"
constexpr uint8_t kVer = 10;  // 10: + какие роли есть на уровнях режима (0.20)  // 9: + лицехват, свой персонаж выживания (0.19)  // 5: + выживание (0.16); 6: + STATE — тела (0.17); 7: + кто есть кто, удар (0.17.1); 8: + миссии, хост за кого угодно (0.18)
constexpr int kChan = 3, kSelfChan = 7;
constexpr size_t kTrigPerPacket = 300;
constexpr uint32_t kOutOnThink = 0x8FECEA29u, kOutRefreshed = 0xAAB30B8Fu;  // ShortGuid("on_think"), ShortGuid("refreshed")
constexpr uint32_t kNoisyPerSec = 3, kNoisyPer10s = 8, kNoisyPerMin = 12;  // чаще — шум (мигающие лампы, периодические проверки)
constexpr ms_t kSendDelayMs = 150;
constexpr ms_t kSilentDrop = 15000;  // столько молчит — пропал
constexpr ms_t kStateEvery = 50;     // тело игрока — 20 раз в секунду
constexpr ms_t kStateAfterLoad = 2000;  // после загрузки уровня — не сразу (персонажи ещё создаются)

// ---------------- состояние ----------------
// Замок не может повесить игру навсегда: если его держат дольше секунды (держатель упал внутри Safe и не отпустил —
// деструкторы там не зовутся), идём дальше без него и пишем в лог. Редкая гонка лучше зависшей игры.
SRWLOCK s_mx = SRWLOCK_INIT;
std::atomic<int> s_lockTimeouts{ 0 };
struct Lk {
  bool owned = false;
  Lk() {
    if (TryAcquireSRWLockExclusive(&s_mx)) {
      owned = true;
      return;
    }
    const ms_t until = NowMs() + 1000;
    while (NowMs() < until) {
      Sleep(0);
      if (TryAcquireSRWLockExclusive(&s_mx)) {
        owned = true;
        return;
      }
    }
    if (s_lockTimeouts++ < 3) Log("mp: lock timeout (held over 1 s) — going on without it");
  }
  ~Lk() {
    if (owned) ReleaseSRWLockExclusive(&s_mx);
  }
};
// быстрый путь хука (атомики, без замка)
std::atomic<int> s_role{ MP_OFF }, s_state{ MPS_IDLE };
std::atomic<bool> s_tap{ false }, s_suppress{ false }, s_gameMenu{ false };
// синхронизация выходов — только когда уровень «устоялся»: хост шлёт, клиент принимает лишь через kSettleMs после того,
// как сам стоит на уровне (и у клиента — на том же, что хост). 02.10.2026: хост загрузился раньше и сразу слал события
// начала уровня, клиент повторял их посреди своей загрузки и глушил свои такие же — у клиента игра встала.
std::atomic<bool> s_settled{ false };
constexpr ms_t kSettleMs = 10000;
std::atomic<bool> s_lobbyOpen{ false }, s_started{ false };  // хост: лобби открыто; игра идёт (у хоста и у клиента)
std::atomic<int> s_memberCount{ 0 };
std::atomic<addr> s_lastTI{ 0 };     // TriggerInfo последнего живого вызова (куча, пул — у вызовов разный)
std::atomic<ms_t> s_lastTIAt{ 0 };   // повтор берёт только свежий: после смены уровня старый может быть освобождён
std::atomic<int> s_replayN{ 0 };
std::atomic<bool> s_stop{ false };
// сторож зависаний: главный поток игры тикает здесь каждый кадр; не тикал 25 с — сетевой поток снимет его стек в лог
std::atomic<DWORD> s_mainTid{ 0 };
std::atomic<ms_t> s_mainTickAt{ 0 };
HANDLE s_thread = nullptr;

struct K3 {
  uint32_t a, b, c;
  bool operator==(const K3& o) const { return a == o.a && b == o.b && c == o.c; }
};
struct K3H {
  size_t operator()(const K3& k) const { return size_t(k.a * 0x9E3779B1u ^ (k.b + 0x7F4A7C15u) * 0x85EBCA77u ^ k.c * 0xC2B2AE3Du); }
};
struct JEntry { MpTrig t; };
struct Replay { WTrig w; };
struct QItem { WTrig w; ms_t at; };
struct Rate { ms_t t0, t10, t60; uint32_t n, n10, n60; bool noisy; };
struct Member { uint64_t id; std::string name; int avatar, skin, chr; uint32_t missions; ms_t joinedAt, lastRecv; int pingMs; };

// всё ниже — под s_mx
bool s_steamOk = false;
uint64_t s_me = 0;
std::string s_name;
int s_skin = 0, s_avatar = AV_ALIEN, s_myChar = 1;  // s_myChar — мой персонаж в выживании 1..9
// хост
std::vector<Member> s_members;
std::vector<MpBan> s_bans;
std::string s_lobbyLevel;                        // уровень хоста сейчас (короткое имя); у клиента — из LOBBY
int s_lobbyMode = 0, s_lobbyDiff = 2;            // что запускать и сложность (у клиента — из LOBBY)
std::string s_lobbyMap;                          // выживание: id карты
int s_lobbyChar = 1;                             // выживание: персонаж 1..9
int s_lobbyMission = 6;                          // миссия кампании 1..19
int s_startMission = 6;
uint32_t s_myMissions = 0;                       // бит n — есть своё сохранение миссии n (MSSVnn.ais)
ms_t s_missionsAt = 0;
uint8_t s_lobbyRoles = 0xF;                      // какие роли есть на уровнях режима (бит 1 << MpAvatar)
std::string s_rolesKey;                          // для чего посчитаны (режим, миссия, карта)
std::string s_startMap;                          // что запускать (главный поток)
int s_startChar = 1;
uint8_t s_startGen = 0, s_myGen = 0;             // номер «Начать» у хоста / какой старт клиент уже выполнил
int s_startReq = -1, s_startDiff = 2;            // запустить режим (главный поток); -1 — нечего
ms_t s_startAt = 0;                              // когда запустили — проверить, что уровень пошёл
bool s_lobbyDirty = false;                       // разослать LOBBY сейчас
ms_t s_lobbySentAt = 0, s_pingAt = 0;
std::vector<std::pair<uint64_t, int>> s_kickQ;   // кого выгнать (и почему) — шлёт сетевой поток
std::vector<uint64_t> s_acceptQ;                 // чьи сессии принять (главный поток просит, сеть принимает)
std::vector<uint64_t> s_actRelay;                // хост: чей удар Чужого переслать остальным клиентам
// клиент
uint64_t s_peer = 0;                             // хост
std::string s_hostName;
std::vector<MpMember> s_lobbyView;               // кто в лобби (как прислал хост)
int s_pingMs = -1;
ms_t s_lastRecv = 0, s_lastHello = 0, s_connectAt = 0;
bool s_helloNow = false;
int s_helloSent = 0;
uint64_t s_byeTo = 0;                            // выйти из лобби — сеть шлёт BYE
// уровни: свой (читает сетевой поток раз в секунду); короткие имена, пусто — меню / загрузка
std::string s_myLevel;
int s_myGameState = 0;
ms_t s_myLevelAt = 0, s_levelSince = 0;          // когда стали на текущий уровень
std::atomic<bool> s_avatarDone{ false };         // клиенту уже отдали управление выбранным персонажем на этом уровне
ms_t s_avatarTryAt = 0, s_avatarSaidAt = 0;
// общее
int s_selfTest = 0;
uint32_t s_selfNonce = 0;
ms_t s_selfAt = 0, s_lastStateLog = 0, s_lastFriends = 0, s_bodyAt = 0;
uint32_t s_atkSeen = 0;                          // клиент: удары Чужого, о которых хост уже знает (g_localAttackSeq)
std::string s_identSent;                         // какое «кем играю» уже отдали puppet.cpp
// персонажи выживания (Character1..9) — как их зовут модели и сущности игры (puppet.cpp ищет того же в третьем лице)
const char* const kCharIdent[kMpCharCount] = { "AMANDA", "AXEL", "DALLAS", "RIPLEY", "HUGHES", "LINGARD", "PARKER", "RANSOME", "RICARDO" };
bool s_selfReq = false;
std::unordered_set<uint64_t> s_seenFrom;         // от кого уже был пакет (первый — в лог)
std::vector<MpFriend> s_friendList;              // друзья, которые сейчас в Alien: Isolation
std::string s_statusRu, s_statusEn;
uint32_t s_sent = 0, s_recv = 0, s_replayed = 0, s_suppressed = 0, s_captured = 0, s_missed = 0;
std::vector<JEntry> s_jr;                        // журнал: новые сверху
std::vector<QItem> s_sendQ;                      // хост: ждут отправки (через kSendDelayMs)
std::vector<WTrig> s_forgetQ;                    // хост: стали шумными после отправки — клиентам «забыть»
std::unordered_map<K3, Rate, K3H> s_rate;        // частота каждого ключа: шумные не шлём
std::unordered_set<K3, K3H> s_sentKeys;          // хост: что уже ушло клиентам
uint32_t s_filtered = 0, s_noisyKeys = 0;
std::deque<Replay> s_replayQ;                    // повторить в потоке игры
std::unordered_set<K3, K3H> s_hostKeys;          // клиент: выходы, которые ведёт хост — свои такие глушим
std::atomic<uint32_t> s_ecxOdd{ 0 };             // вызовов, где this != IfaceOf(сущность) (ожидаем 0)
std::unordered_map<uint64_t, uint32_t> s_stat;   // (VA vtable << 32 | выход) -> сколько раз (журнал включён): кто шумит
std::atomic<bool> s_saveReq{ false };

constexpr size_t kJournal = 48, kSendCap = 8192, kReplayCap = 4096;
int s_verifyLeft = 20;  // первые пойманные выходы: проверить ручку туда-обратно (find_entity даёт ту же сущность?)

// постоянный лог мультиплеера: AlienModUI.mp.log рядом с модом, только дописывается (основной лог перезаписывается при
// каждом внедрении — 01.10.2026 так потерялась неудачная попытка подключиться). Больше 2 МБ — в .mp.prev.log.
SRWLOCK s_logMx = SRWLOCK_INIT;
void MpLog(const char* fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  Log("mp: %s", msg);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[600];
  const int n = std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                              t.wMinute, t.wSecond, t.wMilliseconds, msg);
  AcquireSRWLockExclusive(&s_logMx);
  const std::wstring f = ModBase() + L".mp.log";
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > (2u << 20))
    MoveFileExW(f.c_str(), (ModBase() + L".mp.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
  const HANDLE h = CreateFileW(f.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD w;
    WriteFile(h, line, DWORD(std::max(n, 0)), &w, nullptr);
    CloseHandle(h);
  }
  ReleaseSRWLockExclusive(&s_logMx);
}

}  // namespace
void MpNote(const char* fmt, ...) {  // puppet.cpp: строка в AlienModUI.mp.log (без s_mx)
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  MpLog("%s", msg);
}
namespace {

void Status(const char* ru, const char* en, const char* arg = "") {  // под s_mx; ru/en — с одним %s
  char b[256];
  std::snprintf(b, sizeof b, ru, arg);
  s_statusRu = b;
  std::snprintf(b, sizeof b, en, arg);
  s_statusEn = b;
  MpLog("%s", b);
}
const char* RoleStr(int r) { return r == MP_HOST ? "host" : r == MP_CLIENT ? "client" : "no role"; }
std::string IdStr(uint64_t id) {
  char b[24];
  std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(id));
  return b;
}

uint64_t ParseId(const char* s) {  // MpParseId  // SteamID64, ссылка на профиль или код друга (ID аккаунта)
  uint64_t best = 0;
  size_t bestLen = 0;
  for (const char* p = s; *p;) {
    if (*p < '0' || *p > '9') { p++; continue; }
    uint64_t v = 0;
    size_t n = 0;
    for (; *p >= '0' && *p <= '9'; p++, n++) v = v * 10 + uint64_t(*p - '0');
    if (n > bestLen) best = v, bestLen = n;
  }
  if (!bestLen) return 0;
  if (best < (1ull << 32)) best += kSteamId0;  // код друга в Steam — это ID аккаунта
  return (best >> 56) == 1 && ((best >> 52) & 0xF) == 1 ? best : 0;
}

bool BannedLocked(uint64_t id) {
  for (const MpBan& b : s_bans)
    if (b.id == id) return true;
  return false;
}
Member* MemberLocked(uint64_t id) {
  for (Member& m : s_members)
    if (m.id == id) return &m;
  return nullptr;
}

// ---------------- настройки ----------------
std::wstring Ini() { return ModBase() + L".ini"; }
void SaveCfg() {  // вне s_mx
  const std::wstring f = Ini();
  wchar_t b[64];
  swprintf(b, 64, L"%d", s_role.load());
  WritePrivateProfileStringW(L"mp", L"role", b, f.c_str());
  int skin, av;
  uint64_t peer;
  std::wstring bans;
  {
    Lk l;
    skin = s_skin, peer = s_peer, av = s_avatar;
    for (const MpBan& x : s_bans) {
      swprintf(b, 64, L"%s%llu", bans.empty() ? L"" : L",", static_cast<unsigned long long>(x.id));
      bans += b;
    }
  }
  swprintf(b, 64, L"%d", skin);
  WritePrivateProfileStringW(L"mp", L"skin", b, f.c_str());
  swprintf(b, 64, L"%llu", static_cast<unsigned long long>(peer));
  WritePrivateProfileStringW(L"mp", L"peer", b, f.c_str());
  swprintf(b, 64, L"%d", av);
  WritePrivateProfileStringW(L"mp", L"avatar3", b, f.c_str());
  {
    int chr;
    {
      Lk l;
      chr = s_myChar;
    }
    swprintf(b, 64, L"%d", chr);
    WritePrivateProfileStringW(L"mp", L"my_char", b, f.c_str());
  }
  WritePrivateProfileStringW(L"mp", L"bans", bans.c_str(), f.c_str());
  WritePrivateProfileStringW(L"mp", L"tap", s_tap ? L"1" : L"0", f.c_str());
  WritePrivateProfileStringW(L"mp", L"suppress2", s_suppress ? L"1" : L"0", f.c_str());
  WritePrivateProfileStringW(L"mp", L"game_menu", s_gameMenu ? L"1" : L"0", f.c_str());
  swprintf(b, 64, L"%d", PupHumanBody());
  WritePrivateProfileStringW(L"mp", L"human_body", b, f.c_str());
}
void LoadCfg() {
  const std::wstring f = Ini();
  s_role = std::clamp(int(GetPrivateProfileIntW(L"mp", L"role", MP_OFF, f.c_str())), 0, 2);
  s_tap = GetPrivateProfileIntW(L"mp", L"tap", 0, f.c_str()) != 0;
  s_suppress = GetPrivateProfileIntW(L"mp", L"suppress2", 0, f.c_str()) != 0;  // по умолчанию выкл (см. s_settled)
  s_gameMenu = GetPrivateProfileIntW(L"mp", L"game_menu", 0, f.c_str()) != 0;
  PupSetHumanBody(int(GetPrivateProfileIntW(L"mp", L"human_body", PB_AUTO, f.c_str())));
  wchar_t b[32] = {}, bans[2048] = {};
  GetPrivateProfileStringW(L"mp", L"peer", L"", b, 32, f.c_str());
  GetPrivateProfileStringW(L"mp", L"bans", L"", bans, 2048, f.c_str());
  Lk l;
  s_skin = std::clamp(int(GetPrivateProfileIntW(L"mp", L"skin", 0, f.c_str())), 0, kMpSkinCount - 1);
  s_peer = wcstoull(b, nullptr, 10);
  s_avatar = std::clamp(int(GetPrivateProfileIntW(L"mp", L"avatar3", AV_ALIEN, f.c_str())), 0, AV_COUNT - 1);
  s_myChar = std::clamp(int(GetPrivateProfileIntW(L"mp", L"my_char", 1, f.c_str())), 1, kMpCharCount);
  for (wchar_t* p = bans; *p;) {
    wchar_t* e = nullptr;
    const uint64_t id = wcstoull(p, &e, 10);
    if (id) s_bans.push_back({ id, std::string() });
    if (!e || e == p) break;
    p = *e ? e + 1 : e;
  }
}

// ---------------- Steam: интерфейсы, свой ID, обратный вызов «к тебе стучатся» ----------------
// CCallbackBase из steam_api.h: { vtable, uint8 флаги, int номер }. Порядок методов сверен с колбэками самой игры
// (vtable 0x013E94AC): [0] Run(param, ioFailure, apiCall) ret 0x10, [1] Run(param) ret 4, [2] GetCallbackSizeBytes.
struct CbObj { const void* const* vt; uint8_t flags; int id; };
void OnSessionRequest(uint64_t from) {  // поток игры (SteamAPI_RunCallbacks); только s_mx
  Lk l;
  const int role = s_role;
  if (role == MP_HOST) {  // принимаем всех: забаненному и при закрытом лобби ответим отказом (KICK), а не молчанием
    MpLog("Steam: %llu wants into the lobby — accepting%s", static_cast<unsigned long long>(from),
          BannedLocked(from) ? " (banned: will be told so)" : !s_lobbyOpen ? " (lobby closed: will be told so)" : "");
    s_acceptQ.push_back(from);
  } else if (role == MP_CLIENT && from == s_peer && s_state != MPS_IDLE) {
    s_acceptQ.push_back(from);
  } else {
    MpLog("Steam: session request from %llu ignored (not hosting)", static_cast<unsigned long long>(from));
  }
}
std::atomic<bool> s_cbDead{ false };  // мод отцеплен, а снять обратный вызов не успели — он просто молчит
void __thiscall CbRunIo(CbObj*, void* p, bool, uint64_t) { if (p && !s_cbDead) OnSessionRequest(*static_cast<uint64_t*>(p)); }
void __thiscall CbRun(CbObj*, void* p) { if (p && !s_cbDead) OnSessionRequest(*static_cast<uint64_t*>(p)); }
int __thiscall CbSize(CbObj*) { return 8; }
const void* const kCbVt[3] = { reinterpret_cast<const void*>(&CbRunIo), reinterpret_cast<const void*>(&CbRun),
                               reinterpret_cast<const void*>(&CbSize) };
CbObj s_cb{ kCbVt, 0, kCbSessionRequest };
std::atomic<bool> s_cbReg{ false }, s_unregReq{ false }, s_unregDone{ false };

bool SteamFind() {  // сетевой поток
  if (s_net) return true;
  static ms_t nextTry = 0;
  if (NowMs() < nextTry) return false;
  nextTry = NowMs() + 2000;
  HMODULE dll = GetModuleHandleA("steam_api.dll");
  if (!dll) return false;
  auto get = [&](const char* n) -> addr {
    auto f = reinterpret_cast<SteamGetFn>(reinterpret_cast<void*>(GetProcAddress(dll, n)));
    return f ? f() : 0;
  };
  const addr net = get("SteamNetworking"), user = get("SteamUser"), fr = get("SteamFriends");
  if (!net || !user) return false;
  uint64_t me = 0;
  VSlot<GetSteamIdFn>(user, 2)(user, &me);
  // запасной путь и сверка: ID аккаунта из реестра (Steam пишет его для запущенной игры)
  DWORD acc = 0, sz = sizeof acc;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &acc, &sz);
  const uint64_t regId = acc ? kSteamId0 + acc : 0;
  if ((me >> 56) != 1 && regId) {
    MpLog("ISteamUser::GetSteamID gave %016llX — taking the registry account %llu", static_cast<unsigned long long>(me),
          static_cast<unsigned long long>(regId));
    me = regId;
  } else if (regId && regId != me) {
    MpLog("SteamID %llu differs from the registry %llu", static_cast<unsigned long long>(me), static_cast<unsigned long long>(regId));
  }
  const char* nm = fr ? VSlot<PersonaNameFn>(fr, 0)(fr) : nullptr;
  std::string name = nm ? std::string(nm).substr(0, 63) : std::string();
  VSlot<RelayFn>(net, NET_RELAY)(net, true);  // через серверы Valve, если напрямую не пробиться (NAT)
  s_steamDll = dll;  // обратный вызов зарегистрирует главный поток игры (OnRunCallbacks)
  s_net = net, s_user = user, s_friends = fr;
  Lk l;
  s_steamOk = true;
  s_me = me;
  s_name = name;
  MpLog("=== Steam ok — %s, SteamID %llu", name.c_str(), static_cast<unsigned long long>(me));
  return true;
}

bool Send(uint64_t to, uint8_t type, const void* body, size_t n, int sendType, int chan = kChan) {  // сетевой поток, вне s_mx
  if (!s_net || !to) return false;
  std::vector<uint8_t> b(sizeof(WHdr) + n);
  WHdr h{ kMagic, kVer, type, uint16_t(type == W_TRIG || type == W_FORGET ? n / sizeof(WTrig) : n) };
  memcpy(b.data(), &h, sizeof h);
  if (n) memcpy(b.data() + sizeof h, body, n);
  return VSlot<SendP2PFn>(s_net, NET_SEND)(s_net, to, b.data(), uint32_t(b.size()), sendType, chan);
}
// клиент -> хост: «я тут» (персонаж, облик, какие миссии есть, имя)
void SendHello(uint64_t to) {
  std::string name;
  uint8_t skin, avatar, chr;
  uint32_t missions;
  int nth;
  {
    Lk l;
    name = s_name.substr(0, 63), skin = uint8_t(s_skin), avatar = uint8_t(s_avatar), missions = s_myMissions, chr = uint8_t(s_myChar);
    nth = ++s_helloSent;
  }
  std::vector<uint8_t> b{ avatar, skin, chr };
  for (int i = 0; i < 4; i++) b.push_back(uint8_t(missions >> (8 * i)));
  b.insert(b.end(), name.begin(), name.end());
  const bool ok = Send(to, W_HELLO, b.data(), b.size(), P2P_RELIABLE);
  if (nth <= 3 || !ok) MpLog("HELLO #%d -> host %llu: %s", nth, static_cast<unsigned long long>(to), ok ? "sent" : "SendP2PPacket returned false");
}
// хост -> клиенты: состояние лобби — идёт ли игра, карта, все (первым — хост)
std::vector<uint8_t> LobbyPacketLocked() {
  std::vector<uint8_t> b;
  b.push_back(s_started ? 1 : 0);
  b.push_back(s_startGen);
  b.push_back(uint8_t(s_lobbyMode));
  b.push_back(uint8_t(s_lobbyDiff));
  b.push_back(uint8_t(s_lobbyChar));
  b.push_back(uint8_t(s_lobbyMission));
  b.push_back(s_lobbyRoles);
  const std::string map = s_lobbyMap.substr(0, 40);
  b.push_back(uint8_t(map.size()));
  b.insert(b.end(), map.begin(), map.end());
  const std::string lvl = s_lobbyLevel.substr(0, 120);
  b.push_back(uint8_t(lvl.size()));
  b.insert(b.end(), lvl.begin(), lvl.end());
  b.push_back(uint8_t(s_members.size() + 1));
  auto put = [&](uint64_t id, int avatar, int chr, const std::string& nm) {
    for (int i = 0; i < 8; i++) b.push_back(uint8_t(id >> (8 * i)));
    b.push_back(uint8_t(avatar));
    b.push_back(uint8_t(chr));
    const std::string n = nm.substr(0, 63);
    b.push_back(uint8_t(n.size()));
    b.insert(b.end(), n.begin(), n.end());
  };
  put(s_me, 0x80 | s_avatar, s_myChar, s_name);  // первый — хост (бит 0x80), за кого играет — младшие биты
  for (const Member& m : s_members) put(m.id, m.avatar, m.chr, m.name);
  return b;
}

// ---------------- сохранения миссий ----------------
// какие MSSVnn.ais (начало миссии n — так их грузит меню игры «Загрузить -> миссия») есть в папке Steam Cloud игры:
// <Steam>\userdata\<ID аккаунта>\214490\remote. Сетевой поток, без s_mx
uint32_t ScanMissions(uint64_t me) {
  wchar_t steam[MAX_PATH] = {};
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
  DWORD type = 0, sz = sizeof steam - sizeof(wchar_t);
  const LONG r = RegQueryValueExW(k, L"SteamPath", nullptr, &type, reinterpret_cast<BYTE*>(steam), &sz);
  RegCloseKey(k);
  if (r != ERROR_SUCCESS || type != REG_SZ || !steam[0]) return 0;
  uint32_t mask = 0;
  for (int n = 1; n <= kMpMissionCount; n++) {
    wchar_t f[MAX_PATH + 96];
    swprintf(f, sizeof f / sizeof f[0], L"%ls\\userdata\\%u\\214490\\remote\\MSSV%02d.ais", steam, unsigned(me & 0xFFFFFFFFu), n);
    if (GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES) mask |= 1u << n;
  }
  return mask;
}

// ---------------- какие роли есть в режиме ----------------
// по моделям персонажей уровня (ENV\PRODUCTION\<уровень>\RENDERABLE\MODELS_LEVEL.BIN: "..\CHARACTERS\ALIEN\model0.cs2"
// и т. п.): модель есть — персонаж на уровне бывает. Миссия идёт на нескольких уровнях (как в «Уровнях» мода) —
// объединяем. Сетевой поток, файлы — с кешем
const char* const kMissionLevels[kMpMissionCount] = {
  "BSP_Torrens", "HAB_Airport", "HAB_Airport|Tech_Hub", "Tech_Hub|Tech_Comms", "Tech_Hub|Tech_Comms",
  "Tech_Hub|SCI_Hub|SCI_HospitalUpper|SCI_HospitalLower", "SCI_AndroidLab", "SCI_Hub|HAB_ShoppingCentre",
  "BSP_LV426_Pt01|BSP_LV426_Pt02", "Tech_Hub|HAB_ShoppingCentre|Tech_RnD|Tech_RnD_HzdLab", "HAB_Airport|HAB_ShoppingCentre",
  "SCI_Hub|SCI_AndroidLab", "Tech_MuthrCore", "SCI_Hub|ENG_ReactorCore|ENG_Alien_Nest", "SCI_Hub|SCI_HospitalLower|Solace",
  "Tech_Comms", "HAB_Airport|HAB_CorporatePent", "ENG_TowPlatform", "BSP_Torrens" };
uint8_t LevelRoles(const std::string& level) {
  static std::unordered_map<std::string, uint8_t> cache;
  const auto it = cache.find(level);
  if (it != cache.end()) return it->second;
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string path(exe);
  path = path.substr(0, path.find_last_of("\\/") + 1) + "DATA\\ENV\\PRODUCTION\\" + level + "\\RENDERABLE\\MODELS_LEVEL.BIN";
  uint8_t mask = 0xF;  // нет файла — не ограничиваем
  if (FILE* f = std::fopen(path.c_str(), "rb")) {
    std::string d;
    char buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.append(buf, n);
    std::fclose(f);
    for (char& c : d) c = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    mask = 1u << AV_HUMAN;
    if (d.find("\\CHARACTERS\\ALIEN\\") != std::string::npos) mask |= 1u << AV_ALIEN;
    if (d.find("\\CHARACTERS\\ANDROID") != std::string::npos) mask |= 1u << AV_ANDROID;
    if (d.find("\\CHARACTERS\\FACEHUGGER\\") != std::string::npos) mask |= 1u << AV_FACEHUGGER;
  }
  cache[level] = mask;
  return mask;
}
uint8_t ModeRoles(int mode, int mission, const std::string& mapLevel) {
  std::string levels;
  if (mode == MPM_MISSION) levels = kMissionLevels[std::clamp(mission, 1, kMpMissionCount) - 1];
  else if (mode == MPM_CAMPAIGN) levels = kMissionLevels[0];
  else if (mode == MPM_RIPLEY) levels = "DLC\\BSPNostromo_Ripley";
  else if (mode == MPM_TWOTEAMS) levels = "DLC\\BSPNostromo_TwoTeams";
  else levels = _strnicmp(mapLevel.c_str(), "Production\\", 11) == 0 ? mapLevel.substr(11) : mapLevel;
  if (levels.empty()) return 0xF;
  uint8_t m = 1u << AV_HUMAN;
  size_t p = 0;
  while (p <= levels.size()) {
    const size_t q = levels.find('|', p);
    m |= LevelRoles(levels.substr(p, q == std::string::npos ? std::string::npos : q - p));
    if (q == std::string::npos) break;
    p = q + 1;
  }
  return m;
}

// ---------------- уровень ----------------
std::string ShortLevel(const std::string& n) { return _strnicmp(n.c_str(), "Production\\", 11) == 0 ? n.substr(11) : n; }
// клиент: хост нажал «Начать» (новый номер старта) — запустить тот же режим у себя (под s_mx). Запуск — родным путём
// игры (как пункты меню «DLC» / «Выживание»): уровень через менеджер уровней из главного меню вешал игру, а на уровне
// сущности не спавнились (01.10.2026)
void FollowLocked(ms_t now) {
  if (s_role != MP_CLIENT || s_state != MPS_CONNECTED || !s_started || s_startGen == s_myGen) return;
  s_myGen = s_startGen;
  s_startReq = s_lobbyMode, s_startDiff = s_lobbyDiff, s_startMap = s_lobbyMap, s_startChar = s_myChar, s_startAt = now;
  s_startMission = s_lobbyMission;
  s_avatarDone = false;
  Status("хост начал: %s — запускаю", "the host started: %s — launching", MpModeName(s_lobbyMode));
}
// свой уровень — раз в секунду (сетевой поток; чтение памяти игры — вне замка)
void MyLevelTick(ms_t now) {
  if (now - s_myLevelAt < 1000) return;
  s_myLevelAt = now;
  int st = 0;
  std::string lvl;
  Safe([&] {
    const addr g0 = rp(VA(va::GameGlobals));
    const addr game = g0 ? rp(g0 + 0x48) : 0;
    st = game ? rd<int>(game + 8) : 0;
  });
  lvl = ShortLevel(LevelCurrentName());
  const bool inLevel = (st == 4 || st == 5) && !lvl.empty() && _stricmp(lvl.c_str(), "Frontend") != 0;
  if (!inLevel) lvl.clear();
  Lk l;
  s_myGameState = st;
  if (lvl != s_myLevel) {
    MpLog("my level: %s (game state %d)", lvl.empty() ? "(main menu / loading)" : lvl.c_str(), st);
    s_myLevel = lvl;
    if (!lvl.empty()) s_startAt = 0;  // уровень пошёл — запуск удался
    s_levelSince = now;
    s_settled = false;
    // хост сам перешёл на другой уровень (сюжет): карта лобби — за ним, клиенты пойдут следом
    if (s_role == MP_HOST && s_lobbyOpen && !lvl.empty() && _stricmp(lvl.c_str(), s_lobbyLevel.c_str()) != 0) {
      MpLog("host is on %s", lvl.c_str());
      s_lobbyLevel = lvl;
      s_lobbyDirty = true;
    }
  }
  // запустили, а через 20 с всё ещё в меню — режим недоступен (нет DLC) или игра не приняла запуск
  if (s_startAt && now - s_startAt > 20000 && s_myLevel.empty() && st != 6) {
    s_startAt = 0;
    Status("режим не запустился: %s — есть ли у тебя это DLC?", "the mode didn't start: %s — do you own this DLC?", MpModeName(s_lobbyMode));
  }
  // устоялся ли уровень: стоим на нём kSettleMs, игра не грузится, у клиента — тот же уровень, что у хоста
  bool settled = !s_myLevel.empty() && st != 6 && now - s_levelSince >= kSettleMs;
  if (s_role == MP_CLIENT) settled = settled && _stricmp(s_myLevel.c_str(), s_lobbyLevel.c_str()) == 0;
  if (settled != s_settled) {
    s_settled = settled;
    MpLog("world sync %s (level %s)", settled ? "ON" : "off", s_myLevel.empty() ? "-" : s_myLevel.c_str());
  }
  FollowLocked(now);
}

// ---------------- журнал ----------------
void JournalAdd(const WTrig& w, bool remote, const std::string& kind) {  // под s_mx
  const ms_t now = NowMs();
  for (size_t i = 0; i < s_jr.size(); i++) {
    MpTrig& t = s_jr[i].t;
    if (t.ent == w.ent && t.inst == w.inst && t.out == w.out && t.remote == remote) {
      JEntry e = s_jr[i];
      e.t.n++, e.t.at = uint64_t(now), e.t.delay = w.delay;
      s_jr.erase(s_jr.begin() + ptrdiff_t(i));
      s_jr.insert(s_jr.begin(), e);
      return;
    }
  }
  JEntry e{};
  e.t.ent = w.ent, e.t.inst = w.inst, e.t.out = w.out, e.t.delay = w.delay;
  e.t.n = 1, e.t.at = uint64_t(now), e.t.remote = remote, e.t.kind = kind;
  s_jr.insert(s_jr.begin(), e);
  if (s_jr.size() > kJournal) s_jr.pop_back();
}
void QueueReplay(const WTrig& w) {  // под s_mx
  if (s_replayQ.size() >= kReplayCap) return;
  s_replayQ.push_back({ w });
  s_replayN = int(s_replayQ.size());
}

// тип сущности: RTTI у итоговых vtable сущностей в этой сборке нет (только у баз) — показываем VA vtable, она общая у
// сущностей одного типа (имена — по Ghidra)
std::string KindOf(addr vt) {
  char b[24];
  std::snprintf(b, sizeof b, "vt %08X", unsigned(InGame(vt) ? ToVA(vt) : vt));
  return b;
}

// интерфейс сущности (this для CallTriggers): первый подобъект после +0 с диспетчером связей в слоте +0x394
addr IfaceOf(addr ent) {  // читает память игры — под Safe слушателя
  const addr disp = VA(va::LinkDispatch), dispComp = VA(va::LinkDispatchComposite);
  for (addr off = 4; off <= 0x40; off += 4) {
    const addr vt = rp(ent + off);
    if (!InGame(vt)) continue;
    const addr f = rp(vt + 0x394);
    if (f == disp || f == dispComp) return ent + off;
  }
  return 0;
}

// ---------------- хуки (поток игры) ----------------
void ReleaseRef(addr h);
void OnTrigEnter(Invocation& inv) {
  const addr ecx = inv.ecx, ti = rp(inv.arg[2]);
  if (ti) s_lastTI = ti, s_lastTIAt = NowMs();
  const int role = s_role;
  const bool host = role == MP_HOST && s_lobbyOpen && s_started && s_memberCount > 0 && s_settled;
  const bool sup = role == MP_CLIENT && s_state == MPS_CONNECTED && s_started && s_suppress && s_settled;
  const bool tap = s_tap;
  if (!host && !sup && !tap) return;
  if (Interceptor::ActiveDepth(VA(va::CallTriggers)) > 0) return;  // каскад внутри другого выхода — повторится сам
  const addr hh = rp(inv.arg[0]);
  const addr ent = hh ? rp(hh + 0xc) : 0;
  if (!ent) return;
  uint32_t eh[2] = {};
  Fn<GetEntityHandleFn>(va::GetEntityHandle)(ent, eh);
  double delay = 0;
  memcpy(&delay, &inv.arg[3], sizeof delay);
  const WTrig w{ eh[0], eh[1], rd<uint32_t>(inv.arg[1]), float(delay) };
  const ms_t now = NowMs();
  if (sup) {
    bool kill;
    {
      Lk l;
      kill = s_hostKeys.count({ w.ent, w.inst, w.out }) != 0;
      if (kill) s_suppressed++;
    }
    if (kill) {
      inv.skip = true, inv.skipPop = 0x14;  // thiscall, 3 указателя + double
      return;
    }
  }
  const std::string kind = tap ? KindOf(rp(ent)) : std::string();
  const addr iface = IfaceOf(ent);
  bool verify;
  {
    Lk l;
    verify = s_verifyLeft > 0 && s_verifyLeft--;
  }
  if (verify) {  // ручка {GUID, ID экземпляра} -> find_entity -> та же сущность?
    addr p = 0;
    if (const addr em = rp(VA(va::EntityManagerPtr))) Fn<FindEntityFn>(va::FindEntity)(em, &p, eh);
    const addr back = p ? rp(p + 0xc) : 0;
    if (p) ReleaseRef(p);
    Log("mp: tap %s %08X:%08X out %08X delay %.2f (ent %08X, +0xc %08X, TriggerInfo %08X%s) -> find_entity %s", KindOf(rp(ent)).c_str(),
        unsigned(w.ent), unsigned(w.inst), unsigned(w.out), double(w.delay), unsigned(ent), unsigned(rd<uint32_t>(ent + 0xc)), unsigned(ti),
        ti - inv.cpu->esp < 0x100000 ? " ON STACK" : "", back == ent ? "same entity OK" : back ? "ANOTHER entity" : "not found");
  }
  const addr entVt = rp(ent);  // память игры — до замка: ошибка доступа под замком оставила бы его закрытым
  if (ecx != iface && s_ecxOdd.fetch_add(1) < 3)  // повтор ищет this так же — если где-то не совпало, хотим знать
    Log("mp: CallTriggers this %08X, but the interface found is %08X (entity %08X, %s)", unsigned(ecx), unsigned(iface), unsigned(ent),
        KindOf(entVt).c_str());
  Lk l;
  s_captured++;
  const K3 key{ w.ent, w.inst, w.out };
  bool noisy = w.out == kOutOnThink || w.out == kOutRefreshed;
  if (!noisy) {
    auto it = s_rate.find(key);
    if (it == s_rate.end() && s_rate.size() < 131072) it = s_rate.emplace(key, Rate{ now, now, now, 0, 0, 0, false }).first;
    if (it != s_rate.end()) {
      Rate& r = it->second;
      if (now - r.t0 > 1000) r.t0 = now, r.n = 0;
      if (now - r.t10 > 10000) r.t10 = now, r.n10 = 0;
      if (now - r.t60 > 60000) r.t60 = now, r.n60 = 0;
      r.n++, r.n10++, r.n60++;
      if ((r.n > kNoisyPerSec || r.n10 > kNoisyPer10s || r.n60 > kNoisyPerMin) && !r.noisy) {
        r.noisy = true, s_noisyKeys++;
        if (s_sentKeys.erase(key)) s_forgetQ.push_back(w);  // клиенты уже гасят его — пусть снова считают сами
      }
      noisy = r.noisy;
    }
  }
  if (noisy) s_filtered++;
  if (tap) {
    if (!noisy) JournalAdd(w, false, kind);
    const uint64_t sk = uint64_t(ToVA(entVt)) << 32 | w.out;
    if (s_stat.size() < 8192 || s_stat.count(sk)) s_stat[sk]++;
  }
  if (host && !noisy && s_sendQ.size() < kSendCap) s_sendQ.push_back({ w, now });
}
void OnTrigLeave(Invocation&) {}  // только чтобы у вызовов был кадр (ActiveDepth)

// освободить ссылку MemoryPtr<Entity> от find_entity: как игра, но без последней (её снятие удаляет объект — не наше дело)
void ReleaseRef(addr h) {
  volatile LONG* rc = reinterpret_cast<volatile LONG*>(h + 4);
  for (LONG v = *rc; v > 1; v = *rc)
    if (InterlockedCompareExchange(rc, v - 1, v) == v) return;
}

// игра началась и уровень загружен — отдать игроку (клиенту или хосту) выбранного персонажа (поток игры, общий замок)
void AvatarTick() {
  const bool inGame = s_role == MP_CLIENT ? s_state == MPS_CONNECTED : s_role == MP_HOST && s_lobbyOpen;
  if (!inGame || !s_started || s_avatarDone || !player) return;
  const ms_t now = NowMs();
  if (now - s_avatarTryAt < 1500) return;
  s_avatarTryAt = now;
  int av;
  {
    Lk l;
    av = s_avatar;
    if (s_myLevel.empty()) return;  // ещё грузимся
    if (s_role == MP_CLIENT && _stricmp(s_myLevel.c_str(), s_lobbyLevel.c_str()) != 0) return;  // ещё не на карте хоста
  }
  if (av == AV_HUMAN) {  // свой человек: если мод держал кого-то — вернуть
    if (hugger) ActHugger(0);
    else if (camMode != 0) ActCamera(3);
    s_avatarDone = true;
    MpLog("%s: playing as the human", RoleStr(s_role));
    return;
  }
  if (av == AV_ALIEN) {
    if (camMode != 0 && !hugger) {
      s_avatarDone = true;
      MpLog("%s: playing as the Alien", RoleStr(s_role));
      return;
    }
    if (npc) {
      if (hugger) ActHugger(0);
      ActCamera(2);  // камера за спиной Чужого (CamDo — в потоке игры)
      return;
    }
  } else {  // андроид или лицехват — ближайший свободный (того, кто изображает другого игрока, не берём)
    const bool wantHugger = av == AV_FACEHUGGER;
    if (hugger && IsHugger(hugger) == wantHugger) {
      s_avatarDone = true;
      MpLog("%s: playing as %s", RoleStr(s_role), wantHugger ? "a facehugger" : "an android");
      return;
    }
    for (const HugRow& r : HuggerList())
      if (IsHugger(r.e) == wantHugger && !PupIsBody(r.e)) {
        ActHugger(r.e);
        return;
      }
  }
  if (now - s_avatarSaidAt > 20000) {  // нужного персонажа на уровне (пока) нет
    s_avatarSaidAt = now;
    Lk l;
    Status("на уровне пока нет: %s — жду, управление отдам, как появится", "not on this level yet: %s — waiting, you get control once it appears",
           MpAvatarName(av));
  }
}

void OnGameTick(Invocation&) {  // LocoUpdate: поток игры — персонаж клиента, повтор выходов
  AvatarTick();
  if (s_replayN.load() <= 0) return;
  if (NowMs() - s_lastTIAt.load() > 500) return;  // свежего TriggerInfo нет (уровень грузится) — подождём
  std::vector<Replay> batch;
  {
    Lk l;
    while (!s_replayQ.empty() && batch.size() < 64) {
      batch.push_back(s_replayQ.front());
      s_replayQ.pop_front();
    }
    s_replayN = int(s_replayQ.size());
  }
  const addr em = rp(VA(va::EntityManagerPtr));
  uint32_t done = 0, miss = 0;
  const Replay* firstMiss = nullptr;
  for (const Replay& r : batch) {
    const addr ti = s_lastTI.load();  // всегда свежий: сохранённый в журнале мог уйти обратно в пул
    if (!em || !ti) {
      miss++;
      continue;
    }
    const uint32_t eh[2] = { r.w.ent, r.w.inst };
    addr p = 0;
    Fn<FindEntityFn>(va::FindEntity)(em, &p, eh);
    const addr ent = p ? rp(p + 0xc) : 0, iface = ent ? IfaceOf(ent) : 0;
    if (!iface) {
      if (p) ReleaseRef(p);
      if (!firstMiss) firstMiss = &r;
      miss++;
      continue;
    }
    addr pp = p, tt = ti;
    uint32_t out = r.w.out;
    Fn<CallTriggersFn>(va::CallTriggers)(iface, addr(&pp), addr(&out), addr(&tt), double(r.w.delay));
    ReleaseRef(p);
    done++;
  }
  Lk l;
  s_replayed += done;
  if (firstMiss && !s_missed)
    Log("mp: replay — entity %08X:%08X not found (another level?)", unsigned(firstMiss->w.ent), unsigned(firstMiss->w.inst));
  s_missed += miss;
}

// ---------------- сеть: приём ----------------
void SelfTest();
// хост: пакет от игрока (вход, «я тут», пинг, выход)
void HostHandle(uint64_t from, uint8_t type, const uint8_t* body, uint32_t bn, ms_t now, uint32_t& pongT, bool& replyPong) {
  if (BannedLocked(from)) {
    s_kickQ.push_back({ from, KICK_BANNED });
    return;
  }
  if (!s_lobbyOpen) {
    if (type == W_HELLO) s_kickQ.push_back({ from, KICK_CLOSED });
    return;
  }
  Member* m = MemberLocked(from);
  if (type == W_HELLO && bn >= 7) {
    const std::string name(reinterpret_cast<const char*>(body + 7), std::min<uint32_t>(bn - 7, 63));
    const int av = body[0] < AV_COUNT ? body[0] : AV_HUMAN;
    const int chr = body[2] >= 1 && body[2] <= kMpCharCount ? body[2] : 1;
    uint32_t missions = 0;
    memcpy(&missions, body + 3, 4);
    if (!m) {
      if (int(s_members.size()) >= kMpMaxClients) {
        s_kickQ.push_back({ from, KICK_FULL });
        MpLog("lobby full — %s (%llu) turned away", name.c_str(), static_cast<unsigned long long>(from));
        return;
      }
      s_members.push_back({ from, name, av, body[1], chr, missions, now, now, -1 });
      s_memberCount = int(s_members.size());
      m = &s_members.back();
      MpLog("JOINED: %s (%llu) as %s", name.c_str(), static_cast<unsigned long long>(from), MpAvatarName(av));
      Status("%s зашёл в лобби", "%s joined the lobby", name.c_str());
      s_lobbyDirty = true;
    } else if (m->avatar != av || m->name != name || m->missions != missions || m->chr != chr) {
      m->avatar = av, m->name = name, m->skin = body[1], m->missions = missions, m->chr = chr;
      s_lobbyDirty = true;
    }
    for (MpBan& b : s_bans)
      if (b.id == from) b.name = name;
  }
  if (!m) return;  // не вошёл (ещё не было HELLO) — остальное не слушаем
  m->lastRecv = now;
  if (type == W_PING && bn >= 4) memcpy(&pongT, body, 4), replyPong = true;
  if (type == W_PONG && bn >= 4) {
    uint32_t t;
    memcpy(&t, body, 4);
    m->pingMs = int(uint32_t(now) - t);
  }
  if (type == W_ACT && bn >= 1 && body[0] == ACT_ATTACK && m->avatar == AV_ALIEN) {
    MpLog("ATTACK by %s's Alien", m->name.c_str());
    PupRemoteAttack(from);
    s_actRelay.push_back(from);  // остальным клиентам: его Чужой у них — тоже кукла
  }
  if (type == W_BYE) {
    PupForget(from);
    MpLog("LEFT: %s (%llu)", m->name.c_str(), static_cast<unsigned long long>(from));
    Status("%s вышел из лобби", "%s left the lobby", m->name.c_str());
    s_members.erase(s_members.begin() + (m - s_members.data()));
    s_memberCount = int(s_members.size());
    s_lobbyDirty = true;
  }
}
// клиент: пакет от хоста
void ClientHandle(uint8_t type, const uint8_t* body, uint32_t bn, uint32_t n, ms_t now, uint32_t& pongT, bool& replyPong) {
  s_lastRecv = now;
  switch (type) {
    case W_LOBBY: {
      if (bn < 3) break;
      uint32_t p = 0;
      if (bn < 8) break;
      const bool started = body[p++] != 0;
      const uint8_t gen = body[p++];
      const int mode = body[p++] < kMpModeCount ? body[p - 1] : 0;
      const int diff = body[p++] < kMpDiffCount ? body[p - 1] : 2;
      const int chr = body[p] >= 1 && body[p] <= kMpCharCount ? body[p] : 1;
      p++;
      const int mission = body[p] >= 1 && body[p] <= kMpMissionCount ? body[p] : 1;
      p++;
      const uint8_t roles = body[p++] | uint8_t(1u << AV_HUMAN);
      const uint32_t ml = body[p++];
      if (p + ml + 2 > bn) break;
      const std::string map(reinterpret_cast<const char*>(body + p), ml);
      p += ml;
      const uint32_t ll = body[p++];
      if (p + ll + 1 > bn) break;
      const std::string lvl(reinterpret_cast<const char*>(body + p), ll);
      p += ll;
      const uint32_t cnt = body[p++];
      std::vector<MpMember> view;
      for (uint32_t i = 0; i < cnt && p + 11 <= bn; i++) {
        MpMember mm;
        for (int k = 0; k < 8; k++) mm.id |= uint64_t(body[p + k]) << (8 * k);
        p += 8;
        mm.host = (body[p] & 0x80) != 0;
        mm.avatar = body[p++] & 0x7F;
        mm.chr = body[p] >= 1 && body[p] <= kMpCharCount ? body[p] : 1;
        p++;
        const uint32_t nl = body[p++];
        if (p + nl > bn) break;
        mm.name.assign(reinterpret_cast<const char*>(body + p), nl);
        p += nl;
        view.push_back(mm);
      }
      const bool first = s_state != MPS_CONNECTED;
      s_state = MPS_CONNECTED;
      if (!view.empty() && view[0].host) s_hostName = view[0].name;
      s_lobbyView.swap(view);
      if (first) {
        MpLog("IN THE LOBBY of %s (%llu): %u players, map %s, %s", s_hostName.c_str(), static_cast<unsigned long long>(s_peer),
              unsigned(s_lobbyView.size()), lvl.empty() ? "-" : lvl.c_str(), started ? "game running" : "waiting");
        s_hostKeys.clear();
        s_pingMs = -1;
      }
      s_lobbyMode = mode, s_lobbyDiff = diff, s_lobbyChar = chr, s_lobbyMap = map, s_lobbyMission = mission, s_lobbyRoles = roles;
      if (first) s_myGen = started ? uint8_t(gen - 1) : gen;  // вошли в идущую игру — запустим её и мы
      s_lobbyLevel = lvl;
      if (started != s_started || gen != s_startGen || first) {
        s_started = started;
        s_startGen = gen;
        if (started) {
          MpLog("game started: %s (%s), host level %s", MpModeName(mode), MpDiffName(diff), lvl.empty() ? "-" : lvl.c_str());
          s_avatarDone = false;
        } else {
          Status("в лобби у %s — ждём, когда хост начнёт", "in %s's lobby — waiting for the host to start", s_hostName.c_str());
        }
      }
      FollowLocked(now);
      break;
    }
    case W_KICK: {
      const int why = bn >= 1 ? body[0] : KICK_KICKED;
      PupClear("kicked");
      s_state = MPS_IDLE;
      s_started = false;
      s_lobbyView.clear();
      s_pingMs = -1;
      MpLog("kicked by the host: reason %d", why);
      if (why == KICK_BANNED) Status("хост тебя забанил", "the host banned you");
      else if (why == KICK_CLOSED) Status("хост закрыл лобби (или ещё не открыл)", "the host closed the lobby (or hasn't opened it yet)");
      else if (why == KICK_FULL) Status("лобби заполнено", "the lobby is full");
      else Status("хост тебя выгнал", "the host kicked you");
      break;
    }
    case W_PING:
      if (bn >= 4) memcpy(&pongT, body, 4), replyPong = true;
      break;
    case W_PONG:
      if (bn >= 4) {
        uint32_t t;
        memcpy(&t, body, 4);
        s_pingMs = int(uint32_t(now) - t);
      }
      break;
    case W_TRIG: {
      const size_t k = std::min<size_t>(n, bn / sizeof(WTrig));
      s_recv += uint32_t(k);
      if (!s_settled) break;  // мы ещё грузимся / не на уровне хоста — не повторяем (выбрасываем)
      for (size_t i = 0; i < k; i++) {
        WTrig w;
        memcpy(&w, body + i * sizeof(WTrig), sizeof w);
        if (s_hostKeys.size() < 200000) s_hostKeys.insert({ w.ent, w.inst, w.out });
        QueueReplay(w);
        if (s_tap) JournalAdd(w, true, std::string());
      }
      break;
    }
    case W_ACT: {  // удар Чужого другого игрока: [что][чей u64]
      if (bn < 9 || body[0] != ACT_ATTACK) break;
      uint64_t who = 0;
      memcpy(&who, body + 1, 8);
      if (who && who != s_me) {
        MpLog("ATTACK by the Alien of %llu", static_cast<unsigned long long>(who));
        PupRemoteAttack(who);
      }
      break;
    }
    case W_FORGET: {  // хост понял, что этот ключ шумный: снова считаем его сами
      const size_t k = std::min<size_t>(n, bn / sizeof(WTrig));
      for (size_t i = 0; i < k; i++) {
        WTrig w;
        memcpy(&w, body + i * sizeof(WTrig), sizeof w);
        s_hostKeys.erase({ w.ent, w.inst, w.out });
      }
      break;
    }
    default: break;
  }
}
void Handle(uint64_t from, const uint8_t* d, uint32_t n, int chan) {  // сетевой поток
  if (n < sizeof(WHdr)) return;
  WHdr h;
  memcpy(&h, d, sizeof h);
  if (h.magic != kMagic) return;
  const uint8_t* body = d + sizeof h;
  const uint32_t bn = n - uint32_t(sizeof h);
  const ms_t now = NowMs();
  bool replyPong = false;
  uint32_t pongT = 0;
  bool state = false;  // тело игрока: разобрать и переслать — уже без s_mx
  uint16_t lvl = 0;
  uint64_t me = 0;
  std::vector<uint64_t> relay;
  {
    Lk l;
    if (h.ver != kVer) {
      if (s_seenFrom.insert(from).second)
        MpLog("packet from %llu with protocol %d (mine is %d) — ignored", static_cast<unsigned long long>(from), int(h.ver), int(kVer));
      if (from == s_peer && s_role == MP_CLIENT)
        Status("у хоста другая версия мультиплеера мода — обновитесь оба", "the host has another mod MP version — update both");
      return;
    }
    if (chan == kSelfChan) {
      if (h.type == W_SELF && bn >= 4 && from == s_me) {
        uint32_t nonce;
        memcpy(&nonce, body, 4);
        if (nonce == s_selfNonce && s_selfTest == 1) {
          s_selfTest = 2;
          Status("самопроверка: пакет самому себе дошёл — вызовы Steam работают", "self-test: the packet to yourself arrived — Steam calls work");
        }
      }
      return;
    }
    if (s_seenFrom.insert(from).second)
      MpLog("first packet from %llu: type %d, %u bytes", static_cast<unsigned long long>(from), int(h.type), unsigned(n));
    if (h.type == W_STATE) {
      if (s_role == MP_HOST && s_lobbyOpen && !BannedLocked(from)) {
        if (Member* m = MemberLocked(from)) {
          m->lastRecv = now;
          state = true;
          for (const Member& o : s_members)
            if (o.id != from) relay.push_back(o.id);
        }
      } else if (s_role == MP_CLIENT && from == s_peer && s_state == MPS_CONNECTED) {
        s_lastRecv = now;
        state = true;
      }
      lvl = PupLevelHash(s_myLevel), me = s_me;
    } else if (s_role == MP_HOST) {
      HostHandle(from, h.type, body, bn, now, pongT, replyPong);
    } else if (s_role == MP_CLIENT && from == s_peer && s_state != MPS_IDLE) {
      ClientHandle(h.type, body, bn, h.n, now, pongT, replyPong);
    }
  }
  if (state) {
    PupOnState(body, bn, me, lvl);
    for (uint64_t id : relay) Send(id, W_STATE, body, bn, P2P_UNRELIABLE);
  }
  if (replyPong) Send(from, W_PONG, &pongT, 4, P2P_UNRELIABLE);
}

// ---------------- сеть: такт ----------------
void FriendsTick(ms_t now) {  // друзья, которые сейчас в Alien: Isolation (раз в 5 с)
  {
    Lk l;
    if (now - s_lastFriends < 5000) return;
    s_lastFriends = now;
  }
  if (!s_friends) return;
  std::vector<MpFriend> fl;
  const int cnt = std::min(VSlot<FriendCountFn>(s_friends, FR_COUNT)(s_friends, kFriendImmediate), 2000);
  for (int i = 0; i < cnt; i++) {
    uint64_t id = 0;
    VSlot<FriendByIndexFn>(s_friends, FR_BY_INDEX)(s_friends, &id, i, kFriendImmediate);
    uint8_t gi[32] = {};
    if (!id || !VSlot<FriendGameFn>(s_friends, FR_GAME)(s_friends, id, gi)) continue;
    uint64_t game = 0;
    memcpy(&game, gi, 8);
    if ((game & 0xFFFFFF) != kAppId) continue;
    const char* nm = VSlot<FriendNameFn>(s_friends, FR_NAME)(s_friends, id);
    fl.push_back({ id, nm ? std::string(nm).substr(0, 63) : std::string() });
  }
  static bool once = false;
  if (!once) once = true, MpLog("Steam friends: %d, in Alien: Isolation right now: %u", cnt, unsigned(fl.size()));
  Lk l;
  if (fl.size() != s_friendList.size()) MpLog("friends in Alien: Isolation now: %u", unsigned(fl.size()));
  s_friendList.swap(fl);
}
void LogSession(uint64_t who) {
  P2PState st{};
  if (VSlot<SessionStateFn>(s_net, NET_STATE)(s_net, who, &st))
    MpLog("P2P session with %llu: active %d, connecting %d, error %d (%s), relay %d, queued %d bytes / %d packets",
          static_cast<unsigned long long>(who), int(st.active), int(st.connecting), int(st.error), P2PError(st.error), int(st.relay),
          int(st.bytesQueued), int(st.packetsQueued));
  else
    MpLog("P2P session with %llu: none yet", static_cast<unsigned long long>(who));
}

// Сторож зависаний. SteamAPI_RunCallbacks игра зовёт из потока интерфейса (его цикл задач 0x0083b260 спит на условной
// переменной, когда работы нет) каждый кадр. Не было вызова 25 с — игра встала (или очень долго грузится): снимаем стеки
// ВСЕХ потоков игры по одному (на миг приостановить, скопировать стек в готовый буфер — пока поток стоит, ни выделений
// памяти, ни замков: он может держать кучу — отпустить) и пишем в AlienModUI.mp.log адреса возврата в коде игры (VA).
// Главный поток (самый ранний по времени создания) помечен «main». Один раз на каждую остановку.
void HangWatch(ms_t now) {
  static bool dumped = false;
  const ms_t at = s_mainTickAt.load();
  if (!at) return;
  if (now - at < 25000) {
    dumped = false;
    return;
  }
  if (dumped) return;
  dumped = true;
  std::vector<DWORD> tids;
  if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
      if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId()) tids.push_back(te.th32ThreadID);
    CloseHandle(snap);
  }
  // главный поток — самый ранний
  DWORD mainTid = 0;
  ULONGLONG best = ~0ull;
  for (DWORD t : tids)
    if (HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, t)) {
      FILETIME c, e, k, u;
      if (GetThreadTimes(th, &c, &e, &k, &u)) {
        const ULONGLONG v = (ULONGLONG(c.dwHighDateTime) << 32) | c.dwLowDateTime;
        if (v < best) best = v, mainTid = t;
      }
      CloseHandle(th);
    }
  MpLog("HANG? nothing ticked for %lld s (last tick on thread %lu, main thread %lu) — stacks of %u threads with game code:",
        static_cast<long long>((now - at) / 1000), s_mainTid.load(), mainTid, unsigned(tids.size()));
  static uint32_t stack[768];
  for (DWORD t : tids) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    size_t words = 0;
    bool got = false;
    if (HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t)) {
      if (SuspendThread(th) != DWORD(-1)) {
        if (GetThreadContext(th, &ctx)) {
          got = true;
          for (; words < 768; words++)
            if (!SafeRead(addr(ctx.Esp + words * 4), stack[words])) break;
        }
        ResumeThread(th);
      }
      CloseHandle(th);
    }
    if (!got) continue;
    char line[400];
    int n = std::snprintf(line, sizeof line, "  %s%lu eip %08X:", t == mainTid ? "main " : "", t,
                          InGame(ctx.Eip) ? unsigned(ToVA(ctx.Eip)) : unsigned(ctx.Eip));
    int k = 0;
    for (size_t i = 0; i < words && k < 16 && n < int(sizeof line) - 12; i++)
      if (InGame(stack[i])) n += std::snprintf(line + n, sizeof line - size_t(n), " %08X", unsigned(ToVA(stack[i]))), k++;
    if (k >= 2 || t == mainTid) MpLog("%s", line);
  }
}

void NetTick() {
  if (!SteamFind()) {
    Lk l;
    s_steamOk = false;
    return;
  }
  // 1) входящие
  std::vector<uint8_t> buf;
  for (int chan : { kChan, kSelfChan }) {
    for (int k = 0; k < 512; k++) {
      uint32_t sz = 0;
      if (!VSlot<AvailP2PFn>(s_net, NET_AVAIL)(s_net, &sz, chan) || !sz) break;
      buf.resize(std::max<uint32_t>(sz, 16));
      uint32_t got = 0;
      uint64_t from = 0;
      if (!VSlot<ReadP2PFn>(s_net, NET_READ)(s_net, buf.data(), uint32_t(buf.size()), &got, &from, chan)) break;
      Handle(from, buf.data(), got, chan);
    }
  }
  const ms_t now = NowMs();
  HangWatch(now);
  MyLevelTick(now);
  FriendsTick(now);
  // 2) что отправить — собираем под замком, шлём без него
  std::vector<uint64_t> accept, to;  // to — кому слать (хост: все игроки; клиент: хост)
  std::vector<std::pair<uint64_t, int>> kicks;
  std::vector<uint8_t> lobby;
  uint64_t bye = 0, stateLog = 0, me = 0;
  bool hello = false, ping = false, bodies = false, body = false, act = false;
  std::vector<uint64_t> actRelay;
  std::string ident;
  bool identWeak = false;
  int myAvatar = AV_HUMAN;
  uint16_t lvl = 0;
  std::vector<WTrig> trig, forget;
  {
    Lk l;
    accept.swap(s_acceptQ);
    kicks.swap(s_kickQ);
    bye = s_byeTo, s_byeTo = 0;
    const int role = s_role;
    if (role == MP_HOST && s_lobbyOpen) {
      // пропавшие
      for (size_t i = 0; i < s_members.size();) {
        if (now - s_members[i].lastRecv > kSilentDrop) {
          MpLog("DROPPED (silent %lld s): %s (%llu)", static_cast<long long>((now - s_members[i].lastRecv) / 1000), s_members[i].name.c_str(),
                static_cast<unsigned long long>(s_members[i].id));
          Status("%s пропал (нет связи)", "%s dropped (no connection)", s_members[i].name.c_str());
          PupForget(s_members[i].id);
          s_members.erase(s_members.begin() + ptrdiff_t(i));
          s_lobbyDirty = true;
        } else {
          i++;
        }
      }
      s_memberCount = int(s_members.size());
      for (const Member& m : s_members) to.push_back(m.id);
      if (s_lobbyDirty || now - s_lobbySentAt >= 2000) lobby = LobbyPacketLocked(), s_lobbyDirty = false, s_lobbySentAt = now;
      if (now - s_pingAt >= 1000) ping = true, s_pingAt = now;
      if (s_started && !to.empty()) {  // выходы — только отлежавшиеся kSendDelayMs и не ставшие шумными
        size_t i = 0;
        for (; i < s_sendQ.size() && trig.size() < kTrigPerPacket * 8; i++) {
          const QItem& q = s_sendQ[i];
          if (now - q.at < kSendDelayMs) break;
          const K3 key{ q.w.ent, q.w.inst, q.w.out };
          auto it = s_rate.find(key);
          if (it != s_rate.end() && it->second.noisy) continue;
          trig.push_back(q.w);
          if (s_sentKeys.size() < 200000) s_sentKeys.insert(key);
        }
        s_sendQ.erase(s_sendQ.begin(), s_sendQ.begin() + ptrdiff_t(i));
        forget.swap(s_forgetQ);
      } else {
        s_sendQ.clear(), s_forgetQ.clear();
      }
    } else {
      s_sendQ.clear(), s_forgetQ.clear();
    }
    if (role == MP_CLIENT && s_state != MPS_IDLE && s_peer) {
      to.push_back(s_peer);
      const bool conn = s_state == MPS_CONNECTED;
      if (s_helloNow || now - s_lastHello >= (conn ? 2000 : 1000)) hello = true, s_helloNow = false, s_lastHello = now;
      if (conn && now - s_pingAt >= 1000) ping = true, s_pingAt = now;
      if (conn && now - s_lastRecv > kSilentDrop) {
        s_state = MPS_LOST;
        s_pingMs = -1;
        Status("связь с хостом пропала — переподключаюсь", "lost the host — reconnecting");
      }
      if (s_state == MPS_CONNECTING && s_connectAt && now - s_connectAt > 30000) {
        s_connectAt = 0;
        Status("хост не отвечает 30 с: он создал лобби (мод 0.14+)? Адрес верный? Подробности — в AlienModUI.mp.log",
               "no answer from the host for 30 s: did they create a lobby (mod 0.14+)? Is the address right? Details are in AlienModUI.mp.log");
      }
      if (s_state != MPS_CONNECTED && now - s_lastStateLog >= 5000) stateLog = s_peer, s_lastStateLog = now;
    }
    // тела игроков: игра идёт, свой уровень загружен (у клиента — тот же, что у хоста) — своё тело 20 раз в секунду.
    // «Зеркало» (проверка куклы) — и без лобби
    const bool onLevel = !s_myLevel.empty() && (s_myGameState == 4 || s_myGameState == 5) && now - s_levelSince >= kStateAfterLoad;
    bodies = onLevel && s_started &&
             (role == MP_HOST ? s_lobbyOpen && !s_members.empty()
                              : role == MP_CLIENT && s_state == MPS_CONNECTED && _stricmp(s_myLevel.c_str(), s_lobbyLevel.c_str()) == 0);
    myAvatar = s_avatar;  // и хост выбирает, кем играть
    const bool ghost = onLevel && PupGhostOn();
    PupSetActive(bodies || ghost, myAvatar);
    if ((bodies || ghost) && now - s_bodyAt >= kStateEvery) body = true, s_bodyAt = now;
    lvl = PupLevelHash(s_myLevel), me = s_me;
    // я за Чужого нажал удар — всем (клиент — хосту, хост — клиентам): мой Чужой у них — кукла, она ударит там
    const uint32_t atk = g_localAttackSeq.load();
    if (bodies && s_avatar == AV_ALIEN && atk != s_atkSeen) act = true;
    s_atkSeen = atk;
    actRelay.swap(s_actRelay);
    // кем играю (для куклы у других): кампания — Аманда, «Последний выживший» — Эллен, выживание — выбранный персонаж;
    // «Экипаж» — по своей модели (JACKET_DALLAS...), не узнали — Эллен
    ident = s_lobbyMode == MPM_SURVIVOR                                     ? kCharIdent[std::clamp(s_myChar, 1, kMpCharCount) - 1]
            : s_lobbyMode == MPM_CAMPAIGN || s_lobbyMode == MPM_MISSION ? "AMANDA"
                                                                        : "RIPLEY";
    identWeak = s_lobbyMode == MPM_TWOTEAMS;
    if (ident + (identWeak ? "?" : "") == s_identSent) ident.clear();
    else s_identSent = ident + (identWeak ? "?" : "");
    if (s_selfTest == 1 && now - s_selfAt > 3000) {
      s_selfTest = 3;
      Status("самопроверка: пакет самому себе не вернулся за 3 с (Steam может не пускать пакеты себе — проверь вдвоём)",
             "self-test: no packet back in 3 s (Steam may refuse packets to yourself — test with a partner)");
    }
  }
  for (uint64_t id : accept) {
    const bool ok = VSlot<SessionFn>(s_net, NET_ACCEPT)(s_net, id);
    MpLog("AcceptP2PSessionWithUser(%llu): %s", static_cast<unsigned long long>(id), ok ? "ok" : "false");
  }
  for (const auto& k : kicks) {
    const uint8_t why = uint8_t(k.second);
    Send(k.first, W_KICK, &why, 1, P2P_RELIABLE);
  }
  if (bye) Send(bye, W_BYE, nullptr, 0, P2P_RELIABLE);
  if (stateLog) LogSession(stateLog);
  bool self;
  {
    Lk l;
    self = s_selfReq, s_selfReq = false;
  }
  if (self) SelfTest();
  if (hello && !to.empty()) SendHello(to[0]);
  const uint32_t t = uint32_t(now);
  uint32_t sent = 0;
  for (uint64_t id : to) {
    if (!lobby.empty()) Send(id, W_LOBBY, lobby.data(), lobby.size(), P2P_RELIABLE);
    if (ping) Send(id, W_PING, &t, 4, P2P_UNRELIABLE);
    for (size_t i = 0; i < forget.size(); i += kTrigPerPacket)
      Send(id, W_FORGET, forget.data() + i, std::min(kTrigPerPacket, forget.size() - i) * sizeof(WTrig), P2P_RELIABLE);
    for (size_t i = 0; i < trig.size(); i += kTrigPerPacket) {
      const size_t k = std::min(kTrigPerPacket, trig.size() - i);
      if (Send(id, W_TRIG, trig.data() + i, k * sizeof(WTrig), P2P_RELIABLE)) sent += uint32_t(k);
    }
  }
  if (sent) {
    Lk l;
    s_sent += sent;
  }
  if (!ident.empty()) PupSetMyIdentity(ident.c_str(), identWeak);
  // удар Чужого: [что][чей u64]. Свой — всем, кому шлём; чужой (хост получил от клиента) — остальным клиентам
  auto actBody = [](uint64_t who) {
    std::vector<uint8_t> b{ ACT_ATTACK };
    for (int i = 0; i < 8; i++) b.push_back(uint8_t(who >> (8 * i)));
    return b;
  };
  if (act && !to.empty()) {
    const std::vector<uint8_t> b = actBody(me);
    int ok = 0;
    for (uint64_t id : to) ok += Send(id, W_ACT, b.data(), b.size(), P2P_RELIABLE) ? 1 : 0;
    MpLog("my Alien attacks -> %d of %d players", ok, int(to.size()));
  }
  for (uint64_t who : actRelay) {
    const std::vector<uint8_t> b = actBody(who);
    for (uint64_t id : to)
      if (id != who) Send(id, W_ACT, b.data(), b.size(), P2P_RELIABLE);
  }
  {  // хост: какие роли есть в выбранном режиме (миссии, карте) — по моделям уровней, когда выбор сменился
    int mode = 0, mission = 1;
    std::string map, key;
    bool need = false;
    {
      Lk l;
      if (s_role == MP_HOST) {
        mode = s_lobbyMode, mission = s_lobbyMission, map = s_lobbyMap;
        key = std::to_string(mode) + "/" + std::to_string(mission) + "/" + map;
        need = key != s_rolesKey;
      }
    }
    if (need) {
      std::string lvl;
      if (mode == MPM_SURVIVOR)
        for (const MpMap& mm : MpSurvivorMaps())
          if (mm.id == map) lvl = mm.level;
      const uint8_t roles = ModeRoles(mode, mission, lvl);
      Lk l;
      s_rolesKey = key;
      if (roles != s_lobbyRoles) {
        s_lobbyRoles = roles, s_lobbyDirty = true;
        MpLog("roles on the levels of %s: human%s%s%s", MpModeName(mode), (roles >> AV_ALIEN) & 1 ? ", Alien" : "",
              (roles >> AV_ANDROID) & 1 ? ", android" : "", (roles >> AV_FACEHUGGER) & 1 ? ", facehugger" : "");
      }
    }
  }
  if (now - s_missionsAt >= 10000 && me) {  // свои сохранения миссий — раз в 10 с
    const uint32_t ms = ScanMissions(me);
    Lk l;
    s_missionsAt = now;
    if (ms != s_myMissions) {
      s_myMissions = ms;
      s_helloNow = true;  // хост увидит, какие миссии у меня есть
      int cnt = 0;
      for (int n = 1; n <= kMpMissionCount; n++) cnt += (ms >> n) & 1;
      MpLog("mission saves: %d of %d (mask %05X)", cnt, kMpMissionCount, unsigned(ms));
    }
  }
  if (body) {  // своё тело (память игры читается здесь, вне s_mx)
    std::vector<uint8_t> st;
    if (PupEncodeLocal(myAvatar, me, lvl, st) && bodies)
      for (uint64_t id : to) Send(id, W_STATE, st.data(), st.size(), P2P_UNRELIABLE);
  }
  if (s_saveReq.exchange(false)) SaveCfg();
}

DWORD WINAPI NetThread(void*) {
  Interceptor::IgnoreCurrentThread();
  int errs = 0;
  while (!s_stop) {
    if (!Safe([] { NetTick(); }) && errs++ < 5) Log("err: mp network tick (memory access)");
    Sleep(15);
  }
  return 0;
}

void SelfTest() {  // сетевой поток
  uint64_t me;
  uint32_t nonce;
  {
    Lk l;
    if (!s_steamOk || !s_me) return Status("Steam ещё не найден", "Steam not found yet");
    me = s_me;
    nonce = s_selfNonce = uint32_t(NowMs()) ^ 0x5A17u;
    s_selfTest = 1;
    s_selfAt = NowMs();
  }
  const bool ok = Send(me, W_SELF, &nonce, 4, P2P_RELIABLE, kSelfChan);
  Lk l;
  if (!ok) {
    s_selfTest = 3;
    Status("самопроверка: SendP2PPacket вернул false — Steam не принял пакет", "self-test: SendP2PPacket returned false — Steam refused the packet");
  } else {
    Status("самопроверка: пакет ушёл, жду его обратно...", "self-test: packet sent, waiting for it to come back...");
  }
}

// выгнать (под s_mx): пакет KICK и из списка
void KickLocked(uint64_t id, int why) {
  s_kickQ.push_back({ id, why });
  for (size_t i = 0; i < s_members.size(); i++)
    if (s_members[i].id == id) {
      MpLog("%s: %s (%llu)", why == KICK_BANNED ? "BANNED" : "KICKED", s_members[i].name.c_str(), static_cast<unsigned long long>(id));
      s_members.erase(s_members.begin() + ptrdiff_t(i));
      break;
    }
  s_memberCount = int(s_members.size());
  s_lobbyDirty = true;
}

}  // namespace

uint64_t MpParseId(const char* text) { return text ? ParseId(text) : 0; }

void MpSetSurvivorMap(const std::string& id) {
  Lk l;
  if (id == s_lobbyMap) return;
  s_lobbyMap = id.substr(0, 40);
  s_lobbyDirty = true;
  MpLog("lobby: survivor map %s", s_lobbyMap.c_str());
}

// карты выживания — из менеджера плейлистов игры *(G0+0x68): хеш-карта +0x24 {ShortGuid id, плейлист}; у плейлиста
// +0x10 id ("ChallengeMap12"), +0x14 ключ названия ("A1_DLC05_CH_TITLE_15" — номер DLC), +0x20 картинка ("pack5_m1.dds" —
// номер карты в DLC), +0x2c строка уровня ("Production\DLC\ChallengeMap12"). Названия DLC: 1 Corporate Lockdown,
// 2 Trauma, 3 Safe Haven, 4 Lost Contact, 5 The Trigger
static std::string CStr(addr p, size_t max = 64) {
  std::string s;
  if (!p) return s;
  for (size_t i = 0; i < max; i++) {
    const char c = rd<char>(p + i);
    if (!c) break;
    if (c < 32 || c > 126) return std::string();
    s += c;
  }
  return s;
}
std::vector<MpMap> MpSurvivorMaps() {
  std::vector<MpMap> out;
  Safe([&] {
    const addr g0 = rp(VA(va::GameGlobals));
    const addr pm = g0 ? rp(g0 + 0x68) : 0;
    if (!pm) return;
    const addr arr = rp(pm + 0x24);
    const uint32_t n = std::min<uint32_t>(rd<uint32_t>(pm + 0x28), 256), empty = rd<uint32_t>(pm + 0x2c);
    static const char* const kPack[] = { "?", "Corporate Lockdown", "Trauma", "Safe Haven", "Lost Contact", "The Trigger" };
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t key = rd<uint32_t>(arr + i * 8);
      const addr pl = rp(arr + i * 8 + 4);
      if (key == empty || !pl) continue;
      const std::string id = CStr(rp(pl + 0x10)), title = CStr(rp(pl + 0x14)), pic = CStr(rp(pl + 0x20));
      if (id.rfind("ChallengeMap", 0) != 0 && id.rfind("SalvageMode", 0) != 0) continue;
      const addr lv = rp(pl + 0x2c);
      MpMap m;
      m.id = id;
      m.level = lv ? CStr(rp(lv)) : std::string();
      int pack = 0, num = 0;
      if (title.rfind("A1_DLC0", 0) == 0 && title.size() > 7) pack = title[7] - '0';
      const size_t mp = pic.find("_m");
      if (mp != std::string::npos && mp + 2 < pic.size()) num = pic[mp + 2] - '0';
      char b[96];
      if (pack >= 1 && pack <= 5) {
        if (num > 0 && id.rfind("SalvageMode", 0) != 0) std::snprintf(b, sizeof b, TR("%s — карта %d", "%s — map %d"), kPack[pack], num);
        else std::snprintf(b, sizeof b, "%s", kPack[pack]);
      } else {
        std::snprintf(b, sizeof b, TR("%s (без DLC)", "%s (no DLC)"), id.c_str());
      }
      m.name = b;
      out.push_back(m);
    }
  });
  std::sort(out.begin(), out.end(), [](const MpMap& a, const MpMap& b) { return a.name < b.name; });
  return out;
}
std::string MpMapName(const std::string& id) {
  for (const MpMap& m : MpSurvivorMaps())
    if (m.id == id) return m.name;
  return id;
}
bool MpGameMenuOn() { return s_gameMenu; }
bool MpAvatarLocked() {
  if (!s_avatarDone || !s_started) return false;
  const int role = s_role;
  Lk l;
  const bool in = role == MP_CLIENT ? s_state == MPS_CONNECTED : role == MP_HOST && s_lobbyOpen;
  return in && s_avatar != AV_HUMAN && !s_myLevel.empty();
}

const char* MpOutName(uint32_t guid) {
  const MpOutNameRow* b = std::begin(kMpOutNames);
  const MpOutNameRow* e = std::end(kMpOutNames);
  const MpOutNameRow* it = std::lower_bound(b, e, guid, [](const MpOutNameRow& o, uint32_t g) { return o.guid < g; });
  return it != e && it->guid == guid ? it->name : nullptr;
}

MpSnap MpSnapshot() {
  MpSnap s;
  s.role = s_role, s.state = s_state, s.tap = s_tap, s.suppress = s_suppress, s.gameMenu = s_gameMenu;
  s.lobbyOpen = s_lobbyOpen, s.started = s_started;
  Lk l;
  s.steam = s_steamOk;
  s.name = s_name;
  s.me = s_me;
  s.lobbyLevel = s_lobbyLevel;
  s.lobbyMode = s_lobbyMode, s.lobbyDiff = s_lobbyDiff, s.lobbyMap = s_lobbyMap, s.lobbyChar = s_lobbyChar;
  s.lobbyMission = s_lobbyMission, s.myMissions = s_myMissions, s.lobbyRoles = s_lobbyRoles;
  if (s.role == MP_HOST) {
    for (const Member& m : s_members) s.members.push_back({ m.id, m.name, m.avatar, false, m.chr, m.pingMs, m.missions });
  } else {
    s.members = s_lobbyView;
  }
  s.bans = s_bans;
  s.peer = s_peer;
  s.peerName = s_hostName;
  s.pingMs = s_pingMs;
  s.selfTest = s_selfTest;
  s.status = g_lang ? s_statusEn : s_statusRu;
  s.skin = s_skin;
  s.avatar = s_avatar;
  s.myChar = s_myChar;
  s.myLevel = s_myLevel;
  s.sent = s_sent, s.recv = s_recv, s.replayed = s_replayed, s.suppressed = s_suppressed, s.captured = s_captured, s.missed = s_missed;
  s.ecxOdd = s_ecxOdd;
  s.friends = s_friendList;
  s.filtered = s_filtered, s.noisyKeys = s_noisyKeys;
  s.trig.reserve(s_jr.size());
  for (const JEntry& e : s_jr) s.trig.push_back(e.t);
  return s;
}

void MpCmd(int action, int arg, uint64_t id) {
  bool save = false;
  switch (action) {
    case MPA_ROLE: {
      const int r = std::clamp(arg, 0, 2);
      Lk l;
      if (r != s_role) {
        if (s_role == MP_HOST && s_lobbyOpen) {  // был хостом — лобби закрыть
          for (const Member& m : s_members) s_kickQ.push_back({ m.id, KICK_CLOSED });
          s_members.clear(), s_memberCount = 0;
          s_lobbyOpen = false, s_started = false;
        }
        if (s_role == MP_CLIENT && s_state != MPS_IDLE) s_byeTo = s_peer, s_state = MPS_IDLE, s_started = false;
        s_role = r;
        MpLog("role: %s", RoleStr(r));
      }
      save = true;
      break;
    }
    case MPA_LOBBY_OPEN: {
      Lk l;
      if (s_role == MP_CLIENT && s_state != MPS_IDLE) s_byeTo = s_peer, s_state = MPS_IDLE;
      s_role = MP_HOST;
      s_lobbyOpen = true, s_started = false;
      s_members.clear(), s_memberCount = 0;
      s_lobbyDirty = true;
      MpLog("LOBBY OPEN: host %s (%llu)", s_name.c_str(), static_cast<unsigned long long>(s_me));
      Status("лобби открыто — дай друзьям свой адрес (Steam ID) и выбери карту", "the lobby is open — give friends your address (Steam ID) and pick a map");
      save = true;
      break;
    }
    case MPA_LOBBY_CLOSE: {
      PupClear("lobby closed");
      Lk l;
      for (const Member& m : s_members) s_kickQ.push_back({ m.id, KICK_CLOSED });
      s_members.clear(), s_memberCount = 0;
      s_lobbyOpen = false, s_started = false;
      MpLog("LOBBY CLOSED");
      Status("лобби закрыто", "the lobby is closed");
      break;
    }
    case MPA_START: {
      Lk l;
      if (s_role != MP_HOST || !s_lobbyOpen) break;
      s_started = true;
      s_startGen++;
      s_lobbyLevel.clear();
      s_lobbyDirty = true;
      if (s_lobbyMode == MPM_SURVIVOR && s_lobbyMap.empty()) break;  // карта не выбрана
      s_startReq = s_lobbyMode, s_startDiff = s_lobbyDiff, s_startMap = s_lobbyMap, s_startChar = s_myChar;
      s_startMission = s_lobbyMission;
      s_avatarDone = false;
      s_startAt = NowMs();  // хост запускает сам — тем же путём, что клиенты
      s_sentKeys.clear();
      MpLog("START #%d: %s (%s), %u players", int(s_startGen), MpModeName(s_lobbyMode), MpDiffName(s_lobbyDiff), unsigned(s_members.size()));
      Status("начали: %s — запускается у всех", "started: %s — launching for everyone", MpModeName(s_lobbyMode));
      break;
    }
    case MPA_KICK:
    case MPA_BAN: {
      Lk l;
      if (!id) break;
      std::string name;
      if (const Member* m = MemberLocked(id)) name = m->name;
      KickLocked(id, action == MPA_BAN ? KICK_BANNED : KICK_KICKED);
      if (action == MPA_BAN && !BannedLocked(id)) s_bans.push_back({ id, name }), save = true;
      Status(action == MPA_BAN ? "%s забанен" : "%s выгнан", action == MPA_BAN ? "%s is banned" : "%s is kicked",
             name.empty() ? IdStr(id).c_str() : name.c_str());
      break;
    }
    case MPA_UNBAN: {
      Lk l;
      s_bans.erase(std::remove_if(s_bans.begin(), s_bans.end(), [&](const MpBan& b) { return b.id == id; }), s_bans.end());
      MpLog("unbanned %llu", static_cast<unsigned long long>(id));
      save = true;
      break;
    }
    case MPA_ACCEPT:
      break;
    case MPA_MODE:
    case MPA_DIFF:
    case MPA_CHAR:
    case MPA_MISSION: {
      Lk l;
      if (action == MPA_MODE) s_lobbyMode = std::clamp(arg, 0, kMpModeCount - 1);
      else if (action == MPA_DIFF) s_lobbyDiff = std::clamp(arg, 0, kMpDiffCount - 1);
      else if (action == MPA_CHAR) s_lobbyChar = std::clamp(arg, 1, kMpCharCount);
      else s_lobbyMission = std::clamp(arg, 1, kMpMissionCount);
      s_lobbyDirty = true;
      MpLog("lobby: %s, %s", MpModeName(s_lobbyMode), MpDiffName(s_lobbyDiff));
      break;
    }
    case MPA_CONNECT: {
      Lk l;
      if (!id || id == s_me) break;
      if (s_role == MP_HOST && s_lobbyOpen) {  // был хостом — лобби закрыть
        for (const Member& m : s_members) s_kickQ.push_back({ m.id, KICK_CLOSED });
        s_members.clear(), s_memberCount = 0;
        s_lobbyOpen = false;
      }
      if (s_peer && s_peer != id && s_state != MPS_IDLE) s_byeTo = s_peer;
      s_role = MP_CLIENT;
      s_peer = id;
      s_state = MPS_CONNECTING;
      s_started = false;
      s_lobbyLevel.clear();
      s_lobbyView.clear();
      s_hostName.clear();
      s_pingMs = -1;
      s_helloNow = true, s_helloSent = 0;
      s_connectAt = s_lastRecv = NowMs();
      s_lastStateLog = 0;
      s_acceptQ.push_back(id);  // сессию с хостом примем сразу (если он заговорит первым)
      MpLog("JOIN: me %llu (%s) as %s -> host %llu", static_cast<unsigned long long>(s_me), s_name.c_str(), MpAvatarName(s_avatar),
            static_cast<unsigned long long>(id));
      Status("подключаюсь к хосту %s...", "connecting to the host %s...", IdStr(id).c_str());
      save = true;
      break;
    }
    case MPA_DISCONNECT: {
      PupClear("left the lobby");
      Lk l;
      MpLog("left the lobby");
      if (s_state != MPS_IDLE) s_byeTo = s_peer;
      s_state = MPS_IDLE;
      s_started = false;
      s_lobbyView.clear();
      s_pingMs = -1;
      s_replayQ.clear();
      s_replayN = 0;
      Status("вышел из лобби", "left the lobby");
      break;
    }
    case MPA_SELFTEST: {
      Lk l;
      s_selfReq = true;
      break;
    }
    case MPA_SKIN: {
      Lk l;
      s_skin = std::clamp(arg, 0, kMpSkinCount - 1);
      s_helloNow = true;
      save = true;
      break;
    }
    case MPA_TAP: s_tap = arg != 0, save = true; break;
    case MPA_SUPPRESS: s_suppress = arg != 0, save = true; break;
    case MPA_AVATAR: {
      Lk l;
      s_avatar = std::clamp(arg, 0, AV_COUNT - 1);
      s_avatarDone = false;  // на этом уровне отдадим нового
      s_helloNow = true;     // хост сразу увидит
      save = true;
      MpLog("my avatar: %s", MpAvatarName(s_avatar));
      break;
    }
    case MPA_GAMEMENU: s_gameMenu = arg != 0, save = true, MpLog("game menu buttons %s", arg ? "on" : "off"); break;
    case MPA_HUMANBODY:
      PupSetHumanBody(arg), save = true;
      MpLog("another human's body here: %s", PupHumanBodyName(PupHumanBody()));
      break;
    case MPA_GHOST: PupGhost(arg != 0); break;
    case MPA_MYCHAR: {
      Lk l;
      s_myChar = std::clamp(arg, 1, kMpCharCount);
      s_helloNow = true, s_lobbyDirty = true;
      save = true;
      MpLog("my Survivor character: %s", MpCharName(s_myChar));
      break;
    }
    case MPA_REPLAY: {  // arg — номер в журнале, id — (ent << 32 | out) для сверки: журнал мог сдвинуться
      Lk l;
      const auto match = [&](const JEntry& e) { return (uint64_t(e.t.ent) << 32 | e.t.out) == id; };
      const JEntry* e = arg >= 0 && size_t(arg) < s_jr.size() && match(s_jr[size_t(arg)]) ? &s_jr[size_t(arg)] : nullptr;
      for (size_t i = 0; !e && i < s_jr.size(); i++)
        if (match(s_jr[i])) e = &s_jr[i];
      if (!e) break;
      QueueReplay({ e->t.ent, e->t.inst, e->t.out, e->t.delay });
      Log("mp: replay %08X:%08X out %08X queued", unsigned(e->t.ent), unsigned(e->t.inst), unsigned(e->t.out));
      break;
    }
    case MPA_CLEAR: {
      Lk l;
      s_jr.clear();
      s_hostKeys.clear();
      s_stat.clear();
      s_rate.clear();
      s_filtered = s_noisyKeys = 0;
      s_sent = s_recv = s_replayed = s_suppressed = s_captured = s_missed = 0;
      break;
    }
    default: break;
  }
  if (save) SaveCfg();
}

void MpLogStats(int n) {  // отладка (dev\cmd.txt: mpstat): самые частые «тип сущности + выход»
  std::vector<std::pair<uint64_t, uint32_t>> v;
  {
    Lk l;
    v.assign(s_stat.begin(), s_stat.end());
  }
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  uint64_t total = 0;
  for (const auto& e : v) total += e.second;
  Log("mp: stats — %u kinds, %llu outputs", unsigned(v.size()), static_cast<unsigned long long>(total));
  for (int i = 0; i < n && size_t(i) < v.size(); i++) {
    const uint32_t out = uint32_t(v[size_t(i)].first & 0xFFFFFFFFu);
    const char* nm = MpOutName(out);
    Log("mp: stat vt %08X out %08X %-22s %u (%.1f%%)", unsigned(v[size_t(i)].first >> 32), unsigned(out), nm ? nm : "?",
        unsigned(v[size_t(i)].second), total ? 100.0 * v[size_t(i)].second / double(total) : 0.0);
  }
}

void MpLevelReset() {
  PupClear("level change");
  s_avatarDone = false;
  s_avatarTryAt = NowMs() + 3000;  // уровень грузится — персонажей ищем чуть позже
  Lk l;
  s_replayQ.clear();
  s_replayN = 0;
  s_sendQ.clear();
  s_forgetQ.clear();
  s_hostKeys.clear();
  s_sentKeys.clear();
  s_rate.clear();
  s_jr.clear();
  s_lastTI = 0;
}

namespace {

// Главный поток игры, перед SteamAPI_RunCallbacks: только здесь регистрируем и снимаем обратный вызов Steam (список
// обратных вызовов Steam не потокобезопасен — регистрация из сетевого потока подвешивала игру напарника, 01.10.2026)
// и грузим уровень (queue_level + request_next_level — как страница «Уровни»).
// запустить режим так же, как пункт меню игры (play_dlc_two_teams -> FUN_00823380): сложность FUN_00822f30, затем
// FUN_00813ee0(экран, режим) — сброс игры (FUN_007bcd40), плейлист режима, закрыть экран меню, queue_level +
// request_next_level. Экран — главное меню, если оно есть (его закрывают), иначе пустышка (FUN_00804f30 смотрит только
// +0xc «показан»). Под критической секцией интерфейса — как игра в обработчиках меню.
typedef void(__stdcall* SetDifficultyFn)(int difficulty);
typedef void(__thiscall* SelectPlaylistFn)(addr levelMgr, const char* playlistId);
typedef void(__thiscall* StartWithCharFn)(addr screen, const char* characterId);
typedef void(__thiscall* StartModeFn)(addr screen, int mode);
// загрузить сохранение по имени, как пункт меню игры (FUN_008055a0, thiscall(экран, "MSSV06"), RET 4): готов ли менеджер
// сохранений *(G0+0x78) (+0x824) — иначе его подсказка; дальше FUN_0078c910(менеджер, имя, экран->vt+0x7c())
typedef void(__thiscall* LoadSaveFn)(addr screen, const char* name);
typedef addr(__thiscall* FindOverlayFn)(addr mgr, const char* name);
enum { START_DONE, START_LATER, START_NOT_IN_MENU, START_NO_SAVE };
// выживание — как экран выбора карты: request_character_selection(id карты) -> FUN_0078bff0(менеджер уровней, id)
// (копирует id в +0x2fd), request_gameplay_start(id персонажа) -> FUN_00823d50(экран, "CharacterN"): сброс игры,
// персонаж, первый уровень выбранного плейлиста, закрыть экран, queue_level + request_next_level
int StartMode(int mode, int diff, const std::string& map, int chr, int mission, bool haveMission) {  // главный поток
  if (mode == MPM_MISSION && !haveMission) return START_NO_SAVE;
  const addr mgr = rp(VA(0x0174a78c));
  if (!mgr) return START_LATER;
  CRITICAL_SECTION* cs = reinterpret_cast<CRITICAL_SECTION*>(mgr + 4);
  if (!TryEnterCriticalSection(cs)) return START_LATER;  // интерфейс занят — в следующем кадре
  const addr screen = Fn<FindOverlayFn>(0x00810420)(mgr, "NEW_FRONTEND_MENU");
  LeaveCriticalSection(cs);
  // режимы игра запускает только из главного меню: на уровне — ждём, пока игрок выйдет в меню
  if (!screen || !rd<uint8_t>(screen + 0x28)) return START_NOT_IN_MENU;
  AlienMod::OnLevelChange("mp start");  // мод — как при смене уровня (общий замок; вне секции интерфейса)
  EnterCriticalSection(cs);
  // Movie главного меню здесь НЕ закрепляем (0.16.1 закреплял — не помогло; запуски 0.16 без этого шли). Зависания при
  // загрузке были из-за кнопок мода в меню игры (gamemenu.cpp держал объекты меню) — отпускаем их до закрытия экрана
  GameMenuReleaseNow("mp start");
  if (mode == MPM_MISSION) {  // миссия кампании: сохранение её начала, как «Загрузить -> миссия» в меню игры
    char nm[16];
    std::snprintf(nm, sizeof nm, "MSSV%02d", std::clamp(mission, 1, kMpMissionCount));
    Fn<LoadSaveFn>(0x008055a0)(screen, nm);
  } else if (mode == MPM_SURVIVOR) {
    char cid[16];
    std::snprintf(cid, sizeof cid, "Character%d", std::clamp(chr, 1, kMpCharCount));
    const addr lm = rp(rp(VA(va::GameGlobals)) + 0x3c);
    Fn<SelectPlaylistFn>(0x0078bff0)(lm, map.c_str());
    Fn<StartWithCharFn>(0x00823d50)(screen, cid);
  } else {
    Fn<SetDifficultyFn>(0x00822f30)(diff);
    Fn<StartModeFn>(0x00813ee0)(screen, mode);
  }
  LeaveCriticalSection(cs);
  return START_DONE;
}
bool s_saidNotInMenu = false;
void OnRunCallbacks(Invocation&) {
  s_mainTid = GetCurrentThreadId();
  s_mainTickAt = NowMs();
  int mode, diff, chr, mission;
  bool haveMission;
  std::string map;
  {
    Lk l;
    mode = s_startReq, diff = s_startDiff, map = s_startMap, chr = s_startChar, mission = s_startMission;
    haveMission = (s_myMissions >> std::clamp(mission, 1, kMpMissionCount)) & 1;
  }
  if (mode >= 0) {
    int r = START_LATER;
    if (!Safe([&] { r = StartMode(mode, diff, map, chr, mission, haveMission); })) MpLog("start %d: access violation", mode), r = START_DONE;
    if (r == START_NO_SAVE) {
      Lk l;
      s_startReq = -1, s_startAt = 0;
      Status("нет своего сохранения миссии %s — её надо хоть раз начать в кампании", "no save of %s here — start it once in the campaign",
             MpMissionName(mission));
      return;
    }
    if (r == START_DONE) {
      Lk l;
      s_startReq = -1;
      s_saidNotInMenu = false;
      s_startAt = NowMs();
      if (mode == MPM_MISSION) MpLog("launched %s (MSSV%02d) the game's own way", MpMissionName(mission), mission);
      else if (mode == MPM_SURVIVOR) MpLog("launched survivor map %s as Character%d the game's own way", map.c_str(), chr);
      else MpLog("launched %s (%s) the game's own way", MpModeName(mode), MpDiffName(diff));
    } else if (r == START_NOT_IN_MENU && !s_saidNotInMenu) {
      s_saidNotInMenu = true;
      Lk l;
      s_startAt = 0;
      Status("выйди в главное меню игры — %s запустится сам", "go to the game's main menu — %s will launch by itself", MpModeName(mode));
    }
  }
  if (s_unregReq) {
    if (s_cbReg && s_steamDll)
      if (auto un = reinterpret_cast<UnregCbFn>(reinterpret_cast<void*>(GetProcAddress(s_steamDll, "SteamAPI_UnregisterCallback")))) un(&s_cb);
    s_cbReg = false;
    s_unregDone = true;
    return;
  }
  if (s_cbReg || !s_net || !s_steamDll || s_stop) return;
  if (auto reg = reinterpret_cast<RegCbFn>(reinterpret_cast<void*>(GetProcAddress(s_steamDll, "SteamAPI_RegisterCallback")))) {
    reg(&s_cb, kCbSessionRequest);
    s_cbReg = true;
    MpLog("Steam session callback registered (game main thread %lu)", GetCurrentThreadId());
  }
}

}  // namespace

void InstallMp() {
  using Interceptor::Attach;
  LoadCfg();
  if (HMODULE dll = GetModuleHandleA("steam_api.dll"))
    if (const addr rc = addr(GetProcAddress(dll, "SteamAPI_RunCallbacks"))) Attach(rc, OnRunCallbacks, nullptr, false);
  // горячий (все выходы всех сущностей): без общего замка — только атомики и s_mx
  Attach(VA(va::CallTriggers), OnTrigEnter, OnTrigLeave, false);
  Attach(VA(va::LocoUpdate), OnGameTick, nullptr);
  s_thread = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr);
}

void MpShutdown() {  // отцепление мода: попрощаться, остановить сеть, снять обратный вызов Steam
  {
    Lk l;
    if (s_role == MP_CLIENT && s_state != MPS_IDLE) s_byeTo = s_peer, s_state = MPS_IDLE;
    if (s_role == MP_HOST && s_lobbyOpen) {
      for (const Member& m : s_members) s_kickQ.push_back({ m.id, KICK_CLOSED });
      s_members.clear(), s_memberCount = 0;
      s_lobbyOpen = false;
    }
  }
  for (int i = 0; i < 10; i++) {  // сетевой поток успевает отправить BYE / KICK
    bool pending;
    {
      Lk l;
      pending = s_byeTo || !s_kickQ.empty();
    }
    if (!pending) break;
    Sleep(10);
  }
  s_stop = true;
  if (s_thread) {
    WaitForSingleObject(s_thread, 2000);
    CloseHandle(s_thread);
    s_thread = nullptr;
  }
  // обратный вызов снимает главный поток игры (OnRunCallbacks); не успел (игра стоит) — объект остаётся в памяти и молчит
  if (s_cbReg) {
    s_unregReq = true;
    for (int i = 0; i < 40 && !s_unregDone; i++) Sleep(10);
    if (!s_unregDone) s_cbDead = true, Log("mp: Steam callback left registered but muted (the game thread didn't tick)");
  }
}
