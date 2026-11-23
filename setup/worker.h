#pragma once

static int DefaultLanguage() {
  switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_RUSSIAN: return 1;
    case LANG_SPANISH: return 2;
    case LANG_JAPANESE: return 3;
    case LANG_CHINESE: return 4;
    case LANG_FRENCH: return 5;
    default: return 0;
  }
}

static std::wstring SettingsPath() { return ExeDir() + L"\\AlienModUI.ini"; }
static void SaveSetting(const wchar_t* name, const std::wstring& value) {
  const auto path = SettingsPath();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    const wchar_t bom = 0xfeff;
    DWORD written = 0;
    WriteFile(file, &bom, sizeof bom, &written, nullptr);
    CloseHandle(file);
  }
  WritePrivateProfileStringW(L"setup", name, value.c_str(), path.c_str());
}
static std::wstring LogPath() {
  const std::wstring besideInstaller = ExeDir() + L"\\AlienModSetup.log";
  if (WriteBytes(besideInstaller, "", 0, true)) return besideInstaller;
  wchar_t local[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) {
    const std::wstring folder = std::wstring(local) + L"\\AlienMod";
    CreateDirectoryW(folder.c_str(), nullptr);
    const auto fallback = folder + L"\\AlienModSetup.log";
    WriteBytes(fallback, "", 0, true);
    return fallback;
  }
  return besideInstaller;
}

static void LogReport(const std::wstring& path, const wchar_t* action, const std::wstring& game, const Report& report) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t stamp[32]{};
  swprintf(stamp, 32, L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond);
  const auto message = Utf8(std::wstring(stamp) + L"  " + action + L"  [" +
      std::to_wstring(report.code) + L"]\r\n" + game + L"\r\n" + report.details + L"\r\n");
  WriteBytes(path, message.data(), DWORD(message.size()), true);
}

static Report Elevated(const std::wstring& game, Operation op, unsigned components, int language) {
  wchar_t tempFolder[MAX_PATH]{}, name[MAX_PATH]{}, self[MAX_PATH]{};
  Report result;
  auto fail = [&](DWORD error) {
    result.Add(RES_ACCESS, std::wstring(T(Text::AdminError, language)) + L"\r\n" + game +
                         L"\r\n" + SystemError(error, language));
  };
  if (!GetTempPathW(MAX_PATH, tempFolder) || !GetTempFileNameW(tempFolder, L"AMS", 0, name)) {
    fail(GetLastError());
    return result;
  }
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::wstring args = std::wstring(op == Operation::Restore ? L"--uninstall" : L"--install") +
      L" \"" + game + L"\" --report \"" + name + L"\" --lang " + std::to_wstring(language) +
      L" --components " + std::to_wstring(components);
  SHELLEXECUTEINFOW request{sizeof request};
  request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  request.lpVerb = L"runas";
  request.lpFile = self;
  request.lpParameters = args.c_str();
  request.nShow = SW_HIDE;
  if (!ShellExecuteExW(&request) || !request.hProcess) {
    fail(GetLastError());
  } else {
    DWORD code = RES_FAIL;
    WaitForSingleObject(request.hProcess, INFINITE);
    const bool hasCode = GetExitCodeProcess(request.hProcess, &code);
    const DWORD error = hasCode ? ERROR_SUCCESS : GetLastError();
    CloseHandle(request.hProcess);
    if (!hasCode) fail(error);
    else if (code != RES_OK) {
      result.code = code <= RES_FAIL ? Result(code) : RES_FAIL;
      if (!ReadWideReport(name, result.details))
        result.details = std::wstring(T(Text::AdminError, language)) + L"\r\n" + game +
                         L"\r\n[" + std::to_wstring(code) + L"]";
    }
  }
  DeleteFileW(name);
  return result;
}
