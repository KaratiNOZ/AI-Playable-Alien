// alien_extra.cpp — пока играешь за Чужого:
//   * Аманда прячется под картой (или над ней), а когда возвращаешься к ней — стоит там же, где стояла;
//   * держишь клавишу осмотра (Z) — Чужой встаёт и осматривается (вторичное действие игры SecondaryActLookAround);
//   * голос: клавиша звука (G) и автозвуки — рык, шипение, принюхивание (как узел ActionTriggerSound дерева поведения);
//   * интерфейс Рипли (патроны, фонарь, подсказки «взять») спрятан.
// Адреса и проверки по памяти — в game.h.
#include "game.h"
#include "interceptor.h"
#include "puppet.h"
#include "state.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// ============================ Аманда прячется ============================
// Держим её под домом (или под Чужим, если «Аманда следует» — вокруг неё грузятся зоны уровня) телепортом через
// Havok-тело каждые 16 мс: гравитация не успевает утащить. Выход из вентиляции Аманду больше не трогает (vent.cpp
// выбирает вентиляцию сам, а «у Аманды» — у её дома). Вернули домой — первую секунду следим, чтобы не провалилась
// сквозь пол, пока физика вокруг неё не проснулась.
static const float kHideDy = 25.f;
static V3 s_home{};
static float s_homeYaw = 0;
static ms_t s_settleUntil = 0;

bool AmandaHome(V3& home) {
  if (!amandaHidden) return false;
  home = s_home;
  return true;
}
static void AmandaGoHome(const char* why) {
  if (!player) return;
  TpMove(player, s_home.x, s_home.y + 0.05f, s_home.z);
  Safe([&] { wf(player + 0x174, s_homeYaw); });
  s_settleUntil = NowMs() + 3000;
  Log("amanda: back home %.1f %.1f %.1f (%s)", s_home.x, s_home.y, s_home.z, why);
}
static void AmandaSettle() {  // первая секунда дома: провалилась ниже пола — ставим обратно (ходить не мешает)
  if (!s_settleUntil || !player) return;
  if (NowMs() > s_settleUntil) {
    s_settleUntil = 0;
    return;
  }
  V3 p{};
  if (Safe([&] { p = rv3(player + 0x70); }) && p.y < s_home.y - 0.4f && std::hypot(p.x - s_home.x, p.z - s_home.z) < 1.5f) {
    TpMove(player, p.x, s_home.y + 0.05f, p.z);
    Log("amanda: caught falling through the floor (%.1f -> %.1f)", p.y, s_home.y);
  }
}
static float s_eyeH = -1.f;  // высота глаз Аманды над ногами (камера у неё): стоя ~1.6 м, пригнувшись ~1 м
static void AmandaHideTick() {  // 16 мс, под общим замком
  if (PupOwnsPlayer()) {  // своя Аманда изображает другого игрока (puppet.cpp) — её ведёт сеть, не прячем
    amandaHidden = false, amandaStay = false;
    return;
  }
  const bool want = amandaHide != 0 && camMode != 0 && player && CamTarget() && !g_amandaInput;  // и за лицехвата
  if (!want) {
    if (amandaHidden) {
      AmandaGoHome("playing as Amanda again");
      amandaHidden = false;
    }
    amandaStay = false;
    if (camMode == 0 && !photo.on && player) Safe([] {  // камера у Аманды — её follow-камера и есть её глаза
      const addr G = CamG(), cur = rp(G + 0x1d8);
      if (cur && cur == rp(G + 0x1dc)) s_eyeH = rv3(cur + 0x14).y - rv3(player + 0x70).y;
    });
    AmandaSettle();
    return;
  }
  s_settleUntil = 0;
  if (amandaStay) return;
  // пригнулась (вентиляция, шкаф, под столом): не прячем — телепорт туда и обратно ронял её сквозь пол (тестер)
  if (!amandaHidden && s_eyeH > 0.2f && s_eyeH < 1.3f) {
    amandaStay = true;
    Log("amanda: crouched (eyes %.2f m) — stays where she is (vent / hiding spot)", s_eyeH);
    return;
  }
  if (!amandaHidden) {
    bool ok = Safe([&] { s_home = rv3(player + 0x70), s_homeYaw = rf(player + 0x174); });
    if (!ok || !std::isfinite(s_home.x)) return;
    amandaHidden = true;
    Log("amanda: hidden %s the map (home %.1f %.1f %.1f)", amandaHide == 2 ? "above" : "under", s_home.x, s_home.y, s_home.z);
  }
  V3 base = s_home;
  if (amandaFollow) Safe([&] { base = rv3(CamTarget() + 0x70); });
  TpMove(player, base.x, base.y + (amandaHide == 2 ? kHideDy : -kHideDy), base.z);
}
void ActSetAmandaHide(int mode) {
  amandaHide = std::clamp(mode, 0, 2);
  Log("amanda hide mode %d (0 off, 1 under, 2 above)", amandaHide);
}

// ============================ осмотр ============================
// Контроллер вторичных действий npc+0x370: у слота 2 колбэк выбора (FUN_0078f5c0) сам решает, когда осмотреться
// (слой AT_SECONDARY_LOOK_AROUND — голова и корпус почти на 180°, ноги стоят). Сам игра так делает, только если у
// персонажа поднят флаг «можно осматриваться» (+0x2cb у персонажа из контекста колбэка +0x1640 — это сам Чужой).
// Пока держат клавишу: флаг поднимаем, колбэк снимаем и просим состояние 0x13 (проверено: слот сразу переходит в
// 0x13); отпустили — всё как было. Каждые 4.5 с перезапускаем осмотр (иначе после одного круга Чужой просто стоит).
//
// LOOK по камере (lookMode 1): тот же слот 2, но состояние 0x12 — AT_SECONDARY_AUTONOMIC_LOOK_AT (так смотрит ИИ: поза
// PERSISTENT_ACT_LOOK_AT_IDLE — голова и корпус до 180°, blend set LOOKAT по look_theta/look_phi). Объект состояния
// (vtable 0x012af4a0, создаёт FUN_00670a50): +0x30 — точка, куда смотреть (её копия — перенос матрицы lookAtTarget +0xa0).
// Каждый кадр ставим туда точку в 20 м по взгляду камеры (ViewDir). Номера типов — по таблице строк AT_SECONDARY_*.
static const int kLookSlot = 2;
static const uint16_t kLookAround = 0x13, kLookAt = 0x12;
static addr s_lookNpc = 0, s_lookCb = 0, s_lookFlag = 0;
static uint32_t s_lookWant = 1;
static uint8_t s_lookFlagWas = 0, s_lookAtFlagWas = 0;
static uint16_t s_lookType = kLookAround;
static ms_t s_lookDiagAt = 0;
static bool s_lookOn = false, s_lookDiag = false;
static ms_t s_lookAt = 0, s_lookStart = 0;
static int s_lookRestart = 0;

static std::atomic<addr> s_lookObjHook{ 0 }, s_lookNpcHook{ 0 };  // объект взгляда, которому хук синхронизации подставляет цель (0 — никому)
static void LookTargetRestore();  // ниже: цель взгляда персонажа — как была
static bool LookTargetWrite();    // ниже: цель взгляда персонажа — точка по взгляду камеры
static void LookRestore(const char* why) {
  if (s_lookOn && s_lookNpc && s_lookNpc == npc) {
    const addr c = npc + va::SecActOff;
    Safe([&] {
      wr<uint32_t>(c + 0x30 + kLookSlot * 4, s_lookWant);
      wr<addr>(c + 0x48 + kLookSlot * 4, s_lookCb);
      if (s_lookFlag) wr<uint8_t>(s_lookFlag, s_lookFlagWas);
      if (s_lookType == kLookAt) wr<uint8_t>(npc + 0x2c8, s_lookAtFlagWas);
    });
    LookTargetRestore();
  }
  s_lookFlag = 0;
  s_lookObjHook = 0;
  if (s_lookOn) Log("look around: off (%s)", why);
  s_lookOn = false;
  s_lookNpc = 0;
  alienLooking = false;
}
static void LookTick() {  // 30 мс, под общим замком
  const bool want = npc && !hugger && camMode != 0 && !photo.on && aiMode == 0 && !alienBackstage && !vent.stay && !VentBusy() &&
                    (HeldK(K_LOOK) || NowMs() < devLookUntil.load());
  if (!want) {
    if (s_lookOn) LookRestore("key released");
    return;
  }
  const addr c = npc + va::SecActOff;
  const ms_t t = NowMs();
  if (!s_lookOn || s_lookNpc != npc) {
    if (s_lookOn) LookRestore("alien changed");
    bool ok = Safe([&] {
      s_lookCb = rp(c + 0x48 + kLookSlot * 4);
      s_lookWant = rd<uint32_t>(c + 0x30 + kLookSlot * 4);
      s_lookFlag = 0;
      const addr ctx = rp(c + 0x60 + kLookSlot * 4);  // контекст колбэка: +0x1640 -> персонаж
      if (ctx && rp(ctx + 0x1640) == npc) {
        s_lookFlag = npc + 0x2cb;
        s_lookFlagWas = rd<uint8_t>(s_lookFlag);
        wr<uint8_t>(s_lookFlag, 1);
      }
      s_lookType = lookMode == 1 ? kLookAt : kLookAround;
      if (s_lookType == kLookAt) {  // «смотреть можно» (его же ставит разворот на месте, FUN_00675b10)
        s_lookAtFlagWas = rd<uint8_t>(npc + 0x2c8);
        wr<uint8_t>(npc + 0x2c8, 1);
        LookTargetWrite();  // цель — раньше, чем слот станет 0x12: иначе объект взгляда стартует без цели и сразу «доигран»
      }
      wr<addr>(c + 0x48 + kLookSlot * 4, 0);
      wr<uint32_t>(c + 0x30 + kLookSlot * 4, s_lookType);
    });
    if (!ok) return;
    s_lookOn = true;
    s_lookNpc = npc;
    s_lookAt = s_lookStart = t;
    s_lookRestart = 0;
    s_lookDiag = false;
    alienLooking = true;
    Log("look around: on, %s (flag %s)", s_lookType == kLookAt ? "LOOK by camera (0x12)" : "additive (0x13)", s_lookFlag ? "set" : "not found");
    return;
  }
  if (!s_lookDiag && t - s_lookStart > 600) {  // диагностика: встал ли слот в «осмотр»
    s_lookDiag = true;
    Safe([&] {
      const addr st = rp(c + kLookSlot * 8);
      Log("look around: slot state 0x%X (want 0x%X), flags %02X", st ? unsigned(rd<uint16_t>(st + 0x1e)) : 0u, unsigned(s_lookType),
          st ? unsigned(rd<uint8_t>(st + 8)) : 0u);
    });
  }
  if (s_lookType == kLookAt) return;  // взгляд по камере не перезапускаем — он держится, пока держат клавишу
  if (s_lookRestart == 1) {  // второй тик перезапуска: снова осмотр
    Safe([&] { wr<uint32_t>(c + 0x30 + kLookSlot * 4, 0x13); });
    s_lookRestart = 0;
  } else if (t - s_lookAt > 4500) {
    s_lookAt = t;
    Safe([&] { wr<uint32_t>(c + 0x30 + kLookSlot * 4, 1); });
    s_lookRestart = 1;
  }
}

// поток игры, каждый кадр (выход апдейта локомоции): LOOK по камере — точка взгляда.
// Объект взгляда (0x12) сам точку не хранит: каждый кадр он забирает цель у персонажа (FUN_0064cad0) — если байт
// персонаж+0x29cc не ноль, ставит себе бит +0x42 & 1 («есть цель»), точку +0x30 из персонаж+0x29c0 и флаги; иначе всё
// стирает (поэтому запись прямо в объект не держалась). С битом запускается его дерево анимации (FUN_0066e7e0) и точка
// уходит в матрицу цели +0xa0 (FUN_0064cba0). Так что пишем цель персонажу — дальше всё делает игра. Что было в этих
// полях — запоминаем и возвращаем, когда клавишу отпустили.
static uint8_t s_lookTgtSave[0x18];
static addr s_lookTgtOf = 0;
static void LookTargetRestore() {
  if (s_lookTgtOf && s_lookTgtOf == npc) Safe([] { std::memcpy(reinterpret_cast<void*>(npc + va::CharLookTarget), s_lookTgtSave, sizeof s_lookTgtSave); });
  s_lookTgtOf = 0;
}
static bool LookTargetWrite() {  // точка в 20 м по взгляду камеры -> цель взгляда персонажа (+0x29c0, «есть цель» +0x29cc)
  V3 from{}, dir{};
  if (!npc || !ViewDir(from, dir)) return false;
  if (s_lookTgtOf != npc) {
    std::memcpy(s_lookTgtSave, reinterpret_cast<const void*>(npc + va::CharLookTarget), sizeof s_lookTgtSave);
    s_lookTgtOf = npc;
  }
  const float D = 20.f;
  wv3(npc + va::CharLookTarget, from.x + dir.x * D, from.y + dir.y * D, from.z + dir.z * D);
  wr<uint8_t>(npc + va::CharLookValid, 1);
  return true;
}
typedef void(__fastcall* LookVFn)(addr obj);
// Игра каждый кадр сама сбрасывает у Чужого «есть цель» (персонаж+0x29cc = 0 — чем-то из его ИИ), и если её синхронизация
// объекта взгляда (FUN_0064cad0) успевала прочитать ноль, флаг объекта снимался и дерево анимации перезапускалось:
// плавное включение (0.5 с) не доходило до полной силы — голова поворачивалась едва-едва. Поэтому пишем цель персонажу
// прямо на входе этой синхронизации — для нашего объекта (хук без замка: только атомики и запись в персонажа).
static std::atomic<float> s_lookTx{ 0 }, s_lookTy{ 0 }, s_lookTz{ 0 };
static void OnLookSyncEnter(Invocation& inv) {
  const addr obj = s_lookObjHook.load(std::memory_order_relaxed);
  if (!obj || inv.ecx != obj) return;
  const addr ch = s_lookNpcHook.load();
  if (!ch) return;
  wv3(ch + va::CharLookTarget, s_lookTx.load(), s_lookTy.load(), s_lookTz.load());
  wr<uint8_t>(ch + va::CharLookValid, 1);
}
// углы позы взгляда (проверено снимками, 01.10.2026): look_phi (+0xb4) — поворот головы и корпуса в градусах, плюс —
// влево (против курса игры); look_theta (+0x174) — наклон, 0.5 — прямо, больше — вверх (проверено тестером: было
// наоборот — камера смотрела вниз, а Чужой вверх). theta игра Чужому не считает, а phi в фазе 3 (слот 10,
// FUN_0064cc30) каждый кадр переписывает своим +0x178 — поэтому ставим углы ещё раз на выходе из неё (OnLookPhaseLeave).
// Курс берём от мыши (ViewYaw: курс Аманды / свой взгляд из головы), а не от кадра камеры: за спиной кадр камеры сам
// немного зависит от позы Чужого — голова двигала камеру, камера голову, и у цели голова дрожала. Углы ведём своим
// быстрым сглаживанием (~70 мс) и не перекидываем через ±180: камера ушла за спину — голова остаётся на краю, а не
// мечется с одного плеча на другое.
static float s_lookPhi = 0.f, s_lookTheta = 0.5f;
static float s_lookPhiAnim = 0.f, s_lookThetaAnim = 0.5f;  // оценка, где голова сейчас (анимация догоняет параметр с ~0.15 с)
static std::atomic<float> s_lookOutPhi{ 0.f }, s_lookOutTheta{ 0.5f };  // что поставили — для OnLookPhaseLeave
static bool s_lookAngHave = false;
static float s_lookBody = 0.f;  // курс тела на момент нажатия: пока Чужой осматривается, он стоит, а поза взгляда сама
                                // слегка поворачивает корпус — считать от живого курса значит голова двигает тело, тело —
                                // голову, и у цели голова дрожит
static ms_t s_lookAngAt = 0;
static void LookAngles(addr st, const V3& dir) {
  const ms_t now0 = NowMs();
  if (!s_lookAngHave || now0 - s_lookAngAt > 500) s_lookBody = rf(npc + 0x174);
  float want = -WrapPi(ViewYaw() - s_lookBody) * 57.2958f;  // вправо от тела — минус (плюс у phi — влево)
  const float pitch = std::asin(std::clamp(dir.y, -1.f, 1.f));
  float wantTheta = std::clamp(0.5f + pitch / kPi, 0.15f, 0.85f);  // больше — выше (камера смотрит вниз — и он вниз)
  if (devLookTheta >= 0.f && NowMs() < devLookUntil.load()) want = devLookPhi, wantTheta = devLookTheta;  // отладка: цель из cmd.txt
  const ms_t now = NowMs();
  if (!s_lookAngHave || now - s_lookAngAt > 500) {
    s_lookPhi = rf(st + 0xb4), s_lookTheta = rf(st + 0x174);
    if (!std::isfinite(s_lookPhi)) s_lookPhi = 0.f;
    if (!std::isfinite(s_lookTheta)) s_lookTheta = 0.5f;
    s_lookPhiAnim = s_lookPhi, s_lookThetaAnim = s_lookTheta;
    s_lookAngHave = true;
  }
  float d = want - s_lookPhi;  // без перескока через ±180: продолжаем с той стороны, где голова уже есть
  while (d > 180.f) d -= 360.f;
  while (d < -180.f) d += 360.f;
  want = std::clamp(s_lookPhi + d, -165.f, 165.f);
  const float dt = std::clamp(float(now - s_lookAngAt) / 1000.f, 0.f, 0.1f);
  s_lookAngAt = now;
  const float k = 1.f - std::exp(-dt * 25.f);  // цель — почти без задержки (~40 мс), только от рывков мыши
  s_lookPhi += (want - s_lookPhi) * k;
  s_lookTheta += (wantTheta - s_lookTheta) * k;
  // опережение: анимация доводит голову к параметру с запаздыванием ~0.15 с (снимки кадра), поэтому, пока голова не
  // дошла, даём параметр с запасом — голова доходит примерно в 2.5 раза быстрее; у цели параметр равен цели
  const float G = 1.5f;  // было 2: игра перебивала phi, и без запаса голова не доходила; теперь перебивать не даём
  const float cPhi = std::clamp(s_lookPhiAnim + (s_lookPhi - s_lookPhiAnim) * G, -175.f, 175.f);
  const float cTheta = std::clamp(s_lookThetaAnim + (s_lookTheta - s_lookThetaAnim) * G, 0.05f, 0.95f);
  const float ka = 1.f - std::exp(-dt / 0.15f);
  s_lookPhiAnim += (cPhi - s_lookPhiAnim) * ka;
  s_lookThetaAnim += (cTheta - s_lookThetaAnim) * ka;
  wf(st + 0xb4, cPhi);
  wf(st + 0x174, cTheta);
  s_lookOutPhi = cPhi, s_lookOutTheta = cTheta;
}
// фаза 3 объекта взгляда (FUN_0064cc30): игра только что скопировала свой угол +0x178 в look_phi — возвращаем свои углы
// (и в +0x178 тоже, чтобы её следующая копия совпала). Горячая (все объекты взгляда всех NPC): без замка, атомики.
static void OnLookPhaseLeave(Invocation& inv) {
  const addr obj = s_lookObjHook.load(std::memory_order_relaxed);
  if (!obj || inv.ecx != obj || inv.arg[0] != 3) return;
  const float phi = s_lookOutPhi.load(), th = s_lookOutTheta.load();
  wf(obj + 0xb4, phi), wf(obj + 0x178, phi), wf(obj + 0x174, th);
}
static void LookAtFrame() {
  if (!s_lookOn || s_lookType != kLookAt || !npc || s_lookNpc != npc) {
    s_lookObjHook = 0;
    return;
  }
  const uint8_t validWas = rd<uint8_t>(npc + va::CharLookValid);  // что оставила игра с прошлого кадра (диагностика)
  V3 from{}, dir{};
  if (!ViewDir(from, dir)) return;
  const float D = 20.f;
  s_lookTx = from.x + dir.x * D, s_lookTy = from.y + dir.y * D, s_lookTz = from.z + dir.z * D;
  if (!LookTargetWrite()) return;
  const addr st = rp(npc + va::SecActOff + kLookSlot * 8);
  const bool ours = st && rd<uint16_t>(st + 0x1e) == kLookAt && rp(st) == VA(va::VT_LOOK_AT);
  s_lookNpcHook = npc;
  s_lookObjHook = ours ? st : 0;
  if (ours) {
    const addr vt = rp(st);
    reinterpret_cast<LookVFn>(rp(vt + 3 * 4))(st);                     // забрать цель (бит «есть цель», точка)
    if (!rp(st + 0x14)) reinterpret_cast<LookVFn>(rp(vt + 8 * 4))(st);  // дерева ещё нет — обновить (создаст его)
    LookAngles(st, dir);
  }
  const ms_t now = NowMs();
  if (now - s_lookDiagAt > 1000) {  // диагностика: объект взгляда слота 2
    s_lookDiagAt = now;
    if (ours) {
      const V3 a = rv3(st + 0x30), m = rv3(st + 0xa0);
      Log("look-at: obj %08X tgt %.1f %.1f %.1f  mtx %.1f %.1f %.1f  theta %.3f phi %.1f  tree %08X fl %02X/%02X  valid(before) %u",
          unsigned(st), a.x, a.y, a.z, m.x, m.y, m.z, rf(st + 0x174), rf(st + 0xb4), unsigned(rp(st + 0x14)),
          unsigned(rd<uint8_t>(st + 8)), unsigned(rd<uint8_t>(st + 0x42)), unsigned(validWas));
    } else {
      Log("look-at: slot 2 is 0x%X (vt %08X), not the look-at object", st ? unsigned(rd<uint16_t>(st + 0x1e)) : 0u,
          st ? unsigned(rp(st)) : 0u);
    }
  }
}

// ============================ голос ============================
// Типы — SoundType узла ActionTriggerSound (FUN_006c6c60): 0 огонь рядом, 1 атака, 3 бросок, 4 обожжён, 6 ранен,
// 7 ищет, 8 потерял след, 9 выслеживает, 10 начинает поиск, 11 заподозрил. Три группы (как в меню):
//   поиск (ищет, начинает поиск, потерял след) — обычные, их играет «случайный» и автозвук;
//   «заметил» (заподозрил, выслеживает) и «бросок» — сами по себе только когда цель Чужого — человек рядом (не андроид,
//   не Аманда): иначе выходит, будто Чужой учуял игрока (тестер);
//   громкие (атака, боль, огонь) — только если выбрать их самому.
struct Voice { int type; const char* ru; const char* en; };
static const Voice kVoices[VOICE_COUNT] = {
  { 7, "Ищет", "Searching" },                      { 10, "Начинает поиск", "Starts searching" },
  { 8, "Потерял след", "Lost the trail" },         { 11, "Заподозрил", "Suspects something" },
  { 9, "Выслеживает", "Stalking" },                { 3, "Рык — бросок", "Roar — charge" },
  { 1, "Атака", "Attack" },                        { 6, "Ранен", "Hurt" },
  { 4, "Обожжён", "Burned" },                      { 0, "Огонь рядом", "Fire nearby" },
};
int VoiceType(int i) { return i >= 0 && i < VOICE_COUNT ? kVoices[i].type : 3; }
const char* VoiceName(int i) { return i >= 0 && i < VOICE_COUNT ? TR(kVoices[i].ru, kVoices[i].en) : "?"; }

static std::atomic<int> s_voiceReq{ -1 };  // тип, который сыграть в ближайшем кадре потока игры
static ms_t s_voiceAt = 0, s_autoNext = 0;
static int s_lastType = -1;
void ActAlienVoice(int type) {
  if (!npc || type < 0) return;
  s_voiceReq = type;
}
static int Pick(std::initializer_list<int> types) {  // случайный, но не тот же, что только что
  std::vector<int> v(types);
  if (v.size() > 1) v.erase(std::remove(v.begin(), v.end(), s_lastType), v.end());
  return v[size_t(std::rand()) % v.size()];
}
// цель Чужого — живой человек (не андроид, не лицехват, не Аманда) не дальше 25 м: только тогда «заметил» и «бросок»
static bool HumanNoticed(float* distOut) {
  if (!npc) return false;
  bool r = false;
  Safe([&] {
    const addr T = TargetOf(npc);
    if (!T || T == npc || T == player || !rp(T + 0x64)) return;
    const uint32_t type = rd<uint32_t>(rp(T + 0x64) + 4);
    if (type < 3 || type == 5 || type == 7 || DeathState(T) != 0) return;
    const V3 a = rv3(npc + 0x70), b = rv3(T + 0x70);
    const float d = std::hypot(a.x - b.x, a.z - b.z);
    if (d > 25.f) return;
    if (distOut) *distOut = d;
    r = true;
  });
  return r;
}
// «случайный» звук по обстановке: человек рядом — «заметил» (вплотную — бросок), иначе — поиск
static int PickByContext() {
  float d = 99.f;
  if (HumanNoticed(&d)) return d < 8.f ? Pick({ 9, 3 }) : Pick({ 11, 9 });
  if (alienLooking) return Pick({ 7, 10 });
  if (lastInput.any) return Pick({ 7, 8 });
  return Pick({ 7, 10, 8 });
}
static void VoiceTick() {  // 50 мс, под общим замком
  static ms_t photoKeyAt = 0;  // фоторежим: Чужой в кадре (стрелки у него) — G, как обычно
  if (photo.on && npc && PhotoActorNow() == PA_ALIEN && HotK(K_SOUND) && NowMs() - photoKeyAt > 350) {
    photoKeyAt = NowMs();
    ActAlienVoice(Down(VK_SHIFT) || alienVoiceKey < 0 ? PickByContext() : VoiceType(alienVoiceKey));
  }
  if (!npc || camMode == 0 || photo.on || hugger) {
    s_autoNext = 0;
    return;
  }
  const ms_t t = NowMs();
  static ms_t keyAt = 0;
  if (AlienKeyK(K_SOUND) && t - keyAt > 350) {
    keyAt = t;
    const int type = Down(VK_SHIFT) || alienVoiceKey < 0 ? PickByContext() : VoiceType(alienVoiceKey);
    ActAlienVoice(type);
    s_autoNext = t + ms_t(alienVoiceEvery * 1000);  // сам не перебивает только что нажатый
  }
  if (!alienAutoVoice || alienBackstage || vent.stay) {
    s_autoNext = 0;
    return;
  }
  if (!s_autoNext) s_autoNext = t + ms_t(alienVoiceEvery * 500);
  if (t < s_autoNext) return;
  const float k = 0.5f + float(std::rand() % 1000) / 1000.f;  // 0.5..1.5 от интервала
  s_autoNext = t + ms_t(alienVoiceEvery * 1000 * k);
  if (attack.until > t) return;  // в атаке кричит сама игра — свои «атака/бросок» сюда не подмешиваем
  const int type = PickByContext();
  ActAlienVoice(type);
}
// поток игры: апдейт локомоции персонажей (каждый кадр) — тут безопасно ставить звук в очередь персонажа
static void OnVoiceGameTick(Invocation&) {
  if (s_lookOn) Safe([] { LookAtFrame(); });
  const int type = s_voiceReq.exchange(-1);
  if (type < 0 || !npc) return;
  if (!Safe([&] { Fn<AlienVocalFn>(va::AlienVocal)(npc + 0x11a0, type); })) {
    Log("voice: call failed");
    return;
  }
  s_lastType = type;
  s_voiceAt = NowMs();
  Log("voice: type %d", type);
}

// ============================ интерфейс игры ============================
// Оверлеи Scaleform: weaponstuff (патроны, фонарь, прицел) и pickupOverlay (подсказки «взять») умеют
// force_hud_hidden / force_hud_visible (так игра прячет их сама — колесо инвентаря, катсцены). Зовём из потока игры,
// после апдейта оверлеев. Если игра вернула их сама (+0x40 сбросился) — прячем снова.
static bool s_hudHidden = false;
static ms_t s_hudAt = 0;
static addr FindOverlay(const char* name) {
  addr mgr = rp(VA(va::OverlayMgrPtr));
  if (!mgr) return 0;
  const addr arr = rp(mgr + 0x38);
  const uint32_t n = rd<uint32_t>(mgr + 0x3c);
  if (!arr || n > 256) return 0;
  const size_t len = std::strlen(name);
  for (uint32_t i = 0; i < n; i++) {
    const addr o = rp(arr + i * 4), s = o ? rp(o + 0x34) : 0;
    if (!s) continue;
    bool eq = true;
    for (size_t k = 0; k <= len && eq; k++) eq = rd<char>(s + k) == name[k];
    if (eq) return o;
  }
  return 0;
}
static void Invoke(addr o, const char* fn) {
  if (o && rp(o + 8) && rp(o + 0x68)) Fn<OverlayInvokeFn>(va::OverlayInvoke)(o, fn);
}
static void HudShow() {
  Safe([&] {
    for (const char* nm : { "weaponstuff", "pickupOverlay" }) {
      const addr o = FindOverlay(nm);
      if (!o) continue;
      Invoke(o, "force_hud_visible");
      wr<uint8_t>(o + 0x40, 0);
    }
  });
  s_hudHidden = false;
}
static void OnOverlayUpdateLeave(Invocation&) {
  const bool want = (hideGameHudAsAlien && camMode != 0 && !g_amandaInput) || (photo.on && photoPost.hideGameHud);
  const ms_t t = NowMs();
  if (want) {
    if (s_hudHidden && t - s_hudAt < 250) return;
    s_hudAt = t;
    Safe([&] {
      for (const char* nm : { "weaponstuff", "pickupOverlay" }) {
        const addr o = FindOverlay(nm);
        if (!o || (s_hudHidden && rd<uint8_t>(o + 0x40))) continue;  // уже спрятан
        Invoke(o, "force_hud_hidden");
        if (nm[0] == 'p') Invoke(o, "Set_Interaction_Prompt_Invisible_no_anim");
        wr<uint8_t>(o + 0x40, 1);
      }
    });
    if (!s_hudHidden) Log("game HUD: hidden");
    s_hudHidden = true;
  } else if (s_hudHidden) {
    HudShow();
    Log("game HUD: back");
  }
}

// ============================ сброс / установка ============================
void AlienExtraReset(bool restoreAmanda) {
  if (amandaHidden && restoreAmanda) AmandaGoHome("alien reset");
  amandaHidden = false;
  if (!restoreAmanda) s_settleUntil = 0;
  if (s_lookOn && restoreAmanda) LookRestore("alien reset");
  s_lookOn = false;
  s_lookNpc = 0;
  alienLooking = false;
  s_voiceReq = -1;
  s_autoNext = 0;
  s_hudHidden = false;  // оверлеи уровня пересоздаются
}

// HUD игры возвращает сам хук апдейта оверлеев (поток игры), как только камера у Аманды: reload.cpp ждёт этого до
// выключения хуков. Здесь — Аманда домой и осмотр (это можно из любого потока, как в тиках).
bool GameHudHidden() { return s_hudHidden; }
void AlienExtraShutdown() {
  if (amandaHidden) AmandaGoHome("mod unloaded");
  amandaHidden = false;
  if (s_lookOn) LookRestore("mod unloaded");
  if (s_hudHidden) Log("game HUD: still hidden (the game didn't update overlays in time)");
}

// ============================ слюни и дым изо рта ============================
// Шаблон Чужого Archetypes\NPCs\Alien\Xenomorph_NPC (CATHODE, dev\cathodedump): Thinker «Alien_Dribble_and_Breath» раз в
// секунду сравнивает PositionDistance(сам Чужой, VariableThePlayer) с 6 м — ближе: CharacterAttachmentNode_Breath/_Dribble
// прикрепляют к пасти Alien_Breath (дым) и Alien_Dribble (слюни), дальше — снимают. Игрок — это Аманда, поэтому эффекты
// видны, только когда она рядом. Пока камера на Чужом, подменяем у этой пары расстояние: RHS (игрок) ставим в 5.5 м от
// LHS (Чужой). Ловим геттер матрицы RHS только при вызове из PositionDistance; LHS уже лежит в кадре вызывающего.
static addr s_pdRet = 0;
static bool s_pdLogged = false;
static void OnPdRhsEnter(Invocation& inv) {
  if (inv.ra != s_pdRet) return;
  // FUN_005b9d10 сохранила перенос LHS в [esp+8..0x10] своего кадра — после нашего RET 8 это args[4..6]
  std::memcpy(inv.text, &inv.args[4], 12);
  inv.text[12] = 1;
}
static bool Near(const V3& a, const V3& b, float r) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz < r * r;
}
static void OnPdRhsLeave(Invocation& inv) {
  if (inv.text[12] != 1 || camMode == 0 || hugger) return;
  // из головы (голова спрятана) — наоборот, снять: они висят прямо у камеры (тестер: «слюни видно»)
  const bool hide = camMode == 1 && fpHideHead && !photo.on;
  if (!hide && !alienBreath) return;
  const addr al = npc, pl = player, m = inv.retval();
  if (!al || !pl || !m) return;
  V3 a, b{ rf(m + 0xc), rf(m + 0x1c), rf(m + 0x2c) };
  std::memcpy(&a, inv.text, 12);
  const V3 pa = rv3(al + 0x70), pp = rv3(pl + 0x70);
  if (!(Near(a, pa, 2.f) && Near(b, pp, 2.f)) && !(Near(a, pp, 2.f) && Near(b, pa, 2.f))) return;
  const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float kFake = hide ? 7.f : 5.5f;  // порог игры 6 м; не 0 — у других скриптов уровня свои пороги «совсем рядом»
  if (hide ? !(d < kFake) : !(d > kFake)) return;
  if (d < 0.01f) return;
  const float k = kFake / d;
  wf(m + 0xc, a.x + dx * k), wf(m + 0x1c, a.y + dy * k), wf(m + 0x2c, a.z + dz * k);
  static int s_pdLastMode = -1;
  if (!s_pdLogged || s_pdLastMode != int(hide)) {
    s_pdLogged = true, s_pdLastMode = int(hide);
    Log("breath: alien-player distance %.1f m -> %.1f m (drool and breath %s)", d, kFake, hide ? "off: head view" : "stay on");
  }
}

void InstallAlienExtra() {
  using Interceptor::Attach;
  s_pdRet = VA(va::PosDistRhsRet);
  Attach(VA(va::PosDistRhs), OnPdRhsEnter, OnPdRhsLeave, false);  // горячий геттер: без замка, только чтение глобалов
  Attach(VA(va::LocoUpdate), nullptr, OnVoiceGameTick);
  Attach(VA(va::LookSyncFromChar), OnLookSyncEnter, nullptr, false);  // горячий (все объекты взгляда): без замка
  Attach(VA(va::LookPhase), nullptr, OnLookPhaseLeave, false);           // тоже горячий
  Attach(VA(va::OverlayUpdate), nullptr, OnOverlayUpdateLeave);
  Every(16, "amanda-hide", AmandaHideTick);
  Every(30, "look", LookTick);
  Every(50, "voice", VoiceTick);
}
