# sgnames.py <AI.exe> > mp_names.h — таблица «ShortGuid выхода -> имя» для журнала мультиплеера (mp.cpp). Имена — строки
# AI.exe вида on_*, *ed, *ing, *_on/_off плюс известные выходы; ShortGuid — как в игре (sg.py). Имена выходов из скриптов
# уровней (COMMANDS.PAK) сюда не попадают — в журнале они остаются шестнадцатеричными.
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sg import sg
data = open(sys.argv[1], 'rb').read()
names = set()
for m in re.finditer(rb'\x00([a-z][a-z0-9_]{2,40})\x00', data):
    s = m.group(1).decode()
    if s.startswith('on_') or re.fullmatch(r'[a-z_]*(ed|ing|_on|_off)', s): names.add(s)
names.update(['finished_spawning', 'spawned', 'despawned', 'opened', 'closed', 'locked', 'unlocked', 'activated', 'deactivated',
              'enabled', 'disabled', 'started', 'stopped', 'finished', 'shown', 'hidden', 'triggered', 'refreshed', 'on_think',
              'light_switched_on', 'light_switched_off', 'proxy_enable', 'proxy_disable', 'reset', 'on_success', 'on_failure',
              'on_true', 'on_false', 'on_started', 'on_finished', 'on_enter', 'on_exit', 'on_changed', 'on_activated',
              'on_deactivated', 'on_completed', 'on_cancelled', 'on_interrupted', 'on_detected', 'on_lost', 'on_spotted'])
seen = {}
for n in sorted(names):
    seen.setdefault(sg(n), n)
out = sys.stdout
out.reconfigure(encoding='utf-8', newline='\n')
out.write('// mp_names.h — имена выходов сущностей CATHODE по их ShortGuid (журнал мультиплеера). Сгенерировано:\n')
out.write('//   python dev/probe/sgnames.py <AI.exe> > mp_names.h  (строки AI.exe вида on_*, *ed, *ing, *_on/_off; ShortGuid как\n')
out.write('//   в игре: SHA1 имени -> 16-байтный GUID -> hex -> SHA1 -> первые 4 байта, проверено на finished_spawning = DEF8F174)\n')
out.write('#pragma once\n#include <cstdint>\n\nstruct MpOutNameRow { uint32_t guid; const char* name; };\n')
out.write('inline constexpr MpOutNameRow kMpOutNames[] = {\n')
for g in sorted(seen):
    out.write('  { 0x%08Xu, "%s" },\n' % (g, seen[g]))
out.write('};\n')
