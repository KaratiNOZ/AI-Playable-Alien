// AlienModSetup — установщик мода.
// Сам ищет Alien: Isolation (Steam: реестр, libraryfolders.vdf, манифест игры 214490; GOG; Epic; типовые папки на всех
// дисках), показывает найденную папку, «Обзор…» открывает обычный выбор папки Проводника. «Установить» кладёт в игру
// дерево поведения мода (DATA\BINARY_BEHAVIOR\_DIRECTORY_CONTENTS.BML — ветки управления Чужим и лицехватами), оригинал
// сохраняется рядом (.bak); «Удалить из игры» возвращает оригинал. Нет прав на папку игры — перезапуск от администратора.
//   AlienModSetup.exe [--install <папка игры>] [--uninstall <папка игры>]   — без окна (так он зовёт сам себя с правами)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

// ---------------- язык ----------------
static bool Ru() {
  static const bool ru = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
  return ru;
}
static const wchar_t* TR(const wchar_t* ru, const wchar_t* en) { return Ru() ? ru : en; }

// ---------------- файлы и пути ----------------
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static std::wstring Parent(std::wstring p) {
  while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  const size_t k = p.find_last_of(L"\\/");
  return k == std::wstring::npos ? std::wstring() : p.substr(0, k);
}
static std::wstring Trim(std::wstring p) {
  while (!p.empty() && (iswspace(p.back()) || p.back() == L'"' || p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  size_t i = 0;
  while (i < p.size() && (iswspace(p[i]) || p[i] == L'"')) i++;
  p = p.substr(i);
  for (wchar_t& c : p)
    if (c == L'/') c = L'\\';
  return p;
}
static std::wstring ExeDir() {
  wchar_t b[MAX_PATH];
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return Parent(b);
}
static const wchar_t* const kBmlRel = L"\\DATA\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML";
static bool IsGameDir(const std::wstring& d) { return !d.empty() && Exists(d + L"\\AI.exe") && Exists(d + kBmlRel); }
// выбрали не саму папку игры, а её DATA, BINARY_BEHAVIOR или папку над ней — поправить
static std::wstring Normalize(const std::wstring& raw) {
  std::wstring d = Trim(raw);
  if (d.size() > 7 && _wcsicmp(d.c_str() + d.size() - 7, L"\\AI.exe") == 0) d = Parent(d);
  if (IsGameDir(d)) return d;
  for (std::wstring p = Parent(d); !p.empty() && p.size() > 3; p = Parent(p))
    if (IsGameDir(p)) return p;
  for (const wchar_t* sub : { L"\\Alien Isolation", L"\\steamapps\\common\\Alien Isolation" })
    if (IsGameDir(d + sub)) return d + sub;
  return d;
}
static bool ReadFileText(const std::wstring& p, std::string& out) {
  HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  const DWORD n = GetFileSize(f, nullptr);
  out.resize(n < 16 * 1024 * 1024 ? n : 0);
  DWORD got = 0;
  const bool ok = out.empty() || ReadFile(f, &out[0], DWORD(out.size()), &got, nullptr);
  CloseHandle(f);
  out.resize(got);
  return ok;
}
static bool SameFile(const std::wstring& a, const std::wstring& b) {
  std::string x, y;
  return ReadFileText(a, x) && ReadFileText(b, y) && x == y;
}
static std::wstring Widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(std::max(n, 0)), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
  return w;
}
// "ключ"  "значение" из vdf/acf/json: значение после ключа в кавычках, с \\ -> \ (json и vdf экранируют так же)
static std::vector<std::wstring> QuotedValues(const std::string& text, const std::string& key) {
  std::vector<std::wstring> out;
  const std::string q = "\"" + key + "\"";
  for (size_t i = text.find(q); i != std::string::npos; i = text.find(q, i + 1)) {
    size_t a = text.find('"', i + q.size());
    if (a == std::string::npos) break;
    std::string v;
    for (size_t k = a + 1; k < text.size() && text[k] != '"'; k++) {
      if (text[k] == '\\' && k + 1 < text.size()) k++;
      v += text[k];
    }
    out.push_back(Widen(v));
  }
  return out;
}

// ---------------- поиск игры ----------------
static std::wstring RegStr(HKEY root, const wchar_t* key, const wchar_t* val, REGSAM view = 0) {
  HKEY h;
  if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &h) != ERROR_SUCCESS) return {};
  wchar_t buf[1024];
  DWORD size = sizeof buf, type = 0;
  std::wstring r;
  if (RegQueryValueExW(h, val, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
      (type == REG_SZ || type == REG_EXPAND_SZ))
    r.assign(buf, wcsnlen(buf, size / sizeof(wchar_t)));
  RegCloseKey(h);
  return Trim(r);
}
static std::vector<std::wstring> SteamLibraries() {
  std::vector<std::wstring> roots, libs;
  for (const std::wstring& r : { RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                                 RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
                                 RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath") })
    if (!r.empty()) roots.push_back(r);
  for (const std::wstring& r : roots) {
    libs.push_back(r);
    std::string vdf;
    if (ReadFileText(r + L"\\steamapps\\libraryfolders.vdf", vdf))
      for (const std::wstring& p : QuotedValues(vdf, "path")) libs.push_back(Trim(p));
  }
  return libs;
}
// путь с настоящими заглавными буквами (Steam хранит свой путь строчными)
static std::wstring RealCase(const std::wstring& p) {
  HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return p;
  wchar_t buf[MAX_PATH * 2];
  const DWORD n = GetFinalPathNameByHandleW(h, buf, MAX_PATH * 2, FILE_NAME_NORMALIZED);
  CloseHandle(h);
  if (!n || n >= MAX_PATH * 2) return p;
  std::wstring r(buf, n);
  if (r.rfind(L"\\\\?\\UNC\\", 0) == 0) return p;  // сетевые пути — как были
  if (r.rfind(L"\\\\?\\", 0) == 0) r = r.substr(4);
  return r;
}
static std::wstring FindGameRaw();
static std::wstring FindGame() { const std::wstring g = FindGameRaw(); return g.empty() ? g : RealCase(g); }
static std::wstring FindGameRaw() {
  // Steam: библиотеки и манифест игры (appid 214490)
  for (const std::wstring& lib : SteamLibraries()) {
    std::string acf;
    if (ReadFileText(lib + L"\\steamapps\\appmanifest_214490.acf", acf))
      for (const std::wstring& dir : QuotedValues(acf, "installdir"))
        if (IsGameDir(lib + L"\\steamapps\\common\\" + dir)) return lib + L"\\steamapps\\common\\" + dir;
    if (IsGameDir(lib + L"\\steamapps\\common\\Alien Isolation")) return lib + L"\\steamapps\\common\\Alien Isolation";
  }
  // «Установка и удаление программ»: Steam App 214490
  for (REGSAM view : { REGSAM(KEY_WOW64_64KEY), REGSAM(KEY_WOW64_32KEY) }) {
    const std::wstring p = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 214490",
                                  L"InstallLocation", view);
    if (IsGameDir(p)) return p;
  }
  // GOG: HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id>: gameName, path
  HKEY gog;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ, &gog) == ERROR_SUCCESS) {
    wchar_t sub[256];
    for (DWORD i = 0;; i++) {
      DWORD n = 256;
      if (RegEnumKeyExW(gog, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      const std::wstring key = std::wstring(L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\") + sub;
      const std::wstring name = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"gameName");
      const std::wstring path = RegStr(HKEY_LOCAL_MACHINE, key.c_str(), L"path");
      if (name.find(L"Alien") != std::wstring::npos && IsGameDir(path)) {
        RegCloseKey(gog);
        return path;
      }
    }
    RegCloseKey(gog);
  }
  // Epic: манифесты *.item (json): DisplayName / InstallLocation
  const std::wstring man = L"C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests\\";
  WIN32_FIND_DATAW fd;
  HANDLE f = FindFirstFileW((man + L"*.item").c_str(), &fd);
  if (f != INVALID_HANDLE_VALUE) {
    do {
      std::string js;
      if (!ReadFileText(man + fd.cFileName, js) || js.find("Alien") == std::string::npos) continue;
      for (const std::wstring& p : QuotedValues(js, "InstallLocation"))
        if (IsGameDir(Trim(p))) {
          FindClose(f);
          return Trim(p);
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
  }
  // типовые папки на всех дисках
  static const wchar_t* const kDirs[] = { L"Program Files (x86)\\Steam\\steamapps\\common", L"Program Files\\Steam\\steamapps\\common",
                                          L"SteamLibrary\\steamapps\\common", L"Steam\\steamapps\\common",
                                          L"Games\\Steam\\steamapps\\common", L"Games\\SteamLibrary\\steamapps\\common",
                                          L"GOG Games", L"Program Files (x86)\\GOG Galaxy\\Games", L"Program Files\\Epic Games",
                                          L"Epic Games", L"Games" };
  static const wchar_t* const kNames[] = { L"Alien Isolation", L"Alien - Isolation", L"AlienIsolation", L"Alien Isolation Collection" };
  const DWORD drives = GetLogicalDrives();
  for (int dr = 2; dr < 26; dr++) {  // C: … Z:
    if (!(drives & (1u << dr))) continue;
    const wchar_t root[4] = { wchar_t(L'A' + dr), L':', L'\\', 0 };
    if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
    for (const wchar_t* d : kDirs)
      for (const wchar_t* n : kNames) {
        const std::wstring p = std::wstring(root) + d + L"\\" + n;
        if (IsGameDir(p)) return p;
      }
  }
  return {};
}

// ---------------- установка ----------------
static bool GameRunning() {
  HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (s == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W e;
  e.dwSize = sizeof e;
  bool found = false;
  for (BOOL ok = Process32FirstW(s, &e); ok && !found; ok = Process32NextW(s, &e)) found = _wcsicmp(e.szExeFile, L"AI.exe") == 0;
  CloseHandle(s);
  return found;
}
static bool IsElevated() {
  HANDLE t;
  TOKEN_ELEVATION el{};
  DWORD n = 0;
  bool r = false;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
    if (GetTokenInformation(t, TokenElevation, &el, sizeof el, &n)) r = el.TokenIsElevated != 0;
    CloseHandle(t);
  }
  return r;
}
// то же действие — от администратора (папка игры в Program Files без прав на запись)
static int RunElevated(const std::wstring& args) {
  wchar_t self[MAX_PATH];
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  SHELLEXECUTEINFOW sei{ sizeof sei };
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  sei.lpFile = self;
  sei.lpParameters = args.c_str();
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei) || !sei.hProcess) return -1;
  WaitForSingleObject(sei.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(sei.hProcess, &code);
  CloseHandle(sei.hProcess);
  return int(code);
}
enum Result { RES_OK, RES_NO_GAME, RES_NO_SOURCE, RES_RUNNING, RES_ACCESS, RES_FAIL };
static Result DoInstall(const std::wstring& game, bool install) {
  if (!IsGameDir(game)) return RES_NO_GAME;
  if (GameRunning()) return RES_RUNNING;
  const std::wstring dst = game + kBmlRel, bak = dst + L".bak";
  const std::wstring src = ExeDir() + L"\\data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML";
  const std::wstring orig = ExeDir() + L"\\data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.original.BML";
  auto copy = [](const std::wstring& a, const std::wstring& b) -> Result {
    if (CopyFileW(a.c_str(), b.c_str(), FALSE)) return RES_OK;
    return GetLastError() == ERROR_ACCESS_DENIED ? RES_ACCESS : RES_FAIL;
  };
  if (install) {
    if (!Exists(src)) return RES_NO_SOURCE;
    if (!Exists(bak)) {  // оригинал игры — рядом; если в игре уже чужая/старая версия — кладём оригинал из мода
      const bool vanilla = !Exists(orig) || SameFile(dst, orig);
      const Result r = copy(vanilla ? dst : orig, bak);
      if (r != RES_OK) return r;
    }
    return copy(src, dst);
  }
  if (Exists(bak)) return copy(bak, dst);
  return Exists(orig) ? copy(orig, dst) : RES_FAIL;
}

// ---------------- ярлык на рабочем столе ----------------
static std::wstring ShortcutPath() {
  wchar_t desk[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk))) return {};
  return std::wstring(desk) + L"\\AlienMod (Alien Isolation).lnk";
}
static bool MakeShortcut() {
  IShellLinkW* sl = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&sl)))) return false;
  const std::wstring dir = ExeDir(), exe = dir + L"\\AlienInjectorUI.exe";
  sl->SetPath(exe.c_str());
  sl->SetWorkingDirectory(dir.c_str());
  sl->SetDescription(TR(L"Внедрить мод в Alien: Isolation (можно в любой момент игры)", L"Inject the mod into Alien: Isolation (any time)"));
  IPersistFile* pf = nullptr;
  bool ok = false;
  if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
    ok = SUCCEEDED(pf->Save(ShortcutPath().c_str(), TRUE));
    pf->Release();
  }
  sl->Release();
  return ok;
}

// ---------------- окно ----------------
enum { ID_PATH = 100, ID_BROWSE, ID_INSTALL, ID_UNINSTALL, ID_PLAY, ID_CLOSE, ID_SHORTCUT, ID_STATUS };
static HWND s_wnd, s_path, s_status;
static HFONT s_font, s_bold;
static COLORREF s_statusCol = RGB(0, 0, 0);
static float s_scale = 1.f;
static int S(int v) { return int(v * s_scale + 0.5f); }

static std::wstring GetText(HWND h) {
  std::wstring s(size_t(GetWindowTextLengthW(h)), L'\0');
  if (!s.empty()) GetWindowTextW(h, &s[0], int(s.size() + 1));
  return s;
}
static void SetStatus(const std::wstring& text, COLORREF c) {
  s_statusCol = c;
  SetWindowTextW(s_status, text.c_str());
  InvalidateRect(s_status, nullptr, TRUE);
}
// сборка AI.exe: мод сделан под Steam-версию от 17.02.2015 (штамп PE 0x54E3199D, SizeOfImage 0x01F81000). Другая сборка
// (GOG, Epic, другое обновление) — адреса другие, мод там не запустится (сам мод и инжектор это тоже проверяют)
static int GameBuild(const std::wstring& dir) {  // 1 — та, 0 — другая, -1 — не прочитать
  HANDLE f = CreateFileW((dir + L"\\AI.exe").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return -1;
  unsigned char buf[4096];
  DWORD got = 0;
  const bool rd = ReadFile(f, buf, sizeof buf, &got, nullptr) && got >= 0x200;
  CloseHandle(f);
  if (!rd) return -1;
  const LONG lfanew = *reinterpret_cast<LONG*>(buf + 0x3c);
  if (lfanew <= 0 || lfanew + 0x60 > LONG(got)) return -1;
  const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(buf + lfanew);
  return nt->FileHeader.TimeDateStamp == 0x54E3199D && nt->OptionalHeader.SizeOfImage == 0x01F81000 ? 1 : 0;
}
static void Validate() {
  const std::wstring d = Normalize(GetText(s_path));
  const bool ok = IsGameDir(d);
  if (ok && GameBuild(d) == 0) {  // не Steam-сборка: ставить можно, но честно предупредить
    SetStatus(TR(L"Не Steam-версия игры (или другое обновление) — мод поставится, но часть функций или весь мод может не работать",
                 L"Not the Steam build (or another update) — the mod will install, but some features or the whole mod may not work"),
              RGB(200, 120, 20));
    EnableWindow(GetDlgItem(s_wnd, ID_INSTALL), TRUE);
    EnableWindow(GetDlgItem(s_wnd, ID_UNINSTALL), TRUE);
    EnableWindow(GetDlgItem(s_wnd, ID_PLAY), TRUE);
    return;
  }
  if (d.empty()) SetStatus(TR(L"Игра не найдена — укажи папку кнопкой «Обзор…»", L"Game not found — pick its folder with «Browse…»"), RGB(190, 60, 30));
  else if (!ok) SetStatus(TR(L"В этой папке нет Alien: Isolation (AI.exe и DATA\\BINARY_BEHAVIOR)", L"No Alien: Isolation here (AI.exe and DATA\\BINARY_BEHAVIOR)"), RGB(190, 60, 30));
  else if (Exists(d + kBmlRel + L".bak") && SameFile(d + kBmlRel, ExeDir() + L"\\data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML"))
    SetStatus(TR(L"Игра найдена, мод уже установлен (можно поставить ещё раз)", L"Game found, the mod is already installed (you can reinstall)"), RGB(30, 130, 60));
  else SetStatus(TR(L"Игра найдена — можно ставить", L"Game found — ready to install"), RGB(30, 130, 60));
  EnableWindow(GetDlgItem(s_wnd, ID_INSTALL), ok);
  EnableWindow(GetDlgItem(s_wnd, ID_UNINSTALL), ok);
  EnableWindow(GetDlgItem(s_wnd, ID_PLAY), ok);
}
static void Browse() {
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg)))) return;
  DWORD opt = 0;
  dlg->GetOptions(&opt);
  dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(TR(L"Папка Alien: Isolation (где лежит AI.exe)", L"Alien: Isolation folder (the one with AI.exe)"));
  dlg->SetOkButtonLabel(TR(L"Выбрать папку", L"Select folder"));
  std::wstring start = Normalize(GetText(s_path));
  while (!start.empty() && !Exists(start)) start = Parent(start);
  if (!start.empty()) {
    IShellItem* it = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&it)))) {
      dlg->SetFolder(it);
      it->Release();
    }
  }
  if (SUCCEEDED(dlg->Show(s_wnd))) {
    IShellItem* it = nullptr;
    if (SUCCEEDED(dlg->GetResult(&it))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        SetWindowTextW(s_path, Normalize(p).c_str());
        CoTaskMemFree(p);
      }
      it->Release();
    }
  }
  dlg->Release();
  Validate();
}
static std::wstring ResultText(Result r, bool install) {
  switch (r) {
    case RES_OK: return install ? TR(L"Готово: мод в игре. Запусти игру, потом AlienInjectorUI.exe (ярлык «AlienMod») — в любой момент, хоть на уровне.",
                                   L"Done: the mod is in the game. Start the game, then AlienInjectorUI.exe (the «AlienMod» shortcut) — any time, even mid-level.")
                              : TR(L"Готово: в игре снова оригинальное дерево поведения.", L"Done: the game has its original behaviour tree again.");
    case RES_NO_GAME: return TR(L"В этой папке нет Alien: Isolation.", L"No Alien: Isolation in this folder.");
    case RES_NO_SOURCE: return TR(L"Рядом с установщиком нет data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML — распакуй мод целиком.",
                                L"data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML is missing next to the installer — unpack the whole mod.");
    case RES_RUNNING: return TR(L"Сначала закрой игру: файл, который меняем, она держит открытым.", L"Close the game first: it keeps the file open.");
    case RES_ACCESS: return TR(L"Нет прав на папку игры (и от администратора не вышло).", L"No write access to the game folder (even as administrator).");
    default: return TR(L"Не удалось скопировать файл.", L"Could not copy the file.");
  }
}
static void Act(bool install) {
  const std::wstring game = Normalize(GetText(s_path));
  if (install && GameBuild(game) == 0)  // не Steam-сборка: окно сразу на двух языках, установка всё равно идёт
    MessageBoxW(s_wnd,
      L"Эта Alien: Isolation — не Steam-сборка от 17.02.2015 (GOG, Epic или другое обновление).\n"
      L"Мод сделан под Steam-версию: часть функций или весь мод может не работать, игра может вылететь.\n"
      L"Мод всё равно будет установлен.\n\n"
      L"This Alien: Isolation is not the Steam build of 17 Feb 2015 (GOG, Epic or another update).\n"
      L"The mod is made for the Steam version: some features or the whole mod may not work, and the game may crash.\n"
      L"The mod will be installed anyway.",
      L"Alien Playable", MB_OK | MB_ICONWARNING);
  Result r = DoInstall(game, install);
  if (r == RES_ACCESS && !IsElevated()) {  // Program Files без прав — то же самое от администратора
    const int code = RunElevated(std::wstring(install ? L"--install \"" : L"--uninstall \"") + game + L"\"");
    r = code < 0 ? RES_ACCESS : Result(code);
  }
  if (r == RES_OK && install && SendDlgItemMessageW(s_wnd, ID_SHORTCUT, BM_GETCHECK, 0, 0) == BST_CHECKED) MakeShortcut();
  if (r == RES_OK && !install) DeleteFileW(ShortcutPath().c_str());
  if (r == RES_OK) {
    const std::wstring ini = ExeDir() + L"\\AlienModUI.ini";  // где игра — моду и инжектору
    WritePrivateProfileStringW(L"setup", L"game", game.c_str(), ini.c_str());
  }
  SetStatus(ResultText(r, install), r == RES_OK ? RGB(30, 130, 60) : RGB(190, 60, 30));
}
static void Play() {
  const std::wstring game = Normalize(GetText(s_path));
  std::wstring low = game;
  std::transform(low.begin(), low.end(), low.begin(), ::towlower);
  if (low.find(L"steamapps") != std::wstring::npos) ShellExecuteW(nullptr, L"open", L"steam://rungameid/214490", nullptr, nullptr, SW_SHOWNORMAL);
  else ShellExecuteW(nullptr, L"open", (game + L"\\AI.exe").c_str(), nullptr, game.c_str(), SW_SHOWNORMAL);
}

static HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), s_wnd,
                           reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(c, WM_SETFONT, WPARAM(f ? f : s_font), TRUE);
  return c;
}
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      s_wnd = h;
      Ctl(L"STATIC", TR(L"AlienMod — играть за Чужого и лицехватов", L"AlienMod — play as the Alien and the facehuggers"), SS_LEFT, 20, 16, 520, 26,
          0, s_bold);
      Ctl(L"STATIC", TR(L"Папка Alien: Isolation (там, где AI.exe):", L"Alien: Isolation folder (the one with AI.exe):"), SS_LEFT, 20, 52, 520, 20, 0);
      s_path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(20), S(74), S(410),
                               S(26), h, reinterpret_cast<HMENU>(INT_PTR(ID_PATH)), GetModuleHandleW(nullptr), nullptr);
      SendMessageW(s_path, WM_SETFONT, WPARAM(s_font), TRUE);
      Ctl(L"BUTTON", TR(L"Обзор…", L"Browse…"), WS_TABSTOP | BS_PUSHBUTTON, 438, 73, 102, 28, ID_BROWSE);
      s_status = Ctl(L"STATIC", L"", SS_LEFT, 20, 108, 520, 40, ID_STATUS);
      HWND sc = Ctl(L"BUTTON", TR(L"Ярлык «AlienMod» на рабочем столе", L"«AlienMod» shortcut on the desktop"), WS_TABSTOP | BS_AUTOCHECKBOX, 20, 150, 400,
                    22, ID_SHORTCUT);
      SendMessageW(sc, BM_SETCHECK, BST_CHECKED, 0);
      Ctl(L"BUTTON", TR(L"Установить", L"Install"), WS_TABSTOP | BS_DEFPUSHBUTTON, 20, 184, 128, 32, ID_INSTALL);
      Ctl(L"BUTTON", TR(L"Удалить из игры", L"Remove from game"), WS_TABSTOP | BS_PUSHBUTTON, 156, 184, 140, 32, ID_UNINSTALL);
      Ctl(L"BUTTON", TR(L"Запустить игру", L"Start the game"), WS_TABSTOP | BS_PUSHBUTTON, 304, 184, 128, 32, ID_PLAY);
      Ctl(L"BUTTON", TR(L"Закрыть", L"Close"), WS_TABSTOP | BS_PUSHBUTTON, 440, 184, 100, 32, ID_CLOSE);
      Ctl(L"STATIC",
          TR(L"В игру копируется один файл — DATA\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML (деревья поведения с ветками мода). "
             L"Оригинал сохраняется рядом (.bak), «Удалить из игры» возвращает его.",
             L"Only one file goes into the game — DATA\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML (behaviour trees with the mod's branches). "
             L"The original is kept next to it (.bak); «Remove from game» puts it back."),
          SS_LEFT, 20, 228, 520, 48, 0);
      SetStatus(TR(L"Ищу игру…", L"Looking for the game…"), RGB(90, 90, 90));
      UpdateWindow(h);
      SetWindowTextW(s_path, FindGame().c_str());
      Validate();
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_PATH: if (HIWORD(w) == EN_CHANGE) Validate(); break;
        case ID_BROWSE: Browse(); break;
        case ID_INSTALL: Act(true); break;
        case ID_UNINSTALL: Act(false); break;
        case ID_PLAY: Play(); break;
        case ID_CLOSE: DestroyWindow(h); break;
      }
      return 0;
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(l) == s_status) {
        SetTextColor(reinterpret_cast<HDC>(w), s_statusCol);
        SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
        return LRESULT(GetSysColorBrush(COLOR_WINDOW));
      }
      SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
      return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 3 && (wcscmp(argv[1], L"--install") == 0 || wcscmp(argv[1], L"--uninstall") == 0))  // вызов самого себя с правами
    return int(DoInstall(Normalize(argv[2]), wcscmp(argv[1], L"--install") == 0));
  if (argc >= 3 && wcscmp(argv[1], L"--find") == 0) {  // для проверки: найденная папка игры -> файл (UTF-8)
    const std::wstring g = FindGame();
    HANDLE f = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      const int n = WideCharToMultiByte(CP_UTF8, 0, g.c_str(), int(g.size()), nullptr, 0, nullptr, nullptr);
      std::string u(size_t(std::max(n, 0)), '\0');
      if (n > 0) WideCharToMultiByte(CP_UTF8, 0, g.c_str(), int(g.size()), &u[0], n, nullptr, nullptr);
      DWORD w = 0;
      WriteFile(f, u.data(), DWORD(u.size()), &w, nullptr);
      CloseHandle(f);
    }
    return g.empty() ? 1 : 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_STANDARD_CLASSES };
  InitCommonControlsEx(&icc);
  HDC dc = GetDC(nullptr);
  s_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.f;
  ReleaseDC(nullptr, dc);
  NONCLIENTMETRICSW ncm{ sizeof ncm };
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  LOGFONTW lf = ncm.lfMessageFont;
  lf.lfHeight = -S(15);
  s_font = CreateFontIndirectW(&lf);
  lf.lfHeight = -S(20);
  lf.lfWeight = FW_SEMIBOLD;
  s_bold = CreateFontIndirectW(&lf);
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"AlienModSetup";
  RegisterClassW(&wc);
  RECT r{ 0, 0, S(560), S(288) };
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  const int ww = r.right - r.left, wh = r.bottom - r.top;
  HWND h = CreateWindowExW(0, wc.lpszClassName, TR(L"Установка AlienMod", L"AlienMod setup"), style, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
  ShowWindow(h, show);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  CoUninitialize();
  return 0;
}
