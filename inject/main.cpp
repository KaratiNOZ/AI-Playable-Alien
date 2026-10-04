// AlienInjector — замена run.py/Frida: ждёт AI.exe, ждёт, пока окно игры в фокусе, внедряет AlienMod.dll
// (LoadLibraryW в удалённом потоке) и дальше показывает лог мода, пока игра не закроется.
//   AlienInjector.exe [путь\AlienMod.dll] [--no-focus] [--no-log]
// Обновление без перезапуска игры: если мод уже в игре, сначала зовём его AlienModUnload (он всё возвращает игре и
// передаёт найденные узлы следующей версии), потом внедряем новую. Внедряем не саму DLL, а её копию
// live\AlienModUI-<время>.dll: отцепленная версия остаётся в памяти игры, а исходный файл свободен для пересборки.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <vector>

static_assert(sizeof(void*) == 4, "инжектор тоже x86: адрес LoadLibraryW берётся из своего (WOW64) kernel32");

static std::string U8(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
  return s;
}
static void Say(const char* s) {
  fputs(s, stdout);
  fputc('\n', stdout);
  fflush(stdout);
}
static std::wstring DirOf(const std::wstring& p) {
  size_t k = p.find_last_of(L"\\/");
  return k == std::wstring::npos ? L"." : p.substr(0, k);
}

static DWORD FindGame() {  // как run.py: самый новый pid AI.exe
  DWORD best = 0;
  HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (s == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W e;
  e.dwSize = sizeof e;
  for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
    if (_wcsicmp(e.szExeFile, L"AI.exe") == 0 && e.th32ProcessID > best) best = e.th32ProcessID;
  CloseHandle(s);
  return best;
}

// модули мода в игре: AlienModUI.dll (старый запуск) и копии live\AlienModUI-*.dll
struct RemoteMod { BYTE* base; std::wstring path; };
static std::vector<RemoteMod> ModModules(DWORD pid) {
  std::vector<RemoteMod> out;
  for (int tries = 0; tries < 10; tries++) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) {
      if (GetLastError() == ERROR_BAD_LENGTH) continue;
      return out;
    }
    MODULEENTRY32W m;
    m.dwSize = sizeof m;
    for (BOOL ok = Module32FirstW(s, &m); ok; ok = Module32NextW(s, &m))
      if (_wcsnicmp(m.szModule, L"AlienModUI", 10) == 0) out.push_back({ m.modBaseAddr, m.szExePath });
    CloseHandle(s);
    return out;
  }
  return out;
}

// отцепить версию мода, которая сейчас в игре: её экспорт AlienModUnload в удалённом потоке.
// Адрес экспорта — по той же DLL, загруженной у нас без выполнения кода (смещение от начала модуля то же).
enum UnloadResult { UL_DONE, UL_ALREADY, UL_OLD, UL_FAIL };
static UnloadResult UnloadRemote(HANDLE proc, const RemoteMod& m) {
  HMODULE local = LoadLibraryExW(m.path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!local) return UL_FAIL;
  FARPROC f = nullptr;
  for (const char* n : { "AlienModUnload", "AlienModUnload@4", "_AlienModUnload@4" })
    if (!f) f = GetProcAddress(local, n);
  const uintptr_t off = f ? uintptr_t(f) - uintptr_t(local) : 0;
  FreeLibrary(local);
  if (!f) return UL_OLD;
  HANDLE t = CreateRemoteThread(proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(m.base + off), nullptr, 0, nullptr);
  if (!t) return UL_FAIL;
  DWORD code = 0;
  const bool done = WaitForSingleObject(t, 20000) == WAIT_OBJECT_0 && GetExitCodeThread(t, &code);
  CloseHandle(t);
  return !done ? UL_FAIL : code == 1 ? UL_DONE : UL_ALREADY;
}

// копия DLL для внедрения: live\AlienModUI-<время>.dll; старые копии, которые игра уже отпустила, удаляем
static std::wstring MakeLiveCopy(const std::wstring& dll) {
  const std::wstring dir = DirOf(dll) + L"\\live";
  CreateDirectoryW(dir.c_str(), nullptr);
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW((dir + L"\\AlienModUI-*.dll").c_str(), &fd);
  if (f != INVALID_HANDLE_VALUE) {
    do DeleteFileW((dir + L"\\" + fd.cFileName).c_str());  // занятые игрой не удалятся — и ладно
    while (FindNextFileW(f, &fd));
    FindClose(f);
  }
  wchar_t name[64];
  swprintf(name, 64, L"\\AlienModUI-%llu.dll", static_cast<unsigned long long>(GetTickCount64()));
  const std::wstring copy = dir + name;
  return CopyFileW(dll.c_str(), copy.c_str(), FALSE) ? copy : std::wstring();
}

static bool GameInFront(DWORD pid) {
  DWORD p = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &p);
  return p == pid;
}

static bool Inject(HANDLE proc, const std::wstring& dll) {
  SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
  void* mem = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!mem) return printf("VirtualAllocEx: ошибка %lu\n", GetLastError()), false;
  if (!WriteProcessMemory(proc, mem, dll.c_str(), bytes, nullptr)) {
    printf("WriteProcessMemory: ошибка %lu\n", GetLastError());
    VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    return false;
  }
  auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
  HANDLE t = CreateRemoteThread(proc, nullptr, 0, loadLibrary, mem, 0, nullptr);
  if (!t) {
    printf("CreateRemoteThread: ошибка %lu\n", GetLastError());
    VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    return false;
  }
  if (WaitForSingleObject(t, 20000) != WAIT_OBJECT_0) {  // память не освобождаем — поток ещё может её читать
    Say("LoadLibraryW в игре не завершился за 20 с (игра свёрнута/на паузе?)");
    CloseHandle(t);
    return false;
  }
  DWORD hmod = 0;
  GetExitCodeThread(t, &hmod);
  CloseHandle(t);
  VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
  if (!hmod)
    Say("DLL не загрузилась. Чаще всего: мод уже запущен в этой игре (старая AlienMod.dll или DevTools со встроенным модом) — "
        "перезапусти игру и внедряй только одну копию.");
  return hmod != 0;
}

static void TailLog(HANDLE proc, const std::wstring& path) {
  HANDLE f = INVALID_HANDLE_VALUE;
  char buf[4096];
  for (;;) {
    if (f == INVALID_HANDLE_VALUE)
      f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      // новая версия мода открыла лог заново (файл обрезан) — читаем с начала
      LARGE_INTEGER size{}, pos{}, zero{};
      if (GetFileSizeEx(f, &size) && SetFilePointerEx(f, zero, &pos, FILE_CURRENT) && size.QuadPart < pos.QuadPart)
        SetFilePointerEx(f, zero, nullptr, FILE_BEGIN);
      DWORD n = 0;
      while (ReadFile(f, buf, sizeof buf, &n, nullptr) && n) fwrite(buf, 1, n, stdout);
      fflush(stdout);
    }
    if (WaitForSingleObject(proc, 250) == WAIT_OBJECT_0) break;
  }
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  Say("Игра закрыта.");
}

// сборка игры по заголовку PE файла AI.exe: штамп времени и SizeOfImage — те же, что у Steam-версии (game.h)
static bool ReadGameBuild(HANDLE proc, DWORD& stamp, DWORD& size) {
  wchar_t path[MAX_PATH];
  DWORD n = MAX_PATH;
  if (!QueryFullProcessImageNameW(proc, 0, path, &n)) return false;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  unsigned char buf[4096];
  DWORD got = 0;
  const bool ok = ReadFile(f, buf, sizeof buf, &got, nullptr) && got >= 0x200;
  CloseHandle(f);
  if (!ok) return false;
  const LONG lfanew = *reinterpret_cast<LONG*>(buf + 0x3c);
  if (lfanew <= 0 || lfanew + 0x60 > LONG(got)) return false;
  const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(buf + lfanew);
  stamp = nt->FileHeader.TimeDateStamp;
  size = nt->OptionalHeader.SizeOfImage;
  return true;
}
static bool GameBuildOk(HANDLE proc) {
  DWORD stamp = 0, size = 0;
  if (!ReadGameBuild(proc, stamp, size)) return true;  // не прочитали — решит сам мод (он проверяет то же самое)
  if (stamp == 0x54E3199D && size == 0x01F81000) return true;
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::wstring ini = DirOf(self) + L"\\AlienModUI.ini";
  if (GetPrivateProfileIntW(L"setup", L"any_game_version", 0, ini.c_str())) return true;  // уже предупреждали — молча
  printf("AI.exe другой сборки (штамп %08lX) — мод рассчитан на Steam-версию, внедряю на свой страх.\n", stamp);
  // предупреждаем (сразу на двух языках) и всё равно внедряем
  MessageBoxW(nullptr,
      L"Эта Alien: Isolation — не Steam-сборка от 17.02.2015 (GOG, Epic или другое обновление).\n"
      L"Мод сделан под Steam-версию: часть функций или весь мод может не работать, игра может вылететь.\n"
      L"Мод всё равно будет запущен.\n\n"
      L"This Alien: Isolation is not the Steam build of 17 Feb 2015 (GOG, Epic or another update).\n"
      L"The mod is made for the Steam version: some features or the whole mod may not work, and the game may crash.\n"
      L"The mod will start anyway.",
      L"Alien Playable", MB_OK | MB_ICONWARNING);
  return true;
}

int main() {
  SetConsoleOutputCP(CP_UTF8);
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring dll = DirOf(self) + L"\\AlienModUI.dll";
  bool waitFocus = true, tail = true;
  for (int i = 1; i < argc; i++) {
    std::wstring a = argv[i];
    if (a == L"--no-focus") waitFocus = false;
    else if (a == L"--no-log") tail = false;
    else dll = a;
  }
  wchar_t full[MAX_PATH];
  if (GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr)) dll = full;
  if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
    printf("Нет файла %s\n", U8(dll).c_str());
    return 1;
  }
  const wchar_t* dllName = dll.c_str() + dll.find_last_of(L"\\/") + 1;

  Say("Жду AI.exe (можно в любой момент: в главном меню или прямо на уровне)...");
  DWORD pid;
  while (!(pid = FindGame())) Sleep(1000);
  HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
  if (!proc) {
    printf("OpenProcess(%lu): ошибка %lu (запусти от того же пользователя/от администратора)\n", pid, GetLastError());
    return 1;
  }
  BOOL selfWow = FALSE, gameWow = FALSE;
  IsWow64Process(GetCurrentProcess(), &selfWow);
  IsWow64Process(proc, &gameWow);
  if (selfWow != gameWow) {
    Say("AI.exe не 32-битный? Мод рассчитан на x86 AI.exe из Steam.");
    return 1;
  }
  printf("AI.exe pid %lu\n", pid);
  // адреса мода — из Steam-сборки AI.exe (17.02.2015); на другой сборке — предупредить, но всё равно внедрить
  GameBuildOk(proc);

  if (waitFocus && !GameInFront(pid)) {
    Say("Переключись в окно игры: она должна быть в фокусе (свёрнутая игра стоит на паузе)...");
    while (!GameInFront(pid))
      if (WaitForSingleObject(proc, 300) == WAIT_OBJECT_0) return 1;
  }
  // мод уже в игре — отцепляем (новая версия продолжит с того же места)
  bool reload = false;
  for (const RemoteMod& m : ModModules(pid)) {
    switch (UnloadRemote(proc, m)) {
      case UL_DONE: Say("Старая версия мода отцеплена."), reload = true; break;
      case UL_ALREADY: reload = true; break;  // отцепили раньше — лежит в памяти без дела
      case UL_OLD:
        Say("В игре старая версия мода — она не умеет отцепляться. Перезапусти игру один раз, дальше обновление "
            "будет работать без перезапуска.");
        return 1;
      case UL_FAIL:
        Say("Не удалось отцепить мод, который уже в игре (игра свёрнута или зависла?). Попробуй ещё раз.");
        return 1;
    }
  }
  const std::wstring live = MakeLiveCopy(dll);
  if (live.empty()) {
    printf("Не удалось скопировать DLL в папку live (ошибка %lu)\n", GetLastError());
    return 1;
  }
  Say(reload ? "Цепляю новую версию..." : "Цепляюсь...");
  if (!Inject(proc, live)) return 1;
  Say(reload ? "Новая версия стоит и продолжает с того же места. Снова обновить — ещё раз runUI.bat."
             : "Хук стоит. Если уже на уровне — мод сам найдёт Чужого и лицехватов (пара секунд). Клавиши — см. README. Обновить мод — ещё раз runUI.bat (игру не закрывай).");
  std::wstring logName = dllName;
  if (size_t dot = logName.find_last_of(L'.'); dot != std::wstring::npos) logName.resize(dot);
  if (tail) TailLog(proc, DirOf(dll) + L"\\" + logName + L".log");
  CloseHandle(proc);
  return 0;
}
