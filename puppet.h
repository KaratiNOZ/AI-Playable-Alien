// puppet.h — тела игроков в мультиплеере (puppet.cpp): своё тело -> пакет STATE (mp.cpp шлёт), чужие тела -> «куклы».
// Кукла — персонаж этого мира, которого ведёт другой игрок: ИИ молчит, физическое тело переносится за ним, а поза на
// экране — его поза (поза слоя 7 подменяется сразу после того, как игра обновила мир модели, FUN_00692210).
#pragma once
#include "common.h"
#include <cstdint>
#include <string>
#include <vector>

// тело другого ЧЕЛОВЕКА: сначала — тот же персонаж в третьем лице, если его модель есть на уровне (DLC: экипаж «Ностромо»
// в «Экипаж — расходный материал», Ренсом и Лингард на части карт выживания). Целой модели Аманды в игре нет (у игрока —
// RIPLEY_FP без головы, других моделей Аманды нет ни на одном уровне) — тогда целый персонаж уровня по настройке
enum PupHumanBody { PB_AUTO = 0, PB_ANDROID = 1, PB_NPC = 2, PB_AMANDA_FP = 3, PB_COUNT };
const char* PupHumanBodyName(int i);

struct PupRow {
  uint64_t who = 0;
  int avatar = 0;        // MpAvatar
  std::string ident;     // кем он играет (AMANDA, DALLAS, RANSOME...), пусто — не человек / не знаем
  std::string body;      // кто его изображает здесь (уже переведено), пусто — никто
  float dist = -1;       // от моего тела, м
  int ageMs = -1;        // сколько не было пакета
  int fps = 0;           // пакетов в секунду
  bool hidden = false;   // он в вентиляции / за кулисами
  bool ghost = false;    // «зеркало» (проверка куклы своим же телом)
  bool pose = false;     // поза применяется (скелет совпал или нашлись кости по именам)
  bool same = false;     // изображает тот же персонаж (модель третьего лица)
  bool attacking = false;  // его Чужой сейчас бьёт (кукла отпущена на время удара)
};

// ---- сетевой поток (mp.cpp) ----
// своё тело: avatar — MpAvatar; false — слать нечего (нет уровня)
bool PupEncodeLocal(int avatar, uint64_t me, uint16_t level, std::vector<uint8_t>& out);
// чужое тело пришло (тело пакета без заголовка WHdr); level — хеш своего уровня
void PupOnState(const uint8_t* body, size_t n, uint64_t me, uint16_t level);
void PupForget(uint64_t who);  // игрок вышел
void PupRemoteAttack(uint64_t who);  // хост: его Чужой ударил (нажал кнопку) — наш Чужой-кукла бьёт здесь
// кем играю я (подсказка от лобби: кампания — AMANDA, выживание — выбранный персонаж; пусто — узнать по своей модели)
void PupSetMyIdentity(const char* ident, bool weak);  // weak — только если по своей модели не узнать
void PupClear(const char* why);  // все куклы — забыть (уровень сменился, вышли из лобби)
// идёт ли сейчас игра по сети (тогда своё тело ищется и куклы ведутся); avatar — кем играю я
void PupSetActive(bool on, int avatar);
uint16_t PupLevelHash(const std::string& shortLevel);

// ---- меню / отладка ----
std::vector<PupRow> PupSnapshot();
void PupSetHumanBody(int mode);
int PupHumanBody();
void PupGhost(bool on);  // «зеркало»: своё тело -> кукла в 2.5 м впереди (проверка без второго игрока)
bool PupGhostOn();

// ---- для других частей мода ----
bool PupIsBody(addr ch);   // этот персонаж — чья-то кукла (hugger.cpp: ИИ андроида молчит)
bool PupOwnsPlayer();      // своя Аманда отдана под куклу (alien_extra.cpp не прячет её, camera.cpp прячет только FP-руки)
// camera.cpp: правка палитры «из головы» (шея Чужого в позе слоя 7 на время вызова) — нечётное значение, пока правит
inline std::atomic<uint32_t> g_palEditSeq{ 0 };
// actors.cpp: удар Чужого по кнопке (ActAttack) — счётчик; mp.cpp шлёт хосту, если я клиент за Чужого
inline std::atomic<uint32_t> g_localAttackSeq{ 0 };
void InstallPuppet();
