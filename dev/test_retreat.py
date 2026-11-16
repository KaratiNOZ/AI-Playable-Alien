"""Compile the real retreat tick and cancellation function with a fake game."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / 'retreat.cpp').read_text(encoding='utf-8')
body = source.split('namespace {', 1)[1].rsplit('}\nvoid InstallRetreat', 1)[0]
vent = (root / 'vent.cpp').read_text(encoding='utf-8')
cancel = vent[vent.index('static void CancelVent('):vent.index('\nvoid ActVentGo()')]
stub = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
using addr=uintptr_t; using ms_t=long long;
addr npc=0x1000; ms_t now=100; int gaugeAmount=0;
bool match=true, local=true, alienBackstage=false, autoOutPending=false, paused=false;
std::atomic<bool> g_forcedRetreat{false},g_forceRetreatRequested{false};
bool ForcedRetreatActive(){return g_forcedRetreat;}
ms_t NowMs(){return now;}
bool MpMatchActive(){return match;} bool MpAlienGameplay(){return local;}
bool GameMenuPaused(){return paused;}
uint64_t owner=0; int remoteRequests=0;
uint64_t PupAlienOwner(addr){return owner;}
void MpRequestRetreat(uint64_t id){assert(id==owner);++remoteRequests;}
addr rp(addr a){assert(a==npc+0x31f0);return 0x5000;}
template<class T>T rd(addr a){assert(a==0x5014);return T(gaugeAmount);}
namespace va {constexpr addr VT_GATE=1;}
bool Alive(addr a,addr){return a!=0;}
struct {ms_t goUntil=0,comeUntil=0,comeAt=0,comeEnd=0,keyAt=0;addr go=1;bool stay=false;} vent;
struct {ms_t until=0,startAt=0,nextUntil=0;addr gate=2;} attack;
enum {EXIT_NONE,EXIT_OTHER}; int s_exitMode=EXIT_NONE, exitEnds=0;
void ExitEnd(){++exitEnds;s_exitMode=EXIT_NONE;}
bool VentBusy(){return vent.goUntil>now||vent.comeUntil>now;}
void Log(const char*,...){} void SetGate(addr,bool){}
void ActVentCrawl(bool){} void AutoDuctReset(){}
int goCommands=0;
void ActVentGo(){assert(!ForcedRetreatActive());++goCommands;vent.goUntil=now+10000;vent.keyAt=now;}
void ActVentCancel();
'''
tests = r'''
void ActVentCancel(){CancelVent("menu");}
int main(){
  Tick(); assert(!ForcedRetreatActive());
  gaugeAmount=3; Tick(); assert(ForcedRetreatActive()&&goCommands==1);
  const auto end=vent.goUntil; const auto ends=exitEnds;
  for(const char* input:{"move","key","menu"}) CancelVent(input);
  assert(vent.goUntil==end&&exitEnds==ends&&ForcedRetreatActive());
  now+=11000; Tick(); assert(ForcedRetreatActive()&&vent.goUntil>now&&goCommands==1);
  alienBackstage=true; Tick(); assert(ForcedRetreatActive());
  vent.stay=true; Tick(); assert(!ForcedRetreatActive());
  alienBackstage=false; vent.stay=false; Tick(); assert(goCommands==1);
  gaugeAmount=0; Tick(); gaugeAmount=3; Tick(); assert(goCommands==2&&ForcedRetreatActive());
  match=false; Tick(); assert(!ForcedRetreatActive()&&vent.goUntil==0);
  match=true; local=false; owner=42; Tick(); assert(remoteRequests==1&&!ForcedRetreatActive());
  Tick(); assert(remoteRequests==1);
  local=true; gaugeAmount=0; Tick(); g_forceRetreatRequested=true; Tick(); assert(ForcedRetreatActive());
  paused=true; now+=60000; Tick(); assert(ForcedRetreatActive()&&vent.goUntil>now);
  paused=false; now+=50; Tick(); assert(ForcedRetreatActive());
  now+=45001; Tick(); assert(!ForcedRetreatActive()&&vent.goUntil==0);
  vent.go=0; gaugeAmount=0; Tick(); gaugeAmount=3; Tick(); assert(!ForcedRetreatActive());
  std::puts("PASS: native threshold, cancellation priority, renew/completion, level loss, relay latch, unreachable-vent recovery");
}
'''
out = root / 'build-ghidra'
path = out / 'test_retreat.cpp'
path.write_text(stub + cancel + body + tests, encoding='utf-8')
env = dict(os.environ, PATH=r'C:\msys64\mingw32\bin;' + os.environ.get('PATH', ''))
subprocess.run([r'C:\msys64\mingw32\bin\g++.exe', '-std=c++17', '-O2', '-static', str(path), '-o', str(out/'test_retreat.exe')], env=env, check=True)
subprocess.run([str(out/'test_retreat.exe')], check=True)
