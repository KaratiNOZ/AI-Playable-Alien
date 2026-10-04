// reload.cpp — отцепить мод в любой момент (кнопка в меню или инжектор перед внедрением новой версии) и передать
// следующей DLL найденные узлы дерева поведения.
//
// Отцепление: планировщик останавливается, всё, что мод держит в игре, возвращается (камера — Аманде, рубильники — как их
// создала игра, время и постобработка фоторежима, HUD игры, спрятанная Аманда, курсор), поток рендера выключает ImGui и
// возвращает Present/WndProc, хуки выключаются (MinHook возвращает исходные байты). Саму DLL из памяти НЕ выгружаем:
// поток игры может стоять внутри трамплина или вернуться в заглушку выхода хука — пусть там будет живой код. Поэтому
// инжектор грузит каждую версию из своей копии (live\AlienModUI-<время>.dll): исходную DLL можно пересобирать.
//
// Передача узлов: узлы дерева мод узнаёт, только когда игра их создаёт (загрузка уровня). Старая DLL кладёт найденные
// узлы, камеру и лицехвата в именованную память процесса, новая забирает их при старте (до включения хуков) — и сразу
// работает посреди уровня, с той же камерой.
#include "ModApi.h"
#include "interceptor.h"
#include "state.h"
#include "mp.h"
#include <algorithm>
#include <atomic>
#include <cwchar>

bool StopScheduler();          // runtime.cpp
void LogClose();               // runtime.cpp
void ModReleaseOnce();         // mod_main.cpp: мьютекс «одна копия мода» — отпустить для следующей DLL

namespace {
constexpr uint32_t kMagic = 0x31484D41;  // "AMH1"
constexpr uint32_t kMaxRecs = 4000;
struct Handoff {
  uint32_t magic, recSize, count;
  int32_t cam;        // камера была у Чужого: 1 из головы, 2 за спиной (0 — у Аманды)
  addr hugger;        // играли за этого лицехвата
  uint32_t locked;    // управление закреплено за исходным Чужим (клоны)
  NodeRec recs[kMaxRecs];
};

std::atomic<int> s_state{ 0 };  // 0 — работает, 1 — отцепляется, 2 — отцеплен
HANDLE s_map = nullptr;         // держим открытым: пока старая DLL в памяти, узлы доступны новой

HANDLE OpenHandoff(bool create) {
  wchar_t name[64];
  swprintf(name, 64, L"Local\\AlienPlayableMod_Handoff_%lu", GetCurrentProcessId());
  if (create) return CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Handoff), name);
  return OpenFileMappingW(FILE_MAP_READ, FALSE, name);
}

void WriteHandoff(const std::vector<NodeRec>& recs, int cam, addr hug, bool lock) {
  if (!s_map) s_map = OpenHandoff(true);
  if (!s_map) return Log("reload: handoff memory failed (%lu)", GetLastError());
  auto* h = static_cast<Handoff*>(MapViewOfFile(s_map, FILE_MAP_WRITE, 0, 0, sizeof(Handoff)));
  if (!h) return Log("reload: handoff view failed (%lu)", GetLastError());
  h->magic = 0;  // пока пишем — невалидно
  h->recSize = sizeof(NodeRec);
  h->count = 0;
  for (const NodeRec& r : recs)
    if (h->count < kMaxRecs) h->recs[h->count++] = r;
  h->cam = cam;
  h->hugger = hug;
  h->locked = lock;
  h->magic = kMagic;
  UnmapViewOfFile(h);
  Log("reload: handed over %u tree nodes, camera %d, facehugger %08X", unsigned(recs.size()), cam, unsigned(hug));
}

// ---- новая DLL: продолжить с той же камерой, когда найдутся Чужой / Аманда ----
int s_resumeCam = 0;
addr s_resumeHug = 0;
ms_t s_resumeUntil = 0;
void ResumeTick() {  // под общим замком
  if (!s_resumeCam && !s_resumeHug) return;
  if (NowMs() > s_resumeUntil) {
    Log("reload: gave up resuming the camera");
    s_resumeCam = 0, s_resumeHug = 0;
    return;
  }
  if (!player) return;
  if (s_resumeHug) {
    const std::vector<addr> act = ActiveChars();
    if (HugIsPlayable(s_resumeHug) && DeathState(s_resumeHug) == 0 && std::find(act.begin(), act.end(), s_resumeHug) != act.end())
      ActHugger(s_resumeHug), Log("reload: playing as the facehugger again");
    else
      Log("reload: the facehugger is gone — camera stays with Amanda");
    s_resumeHug = 0, s_resumeCam = 0;
  } else if (npc) {
    ActCamera(s_resumeCam);
    Log("reload: camera back on the alien (%d)", s_resumeCam);
    s_resumeCam = 0;
  }
}
}  // namespace

// до включения хуков, в потоке запуска (планировщик ещё не идёт — замок не нужен)
void InstallReload() {
  Every(100, "reload-resume", ResumeTick);
  HANDLE m = OpenHandoff(false);
  if (!m) return;  // первая DLL в этой игре
  const auto* h = static_cast<const Handoff*>(MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(Handoff)));
  if (h && h->magic == kMagic && h->recSize == sizeof(NodeRec) && h->count <= kMaxRecs) {
    unsigned alien = 0, hug = 0;
    for (uint32_t i = 0; i < h->count; i++) {
      const NodeRec r = h->recs[i];
      if (r.kind == NR_HK_WORLD) CameraAdoptWorld(r.p);
      else if (r.kind >= NR_HUG_GATE) HugAdopt(r), hug++;
      else ControlAdopt(r), alien++;
    }
    ControlAdoptDone();
    locked = h->locked != 0;
    s_resumeCam = h->cam, s_resumeHug = h->hugger;
    s_resumeUntil = NowMs() + 15000;
    Log("reload: took over %u alien and %u facehugger nodes from the previous version", alien, hug);
  } else if (h) {
    Log("reload: the previous version left no usable nodes (reload the level if the alien doesn't answer)");
  }
  if (h) UnmapViewOfFile(h);
  CloseHandle(m);
}

namespace AlienMod {

bool Unloaded() { return s_state == 2; }

bool Unload() {
  int expect = 0;
  if (!s_state.compare_exchange_strong(expect, 1)) return false;  // уже отцепляется / отцеплен
  Interceptor::IgnoreCurrentThread();
  Log("unload: detaching the mod...");
  if (!StopScheduler()) Log("unload: the scheduler didn't stop in time");
  MpShutdown();  // напарнику — «отключился», сеть стоп

  // 0) камеру — Аманде руками потока игры (CamDo в его хуке, camWant = 3), а мы только ждём. Из этого потока функции
  //    камер игры звать нельзя, пока игра идёт: поток игры может стоять в нашем хуке, ожидая общий замок, и держать при
  //    этом свой замок камер — взаимная блокировка (01.10.2026 так зависла игра). Игра на паузе — хуки не зовутся,
  //    тогда камеру возвращаем сами, как раньше (шаг 1).
  JsLock();
  Safe([] { PhotoOffForUnload(); });
  Safe([] { if (hugger) ActHugger(0); });
  Safe([] { if (camMode != 0 || camWant == 1 || camWant == 2) ActCamera(3); });
  JsUnlock();
  for (int i = 0; i < 150 && (camMode != 0 || camWant != 0) && !GameStalled(); i++) Sleep(10);
  Sleep(60);  // части модели Аманды поток игры возвращает сам (FpArmsTick), когда камера у неё
  Log("unload: camera %s", camMode == 0 ? "back with Amanda" : GameStalled() ? "still on the alien (game paused — restoring here)"
                                                                            : "still on the alien (game thread didn't answer)");

  // 1) под замком: запомнить узлы и камеру, вернуть игре всё, что держим; камера — Аманде
  std::vector<NodeRec> recs;
  int cam = 0;
  addr hug = 0;
  bool lock = false;
  JsLock();
  Safe([&] {
    cam = camMode != 0 ? int(camMode) : (camWant == 1 || camWant == 2) ? camWant : 0;
    hug = hugger;
    lock = locked;
    ControlExport(recs);
    HugExport(recs);
    if (hkWorld) recs.push_back({ NR_HK_WORLD, hkWorld, 0 });  // мир Havok: новая версия сразу пускает лучи
  });
  Safe([] { PhotoReset(); });
  Safe([] { ActPanic(); });
  Safe([] { ControlShutdown(); });
  Safe([] { CameraShutdown(GameStalled()); });  // функции камер игры — только если поток игры стоит
  g_pausePass = false;
  g_amandaInput = false;
  JsUnlock();

  // 2) HUD игры возвращает хук апдейта оверлеев в потоке игры — даём ему кадр-другой (камера уже у Аманды)
  for (int i = 0; i < 50 && GameHudHidden(); i++) Sleep(10);

  // 3) поток рендера выключает ImGui и возвращает Present/WndProc (в сборке с DevTools — некому, ждём недолго)
  g_uiUnload = 1;
  for (int i = 0; i < 200 && g_uiUnload == 1; i++) Sleep(10);
  if (g_uiUnload == 1) {
    g_uiUnload = 3;
    Log("unload: the render thread didn't answer (game minimized?) — the menu just stops drawing");
  }

  // 4) хуки
  Interceptor::Shutdown();
  Sleep(50);  // потоки, вошедшие в заглушку до выключения, выходят без слушателей

  // 5) то, что можно из любого потока: Аманда домой, осмотр, курсор
  JsLock();
  Safe([] { AlienExtraShutdown(); });
  Safe([] { MenuCursor(false); });
  JsUnlock();

  // 6) узлы — следующей версии, место «одной копии мода» — ей же
  WriteHandoff(recs, cam, hug, lock);
  ModReleaseOnce();
  Log("unload: the mod is detached — inject the new version (runUI.bat)");
  LogClose();
  s_state = 2;
  return true;
}

static DWORD WINAPI UnloadThread(LPVOID) { return Unload() ? 1 : 0; }
void RequestUnload() {  // из меню (поток рендера): отцепление ждёт поток рендера, поэтому — в своём потоке
  if (s_state != 0) return;
  HANDLE t = CreateThread(nullptr, 0, UnloadThread, nullptr, 0, nullptr);
  if (t) CloseHandle(t);
}

}  // namespace AlienMod
