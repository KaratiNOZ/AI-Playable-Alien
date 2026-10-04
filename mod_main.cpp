// mod_main.cpp — запуск мода внутри AlienIsolation.DevTools.asi (вместо dllmain.cpp + инжектора + окна-оверлея).
// DevTools грузится ASI-лоадером при старте игры, поэтому хуки мода ставятся сразу, а не «из главного меню».
#include "ModApi.h"
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include "mp.h"
#include "puppet.h"
#include <cwchar>
#include <string>

void LogOpen(HMODULE self);
const std::wstring& ModBase();  // runtime.cpp
bool RuntimeInit();
void StartScheduler();
void InstallUi();

static std::atomic<bool> s_ready{ false };
static HANDLE s_once = nullptr;  // мьютекс «одна копия мода на игру»
void ModReleaseOnce() {         // мод отцеплен — следующая версия может стартовать
  if (s_once) CloseHandle(s_once), s_once = nullptr;
}
static void (*s_extraInstall)() = nullptr;

// ---- бывший overlay.cpp: строки статуса теперь рисует ImGui (ui.cpp) ----
static SRWLOCK s_ovLock = SRWLOCK_INIT;
static std::wstring s_ovSlot[3];
void OverlaySet(int slot, const std::wstring& text) {
  if (slot < 0 || slot > 2) return;
  AcquireSRWLockExclusive(&s_ovLock);
  s_ovSlot[slot] = text;
  ReleaseSRWLockExclusive(&s_ovLock);
}
std::wstring OverlayGet(int slot) {
  if (slot < 0 || slot > 2) return {};
  AcquireSRWLockShared(&s_ovLock);
  std::wstring r = s_ovSlot[slot];
  ReleaseSRWLockShared(&s_ovLock);
  return r;
}
void StartOverlay() {}

static DWORD WINAPI InitThread(LPVOID) {
  HMODULE exe = GetModuleHandleW(nullptr);
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(exe, path, MAX_PATH);
  const wchar_t* name = wcsrchr(path, L'\\');
  name = name ? name + 1 : path;
  if (_wcsicmp(name, L"AI.exe") != 0) {
    Log("процесс не AI.exe — мод выключен");
    return 0;
  }
  g_base = addr(exe);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + dos->e_lfanew);
  g_modEnd = g_base + nt->OptionalHeader.SizeOfImage;
  // все адреса мода — из Steam-сборки AI.exe (17.02.2015). На другой сборке (GOG, Epic, другая версия) часть хуков может
  // лечь мимо: мод всё равно включается (инжектор и установщик об этом предупреждают), а в меню — строка
  // «не Steam-версия, часть функций может не работать»
  const DWORD stamp = nt->FileHeader.TimeDateStamp, size = nt->OptionalHeader.SizeOfImage;
  if (stamp != kGameStamp || size != kGameImageSize) {
    Log("ВНИМАНИЕ: AI.exe другой сборки (штамп %08lX, размер %08lX; мод сделан под Steam %08lX / %08lX) — включаюсь, но часть "
        "функций или весь мод может не работать", static_cast<unsigned long>(stamp), static_cast<unsigned long>(size),
        static_cast<unsigned long>(kGameStamp), static_cast<unsigned long>(kGameImageSize));
    g_wrongGame = true;
  }
  if (!RuntimeInit() || !Interceptor::Init()) {
    Log("инициализация не удалась");
    return 0;
  }
  InstallControl();
  InstallCamera();
  InstallVent();
  InstallActors();
  InstallSpawn();
  InstallPhoto();
  InstallAlienExtra();
  InstallHugger();
  InstallInputBlock();
  InstallMp();
  InstallPuppet();
  InstallGameMenu();
  InstallUi();
  if (s_extraInstall) s_extraInstall();
  InstallReload();  // узлы и камера от прошлой версии (если её отцепили посреди уровня)
  if (!Interceptor::Commit()) {
    Log("хуки не включились");
    return 0;
  }
  StartScheduler();
  s_ready = true;
  Log("Хук стоит (AI.exe base %08X). Можно внедрять и посреди уровня (Delete — меню).", unsigned(g_base));
  // прошлая версия узлы не передала — внедрили посреди уровня с нуля: найти уже созданные деревья в памяти
  // (из главного меню ничего не найдётся, узлы потом покажет фабрика при загрузке уровня)
  JsLock();
  const bool need = multi[G_ARROW].empty() && !HugHasGates();
  JsUnlock();
  if (need) ScanExistingTrees();
  return 0;
}

namespace AlienMod {

bool Start(void* hModule, void (*extraInstall)()) {
  // одна копия мода на процесс: две ставят одни и те же хуки и пишут в одни и те же узлы дерева -> вылет
  wchar_t name[64];
  swprintf(name, 64, L"Local\\AlienPlayableMod_%lu", GetCurrentProcessId());
  HANDLE once = CreateMutexW(nullptr, FALSE, name);
  if (!once || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (once) CloseHandle(once);
    OutputDebugStringA("AlienMod: мод уже запущен в этой игре — вторая копия не стартует\n");
    return false;
  }
  s_once = once;
  s_extraInstall = extraInstall;
  LogOpen(static_cast<HMODULE>(hModule));
  HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
  if (t) CloseHandle(t);
  return true;
}

void OnLevelChange(const char* why) {
  if (!s_ready) return;  // до запуска мода сбрасывать нечего (и общий замок ещё не создан)
  JsLock();
  bool ok = Safe([&] {
    if (camMode != 0) camMode = 0;  // блок ввода Аманды снимаем сразу; камеру игра пересоздаст с уровнем
    HardReset(why);
  });
  JsUnlock();
  if (!ok) Log("err: сброс при смене уровня");
}

void SetUiInput(bool menuShown, bool wantTextInput) {
  if (menuShown != g_uiMenuShown) MenuCursor(menuShown);
  g_uiMenuShown = menuShown;
  g_uiMouseBlocked = menuShown;
  g_uiKeysBlocked = wantTextInput;
}

}  // namespace AlienMod
