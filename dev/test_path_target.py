"""Compile the real PathTarget readiness + spare-record promotion against a fake target chain."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'control.cpp').read_text(encoding='utf-8')
start = source.index('static bool RecordUsable(')
end = source.index('\n}\n', source.index('static bool PromoteSpareRecord(')) + 3
stub = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <cstdio>
using addr=uint32_t;
std::map<addr,addr> memory;
addr rp(addr a){auto it=memory.find(a);return it==memory.end()?0:it->second;}
template<class T> T rd(addr a){return (T)rp(a);}
template<class T> void wr(addr a,T v){memory[a]=(addr)v;}
#define VA(x) (x)
'''
tests = r'''
int main(){
  const addr ch=0x1000, brain=0x2000, rec=0x3000, target=0x4000;
  memory={{ch+0x34a8,brain},{brain+0x10,rec},{rec+0x42c,target},
          {rec+0x430,7},{target+0x50,7},{0x016f0c8c,1}};
  assert(PathTargetReady(ch));
  memory[0x016f0c8c]=0; assert(!PathTargetReady(ch)); memory[0x016f0c8c]=1;
  memory[rec+0x430]=8; assert(!PathTargetReady(ch)); memory[rec+0x430]=7;
  memory[rec+0x42c]=0; assert(!PathTargetReady(ch)); memory[rec+0x42c]=target;
  memory[brain+0x10]=0; assert(!PathTargetReady(ch)); memory[brain+0x10]=rec;
  memory[ch+0x34a8]=0; assert(!PathTargetReady(ch)); memory[ch+0x34a8]=brain;

  // текущий слот пуст, годная запись лежит на +0x14 — promovируем и откатываем
  memory[brain+0x10]=0; memory[brain+0x14]=rec;
  assert(!PathTargetReady(ch));
  assert(PromoteSpareRecord(ch));
  assert(memory[brain+0x10]==rec);
  assert(PathTargetReady(ch));
  RestorePromotion();
  assert(memory[brain+0x10]==0);
  assert(!PathTargetReady(ch));

  // +0x0c важнее +0x14
  const addr rec2=0x3100;
  memory[brain+0x0c]=rec2; memory[rec2+0x42c]=target; memory[rec2+0x430]=7;
  assert(PromoteSpareRecord(ch));
  assert(memory[brain+0x10]==rec2);
  RestorePromotion(); assert(memory[brain+0x10]==0);
  memory[brain+0x0c]=0;

  // негодные записи не promovируются: номер не совпал, цель умерла, флаг ИИ выключен
  memory[rec+0x430]=9; assert(!PromoteSpareRecord(ch)); memory[rec+0x430]=7;
  memory[rec+0x42c]=0; assert(!PromoteSpareRecord(ch)); memory[rec+0x42c]=target;
  memory[0x016f0c8c]=0; assert(!PromoteSpareRecord(ch)); memory[0x016f0c8c]=1;
  assert(memory[brain+0x10]==0);

  // непустой исходный слот возвращается как был
  const addr other=0x3200;
  memory[brain+0x10]=other;
  assert(PromoteSpareRecord(ch));
  assert(memory[brain+0x10]==rec);
  RestorePromotion(); assert(memory[brain+0x10]==other);

  // менеджер сам занял слот во время тика — откат не затирает чужую запись
  memory[brain+0x10]=0;
  assert(PromoteSpareRecord(ch));
  memory[brain+0x10]=other;
  RestorePromotion(); assert(memory[brain+0x10]==other);
  puts("PASS: readiness mirrors 0x4128e0; spare record promoted for the tick and restored, stale slot never clobbered");
}
'''
compiler = Path(os.environ.get('CXX', r'C:\msys64\mingw32\bin\g++.exe'))
env = dict(os.environ, PATH=str(compiler.parent)+os.pathsep+os.environ.get('PATH', ''))
with tempfile.TemporaryDirectory(prefix='alien-path-target-test-') as temp:
    cpp, exe = Path(temp)/'test.cpp', Path(temp)/'test.exe'
    cpp.write_text(stub+source[start:end]+tests, encoding='utf-8')
    subprocess.run([str(compiler), '-std=c++17', '-O2', '-static', str(cpp), '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], check=True)