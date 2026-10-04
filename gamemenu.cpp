// gamemenu.cpp — «Совместная игра» прямо в меню игры (главное меню NEWFRONTENDMENU.GFX и меню паузы PAUSEMENU.GFX —
// Scaleform AS3): создать лобби, зайти к другу, режим / миссия / карта / сложность переключателями игры, роль — экраном
// выбора персонажа из DLC (с портретами), игроки (выгнать, забанить), «Начать игру». Окно мода для этого не нужно.
//
// Как устроены меню игры (по байткоду AS3, %TEMP%\claude\ui\avm2.py; подробно — MULTIPLAYER.md, «Меню игры»):
//   * экран-оверлей ищет FUN_00810420(менеджер *(0x0174a78c), имя): «pause_menu», «NEW_FRONTEND_MENU». У экрана +8 —
//     GFx::Movie (у меню паузы появляется только пока оно открыто), +0x28 — показан, +0x68 — указатель на GFx::Value корня
//     (PauseMenu / NewFrontendMenu, оба extends GameMenu). Перед вызовами в AS3 игра берёт критическую секцию менеджера
//     (+4) и «закрепляет» Movie за своим потоком: Movie vt+0x6c(id потока), vt+0xc0() -> объект, +0x14 = id.
//   * root.menu (Menu) -> rootPage (MenuPage); пункт — new frontend.menu.MenuItem(подпись, обработчик(событие)),
//     подменю — MenuItemExpandable(подпись), его страница — item.page; переключатель «‹ значение ›» — как сложность в
//     настройках игры: new MenuItemOptionOptionStepper(подпись, значения[], подписи[], обработчик), новое значение —
//     selectedIndex (сеттер сам глушит событие, пока его ставят из кода); после добавления —
//     menu.rebuildItemMap/rebuildPageMap/refresh. Подпись — любой текст (если это не ключ локализации, он и выводится).
//   * экран выбора персонажа DLC: root.setCharacterSelection(массив dlc.utils.CharacterData {id, name, description,
//     icon, image}) — портрет грузится как "DLC/" + image (DATA\UI\DLC\*.DDS, 248x456 DXT1) и переключает состояние меню
//     в DLC_CHAR (fe_set_state -> FUN_00848ca0(экран, 4, анимация); 1 — меню, 3 — выбор карты). Выбор —
//     ExternalInterface "request_gameplay_start"(id) -> FUN_00823d50(экран, id). Наши карточки — id "MP_…": их выбор
//     перехватываем (игра не запускает выживание), «назад» (в DLC_MAP) — уводим в меню.
//   * GFx::Value (24 байта): +0 ObjectInterface*, +4 тип (2 bool, 3 int, 6 строка, 8 объект, 10 экранный объект;
//     0x40 — управляемый), +8 данные. ObjectInterface: vt+8 Release, +0x10 GetMember, +0x14 SetMember, +0x18 Invoke.
//     Объекты создаёт AS3::MovieRoot (vtable 0x014872E8, ищем среди полей Movie): +0xB8 CreateObject(out, класс, args, n),
//     +0xBC CreateArray(out), +0xC0 CreateFunction(out, FunctionHandler*, userData).
//   * обработчик наших пунктов — своя функция C++: Scaleform зовёт vt[1](const Params&) — Params {pRetVal, pMovie, pThis,
//     pArgsWithThis, pArgs, ArgCount, pUserData} (FUN_00ee66f0); userData — номер пункта. MenuItem подписывает обработчик
//     со слабой ссылкой — функции AS3 держим сами, пока жив Movie.
//   * тик — SteamAPI_RunCallbacks (поток интерфейса игры).
// Ссылки на объекты AS3 держим только пока экран показан. 02.10.2026: с включёнными кнопками игра дважды зависла на
// загрузке уровня из главного меню (01.10 21:53 и 02.10 04:10; с выключенными — все загрузки шли): главный поток ждал
// задачу загрузки, пока мод держал объекты меню и «закреплял» его Movie за собой. Теперь: экран закрывается
// (FUN_00804f30 — так его закрывают запуск режима, «Продолжить» и т. п.) — сразу отпускаем свои объекты (это ещё внутри
// обработчика игры, Movie её); запуск режима мультиплеера отпускает их сам (GameMenuReleaseNow); в меню лезем, только
// пока игра спокойно стоит в нём (главное меню — состояние игры 2, пауза — 4) и экран показан; после закрытия — 10 с тишины.
#include "game.h"
#include "interceptor.h"
#include "mp.h"
#include "mp_portraits.h"
#include "state.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

const std::wstring& ModBase();  // runtime.cpp

namespace {

namespace gva {
constexpr addr UiManager = 0x0174a78c, FindOverlay = 0x00810420, AsMovieRootVt = 0x014872E8;
constexpr addr ScreenClose = 0x00804f30;     // thiscall(экран): закрыть экран (запуск режима, загрузка)
constexpr addr ScreenSetState = 0x00848ca0;  // thiscall(экран, состояние, анимация): 1 меню, 3 выбор карты, 4 выбор персонажа
constexpr addr GameplayStart = 0x00823d50;   // thiscall(экран, id персонажа), RET 4: request_gameplay_start
// входы загрузки, которыми пользуется меню игры («Продолжить», «Загрузить», миссии, «Новая игра», перезапуск из паузы):
// загрузка сохранения FUN_0078c910(менеджер сохранений, имя, …), сброс игры FUN_007bcd40, queue_level, загрузка уровня меню
constexpr addr LoadSave = 0x0078c910, GameReset = 0x007bcd40, QueueLevel = 0x0077b320, MenuLoadLevel = 0x008139c0;
}
enum { FE_MENU = 1, FE_DLC_MAP = 3, FE_DLC_CHAR = 4 };

struct GValue { addr iface; uint32_t type; uint32_t v[2]; uint32_t aux[2]; };
static_assert(sizeof(GValue) == 24, "GFx::Value — 24 байта");
enum : uint32_t { VT_Undefined = 0, VT_Boolean = 2, VT_Int = 3, VT_UInt = 4, VT_Number = 5, VT_String = 6, VT_Object = 8,
                  VT_Array = 9, VT_DisplayObject = 10, VT_Closure = 11, VTC_Managed = 0x40, VTC_TypeMask = 0x8F };

typedef bool(__thiscall* GetMemberFn)(addr iface, addr pdata, const char* name, GValue* out, bool isdobj);
typedef bool(__thiscall* SetMemberFn)(addr iface, addr pdata, const char* name, const GValue* v, bool isdobj);
typedef bool(__thiscall* InvokeFn)(addr iface, addr pdata, GValue* result, const char* name, const GValue* args, uint32_t nargs, bool isdobj);
typedef void(__thiscall* ReleaseFn)(addr iface, GValue* v, addr pdata);
typedef void(__thiscall* CreateObjectFn)(addr asRoot, GValue* out, const char* cls, const GValue* args, uint32_t nargs);
typedef void(__thiscall* CreateArrayFn)(addr asRoot, GValue* out);
typedef void(__thiscall* CreateFunctionFn)(addr asRoot, GValue* out, void* handler, void* userData);
typedef addr(__thiscall* FindOverlayFn)(addr mgr, const char* name);
typedef void(__thiscall* MovieThreadFn)(addr movie, DWORD tid);
typedef addr(__thiscall* MovieGetFn)(addr movie);
typedef addr(__cdecl* SteamGetFn)();
typedef const char*(__thiscall* GameLangFn)(addr self);
typedef void(__thiscall* SetStateFn)(addr screen, int state, bool animate);
template <class F> F VSlot(addr obj, addr off) { return reinterpret_cast<F>(rp(rp(obj) + off)); }

bool IsDObj(const GValue& v) { return (v.type & VTC_TypeMask) == VT_DisplayObject; }
bool IsObj(const GValue& v) {
  const uint32_t t = v.type & 0x0F;
  return v.iface && (t == VT_Object || t == VT_Array || t == VT_DisplayObject || t == VT_Closure);
}
void Rel(GValue& v) {
  if ((v.type & VTC_Managed) && v.iface) VSlot<ReleaseFn>(v.iface, 8)(v.iface, &v, v.v[0]);
  v = GValue{};
}
bool Get(const GValue& o, const char* name, GValue& out) {
  Rel(out);
  return IsObj(o) && VSlot<GetMemberFn>(o.iface, 0x10)(o.iface, o.v[0], name, &out, IsDObj(o));
}
bool Set(const GValue& o, const char* name, const GValue& v) {
  return IsObj(o) && VSlot<SetMemberFn>(o.iface, 0x14)(o.iface, o.v[0], name, &v, IsDObj(o));
}
bool Call(const GValue& o, const char* name, const GValue* args = nullptr, uint32_t n = 0) {
  GValue tmp{};
  const bool ok = IsObj(o) && VSlot<InvokeFn>(o.iface, 0x18)(o.iface, o.v[0], &tmp, name, args, n, IsDObj(o));
  Rel(tmp);
  return ok;
}
GValue Str(const char* s) { GValue v{}; v.type = VT_String; v.v[0] = addr(s); return v; }
GValue Int(int i) { GValue v{}; v.type = VT_Int; v.v[0] = uint32_t(i); return v; }
GValue Bool(bool b) { GValue v{}; v.type = VT_Boolean; v.v[0] = b ? 1u : 0u; return v; }
int AsInt(const GValue& v) {
  switch (v.type & 0x0F) {
    case VT_Int: case VT_UInt: return int(v.v[0]);
    case VT_Number: { double d; memcpy(&d, v.v, 8); return int(d); }
    default: return -1;
  }
}

// ---------------- язык подписей: как у игры (Steam), иначе как у мода ----------------
int s_ru = -1;
bool Ru() {
  if (s_ru < 0) {
    s_ru = g_lang ? 0 : 1;
    if (HMODULE dll = GetModuleHandleA("steam_api.dll"))
      if (auto f = reinterpret_cast<SteamGetFn>(reinterpret_cast<void*>(GetProcAddress(dll, "SteamApps"))))
        if (const addr apps = f()) {
          const char* l = VSlot<GameLangFn>(apps, 4 * 4)(apps);  // ISteamApps::GetCurrentGameLanguage
          if (l && *l) s_ru = strcmp(l, "russian") == 0 ? 1 : 0, Log("game menu: game language %s", l);
        }
  }
  return s_ru == 1;
}
const char* L(const char* ru, const char* en) { return Ru() ? ru : en; }

// ---------------- наши пункты ----------------
constexpr int kFriends = 8, kPlayers = kMpMaxClients, kViews = kMpMaxClients + 1;
enum ItemId {
  I_ROOT,       // «Совместная игра» — подменю в корне меню игры
  I_STATUS,     // состояние (нажатие ничего не делает, описание — подробности)
  I_HOST,       // создать лобби
  I_JOIN,       // подменю «К другу»
  I_FRIEND0, I_LAST = I_FRIEND0 + kFriends, I_CLIP,
  I_MODE, I_MISSION, I_MAP, I_DIFF,  // переключатели хоста
  I_ROLE,       // роль — экран выбора персонажа
  I_START,      // начать игру
  I_PLAYERS,    // подменю игроков
  I_PL0,        // хост: игрок k — подменю (выгнать / забанить)
  I_KICK0 = I_PL0 + kPlayers, I_BAN0 = I_KICK0 + kPlayers,
  I_PV0 = I_BAN0 + kPlayers,  // клиент: игроки лобби (только посмотреть)
  I_CLOSE = I_PV0 + kViews,   // хост: закрыть лобби
  I_LEAVE,      // клиент: выйти из лобби / отменить подключение
  I_COUNT
};
enum Kind { K_BTN, K_EXP, K_STEP };
struct Spec { Kind kind; int parent; };  // parent — подменю, где лежит пункт (-1 — корень меню игры)
Spec SpecOf(int i) {
  if (i == I_ROOT) return { K_EXP, -1 };
  if (i == I_JOIN || i == I_PLAYERS) return { K_EXP, I_ROOT };
  if (i >= I_FRIEND0 && i <= I_CLIP) return { K_BTN, I_JOIN };
  if (i == I_MODE || i == I_MISSION || i == I_MAP || i == I_DIFF) return { K_STEP, I_ROOT };
  if (i >= I_PL0 && i < I_PL0 + kPlayers) return { K_EXP, I_PLAYERS };
  if (i >= I_KICK0 && i < I_KICK0 + kPlayers) return { K_BTN, I_PL0 + (i - I_KICK0) };
  if (i >= I_BAN0 && i < I_BAN0 + kPlayers) return { K_BTN, I_PL0 + (i - I_BAN0) };
  if (i >= I_PV0 && i < I_PV0 + kViews) return { K_BTN, I_PLAYERS };
  return { K_BTN, I_ROOT };
}
// порядок на странице: корень — состояние, создать / к другу, режим..., роль, начать, игроки, закрыть / выйти
const int kOrder[] = { I_ROOT, I_STATUS, I_HOST, I_JOIN, I_MODE, I_MISSION, I_MAP, I_DIFF, I_ROLE, I_START, I_PLAYERS, I_CLOSE, I_LEAVE };
constexpr int kIdBase = 0x41500;  // MenuItem.data наших пунктов: kIdBase + ItemId

struct MenuKind {
  const char* overlay;   // имя экрана-оверлея
  int index;             // куда вставить в корневую страницу
  int gameState;         // в каком состоянии игры этот экран можно трогать (пауза — 4, главное меню — 2)
  bool pause;            // меню паузы: только состояние, игроки, выйти / закрыть
  // живое (только пока тот же Movie)
  addr screen = 0, movie = 0, rootData = 0;
  bool injected = false, failed = false;
  GValue menu{}, items[I_COUNT]{}, fns[I_COUNT]{};  // fns — держим, пока жив Movie (подписка у MenuItem слабая)
  std::string label[I_COUNT], desc[I_COUNT];
  bool shown[I_COUNT]{};
  int step[I_COUNT]{};    // что стоит в переключателе
  std::vector<std::string> mapIds;  // значения переключателя карты (id плейлистов выживания на момент вставки)
  ms_t refreshAt = 0, quietUntil = 0;
};
MenuKind s_menus[2] = { { "pause_menu", 2, 4, true }, { "NEW_FRONTEND_MENU", 1, 2, false } };
// состояние игры: *(*(G0)+0x48)+8 — 2 меню, 4 уровень, 5 конец игры, 6 загрузка
int GameState() {
  int st = 0;
  Safe([&] {
    const addr g0 = rp(VA(va::GameGlobals));
    const addr game = g0 ? rp(g0 + 0x48) : 0;
    st = game ? rd<int>(game + 8) : 0;
  });
  return st;
}
// тик, закрытие экрана и выбор персонажа могут прийти из разных потоков — состояние меню трогает кто-то один
std::atomic<bool> s_busy{ false };
std::atomic<DWORD> s_uiTid{ 0 };            // поток интерфейса игры (тот, что зовёт SteamAPI_RunCallbacks)
std::atomic<bool> s_releaseReq{ false };   // загрузка началась не в потоке интерфейса — тик отпустит объекты меню
struct Busy {
  bool ok;
  Busy() : ok(!s_busy.exchange(true)) {}
  ~Busy() {
    if (ok) s_busy = false;
  }
};

addr Overlay(const char* name) {
  const addr mgr = rp(VA(gva::UiManager));
  return mgr ? Fn<FindOverlayFn>(gva::FindOverlay)(mgr, name) : 0;
}
// AS3::MovieRoot экрана — среди полей Movie указатель на объект с его vtable (смещение запоминаем)
addr s_asRootOff = 0;
addr AsRoot(addr movie) {
  const addr vt = VA(gva::AsMovieRootVt);
  if (s_asRootOff) {
    addr p = 0, pv = 0;
    if (SafeRead(movie + s_asRootOff, p) && p && SafeRead(p, pv) && pv == vt) return p;
  }
  for (addr off = 4; off < 0x800; off += 4) {
    addr p = 0, pv = 0;
    if (!SafeRead(movie + off, p) || !p || (p & 3)) continue;
    if (!SafeRead(p, pv) || pv != vt) continue;
    if (s_asRootOff != off) Log("game menu: AS3 MovieRoot at Movie+0x%X", unsigned(off));
    s_asRootOff = off;
    return p;
  }
  return 0;
}
// захватить Movie для этого потока, как игра перед вызовами в AS3; без ожидания — если интерфейс занят, в другой раз
struct MovieLock {
  CRITICAL_SECTION* cs = nullptr;
  bool ok = false;
  explicit MovieLock(addr movie) {
    const addr mgr = rp(VA(gva::UiManager));
    if (!mgr || !movie) return;
    cs = reinterpret_cast<CRITICAL_SECTION*>(mgr + 4);
    if (!TryEnterCriticalSection(cs)) {
      cs = nullptr;
      return;
    }
    const DWORD tid = GetCurrentThreadId();
    ok = Safe([&] {
      VSlot<MovieThreadFn>(movie, 0x6c)(movie, tid);
      if (const addr h = VSlot<MovieGetFn>(movie, 0xc0)(movie)) wr<DWORD>(h + 0x14, tid);
    });
    if (!ok) LeaveCriticalSection(cs), cs = nullptr;
  }
  ~MovieLock() {
    if (cs) LeaveCriticalSection(cs);
  }
};

void Forget(MenuKind& k) {  // Movie ушёл: ссылки не отпускаем (их куча ушла вместе с ним)
  k.menu = GValue{};
  for (GValue& v : k.items) v = GValue{};
  for (GValue& v : k.fns) v = GValue{};
  for (auto& s : k.label) s.clear();
  for (auto& s : k.desc) s.clear();
  k.mapIds.clear();
  k.injected = k.failed = false;
}
// отпустить все свои объекты AS3 этого экрана (тот же поток, что держит Movie)
void ReleaseAll(MenuKind& k) {
  Rel(k.menu);
  for (GValue& v : k.items) Rel(v);
  for (GValue& v : k.fns) Rel(v);
  Forget(k);
}

// ---------------- роли и персонажи ----------------
// персонажи выживания (Character1..9): портреты — как у игры (DATA\UI\DLC и UI.PAK), иконки — её же
struct CharCard { const char* image; const char* icon; };
const CharCard kChar[kMpCharCount] = { { "amanda.dds", "IconCharacter8" }, { "char_axel.dds", "IconCharacter8" },
                                       { "dallas.dds", "IconCharacter3" }, { "ellen.dds", "IconCharacter4" },
                                       { "char_hughes.dds", "IconCharacter6" }, { "char_lingard.dds", "IconCharacter6" },
                                       { "parker.dds", "IconCharacter7" }, { "char_ransome.dds", "IconCharacter8" },
                                       { "char_ricardo.dds", "IconCharacter8" } };
// кем человек начинает в этом режиме: в выживании — своим персонажем, в DLC «Ностромо» — их героиней, иначе Аманда
std::string HumanName(const MpSnap& m) {
  if (m.lobbyMode == MPM_SURVIVOR) return MpCharName(m.myChar);
  if (m.lobbyMode == MPM_RIPLEY) return L("Эллен Рипли", "Ellen Ripley");
  if (m.lobbyMode == MPM_TWOTEAMS) return L("экипаж «Ностромо»", "the Nostromo crew");
  return L("Аманда Рипли", "Amanda Ripley");
}
std::string RoleName(const MpSnap& m, int avatar, int chr) {
  if (avatar == AV_ALIEN) return L("Чужой", "the Alien");
  if (avatar == AV_ANDROID) return L("рабочий Джо (андроид)", "Working Joe (android)");
  if (avatar == AV_FACEHUGGER) return L("лицехват", "facehugger");
  if (m.lobbyMode == MPM_SURVIVOR) return MpCharName(chr);
  return HumanName(m);
}
const char* RoleDesc(int avatar) {
  switch (avatar) {
    case AV_ALIEN:
      return L("ОХОТНИК. Ходит и по вентиляции, слышит шум, убивает одним ударом — ЛКМ. Касанием не убивает: только по кнопке. "
               "Камера за спиной или из головы (V).",
               "THE HUNTER. Moves through the vents too, hears noise, kills with one strike — LMB. Never kills by touch: only "
               "on the button. Camera behind or from the head (V).");
    case AV_FACEHUGGER:
      return L("ПАРАЗИТ. Маленький и быстрый. ЛКМ — прыжок на лицо жертвы. Есть не на каждом уровне — если его нет, роль "
               "подождёт, пока он появится.",
               "THE PARASITE. Small and fast. LMB — leap onto the victim's face. Not on every level — if there is none, the role "
               "waits until one appears.");
    case AV_ANDROID:
      return L("СИНТЕТИК SEEGSON. Медленный, но сильный: хватает и душит. Огнемёт его почти не берёт. Есть на большинстве "
               "уровней станции.",
               "SEEGSON SYNTHETIC. Slow but strong: grabs and strangles. Fire barely hurts it. Found on most levels of the "
               "station.");
    default:
      return L("ЧЕЛОВЕК. Прячься, чини, беги — как в обычной игре. Другие видят тебя персонажем уровня (в DLC — тем же "
               "героем в третьем лице).",
               "HUMAN. Hide, repair, run — as in the normal game. Others see you as a character of the level (in the DLC — the "
               "same hero in third person).");
  }
}

// ---------------- что показать ----------------
struct Want {
  std::string label[I_COUNT], desc[I_COUNT];
  bool shown[I_COUNT];
  int step[I_COUNT];
  std::vector<std::string> stepLabels[I_COUNT];  // подписи значений переключателей (только при вставке)
};
bool HasMission(uint32_t mask, int n) { return n >= 1 && n <= kMpMissionCount && ((mask >> n) & 1); }
Want Compute(const MenuKind& k, bool withLabels) {
  const MpSnap m = MpSnapshot();
  Want w;
  for (int i = 0; i < I_COUNT; i++) w.shown[i] = false, w.step[i] = -1, w.label[i] = "-";
  char b[256];
  const bool host = m.role == MP_HOST && m.lobbyOpen;
  const bool client = m.role == MP_CLIENT && m.state != MPS_IDLE;
  const bool idle = !host && !client;
  const bool menu = !k.pause;
  // общая строка — состояние; её описание — что сейчас в лобби
  std::string sum;
  if (host || client) {
    if (m.lobbyMode == MPM_SURVIVOR) std::snprintf(b, sizeof b, "%s: %s", MpModeName(m.lobbyMode), m.lobbyMap.empty() ? "?" : MpMapName(m.lobbyMap).c_str());
    else if (m.lobbyMode == MPM_MISSION) std::snprintf(b, sizeof b, L("Кампания: %s", "Campaign: %s"), MpMissionName(m.lobbyMission));
    else std::snprintf(b, sizeof b, "%s, %s", MpModeName(m.lobbyMode), MpDiffName(m.lobbyDiff));
    sum = b;
    sum += "\n";
    if (host) {
      std::snprintf(b, sizeof b, L("Ты (хост) — %s", "You (host) — %s"), RoleName(m, m.avatar, m.myChar).c_str());
      sum += b;
      for (const MpMember& p : m.members) {
        std::snprintf(b, sizeof b, "\n%s — %s", p.name.empty() ? "?" : p.name.c_str(), RoleName(m, p.avatar, p.chr).c_str());
        sum += b;
      }
    } else {
      for (const MpMember& p : m.members) {
        std::snprintf(b, sizeof b, "%s%s%s — %s\n", p.name.empty() ? "?" : p.name.c_str(), p.host ? L(" (хост)", " (host)") : "",
                      p.id == m.me ? L(" (ты)", " (you)") : "", RoleName(m, p.avatar, p.chr).c_str());
        sum += b;
      }
    }
  }
  const std::string status = m.status.empty() ? std::string() : "\n\n" + m.status;
  w.label[I_ROOT] = L("Совместная Игра", "Co-op"), w.shown[I_ROOT] = true;
  w.desc[I_ROOT] = L("Игра вдвоём и больше через Steam: один создаёт лобби, остальные заходят к нему. Кто-то — человек, "
                     "кто-то — Чужой.",
                     "Two or more players over Steam: one creates a lobby, the others join. Someone is a human, someone is the "
                     "Alien.");
  w.shown[I_STATUS] = true;
  if (idle) {
    w.label[I_STATUS] = L("Ты не в лобби", "You're not in a lobby");
    w.desc[I_STATUS] = std::string(L("Создай лобби — друзья зайдут к тебе. Или зайди к другу.", "Create a lobby — friends join you. Or join a friend.")) + status;
  } else if (host) {
    std::snprintf(b, sizeof b, m.started ? L("Игра идёт · игроков %d", "Game running · %d players") : L("Лобби открыто · игроков %d", "Lobby open · %d players"),
                  int(m.members.size()) + 1);
    w.label[I_STATUS] = b;
    w.desc[I_STATUS] = sum + status;
  } else {
    const char* who = m.peerName.empty() ? "?" : m.peerName.c_str();
    if (m.state == MPS_CONNECTING) std::snprintf(b, sizeof b, L("Подключаюсь к %s…", "Connecting to %s…"), who);
    else if (m.state == MPS_LOST) std::snprintf(b, sizeof b, L("Связь с %s пропала — переподключаюсь", "Lost %s — reconnecting"), who);
    else if (m.started) std::snprintf(b, sizeof b, L("Лобби %s · игра идёт", "%s's lobby · game running"), who);
    else std::snprintf(b, sizeof b, L("Лобби %s · ждём начала", "%s's lobby · waiting for the start"), who);
    w.label[I_STATUS] = b;
    w.desc[I_STATUS] = sum + status;
  }
  // не в лобби: создать / к другу
  if (idle && menu) {
    w.shown[I_HOST] = true, w.label[I_HOST] = L("Создать лобби", "Create a lobby");
    w.desc[I_HOST] = L("Ты — хост: выбираешь режим, миссию или карту и нажимаешь «Начать игру». Друзья заходят к тебе через "
                       "«Присоединиться к другу» (ты должен быть у них в друзьях Steam) или по твоему Steam ID.",
                       "You're the host: pick the mode, mission or map and press «Start game». Friends join you via «Join a "
                       "friend» (you must be their Steam friend) or by your Steam ID.");
    w.shown[I_JOIN] = true, w.label[I_JOIN] = L("Присоединиться к другу", "Join a friend");
    w.desc[I_JOIN] = L("Друзья Steam, которые сейчас в Alien: Isolation, — или Steam ID хоста из буфера обмена.",
                       "Steam friends who are in Alien: Isolation right now — or the host's Steam ID from the clipboard.");
    for (size_t i = 0; i < m.friends.size() && i < size_t(kFriends); i++) {
      const int id = I_FRIEND0 + int(i);
      w.shown[id] = true;
      std::snprintf(b, sizeof b, L("Войти к: %s", "Join: %s"), m.friends[i].name.empty() ? "?" : m.friends[i].name.c_str());
      w.label[id] = b;
      w.desc[id] = L("Друг сейчас в игре. Он должен создать лобби (Совместная игра → Создать лобби).",
                     "Your friend is in the game now. They must create a lobby (Co-op → Create a lobby).");
    }
    if (m.peer) {
      w.shown[I_LAST] = true;
      std::snprintf(b, sizeof b, L("Последний хост: %s", "Last host: %s"), m.peerName.empty() ? std::to_string(m.peer).c_str() : m.peerName.c_str());
      w.label[I_LAST] = b;
      w.desc[I_LAST] = L("Подключиться к тому, у кого был в прошлый раз.", "Connect to the host you joined last time.");
    }
    w.shown[I_CLIP] = true, w.label[I_CLIP] = L("Steam ID из буфера обмена", "Steam ID from the clipboard");
    w.desc[I_CLIP] = L("Скопируй адрес хоста (Steam ID, ссылку на профиль или код друга) и нажми — мод возьмёт его из буфера обмена.",
                       "Copy the host's address (Steam ID, profile link or friend code) and press — the mod takes it from the "
                       "clipboard.");
  }
  // хост: что запускаем
  if (host && menu) {
    w.shown[I_MODE] = true, w.label[I_MODE] = L("Режим", "Mode"), w.step[I_MODE] = m.lobbyMode;
    w.desc[I_MODE] = L("Кампания с начала или с выбранной миссии, DLC «Ностромо» или карты выживания. У всех запускается одно и то же.",
                       "The campaign from the start or from a mission, the Nostromo DLC or Survivor maps. Everyone launches the same.");
    if (m.lobbyMode == MPM_MISSION) {
      w.shown[I_MISSION] = true, w.label[I_MISSION] = L("Миссия", "Mission"), w.step[I_MISSION] = m.lobbyMission - 1;
      w.desc[I_MISSION] = HasMission(m.myMissions, m.lobbyMission)
                              ? L("Начало миссии из сохранения (как «Загрузить → миссия»). У каждого — своё: миссию надо хоть раз начать в своей кампании.",
                                  "The mission start from its save (like «Load → mission»). Everyone loads their own: the mission must have been started in their campaign.")
                              : L("У тебя нет сохранения этой миссии — начни её в кампании или выбери другую.",
                                  "You have no save of this mission — start it in the campaign or pick another.");
    }
    if (m.lobbyMode == MPM_SURVIVOR) {
      w.shown[I_MAP] = true, w.label[I_MAP] = L("Карта", "Map");
      for (size_t i = 0; i < k.mapIds.size(); i++)
        if (k.mapIds[i] == m.lobbyMap) w.step[I_MAP] = int(i);
      w.desc[I_MAP] = L("Карта выживания из DLC. Каждый игрок-человек выбирает себе персонажа сам — в «Роль».",
                        "A Survivor map from the DLC. Each human player picks their own character — in «Role».");
    }
    if (m.lobbyMode == MPM_CAMPAIGN || m.lobbyMode == MPM_RIPLEY || m.lobbyMode == MPM_TWOTEAMS) {
      w.shown[I_DIFF] = true, w.label[I_DIFF] = L("Сложность", "Difficulty"), w.step[I_DIFF] = m.lobbyDiff;
      w.desc[I_DIFF] = L("Сложность у всех одинаковая.", "Everyone gets the same difficulty.");
    }
    const bool can = m.lobbyMode == MPM_SURVIVOR ? !m.lobbyMap.empty() : m.lobbyMode == MPM_MISSION ? HasMission(m.myMissions, m.lobbyMission) : true;
    w.shown[I_START] = true;
    w.label[I_START] = !can ? L("Начать игру — нет сохранения миссии", "Start game — no save of the mission")
                       : m.started ? L("Запустить заново", "Launch again")
                                   : L("НАЧАТЬ ИГРУ", "START GAME");
    w.desc[I_START] = L("Режим запустится у тебя и у всех в лобби. Кто зайдёт позже — запустит его сразу.",
                        "The mode launches for you and everyone in the lobby. Whoever joins later launches it right away.");
    w.shown[I_CLOSE] = true, w.label[I_CLOSE] = L("Закрыть лобби", "Close the lobby");
    w.desc[I_CLOSE] = L("Все игроки выйдут из лобби.", "Everyone leaves the lobby.");
  }
  if ((host || client) && menu) {
    const bool here = (m.lobbyRoles >> m.avatar) & 1;
    std::snprintf(b, sizeof b, here ? L("Роль: %s", "Role: %s") : L("Роль: %s — её нет в этом режиме", "Role: %s — not in this mode"),
                  RoleName(m, m.avatar, m.myChar).c_str());
    w.shown[I_ROLE] = true, w.label[I_ROLE] = b;
    w.desc[I_ROLE] = std::string(RoleDesc(m.avatar)) +
                     L("\n\nНажми — выбор роли: человек (в выживании — свой персонаж) или Чужой, лицехват, андроид.",
                       "\n\nPress — pick a role: a human (in Survivor — your own character) or the Alien, a facehugger, an android.");
  }
  if (client) {
    w.shown[I_LEAVE] = true;
    w.label[I_LEAVE] = m.state == MPS_CONNECTING ? L("Отменить подключение", "Cancel connecting") : L("Выйти из лобби", "Leave the lobby");
    w.desc[I_LEAVE] = L("Вернуться в одиночную игру.", "Back to single player.");
  }
  if (host && k.pause) w.shown[I_CLOSE] = true, w.label[I_CLOSE] = L("Закрыть лобби", "Close the lobby");
  // игроки
  if (host || (client && m.state == MPS_CONNECTED)) {
    const int n = host ? int(m.members.size()) + 1 : int(m.members.size());
    std::snprintf(b, sizeof b, L("Игроки (%d)", "Players (%d)"), n);
    w.shown[I_PLAYERS] = true, w.label[I_PLAYERS] = b, w.desc[I_PLAYERS] = sum;
    if (host) {
      for (size_t i = 0; i < m.members.size() && i < size_t(kPlayers); i++) {
        const MpMember& p = m.members[i];
        char ping[32] = "";
        if (p.pingMs >= 0) std::snprintf(ping, sizeof ping, L(", пинг %d", ", ping %d"), p.pingMs);
        std::snprintf(b, sizeof b, "%s — %s%s", p.name.empty() ? "?" : p.name.c_str(), RoleName(m, p.avatar, p.chr).c_str(), ping);
        const int pl = I_PL0 + int(i);
        w.shown[pl] = true, w.label[pl] = b;
        w.desc[pl] = std::string(RoleDesc(p.avatar)) +
                     (m.lobbyMode == MPM_MISSION && !HasMission(p.missions, m.lobbyMission)
                          ? L("\n\nУ него нет сохранения выбранной миссии — у него она не запустится.", "\n\nThey have no save of the picked mission — it won't start for them.")
                          : "");
        w.shown[I_KICK0 + int(i)] = true, w.label[I_KICK0 + int(i)] = L("Выгнать", "Kick");
        w.desc[I_KICK0 + int(i)] = L("Выгнать из лобби (сможет зайти снова).", "Kick from the lobby (can join again).");
        w.shown[I_BAN0 + int(i)] = true, w.label[I_BAN0 + int(i)] = L("Забанить", "Ban");
        w.desc[I_BAN0 + int(i)] = L("Выгнать и больше не пускать (снять бан — в меню мода).", "Kick and never let back in (unban — in the mod menu).");
      }
    } else {
      for (size_t i = 0; i < m.members.size() && i < size_t(kViews); i++) {
        const MpMember& p = m.members[i];
        std::snprintf(b, sizeof b, "%s%s%s — %s", p.name.empty() ? "?" : p.name.c_str(), p.host ? L(" (хост)", " (host)") : "",
                      p.id == m.me ? L(" (ты)", " (you)") : "", RoleName(m, p.avatar, p.chr).c_str());
        w.shown[I_PV0 + int(i)] = true, w.label[I_PV0 + int(i)] = b, w.desc[I_PV0 + int(i)] = RoleDesc(p.avatar);
      }
    }
  }
  // переключатели — подписи значений (только при вставке)
  if (withLabels) {
    for (int i = 0; i < kMpModeCount; i++) w.stepLabels[I_MODE].push_back(MpModeName(i));
    for (int i = 1; i <= kMpMissionCount; i++) w.stepLabels[I_MISSION].push_back(MpMissionName(i));
    for (int i = 0; i < kMpDiffCount; i++) w.stepLabels[I_DIFF].push_back(MpDiffName(i));
  }
  return w;
}

// применить (только то, что поменялось); внутри AS3 или под MovieLock
void Apply(MenuKind& k, const Want& w) {
  bool dirty = false;
  for (int i = 0; i < I_COUNT; i++) {
    GValue& it = k.items[i];
    if (!IsObj(it)) continue;
    if (k.label[i] != w.label[i]) Set(it, "name", Str(w.label[i].c_str())), k.label[i] = w.label[i], dirty = true;
    if (k.shown[i] != w.shown[i]) Set(it, "visible", Bool(w.shown[i])), k.shown[i] = w.shown[i], dirty = true;
    if (k.desc[i] != w.desc[i]) Set(it, "description", Str(w.desc[i].c_str())), k.desc[i] = w.desc[i], dirty = true;
    if (SpecOf(i).kind == K_STEP && w.step[i] >= 0 && k.step[i] != w.step[i]) {
      Set(it, "selectedIndex", Int(w.step[i]));  // сеттер сам глушит событие смены
      k.step[i] = w.step[i], dirty = true;
    }
  }
  if (dirty) Call(k.menu, "invalidateRenderers"), Call(k.menu, "refresh");
}
MenuKind* KindOf(addr movie) {
  for (MenuKind& m : s_menus)
    if (m.injected && m.movie == movie) return &m;
  return nullptr;
}

// ---------------- экран выбора роли (экран выбора персонажа DLC) ----------------
// 1 — роли (человек, Чужой, лицехват, андроид), 2 — персонаж-человек выживания; 0 — экран не наш
std::atomic<int> s_pick{ 0 };
std::atomic<addr> s_pickScreen{ 0 };
GValue s_pickRoot{};  // корень меню, где открыт экран (копия Value экрана, не отпускаем)
void SetFrontendState(addr screen, int state) { Fn<SetStateFn>(gva::ScreenSetState)(screen, state, true); }
void AddCard(addr as, GValue& arr, const char* id, const std::string& name, const char* desc, const char* icon, const char* image) {
  GValue cd{};
  VSlot<CreateObjectFn>(as, 0xb8)(as, &cd, "dlc.utils.CharacterData", nullptr, 0);
  if (!IsObj(cd)) return;
  Set(cd, "id", Str(id));
  Set(cd, "name", Str(name.c_str()));
  Set(cd, "description", Str(desc));
  Set(cd, "icon", Str(icon));
  Set(cd, "image", Str(image));
  Call(arr, "push", &cd, 1);
  Rel(cd);
}
bool OpenPicker(MenuKind& k, int what) {  // внутри AS3 (нажатие) или под MovieLock
  const addr as = AsRoot(k.movie);
  const addr vp = k.screen ? rp(k.screen + 0x68) : 0;
  if (!as || !vp) return false;
  const GValue root = *reinterpret_cast<GValue*>(vp);
  GValue arr{};
  VSlot<CreateArrayFn>(as, 0xbc)(as, &arr);
  if (!IsObj(arr)) return false;
  const MpSnap m = MpSnapshot();
  if (what == 1) {
    const std::string human = std::string(L("Человек — ", "Human — ")) + HumanName(m);
    const int hc = m.lobbyMode == MPM_SURVIVOR ? m.myChar : m.lobbyMode == MPM_RIPLEY ? 4 : 1;
    AddCard(as, arr, "MP_R0", human, RoleDesc(AV_HUMAN), kChar[hc - 1].icon, kChar[hc - 1].image);
    // только те, кто есть на уровнях выбранного режима (миссии, карты) — по моделям уровня (mp.cpp, ModeRoles)
    if ((m.lobbyRoles >> AV_ALIEN) & 1) AddCard(as, arr, "MP_R1", L("Чужой", "The Alien"), RoleDesc(AV_ALIEN), "", "mp_alien.dds");
    if ((m.lobbyRoles >> AV_FACEHUGGER) & 1)
      AddCard(as, arr, "MP_R3", L("Лицехват", "Facehugger"), RoleDesc(AV_FACEHUGGER), "", "mp_hugger.dds");
    if ((m.lobbyRoles >> AV_ANDROID) & 1) AddCard(as, arr, "MP_R2", L("Рабочий Джо", "Working Joe"), RoleDesc(AV_ANDROID), "", "mp_android.dds");
  } else {
    char id[16];
    for (int c = 1; c <= kMpCharCount; c++) {
      std::snprintf(id, sizeof id, "MP_C%d", c);
      AddCard(as, arr, id, MpCharName(c), L("Персонаж выживания — так тебя увидят другие игроки.", "A Survivor character — this is how the others see you."),
              kChar[c - 1].icon, kChar[c - 1].image);
    }
  }
  s_pick = what, s_pickScreen = k.screen, s_pickRoot = root;
  const bool ok = Call(root, "setCharacterSelection", &arr, 1);
  Rel(arr);
  Log("game menu: role picker %d opened (%s)", what, ok ? "ok" : "setCharacterSelection failed");
  if (!ok) s_pick = 0;
  return ok;
}
// выбор на экране (request_gameplay_start -> FUN_00823d50(экран, id)): наши карточки «MP_…» — себе, игре — нет
void PickMade(addr screen, const char* id);
void OnGameplayStart(Invocation& inv) {  // поток интерфейса, внутри AS3
  char id[16] = {};
  if (!Safe([&] { const char* p = reinterpret_cast<const char*>(inv.arg[0]); for (int i = 0; i < 15 && p[i]; i++) id[i] = p[i]; })) return;
  if (strncmp(id, "MP_", 3) != 0) return;
  inv.skip = true, inv.skipPop = 4;
  const addr screen = inv.ecx;
  if (!Safe([&] { PickMade(screen, id); })) Log("game menu: access violation after a role pick");
}
void PickMade(addr screen, const char* id) {
  const MpSnap m = MpSnapshot();
  bool again = false;
  if (id[3] == 'R') {
    const int av = id[4] - '0';
    MpCmd(MPA_AVATAR, av, 0);
    Log("game menu: role picked — %d", av);
    again = av == AV_HUMAN && m.lobbyMode == MPM_SURVIVOR;  // человек в выживании — дальше выбрать персонажа
  } else if (id[3] == 'C') {
    MpCmd(MPA_AVATAR, AV_HUMAN, 0);
    MpCmd(MPA_MYCHAR, atoi(id + 4), 0);
    Log("game menu: survivor character picked — %s", id + 4);
  }
  Busy busy;
  MenuKind* k = nullptr;
  for (MenuKind& mk : s_menus)
    if (mk.injected && mk.screen == screen) k = &mk;
  if (again && k && OpenPicker(*k, 2)) return;
  s_pick = 0;
  Safe([&] { SetFrontendState(screen, FE_MENU); });
  if (k && busy.ok) Apply(*k, Compute(*k, false));
}
// «назад» с нашего экрана: игра просит выбор карты (DLC_MAP) — уводим в меню
void OnSetState(Invocation& inv) {
  if (!s_pick.load() || inv.ecx != s_pickScreen.load() || int(inv.arg[0]) != FE_DLC_MAP) return;
  inv.args[0] = FE_MENU;
  s_pick = 0;
  Log("game menu: role picker closed (back)");
}

// ---------------- нажатия ----------------
uint64_t ClipboardId() {
  uint64_t id = 0;
  if (!OpenClipboard(nullptr)) return 0;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
      char a[256];
      int n = 0;
      for (; n < 255 && w[n]; n++) a[n] = w[n] < 128 ? char(w[n]) : '?';
      a[n] = 0;
      id = MpParseId(a);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return id;
}
void Pressed(MenuKind* k, int id) {  // внутри AS3 — Movie уже наш
  const MpSnap m = MpSnapshot();
  if (id == I_HOST) MpCmd(MPA_LOBBY_OPEN, 0, 0);
  else if (id >= I_FRIEND0 && id < I_FRIEND0 + kFriends && size_t(id - I_FRIEND0) < m.friends.size())
    MpCmd(MPA_CONNECT, 0, m.friends[size_t(id - I_FRIEND0)].id);
  else if (id == I_LAST && m.peer) MpCmd(MPA_CONNECT, 0, m.peer);
  else if (id == I_CLIP) {
    const uint64_t to = ClipboardId();
    if (to && to != m.me) MpCmd(MPA_CONNECT, 0, to);
    else Log("game menu: no Steam ID in the clipboard");
  } else if (id == I_ROLE && k) OpenPicker(*k, 1);
  else if (id == I_START) MpCmd(MPA_START, 0, 0);
  else if (id == I_CLOSE) MpCmd(MPA_LOBBY_CLOSE, 0, 0);
  else if (id == I_LEAVE) MpCmd(MPA_DISCONNECT, 0, 0);
  else if (id >= I_KICK0 && id < I_KICK0 + kPlayers && size_t(id - I_KICK0) < m.members.size())
    MpCmd(MPA_KICK, 0, m.members[size_t(id - I_KICK0)].id);
  else if (id >= I_BAN0 && id < I_BAN0 + kPlayers && size_t(id - I_BAN0) < m.members.size())
    MpCmd(MPA_BAN, 0, m.members[size_t(id - I_BAN0)].id);
  else if (k && SpecOf(id).kind == K_STEP) {  // переключатель: новое значение
    GValue v{};
    const int idx = Get(k->items[id], "selectedIndex", v) ? AsInt(v) : -1;
    Rel(v);
    if (idx < 0) return;
    k->step[id] = idx;
    if (id == I_MODE) MpCmd(MPA_MODE, idx, 0);
    else if (id == I_MISSION) MpCmd(MPA_MISSION, idx + 1, 0);
    else if (id == I_DIFF) MpCmd(MPA_DIFF, idx, 0);
    else if (id == I_MAP && size_t(idx) < k->mapIds.size()) MpSetSurvivorMap(k->mapIds[size_t(idx)]);
  } else {
    return;
  }
  Log("game menu: item %d in %s", id, k ? k->overlay : "?");
  if (k) Apply(*k, Compute(*k, false));  // подписи — сразу
}
// GFx::FunctionHandler: {vtable, счётчик ссылок}; vt[0] — удаляющий деструктор (не удаляем: объект статический),
// vt[1] — Call(const Params&): Params[1] — Movie, Params[6] — userData (номер пункта)
struct PressHandler { const void* const* vt; volatile LONG refs; };
void* __thiscall PressDtor(PressHandler* self, unsigned) { return self; }
void __thiscall PressCall(PressHandler*, const uint32_t* params) {
  if (!params) return;
  const addr movie = params[1];
  const int id = int(params[6]);
  if (id <= I_ROOT || id >= I_COUNT) return;
  Busy busy;  // тик сейчас не трогает меню (тот же поток — значит, вложенно из Apply: не наше событие)
  if (!busy.ok) return;
  if (!Safe([&] { Pressed(KindOf(movie), id); })) Log("game menu: access violation in a button handler");
}
const void* const kPressVt[2] = { reinterpret_cast<const void*>(&PressDtor), reinterpret_cast<const void*>(&PressCall) };
PressHandler s_press{ kPressVt, 1 << 24 };

// ---------------- вставка ----------------
void Inject(MenuKind& k, const GValue& root) {
  const addr as = AsRoot(k.movie);
  GValue rootPage{};
  const bool gm = Get(root, "menu", k.menu), gp = gm && Get(k.menu, "rootPage", rootPage);
  if (!as || !gm || !gp) {
    Log("game menu: %s — asRoot %08X, root type %X; menu %d (type %X), rootPage %d (type %X)", k.overlay, unsigned(as),
        unsigned(root.type), int(gm), unsigned(k.menu.type), int(gp), unsigned(rootPage.type));
    k.failed = true;
    Rel(rootPage);
    return;
  }
  if (!k.pause) {  // карты выживания — значения переключателя
    k.mapIds.clear();
    for (const MpMap& mp : MpSurvivorMaps()) k.mapIds.push_back(mp.id);
  }
  Want w = Compute(k, true);
  for (const std::string& id : k.mapIds) w.stepLabels[I_MAP].push_back(MpMapName(id));
  if (w.stepLabels[I_MAP].empty()) w.stepLabels[I_MAP].push_back(L("карт выживания нет (DLC)", "no Survivor maps (DLC)"));
  GValue pages[I_COUNT]{};
  int made = 0;
  auto create = [&](int i) {
    const Spec sp = SpecOf(i);
    const GValue& page = sp.parent < 0 ? rootPage : pages[sp.parent];
    if (!IsObj(page) && sp.parent >= 0) return;
    if (sp.kind == K_EXP) {
      const GValue a[1] = { Str(w.label[i].c_str()) };
      VSlot<CreateObjectFn>(as, 0xb8)(as, &k.items[i], "frontend.menu.MenuItemExpandable", a, 1);
      if (IsObj(k.items[i])) Get(k.items[i], "page", pages[i]);
    } else {
      VSlot<CreateFunctionFn>(as, 0xc0)(as, &k.fns[i], &s_press, reinterpret_cast<void*>(intptr_t(i)));
      if (!IsObj(k.fns[i])) return;
      if (sp.kind == K_STEP) {
        GValue vals{}, labs{};
        VSlot<CreateArrayFn>(as, 0xbc)(as, &vals);
        VSlot<CreateArrayFn>(as, 0xbc)(as, &labs);
        for (size_t j = 0; j < w.stepLabels[i].size(); j++) {
          const GValue v = Int(int(j)), l = Str(w.stepLabels[i][j].c_str());
          Call(vals, "push", &v, 1), Call(labs, "push", &l, 1);
        }
        const GValue a[4] = { Str(w.label[i].c_str()), vals, labs, k.fns[i] };
        VSlot<CreateObjectFn>(as, 0xb8)(as, &k.items[i], "frontend.menu.MenuItemOptionOptionStepper", a, 4);
        Rel(vals), Rel(labs);
      } else {
        const GValue a[2] = { Str(w.label[i].c_str()), k.fns[i] };
        VSlot<CreateObjectFn>(as, 0xb8)(as, &k.items[i], "frontend.menu.MenuItem", a, 2);
      }
    }
    if (!IsObj(k.items[i])) {
      Log("game menu: %s — item %d (kind %d) not created", k.overlay, i, int(sp.kind));
      return;
    }
    Set(k.items[i], "data", Int(kIdBase + i));
    if (i == I_ROOT) {
      const GValue add[2] = { k.items[i], Int(k.index) };
      Call(rootPage, "addItemAtIndex", add, 2);
    } else {
      Call(page, "addItem", &k.items[i], 1);
    }
    k.label[i] = w.label[i], k.shown[i] = true, k.step[i] = 0;
    made++;
  };
  bool done[I_COUNT] = {};
  for (int i : kOrder) {  // корень и его страница — в нужном порядке; вложенное — сразу за своим подменю
    create(i), done[i] = true;
    if (i == I_JOIN)
      for (int j = I_FRIEND0; j <= I_CLIP; j++) create(j), done[j] = true;
    if (i == I_PLAYERS) {
      for (int p = 0; p < kPlayers; p++) {
        create(I_PL0 + p), done[I_PL0 + p] = true;
        create(I_KICK0 + p), done[I_KICK0 + p] = true;
        create(I_BAN0 + p), done[I_BAN0 + p] = true;
      }
      for (int v = 0; v < kViews; v++) create(I_PV0 + v), done[I_PV0 + v] = true;
    }
  }
  for (GValue& p : pages) Rel(p);
  Call(k.menu, "rebuildItemMap"), Call(k.menu, "rebuildPageMap");
  Apply(k, w);
  Call(k.menu, "refresh");
  Rel(rootPage);
  k.injected = true;
  Log("game menu: «%s» added to %s (%d items)", w.label[I_ROOT].c_str(), k.overlay, made);
}

ms_t s_tickAt = 0;
void MenuTick() {  // поток интерфейса игры, SteamAPI_RunCallbacks
  static bool once = false;
  if (!once) once = true, Log("game menu: tick alive (SteamAPI_RunCallbacks, thread %lu)", GetCurrentThreadId());
  s_uiTid = GetCurrentThreadId();
  const ms_t now = NowMs();
  const bool rel = s_releaseReq.exchange(false);
  if (now - s_tickAt < 100 && !rel) return;
  s_tickAt = now;
  Busy busy;
  if (!busy.ok) {
    if (rel) s_releaseReq = true;
    return;
  }
  const int st = GameState();
  if (rel)  // началась загрузка (не из потока интерфейса): свои объекты меню — отпустить сейчас, пока Movie жив
    for (MenuKind& k : s_menus) {
      if (!k.injected && !IsObj(k.menu)) continue;
      MovieLock lk(k.movie);
      if (lk.ok && Safe([&] { ReleaseAll(k); })) Log("game menu: %s — a load started, our items released", k.overlay);
      else Forget(k);
      k.movie = 0, k.quietUntil = now + 10000;
    }
  if (!MpGameMenuOn()) {  // кнопки выключили: свои объекты отпустить (пункты в меню игры останутся до его пересоздания)
    for (MenuKind& k : s_menus) {
      if (!k.injected && !IsObj(k.menu)) continue;
      if (st != k.gameState) {  // экрана уже нет (вышли из меню) — только забыть
        Forget(k), k.movie = 0;
        continue;
      }
      MovieLock lk(k.movie);
      if (!lk.ok) continue;
      if (!Safe([&] { ReleaseAll(k); })) Forget(k);
      k.movie = 0;
      Log("game menu: buttons off — our items in %s released", k.overlay);
    }
    return;
  }
  for (MenuKind& k : s_menus) {
    // игра не стоит спокойно в этом меню (грузится, другой экран) — не трогаем Movie вообще; свои объекты уже отпущены
    // при закрытии экрана (OnScreenClose), остались — отпускаем сейчас
    // игра ушла из этого меню (выход из уровня, загрузка, другой экран): Movie мог уже разрушиться — НЕ трогаем, только
    // забываем. Свои объекты отпускаются при закрытии экрана (OnScreenClose) и перед запуском режима (GameMenuReleaseNow).
    // 02.10 07:37: отпускание здесь попало в разрушенное меню паузы — ошибка доступа под замком интерфейса, замок остался
    // занятым, выход в главное меню повис
    if (st != k.gameState || now < k.quietUntil) {
      if (k.injected || IsObj(k.menu)) {
        bool released = false;
        if (!k.pause) {  // главное меню живёт и при загрузке (её и ждёт загрузка) — отпустить под замком
          MovieLock lk(k.movie);
          released = lk.ok && Safe([&] { ReleaseAll(k); });
        }
        Log("game menu: %s — the game left it (state %d), %s", k.overlay, st, released ? "our items released" : "forgotten");
        if (!released) Forget(k);
        k.movie = 0;
      }
      continue;
    }
    addr screen = 0, movie = 0;
    GValue root{};
    bool shown = false;
    if (!Safe([&] {
          screen = Overlay(k.overlay);
          movie = screen ? rp(screen + 8) : 0;
          const addr vp = movie ? rp(screen + 0x68) : 0;
          if (vp) root = *reinterpret_cast<GValue*>(vp);  // принадлежит экрану: не отпускаем
          shown = screen && rd<uint8_t>(screen + 0x28);
        }))
      continue;
    if (!IsObj(root)) {
      if (k.movie) Forget(k), k.movie = 0;
      continue;
    }
    if (screen != k.screen || movie != k.movie || root.v[0] != k.rootData) {
      Forget(k);
      k.screen = screen, k.movie = movie, k.rootData = root.v[0];
    }
    if (k.failed || !shown) continue;  // экран не показан — не трогаем
    if (!k.injected) {
      MovieLock lk(movie);
      if (lk.ok && !Safe([&] { Inject(k, root); })) {
        Log("game menu: %s — access violation while adding the items", k.overlay);
        Forget(k), k.failed = true;
      }
      continue;
    }
    if (now - k.refreshAt < 500) continue;
    k.refreshAt = now;
    MovieLock lk(movie);
    if (lk.ok && !Safe([&] { Apply(k, Compute(k, false)); })) Log("game menu: %s — access violation while updating", k.overlay);
  }
}
void OnRunCallbacks(Invocation&) {
  if (!Safe([] { MenuTick(); })) Log("game menu: access violation in the tick");
}
// экран закрывают (FUN_00804f30, thiscall(экран)): запуск режима, «Продолжить», загрузка — свои объекты отпускаем сразу,
// пока Movie ещё жив и закреплён за игрой (мы внутри её обработчика)
void OnScreenClose(Invocation& inv) {
  const addr screen = inv.ecx;
  if (!screen) return;
  Busy busy;
  if (!busy.ok) return;
  for (MenuKind& k : s_menus) {
    if (k.screen != screen) continue;
    k.quietUntil = NowMs() + 10000;
    if (!k.injected && !IsObj(k.menu)) continue;
    if (Safe([&] { ReleaseAll(k); })) Log("game menu: %s is closing — our items released", k.overlay);
    else Forget(k), Log("game menu: %s is closing — access violation while releasing, forgotten", k.overlay);
    k.movie = 0;
  }
}
// Меню игры начинает загрузку («Продолжить», «Загрузить», миссия, «Новая игра», «Перезапуск» в паузе): свои объекты меню
// отпускаем сразу. Пока мод держал их, загрузка из главного меню висела навсегда (01.10 21:53, 02.10 04:10, 04:22, 08:04 —
// главный поток ждёт задачу загрузки), а запуски мультиплеера, перед которыми мод их отпускал, шли. В потоке интерфейса
// (обработчик меню игры, Movie закреплён за ним) — прямо здесь; в другом потоке — просим тик
void OnLoadRequest(Invocation&) {
  if (GetCurrentThreadId() != s_uiTid.load()) {
    s_releaseReq = true;
    return;
  }
  Busy busy;
  if (!busy.ok) {
    s_releaseReq = true;
    return;
  }
  for (MenuKind& k : s_menus) {
    k.quietUntil = NowMs() + 10000;
    if (!k.injected && !IsObj(k.menu)) continue;
    if (Safe([&] { ReleaseAll(k); })) Log("game menu: %s — the game starts a load, our items released", k.overlay);
    else Forget(k), Log("game menu: %s — access violation while releasing before a load, forgotten", k.overlay);
    k.movie = 0;
  }
}
// портрет Чужого для экрана выбора: игра берёт картинки "DLC/<имя>" и из обычной папки DATA\UI\DLC
void InstallPortraits() {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir(exe);
  dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"DATA\\UI\\DLC\\";
  CreateDirectoryW(dir.c_str(), nullptr);
  for (const MpPortrait& p : kMpPortraits) {
    const std::wstring f = dir + p.file;
    // уже лежит такой же (по размеру и началу данных после заголовка) — не трогаем
    if (const HANDLE r = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); r != INVALID_HANDLE_VALUE) {
      uint8_t head[256] = {};
      DWORD got = 0;
      const DWORD size = GetFileSize(r, nullptr);
      ReadFile(r, head, sizeof head, &got, nullptr);
      CloseHandle(r);
      if (size == p.size && got == sizeof head && !memcmp(head, p.data, sizeof head)) continue;
    }
    const HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      Log("game menu: couldn't write the portrait %ls (error %lu)", f.c_str(), GetLastError());
      continue;
    }
    DWORD wr = 0;
    WriteFile(h, p.data, DWORD(p.size), &wr, nullptr);
    CloseHandle(h);
    Log("game menu: portrait written to %ls", f.c_str());
  }
}

}  // namespace

// мультиплеер запускает режим (mp.cpp, StartMode — поток интерфейса, секция интерфейса взята): свои объекты меню отпустить
// до закрытия экрана. Изнутри слушателя SteamAPI_RunCallbacks перехват FUN_00804f30 не срабатывает (вложенные хуки в
// слушателе молчат) — поэтому явно
void GameMenuReleaseNow(const char* why) {
  Busy busy;
  if (!busy.ok) return;
  s_pick = 0;
  for (MenuKind& k : s_menus) {
    k.quietUntil = NowMs() + 10000;
    if (!k.injected && !IsObj(k.menu)) continue;
    if (Safe([&] { ReleaseAll(k); })) Log("game menu: %s — our items released (%s)", k.overlay, why);
    else Forget(k), Log("game menu: %s — access violation while releasing (%s), forgotten", k.overlay, why);
    k.movie = 0;
  }
}

void InstallGameMenu() {
  using Interceptor::Attach;
  addr rc = 0;
  if (HMODULE dll = GetModuleHandleA("steam_api.dll")) rc = addr(GetProcAddress(dll, "SteamAPI_RunCallbacks"));
  if (rc) Attach(rc, OnRunCallbacks, nullptr, false);  // без общего замка: только своё состояние и игра
  else Log("game menu: steam_api.dll / SteamAPI_RunCallbacks not found — no buttons in the game menu");
  Attach(VA(gva::ScreenClose), OnScreenClose, nullptr, false);       // экран закрывают — свои объекты отпустить сразу
  Attach(VA(gva::GameplayStart), OnGameplayStart, nullptr, false);   // выбор на экране персонажа: наши карточки — себе
  Attach(VA(gva::ScreenSetState), OnSetState, nullptr, false);       // «назад» с нашего экрана — в меню
  for (addr a : { gva::LoadSave, gva::GameReset, gva::QueueLevel, gva::MenuLoadLevel })
    Attach(VA(a), OnLoadRequest, nullptr, false);                     // загрузка из меню игры — свои объекты отпустить
  InstallPortraits();
}
