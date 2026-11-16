// mod_main.cpp — запуск мода внутри AlienIsolation.DevTools.asi (вместо dllmain.cpp + инжектора + окна-оверлея).
// DevTools грузится ASI-лоадером при старте игры, поэтому хуки мода ставятся сразу, а не «из главного меню».
#include "ModApi.h"
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include "mp.h"
#include "puppet.h"
#include <cstring>
#include <cwchar>
#include <string>

void LogOpen(HMODULE self);
const std::wstring& ModBase();  // runtime.cpp
bool RuntimeInit();
void StartScheduler();
void InstallUi();

// ---- временный probe: какие анимационные параметры и на каком объекте ставит игра ----
// AnimSetParams thiscall(аним+0x50, &описания, n); описание 0x20 байт, имя — C-строка по +0x14.
static addr FindParamValuePtr(addr desc, int n, const char* wanted, uint32_t* outValue) {
  for (int i = 0; i < n && i < 64; i++) {
    const addr d = desc + addr(i) * 0x20;
    addr np = 0;
    Safe([&] { np = rp(d + 0x14); });
    if (!np) continue;
    char name[80] = {};
    Safe([&] {
      for (int k = 0; k < 79; k++) {
        const char c = rd<char>(np + addr(k));
        if (!c) break;
        name[k] = c;
      }
    });
    if (std::strcmp(name, wanted) != 0) continue;
    addr valuePtr = 0;
    uint32_t value = 0;
    Safe([&] {
      valuePtr = rp(d + 4);
      value = valuePtr ? rd<uint32_t>(valuePtr) : 0;
    });
    if (outValue) *outValue = value;
    return valuePtr;
  }
  return 0;
}
static void OnAnimSetParamsEnter(Invocation& inv) {
  static addr seen[48] = {};
  static int seenN = 0;
  const addr self = inv.ecx;                  // аним+0x50
  const addr desc = inv.arg[0];
  const int n = int(inv.arg[1]);
  if (!self || !desc || n <= 0 || n > 64) return;
  // LOCOMOTION_TYPE may be updated after the set was first seen; track its value pointer
  // on every call, before the one-shot logging guard below.
  uint32_t locoValue = 0;
  const addr locoPtr = FindParamValuePtr(desc, n, "LOCOMOTION_TYPE", &locoValue);
  if (locoPtr) {
    testLocoTypePtr.store(locoPtr, std::memory_order_relaxed);
    static addr lastLocoPtr = 0;
    if (locoPtr != lastLocoPtr) {
      lastLocoPtr = locoPtr;
      Log("loco-type: ptr %08X value %08X", unsigned(locoPtr), unsigned(locoValue));
    }
  }
  for (int i = 0; i < seenN; i++)
    if (seen[i] == self) return;
  if (seenN >= 48) return;
  seen[seenN++] = self;
  const addr anim = self - 0x50;
  Log("AnimSetParams anim=%08X block=%08X n=%d", unsigned(anim), unsigned(self), n);
  for (int i = 0; i < n && i < 16; i++) {
    const addr d = desc + addr(i) * 0x20;
    addr np = 0;
    Safe([&] { np = rp(d + 0x14); });
    char name[80] = {};
    if (np)
      Safe([&] {
        for (int k = 0; k < 79; k++) {
          const char c = rd<char>(np + addr(k));
          if (!c) break;
          name[k] = c;
        }
      });
    uint32_t v0 = 0, v1 = 0, v2 = 0, v3 = 0, v4 = 0;
    Safe([&] {
      v0 = rd<uint32_t>(d);
      v1 = rd<uint32_t>(d + 4);
      v2 = rd<uint32_t>(d + 8);
      v3 = rd<uint32_t>(d + 0xC);
      v4 = rd<uint32_t>(d + 0x10);
    });
    Log("  [%d] '%s'  %08X %08X %08X %08X %08X", i, name, unsigned(v0), unsigned(v1), unsigned(v2),
        unsigned(v3), unsigned(v4));
  }
}
static void InstallAnimProbe() {
  using Interceptor::Attach;
  // Diagnostic hook only. The old auto-probe ran on the scheduler thread and dereferenced stale entity
  // wrappers; it is intentionally not scheduled. ProbeAnimSet() below is kept for manual experiments only.
  Attach(VA(va::AnimSetParams), OnAnimSetParamsEnter, nullptr);
}

// Временный C++-пробник: GetEntityHandle(npc) -> FindEntity -> Character entity;
// entity+8 — обёртка, которую ждёт FindParamString (arg0 = &wrapper).
// Читает строковые параметры персонажа, в первую очередь anim_set.
void ProbeAnimSet() {
  if (!npc) {
    Log("anim-probe: no npc");
    return;
  }
  const addr ei = SpawnEntityInterface();
  Log("anim-probe: start npc=%08X ei=%08X", unsigned(npc), unsigned(ei));
  addr hh = 0x3B027CF8;  // temporary: wrapper found by find_ref for the live alien
  addr hhActor = 0;
  Safe([&] { hhActor = rp(hh + 0xc); });
  Log("anim-probe: hardcoded hh=%08X actor=%08X", unsigned(hh), unsigned(hhActor));
  if (!ei || hhActor != npc) {
    Log("anim-probe: bad wrapper (ei=%08X hh=%08X actor=%08X)", unsigned(ei), unsigned(hh), unsigned(hhActor));
    return;
  }
  // Живые Cathode-сущности из реальных вызовов FindParamString (лог "real").
  // Для FindParamString arg0 — это указатель на dword с указателем на сущность;
  // 0x8BCA90 сам инкрементит refcount и вызывает [obj+0xC]->vtable+0x11C.
  struct ObjPair { addr ei, obj; const char* label; };
  const ObjPair objs[] = {
    { 0x141926D8, 0x0C88E690, "A" },
    { 0x14192860, 0x0C82A060, "B" },
  };
  const uint32_t guids[] = { 0xE6C336D2, 0xFB254860, 0x940A731A, 0x71B80569, 0x6073C07F, 0x9958FB6B, 0xFCEA573F };
  int hits = 0;
  for (const ObjPair& p : objs) {
    for (uint32_t guid : guids) {
      addr objPtr = p.obj;
      addr out = 0;
      uint8_t ok = 0;
      Safe([&] { ok = Fn<FindParamStringFn>(va::FindParamString)(p.ei, addr(&objPtr), addr(&guid), addr(&out)); });
      char text[160] = {};
      if (out)
        Safe([&] {
          for (int k = 0; k < 159; k++) {
            const char c = rd<char>(out + addr(k));
            if (!c) break;
            text[k] = c;
          }
        });
      if (ok || out) {
        Log("  obj %s=%08X guid=%08X ok=%u out=%08X '%s'", p.label, unsigned(p.obj), unsigned(guid), unsigned(ok),
            unsigned(out), text);
        hits++;
      }
    }
  }
  Log("anim-probe: live objects done, hits=%d", hits);
}

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
#ifdef ALIENMOD_DEV_TOOLS
  InstallAnimProbe();
#endif
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
  // Multiplayer screens remain in gamemenu.cpp for future work; no UI hooks for now.
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
