# Дампы Ghidra (AI.exe, Steam; VA с базой 0x400000, в игре +0x170000)

Выход скриптов из `dev\ghidra` (headless, проект `%TEMP%\claude\ghidra_ai\AlienSteam`). Новые дампы кладутся сюда же.
Запуск (Git Bash):

```
W='C:\Users\lelil\AppData\Local\Temp\claude\ghidra_ai'   # проект Ghidra
D='C:\Users\lelil\Downloads\AI Playable Alien\dev\ghidra_dumps'
cmd //c "C:\Users\lelil\Downloads\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat $W AlienSteam -process AI.exe \
  -readOnly -noanalysis -scriptPath <папка со скриптами> -postScript Decomp.java 717d90 $D\имя.txt 1"
```

Скрипты: `Decomp` (функции + вызываемые на N уровней), `DecompAt` (функция, содержащая адрес), `Disasm` (листинг
диапазона), `VtSlots` (слоты vtable), `Callers`, `RefList` (все ссылки на адрес), `StrRefs2` (кто ссылается на строку),
`OpScan2` (поиск по операндам: `~` = пробел, `@` = запятая), `FindVt`, `DataRefs`.

## Лицехват: прыжок, смерть жертвы, кости, камера (сессия 25–26.09.2026)

| файл | что |
|---|---|
| `melee.txt` | MeleeStart `0x717d90` (ActionMeleeAttack, слот 13) и всё, что он зовёт; `FUN_0065c000` — прыжок лицехвата |
| `m1.txt` | листинг MeleeStart (аргументы, RET 0x14, выбор по типу атаки) |
| `hvt.txt` | vtable состояния прыжка `0x012adce8`: апдейт (слот 8), старт (слот 24) |
| `m2.txt`, `m29.txt` | старт прыжка `0x66fff0`: ACT_FACEHUGGER_VENT_ATTACK, PARAMETER_A, ветка «цель — не игрок» |
| `m5.txt` | `FUN_0065fb80` — проверка цели прыжка |
| `hl1.txt`, `hl2.txt` | помощники прыжка, пара нападающий/цель `FUN_006d4410` |
| `m8.txt`, `m10.txt` | апдейт прыжка: «прилип» (матрица в `+0x3f0+0xba0`), тайм-аут 5 с |
| `hl3.txt`, `m9.txt` | End_Action `0x65d610`: урон жертве / сдачи лицехвату |
| `hs1.txt` | кто ссылается на ACT_DEATH_FROM_FACEHUGGER и т. п. |
| `hvt3.txt` | состояние «смерть от лицехвата» `0x012ac848` (старт `0x66ff00`) |
| `hc1.txt`..`hc5.txt`, `m3.txt`, `m4.txt` | команда персонажу 28 (`FUN_00778df0`), таблица команд `0x16ebe18` |
| `hl5.txt`..`hl8.txt`, `ho1.txt`..`ho5.txt` | слот команды персонажа `+0x33f0`, тип `+0x3430` |
| `hl9.txt`, `hl14.txt` | кости: `FUN_006b3100` (номер по имени), `FUN_006a2fb0` (мировая матрица), `FUN_006894c0` |
| `m24.txt`..`m28.txt` | листинги функций костей (RET, выход 4x4) |
| `hvt4.txt`, `hs2.txt`, `m21.txt`, `m22.txt` | камера BoneAttached (vtable `0x012F1A18`) — как она берёт кость |
| `hcr4.txt` | `FUN_0042c9f0` — сброс follow-камеры ищет «Head1» у цели (вылет при выгрузке уровня) |
| `hl15.txt` | кто читает матрицу «прилип» `+0xba0` (режим `+0xc04`) |
| `hcr.txt`, `hcr2.txt`, `hcr3.txt`, `hl13.txt` | вылеты: `0x6623b0` (вторичное «aim»), стойка Аманды, `_abort` |
| `m30.txt`, `m31.txt` | CRT: `_purecall` `0xA6BBEA` -> `_abort` |
| `hl10.txt`, `ho6.txt` | состояние `0x39` (поворот на месте) — чем дерево перебивало смерть «от лицехвата» |
| `m11.txt`, `m12.txt` | `LocoGet`: объект локомоции `*(персонаж+0x42a8)` |
| `move_rec_create.txt`, `move2_rec_create.txt` | создание записи узла движения `0x6f0a10` (НЕ обнуляет `+0x34/+0x38`) и соседнего класса `0x40e510` |
| `moveshut_disasm.txt`, `move_shutdown_inner.txt`, `move_shutdown_caller.txt` | Shutdown `0x40f0f0` -> `0x6f8490` -> `0x471ae0`; сброс дерева `FUN_006fdf60` |
| `vt_move.txt`, `vt_012bf1f0.txt`, `vt_c8250.txt` | vtable узлов: MoveInDirection `0x012bf628`, соседний `0x012bf208`, узел с записью `0x6f11e0` |
| `vt_012b4480_refs.txt`, `vt_012b4480_users.txt`, `rec_*.txt` | базовая vtable `0x012b4480` (мусор в записи при вылете андроида), запись `0x012ecbe8` |
| `reqvt_012b19ac.txt`, `reqvts.txt` | класс запроса движения `0x012b19ac` (у андроида тот же тип 9) |

Остальные файлы — дампы прошлых сессий (камера, время/пауза, вентиляция, постобработка, Havok-лучи и т. д.), их разбор —
в `DEVELOPERS.md`.
