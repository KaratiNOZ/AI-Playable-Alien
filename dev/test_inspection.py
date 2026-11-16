"""Compile the real inspection action transitions against a fake native API."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
src=(root/'inspection.cpp').read_text(encoding='utf-8')
ownership=src[src.index('bool OwnAction()'):src.index('bool Available()')]
frame=src[src.index('void AdvanceInspection('):src.index('\n}\nvoid InspectionReset')]
cooldown=src[src.index('std::map<int,ms_t> inspectedUntil'):src.index('\nvoid Release(')]
stub=r'''
#include <array>
#include <atomic>
#include <algorithm>
#include <map>
#include <cstdint>
#include <cassert>
#include <cstdio>
#undef __thiscall
#undef __fastcall
#define __thiscall
#define __fastcall
using DWORD=unsigned;
using addr=uint32_t;using ms_t=long long;using Spec=std::array<uint32_t,22>;
using PlayFn=void(*)(addr,const uint32_t*,uint32_t);using PrimToIdleFn=void(*)(addr);
namespace va{constexpr addr PrimToIdle=1;}
std::map<addr,uint32_t> memory;
addr actor=0x1000,npc=0x1000,ownedAction=0;uint8_t ownedSerial=0;
addr overrideRef[2]{};
struct GameInput{addr npc=0,loco=0;int camera=0;bool available=false,photo=false,paused=false,forced=false,moving=false,rawAny=false,network=false;} input;
bool network=false;bool MpAlienGameplay(){return network;}
addr loco=0x3000,hugger=0;int camMode=2,aiMode=0;
bool moving=false,alienBackstage=false,ductOn=false,ventCrawl=false,autoDuctPath=false,turnFollowBody=false;
struct{bool stay=false;}vent;struct{ms_t until=0;}attack;struct{bool rawAny=false;}lastInput;
bool locked=false,allowActions=true;int creates=0,stops=0,releases=0;
int phase=-1,candidate=0,plays=0,restores=0;ms_t phaseAt=0,tickAt=0,now=100;
bool armed=true;float g_inspectionProgress=0;
bool cancel=false,unloading=false,g_inspecting=false,available=true,paused=false,forced=false;
int g_inspectionHint=0;std::array<Spec,3> specs{};
struct{bool on=false;}photo;
struct{int high=1;}kInspectionPoints[1];
struct Invocation{char text[64]{};};
addr rp(addr a){return memory.at(a);}template<class T>T rd(addr a){return T(memory.at(a));}
template<class T>void wr(addr a,T v){memory[a]=v;}
ms_t NowMs(){return now;}bool GameMenuPaused(){return paused;}bool Available(){return available;}
bool ForcedRetreatActive(){return forced;}
bool VentBusy(){return false;}DWORD GetCurrentThreadId(){return 77;}
void JsLock(){assert(!locked);locked=true;}void JsUnlock(){assert(locked);locked=false;}
template<class F>bool Safe(F fn){try{fn();return true;}catch(...){return false;}}
void Log(const char*,...){}
void InspectionGameTick(){assert(!locked);}
void idle(addr a){assert(!locked);++restores;memory[a+0x1a]=1;++memory[a+0x19];}
void play(addr a,const uint32_t*,uint32_t){assert(!locked&&overrideRef[0]);++plays;memory[a+0x1a]=18;++memory[a+0x19];memory[a+8]=0;}
addr* create(addr manager,addr*out){assert(!locked&&manager==actor+0x2cd0);++creates;out[0]=0x6000;out[1]=0x7000;memory[0x6010]=5;memory[0x600c]=0;return out;}
void activate(addr obj){assert(!locked&&obj==0x6000&&memory[obj+0x1c]==18&&memory[obj+0x22]==1&&memory[obj+0x18]==1);}
void stop(addr obj){assert(!locked&&obj==0x6000&&memory[obj+0x20]==0);++stops;memory[obj+0x18]=0;}
void release(addr*ref){assert(!locked);++releases;ref[0]=ref[1]=0;}
template<class T>T Fn(addr v){uintptr_t fn=0;
 switch(v){case 1:fn=reinterpret_cast<uintptr_t>(&idle);break;case 0x6579f0:fn=reinterpret_cast<uintptr_t>(&play);break;
 case 0x682410:fn=reinterpret_cast<uintptr_t>(&create);break;case 0x683ce0:fn=reinterpret_cast<uintptr_t>(&activate);break;
 case 0x683db0:fn=reinterpret_cast<uintptr_t>(&stop);break;case 0x6f58d0:fn=reinterpret_cast<uintptr_t>(&release);break;default:assert(false);}
 return reinterpret_cast<T>(fn);}
'''
tests=r'''
void reset(){actor=npc=0x1000;ownedAction=0;ownedSerial=0;phase=-2;candidate=0;now=100;phaseAt=tickAt=100;
 armed=true;g_inspectionProgress=0;allowActions=true;
 network=false;inspectedUntil.clear();
 cancel=unloading=paused=forced=false;photo.on=false;available=g_inspecting=true;plays=restores=0;
 overrideRef[0]=overrideRef[1]=0;creates=stops=releases=0;
 memory={{0x136c,0x2000},{0x201a,1},{0x2019,0},{0x2008,0}};kInspectionPoints[0].high=1;}
int main(){Invocation inv;inv.text[0]=1;
 reset();InspectionFrame(inv);assert(phase==0&&plays==1);
 memory[0x2008]=4;InspectionFrame(inv);assert(phase==1&&plays==2);
 now+=6000;InspectionFrame(inv);assert(phase==1&&plays==2&&g_inspectionProgress==.25f);
 memory[0x2008]=4;InspectionFrame(inv);assert(phase==2&&plays==3);
 memory[0x2008]=4;InspectionFrame(inv);assert(!g_inspecting&&restores==1&&memory[0x201a]==1);
 reset();cancel=true;InspectionFrame(inv);assert(!g_inspecting&&plays==0&&restores==0);
 reset();InspectionFrame(inv);cancel=true;memory[0x2008]=4;InspectionFrame(inv);assert(!g_inspecting&&plays==1&&restores==1);
 reset();InspectionFrame(inv);++memory[0x2019];InspectionFrame(inv);assert(!g_inspecting&&restores==0);
 reset();InspectionFrame(inv);memory[0x201a]=57;InspectionFrame(inv);assert(!g_inspecting&&restores==0);
 reset();InspectionFrame(inv);photo.on=true;now+=60000;InspectionFrame(inv);assert(g_inspecting&&restores==0);
 photo.on=false;now+=50;InspectionFrame(inv);assert(g_inspecting&&restores==0);
 forced=true;InspectionFrame(inv);assert(!g_inspecting&&restores==1);
 reset();InspectionFrame(inv);npc=0;InspectionFrame(inv);assert(!g_inspecting&&restores==0);
 reset();InspectionFrame(inv);now+=30001;InspectionFrame(inv);assert(!g_inspecting&&restores==1);
 reset();kInspectionPoints[0].high=0;InspectionFrame(inv);memory[0x2008]=4;InspectionFrame(inv);assert(!g_inspecting&&plays==1&&restores==1);
 assert(creates==1&&stops==1&&releases==1);
 reset();kInspectionPoints[0].high=0;InspectionFrame(inv);now+=4600;InspectionFrame(inv);assert(g_inspecting&&restores==0);
 memory[0x2008]=4;InspectionFrame(inv);assert(!g_inspecting&&restores==1&&g_inspectionProgress==0&&!armed);
 reset();InspectionFrame(inv);now+=1000;InspectionFrame(inv);const auto progress=g_inspectionProgress;
 photo.on=true;now+=60000;InspectionFrame(inv);assert(g_inspectionProgress==progress);
 photo.on=false;now+=100;InspectionFrame(inv);assert(g_inspecting&&g_inspectionProgress==progress);
 reset();InspectionFrame(inv);allowActions=false;InspectionFrame(inv);assert(!g_inspecting&&restores==1);
 reset();InspectionFrame(inv);memory[0x2008]=4;InspectionFrame(inv);memory[0x2008]=4;InspectionFrame(inv);
 cancel=true;memory[0x2008]=4;InspectionFrame(inv);assert(!g_inspecting&&g_inspectionProgress==0&&!armed);
 reset();kInspectionPoints[0].high=0;InspectionFrame(inv);cancel=true;now+=300;InspectionFrame(inv);assert(!g_inspecting&&restores==1);
 reset();g_inspecting=false;InspectionFrame(inv);assert(plays==0);
 reset();InspectionFrame(inv);memory[0x600c]=5;npc=0;InspectionFrame(inv);assert(stops==0&&releases==1);
 reset();network=true;InspectionFrame(inv);memory[0x2008]=4;InspectionFrame(inv);
 memory[0x2008]=4;InspectionFrame(inv);memory[0x2008]=4;InspectionFrame(inv);
 assert(CooldownSeconds(0,now)==120&&CooldownSeconds(1,now)==0);
 assert(CooldownSeconds(0,now+119999)==1&&CooldownSeconds(0,now+120000)==0);
 input.network=false;assert(CooldownSeconds(0,now)==0);
 reset();network=true;InspectionFrame(inv);cancel=true;InspectionFrame(inv);assert(inspectedUntil.empty());
 reset();network=true;InspectionFrame(inv);now+=30001;InspectionFrame(inv);assert(inspectedUntil.empty());
 puts("PASS: native lifecycle, cancellation, pause and per-hiding-place multiplayer cooldown");}
'''
compiler=Path(os.environ.get('CXX',r'C:\msys64\mingw32\bin\g++.exe'))
env=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ.get('PATH',''))
with tempfile.TemporaryDirectory(prefix='alien-inspection-test-') as tmp:
    cpp,exe=Path(tmp)/'test.cpp',Path(tmp)/'test.exe'
    cpp.write_text(stub+cooldown+ownership+frame+tests.replace('InspectionFrame(inv)','OnGameFrame(inv)'),encoding='utf-8')
    subprocess.run([str(compiler),'-std=c++17','-O2','-static',str(cpp),'-o',str(exe)],env=env,check=True)
    subprocess.run([str(exe)],check=True)
