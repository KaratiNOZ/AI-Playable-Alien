"""Compile the actual floor-crawl code against a fake native posture API."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / "control.cpp").read_text(encoding="utf-8")
start = source.index("struct FloorCrawlState")
end = source.index("static void OnFaceLeave", start)
prefix = r"""
#include <atomic>
#include <cstdint>
#include <map>
#include <cassert>
#include <cstdio>
using addr=uint32_t;
using CharacterPostureFn=void(*)(addr);
namespace va { constexpr addr CrouchEnter=1,CrouchExit=2; }
std::map<addr,uint32_t> mem;
addr npc=0x1000;
bool moving=false,alienBackstage=false,ductOn=false,busy=false,blocked=false;
bool forcedRetreat=false;
bool ForcedRetreatActive(){return forcedRetreat;}
struct { bool stay=false; } vent;
std::atomic<bool> ventCrawl{false},testMoveForward{false},s_snapHold{false};
std::atomic<uint64_t> s_snapIdleUntil{0};
int enters=0,exits=0,writes=0;
bool VentBusy(){return busy;}
template<class F> bool Safe(F f){try{f();return true;}catch(...){return false;}}
template<class T> T rd(addr a){return T(mem.at(a));}
void Log(const char*,...){}
void enter(addr a){if(blocked)return; ++enters; ++writes; mem[a+0x2af]=1;}
void exitPosture(addr a){if(blocked)return; ++exits; ++writes; mem[a+0x2af]=0;}
template<class T> T Fn(addr a){return reinterpret_cast<T>(a==va::CrouchEnter?&enter:&exitPosture);}
"""
tests = r"""
void reset(){
  npc=0x1000; mem.clear(); mem[npc+0x2af]=0;
  mem[0x2008]=7; mem[0x200c]=7; mem[0x2050]=0x4005c960; mem[0x301a]=56; mem[0x3008]=0x27;
  busy=ductOn=alienBackstage=blocked=forcedRetreat=false; vent.stay=false;
  ForgetVentCrawl(); enters=exits=writes=0;
}
int main(){
  reset(); auto original=mem;
  ActVentCrawl(true); ApplyVentCrawl(); assert(enters==1&&VentCrawlPending());
  for(int i=0;i<20;++i){moving=(i%2)==0; ApplyVentCrawl();}
  assert(enters==1); // Input never changes modes, action, gait or speed.
  ActVentCrawl(false); ApplyVentCrawl();
  assert(exits==1&&!VentCrawlPending()&&mem==original);
  reset(); ActVentCrawl(true); ActVentCrawl(false); ApplyVentCrawl(); assert(writes==0);
  reset(); blocked=true; ActVentCrawl(true); ApplyVentCrawl(); assert(!VentCrawlPending());
  blocked=false; ApplyVentCrawl(); assert(enters==1&&VentCrawlPending());
  blocked=true; ActVentCrawl(false); ApplyVentCrawl(); assert(VentCrawlPending());
  blocked=false; ApplyVentCrawl(); assert(!VentCrawlPending()&&exits==1);
  reset(); mem[npc+0x2af]=1; ActVentCrawl(true); ApplyVentCrawl();
  ActVentCrawl(false); ApplyVentCrawl(); assert(writes==0&&mem[npc+0x2af]==1);
  reset(); busy=true; ActVentCrawl(true); ApplyVentCrawl(); assert(writes==0);
  busy=false; ductOn=true; ApplyVentCrawl(); assert(writes==0);
  reset(); forcedRetreat=true; ActVentCrawl(true); ApplyVentCrawl(); assert(!ventCrawl&&writes==0);
  reset(); ActVentCrawl(true); ApplyVentCrawl(); int before=writes;
  npc=0; ApplyVentCrawl(); assert(writes==before&&!VentCrawlPending());
  reset(); ActVentCrawl(true); ApplyVentCrawl(); testMoveForward=true;
  before=writes; ForgetVentCrawl(); assert(writes==before&&!ventCrawl&&!testMoveForward&&!VentCrawlPending());
  puts("PASS: native posture lifecycle, retries, natural duct ownership, unchanged actions/modes/speed");
}
"""
with tempfile.TemporaryDirectory(prefix="alien-crawl-test-") as temp:
    cpp=Path(temp)/"test.cpp"
    exe=Path(temp)/"test.exe"
    cpp.write_text(prefix+source[start:end]+tests,encoding="utf-8")
    compiler=Path(os.environ.get("CXX",r"C:\msys64\mingw32\bin\g++.exe"))
    env=dict(os.environ)
    env["PATH"]=str(compiler.parent)+os.pathsep+env.get("PATH","")
    subprocess.run([str(compiler),"-std=c++17","-static",str(cpp),"-o",str(exe)],env=env,check=True)
    subprocess.run([str(exe)],check=True)
