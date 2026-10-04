// runtime.cpp — основа вместо рантайма Frida: лог (AlienMod.log рядом с .asi + окно в ImGui), время, общий замок, setInterval,
// Safe() через векторный обработчик исключений, логирование вылетов (Process.setExceptionHandler в hot.js §0).
#include "interceptor.h"
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

addr g_base = 0, g_modEnd = 0;
SafeCallFn g_safeCall = nullptr;
static uint8_t* g_resume = nullptr;

// ---------------- время ----------------
ms_t NowMs() {
  static const LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return ms_t(c.QuadPart * 1000 / f.QuadPart);
}

// ---------------- лог: AlienMod.log рядом с DLL (инжектор показывает его в консоли) ----------------
static CRITICAL_SECTION g_logCs;
static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static ms_t g_t0;
static bool g_logReady = false;
static std::deque<std::string> g_logRing;
static unsigned g_logSeq = 0;

// «дом» мода: папка и имя DLL без расширения — рядом с ней лог и .ini. Инжектор грузит копию из live\AlienModUI-123.dll
// (чтобы DLL можно было пересобирать при запущенной игре) — для неё дом всё равно исходная папка и имя AlienModUI.
static std::wstring g_modBase;
const std::wstring& ModBase() { return g_modBase; }
static std::wstring ComputeModBase(HMODULE self) {
  wchar_t full[MAX_PATH];
  GetModuleFileNameW(self, full, MAX_PATH);
  std::wstring p = full;
  const size_t slash = p.find_last_of(L'\\');
  std::wstring dir = slash == std::wstring::npos ? L"." : p.substr(0, slash);
  std::wstring base = slash == std::wstring::npos ? p : p.substr(slash + 1);
  if (size_t dot = base.find_last_of(L'.'); dot != std::wstring::npos) base.resize(dot);
  if (dir.size() >= 5 && _wcsicmp(dir.c_str() + dir.size() - 5, L"\\live") == 0) {  // копия инжектора
    dir.resize(dir.size() - 5);
    if (size_t dash = base.find(L'-'); dash != std::wstring::npos) base.resize(dash);
  }
  // лог по имени DLL: AlienMod.dll -> AlienMod.log, AlienModUI.dll -> AlienModUI.log, DevTools .asi -> AlienMod.log
  if (base.rfind(L"AlienMod", 0) != 0) base = L"AlienMod";
  return dir + L"\\" + base;
}

void LogOpen(HMODULE self) {
  InitializeCriticalSection(&g_logCs);
  g_t0 = NowMs();
  g_modBase = ComputeModBase(self);
  std::wstring path = g_modBase + L".log";
  // лог прошлой версии (горячая замена мода, перезапуск игры) — рядом, .prev.log: в нём то, что было до замены
  MoveFileExW(path.c_str(), (g_modBase + L".prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
  g_logFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  g_logReady = true;
}

void Log(const char* fmt, ...) {
  // форматируем ДО замка: если в аргументах окажется битый указатель игры, упадём (и Safe поймает) без удержания замка
  char buf[1024];
  int n = snprintf(buf, sizeof buf, "[%9.3f] ", double(NowMs() - g_t0) / 1000.0);
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(buf + n, sizeof buf - n - 3, fmt, ap);
  va_end(ap);
  n += std::max(0, std::min(m, int(sizeof buf) - n - 4));
  buf[n++] = '\r';
  buf[n++] = '\n';
  buf[n] = 0;
  OutputDebugStringA(buf);
  if (!g_logReady) return;
  EnterCriticalSection(&g_logCs);
  if (g_logFile != INVALID_HANDLE_VALUE) {
    DWORD w;
    WriteFile(g_logFile, buf, DWORD(n), &w, nullptr);
  }
  buf[n - 2] = 0;  // без \r\n — для окна лога в ImGui
  g_logRing.emplace_back(buf);
  if (g_logRing.size() > 400) g_logRing.pop_front();
  g_logSeq++;
  LeaveCriticalSection(&g_logCs);
}

void LogClose() {  // мод отцеплен: файл лога отпускаем (новая DLL откроет его заново)
  if (!g_logReady) return;
  EnterCriticalSection(&g_logCs);
  if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile), g_logFile = INVALID_HANDLE_VALUE;
  LeaveCriticalSection(&g_logCs);
}

// копия последних строк лога для ImGui (свой замок, общий замок мода не нужен)
unsigned LogRingCopy(std::vector<std::string>& out, unsigned haveSeq) {
  if (!g_logReady) return 0;
  EnterCriticalSection(&g_logCs);
  unsigned seq = g_logSeq;
  if (seq != haveSeq) out.assign(g_logRing.begin(), g_logRing.end());
  LeaveCriticalSection(&g_logCs);
  return seq;
}

// ---------------- общий замок ----------------
static CRITICAL_SECTION g_js;
void JsLock() { EnterCriticalSection(&g_js); }
void JsUnlock() { LeaveCriticalSection(&g_js); }

// ---------------- состояние потока ----------------
static DWORD g_tls = TLS_OUT_OF_INDEXES;
ThreadState* TS(bool create) {
  if (g_tls == TLS_OUT_OF_INDEXES) return nullptr;
  auto* t = static_cast<ThreadState*>(TlsGetValue(g_tls));
  if (!t && create) {
    t = static_cast<ThreadState*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadState)));
    if (t) TlsSetValue(g_tls, t);
  }
  return t;
}

// ---------------- исполняемая память под заглушки ----------------
static uint8_t* g_code = nullptr;
static size_t g_codeUsed = 0;
enum { kCodeSize = 0x10000 };
uint8_t* CodeAlloc(size_t n) {
  if (!g_code) g_code = static_cast<uint8_t*>(VirtualAlloc(nullptr, kCodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  n = (n + 15) & ~size_t(15);
  if (!g_code || g_codeUsed + n > kCodeSize) return nullptr;
  uint8_t* p = g_code + g_codeUsed;
  g_codeUsed += n;
  return p;
}

bool Readable(const MEMORY_BASIC_INFORMATION& m) {
  const DWORD R = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  return m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (m.Protect & R);
}

// ---------------- Safe: охрана ----------------
static void*& ExceptionListRef() { return reinterpret_cast<void*&>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->ExceptionList); }

extern "C" void __cdecl GuardEnter(uint32_t esp, uint32_t ebp) {
  ThreadState* t = TS(true);
  if (!t) return;
  if (t->gdepth < kMaxGuards) t->guards[t->gdepth] = { esp, ebp, ExceptionListRef() };
  t->gdepth++;
}
extern "C" void __cdecl GuardLeave() {
  ThreadState* t = TS(false);
  if (t && t->gdepth > 0) t->gdepth--;
}
extern "C" void __cdecl GuardFault() {
  ThreadState* t = TS(false);
  if (!t || t->gdepth <= 0) return;
  if (t->gdepth <= kMaxGuards) ExceptionListRef() = t->guards[t->gdepth - 1].exList;  // кадры SEH прерванного кода мертвы
  t->gdepth--;
}

// int SafeCall(fn, arg): сохраняет ebx/esi/edi/ebp и слово управления FPU, регистрирует охрану, вызывает fn(arg).
// При ошибке доступа обработчик переставляет esp/ebp/eip на метку resume -> возврат 0.
static void BuildSafeCall() {
  uint8_t* s = CodeAlloc(96);
  if (!s) return;
  Asm a{ s };
  a.b({ 0x55, 0x89, 0xE5, 0x53, 0x56, 0x57 });  // push ebp; mov ebp,esp; push ebx; push esi; push edi
  a.b({ 0x83, 0xEC, 0x04, 0xD9, 0x7D, 0xF0 });  // sub esp,4; fnstcw [ebp-16]
  a.b({ 0x55, 0x8D, 0x45, 0xF0, 0x50 });        // push ebp; lea eax,[ebp-16]; push eax
  a.call(reinterpret_cast<const void*>(&GuardEnter));
  a.b({ 0x83, 0xC4, 0x08 });                    // add esp,8
  a.b({ 0xFF, 0x75, 0x0C, 0xFF, 0x55, 0x08 });  // push [ebp+12]; call [ebp+8]
  a.b({ 0x83, 0xC4, 0x04 });                    // add esp,4
  a.call(reinterpret_cast<const void*>(&GuardLeave));
  a.b({ 0xB8, 0x01, 0x00, 0x00, 0x00 });        // mov eax,1
  uint8_t* epilogue = a.p;
  a.b({ 0x83, 0xC4, 0x04, 0x5F, 0x5E, 0x5B, 0x5D, 0xC3 });  // add esp,4; pop edi; pop esi; pop ebx; pop ebp; ret
  g_resume = a.p;
  a.b({ 0xDB, 0xE3, 0xD9, 0x6D, 0xF0 });        // fninit; fldcw [ebp-16]
  a.call(reinterpret_cast<const void*>(&GuardFault));
  a.b({ 0x31, 0xC0, 0xEB });                    // xor eax,eax; jmp epilogue
  *a.p = uint8_t(epilogue - (a.p + 1));
  a.p++;
  FlushInstructionCache(GetCurrentProcess(), s, size_t(a.p - s));
  g_safeCall = reinterpret_cast<SafeCallFn>(s);
}

// цепочка адресов возврата в коде игры со стека (для разбора вылетов)
void LogStackChain(const char* tag, uint32_t esp) {
  char chain[240];
  int n = 0, k = 0;
  const uint32_t* sp = reinterpret_cast<const uint32_t*>(uintptr_t(esp));
  for (int i = 0; i < 512 && k < 12; i++) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(sp + i, &m, sizeof m) || !Readable(m)) break;
    const uint32_t v = sp[i];
    if (InGame(v)) n += snprintf(chain + n, sizeof chain - n, " %08X", unsigned(ToVA(v))), k++;
  }
  chain[n] = 0;
  Log("%s, stack:%s", tag, n ? chain : " -");
}
// исключение C++ игры (0xE06D7363): без обработчика игра зовёт terminate() -> abort() (Windows: код 0x40000015 в _abort).
// Пишем, какого типа и откуда брошено — первые 20
static void LogCppThrow(EXCEPTION_POINTERS* ep) {
  static LONG s_n = 0;
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  if (InterlockedIncrement(&s_n) > 20 || er->NumberParameters < 3) return;
  char type[96] = "?";
  const uintptr_t ti = er->ExceptionInformation[2];
  auto rdp = [](uintptr_t a, uintptr_t& v) {
    MEMORY_BASIC_INFORMATION m;
    if (!a || !VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof m) || !Readable(m)) return false;
    v = *reinterpret_cast<const uint32_t*>(a);
    return true;
  };
  uintptr_t cta = 0, ct = 0, td = 0;
  if (rdp(ti + 0xc, cta) && rdp(cta + 4, ct) && rdp(ct + 4, td) && td) {
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(reinterpret_cast<void*>(td + 8), &m, sizeof m) && Readable(m))
      snprintf(type, sizeof type, "%s", reinterpret_cast<const char*>(td + 8));
  }
  char chain[200];
  int n = 0, k = 0;
  const uint32_t* sp = reinterpret_cast<const uint32_t*>(uintptr_t(ep->ContextRecord->Esp));
  for (int i = 0; i < 512 && k < 10; i++) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(sp + i, &m, sizeof m) || !Readable(m)) break;
    const uint32_t v = sp[i];
    if (InGame(v)) n += snprintf(chain + n, sizeof chain - n, " %08X", unsigned(ToVA(v))), k++;
  }
  chain[n] = 0;
  Log("C++ EXCEPTION in the game: %s, stack:%s", type, n ? chain : " -");
}

static LONG CALLBACK Veh(EXCEPTION_POINTERS* ep) {
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  if (code == 0xE06D7363) {
    LogCppThrow(ep);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_DATATYPE_MISALIGNMENT: case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: break;
    default: return EXCEPTION_CONTINUE_SEARCH;
  }
  DWORD le = GetLastError();
  CONTEXT* c = ep->ContextRecord;
  ThreadState* t = TS(false);
  if (t && t->gdepth > 0 && t->gdepth <= kMaxGuards) {
    const GuardRec& r = t->guards[t->gdepth - 1];
    c->Esp = r.esp;
    c->Ebp = r.ebp;
    c->Eip = DWORD(uintptr_t(g_resume));
    SetLastError(le);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  static LONG s_logged = 0;
  if (InGame(c->Eip) && InterlockedIncrement(&s_logged) <= 50) {
    Log("CRASH? code %08lX pc VA %08X addr %08X (первый шанс — игра может обработать сама)", code, unsigned(ToVA(c->Eip)),
        code == EXCEPTION_ACCESS_VIOLATION ? unsigned(ep->ExceptionRecord->ExceptionInformation[1]) : 0u);
    // цепочка вызовов: адреса возврата в коде игры со стека (для разбора — DEVELOPERS.md)
    char chain[200];
    int n = 0, k = 0;
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(uintptr_t(c->Esp));
    for (int i = 0; i < 256 && k < 8; i++) {
      MEMORY_BASIC_INFORMATION m;
      if (!VirtualQuery(sp + i, &m, sizeof m) || !Readable(m)) break;
      const uint32_t v = sp[i];
      if (InGame(v)) n += snprintf(chain + n, sizeof chain - n, " %08X", unsigned(ToVA(v))), k++;
    }
    chain[n] = 0;
    Log("CRASH? stack:%s", n ? chain : " -");
  }
  SetLastError(le);
  return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------- setInterval ----------------
struct Task { int period; const char* name; void (*fn)(); ms_t next; int errs; };
static std::vector<Task> g_tasks;
void Every(int periodMs, const char* name, void (*fn)()) { g_tasks.push_back({ periodMs, name, fn, 0, 0 }); }

static std::atomic<bool> g_schedStop{ false };
static HANDLE g_schedThread = nullptr;
static DWORD WINAPI SchedThread(void*) {
  Interceptor::IgnoreCurrentThread();
  timeBeginPeriod(1);
  while (!g_schedStop) {
    ms_t now = NowMs();
    for (Task& t : g_tasks) {
      if (now < t.next) continue;
      t.next = now + t.period;
      JsLock();
      bool ok = Safe([&] { t.fn(); });
      JsUnlock();
      if (!ok && t.errs++ < 5) Log("err: задача %s (ошибка доступа к памяти)", t.name);
    }
    Sleep(1);
  }
  timeEndPeriod(1);
  return 0;
}
void StartScheduler() { g_schedThread = CreateThread(nullptr, 0, SchedThread, nullptr, 0, nullptr); }
bool StopScheduler() {  // отцепление мода: задачи больше не зовутся (ждём, пока текущая досчитает)
  g_schedStop = true;
  if (!g_schedThread) return true;
  const bool ok = WaitForSingleObject(g_schedThread, 5000) == WAIT_OBJECT_0;
  CloseHandle(g_schedThread);
  g_schedThread = nullptr;
  return ok;
}

bool RuntimeInit() {
  InitializeCriticalSection(&g_js);
  g_tls = TlsAlloc();
  if (g_tls == TLS_OUT_OF_INDEXES) return false;
  BuildSafeCall();
  if (!g_safeCall) return false;
  AddVectoredExceptionHandler(1, Veh);
  return true;
}
