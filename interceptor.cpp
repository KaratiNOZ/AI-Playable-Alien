// interceptor.cpp — Interceptor.attach на MinHook.
// MinHook ставит jmp и строит трамплин; вместо обычной «детур-функции» с известной сигнатурой каждый хук ведёт в
// универсальную заглушку: сохраняет все регистры и FPU, зовёт слушателей onEnter, и если нужен onLeave — подменяет
// адрес возврата на свою заглушку выхода (как Frida). Поэтому соглашение и число аргументов функции знать не нужно.
#include "interceptor.h"
#include "MinHook.h"
#include <cstring>
#include <map>
#include <vector>

struct Listener { Callback enter, leave; };
struct Hook {
  addr target = 0, tramp = 0;
  uint8_t* thunk = nullptr;
  std::vector<Listener> ls;
  bool anyLeave = false, lock = false;
};

static std::map<addr, Hook*> g_hooks;
static uint8_t* g_enterStub = nullptr;
static uint8_t* g_leaveStub = nullptr;
static uint8_t* g_retStubs = nullptr;  // inv.skip: «ret N» для N = 0, 4, ..., 60 (по 4 байта на заглушку)

struct EnterCtx { CpuContext cpu; uint32_t slot; uint32_t ret; };  // [slot] = Hook*, дальше адрес возврата и аргументы
struct LeaveCtx { CpuContext cpu; uint32_t slot; };                 // [slot] = куда вернуться

struct Pack { Callback cb; Invocation* inv; };
static void RunPack(void* p) { auto* k = static_cast<Pack*>(p); k->cb(*k->inv); }

static volatile bool g_dead = false;  // мод отцеплен: хуки выключены, слушателей больше не зовём

static void CallListeners(Hook* h, Invocation& inv, bool enter) {
  if (g_dead) return;  // поток мог войти в заглушку до выключения хука — пропускаем его без слушателей
  if (h->lock) JsLock();
  for (const Listener& l : h->ls) {
    Callback cb = enter ? l.enter : l.leave;
    if (!cb) continue;
    Pack k{ cb, &inv };
    g_safeCall(RunPack, &k);  // ошибка в слушателе не роняет игру (как исключение в колбэке Frida)
  }
  if (h->lock) JsUnlock();
}

extern "C" uint32_t __cdecl OnEnterC(EnterCtx* c) {
  Hook* h = reinterpret_cast<Hook*>(c->slot);
  DWORD le = GetLastError();
  ThreadState* ts = TS(true);
  if (ts && !ts->inCb) {
    Invocation inv;
    memset(&inv, 0, sizeof inv);
    inv.cpu = &c->cpu;
    inv.args = &c->ret + 1;
    inv.ecx = c->cpu.ecx;
    inv.ra = c->ret;
    for (int i = 0; i < 6; i++) inv.arg[i] = inv.args[i];
    ts->inCb++;
    CallListeners(h, inv, true);
    ts->inCb--;
    if (inv.skip && inv.skipPop <= 60 && !(inv.skipPop & 3)) {  // функцию не зовём: заглушка вместо трамплина
      SetLastError(le);
      return uint32_t(uintptr_t(g_retStubs + inv.skipPop));
    }
    if (h->anyLeave && ts->depth < kMaxFrames) {
      Frame& f = ts->frames[ts->depth++];
      f.hook = h;
      f.origRet = c->ret;
      f.entryEsp = uint32_t(uintptr_t(&c->ret));
      f.inv = inv;
      f.inv.cpu = nullptr;
      f.inv.args = nullptr;
      c->ret = uint32_t(uintptr_t(g_leaveStub));
    }
  }
  SetLastError(le);
  return uint32_t(h->tramp);
}

extern "C" void __cdecl OnLeaveC(LeaveCtx* c) {
  DWORD le = GetLastError();
  ThreadState* ts = TS(false);
  uint32_t leaveEsp = uint32_t(uintptr_t(&c->slot)) + 4;
  // кадры функций, покинутых исключением (не через ret), висят сверху — снимаем их
  while (ts->depth > 1 && ts->frames[ts->depth - 1].entryEsp + 4 + 0x40 < leaveEsp) ts->depth--;
  Frame& f = ts->frames[--ts->depth];
  c->slot = f.origRet;
  Invocation inv = f.inv;
  inv.cpu = &c->cpu;
  ts->inCb++;
  CallListeners(f.hook, inv, false);
  ts->inCb--;
  SetLastError(le);
}

// заглушка входа: [esp] = Hook* (кладёт персональный thunk), [esp+4] = адрес возврата
static void BuildStubs() {
  g_enterStub = CodeAlloc(64);
  Asm a{ g_enterStub };
  a.b({ 0x9C, 0x60, 0xFC, 0x89, 0xE3 });              // pushfd; pushad; cld; mov ebx,esp
  a.b({ 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00 });        // sub esp,512
  a.b({ 0x83, 0xE4, 0xF0, 0x0F, 0xAE, 0x04, 0x24 });  // and esp,-16; fxsave [esp]
  a.b({ 0xDB, 0xE3 });                                // fninit: свой код — с чистым стеком x87
  a.b({ 0x53 });                                      // push ebx (ctx)
  a.call(reinterpret_cast<const void*>(&OnEnterC));  // eax = трамплин
  a.b({ 0x83, 0xC4, 0x04, 0x0F, 0xAE, 0x0C, 0x24 });  // add esp,4; fxrstor [esp]
  a.b({ 0x89, 0xDC, 0x89, 0x44, 0x24, 0x24 });        // mov esp,ebx; mov [esp+36],eax (слот <- трамплин)
  a.b({ 0x61, 0x9D, 0xC3 });                          // popad; popfd; ret -> трамплин, стек как при входе

  g_leaveStub = CodeAlloc(64);
  a = Asm{ g_leaveStub };
  a.b({ 0x6A, 0x00, 0x9C, 0x60, 0xFC, 0x89, 0xE3 });  // push 0 (слот); pushfd; pushad; cld; mov ebx,esp
  a.b({ 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00 });
  a.b({ 0x83, 0xE4, 0xF0, 0x0F, 0xAE, 0x04, 0x24 });
  a.b({ 0xDB, 0xE3 });                                // fninit (ST0 = float-возврат игры сохранён в fxsave)
  a.b({ 0x53 });
  a.call(reinterpret_cast<const void*>(&OnLeaveC));  // слот <- настоящий адрес возврата, eax можно подменить
  a.b({ 0x83, 0xC4, 0x04, 0x0F, 0xAE, 0x0C, 0x24 });
  a.b({ 0x89, 0xDC, 0x61, 0x9D, 0xC3 });              // mov esp,ebx; popad; popfd; ret -> вызывающему

  // заглушки пропуска: в [esp] — настоящий адрес возврата, дальше аргументы
  g_retStubs = CodeAlloc(64);
  for (int n = 0; n <= 60; n += 4) {
    uint8_t* p = g_retStubs + n;
    if (n == 0) p[0] = 0xC3, p[1] = p[2] = p[3] = 0xCC;  // ret
    else p[0] = 0xC2, p[1] = uint8_t(n), p[2] = 0, p[3] = 0xCC;  // ret n
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}

namespace Interceptor {

bool Init() {
  BuildStubs();
  MH_STATUS s = MH_Initialize();
  if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
    Log("MH_Initialize: %s", MH_StatusToString(s));
    return false;
  }
  return true;
}

static void LogTarget(const char* what, addr target, MH_STATUS s) {
  if (InGame(target)) Log("%s VA %08X: %s", what, unsigned(ToVA(target)), MH_StatusToString(s));
  else Log("%s %08X: %s", what, unsigned(target), MH_StatusToString(s));
}

bool Attach(addr target, Callback onEnter, Callback onLeave, bool lock) {
  if (!target) return false;
  Hook* h;
  auto it = g_hooks.find(target);
  if (it != g_hooks.end()) {
    h = it->second;
  } else {
    h = new Hook();
    h->target = target;
    h->thunk = CodeAlloc(16);
    Asm a{ h->thunk };
    a.b({ 0x68 });  // push Hook*
    a.d(uint32_t(uintptr_t(h)));
    a.jmp(g_enterStub);
    MH_STATUS s = MH_CreateHook(reinterpret_cast<LPVOID>(target), h->thunk, reinterpret_cast<LPVOID*>(&h->tramp));
    if (s != MH_OK) {
      LogTarget("MH_CreateHook", target, s);
      delete h;
      return false;
    }
    g_hooks[target] = h;
  }
  h->ls.push_back({ onEnter, onLeave });
  if (onLeave) h->anyLeave = true;
  if (lock) h->lock = true;
  return true;
}

bool Commit() {
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  MH_STATUS s = MH_EnableHook(MH_ALL_HOOKS);
  if (s != MH_OK) Log("MH_EnableHook: %s", MH_StatusToString(s));
  Log("хуков: %u", unsigned(g_hooks.size()));
  return s == MH_OK;
}

int ActiveDepth(addr target) {
  ThreadState* ts = TS(false);
  if (!ts) return 0;
  int n = 0;
  for (int i = 0; i < ts->depth && i < kMaxFrames; i++) {
    const Frame& f = ts->frames[i];
    // кадр функции, покинутой исключением, ещё висит: живой — только если в его ячейке возврата стоит заглушка выхода
    if (f.hook && f.hook->target == target && *reinterpret_cast<volatile uint32_t*>(uintptr_t(f.entryEsp)) == uint32_t(uintptr_t(g_leaveStub)))
      n++;
  }
  return n;
}

bool AttachNow(addr target, Callback onEnter, Callback onLeave) {
  if (g_hooks.count(target)) {  // список слушателей живого хука не трогаем (его читают другие потоки)
    LogTarget("AttachNow: адрес уже перехвачен", target, MH_OK);
    return false;
  }
  if (!Attach(target, onEnter, onLeave, true)) return false;
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  MH_STATUS s = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (s != MH_OK) LogTarget("MH_EnableHook", target, s);
  return s == MH_OK;
}

// Отцепить: все хуки выключить (MinHook возвращает исходные байты). Заглушки и трамплины НЕ освобождаем — поток игры
// может стоять внутри трамплина или вернуться в заглушку выхода (подменённый адрес возврата); DLL остаётся в памяти.
bool Shutdown() {
  g_dead = true;
  MH_STATUS s = MH_DisableHook(MH_ALL_HOOKS);
  if (s != MH_OK) Log("MH_DisableHook(all): %s", MH_StatusToString(s));
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return s == MH_OK;
}

void IgnoreCurrentThread() {
  if (ThreadState* t = TS(true)) t->inCb = 1;
}

}  // namespace Interceptor
