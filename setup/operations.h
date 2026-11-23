#pragma once

#include <filesystem>
#include <functional>
#include <memory>

enum Result { RES_OK, RES_NO_GAME, RES_NO_SOURCE, RES_RUNNING, RES_ACCESS, RES_FAIL };
enum class Operation { Install, Restore, Verify };
enum Component : unsigned { Behaviour = 1, Levels = 2 };
using Progress = std::function<void(Text, int)>;

struct Report {
  Result code = RES_OK;
  std::wstring details;
  void Add(Result result, std::wstring text) {
    if (code == RES_OK || result == RES_ACCESS) code = result;
    if (!details.empty()) details += L"\r\n\r\n";
    details += text;
  }
};

static std::wstring SystemError(DWORD error, int language) {
  LPWSTR buffer = nullptr;
  static constexpr LANGID langs[] = {0x0409, 0x0419, 0x0c0a, 0x0411, 0x0804, 0x040c};
  auto format = [&](LANGID lang) {
    return FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, lang, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  };
  if (!format(langs[language])) format(0);
  std::wstring result = L"[" + std::to_wstring(error) + L"]";
  if (buffer) {
    std::wstring message(buffer);
    while (!message.empty() && iswspace(message.back())) message.pop_back();
    result += L" " + message;
    LocalFree(buffer);
  }
  return result;
}

static bool WriteBytes(const std::wstring& path, const void* data, DWORD size, bool append = false) {
  HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE,
      FILE_SHARE_READ, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
  CloseHandle(file);
  return ok;
}

static bool ReadWideReport(const std::wstring& path, std::wstring& report) {
  std::string bytes;
  if (!ReadFileText(path, bytes) || bytes.empty() || bytes.size() % sizeof(wchar_t)) return false;
  report.resize(bytes.size() / sizeof(wchar_t));
  std::memcpy(report.data(), bytes.data(), bytes.size());
  return true;
}

static std::string Utf8(const std::wstring& s) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size, '\0');
  if (size) WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), size, nullptr, nullptr);
  return out;
}

struct FileEntry { std::wstring source, target, original, backup; bool level = false; };
namespace fs = std::filesystem;
static constexpr auto kLevels = L"\\DATA\\ENV\\PRODUCTION";

// The shipped layout mirrors the game's directories. Only COMMANDS.PAK files
// at <level>/WORLD or DLC/<level>/WORLD are accepted as level components.
static std::vector<std::wstring> LevelFiles(const std::wstring& root, const wchar_t* filename) {
  std::vector<std::wstring> files;
  for (const std::wstring& prefix : {std::wstring(), std::wstring(L"\\DLC")}) {
    std::error_code error;
    fs::directory_iterator it(root + prefix, fs::directory_options::skip_permission_denied, error), end;
    for (; it != end && !error; it.increment(error)) {
      std::error_code typeError;
      if (!it->is_directory(typeError) || it->is_symlink(typeError)) continue;
      const std::wstring relative = prefix + L"\\" + it->path().filename().wstring() + L"\\WORLD\\";
      if (Exists(root + relative + filename)) files.push_back(relative);
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

static std::vector<FileEntry> FilesFor(const std::wstring& game, unsigned components, Operation operation) {
  std::vector<FileEntry> files;
  if (components & Behaviour) {
    const auto source = ExeDir() + kBmlRel;
    const auto target = game + kBmlRel;
    files.push_back({source, target, ExeDir() + L"\\data\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.original.BML",
                     target + L".bak"});
  }
  if (components & Levels) {
    const std::wstring package = ExeDir() + kLevels, installed = game + kLevels;
    const auto levels = operation == Operation::Restore ? LevelFiles(installed, L"COMMANDS.PAK.alienmod.bak") :
                                                         LevelFiles(package, L"COMMANDS.PAK");
    for (const auto& level : levels) {
      const auto target = installed + level + L"COMMANDS.PAK";
      files.push_back({package + level + L"COMMANDS.PAK", target, {}, target + L".alienmod.bak", true});
    }
  }
  return files;
}

// Generated level files append data and change the table offsets at bytes
// 12–27. Compare the rest against the original before allowing installation.
static bool MatchesPackageBase(const std::wstring& original, const std::wstring& patch) {
  std::string a, b;
  return ReadFileText(original, a) && ReadFileText(patch, b) && a.size() >= 28 && b.size() > a.size() &&
      a.compare(0, 12, b, 0, 12) == 0 && a.compare(28, a.size() - 28, b, 28, a.size() - 28) == 0;
}

static DWORD ReplaceFileFrom(const std::wstring& source, const std::wstring& target) {
  // Stage next to the target, so a failed copy leaves the existing file intact.
  wchar_t temporary[MAX_PATH]{};
  if (!GetTempFileNameW(Parent(target).c_str(), L"AMO", 0, temporary)) return GetLastError();
  DWORD error = ERROR_SUCCESS;
  if (!CopyFileW(source.c_str(), temporary, FALSE) ||
      !MoveFileExW(temporary, target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    error = GetLastError();
  if (error) DeleteFileW(temporary);
  return error;
}

struct OperationLock {
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AlienModSetupOperation");
  bool held = false;
  ~OperationLock() {
    if (held) ReleaseMutex(mutex);
    if (mutex) CloseHandle(mutex);
  }
};

struct PreviousFile {
  wchar_t temporary[MAX_PATH]{};
  std::wstring target;
  ~PreviousFile() { if (*temporary) DeleteFileW(temporary); }
};

static Report Perform(const std::wstring& game, Operation operation, unsigned components, int language,
                      const Progress& progress = [](Text, int) {}) {
  auto text = [&](Text key) { return std::wstring(T(key, language)); };
  Report report;
  if (!IsGameDir(game)) {
    report.Add(RES_NO_GAME, text(Text::MissingGame) + L"\r\n" + game + L"\\AI.exe\r\n" + game + kBmlRel);
    return report;
  }
  if (operation != Operation::Verify && GameRunning()) {
    report.Add(RES_RUNNING, text(Text::Running) + L"\r\n" + game + L"\\AI.exe");
    return report;
  }
  const bool restore = operation == Operation::Restore;
  const bool verify = operation == Operation::Verify;
  OperationLock lock;
  if (!lock.mutex) {
    const DWORD error = GetLastError();
    report.Add(RES_FAIL, text(Text::FileOperation) + L"\r\n" + game + L"\r\n" + SystemError(error, language));
    return report;
  }
  const DWORD acquired = WaitForSingleObject(lock.mutex, 0);
  if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
    const DWORD error = acquired == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError();
    report.Add(RES_FAIL, text(Text::FileOperation) + L"\r\n" + game + L"\r\n" + SystemError(error, language));
    return report;
  }
  lock.held = true;
  const auto files = FilesFor(game, components, operation);
  if (files.empty()) {
    report.Add(RES_NO_SOURCE, text(Text::MissingLevels) + L"\r\n" + ExeDir() + kLevels);
    return report;
  }
  if (!restore) {
    for (const auto* binary : {L"\\AlienModUI.dll", L"\\AlienInjectorUI.exe"}) {
      const auto path = ExeDir() + binary;
      if (!Exists(path)) report.Add(RES_NO_SOURCE, text(Text::FileMissing) + L"\r\n" + path);
    }
  }
  for (const auto& file : files) {
    if (!restore && !Exists(file.source)) report.Add(RES_NO_SOURCE, text(Text::FileMissing) + L"\r\n" + file.source);
    if (!Exists(file.target)) report.Add(RES_FAIL, text(Text::FileMissing) + L"\r\n" + file.target);
    if (restore && !Exists(file.backup) && !Exists(file.original))
      report.Add(RES_FAIL, text(Text::NoBackup) + L"\r\n" + file.backup + L"\r\n" + file.original);
    if (restore && file.level && Exists(file.target) && !SameFile(file.backup, file.target) &&
        !MatchesPackageBase(file.backup, file.target))
      report.Add(RES_FAIL, text(Text::NoBackup) + L"\r\n" + file.backup + L"\r\n" + file.target);
    if (!restore && !verify && file.level && Exists(file.source) && Exists(file.target)) {
      const bool current = SameFile(file.target, file.source);
      const bool backedUp = Exists(file.backup);
      const bool stock = MatchesPackageBase(file.target, file.source);
      const bool usableBackup = backedUp && MatchesPackageBase(file.backup, file.source);
      if ((!stock && !usableBackup) || (backedUp && !usableBackup) ||
          (!current && !stock && !MatchesPackageBase(file.backup, file.target)))
        report.Add(RES_FAIL, text(Text::NoBackup) + L"\r\n" + file.target + L"\r\n" +
            file.source + L"\r\n" + file.backup);
    }
  }
  if (report.code != RES_OK) return report;

  auto copy = [&](const std::wstring& source, const std::wstring& target) {
    const DWORD error = ReplaceFileFrom(source, target);
    if (!error) return true;
    report.Add(error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD ? RES_ACCESS : RES_FAIL,
        text(Text::FileOperation) + L"\r\n" + source + L"\r\n→ " + target +
        L"\r\n" + SystemError(error, language));
    return false;
  };

  if (!restore && !verify) {
    progress(Text::Backup, 8);
    for (const auto& file : files)
      if (!Exists(file.backup)) {
        const auto& original = Exists(file.original) && !SameFile(file.target, file.original) ? file.original : file.target;
        if (!copy(original, file.backup)) return report;
      }
  }
  const Text phase = verify ? Text::Checking : restore ? Text::Restoring : Text::Writing;
  std::vector<std::unique_ptr<PreviousFile>> changes;
  changes.reserve(files.size());
  auto rollback = [&] {
    for (auto i = changes.rbegin(); i != changes.rend(); ++i) {
      const auto& previous = **i;
      if (!MoveFileExW(previous.temporary, previous.target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        // Keep the previous file available if Windows refuses to replace it.
        report.Add(RES_FAIL, text(Text::FileOperation) + L"\r\n" + previous.temporary +
            L"\r\n→ " + previous.target + L"\r\n" + SystemError(error, language));
        report.code = RES_FAIL;
        (*i)->temporary[0] = L'\0';
      }
    }
  };
  for (size_t i = 0; i < files.size(); ++i) {
    progress(phase, 20 + int(i * 65 / files.size()));
    const auto& file = files[i];
    const auto& source = restore ? (Exists(file.backup) ? file.backup : file.original) : file.source;
    if (SameFile(source, file.target)) continue;
    if (!verify) {
      auto previous = std::make_unique<PreviousFile>();
      previous->target = file.target;
      if (!GetTempFileNameW(Parent(file.target).c_str(), L"AMB", 0, previous->temporary) ||
          !CopyFileW(file.target.c_str(), previous->temporary, FALSE)) {
        const DWORD error = GetLastError();
        report.Add(error == ERROR_ACCESS_DENIED ? RES_ACCESS : RES_FAIL,
            text(Text::FileOperation) + L"\r\n" + file.target + L"\r\n" + SystemError(error, language));
        rollback();
        return report;
      }
      if (!copy(source, file.target)) {
        rollback();
        return report;
      }
      changes.push_back(std::move(previous));
      if (SameFile(source, file.target)) continue;
    }
    report.Add(RES_FAIL, text(Text::Mismatch) + L"\r\n" + source + L"\r\n" + file.target);
    if (!verify) {
      rollback();
      return report;
    }
  }
  progress(Text::Checking, 95);
  return report;
}
