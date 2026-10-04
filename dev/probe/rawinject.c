// rawinject.c — загрузить DLL в процесс (LoadLibraryW через CreateRemoteThread), больше ничего. rawinject <pid> <dll>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: rawinject <pid> <dll>\n"); return 1; }
  DWORD pid = (DWORD)_wtoi(argv[1]);
  wchar_t full[MAX_PATH];
  GetFullPathNameW(argv[2], MAX_PATH, full, NULL);
  HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
  if (!p) { wprintf(L"OpenProcess failed %lu\n", GetLastError()); return 2; }
  SIZE_T n = (wcslen(full) + 1) * sizeof(wchar_t);
  void* mem = VirtualAllocEx(p, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  WriteProcessMemory(p, mem, full, n, NULL);
  LPTHREAD_START_ROUTINE ll = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
  HANDLE t = CreateRemoteThread(p, NULL, 0, ll, mem, 0, NULL);
  if (!t) { wprintf(L"CreateRemoteThread failed %lu\n", GetLastError()); return 3; }
  WaitForSingleObject(t, 10000);
  DWORD code = 0; GetExitCodeThread(t, &code);
  wprintf(L"loaded: module %08lX (%ls)\n", code, full);
  VirtualFreeEx(p, mem, 0, MEM_RELEASE);
  return code ? 0 : 4;
}
