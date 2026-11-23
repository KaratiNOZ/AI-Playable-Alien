// AlienModSetup entry point and game discovery.
// The same operations are available in the window and the --install,
// --uninstall and --verify modes used for headless work.
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
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include "strings.h"

static const wchar_t* T(Text text, int language) {
  return kText[static_cast<size_t>(text)][language];
}

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
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(f, &size) || size.QuadPart > 64ll * 1024 * 1024) {
    CloseHandle(f);
    return false;
  }
  const DWORD n = DWORD(size.QuadPart);
  out.resize(n);
  DWORD got = 0;
  const bool ok = out.empty() || ReadFile(f, &out[0], DWORD(out.size()), &got, nullptr);
  CloseHandle(f);
  out.resize(got);
  return ok && got == n;
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
static std::wstring ShortcutPath() {
  wchar_t desk[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk))) return {};
  return std::wstring(desk) + L"\\AlienMod (Alien Isolation).lnk";
}
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
static HRESULT MakeShortcut() {
  const auto destination = ShortcutPath();
  if (destination.empty()) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
  IShellLinkW* link = nullptr;
  HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
      IID_IShellLinkW, reinterpret_cast<void**>(&link));
  if (FAILED(result)) return result;
  const auto directory = ExeDir();
  result = link->SetPath((directory + L"\\AlienInjectorUI.exe").c_str());
  if (SUCCEEDED(result)) result = link->SetWorkingDirectory(directory.c_str());
  if (SUCCEEDED(result)) result = link->SetDescription(L"Gagen's Playable Alien");
  IPersistFile* file = nullptr;
  if (SUCCEEDED(result))
    result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
  if (SUCCEEDED(result)) {
    result = file->Save(destination.c_str(), TRUE);
    file->Release();
  }
  link->Release();
  return result;
}

#include "operations.h"
#include "worker.h"

static std::wstring StatusReport(const std::wstring& path) {
  const bool valid = IsGameDir(path);
  const auto directory = ExeDir();
  const bool installed = valid && SameFile(path + kBmlRel, directory + kBmlRel);
  const bool backup = valid && (Exists(path + kBmlRel + L".bak") ||
      !LevelFiles(path + kLevels, L"COMMANDS.PAK.alienmod.bak").empty());
  const bool canRestore = valid && (installed || backup ||
      Exists(directory + L"\\data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.original.BML"));
  const bool levels = !LevelFiles(directory + kLevels, L"COMMANDS.PAK").empty();
  const bool treeSource = Exists(directory + kBmlRel);
  const bool dll = Exists(directory + L"\\AlienModUI.dll");
  const bool injector = Exists(directory + L"\\AlienInjectorUI.exe");
  return path + L"\n" + std::to_wstring(valid) + L"\n" + std::to_wstring(installed) + L"\n" +
      std::to_wstring(backup) + L"\n" + std::to_wstring(canRestore) + L"\n" +
      std::to_wstring(levels) + L"\n" + std::to_wstring(valid ? GameBuild(path) : -1) +
      L"\n" + std::to_wstring(treeSource) + L"\n" + std::to_wstring(dll) +
      L"\n" + std::to_wstring(injector);
}

static Report Launch(const std::wstring& game, int language) {
  Report report;
  if (!IsGameDir(game)) {
    report.Add(RES_NO_GAME, std::wstring(T(Text::MissingGame, language)) + L"\r\n" + game);
    return report;
  }
  const auto directory = ExeDir();
  const auto launcher = directory + L"\\AlienInjectorUI.exe";
  for (const auto& path : {launcher, directory + L"\\AlienModUI.dll"}) {
    if (!Exists(path))
      report.Add(RES_NO_SOURCE, std::wstring(T(Text::FileMissing, language)) + L"\r\n" + path);
  }
  if (report.code != RES_OK) return report;
  auto open = [&](const std::wstring& path, const wchar_t* options, const std::wstring& working) {
    SHELLEXECUTEINFOW request{sizeof request};
    request.fMask = SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpFile = path.c_str();
    request.lpParameters = options;
    request.lpDirectory = working.c_str();
    request.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&request)) return true;
    const auto error = GetLastError();
    report.Add(RES_FAIL, std::wstring(T(Text::OpenError, language)) + L"\r\n" + path +
                         L"\r\n" + SystemError(error, language));
    return false;
  };
  auto lower = game;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
  if (!GameRunning()) {
    const bool steam = lower.find(L"steamapps") != std::wstring::npos;
    if (!open(steam ? L"steam://rungameid/214490" : game + L"\\AI.exe", nullptr, game)) return report;
  }
  open(launcher, L"--no-focus --no-log", directory);
  return report;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  for (int i = 0; argv && i < argc; ++i) args.emplace_back(argv[i]);
  if (argv) LocalFree(argv);
  if (args.size() < 3) return RES_FAIL;
  const auto& action = args[1];
  if (action == L"--find") {
    const auto directory = FindGame();
    const auto bytes = Utf8(directory);
    return !WriteBytes(args[2], bytes.data(), DWORD(bytes.size())) || directory.empty();
  }
  if (action != L"--install" && action != L"--uninstall" && action != L"--verify" &&
      action != L"--status" && action != L"--play") return RES_FAIL;
  const Operation operation = action == L"--install" ? Operation::Install :
      action == L"--uninstall" ? Operation::Restore : Operation::Verify;
  unsigned components = Behaviour;
  int language = DefaultLanguage();
  bool managed = false, shortcut = false;
  std::wstring reportPath, progressPath;
  for (size_t i = 3; i + 1 < args.size(); i += 2) {
    if (args[i] == L"--report") reportPath = args[i + 1];
    if (args[i] == L"--progress") progressPath = args[i + 1];
    if (args[i] == L"--lang") language = std::clamp(_wtoi(args[i + 1].c_str()), 0, kLanguageCount - 1);
    if (args[i] == L"--components") components = unsigned(_wtoi(args[i + 1].c_str())) & (Behaviour | Levels);
    if (args[i] == L"--managed") managed = args[i + 1] == L"1";
    if (args[i] == L"--shortcut") shortcut = args[i + 1] == L"1";
  }
  Report report;
  const auto path = Normalize(args[2]);
  auto progress = [&](Text phase, int percent) {
    if (!managed || progressPath.empty()) return;
    const char* key = phase == Text::Backup ? "Backup" : phase == Text::Admin ? "Admin" :
        phase == Text::Writing ? "Writing" : phase == Text::Restoring ? "Restoring" : "Checking";
    const std::string line = std::string(key) + " " + std::to_string(percent) + "\n";
    WriteBytes(progressPath, line.data(), DWORD(line.size()), true);
  };
  try {
    if (action == L"--status") report.details = StatusReport(path);
    else if (action == L"--play") report = Launch(path, language);
    else {
      report = Perform(path, operation, components, language, progress);
      if (managed && report.code == RES_ACCESS && !IsElevated() && operation != Operation::Verify) {
        progress(Text::Admin, 0);
        const auto elevated = Elevated(path, operation, components, language);
        if (elevated.code == RES_OK) report = elevated;
        else report.Add(elevated.code, elevated.details);
      }
      if (report.code == RES_OK && managed && operation != Operation::Verify) {
        SaveSetting(L"game", path);
        if (operation == Operation::Install && shortcut) {
          const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
          const HRESULT error = SUCCEEDED(com) ? MakeShortcut() : com;
          if (SUCCEEDED(com)) CoUninitialize();
          if (FAILED(error))
            report.Add(RES_FAIL, std::wstring(T(Text::ShortcutError, language)) + L"\r\n" +
                                 ShortcutPath() + L"\r\n" + SystemError(error, language));
        }
      }
    }
  } catch (const std::exception& error) {
    report.Add(RES_FAIL, std::wstring(T(Text::FileOperation, language)) + L"\r\n" +
                         path + L"\r\n" + Widen(error.what()));
  }
  if (managed && action != L"--status") LogReport(LogPath(), action.c_str() + 2, path, report);
  if (!reportPath.empty() && !WriteBytes(reportPath, report.details.data(), DWORD(report.details.size() * sizeof(wchar_t))))
    return RES_FAIL;
  return report.code;
}
