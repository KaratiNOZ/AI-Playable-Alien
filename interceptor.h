// interceptor.h — аналог Interceptor.attach из Frida: хук по адресу без знания сигнатуры,
// onEnter (регистры/аргументы, можно менять ecx и аргументы) и onLeave (можно подменить возврат), несколько слушателей на адрес.
#pragma once
#include "common.h"
#include <initializer_list>

struct CpuContext { uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, eflags; };  // порядок pushad

struct Invocation {
  CpuContext* cpu;   // живые регистры: в onEnter можно менять (this.context.ecx = ...), в onLeave eax = возврат
  uint32_t* args;    // только в onEnter: стековые аргументы, можно менять (args[4] = ptr(0))
  addr ecx;          // ecx на входе (this у __thiscall)
  addr ra;           // настоящий адрес возврата (this.returnAddress)
  uint32_t arg[6];   // копия аргументов на входе — доступна и в onLeave
  char text[64];     // место под данные между onEnter и onLeave (this.x во Frida)
  // только в onEnter: не вызывать функцию — сразу вернуться к вызывающему (eax = cpu->eax), сняв skipPop байт
  // аргументов (0 — cdecl, 4*N — stdcall/thiscall). onLeave при этом не зовётся.
  bool skip;
  uint16_t skipPop;
  addr retval() const { return cpu->eax; }
  void replaceRet(addr v) { cpu->eax = v; }
};
typedef void (*Callback)(Invocation&);

namespace Interceptor {
bool Init();
// до Commit(); lock = брать общий замок (как JS во Frida). Слушатели одного адреса вызываются в порядке установки.
bool Attach(addr target, Callback onEnter, Callback onLeave, bool lock = true);
bool Commit();
// поставить хук во время игры (только на новый адрес)
bool AttachNow(addr target, Callback onEnter, Callback onLeave);
// отцепить мод: выключить все хуки, слушателей больше не звать (память хуков остаётся — DLL не выгружается)
bool Shutdown();
// наши потоки (таймеры, оверлей): хуки на них не срабатывают — как поток JS во Frida
void IgnoreCurrentThread();
// сколько вызовов target сейчас «живы» выше по стеку этого потока (вызов, в чьём onEnter спрашивают, не считается).
// Видно только у хуков с onLeave: их кадры лежат в ThreadState.
int ActiveDepth(addr target);
}

// ---- внутреннее: состояние потока (кадры для onLeave + охрана Safe) ----
struct Hook;
struct GuardRec { uint32_t esp, ebp; void* exList; };
struct Frame { Hook* hook; uint32_t origRet, entryEsp; Invocation inv; };
enum { kMaxGuards = 32, kMaxFrames = 64 };
struct ThreadState { int inCb, depth, gdepth; GuardRec guards[kMaxGuards]; Frame frames[kMaxFrames]; };
ThreadState* TS(bool create);
uint8_t* CodeAlloc(size_t n);

// крошечный «ассемблер» для заглушек
struct Asm {
  uint8_t* p;
  void b(std::initializer_list<uint8_t> xs) { for (uint8_t x : xs) *p++ = x; }
  void d(uint32_t v) { for (int i = 0; i < 4; i++) *p++ = uint8_t(v >> (i * 8)); }
  void rel(uint8_t op, const void* t) { *p++ = op; d(uint32_t(uintptr_t(t)) - uint32_t(uintptr_t(p + 4))); }
  void call(const void* t) { rel(0xE8, t); }
  void jmp(const void* t) { rel(0xE9, t); }
};
