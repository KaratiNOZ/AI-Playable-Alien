// puppet.cpp — тела игроков в мультиплеере: своё тело уходит другим пакетом STATE (mp.cpp шлёт 20 раз в секунду), чужие
// тела ведут «куклы» — персонажей этого мира.
//
// Кто кого изображает (MULTIPLAYER.md, «Куклы»):
//   * Чужой другого игрока — Чужой этого уровня (npc мода): ИИ — «ручное», пока он кукла.
//   * андроид — тот же андроид (ручка сущности +0x54/+0x58 одинакова на всех машинах), не нашёлся — ближайший; его ИИ
//     молчит (hugger.cpp: рубильник «стоять»).
//   * человек — тот же персонаж в третьем лице, если его модель есть на уровне (у DLC есть обе формы: экипаж «Ностромо» —
//     E_RIPLEY, DALLAS, PARKER, LAMBERT, ASH в BSPNOSTROMO_TWOTEAMS; RANSOME, LINGARD на части карт выживания). Кто есть
//     кто: отправитель шлёт имя персонажа (подсказка лобби или части своей FP-модели: JACKET_DALLAS...), здесь ищется
//     персонаж с головой (части HEAD/EYE/HAIR) с тем же именем сущности или частью LOD_<имя>/HEAD_<имя>. Целой модели
//     Аманды в игре нет (RIPLEY_FP без головы, других нет ни на одном уровне) — тогда целый персонаж уровня: андроид,
//     NPC-человек или своя Аманда без головы (настройка «Другой человек здесь — это»).
//   * удар — только по кнопке: Чужой-кукла бьёт, когда другой игрок нажал удар (W_ACT, mp.cpp): Аманда хоста рядом и
//     впереди — она (ActAttackTarget), иначе обычный удар мода; на время удара кукла отпущена (поза и перенос — игры).
//     Сам по себе (ИИ при касании) Чужой-кукла не бьёт: старт ActionMeleeAttack (FUN_00717d90) получает отказ — как у
//     самой игры, состояние+4 = 8. Свой Чужой клиента в «ручном» ИИ тоже бьёт только после нажатия.
// Как ведётся кукла:
//   * физическое тело — TpMove к его позиции каждый кадр (поток игры): двери, датчик движения, касания — как у живого;
//   * картинка — его поза: на выходе FUN_00692210 (мир модели этого кадра уже посчитан) поза слоя 7 синхронизируется
//     (FUN_00bba260) и переписывается на месте: кости — его (по номерам, если скелет тот же; по именам — если другой),
//     длины костей — свои; вся поза сдвигается так, чтобы на экране тело стояло ровно там и так, как у него (мир модели
//     анимация+0x10 — матрица 4x4 «строкой»: перенос +0x40). Дальше игра сама строит палитру, тени, кости для камер.
//   * он в вентиляции / за кулисами — кукла невидима (все кости в точку, масштаб 0).
// Потоки: сеть кладёт кадры (s_lock исключительно), поток игры выбирает тела и переносит их (общий замок мода + s_lock),
// рабочие потоки рендера подменяют позу (s_lock разделяемо; память игры — только после того, как замок отпущен: ошибка
// доступа под Safe не должна оставить его закрытым).
#include "puppet.h"
#include "game.h"
#include "interceptor.h"
#include "mp.h"
#include "state.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

void MpNote(const char* fmt, ...);  // mp.cpp: строка в AlienModUI.mp.log

namespace {

constexpr int kMaxB = 256;
constexpr int kSlots = 5;            // до 4 игроков + «зеркало»
constexpr int kFr = 4;               // кадров на игрока (между ними — интерполяция)
constexpr ms_t kDelayMs = 100;       // показываем чуть в прошлом — между двумя пришедшими кадрами
constexpr ms_t kStaleMs = 3000;      // столько нет пакетов — кукла отпускается
constexpr uint64_t kGhostId = 1;
constexpr float kGhostAhead = 2.5f;  // «зеркало» — на столько метров впереди своего тела
constexpr addr kPoseSync = 0x00bba260;      // hkaPose::getSyncedPoseModelSpace: thiscall(поза) -> {массив, число}
constexpr addr kAnimWorldUpd = 0x00692210;  // thiscall(анимация, 7): мир модели этого кадра (из FUN_006a5040)
// старт ActionMeleeAttack: thiscall(узел, персонаж, состояние), RET 8; отказ игры — состояние+4 = 8
constexpr addr kMeleeStart = 0x00717d90;
typedef addr(__thiscall* PoseSyncFn)(addr pose);

// кости тела по именам: у всех людей и андроидов игры одни имена (ANIMATION.PAK, *_BONE_GROUPS.TXT) — по ним поза
// Аманды (скелет FEMALEFP) ложится на NPC (FEMALENPC, MALE...) и андроида
const char* const kNames[] = {
  "Hips", "Pelvis", "Spine", "Spine1", "Spine2", "Spine3", "Neck", "Neck1", "Head", "Head1",
  "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase",
  "LeftShoulder", "LeftArm", "LeftArmRoll", "LeftForeArm", "LeftForeArmRoll", "LeftForeArmRoll1", "LeftHand",
  "RightShoulder", "RightArm", "RightArmRoll", "RightForeArm", "RightForeArmRoll", "RightForeArmRoll1", "RightHand",
  "LeftHandIndex1", "LeftHandIndex2", "LeftHandIndex3", "LeftHandMiddle1", "LeftHandMiddle2", "LeftHandMiddle3",
  "LeftHandPinky1", "LeftHandPinky2", "LeftHandPinky3", "LeftHandRing1", "LeftHandRing2", "LeftHandRing3",
  "LeftHandThumb1", "LeftHandThumb2", "LeftHandThumb3",
  "RightHandIndex1", "RightHandIndex2", "RightHandIndex3", "RightHandMiddle1", "RightHandMiddle2", "RightHandMiddle3",
  "RightHandPinky1", "RightHandPinky2", "RightHandPinky3", "RightHandRing1", "RightHandRing2", "RightHandRing3",
  "RightHandThumb1", "RightHandThumb2", "RightHandThumb3",
};
constexpr int kNamed = int(sizeof(kNames) / sizeof(kNames[0]));

// ---------------- провод: тело STATE (после WHdr) ----------------
enum : uint8_t { F_HIDDEN = 1, F_POSE = 2, F_INDEX = 4, F_DEAD = 8, F_HIPS = 16, F_NOBODY = 32 };
#pragma pack(push, 1)
struct WState {
  uint64_t who;          // чьё тело (хост пересылает чужие как есть)
  uint32_t ts;           // NowMs отправителя
  uint16_t level;        // PupLevelHash его уровня
  uint8_t avatar, flags;
  uint32_t ent, inst;    // андроид: его ручка (+0x54/+0x58)
  float pos[3], yaw;     // персонаж +0x70, +0x174
  int16_t wq[4];         // мир модели: поворот (×32767)
  float wt[3];           //             перенос
  uint16_t restHips;     // высота Hips в позе привязки, мм (масштаб для другого скелета)
  int16_t hips[3];       // Hips (или корень) в пространстве модели, мм
  uint32_t skelHash;     // скелет (родители + число): «тот же скелет» — кости по номерам
  uint16_t nb;           // костей в позе: по номерам — все, по именам — kNamed
  char ident[12];        // кем играет человек (AMANDA, DALLAS...), пусто — не знаем
};
#pragma pack(pop)
// дальше: маска (nb бит), номер большей компоненты кватерниона (по 2 бита на кость), по 3 int16 на каждую кость из маски
constexpr float kQS = 32767.f / 0.70710678f;

struct Q { float x, y, z, w; };
inline Q QMul(const Q& a, const Q& b) {
  return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
inline Q QConj(const Q& a) { return { -a.x, -a.y, -a.z, a.w }; }
inline Q QNorm(Q q) {
  const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (!(l > 1e-6f)) return { 0, 0, 0, 1 };
  return { q.x / l, q.y / l, q.z / l, q.w / l };
}
inline void QRot(const Q& q, const float v[3], float o[3]) {  // v' = q v q*
  const float tx = 2 * (q.y * v[2] - q.z * v[1]), ty = 2 * (q.z * v[0] - q.x * v[2]), tz = 2 * (q.x * v[1] - q.y * v[0]);
  o[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty);
  o[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz);
  o[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
}
inline Q QNlerp(Q a, Q b, float t) {
  if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = { -b.x, -b.y, -b.z, -b.w };
  return QNorm({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}
// мир модели (анимация+0x10): 4x4 «строкой» — строки 0..2 — куда идут оси X, Y, Z модели, строка 3 (+0x40) — перенос.
// В виде «столбцом» (v' = R v + t): R[r][c] = a[c*4 + r]
Q QFromRowMatrix(const float* a) {
  const float m00 = a[0], m01 = a[4], m02 = a[8], m10 = a[1], m11 = a[5], m12 = a[9], m20 = a[2], m21 = a[6], m22 = a[10];
  const float tr = m00 + m11 + m22;
  Q q;
  if (tr > 0) {
    const float s = std::sqrt(tr + 1.f) * 2;
    q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s };
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.f + m00 - m11 - m22) * 2;
    q = { 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
  } else if (m11 > m22) {
    const float s = std::sqrt(1.f + m11 - m00 - m22) * 2;
    q = { (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s };
  } else {
    const float s = std::sqrt(1.f + m22 - m00 - m11) * 2;
    q = { (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s };
  }
  return QNorm(q);
}
void EncQ(const Q& q, uint8_t& idx, int16_t c[3]) {
  const float v[4] = { q.x, q.y, q.z, q.w };
  int m = 0;
  for (int i = 1; i < 4; i++)
    if (std::fabs(v[i]) > std::fabs(v[m])) m = i;
  const float sg = v[m] < 0 ? -1.f : 1.f;
  int k = 0;
  for (int i = 0; i < 4; i++)
    if (i != m) c[k++] = int16_t(std::clamp(std::lround(v[i] * sg * kQS), -32767L, 32767L));
  idx = uint8_t(m);
}
Q DecQ(uint8_t idx, const int16_t c[3]) {
  float v[4];
  float sum = 0;
  int k = 0;
  for (int i = 0; i < 4; i++) {
    if (i == idx) continue;
    v[i] = float(c[k++]) / kQS;
    sum += v[i] * v[i];
  }
  v[idx & 3] = std::sqrt(std::max(0.f, 1.f - sum));
  return { v[0], v[1], v[2], v[3] };
}
inline int16_t Mm(float m) { return int16_t(std::clamp(std::lround(m * 1000.f), -32767L, 32767L)); }

// ---------------- скелет персонажа (поток игры заполняет) ----------------
struct Skel {
  addr skel = 0;
  int n = 0;
  int16_t par[kMaxB];
  uint32_t hash = 0;
  int16_t named[kNamed];
  int found = 0;         // сколько костей нашлось по именам
  int hips = -1;         // «Hips» или первый корень
  float restHips = 0;    // высота hips в позе привязки, м
  bool fromNames = false;
};
// анимация персонажа = персонаж+0x3f0; поза слоя 7 — *(анимация+0x24c): +0 скелет, +0x10 поза модели, +0x14 число.
// hkaSkeleton: +0xc родители (int16), +0x18 кости (hkaBone по 8 байт: имя), +0x1c число, +0x24 поза привязки, +0x28 число
bool ReadSkel(addr ch, Skel& s) {  // поток игры (BoneIndex — функция игры)
  const addr anim = ch + 0x3f0;
  bool ok = Safe([&] {
    const addr P7 = rp(anim + 0x24c);
    s.skel = P7 ? rp(P7) : 0;
    if (!s.skel) return;
    s.n = rd<int>(s.skel + 0x1c);
    if (s.n <= 0 || s.n > kMaxB || rd<int>(P7 + 0x14) != s.n) {
      s.n = 0;
      return;
    }
    const addr par = rp(s.skel + 0xc);
    uint32_t h = 2166136261u ^ uint32_t(s.n);
    for (int i = 0; i < s.n; i++) {
      s.par[i] = rd<int16_t>(par + i * 2);
      h = (h ^ uint16_t(s.par[i])) * 16777619u;
    }
    s.hash = h;
  });
  if (!ok || !s.n) return false;
  // имена — из скелета; нет (вырезаны) — функцией игры «кость по имени»
  for (int k = 0; k < kNamed; k++) s.named[k] = -1;
  s.found = 0;
  s.fromNames = false;
  Safe([&] {
    const addr bones = rp(s.skel + 0x18);
    if (!bones) return;
    for (int i = 0; i < s.n; i++) {
      const addr np = rp(bones + i * 8) & ~addr(1);
      if (!np) continue;
      char nm[48];
      int l = 0;
      for (; l < 47; l++) {
        const char c = rd<char>(np + l);
        if (!c) break;
        nm[l] = c;
      }
      nm[l] = 0;
      for (int k = 0; k < kNamed; k++)
        if (s.named[k] < 0 && !_stricmp(nm, kNames[k])) s.named[k] = int16_t(i), s.found++, s.fromNames = true;
    }
  });
  if (!s.found)
    for (int k = 0; k < kNamed; k++) {
      int idx = -1;
      Safe([&] { idx = Fn<BoneIndexFn>(va::BoneIndex)(anim, kNames[k], 7); });
      if (idx >= 0 && idx < s.n) s.named[k] = int16_t(idx), s.found++;
    }
  s.hips = s.named[0];
  if (s.hips < 0)
    for (int i = 0; i < s.n; i++)
      if (s.par[i] < 0) {
        s.hips = i;
        break;
      }
  // высота hips в позе привязки: цепочка от корня (поза привязки — локальная)
  s.restHips = 0;
  Safe([&] {
    const addr ref = rp(s.skel + 0x24);
    if (!ref || rd<int>(s.skel + 0x28) != s.n || s.hips < 0) return;
    int chain[kMaxB], cn = 0;
    for (int i = s.hips; i >= 0 && cn < kMaxB; i = s.par[i] < i ? s.par[i] : -1) chain[cn++] = i;
    float t[3] = { 0, 0, 0 };
    Q q{ 0, 0, 0, 1 };
    for (int k = cn - 1; k >= 0; k--) {
      const addr b = ref + chain[k] * 0x30;
      const float lt[3] = { rf(b), rf(b + 4), rf(b + 8) };
      float o[3];
      QRot(q, lt, o);
      t[0] += o[0], t[1] += o[1], t[2] += o[2];
      q = QMul(q, Q{ rf(b + 0x10), rf(b + 0x14), rf(b + 0x18), rf(b + 0x1c) });
    }
    if (t[1] > 0.1f && t[1] < 5.f) s.restHips = t[1];
  });
  return true;
}

// ---------------- кадры и игроки ----------------
struct Frame {
  uint32_t ts = 0;
  ms_t recv = 0;
  uint8_t flags = 0, avatar = 0;
  uint32_t ent = 0, inst = 0;
  float pos[3]{}, yaw = 0, wt[3]{}, hips[3]{}, restHips = 0;
  Q wq{ 0, 0, 0, 1 };
  uint32_t skelHash = 0;
  int nb = 0;
  char ident[12]{};
  uint8_t has[kMaxB];
  Q q[kMaxB];
};
enum BodyKind { BK_NONE = 0, BK_ALIEN, BK_ANDROID, BK_NPC, BK_PLAYER, BK_SAME, BK_HUGGER };
struct Slot {
  bool used = false, ghost = false, forget = false;
  uint64_t who = 0;
  int avatar = 0;
  Frame fr[kFr];
  int head = -1, count = 0;
  ms_t lastRecv = 0;
  int32_t off = 0;   // NowMs здесь − NowMs там (минимум: задержка сети + разница часов)
  bool offOk = false;
  ms_t offRelaxAt = 0, fpsAt = 0;
  int pk = 0, fps = 0;
  // тело здесь (поток игры)
  addr body = 0;
  int kind = BK_NONE;
  Skel sk;
  bool aiTaken = false;
  int aiWas = 0, pref = 0;
  ms_t ventTryAt = 0, pickTryAt = 0;
  float dist = -1;
  bool worldLogged = false;
  int moves = 0;
  bool same = false;                 // тот же персонаж в третьем лице
  bool attacking = false;            // его Чужой бьёт: кукла отпущена (поток игры пишет под s_lock)
  ms_t attackReqAt = 0, attackStart = 0, attackUntil = 0;
  ms_t sameCheckAt = 0;              // изображает не «он же» — раз в 5 с: не появился ли тот же персонаж на уровне
  ms_t reportAt = 0;                 // отчёт в mp.log раз в 10 с: подмены позы, где на самом деле его тело
  uint32_t appliedWas = 0;
};
Slot s_slot[kSlots];
std::atomic<addr> s_animKey[kSlots];      // анимация куклы — для горячего хука (без замка)
std::atomic<uint32_t> s_applied[kSlots];  // поз подменено (для меню)
std::atomic<bool> s_poseOk[kSlots];
SRWLOCK s_lock = SRWLOCK_INIT;
struct Shared {
  Shared() { AcquireSRWLockShared(&s_lock); }
  ~Shared() { ReleaseSRWLockShared(&s_lock); }
};
struct Excl {
  Excl() { AcquireSRWLockExclusive(&s_lock); }
  ~Excl() { ReleaseSRWLockExclusive(&s_lock); }
};

struct Mine { addr ch = 0; int avatar = 0; Skel sk; char fp[12]{}; };
Mine s_mine;  // своё тело (под s_lock)
char s_identHint[12]{};                    // кем играю — подсказка лобби (под s_lock)
bool s_identWeak = false;                  // подсказка — только если по своей модели не узнать
std::atomic<addr> s_myChar{ 0 };          // своё тело — для горячего хука удара
std::atomic<ms_t> s_meleeAllowUntil{ 0 };  // Чужому-кукле можно начать удар до этого времени
std::atomic<bool> s_active{ false }, s_ghost{ false }, s_ownsPlayer{ false };
std::atomic<int> s_myAvatar{ AV_HUMAN }, s_humanBody{ PB_AUTO };

// ---------------- интерполяция ----------------
struct Moment {
  const Frame *a = nullptr, *b = nullptr;
  float t = 1;
  uint8_t flags = 0;
  float pos[3]{}, yaw = 0, wt[3]{}, hips[3]{};
  Q wq{ 0, 0, 0, 1 };
};
bool Interp(const Slot& s, ms_t now, Moment& m) {  // под s_lock
  if (!s.count) return false;
  const uint32_t rts = uint32_t(now) - uint32_t(s.off) - uint32_t(kDelayMs);
  const Frame *newer = nullptr, *older = nullptr;
  for (int k = 0; k < s.count; k++) {
    const Frame* f = &s.fr[(s.head - k + kFr) % kFr];
    if (int32_t(f->ts - rts) > 0) newer = f;
    else {
      older = f;
      break;
    }
  }
  if (!older) older = newer;
  if (!newer) newer = older;
  float t = 1;
  if (newer != older) {
    const float span = float(int32_t(newer->ts - older->ts));
    t = span > 0 ? std::clamp(float(int32_t(rts - older->ts)) / span, 0.f, 1.f) : 1.f;
  }
  m.a = older, m.b = newer, m.t = t;
  m.flags = newer->flags;
  for (int i = 0; i < 3; i++) {
    m.pos[i] = older->pos[i] + (newer->pos[i] - older->pos[i]) * t;
    m.wt[i] = older->wt[i] + (newer->wt[i] - older->wt[i]) * t;
    m.hips[i] = older->hips[i] + (newer->hips[i] - older->hips[i]) * t;
  }
  m.yaw = older->yaw + WrapPi(newer->yaw - older->yaw) * t;
  m.wq = QNlerp(older->wq, newer->wq, t);
  return true;
}

// ---------------- приём ----------------
int SlotOf(uint64_t who, bool alloc) {  // под s_lock (исключительно)
  for (int i = 0; i < kSlots; i++)
    if (s_slot[i].used && s_slot[i].who == who) return i;
  if (!alloc) return -1;
  for (int i = 0; i < kSlots; i++)
    if (!s_slot[i].used && !s_slot[i].body) {
      Slot& s = s_slot[i];
      s.used = true, s.ghost = who == kGhostId, s.forget = false;
      s.who = who, s.count = 0, s.head = -1, s.offOk = false, s.pk = 0, s.fps = 0, s.lastRecv = 0;
      s.kind = BK_NONE, s.dist = -1, s.pickTryAt = 0, s.worldLogged = false;
      return i;
    }
  return -1;
}
Frame s_tmp;  // разбор пакета — только сетевой поток
void OnState(const uint8_t* b, size_t n, uint64_t me, uint16_t level, bool ghost) {
  if (n < sizeof(WState)) return;
  WState h;
  memcpy(&h, b, sizeof h);
  if (!ghost && (h.who == me || h.who <= kGhostId)) return;
  if (h.level != level || h.nb > kMaxB || h.avatar >= AV_COUNT) return;
  Frame& f = s_tmp;
  f.ts = h.ts, f.recv = NowMs(), f.flags = h.flags, f.avatar = h.avatar, f.ent = h.ent, f.inst = h.inst;
  memcpy(f.pos, h.pos, sizeof f.pos);
  f.yaw = h.yaw;
  f.wq = QNorm({ h.wq[0] / 32767.f, h.wq[1] / 32767.f, h.wq[2] / 32767.f, h.wq[3] / 32767.f });
  memcpy(f.wt, h.wt, sizeof f.wt);
  f.restHips = h.restHips / 1000.f;
  for (int i = 0; i < 3; i++) f.hips[i] = h.hips[i] / 1000.f;
  f.skelHash = h.skelHash;
  f.nb = h.nb;
  memcpy(f.ident, h.ident, sizeof f.ident);
  f.ident[sizeof f.ident - 1] = 0;
  memset(f.has, 0, sizeof f.has);
  if (f.flags & F_POSE) {
    const size_t maskN = (size_t(f.nb) + 7) / 8, idxN = (size_t(f.nb) + 3) / 4;
    const uint8_t* mask = b + sizeof h;
    const uint8_t* idx = mask + maskN;
    const uint8_t* comp = idx + idxN;
    if (sizeof h + maskN + idxN > n) return;
    size_t present = 0;
    for (int i = 0; i < f.nb; i++) present += (mask[i >> 3] >> (i & 7)) & 1;
    if (comp + present * 6 > b + n) return;
    for (int i = 0; i < f.nb; i++) {
      if (!((mask[i >> 3] >> (i & 7)) & 1)) continue;
      int16_t c[3];
      memcpy(c, comp, 6);
      comp += 6;
      f.q[i] = DecQ((idx[i >> 2] >> ((i & 3) * 2)) & 3, c);
      f.has[i] = 1;
    }
  }
  const uint64_t who = ghost ? kGhostId : h.who;
  if (ghost) {  // «зеркало»: то же тело, только впереди
    const float dx = std::sin(f.yaw) * kGhostAhead, dz = std::cos(f.yaw) * kGhostAhead;
    f.pos[0] += dx, f.pos[2] += dz, f.wt[0] += dx, f.wt[2] += dz;
  }
  Excl l;
  const int si = SlotOf(who, true);
  if (si < 0) return;
  Slot& s = s_slot[si];
  s.avatar = f.avatar;
  s.head = (s.head + 1) % kFr;
  s.fr[s.head] = f;
  s.count = std::min(s.count + 1, kFr);
  s.lastRecv = f.recv;
  const int32_t off = int32_t(uint32_t(f.recv) - f.ts);
  if (!s.offOk || off < s.off) s.off = off, s.offOk = true, s.offRelaxAt = f.recv;
  else if (f.recv - s.offRelaxAt > 500) s.off += 2, s.offRelaxAt = f.recv;  // часы плывут / был редкий быстрый пакет
  s.pk++;
  if (f.recv - s.fpsAt >= 1000) s.fps = s.pk, s.pk = 0, s.fpsAt = f.recv;
}

// ---------------- подмена позы (рабочие потоки рендера) ----------------
struct Target {
  int n = 0;
  int16_t par[kMaxB];
  uint8_t has[kMaxB];
  Q q[kMaxB];
  bool hips = false, hidden = false;
  int hipsIdx = -1;
  float hipsT[3]{};
  Q ws{ 0, 0, 0, 1 };
  float wt[3]{};
};
bool BuildTarget(const Slot& s, ms_t now, Target& T) {  // под s_lock (разделяемо)
  Moment m;
  if (!s.body || !s.sk.n || s.attacking || !Interp(s, now, m)) return false;
  T.n = s.sk.n;
  memcpy(T.par, s.sk.par, sizeof(int16_t) * size_t(T.n));
  memset(T.has, 0, size_t(T.n));
  T.hidden = (m.flags & F_HIDDEN) != 0;
  T.ws = m.wq;
  memcpy(T.wt, m.wt, sizeof T.wt);
  T.hips = false;
  T.hipsIdx = s.sk.hips;
  const Frame& a = *m.a;
  const Frame& b = *m.b;
  if (!(b.flags & F_POSE)) return true;
  const bool same = (a.flags & (F_POSE | F_INDEX)) == (b.flags & (F_POSE | F_INDEX)) && a.nb == b.nb;
  auto rot = [&](int k) { return same && a.has[k] ? QNlerp(a.q[k], b.q[k], m.t) : b.q[k]; };
  float scale = 1;
  if (b.flags & F_INDEX) {  // тот же скелет — по номерам
    if (b.skelHash != s.sk.hash || b.nb != s.sk.n) return true;
    for (int i = 0; i < T.n; i++)
      if (b.has[i]) T.has[i] = 1, T.q[i] = rot(i);
  } else {  // по именам
    for (int k = 0; k < kNamed && k < b.nb; k++) {
      const int own = s.sk.named[k];
      if (own >= 0 && b.has[k]) T.has[own] = 1, T.q[own] = rot(k);
    }
    if (s.sk.restHips > 0.1f && b.restHips > 0.1f) scale = s.sk.restHips / b.restHips;
  }
  if ((b.flags & F_HIPS) && T.hipsIdx >= 0) {
    T.hips = true;
    for (int i = 0; i < 3; i++) T.hipsT[i] = m.hips[i] * scale;
  }
  return true;
}
struct X { float t[3]; Q q; float s[3]; };
void ApplyTarget(addr anim, const Target& T) {  // под Safe, без s_lock
  const addr P7 = rp(anim + 0x24c);
  if (!P7) return;
  const addr hdr = Fn<PoseSyncFn>(kPoseSync)(P7);  // синхронизировать позу модели (игра сделает то же сразу после)
  if (!hdr) return;
  const addr arr = rp(hdr);
  const int n = rd<int>(hdr + 4);
  if (!arr || n != T.n) return;
  float a[16];
  for (int k = 0; k < 16; k++) a[k] = rf(anim + 0x10 + addr(k) * 4);
  const Q qw = QFromRowMatrix(a);
  const float tw[3] = { a[12], a[13], a[14] };
  // поправка: тело на экране — там и так, как у него: M' = conj(qw)·qs·M, t' = conj(qw)(ts − tw)
  const Q cw = QConj(qw), qc = QNorm(QMul(cw, T.ws));
  float dt[3] = { T.wt[0] - tw[0], T.wt[1] - tw[1], T.wt[2] - tw[2] }, tc[3];
  QRot(cw, dt, tc);
  if (T.hidden) {  // в вентиляции: все кости в одну точку, масштаб 0
    for (int i = 0; i < n; i++) {
      const addr b = arr + addr(i) * 0x30;
      wf(b, tc[0]), wf(b + 4, tc[1]), wf(b + 8, tc[2]);
      wf(b + 0x20, 0.f), wf(b + 0x24, 0.f), wf(b + 0x28, 0.f);
    }
    return;
  }
  static thread_local X* L = nullptr;  // своя поза в локальном виде (у каждого рабочего потока свой буфер)
  if (!L) L = static_cast<X*>(HeapAlloc(GetProcessHeap(), 0, sizeof(X) * kMaxB));
  if (!L) return;
  auto load = [&](int i, X& x) {
    const addr b = arr + addr(i) * 0x30;
    x.t[0] = rf(b), x.t[1] = rf(b + 4), x.t[2] = rf(b + 8);
    x.q = { rf(b + 0x10), rf(b + 0x14), rf(b + 0x18), rf(b + 0x1c) };
    x.s[0] = rf(b + 0x20), x.s[1] = rf(b + 0x24), x.s[2] = rf(b + 0x28);
  };
  for (int i = 0; i < n; i++) {  // локальная = (модель родителя)^-1 · модель
    X m;
    load(i, m);
    const int p = T.par[i];
    if (p < 0 || p >= i) {
      L[i] = m;
      continue;
    }
    X pm;
    load(p, pm);
    const Q ci = QConj(pm.q);
    const float d[3] = { m.t[0] - pm.t[0], m.t[1] - pm.t[1], m.t[2] - pm.t[2] };
    float lt[3];
    QRot(ci, d, lt);
    X& l = L[i];
    for (int k = 0; k < 3; k++) {
      const float ps = std::fabs(pm.s[k]) > 1e-6f ? pm.s[k] : 1.f;
      l.t[k] = lt[k] / ps, l.s[k] = m.s[k] / ps;
    }
    l.q = QMul(ci, m.q);
  }
  for (int i = 0; i < n; i++) {  // новая поза модели: повороты — его, длины костей — свои; родители раньше детей
    const int p = T.par[i];
    X m = L[i];
    if (p >= 0 && p < i) {
      const X& P = L[p];  // уже новая
      const float st[3] = { P.s[0] * m.t[0], P.s[1] * m.t[1], P.s[2] * m.t[2] };
      float o[3];
      QRot(P.q, st, o);
      for (int k = 0; k < 3; k++) m.t[k] = P.t[k] + o[k], m.s[k] = P.s[k] * m.s[k];
      m.q = QMul(P.q, m.q);
    }
    if (T.has[i]) m.q = T.q[i];
    if (T.hips && i == T.hipsIdx) memcpy(m.t, T.hipsT, sizeof m.t);
    L[i] = m;
  }
  for (int i = 0; i < n; i++) {
    const X& m = L[i];
    float o[3];
    QRot(qc, m.t, o);
    const Q q = QNorm(QMul(qc, m.q));
    const addr b = arr + addr(i) * 0x30;
    wf(b, o[0] + tc[0]), wf(b + 4, o[1] + tc[1]), wf(b + 8, o[2] + tc[2]);
    wf(b + 0x10, q.x), wf(b + 0x14, q.y), wf(b + 0x18, q.z), wf(b + 0x1c, q.w);
  }
}
void OnAnimWorldLeave(Invocation& inv) {  // горячий: все персонажи, рабочие потоки
  const addr anim = inv.ecx;
  int si = -1;
  for (int i = 0; i < kSlots; i++)
    if (anim && s_animKey[i].load(std::memory_order_relaxed) == anim) {
      si = i;
      break;
    }
  if (si < 0) return;
  static thread_local Target* T = nullptr;
  if (!T) T = static_cast<Target*>(HeapAlloc(GetProcessHeap(), 0, sizeof(Target)));
  if (!T) return;
  bool ok;
  {
    Shared l;
    const Slot& s = s_slot[si];
    ok = s.body && s.body + 0x3f0 == anim && BuildTarget(s, NowMs(), *T);
  }
  if (!ok) return;
  bool pose = false;
  for (int i = 0; i < T->n && !pose; i++) pose = T->has[i] != 0;
  if (Safe([&] { ApplyTarget(anim, *T); })) s_applied[si].fetch_add(1, std::memory_order_relaxed);
  s_poseOk[si].store(pose, std::memory_order_relaxed);
}

// ---------------- удар только по кнопке ----------------
// Чужой-кукла: старт удара — только пока другой игрок нажал удар (s_meleeAllowUntil). Свой Чужой клиента в «ручном» ИИ —
// только после своей кнопки (attack.keyAt). Иначе — отказ, как у самой игры (нет цели): состояние+4 = 8, RET 8
void OnMeleeStartEnter(Invocation& inv) {  // поток игры, без замка
  const addr ch = inv.arg[0];
  if (!ch || ch != npc || !s_active.load(std::memory_order_relaxed)) return;
  const ms_t now = NowMs();
  const char* who = nullptr;
  if (PupIsBody(ch)) {
    if (now >= s_meleeAllowUntil.load()) who = "the puppet Alien (nobody pressed attack)";
  } else if (ch == s_myChar.load() && s_myAvatar.load() == AV_ALIEN && aiMode == 0) {
    if (now - attack.keyAt > 2500 && !forceTarget.load()) who = "my Alien (attack key not pressed)";
  }
  if (!who) return;
  uint32_t st = 0;
  if (!SafeRead(inv.arg[1] + 4, st)) return;
  wr<uint32_t>(inv.arg[1] + 4, 8);
  inv.skip = true, inv.skipPop = 8;
  static ms_t saidAt = 0;
  if (now - saidAt > 3000) saidAt = now, Log("puppet: melee refused — %s", who);
}

// ---------------- кто изображает (поток игры) ----------------
uint32_t ClassOf(addr e) {
  uint32_t c = 0xFF;
  Safe([&] { c = rd<uint32_t>(rp(e + 0x64) + 4); });
  return c;
}
bool IsHumanNpc(addr e) {  // CHARACTER_CLASS: 3 CIVILIAN, 4 SECURITY, 6 INNOCENT, 9 MELEE_HUMAN
  if (!e || e == player || e == npc) return false;
  const uint32_t c = ClassOf(e);
  return c == 3 || c == 4 || c == 6 || c == 9;
}
float Dist2(addr e, const float p[3]) {
  V3 q{};
  if (!Safe([&] { q = rv3(e + 0x70); })) return 1e30f;
  return (q.x - p[0]) * (q.x - p[0]) + (q.y - p[1]) * (q.y - p[1]) + (q.z - p[2]) * (q.z - p[2]);
}
// ---- внешность персонажа: части модели (как CHR_SetSubModelVisibility; функции игры — только поток игры) ----
template <class F> void ForParts(addr ch, F f) {
  const addr anim = ch + 0x3f0;
  auto visit = [&](addr m) {
    if (!m) return;
    const uint32_t n = std::min<uint32_t>(rd<uint32_t>(rp(m + 200) + 0x54), 256);
    const addr arr = rp(m + 0x10c);
    for (uint32_t i = 0; i < n; i++)
      if (const addr sub = rp(arr + i * 4))
        if (const char* nm = reinterpret_cast<const char*>(rp(rp(rp(sub + 0x38)) + 8))) f(nm);
  };
  if (!rd<uint8_t>(anim + 0x3cd)) visit(Fn<ModelInstFn>(va::ModelInst)(anim));
  else
    for (uint32_t k = 0, n = std::min<uint32_t>(rd<uint32_t>(anim + 0x3d0), 16); k < n; k++) visit(Fn<ModelInstAtFn>(va::ModelInstAt)(anim, k));
}
// персонажи, у которых в данных есть модель (FP и/или третьего лица): имя сущности или части LOD_/HEAD_/HAIR_<имя>
const char* const kIdents[] = { "RIPLEY", "DALLAS", "PARKER", "LAMBERT", "ASH", "KANE", "BRETT", "RANSOME", "LINGARD", "AXEL",
                                "HUGHES", "RICARDO", "TAYLOR", "SAMUELS", "WAITS", "MARLOW", "VERLAINE", "CONNOR", "KUHLMAN" };
struct Look {
  char ident[12]{};   // кто это по частям модели
  bool third = false;  // модель третьего лица: есть голова / глаза / волосы
  int parts = 0;
};
bool HasWord(const char* s, const char* w) {  // слово w в имени части (разделители _ и пробел), без учёта регистра
  const size_t l = strlen(w);
  for (const char* p = s; *p; p++) {
    if (_strnicmp(p, w, l)) continue;
    const bool before = p == s || p[-1] == '_' || p[-1] == ' ';
    const bool after = !p[l] || p[l] == '_' || p[l] == ' ';
    if (before && after) return true;
  }
  return false;
}
Look ReadLook(addr ch) {  // поток игры
  Look L;
  Safe([&] {
    ForParts(ch, [&](const char* nm) {
      char b[64];
      int i = 0;
      for (; i < 63 && nm[i]; i++) b[i] = nm[i];
      b[i] = 0;
      L.parts++;
      if (HasWord(b, "HEAD") || HasWord(b, "EYE") || HasWord(b, "HAIR") || strstr(b, "EYE_") || strstr(b, "HEAD_")) L.third = true;
      if (L.ident[0]) return;
      if (!_stricmp(b, "FLIGHTSUIT")) strcpy(L.ident, "AMANDA");  // RIPLEY_FP — Аманда (головы нет)
      else if (!_stricmp(b, "LOD_DOCTOR")) strcpy(L.ident, "LINGARD");
      else
        for (const char* id : kIdents)
          if (HasWord(b, id) && (strstr(b, "LOD") || strstr(b, "HEAD") || strstr(b, "HAIR") || strstr(b, "JACKET") || strstr(b, "EYE"))) {
            strcpy(L.ident, id);
            break;
          }
    });
  });
  return L;
}
bool NameHas(addr ch, const char* id) {  // имя сущности персонажа (+0x10) содержит id
  const std::string n = CharName(ch);
  const size_t l = strlen(id);
  for (size_t i = 0; i + l <= n.size(); i++)
    if (!_strnicmp(n.c_str() + i, id, l)) return true;
  return false;
}

const char* KindName(int k) {
  switch (k) {
    case BK_ALIEN: return TR("Чужой уровня", "the level's Alien");
    case BK_ANDROID: return TR("андроид", "android");
    case BK_NPC: return TR("NPC", "NPC");
    case BK_SAME: return TR("он же (3-е лицо)", "same character (3rd person)");
    case BK_HUGGER: return TR("лицехват", "facehugger");
    case BK_PLAYER: return TR("своя Аманда (без головы)", "own Amanda (no head)");
    default: return "";
  }
}

// тот же персонаж в третьем лице (модель с головой, то же имя сущности или части LOD_/HEAD_<имя>), ближайший к нему
addr FindSame(const Frame& f, addr mine, const std::vector<addr>& taken) {
  if (!f.ident[0] || !strcmp(f.ident, "AMANDA")) return 0;  // Аманды в третьем лице в игре нет
  addr best = 0;
  float bd = 1e30f;
  for (addr e : ActiveChars()) {
    if (!e || e == mine || e == player || e == npc || std::find(taken.begin(), taken.end(), e) != taken.end() || DeathState(e) != 0) continue;
    if (IsAndroid(e) || IsHugger(e)) continue;
    const uint32_t c = ClassOf(e);
    if (c == 1 || c == 2 || c == 5 || c == 7) continue;
    const Look L = ReadLook(e);
    if (!L.third || (strcmp(L.ident, f.ident) != 0 && !NameHas(e, f.ident))) continue;
    const float d = Dist2(e, f.pos);
    if (d < bd) bd = d, best = e;
  }
  return best;
}

addr PickBody(int avatar, const Frame& f, addr mine, const std::vector<addr>& taken, int& kind) {
  const std::vector<addr> act = ActiveChars();
  auto free = [&](addr e) {
    return e && e != mine && std::find(taken.begin(), taken.end(), e) == taken.end() && DeathState(e) == 0;
  };
  auto nearest = [&](bool (*ok)(addr)) {
    addr best = 0;
    float bd = 1e30f;
    for (addr e : act)
      if (free(e) && ok(e)) {
        const float d = Dist2(e, f.pos);
        if (d < bd) bd = d, best = e;
      }
    return best;
  };
  kind = BK_NONE;
  if (avatar == AV_ALIEN) {
    if (npc && npc != mine && std::find(taken.begin(), taken.end(), npc) == taken.end()) kind = BK_ALIEN;
    return kind ? npc : 0;
  }
  if (avatar == AV_ANDROID || avatar == AV_FACEHUGGER) {  // тот же андроид / лицехват, что у него, иначе ближайший
    const bool hug = avatar == AV_FACEHUGGER;
    auto is = [hug](addr e) { return hug ? IsHugger(e) : IsAndroid(e); };
    for (addr e : act) {
      uint32_t a = 0, b = 0;
      if (free(e) && is(e) && Safe([&] { a = rd<uint32_t>(e + 0x54), b = rd<uint32_t>(e + 0x58); }) && a == f.ent && b == f.inst)
        return kind = hug ? BK_HUGGER : BK_ANDROID, e;
    }
    const addr e = nearest(hug ? +[](addr x) { return IsHugger(x); } : +[](addr x) { return IsAndroid(x); });
    if (e) kind = hug ? BK_HUGGER : BK_ANDROID;
    return e;
  }
  // человек: тот же персонаж в третьем лице, если он есть на уровне
  if (const addr e = FindSame(f, mine, taken)) return kind = BK_SAME, e;
  const addr pl = player && free(player) ? player : 0;
  const int pref = s_humanBody.load();
  const int order[PB_COUNT][3] = { { BK_ANDROID, BK_NPC, BK_PLAYER },    // авто
                                   { BK_ANDROID, BK_PLAYER, BK_NONE },   // андроид
                                   { BK_NPC, BK_ANDROID, BK_PLAYER },    // NPC-человек
                                   { BK_PLAYER, BK_ANDROID, BK_NPC } };  // своя Аманда
  for (int k : order[std::clamp(pref, 0, PB_COUNT - 1)]) {
    addr e = 0;
    if (k == BK_ANDROID) e = nearest(IsAndroid);
    else if (k == BK_NPC) e = nearest(IsHumanNpc);
    else if (k == BK_PLAYER) e = pl;
    if (e) return kind = k, e;
  }
  return 0;
}

void Release(Slot& s, const char* why) {  // поток игры, общий замок мода; s_lock — НЕ держим
  if (!s.body) return;
  const addr b = s.body;
  {
    Excl l;
    for (int i = 0; i < kSlots; i++)
      if (&s_slot[i] == &s) s_animKey[i] = 0;
    s.body = 0;
    s.kind = BK_NONE;
  }
  s.attacking = false;
  if (s.aiTaken) {
    s.aiTaken = false;
    if (npc == b && aiMode == 0 && s.aiWas != 0) ActSetAi(s.aiWas);
  }
  if (b == player) s_ownsPlayer = false;
  MpNote("puppet of %llu: body %s released (%s)", static_cast<unsigned long long>(s.who), CharName(b).c_str(), why);
}

addr MyBody() {
  if (!s_active) return 0;
  const int av = s_myAvatar;
  if (av == AV_ALIEN) return npc;
  if (av == AV_ANDROID) return hugger && !IsHugger(hugger) ? hugger : 0;
  if (av == AV_FACEHUGGER) return hugger && IsHugger(hugger) ? hugger : 0;
  return player;
}

ms_t s_tickAt = 0;
void OnGameTick(Invocation&) {  // LocoUpdate (поток игры, общий замок мода): раз в ~10 мс
  const ms_t now = NowMs();
  if (now - s_tickAt < 10) return;
  s_tickAt = now;
  // своё тело и его скелет
  const addr mine = MyBody();
  {
    addr curCh, curSkel;
    {
      Shared l;
      curCh = s_mine.ch, curSkel = s_mine.sk.skel;
    }
    addr skelNow = 0;
    if (mine) Safe([&] { const addr P7 = rp(mine + 0x3f0 + 0x24c); skelNow = P7 ? rp(P7) : 0; });
    if (mine != curCh || skelNow != curSkel) {
      static Skel sk;
      const bool ok = mine && ReadSkel(mine, sk);
      const Look look = mine ? ReadLook(mine) : Look{};
      {
        Excl l;
        s_mine.ch = mine;
        s_mine.avatar = s_myAvatar;
        if (ok) s_mine.sk = sk;
        else s_mine.sk.n = 0, s_mine.sk.skel = skelNow;
        memcpy(s_mine.fp, look.ident, sizeof s_mine.fp);
      }
      s_myChar = mine;
      if (mine)
        MpNote("puppet: my body %s %08X (%s, %d parts) — skeleton %d bones, %d by name%s, hips %d (%.2f m)", CharName(mine).c_str(),
               unsigned(mine), look.ident[0] ? look.ident : "?", look.parts, ok ? sk.n : 0, ok ? sk.found : 0,
               ok && !sk.fromNames ? " (game lookup)" : "", ok ? sk.hips : -1, double(ok ? sk.restHips : 0.f));
    }
  }
  // куклы: что известно о каждом игроке (под замком — копия), решения и перенос — без него
  struct Info { bool used, forget, stale, nobody; int avatar; addr body; Frame last; };
  static Info info[kSlots];
  {
    Shared l;
    for (int i = 0; i < kSlots; i++) {
      const Slot& s = s_slot[i];
      Info& in = info[i];
      in.used = s.used, in.forget = s.forget, in.avatar = s.avatar, in.body = s.body;
      in.stale = !s.count || now - s.lastRecv > kStaleMs;
      if (s.count) in.last = s.fr[s.head];
      in.nobody = s.count && (in.last.flags & F_NOBODY);
    }
  }
  const bool active = s_active;
  const std::vector<addr> act = ActiveChars();
  auto fits = [](int avatar, int kind) {
    return avatar == AV_ALIEN        ? kind == BK_ALIEN
         : avatar == AV_ANDROID     ? kind == BK_ANDROID
         : avatar == AV_FACEHUGGER  ? kind == BK_HUGGER
                                    : kind == BK_ANDROID || kind == BK_NPC || kind == BK_PLAYER || kind == BK_SAME;
  };
  for (int i = 0; i < kSlots; i++) {
    Slot& s = s_slot[i];
    const Info& in = info[i];
    const bool drop = !in.used || in.forget || in.stale || !active || (s.ghost && !s_ghost);
    if (in.body) {
      const addr b = in.body;
      const bool present = b == npc || b == player || std::find(act.begin(), act.end(), b) != act.end();
      const bool alive = present && (b == npc || DeathState(b) == 0);
      const char* why = drop                                              ? "player gone"
                        : in.nobody                                       ? "no body over there"
                        : b == mine                                       ? "it's my body now"
                        : !alive                                          ? "body gone or dead"
                        : !fits(in.avatar, s.kind)                        ? "avatar changed"
                        : in.avatar == AV_HUMAN && s.kind != BK_SAME && s.pref != s_humanBody ? "body choice changed"
                                                                          : nullptr;
      // изображает не он сам, а тот же персонаж в третьем лице появился (скрипт уровня) — пересесть в него
      if (!why && in.avatar == AV_HUMAN && s.kind != BK_SAME && in.last.ident[0] && now - s.sameCheckAt > 5000) {
        s.sameCheckAt = now;
        std::vector<addr> taken;
        for (int j = 0; j < kSlots; j++)
          if (s_slot[j].body) taken.push_back(s_slot[j].body);
        if (FindSame(in.last, mine, taken)) why = "the same character in third person is here", s.pickTryAt = 0;
      }
      if (why) Release(s, why);
    }
    if (drop) {
      if (in.used && (in.forget || (s.ghost && !s_ghost) || !active || now - s.lastRecv > kStaleMs * 4)) {
        Excl l;
        s.used = false, s.count = 0;
      }
    }
  }
  // новые тела: Чужой и андроиды — раньше людей (андроида, за которого играет другой, не отдаём под человека)
  std::vector<addr> taken;
  if (mine) taken.push_back(mine);
  for (int i = 0; i < kSlots; i++)
    if (s_slot[i].body) taken.push_back(s_slot[i].body);
  for (int pass = 0; pass < 3 && active; pass++)
    for (int i = 0; i < kSlots; i++) {
      Slot& s = s_slot[i];
      const Info& in = info[i];
      if (!in.used || in.forget || in.stale || in.nobody || s.body) continue;
      const bool creature = in.avatar == AV_ANDROID || in.avatar == AV_FACEHUGGER;
      if ((pass == 0) != (in.avatar == AV_ALIEN) || (pass == 1) != creature) continue;
      if (now - s.pickTryAt < 1000) continue;
      s.pickTryAt = now;
      int kind = BK_NONE;
      const addr b = PickBody(in.avatar, in.last, mine, taken, kind);
      if (!b) continue;
      static Skel sk;
      if (!ReadSkel(b, sk)) continue;
      taken.push_back(b);
      if (kind == BK_ALIEN && aiMode != 0) s.aiTaken = true, s.aiWas = aiMode, ActSetAi(0);
      if (kind == BK_PLAYER) s_ownsPlayer = true;
      {
        Excl l;
        s.pref = s_humanBody;
        s.same = kind == BK_SAME;
        s.attacking = false;
        s.body = b, s.kind = kind, s.sk = sk, s.worldLogged = false, s.moves = 0, s.ventTryAt = 0, s.reportAt = 0;
        s_animKey[i] = b + 0x3f0;
      }
      MpNote("puppet of %llu (%s %s): body %s %08X (%s), skeleton %d bones, %d by name, hips %d (%.2f m)", static_cast<unsigned long long>(s.who),
             MpAvatarName(in.avatar), in.last.ident[0] ? in.last.ident : "", CharName(b).c_str(), unsigned(b), KindName(kind), sk.n, sk.found,
             sk.hips, double(sk.restHips));
    }
  // удар Чужого другого игрока (он нажал кнопку): Аманда рядом и впереди — бьём её, иначе обычный удар мода
  for (int i = 0; i < kSlots; i++) {
    Slot& s = s_slot[i];
    if (s.kind != BK_ALIEN || s.body != npc || !npc) continue;
    ms_t req;
    {
      Shared l;
      req = s.attackReqAt;
    }
    if (req && !s.attacking && now - req < 800 && !alienBackstage) {
      bool amanda = false;
      float dist = -1;
      Safe([&] {
        if (!player || DeathState(player) != 0) return;
        const V3 a = rv3(npc + 0x70), p = rv3(player + 0x70);
        dist = std::hypot(p.x - a.x, p.z - a.z);
        const float ang = std::fabs(WrapPi(std::atan2(p.x - a.x, p.z - a.z) - rf(npc + 0x174)));
        amanda = std::fabs(p.y - a.y) < 2.f && (dist < 1.5f || (dist < 4.f && ang < 1.75f));
      });
      s_meleeAllowUntil = now + 2000;
      {
        Excl l;
        s.attackReqAt = 0;
        s.attacking = true;
        s.attackStart = now, s.attackUntil = now + 1500;
      }
      if (amanda) ActAttackTarget(player);
      else ActAttack();
      MpNote("puppet: %llu's Alien attacks — %s (Amanda %.1f m)", static_cast<unsigned long long>(s.who), amanda ? "Amanda" : "nearest target",
             double(dist));
    } else if (req && now - req >= 800) {
      Excl l;
      s.attackReqAt = 0;
    }
    if (s.attacking) {  // пока идёт удар (действие не «стоит/идёт») или Аманда умирает — кукла отпущена, не дольше 8 с
      uint8_t prim = 1;
      Safe([&] { if (const addr a = rp(npc + 0x36c)) prim = rd<uint8_t>(a + 0x1a); });
      const bool busy = prim != 1 && prim != 55 && prim != 56 && prim != 57 && prim != 59;
      if ((busy || (player && DeathState(player) != 0)) && now - s.attackStart < 8000) s.attackUntil = std::max(s.attackUntil, now + 300);
      if (now >= s.attackUntil) {
        Excl l;
        s.attacking = false;
      }
    }
  }
  // перенос физического тела
  V3 me{};
  const bool haveMe = mine && Safe([&] { me = rv3(mine + 0x70); });
  for (int i = 0; i < kSlots; i++) {
    Slot& s = s_slot[i];
    if (!s.body || s.attacking) continue;
    Moment m;
    bool ok;
    {
      Shared l;
      ok = Interp(s, now, m);
    }
    if (!ok) continue;
    const addr b = s.body;
    // Чужой-кукла за кулисами, а тот уже в мире — вывести его (ближайшая вентиляция); пока выходит — не переносим
    if (s.kind == BK_ALIEN && alienBackstage) {
      if (!(m.flags & F_HIDDEN) && now - s.ventTryAt > 6000) {
        s.ventTryAt = now;
        TpMove(b, m.pos[0], m.pos[1], m.pos[2]);
        ActVentExitHere();
        MpNote("puppet: the alien is backstage, the player's isn't — coming out through the nearest vent");
      }
      continue;
    }
    TpMove(b, m.pos[0], m.pos[1], m.pos[2]);
    Safe([&] { wf(b + 0x174, m.yaw); });
    if (haveMe) s.dist = std::sqrt((me.x - m.pos[0]) * (me.x - m.pos[0]) + (me.y - m.pos[1]) * (me.y - m.pos[1]) + (me.z - m.pos[2]) * (me.z - m.pos[2]));
    if (!s.worldLogged && ++s.moves >= 30) {  // проверка раскладки мира модели: перенос (+0x40) — у ног персонажа
      s.worldLogged = true;
      Safe([&] {
        const addr a = b + 0x3f0 + 0x10;
        const V3 p = rv3(b + 0x70);
        const float d = std::hypot(rf(a + 0x30) - p.x, rf(a + 0x38) - p.z);
        const float myaw = std::atan2(rf(a + 0x20), rf(a + 0x28));  // ось Z модели в мире
        MpNote("puppet: world of %s: translation %.2f m from +0x70, model Z yaw %.2f vs +0x174 %.2f", CharName(b).c_str(), double(d),
            double(myaw), double(rf(b + 0x174)));
      });
    }
    // раз в 10 с: сколько поз подменено (0 — игра не обновляла его модель или подмена не сработала) и где его тело на самом
    // деле (мир модели) относительно того, куда мы его переносим (далеко — тело держит скрипт уровня, переносом не сдвинуть)
    if (now - s.reportAt >= 10000) {
      const uint32_t ap = s_applied[i].load();
      if (s.reportAt) {
        float off = -1;
        Safe([&] {
          const addr a = b + 0x3f0 + 0x10;
          off = std::hypot(rf(a + 0x30) - m.pos[0], rf(a + 0x38) - m.pos[2]);
        });
        MpNote("puppet of %llu: %s %08X — %u poses in 10 s (%s), real body %.1f m from the player, %.1f m from me%s",
               static_cast<unsigned long long>(s.who), CharName(b).c_str(), unsigned(b), ap - s.appliedWas,
               s_poseOk[i].load() ? "bones applied" : "no bones", double(off), double(s.dist), (m.flags & F_HIDDEN) ? ", hidden" : "");
      }
      s.reportAt = now, s.appliedWas = ap;
    }
  }
}

}  // namespace

// ---------------- API ----------------
const char* PupHumanBodyName(int i) {
  switch (i) {
    case PB_AUTO: return TR("авто: андроид, NPC, своя Аманда", "auto: android, NPC, own Amanda");
    case PB_ANDROID: return TR("андроид", "an android");
    case PB_NPC: return TR("NPC-человек уровня", "a human NPC of the level");
    case PB_AMANDA_FP: return TR("своя Аманда (без головы)", "own Amanda (no head)");
    default: return "?";
  }
}
uint16_t PupLevelHash(const std::string& lvl) {
  uint32_t h = 2166136261u;
  for (char c : lvl) h = (h ^ uint8_t(c >= 'A' && c <= 'Z' ? c + 32 : c)) * 16777619u;
  return uint16_t(h ^ (h >> 16));
}
void PupSetActive(bool on, int avatar) {
  s_active = on;
  s_myAvatar = avatar;
}
void PupSetHumanBody(int m) { s_humanBody = std::clamp(m, 0, PB_COUNT - 1); }
int PupHumanBody() { return s_humanBody; }
void PupGhost(bool on) {
  s_ghost = on;
  Log("puppet: mirror %s", on ? "on" : "off");
}
bool PupGhostOn() { return s_ghost; }
bool PupOwnsPlayer() { return s_ownsPlayer; }
bool PupIsBody(addr ch) {
  if (!ch) return false;
  const addr a = ch + 0x3f0;
  for (int i = 0; i < kSlots; i++)
    if (s_animKey[i].load(std::memory_order_relaxed) == a) return true;
  return false;
}
void PupRemoteAttack(uint64_t who) {
  Excl l;
  const int i = SlotOf(who, false);
  if (i >= 0 && s_slot[i].body && s_slot[i].kind == BK_ALIEN) s_slot[i].attackReqAt = NowMs();
}
void PupSetMyIdentity(const char* id, bool weak) {
  Excl l;
  std::snprintf(s_identHint, sizeof s_identHint, "%s", id ? id : "");
  s_identWeak = weak;
}
void PupForget(uint64_t who) {
  Excl l;
  const int i = SlotOf(who, false);
  if (i >= 0) s_slot[i].forget = true;
}
void PupClear(const char* why) {
  Excl l;
  int n = 0;
  for (Slot& s : s_slot)
    if (s.used) s.forget = true, n++;
  if (n) Log("puppet: all %d forgotten (%s)", n, why);
}

bool PupEncodeLocal(int avatar, uint64_t me, uint16_t level, std::vector<uint8_t>& out) {  // сетевой поток
  static Mine m;
  char ident[12];
  {
    Shared l;
    m = s_mine;
    const bool hint = s_identHint[0] && (!s_identWeak || !s_mine.fp[0]);
    memcpy(ident, hint ? s_identHint : s_mine.fp, sizeof ident);
  }
  WState h{};
  if (avatar == AV_HUMAN) memcpy(h.ident, ident, sizeof h.ident);
  h.ident[sizeof h.ident - 1] = 0;
  h.who = me, h.ts = uint32_t(NowMs()), h.level = level, h.avatar = uint8_t(avatar);
  h.wq[3] = 32767;
  uint8_t flags = 0;
  static Q q[kMaxB];
  static uint8_t has[kMaxB];
  int nb = 0;
  if (!m.ch) {
    flags |= F_NOBODY;
  } else {
    const addr ch = m.ch, anim = ch + 0x3f0;
    float a[16];
    bool dead = false;
    const bool ok = Safe([&] {
      const V3 p = rv3(ch + 0x70);
      h.pos[0] = p.x, h.pos[1] = p.y, h.pos[2] = p.z;
      h.yaw = rf(ch + 0x174);
      for (int k = 0; k < 16; k++) a[k] = rf(anim + 0x10 + addr(k) * 4);
      h.ent = rd<uint32_t>(ch + 0x54), h.inst = rd<uint32_t>(ch + 0x58);
      dead = rd<uint32_t>(ch + 0x2f90) >= 2;
    });
    if (!ok || !std::isfinite(h.pos[0])) {
      flags |= F_NOBODY;
    } else {
      const Q wq = QFromRowMatrix(a);
      h.wq[0] = int16_t(std::lround(wq.x * 32767)), h.wq[1] = int16_t(std::lround(wq.y * 32767));
      h.wq[2] = int16_t(std::lround(wq.z * 32767)), h.wq[3] = int16_t(std::lround(wq.w * 32767));
      h.wt[0] = a[12], h.wt[1] = a[13], h.wt[2] = a[14];
      if (dead) flags |= F_DEAD;
      if (avatar == AV_ALIEN && alienBackstage) flags |= F_HIDDEN;
      const bool index = avatar != AV_HUMAN;  // Чужой и андроид — та же сущность у всех: скелет тот же
      if (m.sk.n) {
        const uint32_t s0 = g_palEditSeq.load();
        float hips[3] = { 0, 0, 0 };
        bool got = false;
        if (!(s0 & 1)) got = Safe([&] {
            const addr P7 = rp(anim + 0x24c);
            if (!P7 || rp(P7) != m.sk.skel || rd<int>(P7 + 0x14) != m.sk.n) return;
            const addr arr = rp(P7 + 0x10);
            if (!arr) return;
            auto rq = [&](int i) {
              const addr b = arr + addr(i) * 0x30 + 0x10;
              return Q{ rf(b), rf(b + 4), rf(b + 8), rf(b + 12) };
            };
            nb = index ? m.sk.n : kNamed;
            for (int i = 0; i < nb; i++) {
              const int src = index ? i : m.sk.named[i];
              has[i] = src >= 0;
              if (src >= 0) q[i] = QNorm(rq(src));
            }
            if (m.sk.hips >= 0) {
              const addr b = arr + addr(m.sk.hips) * 0x30;
              hips[0] = rf(b), hips[1] = rf(b + 4), hips[2] = rf(b + 8);
            }
          });
        if (got && g_palEditSeq.load() == s0 && nb > 0) {
          flags |= F_POSE | (index ? F_INDEX : 0);
          if (m.sk.hips >= 0 && std::isfinite(hips[1])) flags |= F_HIPS, h.hips[0] = Mm(hips[0]), h.hips[1] = Mm(hips[1]), h.hips[2] = Mm(hips[2]);
          h.restHips = uint16_t(std::clamp(std::lround(m.sk.restHips * 1000.f), 0L, 65535L));
          h.skelHash = m.sk.hash;
        } else {
          nb = 0;
        }
      }
    }
  }
  h.flags = flags;
  h.nb = uint16_t(nb);
  out.assign(reinterpret_cast<const uint8_t*>(&h), reinterpret_cast<const uint8_t*>(&h) + sizeof h);
  if (flags & F_POSE) {
    const size_t maskAt = out.size(), maskN = (size_t(nb) + 7) / 8, idxN = (size_t(nb) + 3) / 4;
    out.resize(out.size() + maskN + idxN, 0);
    for (int i = 0; i < nb; i++) {
      if (!has[i]) continue;
      uint8_t ix;
      int16_t c[3];
      EncQ(q[i], ix, c);
      out[maskAt + size_t(i >> 3)] |= uint8_t(1u << (i & 7));
      out[maskAt + maskN + size_t(i >> 2)] |= uint8_t(ix << ((i & 3) * 2));
      const uint8_t* cb = reinterpret_cast<const uint8_t*>(c);
      out.insert(out.end(), cb, cb + 6);
    }
  }
  if (s_ghost && m.ch) OnState(out.data(), out.size(), me, level, true);
  return true;
}
void PupOnState(const uint8_t* body, size_t n, uint64_t me, uint16_t level) { OnState(body, n, me, level, false); }

std::vector<PupRow> PupSnapshot() {
  std::vector<PupRow> r;
  const ms_t now = NowMs();
  Shared l;
  for (int i = 0; i < kSlots; i++) {
    const Slot& s = s_slot[i];
    if (!s.used) continue;
    PupRow row;
    row.who = s.who, row.avatar = s.avatar, row.ghost = s.ghost;
    if (s.count) row.ident = s.fr[s.head].ident;
    row.same = s.kind == BK_SAME;
    row.attacking = s.attacking;
    if (s.body) row.body = std::string(KindName(s.kind)) + " «" + CharName(s.body) + "»";
    row.dist = s.dist;
    row.ageMs = s.count ? int(now - s.lastRecv) : -1;
    row.fps = s.fps;
    row.hidden = s.count && (s.fr[s.head].flags & F_HIDDEN);
    row.pose = s.body && s_poseOk[i].load();
    r.push_back(row);
  }
  return r;
}

void InstallPuppet() {
  using Interceptor::Attach;
  Attach(VA(kAnimWorldUpd), nullptr, OnAnimWorldLeave, false);  // горячий (все персонажи, рабочие потоки): без замка
  Attach(VA(va::LocoUpdate), nullptr, OnGameTick);               // поток игры, общий замок
  Attach(VA(kMeleeStart), OnMeleeStartEnter, nullptr, false);     // удар только по кнопке
}
