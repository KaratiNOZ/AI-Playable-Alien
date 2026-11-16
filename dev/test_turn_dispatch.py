"""Exercise the actual locomotion ownership callback with multiple NPC entries."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'control.cpp').read_text(encoding='utf-8')
start = source.index('static void OnTurnLocoEnter(')
end = source.index('// поток игры: выход собственного', start)
stub = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <cstdio>
using addr=uint32_t;
addr loco=0x5000;
std::map<addr,addr> memory;
addr rp(addr a){return memory.at(a);}
struct Invocation {addr ecx=0;uint32_t arg[6]{};char text[64]{};};
'''
tests = r'''
int main(){
  memory={{0x1000,0x2000},{0x1004,0x2018},
          {0x2000,0x4000},{0x2008,0x5000},{0x2010,0x6000}};
  Invocation inv; inv.ecx=0x1000;
  for(unsigned i=0;i<3;++i){inv.arg[0]=i;OnTurnLocoEnter(inv);assert(bool(inv.text[0])==(i==1));}
  inv.arg[0]=3; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  inv.arg[0]=0xffffffff; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  inv.arg[0]=1; loco=0; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  loco=0x5000; inv.ecx=0; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  inv.ecx=0x1000; memory[0x1004]=0x1ff0; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  memory[0x1000]=0; OnTurnLocoEnter(inv); assert(!inv.text[0]);
  puts("PASS: only owned locomotion drives turn transitions; other NPCs, stale indices and empty ranges excluded");
}
'''
compiler = Path(os.environ.get('CXX', r'C:\msys64\mingw32\bin\g++.exe'))
env = dict(os.environ, PATH=str(compiler.parent)+os.pathsep+os.environ.get('PATH', ''))
with tempfile.TemporaryDirectory(prefix='alien-turn-test-') as temp:
    cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test.exe'
    cpp.write_text(stub+source[start:end]+tests, encoding='utf-8')
    subprocess.run([str(compiler), '-std=c++17', '-O2', '-static', str(cpp), '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)
