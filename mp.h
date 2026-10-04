// mp.h — мультиплеер (mp.cpp): лобби через Steam P2P (хост и до 4 клиентов), выбор карты, кто за кого играет,
// выгнать/забанить, «Начать» — у всех грузится уровень; журнал выходов сущностей CATHODE.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// одна запись журнала: сущность-источник (EntityHandle = {GUID сущности, ID экземпляра составной}) и GUID её выхода
struct MpTrig {
  uint32_t ent = 0, inst = 0, out = 0;
  float delay = 0;      // задержка выхода (последний аргумент CallTriggers), с
  uint32_t n = 0;       // сколько раз (одинаковые склеиваются)
  uint64_t at = 0;      // NowMs последнего
  bool remote = false;  // пришло от хоста
  std::string kind;     // тип сущности: VA её vtable (RTTI у итоговых vtable нет) — только у своих
};

struct MpFriend { uint64_t id = 0; std::string name; };  // друг из Steam, который сейчас в Alien: Isolation

enum MpRole { MP_OFF = 0, MP_HOST = 1, MP_CLIENT = 2 };
// за кого играет игрок — и клиент, и хост (выбирает до начала; после загрузки уровня мод сам отдаёт ему управление).
// Человек — свой человек (Аманда в своей копии игры): мод ничего не перехватывает
enum MpAvatar { AV_HUMAN = 0, AV_ALIEN = 1, AV_ANDROID = 2, AV_FACEHUGGER = 3, AV_COUNT };
const char* MpAvatarName(int a);
// клиент: в лобби ещё нет (подключаюсь), в лобби, связь с хостом пропала
enum MpState { MPS_IDLE = 0, MPS_CONNECTING = 1, MPS_CONNECTED = 2, MPS_LOST = 3 };
constexpr int kMpMaxClients = 4;
// что запускает «Начать» (как меню игры): 0 кампания с начала, 1–2 DLC «Ностромо» (плейлисты, FUN_00813ee0),
// 3 выживание — карта (плейлист ChallengeMapN / SalvageModeN) и персонаж (Character1..9), как экран выбора карты.
// Сложность 0–4 (новичок … кошмар) — для кампании и «Ностромо»
// 4 — миссия кампании: сохранение начала миссии MSSVnn (как «Загрузить → миссия» в меню игры), у каждого — своё
enum { MPM_CAMPAIGN = 0, MPM_RIPLEY = 1, MPM_TWOTEAMS = 2, MPM_SURVIVOR = 3, MPM_MISSION = 4 };
constexpr int kMpModeCount = 5, kMpDiffCount = 5, kMpCharCount = 9, kMpMissionCount = 19;
const char* MpMissionName(int n);  // 1..19: «миссия 6 — госпиталь Сан-Кристобаль»
const char* MpModeName(int mode);
const char* MpDiffName(int diff);
const char* MpCharName(int ch);  // 1..9 — персонажи выживания
struct MpMap { std::string id, name, level; };  // карта выживания: id плейлиста, подпись, уровень
std::vector<MpMap> MpSurvivorMaps();            // из игры (менеджер плейлистов) — можно звать из меню
std::string MpMapName(const std::string& id);

struct MpMember {        // игрок в лобби
  uint64_t id = 0;
  std::string name;
  int avatar = AV_HUMAN;
  bool host = false;      // это хост
  int chr = 1;            // человек в выживании: свой персонаж Character1..9
  int pingMs = -1;        // у хоста — пинг до этого клиента
  uint32_t missions = 0;  // у хоста: бит n — у него есть сохранение миссии n (MSSVnn)
};
struct MpBan { uint64_t id = 0; std::string name; };

struct MpSnap {
  bool steam = false;          // steam_api.dll и интерфейсы есть
  std::string name;            // своё имя в Steam
  uint64_t me = 0;             // свой SteamID64
  int role = MP_OFF, state = MPS_IDLE;
  // лобби: у хоста — своё, у клиента — то, что прислал хост
  bool lobbyOpen = false;      // хост: лобби создано и принимает игроков
  bool started = false;        // хост нажал «Начать» — все на уровне lobbyLevel
  int lobbyMode = 0, lobbyDiff = 2;  // что запускать и сложность
  std::string lobbyMap;        // выживание: id карты (плейлиста)
  int lobbyChar = 1;           // выживание: персонаж 1..9
  int lobbyMission = 6;        // миссия кампании 1..19
  int lobbyRoles = 0xF;        // какие роли есть на уровнях режима: бит (1 << MpAvatar); человек — всегда
  uint32_t myMissions = 0;     // бит n — у меня есть сохранение миссии n
  std::string lobbyLevel;      // уровень хоста сейчас (короткое имя), пусто — меню / загрузка
  std::vector<MpMember> members;  // хост: клиенты; клиент: все в лобби, первым — хост
  std::vector<MpBan> bans;     // хост: кого не пускать
  uint64_t peer = 0;           // клиент: хост (последний, к кому подключались)
  std::string peerName;        // клиент: имя хоста
  int pingMs = -1;             // клиент: пинг до хоста
  int selfTest = 0;            // 0 не было, 1 идёт, 2 ок, 3 провал
  uint64_t incoming = 0;       // (не используется: хост принимает всех, кроме забаненных)
  std::string status;          // последнее событие (уже переведено)
  int skin = 0;
  int avatar = AV_ALIEN;       // за кого играю (и хост, и клиент)
  int myChar = 1;              // мой персонаж в выживании 1..9 (как экран выбора персонажа DLC)
  std::string myLevel;         // свой уровень (короткое имя); пусто — главное меню / загрузка
  bool tap = false, suppress = true, gameMenu = false;
  uint32_t sent = 0, recv = 0, replayed = 0, suppressed = 0, captured = 0, missed = 0;
  uint32_t ecxOdd = 0;         // вызовов CallTriggers, где this — не найденный модом интерфейс сущности (ожидаем 0)
  uint32_t filtered = 0, noisyKeys = 0;  // отсеяно как шум (тиканье, пересчёт каждый кадр): выходов / разных ключей
  std::vector<MpTrig> trig;    // последние выходы (новые сверху)
  std::vector<MpFriend> friends;  // друзья в Alien: Isolation
};

// облики человека (кем видит тебя другой игрок); имена — ru/en
constexpr int kMpSkinCount = 13;
const char* MpSkinName(int i);

MpSnap MpSnapshot();
// действия из меню — прямо из потока рендера: всё состояние мультиплеера под своим маленьким замком, общий замок
// и функции игры здесь не нужны
enum MpAction {
  MPA_ROLE = 1,       // arg — роль (хост / клиент / выкл)
  MPA_CONNECT = 2,    // клиент: id — SteamID64 хоста — войти в его лобби
  MPA_DISCONNECT = 3, // клиент: выйти из лобби
  MPA_SELFTEST = 4,   // пакет самому себе: проверка порядка методов ISteamNetworking
  MPA_SKIN = 5,       // arg — облик
  MPA_TAP = 6,        // arg — журнал выходов вкл/выкл
  MPA_SUPPRESS = 7,   // arg — клиент глушит свои выходы у сущностей, которые ведёт хост
  MPA_REPLAY = 8,     // arg — номер записи журнала: повторить этот выход здесь (проверка find_entity + CallTriggers)
  MPA_CLEAR = 9,
  MPA_ACCEPT = 10,    // (не используется)
  MPA_GAMEMENU = 11,  // arg — пункт «Мультиплеер» в меню игры (главное, пауза) вкл/выкл (экспериментально)
  MPA_AVATAR = 12,    // arg — за кого играет клиент (MpAvatar)
  MPA_LOBBY_OPEN = 14,   // хост: создать лобби
  MPA_LOBBY_CLOSE = 15,  // хост: закрыть лобби (всех — домой)
  MPA_START = 16,        // хост: начать — у всех грузится выбранная карта
  MPA_KICK = 17,         // хост: id — выгнать
  MPA_BAN = 18,          // хост: id — выгнать и не пускать
  MPA_UNBAN = 19,        // хост: id — снова пускать
  MPA_MODE = 20,         // хост: arg — что запускать (0..kMpModeCount-1)
  MPA_DIFF = 21,         // хост: arg — сложность (0..kMpDiffCount-1)
  MPA_CHAR = 22,         // хост: arg — персонаж выживания 1..9
  MPA_HUMANBODY = 23,    // arg — кто изображает здесь другого человека (PupHumanBody, puppet.h)
  MPA_GHOST = 24,        // arg — «зеркало»: своё тело куклой впереди себя (проверка без второго игрока)
  MPA_MISSION = 25,      // хост: arg — миссия кампании 1..19
  MPA_MYCHAR = 26,       // arg — мой персонаж в выживании 1..9
};
bool MpGameMenuOn();   // gamemenu.cpp: вставлять ли пункт в меню игры
// в игре по сети играю не человеком и персонажа уже получил — переключиться на Аманду нельзя (B, «вернуть Аманде»)
bool MpAvatarLocked();
void MpSetSurvivorMap(const std::string& id);  // хост: карта выживания
void MpCmd(int action, int arg, uint64_t id);

// SteamID64 из того, что вставил игрок: сам ID, ссылка на профиль или код друга (ID аккаунта); 0 — не ID
uint64_t MpParseId(const char* text);
// имя выхода по его ShortGuid (mp_names.h) или nullptr
const char* MpOutName(uint32_t guid);
void MpLogStats(int n);  // отладка: самые частые «тип сущности + выход» — в лог
void MpLevelReset();     // смена уровня (HardReset): очередь повторов и ключи — прошлого уровня
void InstallMp();
void MpShutdown();
