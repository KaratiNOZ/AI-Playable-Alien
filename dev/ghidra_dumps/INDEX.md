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

## Нативная подсказка взаимодействия (сессия 16.11.2026)

Проект: `build-ghidra\CrawlState.gpr` (AI.exe, md5 bb0d932f… — тот же, что в игре). Проект `%TEMP%\claude\ghidra_ai\AlienSteam`
опустел (программа удалена). В базе CrawlState нет operand-ссылок — `StrRefs2`/`Callers` молчат; искать через `RefScan`
(побайтовый поиск LE-адреса в коде) или `DecompForce` (создаёт функцию, если её нет). Новые скрипты: `StrSub` (строки по
подстроке), `DecompForce`, `RefScan`.

| файл | что |
|---|---|
| `prompt_strsub.txt` | AS-имена с «Interaction_Prompt»: только `Set_Interaction_Prompt_Visible` (012fb300) и `Set_Interaction_Prompt_Invisible_no_anim` (012fb478). Сеттера текста/клавиши/прогресса среди AS-вызовов НЕТ |
| `prompt_strings.txt` | все строки «Prompt/prompt/hold_time»: `prompt_text` (013de2c4), `push_hold_time` (013de3dc), `SetControllerPrompt` (012ff1b4), `set_scripted_mash_prompt_percentage`, `set_breathing_game_prompt_text/progress` |
| `prompt_invokers.txt` | обёртки AS-вызова — thiscall НА ОВЕРЛЕЙ: `008081e0`(o,name) 0 арг; `00534240`(o,name,float); `005341a0`(o,name,int); `0082a730`(o,name,str); `0082b010`(o,name,str,str,float). Все берут CS менеджера +0x40174a78c и закрепляют Movie |
| `prompt_refscan.txt` | `prompt_text`/`push_hold_time` регистрируются в GUID через `FUN_008bf7f0(out*,str)` (инициализаторы `01163020`/`011631d0`); прямых потребителей GUID в коде нет (доступ через структуру). Visible зовут `FUN_0083e32e`/`0084bef8` (куска апдейта HUD `0084bbe0`) — только видимость, не текст |
| `prompt_reg.txt` | `FUN_008bf7f0` = строка→GUID (CATHODE ShortGuid), результат пишется по месту строки в .data |

AS3-сторона (`dev/ui_extract/PICKUPOVERLAY.GFX` из `DATA\UI.PAK`, парсер `abc_strings.py`): корень `PickupOverlay`,
члены `staticPrompt_mc` (mcStaticPromptAnim_6), `spinner_mc`/`mc_prompt_spinner` (mcSpinnerProgressMeter_19), `legend_mc`,
`text_mc`, `glyph_txt`, `continueGlyph`; методы `SetText`, `SetControllerPrompt` (клавиша), `Set_Interaction_Prompt_Visible/Invisible`.
Текст подсказки нативный контроллер ставит GFx-членом `prompt_text`, прогресс удержания — `push_hold_time` (не AS-вызов).
Вывод: готового AS-сеттера текста нет; для нативной подсказки мода нужно либо звать GFx SetMember на клипе
(`staticPrompt_mc`/`glyph_txt` + `push_hold_time`), либо оставить мод-HUD. Решение — за пользователем.
