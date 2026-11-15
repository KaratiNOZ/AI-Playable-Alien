"""Test the real duct-entry decision code using deterministic geometry/input."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/"duct_entry.cpp").read_text(encoding="utf-8")
source="\n".join(line for line in source.splitlines() if not line.startswith('#include'))
prefix=r"""
#include <cmath>
#include <cstdint>
#include <cassert>
#include <cstdio>
using addr=uint32_t; using ms_t=long long;
using PathCancelFn=void(*)(addr,addr,addr);
namespace va { constexpr addr PathCancel=2; }
int cancelled=0;
void cancelPath(addr,addr,addr){++cancelled;}
template<class T>T Fn(addr){return &cancelPath;}
struct V3{float x,y,z;};
struct Input{bool any=true,rawAny=true; float camYaw=0,worldYaw=0; int speed=1;} lastInput;
addr npc=1,player=2,hugger=0,pathGate=3,gate=4;
int camMode=2,aiMode=0;
bool autoDuctPath=false,alienBackstage=false,alienLooking=false,g_uiMenuShown=false,g_pausePass=false,ductOn=false;
struct{bool on=false;}photo;
struct{bool stay=false;}vent;
struct{ms_t until=0;}attack;
namespace va{constexpr addr VT_GATE=1;}
ms_t clockNow=1000;
V3 position{}; bool crouched=false,focused=true,solid=false,noHigh=false,noFloor=false;
bool clearAbove=false; bool pathEnabled=false; float floorY=0.4f;
ms_t NowMs(){return clockNow;}
bool VentBusy(){return false;}
bool GameFocused(){return focused;}
bool Alive(addr,addr){return true;}
float WrapPi(float x){return std::remainder(x,6.28318530718f);}
V3 rv3(addr){return position;}
template<class T>T rd(addr){return T(crouched);}
void Log(const char*,...){}
void SetGate(addr a,bool b){if(a==pathGate)pathEnabled=b;}
bool RayHit(const V3& a,const V3& b,V3& hit){
 if(std::hypot(a.x-b.x,a.z-b.z)<0.01f)return !clearAbove;
 if(a.y<position.y+1.0f)return solid;
 if(noHigh)return false;
 hit={a.x+(b.x-a.x)*0.4f,a.y,a.z+(b.z-a.z)*0.4f}; return true;
}
bool RayDown(float,float,float,float,float& y){y=floorY;return !noFloor;}
"""
tests=r"""
void reset(){
 AutoDuctReset(); clockNow=1000;position={};lastInput={};
 focused=true;solid=noHigh=noFloor=crouched=ductOn=false;floorY=0.4f;pathEnabled=false;clearAbove=false;
}
void acknowledge(){if(autoDuctPath&&s_entry.closing)AutoDuctCancelPath(11,npc,22);}
void pushIntoOpening(){AutoDuctTick();clockNow+=350;AutoDuctTick();}
int main(){
 reset();pushIntoOpening();assert(autoDuctPath&&pathEnabled);
 V3 goal{};assert(AutoDuctTarget(goal)&&goal.z>2.7f&&std::fabs(goal.y-0.4f)<0.001f);
 position.z=0.6f;crouched=true;AutoDuctTick();acknowledge();assert(!autoDuctPath&&!pathEnabled);
 reset();pushIntoOpening();lastInput.rawAny=false;AutoDuctTick();acknowledge();assert(!autoDuctPath&&!pathEnabled);
 reset();pushIntoOpening();lastInput.worldYaw=3.14f;AutoDuctTick();acknowledge();assert(!autoDuctPath);
 reset();pushIntoOpening();clockNow+=2300;AutoDuctTick();acknowledge();assert(!autoDuctPath);
 reset();pushIntoOpening();focused=false;AutoDuctTick();acknowledge();assert(!autoDuctPath);
 reset();solid=true;pushIntoOpening();assert(!autoDuctPath); // solid wall
 reset();noHigh=true;pushIntoOpening();assert(!autoDuctPath); // open room
 reset();noFloor=true;pushIntoOpening();assert(!autoDuctPath); // pit
 reset();floorY=1.2f;pushIntoOpening();assert(!autoDuctPath); // high/vertical opening
 reset();floorY=-1.2f;pushIntoOpening();assert(!autoDuctPath);
 reset();AutoDuctTick();position.z=0.4f;clockNow+=350;AutoDuctTick();acknowledge();assert(!autoDuctPath); // walking normally
 reset();ductOn=true;pushIntoOpening();assert(!autoDuctPath); // already inside
 reset();pushIntoOpening();lastInput.rawAny=false;AutoDuctTick();
 assert(autoDuctPath&&s_entry.closing); // no new direct request while native path is pending
 assert(!AutoDuctCancelPath(11,999,22)&&autoDuctPath);
 acknowledge();assert(!autoDuctPath);
 reset();ductOn=true;crouched=true;clearAbove=true;floorY=-0.4f;pushIntoOpening();
 assert(autoDuctPath&&s_entry.exiting);position.z=0.7f;AutoDuctTick();assert(autoDuctPath);
 crouched=false;AutoDuctTick();acknowledge();assert(!autoDuctPath&&!pathEnabled);
 reset();ductOn=true;crouched=true;clearAbove=true;floorY=-1.2f;pushIntoOpening();assert(!autoDuctPath);
 reset();ductOn=true;crouched=true;clearAbove=true;solid=true;pushIntoOpening();assert(!autoDuctPath);
 reset();pushIntoOpening();AutoDuctReset();assert(!autoDuctPath&&!AutoDuctTarget(goal));
 puts("PASS: low opening intent, no walls/pits/vertical vents, input/direction/focus/timeout cancellation");
}
"""
with tempfile.TemporaryDirectory(prefix="alien-duct-test-") as tmp:
 cpp=Path(tmp)/"test.cpp";exe=Path(tmp)/"test.exe"
 cpp.write_text(prefix+source+tests,encoding="utf-8")
 compiler=Path(os.environ.get("CXX",r"C:\msys64\mingw32\bin\g++.exe"));env=dict(os.environ)
 env["PATH"]=str(compiler.parent)+os.pathsep+env.get("PATH","")
 subprocess.run([str(compiler),"-std=c++17","-msse2","-mfpmath=sse","-static",str(cpp),"-o",str(exe)],env=env,check=True)
 subprocess.run([str(exe)],check=True)
