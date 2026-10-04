// game.h — адреса AI.exe (Alien: Isolation, Steam retail x86) как VA из Ghidra и сигнатуры функций игры.
// Всё взято из scripts/alien.js, scripts/hot.js и docs/HOOKING.md.
#pragma once
#include "common.h"

// сборка игры, под которую все адреса: Steam AI.exe от 17.02.2015 (PE: штамп времени, SizeOfImage)
constexpr unsigned long kGameStamp = 0x54E3199Dul, kGameImageSize = 0x01F81000ul;
namespace va {
// ---- дерево поведения (LegendPlugin.Nodes.*) ----
constexpr addr FactoryMenuLink = 0x00715700;  // фабрика ConditionDebugMenuLinkTest(xml) -> узел; узел+0x14 = DefaultBehaviour (рубильник)
constexpr addr MoveParams      = 0x0040e340;  // парсер ActionMoveInDirection (ecx = узел): +8 speed, +0xC dir, +0x10 distance
constexpr addr MoveStart       = 0x0040eb00;  // Start(ecx = узел, [esp+4] = npc)
constexpr addr MoveShutdown    = 0x0040f0f0;
// запись узла ActionMoveInDirection (слот +0x48 vtable 0x012bf628): берётся из общего пула (0x16b1c24) и НЕ обнуляет
// +0x34/+0x38 (умный указатель на запрос движения; соседний класс 0x40e510 обнуляет). Сброс дерева до Start зовёт
// Shutdown, тот читает мусор прежнего владельца записи -> вылет 0x471aff/0x471afb. Мод обнуляет их на выходе
constexpr addr MoveRecCreate   = 0x006f0a10;
constexpr addr SmartPtrCopy    = 0x004460a0;  // во время Start: args[0] с +0x10 == 9 — запрос движения
constexpr addr LocoGet         = 0x00402080;  // (npc) -> pair; *pair = объект локомоции L (берём только из 0x6813a0..0x681590)
constexpr addr LocoGetFrom = 0x006813a0, LocoGetTo = 0x00681590;
constexpr addr LocoReqStart    = 0x006815a0;  // слот 2 запроса локомоции
constexpr addr LocoReqUpdate   = 0x006813a0;  // слот 3
constexpr addr LocoUpdate      = 0x0044d4f0;  // апдейт локомоции — поток игры, каждый кадр
constexpr addr AnimInputUpdate = 0x00660dc0;  // вход анимации — поток игры, каждый кадр
// разворот на месте (основное действие 57, AT_NPCLocomotionStandingTurn): так его начинает сама игра из состояния «стоит»
// (FUN_00451040): объект ходьбы L: +8 = 4, +0x10/+0x14/+0x18/+0x20 = 0, +0x24 = [TurnConstA], +0x28 = [TurnConstB],
// +0x1c = курс цели; потом thiscall(*(персонаж+0x36c), 0, 1) — текущее действие пересобирается на месте в разворот
constexpr addr PrimToStandingTurn = 0x00657860, TurnConstA = 0x011c666c, TurnConstB = 0x011bbcfc;
// «стать стоящим»: FUN_0065bdd0 (его thunk 0x0065bec0), fastcall(ECX = текущее основное действие), без аргументов —
// действие пересобирается на месте в AT_IDLE (у Чужого — FUN_00401520(0)); так игра заканчивает свои переходы
constexpr addr PrimToIdle = 0x0065bec0;
// событие действия-разворота (слот 2 vtable 0x012ac040): thiscall(действие, событие*, ?, ?), RET 0xc; событие +8 — id.
// На «turnFinished»/«urgentTurnFinished» помечает дерево анимации законченным (FUN_00694fa0) и действие +8 |= 6.
// id имён: FUN_00402cf0 / FUN_00402f40 — cdecl(const char*) -> id
constexpr addr TurnActEvent = 0x0066a200, EventIdA = 0x00402cf0, EventIdB = 0x00402f40;
// цель взгляда персонажа (её каждый кадр забирает объект взгляда AUTONOMIC_LOOK_AT, FUN_0064cad0): +0x29c0 — точка V3,
// +0x29cc — байт «есть цель», +0x29cd/+0x29ce — флаги (биты 2/4 объекта), +0x29d0/+0x29d4 — байты в объект +0x40/+0x41
constexpr addr CharLookTarget = 0x29c0, CharLookValid = 0x29cc;
// объект взгляда забирает цель у персонажа (слот 3 его vtable, fastcall(ECX = объект)); vtable объекта AUTONOMIC_LOOK_AT
constexpr addr LookSyncFromChar = 0x0064cad0, VT_LOOK_AT = 0x012af4a0;
// слот 10 того же объекта — thiscall(объект, фаза): в фазе 3 у NPC с деревом считает свой угол (FUN_006a4710) и
// КОПИРУЕТ +0x178 в look_phi +0xb4 — поверх того, что поставил мод (поэтому голова доходила медленно и дрожала у цели)
constexpr addr LookPhase = 0x0064cc30;
// CATHODE PositionDistance (FUN_005b9d10 — слот 3 vtable 0x01271c60): матрица LHS (FUN_008e8e50), потом RHS
// (FUN_008e8dc0(буфер, контекст), RET 8; возврат сюда — 0x005b9d57), длина разности переносов (+0xc, +0x1c, +0x2c)
constexpr addr PosDistRhs = 0x008e8dc0, PosDistRhsRet = 0x005b9d57;
constexpr addr TreeNodeDestroy = 0x006d01b0;
constexpr addr ConditionEval   = 0x00411d80;  // слот 16 vtable всех узлов-условий: thiscall(узел, персонаж, контекст)
constexpr addr VT_GATE = 0x012e4870, VT_MOVE = 0x012bf628;
// узлы-«дети» рубильников мода (vtable, проверено по живым деревьям) — по ним узнаём рубильники в памяти (treescan.cpp)
constexpr addr VT_LOCK_VENTS = 0x012c18b0, VT_MELEE = 0x012cb770, VT_NEXT_TARGET = 0x012c44b0, VT_MOVE_SENSED = 0x012b84d0,
               VT_IDLE = 0x012c2c10;
// проверка рубильника ConditionDebugMenuLinkTest (слот 248 его vtable): thiscall(узел, персонаж, контекст) -> bool,
// тело — «cmp byte [ecx+0x14], 0; setnz al; ret 8». Узлы дерева общие для всех персонажей одного типа.
constexpr addr GateTest = 0x006ef6f0;

// родная пауза игры (меню Esc и т. п.): биты — пока меню паузы открыто, они отличаются от обычных (control.cpp, EscTick)
constexpr addr PauseBits = 0x016f194c;

// ---- смена уровня ----
constexpr addr QueueLevel = 0x0077b320, RequestNextLevel = 0x0079a650, MenuLoadLevel = 0x008139c0,
               LoadLevel1 = 0x007bcd40, LoadLevel2 = 0x007a86c0;

// ---- глобалы ----
constexpr addr GameGlobals = 0x016f0c88;  // +8 игроки (+0x80 массив, +0x84 число), +0x24 навигация, +0x58 менеджер камер
constexpr addr ActiveChars = 0x01707a48;  // шаг 0xB0, 18 слотов: [+0] entity, [+0xC] контроллер (+0x10 = Havok rigid body)

// ---- камера ----
constexpr addr CamSetEntity = 0x00429220, CamActivate = 0x00432e10, FollowUpdate = 0x0042e970;
// ---- столкновения камеры ----
constexpr addr NavCtxOf = 0x004458e0, NavFilter = 0x00933190, NavRay = 0x0044b780, HkRay = 0x0068dab0, HkWorldUse = 0x00b48890;
// ---- звук: комнатная система слушателя ----
constexpr addr SoundListener = 0x00689230, SoundListenerRet = 0x006be324;
// ---- вентиляция ----
// менеджер вентиляций: 0x4132f0 — вентиляция для ухода (mgr+0x40+reason*4), 0x413310 — для выхода (mgr+0x48+reason*4,
// единственный вызов — старт узла выхода FUN_0040fb10: ведёт Чужого к позиции объекта вентиляции entry+0x28)
constexpr addr VentPickAlien = 0x004132f0, VentPickExit = 0x00413310, BackstageShutdown = 0x0040fbc0;
// ---- режим пути (ActionMoveToMostRecentSensedPosition) ----
constexpr addr PathTick = 0x00734a40, PathTickEnd = 0x00734c40, PathTarget = 0x004128e0, PathMoveTo = 0x0070a940;
// ---- Аманда: позиция физического тела (вызов из 0x8030xx..0x8034xx) ----
constexpr addr CharPhysPos = 0x00b85150, CharPhysFrom = 0x00803000, CharPhysTo = 0x00803400;
// ---- CATHODE / спавн ----
constexpr addr CallTriggers = 0x008bd920, ShortGuid = 0x008bf7f0;
// ---- мультиплеер: постоянная ссылка на сущность EntityHandle = {GUID сущности, ID экземпляра составной} ----
// FUN_008c3360 thiscall(объект сущности, uint32_t[2] выход) — ручка сущности (ID экземпляра — свёртка GUID родителей);
// FUN_009236a0 thiscall(EntityManager, MemoryPtr<Entity>* выход, const uint32_t[2] ручка) — find_entity;
// EntityManager = *(0x0174ef40). Объект сущности у ручки MemoryPtr: *(ручка+0xc); +4 — счётчик ссылок.
constexpr addr GetEntityHandle = 0x008c3360, FindEntity = 0x009236a0, EntityManagerPtr = 0x0174ef40;
// this у CallTriggers — «интерфейс» сущности: первый подобъект после +0, у чьей vtable слот +0x394 — диспетчер связей
// FUN_008be0d0 (у разных классов на +0x14 или +0x1c — смещение зависит от раскладки класса); у экземпляров составных
// (CompositeInstance, vt 0x013E196C) там FUN_00965920 — сначала свои внутренние связи, потом тот же диспетчер
constexpr addr LinkDispatch = 0x008be0d0, LinkDispatchComposite = 0x00965920;
constexpr addr FindParamString = 0x005991a0, FindParamBool = 0x00630590, FindParamVector = 0x008d63c0;
constexpr addr SpawnImpl = 0x004905c0, VT_ENT = 0x011d08d8, VT_ENT_IFACE = 0x011d0540;
// ---- часы игры (FUN_00773050): игровая дельта = реальная × масштаб, если флаг включён ----
constexpr addr TimeScaleOn = 0x016410d8;  // bool: использовать TimeScale
constexpr addr TimeScale   = 0x016410c8;  // double: масштаб времени (1 — норма, ~0 — стоп)
constexpr addr RealDelta   = 0x016410d0;  // double: реальная дельта кадра
// FUN_0099db50 — stdcall(float время, float дельта): игровые время/дельта для рендера — глобалы 0x01758148/0x0175814c
// и константа шейдеров (слот 0x15). Шейдеры (motion blur) делят на эту дельту: при дельте ~0 весь кадр размыт.
constexpr addr ShaderClock = 0x0099db50, GameDeltaF = 0x0175814c;
// FUN_0077d440 — cdecl(float lo, float hi): игровая дельта, зажатая в [lo, hi]. Так её берут анимация персонажей
// (lo = 1/360, hi = 1/24) и ещё ~18 мест: при остановленном времени lo держит анимацию живой (~1/6 скорости).
constexpr addr DeltaClamp = 0x0077d440;
// физика Havok идёт в реальном времени: задача кадра FUN_007f7550 (cdecl, без аргументов) зовёт шаг мира
// FUN_00b42850(this, очередь, float dt = 1/FPS). На своей паузе игра задачу просто не вызывает (хвост Чужого — физика).
constexpr addr PhysicsTask = 0x007f7550, PhysicsStep = 0x00b42850;
// ---- постобработка: менеджер 0x019d0970 копит пакеты CATHODE-сущностей (ColourSettings, VignetteSettings, ...),
// FUN_00a08c50 (ecx = менеджер) каждый кадр смешивает их в итог, который читает рендер ----
constexpr addr PostBlend = 0x00a08c50;
// ---- фоторежим: точки, где камера уже посчитана игрой (look-at без крена) ----
constexpr addr CamVelocity = 0x00429ad0;  // thiscall(камера, dt): скорость камеры (блок 1 − блок 2) для motion blur
constexpr addr CamSubmit   = 0x00432300;  // fastcall(менеджер камер): блок 1 текущей камеры -> рендер
// комната (зона) для отрисовки: FUN_00432300 берёт её у персонажа, за которым следит камера (камера+0x148 -> +0x1b0),
// и если у камеры +0x155/+0x156 — пишет [0x01758140] = комната, [0x01758144] = 1. FUN_009b45a0 отдаёт её активной
// точке зрения рендера (POV +0x330, бит 0x80 у +0x36c — «зона задана»); без флага зону POV ищет сама игра по позиции
// камеры (FUN_0074c870, pov_zone_matching.cpp — запрос в физику)
constexpr addr RenderRoom = 0x01758140, RenderRoomForced = 0x01758144;
// [0x01758145] = камера+0x150 — «зону по позиции не искать» (у камеры следования 1): FUN_009b45a0 кладёт в бит 0 у POV
// +0x36d, и без заданной комнаты FUN_0074c870 ставит зону −1 — не рисуется ничего
constexpr addr RenderNoZoneMatch = 0x01758145;
// менеджер точек зрения рендера: *(0x01766a00): +0x88 число (байт), +8 массив, +0x8c активная; POV +0x330 — зона
constexpr addr PovManager = 0x01766a00;
// палитра скиннинга: FUN_0068e9a0 thiscall(модель рендера, hkQsTransform* поза модели, int число) — для каждой кости
// матрица позы × мировая × обратная привязка. Персонаж (FUN_006a5040) передаёт позу СЛОЯ 7: FUN_00bba260(*(анимация+0x24c))
// -> {массив, число} = поза+0x10/+0x14 (синхронизированная поза модели). Слой 0 (+0x230) на картинку не влияет.
constexpr addr SkinPalette = 0x0068e9a0, AnimFinalPoseOff = 0x24c;
// комнатная система звука: FUN_006be2f0 fastcall(менеджер): позиция слушателя — если менеджер+0x68 и игрок есть — кость 0
// персонажа (FUN_00689230, вызов 0x006be31f, мод подставляет Чужого), иначе объект менеджер+0xc -> FUN_00853d70()+0x10
constexpr addr SoundRooms = 0x006be2f0;
// ---- голос Чужого: узел ActionTriggerSound (ATOMIC_TRIGGER_SOUND) зовёт FUN_00784230(персонаж + 0x11a0, тип):
// тип -> событие вокализации -> очередь звуков персонажа (проверено: [npc+0x11a0] = сам Чужой, тип персонажа = 1) ----
constexpr addr AlienVocal = 0x00784230;
// ---- подмодели персонажа (как CATHODE CHR_SetSubModelVisibility, FUN_00494160): anim = персонаж+0x3f0 ----
// anim+0x3cd == 0: одна модель — ModelInst(anim); иначе anim+0x3d0 штук — ModelInstAt(anim, i). Модель: +200 -> +0x54
// число подмоделей, +0x10c массив; имя подмодели *( *( *(sub+0x38) ) + 8 ); SubFlags(sub) -> флаги, байт +7: 0 — скрыта
constexpr addr ModelInst       = 0x00698b00;  // thiscall(anim) -> модель
constexpr addr ModelInstAt     = 0x00698bd0;  // thiscall(anim, i) -> модель
constexpr addr SubFlags        = 0x009b01a0;  // fastcall(sub) -> флаги отрисовки
// ---- речь НПС: «динамический диалог» (как CATHODE NPC_DynamicDialogue, FUN_004d79a0) ----
constexpr addr SoundMgrPtr     = 0x016b1758;  // *(...) — менеджер звука; +0xc4 — таблица имён событий/аргументов
constexpr addr SoundDb         = 0x006b58b0;  // thiscall(менеджер) -> база 0xb (очередь речи)
constexpr addr DlgEventFind    = 0x008572a0;  // thiscall(имена, "событие", &запись) -> AL
constexpr addr DlgArgName      = 0x00857340;  // thiscall(имена, "значение", pair[2] = {имя, хэш}) -> AL
constexpr addr DlgPathInit     = 0x0049fbc0;  // thiscall(путь 0xc4)
constexpr addr DlgPathTemplate = 0x00775920;  // thiscall(персонаж+0x11a0, out 0xd0) -> out (шаблон пути персонажа)
constexpr addr DlgPathCopy     = 0x0049f930;  // thiscall(путь, шаблон)
constexpr addr DlgPathAdd      = 0x0049fa20;  // thiscall(путь, имя, хэш) — следующий аргумент (хэш 0 — «любое»)
constexpr addr VoiceActorName  = 0x008b7880;  // cdecl(*(персонаж+0x11a8)) -> "AN1"/"AN2"/"AN3"/"ANH"...
constexpr addr SpeechQueue     = 0x006bec90;  // thiscall(база, приоритет 1..6, запись, путь, *(перс+0x1320), перс, float ждать, cb, ctx) -> AL
constexpr addr AndroidChooseAttack = 0x006d63d0;
constexpr addr WwResolveDialogue = 0x00c2d2c0;  // AK DynamicDialogue::ResolveDialogueEvent(событие, значения[], число, seq) -> узел
constexpr addr WwDynSeqOpen      = 0x00c2bbe0;  // AK DynamicSequence::Open(объект, флаги, cb, cookie) -> playing id
constexpr addr WwPlayDialogue    = 0x00856580;
constexpr addr SpeechTryPlay     = 0x006bdba0;  // очередь речи: попробовать проиграть элемент сейчас -> AL
constexpr addr SpeechObjState    = 0x006b7750;  // очередь речи: состояние элементов этого звукового объекта (4 — занят)  // игра: проиграть событие с путём аргументов (очередь речи)  // слот +0x24 выбора атак (vt 0x012cc390): thiscall(выбор, цель, тип, маска) -> номер
// ---- смерть: FUN_007d0c10 thiscall(персонаж, нападающий) — урон на всё здоровье (+0x33c, компонент здоровья +0x320,
// FUN_007caac0), если у персонажа флаг «может умереть» (+0x64 -> +0x1f). Так игра добивает жертву («Start_Death»,
// FUN_00652e90) — и так мод убивает андроида за Чужого ----
constexpr addr CharKill = 0x007d0c10;
// старт ActionMeleeAttack (слот 13): thiscall(узел, персонаж, состояние); старт «половины жертвы» в ближнем бою
// FUN_00675560 (ecx = акт жертвы: +4 жертва, +8 флаги — 0x46 = отказ, +0xd0 нападающий) — для диагностики
constexpr addr MeleeStart = 0x00717d90, VictimActStart = 0x00675560;
// ---- CRT игры (VS2010): _purecall 0xA6BBEA (вызов «чистого виртуального» метода — объект уже разрушен) -> abort
// 0xA785F9 (Windows: код 0x40000015 по смещению 0x678621); terminate 0xA7837E (исключение C++ без обработчика) ----
constexpr addr CrtPurecall = 0x00a6bbea, CrtTerminate = 0x00a7837e;
// ---- прыжок лицехвата: MeleeStart с типом VENT у персонажа типа 5 зовёт FUN_0065c000 thiscall(персонаж+0x36c, цель) —
// состояние персонажа (объект на месте, vtable 0x012adce8, id +0x1a = 0x3e), анимация ACT_FACEHUGGER_VENT_ATTACK.
// Старт — слот 24 (0x0066fff0): FUN_0065fb80 thiscall(состояние, &ручка цели, индекс) RET 8 -> al (провал: +8 |= 0x46),
// в нём проверка пары FUN_006d4410 thiscall(*(G+0x5c), нападающий, цель) -> al. Апдейт — слот 8 (0x0065d340): время
// double 0x016410a8 минус float +0x78 (старт); через +0x8c (0.5 с) +0x88 = 1 — лицехват играет прыжок в системе координат
// цели («прилип»), через 5 с у цели-не-игрока — конец. Конец анимации («End_Action», FUN_0065d610, fastcall): людям
// (класс 3/4/6/9) урон на всё здоровье, цели-не-игроку — сдачи лицехвату и конец. +0x44 цель (указатель), +0x84 бит 0 —
// End_Action уже был. Слот 0 (0x006600a0) — разрушение состояния ----
constexpr addr HugLeapSet = 0x0065c000, VT_HUG_LEAP = 0x012adce8, HugLeapCheck = 0x0065fb80, HugPairOk = 0x006d4410,
               HugLeapUpdate = 0x0065d340, HugLeapEnd = 0x0065d610, HugLeapDtor = 0x006600a0, GameTimeD = 0x016410a8;
// ---- смерть «от лицехвата»: команда персонажа 28 — FUN_00778df0 thiscall(&нападающий, жертва): состояние жертвы
// (id 0x3f, vtable 0x012ac848, старт FUN_0066ff00: удар на всё здоровье + ACT_DEATH_FROM_FACEHUGGER —
// HUMAN\DEATH_FROM_FACEHUGGER_STANDING / _CROUCHED). Объект локомоции персонажа — *(персонаж+0x42a8) ----
constexpr addr FhDeathCmd = 0x00778df0, FhDeathStart = 0x0066ff00, CharLocoOff = 0x42a8;
// ---- цели персонажа: *(персонаж+0x34a8) — +0x10 текущая запись цели (её +0x42c цель, +0x430 номер), +0x0c/+0x14 —
// другие записи (vtable 0x012EE8B8). У лицехвата, который ещё никого не «заметил», +0x10 пусто (запись про Аманду лежит
// в +0x14) — старт атаки тогда сразу отказывает ----
constexpr addr VT_TARGET_ENTRY = 0x012ee8b8;
// ---- персонаж по ручке (GUID сущности +0x54/+0x58): FUN_006544e0 / FUN_006545c0 thiscall(реестр *0x0174ef40, &ручка).
// Копии из одной сущности (спавн мода) делят GUID — находится первая. Прыжок хранит цель ручкой: мод подменяет ответ на
// выбранную жертву в коде прыжка (старт 0x66fff0..0x6701db, проверка 0x65fb80..0x65fc7d, апдейт/«прилип»/End_Action
// 0x65d2e0..0x65d70d) ----
constexpr addr CharByHandleA = 0x006544e0, CharByHandleB = 0x006545c0;
// ---- параметры анимации действия: FUN_0068cae0 thiscall(аним+0x50, &параметры, n); запись параметра {&значение, ...,
// имя, ...}. Смерть «от лицехвата» (старт 0x66ff00) ставит VICTIM_STATE (0/1 — заглушки игрока «_ph», 2/3 — настоящие
// HUMAN\DEATH_FROM_FACEHUGGER_STANDING/_CROUCHED?) — вызов по адресу 0x66ffba, возврат 0x66ffbf ----
constexpr addr AnimSetParams = 0x0068cae0, FhDeathParamsRet = 0x0066ffbf;
// ---- кости: номер по имени FUN_006b3100 thiscall(персонаж+0x3f0, "Head", 7) RET 8 -> int (-1 — нет); мировая матрица
// 4x4 FUN_006a2fb0 thiscall(персонаж+0x3f0, номер, 7, float out[16] (выровнять на 16)) RET 0xc -> al (так камера
// BoneAttached берёт «Head1»); +0xb5c — скелет готов. «Прилип» лицехвата: FUN_006894c0 thiscall(персонаж+0x3f0, матрица)
// копирует её в +0xba0; из апдейта прыжка зовётся по 0x65d5f5 (возврат 0x65d5fa) ----
constexpr addr BoneIndex = 0x006b3100, BoneMatrix = 0x006a2fb0, AttachFrameSet = 0x006894c0, LeapFrameRet = 0x0065d5fa;
// ---- вторичные действия персонажа (слои анимации): контроллер npc+0x370, 6 слотов; +0x30+i*4 — желаемое состояние,
// +0x48+i*4 — колбэк выбора (если есть — он решает). Состояние 0x13 = SecondaryActLookAround («осмотреться»),
// игра сама ставит его в слоте 2 (FUN_0078f5c0), когда персонаж стоит и включён enable_lookaround ----
constexpr addr SecActOff = 0x370;
// ---- интерфейс игры (Scaleform): менеджер оверлеев *(0x0174a78c): +0x38 массив, +0x3c число, оверлей +0x34 имя,
// +0x68 корень ролика. FUN_008081e0(оверлей, "функция") — вызвать AS-функцию ролика (сам берёт замок менеджера).
// FUN_0083a590(менеджер, dt) — покадровый апдейт всех оверлеев (поток игры) ----
constexpr addr OverlayMgrPtr = 0x0174a78c, OverlayInvoke = 0x008081e0, OverlayUpdate = 0x0083a590;
}  // namespace va
typedef void(__thiscall* AlienVocalFn)(addr soundOwner, int type);
typedef void(__thiscall* PrimToStandingTurnFn)(addr action, int a, int b);
typedef void(__fastcall* PrimToIdleFn)(addr action);
typedef uint32_t(__cdecl* EventIdFn)(const char* name);
typedef addr(__thiscall* ModelInstFn)(addr anim);
typedef addr(__thiscall* ModelInstAtFn)(addr anim, unsigned i);
typedef addr(__fastcall* SubFlagsFn)(addr sub);
typedef uint32_t(__thiscall* DlgEventFindFn)(addr names, const char* name, addr* out);
typedef uint32_t(__thiscall* DlgArgNameFn)(addr names, const char* name, uint32_t* pair);
typedef void(__thiscall* DlgPathInitFn)(void* path);
typedef void*(__thiscall* DlgPathTemplateFn)(addr src, void* out);
typedef void*(__thiscall* DlgPathCopyFn)(void* path, void* src);
typedef void(__thiscall* DlgPathAddFn)(void* path, const char* name, uint32_t hash);
typedef const char*(__cdecl* VoiceActorNameFn)(int actor);
typedef addr(__thiscall* SoundDbFn)(addr mgr);
typedef uint32_t(__thiscall* SpeechQueueFn)(addr db, int prio, addr ev, void* path, addr soundObj, addr ch, float wait, addr cb,
                                            addr ctx);
typedef void(__thiscall* CharKillFn)(addr victim, addr attacker);
typedef void(__thiscall* FhDeathCmdFn)(addr attackerRef, addr victim);
typedef int(__thiscall* BoneIndexFn)(addr anim, const char* name, int layer);
typedef uint8_t(__thiscall* BoneMatrixFn)(addr anim, int bone, int layer, float* out16);
typedef void(__thiscall* OverlayInvokeFn)(addr overlay, const char* fn);

typedef void(__thiscall* CamSetEntityFn)(addr cam, addr entity);
typedef unsigned(__thiscall* CamActivateFn)(addr mgr, addr cam);
typedef addr(__thiscall* NavCtxOfFn)(addr self);
typedef int(__cdecl* NavFilterFn)(addr);
typedef int(__cdecl* NavRayFn)(addr nav, addr from, addr to, addr hit, int filter, int zero);
typedef void(__thiscall* HkRayFn)(addr self, addr hit, addr world, addr from4, addr to4, addr pos4, addr nrm4);
typedef int(__thiscall* ShortGuidFn)(addr out, const char* name);
typedef void(__thiscall* CallTriggersFn)(addr entityInterface, addr entRef, addr guidRef, addr triggerInfoRef, double t);
typedef void(__thiscall* GetEntityHandleFn)(addr entity, uint32_t* out);
typedef addr*(__thiscall* FindEntityFn)(addr entityManager, addr* out, const uint32_t* handle);
typedef uint32_t(__thiscall* SpawnFn)(addr self, addr handleRef, addr triggerInfoRef);

template <class F> inline F Fn(addr va) { return reinterpret_cast<F>(VA(va)); }
