// booster_ui — реализация. Всё рисуется через ImDrawList; от ImGui берём раскладку, ID и попадание мыши.
#include "booster_ui.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"  // ImHashStr, GetActiveID — только для id/фокуса

namespace BUI {

// ============================ палитра ============================
static ImU32 Hex(unsigned rgb, float a = 1.f) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, int(a * 255.f + 0.5f));
}

// Палитра Alien Playable: терминал Seegson / Weyland-Yutani — почти чёрный с зеленцой, фосфорно-зелёный акцент (как ЭЛТ
// в игре), янтарь — предупреждения, красный — опасное. Разметка и повадки — из booster_ui (меню друга, ZDS-Booster)
static Palette MakePalette() {
  Palette p;
  p.bg = Hex(0x090E0B);
  p.surface = Hex(0x101813);
  p.surfaceVar = Hex(0x18241D);
  p.surfaceHi = Hex(0x213228);
  p.primary = Hex(0x5FDC8A);
  p.primaryDark = Hex(0x379E5C);
  p.accent = Hex(0x5FDC8A);
  p.success = Hex(0x6BE895);
  p.warning = Hex(0xFFB547);
  p.danger = Hex(0xFF5A48);
  p.text = Hex(0xD6F0DE);
  p.textDim = Hex(0x85A792);
  p.textFaint = Hex(0x4F685A);
  p.onPrimary = Hex(0x05100A);
  p.border = Hex(0x1D3326);
  p.borderLight = Hex(0x2B4B38);
  p.shadow = Hex(0x000000);
  return p;
}

const Palette& P() {
  static const Palette pal = MakePalette();
  return pal;
}

// ============================ размеры ============================
namespace M {
constexpr float kRow = 34.f;        // высота обычной строки
constexpr float kSliderRow = 48.f;  // строка слайдера (подпись сверху)
constexpr float kPad = 14.f;        // внутренний отступ карточки/секции
constexpr float kGap = 4.f;         // между строками
constexpr float kRadius = 6.f;      // скругление панелей (терминал — почти прямые углы)
constexpr float kRadiusSm = 4.f;    // скругление строк/кнопок
constexpr float kWinRadius = 8.f;
constexpr float kHeader = 56.f;         // хедер окна
constexpr float kSectionHeader = 40.f;  // заголовок секции
constexpr float kTrackW = 44.f;         // таблетка тумблера
constexpr float kTrackH = 26.f;
constexpr float kThumb = 10.f;
constexpr float kSliderThumb = 9.f;
constexpr float kTextSm = 0.86f;  // множители к размеру шрифта
constexpr float kTextXs = 0.78f;
constexpr float kTextLg = 1.18f;
}  // namespace M

// ============================ состояние ============================
static ImGuiStorage g_store;       // анимации и «открыто/закрыто» по id
static float g_alpha[24] = { 1.f };
static int g_alphaTop = 0;
static int g_disabled = 0;
static ImFont* g_bodyFont = nullptr;
static ImFont* g_titleFont = nullptr;
static int g_expandAllFrame = -1;
static bool g_expandAllValue = false;
static char g_filter[64] = "";
static int g_frame = -1;

// стек открытых секций/карточек — чтобы EndSection знал, что мерить
struct SecFrame {
  ImGuiID id;
  bool child;   // открывали ли BeginChild (t > 0)
  bool alpha;   // пушили ли альфу
  float top;    // CursorPosY внутри child на старте
};
static SecFrame g_sec[16];
static int g_secTop = 0;

struct CardFrame {
  ImVec2 p0;
  float width;
  ImDrawListSplitter* split;
};
static CardFrame g_card[8];
static int g_cardTop = 0;
static ImDrawListSplitter g_cardSplit[8];

void SetFonts(ImFont* body, ImFont* title) {
  g_bodyFont = body;
  g_titleFont = title;
}
ImFont* TitleFont() { return g_titleFont; }

void NewFrame() {
  if (g_frame == ImGui::GetFrameCount()) return;
  g_frame = ImGui::GetFrameCount();
  g_alphaTop = 0;
  g_alpha[0] = 1.f;
  g_disabled = 0;
  g_secTop = 0;
  g_cardTop = 0;
}

// ============================ альфа / блокировка ============================
void PushAlpha(float a) {
  const float cur = g_alpha[g_alphaTop];
  if (g_alphaTop < 23) g_alphaTop++;
  g_alpha[g_alphaTop] = cur * ImClamp(a, 0.f, 1.f);
}
void PopAlpha() {
  if (g_alphaTop > 0) g_alphaTop--;
}
float CurAlpha() { return g_alpha[g_alphaTop]; }

void PushDisabled(bool disabled) {
  if (disabled) g_disabled++;
}
void PopDisabled() {
  if (g_disabled > 0) g_disabled--;
}
bool IsDisabled() { return g_disabled > 0; }

// альфа с учётом стека и блокировки
static ImU32 A(ImU32 c) {
  float mul = CurAlpha() * (g_disabled ? 0.38f : 1.f);
  return Fade(c, mul);
}

ImU32 Fade(ImU32 c, float mul) {
  const int a = int(((c >> IM_COL32_A_SHIFT) & 0xFF) * ImClamp(mul, 0.f, 1.f) + 0.5f);
  return (c & ~ImU32(0xFFu << IM_COL32_A_SHIFT)) | (ImU32(a & 0xFF) << IM_COL32_A_SHIFT);
}

ImU32 LerpCol(ImU32 a, ImU32 b, float t) {
  t = ImClamp(t, 0.f, 1.f);
  const int ar = (a >> IM_COL32_R_SHIFT) & 0xFF, ag = (a >> IM_COL32_G_SHIFT) & 0xFF;
  const int ab = (a >> IM_COL32_B_SHIFT) & 0xFF, aa = (a >> IM_COL32_A_SHIFT) & 0xFF;
  const int br = (b >> IM_COL32_R_SHIFT) & 0xFF, bg = (b >> IM_COL32_G_SHIFT) & 0xFF;
  const int bb = (b >> IM_COL32_B_SHIFT) & 0xFF, ba = (b >> IM_COL32_A_SHIFT) & 0xFF;
  return IM_COL32(int(ar + (br - ar) * t), int(ag + (bg - ag) * t), int(ab + (bb - ab) * t), int(aa + (ba - aa) * t));
}

// ============================ анимация ============================
float Anim(ImGuiID id, float target, float speed) {
  float* v = g_store.GetFloatRef(id, target);
  const float dt = ImMin(ImGui::GetIO().DeltaTime, 0.1f);  // как в booster: клампим кадр
  const float k = ImClamp(speed * dt, 0.f, 1.f);
  if (ImFabs(target - *v) < 0.0008f)
    *v = target;
  else
    *v += (target - *v) * k;
  return *v;
}
float AnimFor(const char* id, float target, float speed) { return Anim(ImGui::GetID(id), target, speed); }

float EaseOutCubic(float t) {
  t = ImClamp(t, 0.f, 1.f) - 1.f;
  return 1.f + t * t * t;
}
float EaseOutBack(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.f;
  t = ImClamp(t, 0.f, 1.f) - 1.f;
  return 1.f + c3 * t * t * t + c1 * t * t;
}
float EaseInOutQuart(float t) {
  t = ImClamp(t, 0.f, 1.f);
  if (t < 0.5f) return 8.f * t * t * t * t;
  const float u = -2.f * t + 2.f;
  return 1.f - u * u * u * u * 0.5f;
}
float EaseOutElastic(float t) {
  t = ImClamp(t, 0.f, 1.f);
  if (t <= 0.f) return 0.f;
  if (t >= 1.f) return 1.f;
  const float c4 = (2.f * IM_PI) / 3.f;
  return powf(2.f, -10.f * t) * sinf((t * 10.f - 0.75f) * c4) + 1.f;
}

// ============================ текст ============================
static const char* Vis(const char* label) {  // то, что показываем: до "##"
  return label;
}
static const char* VisEnd(const char* label) {
  const char* h = strstr(label, "##");
  return h ? h : label + strlen(label);
}

static ImVec2 TSize(const char* t, const char* tEnd, float sz, ImFont* font = nullptr) {
  ImFont* f = font ? font : ImGui::GetFont();
  return f->CalcTextSizeA(sz, FLT_MAX, 0.f, t, tEnd);
}
static ImVec2 TSizeL(const char* label, float mul, ImFont* font = nullptr) {
  ImFont* f = font ? font : ImGui::GetFont();
  return TSize(Vis(label), VisEnd(label), ImGui::GetFontSize() * mul, f);
}

// текст с еле заметной тенью — как DrawModernText в booster
static void TDraw(ImDrawList* dl, ImVec2 p, ImU32 col, const char* t, const char* tEnd, float mul = M::kTextSm,
                  ImFont* font = nullptr) {
  ImFont* f = font ? font : ImGui::GetFont();
  const float sz = ImGui::GetFontSize() * mul;
  dl->AddText(f, sz, ImVec2(p.x, p.y + 1.f), A(Fade(P().shadow, 0.55f)), t, tEnd);
  dl->AddText(f, sz, p, col, t, tEnd);
}
static void TDrawL(ImDrawList* dl, ImVec2 p, ImU32 col, const char* label, float mul = M::kTextSm, ImFont* font = nullptr) {
  TDraw(dl, p, col, Vis(label), VisEnd(label), mul, font);
}

// ============================ мягкая тень (DrawShadow из booster) ============================
static void SoftShadow(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, float intensity) {
  for (int i = 1; i <= 6; i++) {
    const float spread = i * 3.f * intensity;
    const float a = (0.34f * intensity) / (3.f + i * 2.2f);
    if (a <= 0.002f) continue;
    dl->AddRectFilled(ImVec2(p0.x - spread * 0.5f, p0.y - spread * 0.25f + spread * 0.55f),
                      ImVec2(p1.x + spread * 0.5f, p1.y + spread * 0.85f), A(Fade(P().shadow, a)), rounding + spread * 0.4f);
  }
}

// таблетка (две полуокружности + прямоугольник) — через скругление на всю высоту
static void Pill(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
  dl->AddRectFilled(p0, p1, col, (p1.y - p0.y) * 0.5f);
}

// ============================ иконки ============================
void DrawIcon(ImDrawList* dl, ImVec2 c, float size, Icon ico, ImU32 col) {
  const float r = size * 0.5f, th = ImMax(1.4f, size * 0.11f);
  switch (ico) {
    case ICO_GAMEPAD:
      dl->AddRect(ImVec2(c.x - r, c.y - r * 0.62f), ImVec2(c.x + r, c.y + r * 0.62f), col, r * 0.5f, 0, th);
      dl->AddLine(ImVec2(c.x - r * 0.55f, c.y), ImVec2(c.x - r * 0.15f, c.y), col, th);
      dl->AddLine(ImVec2(c.x - r * 0.35f, c.y - r * 0.2f), ImVec2(c.x - r * 0.35f, c.y + r * 0.2f), col, th);
      dl->AddCircleFilled(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), th * 0.9f, col, 8);
      dl->AddCircleFilled(ImVec2(c.x + r * 0.6f, c.y + r * 0.16f), th * 0.9f, col, 8);
      break;
    case ICO_CAMERA:
      dl->AddRect(ImVec2(c.x - r, c.y - r * 0.68f), ImVec2(c.x + r, c.y + r * 0.72f), col, r * 0.28f, 0, th);
      dl->AddCircle(ImVec2(c.x, c.y + r * 0.04f), r * 0.38f, col, 16, th);
      dl->AddLine(ImVec2(c.x - r * 0.34f, c.y - r * 0.68f), ImVec2(c.x - r * 0.1f, c.y - r), col, th);
      dl->AddLine(ImVec2(c.x - r * 0.1f, c.y - r), ImVec2(c.x + r * 0.28f, c.y - r), col, th);
      break;
    case ICO_APERTURE: {
      dl->AddCircle(c, r * 0.92f, col, 20, th);
      for (int i = 0; i < 6; i++) {
        const float a0 = i * (IM_PI / 3.f) + 0.35f;
        dl->AddLine(ImVec2(c.x + cosf(a0) * r * 0.9f, c.y + sinf(a0) * r * 0.9f),
                    ImVec2(c.x + cosf(a0 + 1.05f) * r * 0.32f, c.y + sinf(a0 + 1.05f) * r * 0.32f), col, th * 0.8f);
      }
      break;
    }
    case ICO_LAYERS:
      for (int i = 0; i < 3; i++) {
        const float y = c.y - r * 0.55f + i * r * 0.55f;
        dl->AddQuad(ImVec2(c.x, y - r * 0.34f), ImVec2(c.x + r, y), ImVec2(c.x, y + r * 0.34f), ImVec2(c.x - r, y), col, th);
      }
      break;
    case ICO_TERMINAL:
      dl->AddRect(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y + r * 0.8f), col, r * 0.24f, 0, th);
      dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.24f), ImVec2(c.x - r * 0.14f, c.y + r * 0.04f), col, th);
      dl->AddLine(ImVec2(c.x - r * 0.14f, c.y + r * 0.04f), ImVec2(c.x - r * 0.5f, c.y + r * 0.34f), col, th);
      dl->AddLine(ImVec2(c.x + r * 0.06f, c.y + r * 0.34f), ImVec2(c.x + r * 0.56f, c.y + r * 0.34f), col, th);
      break;
    case ICO_KEY:
      dl->AddCircle(ImVec2(c.x - r * 0.4f, c.y - r * 0.32f), r * 0.42f, col, 14, th);
      dl->AddLine(ImVec2(c.x - r * 0.12f, c.y - r * 0.04f), ImVec2(c.x + r * 0.78f, c.y + r * 0.82f), col, th);
      dl->AddLine(ImVec2(c.x + r * 0.34f, c.y + r * 0.38f), ImVec2(c.x + r * 0.58f, c.y + r * 0.14f), col, th);
      break;
    case ICO_EYE:
      dl->AddBezierQuadratic(ImVec2(c.x - r, c.y), ImVec2(c.x, c.y - r * 0.95f), ImVec2(c.x + r, c.y), col, th, 16);
      dl->AddBezierQuadratic(ImVec2(c.x - r, c.y), ImVec2(c.x, c.y + r * 0.95f), ImVec2(c.x + r, c.y), col, th, 16);
      dl->AddCircleFilled(c, r * 0.28f, col, 12);
      break;
    case ICO_WIND:
      dl->AddLine(ImVec2(c.x - r, c.y - r * 0.42f), ImVec2(c.x + r * 0.3f, c.y - r * 0.42f), col, th);
      dl->AddBezierQuadratic(ImVec2(c.x + r * 0.3f, c.y - r * 0.42f), ImVec2(c.x + r, c.y - r * 0.42f),
                             ImVec2(c.x + r * 0.55f, c.y + r * 0.02f), col, th, 12);
      dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.18f), ImVec2(c.x + r * 0.1f, c.y + r * 0.18f), col, th);
      dl->AddBezierQuadratic(ImVec2(c.x + r * 0.1f, c.y + r * 0.18f), ImVec2(c.x + r * 0.72f, c.y + r * 0.18f),
                             ImVec2(c.x + r * 0.34f, c.y + r * 0.66f), col, th, 12);
      break;
    case ICO_TARGET:
      dl->AddCircle(c, r * 0.9f, col, 20, th);
      dl->AddCircle(c, r * 0.42f, col, 14, th);
      dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y - r * 0.6f), col, th);
      dl->AddLine(ImVec2(c.x, c.y + r * 0.6f), ImVec2(c.x, c.y + r), col, th);
      dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.6f, c.y), col, th);
      dl->AddLine(ImVec2(c.x + r * 0.6f, c.y), ImVec2(c.x + r, c.y), col, th);
      break;
    case ICO_SPAWN:
      dl->AddCircle(c, r * 0.9f, col, 20, th);
      dl->AddLine(ImVec2(c.x - r * 0.45f, c.y), ImVec2(c.x + r * 0.45f, c.y), col, th);
      dl->AddLine(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x, c.y + r * 0.45f), col, th);
      break;
    case ICO_ALIEN: {
      // вытянутая голова + два «глаза»-штриха
      dl->AddBezierCubic(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.85f, c.y - r * 0.7f), ImVec2(c.x + r * 0.5f, c.y + r * 0.65f),
                         ImVec2(c.x, c.y + r), col, th, 20);
      dl->AddBezierCubic(ImVec2(c.x, c.y - r), ImVec2(c.x - r * 0.85f, c.y - r * 0.7f), ImVec2(c.x - r * 0.5f, c.y + r * 0.65f),
                         ImVec2(c.x, c.y + r), col, th, 20);
      dl->AddLine(ImVec2(c.x - r * 0.42f, c.y - r * 0.1f), ImVec2(c.x - r * 0.12f, c.y - r * 0.26f), col, th * 0.9f);
      dl->AddLine(ImVec2(c.x + r * 0.42f, c.y - r * 0.1f), ImVec2(c.x + r * 0.12f, c.y - r * 0.26f), col, th * 0.9f);
      break;
    }
    case ICO_BUG:
      dl->AddCircle(c, r * 0.5f, col, 14, th);
      for (int i = 0; i < 6; i++) {
        const float a0 = i * (IM_PI / 3.f) + 0.25f;
        dl->AddLine(ImVec2(c.x + cosf(a0) * r * 0.5f, c.y + sinf(a0) * r * 0.5f),
                    ImVec2(c.x + cosf(a0) * r, c.y + sinf(a0) * r), col, th * 0.85f);
      }
      break;
    case ICO_MOVE:
      dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, th);
      dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, th);
      dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x + r * 0.45f, c.y - r * 0.3f), ImVec2(c.x + r * 0.45f, c.y + r * 0.3f), col);
      dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.45f, c.y - r * 0.3f), ImVec2(c.x - r * 0.45f, c.y + r * 0.3f), col);
      dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x - r * 0.3f, c.y - r * 0.45f), ImVec2(c.x + r * 0.3f, c.y - r * 0.45f), col);
      dl->AddTriangleFilled(ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.3f, c.y + r * 0.45f), ImVec2(c.x + r * 0.3f, c.y + r * 0.45f), col);
      break;
    case ICO_GEAR: {
      dl->AddCircle(c, r * 0.44f, col, 16, th);
      for (int i = 0; i < 8; i++) {
        const float a0 = i * (IM_PI / 4.f);
        dl->AddLine(ImVec2(c.x + cosf(a0) * r * 0.62f, c.y + sinf(a0) * r * 0.62f),
                    ImVec2(c.x + cosf(a0) * r * 0.98f, c.y + sinf(a0) * r * 0.98f), col, th);
      }
      break;
    }
    case ICO_NET: {  // два узла и связь между ними
      dl->AddRect(ImVec2(c.x - r, c.y - r * 0.9f), ImVec2(c.x - r * 0.2f, c.y - r * 0.25f), col, r * 0.12f, 0, th);
      dl->AddRect(ImVec2(c.x + r * 0.2f, c.y + r * 0.25f), ImVec2(c.x + r, c.y + r * 0.9f), col, r * 0.12f, 0, th);
      dl->AddLine(ImVec2(c.x - r * 0.6f, c.y - r * 0.25f), ImVec2(c.x - r * 0.6f, c.y + r * 0.58f), col, th);
      dl->AddLine(ImVec2(c.x - r * 0.6f, c.y + r * 0.58f), ImVec2(c.x + r * 0.2f, c.y + r * 0.58f), col, th);
      dl->AddLine(ImVec2(c.x + r * 0.6f, c.y + r * 0.25f), ImVec2(c.x + r * 0.6f, c.y - r * 0.58f), col, th);
      dl->AddLine(ImVec2(c.x + r * 0.6f, c.y - r * 0.58f), ImVec2(c.x - r * 0.2f, c.y - r * 0.58f), col, th);
      break;
    }
    default: break;
  }
}

// шеврон, который поворачивается при раскрытии (0 — вправо, 1 — вниз)
static void Chevron(ImDrawList* dl, ImVec2 c, float size, float t, ImU32 col) {
  const float a = t * (IM_PI * 0.5f);  // 0..90°
  const float s = sinf(a), k = cosf(a);
  const float r = size * 0.5f;
  // базовая «>» из трёх точек, повёрнутая на a
  const ImVec2 pts[3] = { ImVec2(-r * 0.45f, -r), ImVec2(r * 0.55f, 0.f), ImVec2(-r * 0.45f, r) };
  ImVec2 o[3];
  for (int i = 0; i < 3; i++) o[i] = ImVec2(c.x + pts[i].x * k - pts[i].y * s, c.y + pts[i].x * s + pts[i].y * k);
  dl->AddLine(o[0], o[1], col, ImMax(1.6f, size * 0.16f));
  dl->AddLine(o[1], o[2], col, ImMax(1.6f, size * 0.16f));
}

// ============================ вспомогательное для виджетов ============================
struct Hit {
  ImVec2 p0, p1;
  float w;
  bool hovered, held, clicked;
  ImGuiID id;
  float hoverT, pressT;
};

// InvisibleButton + анимации наведения/нажатия. При блокировке — Dummy (не реагирует).
static Hit Slot(const char* strId, float h, float widthOverride = 0.f) {
  Hit r;
  r.w = widthOverride > 0.f ? widthOverride : ImMax(1.f, ImGui::GetContentRegionAvail().x);
  r.p0 = ImGui::GetCursorScreenPos();
  r.p1 = ImVec2(r.p0.x + r.w, r.p0.y + h);
  r.id = ImGui::GetID(strId);
  if (g_disabled) {
    ImGui::Dummy(ImVec2(r.w, h));
    r.hovered = r.held = r.clicked = false;
  } else {
    r.clicked = ImGui::InvisibleButton(strId, ImVec2(r.w, h));
    r.hovered = ImGui::IsItemHovered();
    r.held = ImGui::IsItemActive();
  }
  r.hoverT = Anim(r.id ^ 0x9E3779B9u, r.hovered ? 1.f : 0.f, 18.f);
  r.pressT = Anim(r.id ^ 0x85EBCA6Bu, r.held ? 1.f : 0.f, 26.f);
  return r;
}

// подложка строки при наведении
static void RowBg(ImDrawList* dl, const Hit& h, float extra = 0.f) {
  const float a = h.hoverT * 0.055f + h.pressT * 0.045f + extra;
  if (a > 0.002f) dl->AddRectFilled(h.p0, h.p1, A(Fade(P().text, a)), M::kRadiusSm);
}

void Tooltip(const char* text) {
  if (!text || !*text) return;
  if (!ImGui::IsItemHovered()) return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, P().surface);
  ImGui::PushStyleColor(ImGuiCol_Border, P().border);
  ImGui::PushStyleColor(ImGuiCol_Text, P().text);
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
}

// ============================ окно ============================
struct WinFrame {
  bool pushedAlpha;
  bool visible;
};
static WinFrame g_win[4];
static int g_winTop = 0;

bool BeginWindow(const char* id, const char* title, const char* subtitle, ImVec2 firstSize, bool* open, float openT) {
  NewFrame();
  const ImGuiID gid = ImGui::GetID(id);
  const float e = EaseOutCubic(ImClamp(openT, 0.f, 1.f));

  float* bx = g_store.GetFloatRef(gid ^ 0x11111111u, -1e9f);
  float* by = g_store.GetFloatRef(gid ^ 0x22222222u, -1e9f);
  if (e < 0.999f && *bx > -1e8f)  // пока въезжает — подталкиваем вверх
    ImGui::SetNextWindowPos(ImVec2(*bx, *by + (1.f - e) * 26.f), ImGuiCond_Always);

  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowSize(ImVec2(ImMin(firstSize.x, ds.x - 40.f), ImMin(firstSize.y, ds.y - 40.f)), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(72.f, 72.f), ImGuiCond_FirstUseEver);
  // не уже, чем влезают подписи, и не больше экрана (тестер: растянул за край — не вернуть)
  ImGui::SetNextWindowSizeConstraints(ImVec2(ImMin(640.f, ds.x), ImMin(380.f, ds.y)), ImVec2(ImMax(ds.x, 1.f), ImMax(ds.y, 1.f)));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, M::kWinRadius);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));  // фон рисуем сами

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
  const bool vis = ImGui::Begin(id, open, flags);

  if (g_winTop < 3) g_winTop++;
  g_win[g_winTop].visible = vis;
  g_win[g_winTop].pushedAlpha = false;

  if (!vis) return false;

  if (e >= 0.999f) {  // окно целиком на экране (заголовок не уезжает за край)
    const ImVec2 wp0 = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const ImVec2 q(ImMax(0.f, ImMin(wp0.x, ds.x - ws.x)), ImMax(0.f, ImMin(wp0.y, ds.y - ws.y)));
    if (q.x != wp0.x || q.y != wp0.y) ImGui::SetWindowPos(q);
  }
  if (e >= 0.999f) {  // запомнили «настоящую» позицию, чтобы анимация не воевала с перетаскиванием
    const ImVec2 wp = ImGui::GetWindowPos();
    *bx = wp.x;
    *by = wp.y;
  }

  PushAlpha(e);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ImMax(e, 0.001f));
  g_win[g_winTop].pushedAlpha = true;

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetWindowPos();
  const ImVec2 sz = ImGui::GetWindowSize();
  const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);

  dl->PushClipRectFullScreen();
  SoftShadow(dl, p0, p1, M::kWinRadius, 0.55f + 0.45f * e);
  dl->PopClipRect();

  // тело
  dl->AddRectFilled(p0, p1, A(P().bg), M::kWinRadius);
  // хедер чуть светлее + разделитель
  dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + M::kHeader), A(P().surface), M::kWinRadius, ImDrawFlags_RoundCornersTop);
  dl->AddLine(ImVec2(p0.x, p0.y + M::kHeader), ImVec2(p1.x, p0.y + M::kHeader), A(Fade(P().border, 0.8f)), 1.f);
  // тонкая внешняя рамка
  dl->AddRect(p0, p1, A(Fade(P().borderLight, 0.55f)), M::kWinRadius, 0, 1.f);
  // фосфорная кромка сверху — как подсветка экрана терминала
  dl->AddLine(ImVec2(p0.x + M::kWinRadius, p0.y + 1.f), ImVec2(p1.x - M::kWinRadius, p0.y + 1.f), A(Fade(P().primary, 0.35f)), 1.f);

  // перетаскивание за хедер
  ImGui::SetCursorScreenPos(p0);
  const bool dragArea = ImGui::InvisibleButton("##bui_titlebar", ImVec2(ImMax(1.f, sz.x - 44.f), M::kHeader));
  (void)dragArea;
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    ImGui::SetWindowPos(ImVec2(p0.x + d.x, p0.y + d.y));
    *bx = p0.x + d.x;
    *by = p0.y + d.y;
  }

  // заголовок
  const float ty = subtitle && *subtitle ? p0.y + 9.f : p0.y + (M::kHeader - ImGui::GetFontSize() * M::kTextLg) * 0.5f;
  TDraw(dl, ImVec2(p0.x + 20.f, ty), A(P().text), title, title + strlen(title), M::kTextLg, g_titleFont);
  if (subtitle && *subtitle)
    TDraw(dl, ImVec2(p0.x + 20.f, p0.y + 11.f + ImGui::GetFontSize() * M::kTextLg), A(P().textDim), subtitle,
          subtitle + strlen(subtitle), M::kTextXs);

  // крестик
  if (open) {
    const float cs = 26.f;
    ImGui::SetCursorScreenPos(ImVec2(p1.x - cs - 14.f, p0.y + (M::kHeader - cs) * 0.5f));
    const Hit h = Slot("##bui_close", cs, cs);
    const ImVec2 c((h.p0.x + h.p1.x) * 0.5f, (h.p0.y + h.p1.y) * 0.5f);
    if (h.hoverT > 0.01f) dl->AddCircleFilled(c, cs * 0.5f, A(Fade(P().danger, 0.18f + 0.22f * h.hoverT)), 20);
    const ImU32 xc = A(LerpCol(P().textDim, P().danger, h.hoverT));
    const float k = cs * 0.22f;
    dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), xc, 1.8f);
    dl->AddLine(ImVec2(c.x + k, c.y - k), ImVec2(c.x - k, c.y + k), xc, 1.8f);
    if (h.clicked) *open = false;
  }

  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + M::kHeader));
  return true;
}

void EndWindow() {
  if (g_win[g_winTop].visible && g_win[g_winTop].pushedAlpha) {
    ImGui::PopStyleVar();  // Alpha
    PopAlpha();
  }
  ImGui::End();
  ImGui::PopStyleColor();   // WindowBg
  ImGui::PopStyleVar(3);    // WindowPadding / WindowRounding / WindowBorderSize
  if (g_winTop > 0) g_winTop--;
}

void PageTitle(const char* title, const char* subtitle, Icon ico) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = subtitle && *subtitle ? 44.f : 30.f;
  float x = p.x;
  if (ico != ICO_NONE) {
    DrawIcon(dl, ImVec2(p.x + 11.f, p.y + 11.f), 21.f, ico, A(P().primary));
    x += 30.f;
  }
  TDraw(dl, ImVec2(x, p.y), A(P().text), title, title + strlen(title), 1.06f, g_titleFont);
  if (subtitle && *subtitle)
    TDraw(dl, ImVec2(x, p.y + ImGui::GetFontSize() * 1.06f + 4.f), A(P().textDim), subtitle, subtitle + strlen(subtitle),
          M::kTextXs);
  ImGui::Dummy(ImVec2(1.f, h));
}

// ============================ сайдбар ============================
bool Sidebar(const char* id, const NavItem* items, int count, int* current, float width) {
  ImGui::PushID(id);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float avail = ImGui::GetContentRegionAvail().y;
  const ImVec2 p1(p0.x + width, p0.y + avail);

  dl->AddRectFilled(p0, p1, A(Fade(P().surface, 0.55f)), 0.f);
  dl->AddLine(ImVec2(p1.x, p0.y), ImVec2(p1.x, p1.y), A(Fade(P().border, 0.8f)), 1.f);

  ImGui::BeginChild("##nav", ImVec2(width, avail), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
  // без межстрочного отступа ImGui: шаг строк — ровно itemH + 4, как у скользящей подсветки (с отступом стиля подсветка
  // отставала на 12 px на каждую строку — нажал «Лог», а подсвечено выше)
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
  ImGui::Dummy(ImVec2(1.f, 10.f));

  const float itemH = 35.f, pad = 10.f;  // 15 страниц влезают и в 720p
  // скользящий индикатор
  const float indY = Anim(ImGui::GetID("##ind"), float(*current), 16.f);

  bool changed = false;
  const float rowW = width - pad * 2.f;
  const ImVec2 listTop = ImGui::GetCursorScreenPos();

  // подсветка выбранного — рисуем ДО строк, чтобы текст был сверху
  {
    const ImVec2 a(listTop.x + pad, listTop.y + indY * (itemH + 4.f));
    const ImVec2 b(a.x + rowW, a.y + itemH);
    dl->AddRectFilled(a, b, A(Fade(P().primary, 0.20f)), M::kRadiusSm);
    dl->AddRectFilled(ImVec2(a.x, a.y + 7.f), ImVec2(a.x + 3.f, b.y - 7.f), A(P().primary), 1.5f);
  }

  for (int i = 0; i < count; i++) {
    ImGui::SetCursorPosX(pad);
    char sid[32];
    snprintf(sid, sizeof sid, "##nav%d", i);
    const Hit h = Slot(sid, itemH, rowW);
    const bool sel = (i == *current);
    if (!sel) RowBg(dl, h);

    const float selT = Anim(h.id ^ 0x5Cu, sel ? 1.f : 0.f, 16.f);
    const ImU32 col = A(LerpCol(LerpCol(P().textDim, P().text, h.hoverT), P().onPrimary, selT));

    float x = h.p0.x + 12.f;
    if (items[i].ico != ICO_NONE) {
      DrawIcon(dl, ImVec2(x + 9.f, (h.p0.y + h.p1.y) * 0.5f), 18.f, items[i].ico,
               A(LerpCol(LerpCol(P().textFaint, P().textDim, h.hoverT), P().primary, selT)));
      x += 27.f;
    }
    const ImVec2 ts = TSizeL(items[i].label, M::kTextSm);
    TDrawL(dl, ImVec2(x, (h.p0.y + h.p1.y) * 0.5f - ts.y * 0.5f), col, items[i].label, M::kTextSm);

    if (items[i].badge && *items[i].badge) {
      const ImVec2 bs = TSize(items[i].badge, nullptr, ImGui::GetFontSize() * 0.7f);
      const ImVec2 b0(h.p1.x - bs.x - 16.f, (h.p0.y + h.p1.y) * 0.5f - bs.y * 0.5f - 3.f);
      Pill(dl, b0, ImVec2(b0.x + bs.x + 12.f, b0.y + bs.y + 6.f), A(Fade(P().primary, 0.28f)));
      TDraw(dl, ImVec2(b0.x + 6.f, b0.y + 3.f), A(P().text), items[i].badge, nullptr, 0.7f);
    }

    if (h.clicked && !sel) {
      *current = i;
      changed = true;
    }
    ImGui::Dummy(ImVec2(1.f, 4.f));
  }

  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(p1.x + 1.f, p0.y));
  return changed;
}

bool BeginContent(const char* id) {
  ImGui::SameLine(0.f, 0.f);
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 14.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 16.f);  // пошире — удобнее хватать
  const bool r = ImGui::BeginChild(id, ImVec2(ImMax(1.f, avail.x), ImMax(1.f, avail.y)), false,
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding);
  return r;
}
void EndContent() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
}

// ============================ секции ============================
void RequestExpandAll(bool open) {
  g_expandAllFrame = ImGui::GetFrameCount();
  g_expandAllValue = open;
}
void SetFilter(const char* text) {
  if (!text) text = "";
  snprintf(g_filter, sizeof g_filter, "%s", text);
}
const char* GetFilter() { return g_filter; }

// в нижний регистр: ASCII и кириллица UTF-8 (А–Я, Ё) — чтобы «камера» находила «Камера»
static void Fold(const char* s, const char* e, char* out, size_t cap) {
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); p < reinterpret_cast<const unsigned char*>(e) && o + 2 < cap; p++) {
    const unsigned char c = *p;
    if (c == 0xD0 && p + 1 < reinterpret_cast<const unsigned char*>(e)) {
      const unsigned char n = p[1];
      if (n >= 0x90 && n <= 0x9F) { out[o++] = char(0xD0), out[o++] = char(n + 0x20), p++; continue; }  // А–П -> а–п
      if (n >= 0xA0 && n <= 0xAF) { out[o++] = char(0xD1), out[o++] = char(n - 0x20), p++; continue; }  // Р–Я -> р–я
      if (n == 0x81) { out[o++] = char(0xD1), out[o++] = char(0x91), p++; continue; }                     // Ё -> ё
    }
    out[o++] = char(tolower(c));
  }
  out[o] = 0;
}
static bool CiFind(const char* hay, const char* hayEnd, const char* needle) {
  if (!needle || !*needle) return true;
  if (!hay) return false;
  if (!hayEnd) hayEnd = hay + strlen(hay);
  char h[512], n[128];
  Fold(hay, hayEnd, h, sizeof h);
  Fold(needle, needle + strlen(needle), n, sizeof n);
  return strstr(h, n) != nullptr;
}

static ImGuiID SecKey(const char* id) { return ImHashStr(id, 0, 0xB0057E12u); }

void SetSectionOpen(const char* id, bool open) { g_store.SetBool(SecKey(id), open); }
bool IsSectionOpen(const char* id) { return g_store.GetBool(SecKey(id), false); }

bool BeginSection(const char* id, const char* label, const char* hint, bool defaultOpen, const char* badge, Icon ico) {
  const ImGuiID key = SecKey(id);
  bool* openRef = g_store.GetBoolRef(key, defaultOpen);

  const bool filtering = g_filter[0] != 0;
  if (filtering) {
    const bool match = CiFind(Vis(label), VisEnd(label), g_filter) || CiFind(hint, nullptr, g_filter);
    if (!match) {
      // секция не подошла под поиск — не показываем совсем
      SecFrame& sf = g_sec[g_secTop < 15 ? ++g_secTop : g_secTop];
      sf.id = key;
      sf.child = false;
      sf.alpha = false;
      sf.top = 0.f;
      return false;
    }
    *openRef = true;  // совпавшие раскрываем сами
  } else if (g_expandAllFrame == ImGui::GetFrameCount()) {
    *openRef = g_expandAllValue;
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushID(id);

  // ---- заголовок ----
  const Hit h = Slot("##hdr", M::kSectionHeader);
  const float openT = Anim(key ^ 0x7777u, *openRef ? 1.f : 0.f, 13.f);
  const float eOpen = EaseInOutQuart(openT);

  const ImU32 bg = LerpCol(Fade(P().surface, 0.75f), P().surface, ImMax(h.hoverT, eOpen * 0.6f));
  dl->AddRectFilled(h.p0, h.p1, A(bg), M::kRadius,
                    eOpen > 0.02f ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
  if (eOpen < 0.02f) dl->AddRect(h.p0, h.p1, A(Fade(P().border, 0.7f)), M::kRadius, 0, 1.f);

  const float cy = (h.p0.y + h.p1.y) * 0.5f;
  Chevron(dl, ImVec2(h.p0.x + 18.f, cy), 12.f, eOpen, A(LerpCol(P().textDim, P().primary, ImMax(h.hoverT, eOpen))));

  float x = h.p0.x + 32.f;
  if (ico != ICO_NONE) {
    DrawIcon(dl, ImVec2(x + 9.f, cy), 18.f, ico, A(LerpCol(P().textDim, P().primary, eOpen)));
    x += 26.f;
  }
  const ImVec2 ls = TSizeL(label, 0.94f, g_titleFont);
  TDrawL(dl, ImVec2(x, cy - ls.y * 0.5f), A(LerpCol(P().text, P().onPrimary, h.hoverT * 0.4f)), label, 0.94f, g_titleFont);

  if (badge && *badge) {
    const ImVec2 bs = TSize(badge, nullptr, ImGui::GetFontSize() * 0.72f);
    const ImVec2 b0(x + ls.x + 10.f, cy - bs.y * 0.5f - 3.f);
    Pill(dl, b0, ImVec2(b0.x + bs.x + 14.f, b0.y + bs.y + 6.f), A(Fade(P().primary, 0.26f)));
    TDraw(dl, ImVec2(b0.x + 7.f, b0.y + 3.f), A(P().primary), badge, nullptr, 0.72f);
  }
  if (hint && *hint && eOpen < 0.5f) {
    const ImVec2 hs = TSize(hint, nullptr, ImGui::GetFontSize() * M::kTextXs);
    if (h.p1.x - 16.f - hs.x > x + ls.x + 60.f)
      TDraw(dl, ImVec2(h.p1.x - 16.f - hs.x, cy - hs.y * 0.5f), A(Fade(P().textFaint, 1.f - eOpen * 2.f)), hint, nullptr,
            M::kTextXs);
  }

  if (h.clicked) *openRef = !*openRef;

  SecFrame& sf = g_sec[g_secTop < 15 ? ++g_secTop : g_secTop];
  sf.id = key;
  sf.child = false;
  sf.alpha = false;
  sf.top = 0.f;

  if (openT < 0.004f) {
    ImGui::PopID();
    ImGui::Dummy(ImVec2(1.f, M::kGap));
    return false;
  }

  // ---- тело: высота анимируется, содержимое подрезается ----
  const float measured = g_store.GetFloat(key ^ 0x8888u, 0.f);
  const float bodyH = ImMax(1.f, (measured > 0.f ? measured : 1.f) * eOpen);
  const ImVec2 b0 = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(b0, ImVec2(b0.x + h.w, b0.y + bodyH), A(Fade(P().surface, 0.45f)), M::kRadius,
                    ImDrawFlags_RoundCornersBottom);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(M::kPad, M::kPad * 0.6f));
  ImGui::BeginChild("##body", ImVec2(h.w, bodyH), false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                        ImGuiWindowFlags_AlwaysUseWindowPadding);
  sf.child = true;
  sf.top = ImGui::GetCursorPosY();
  PushAlpha(ImClamp(eOpen * 1.35f, 0.f, 1.f));
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ImClamp(eOpen * 1.35f, 0.02f, 1.f));
  sf.alpha = true;
  return true;
}

void EndSection() {
  SecFrame& sf = g_sec[g_secTop];
  if (sf.child) {
    const float h = ImGui::GetCursorPosY() - sf.top + M::kPad * 0.6f + ImGui::GetStyle().WindowPadding.y;
    g_store.SetFloat(sf.id ^ 0x8888u, ImMax(h, 2.f));
    if (sf.alpha) {
      ImGui::PopStyleVar();  // Alpha
      PopAlpha();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();  // WindowPadding
    ImGui::PopID();
    ImGui::Dummy(ImVec2(1.f, M::kGap + 4.f));
  }
  if (g_secTop > 0) g_secTop--;
}

// ============================ карточка ============================
void BeginCard(const char* id, const char* caption) {
  ImGui::PushID(id);
  CardFrame& cf = g_card[g_cardTop < 7 ? ++g_cardTop : g_cardTop];
  cf.p0 = ImGui::GetCursorScreenPos();
  cf.width = ImMax(1.f, ImGui::GetContentRegionAvail().x);
  cf.split = &g_cardSplit[g_cardTop];
  cf.split->Split(ImGui::GetWindowDrawList(), 2);
  cf.split->SetCurrentChannel(ImGui::GetWindowDrawList(), 1);  // содержимое — поверх подложки
  ImGui::Dummy(ImVec2(1.f, 10.f));
  if (caption && *caption) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    TDraw(dl, ImVec2(p.x + 12.f, p.y), A(P().textDim), caption, nullptr, 0.74f);
    ImGui::Dummy(ImVec2(1.f, ImGui::GetFontSize() * 0.74f + 6.f));
  }
  ImGui::Indent(12.f);
  ImGui::PushClipRect(ImVec2(cf.p0.x, cf.p0.y), ImVec2(cf.p0.x + cf.width, cf.p0.y + 1e5f), true);
}

void EndCard() {
  CardFrame& cf = g_card[g_cardTop];
  ImGui::PopClipRect();
  ImGui::Unindent(12.f);
  ImGui::Dummy(ImVec2(1.f, 10.f));
  const float y1 = ImGui::GetCursorScreenPos().y;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  cf.split->SetCurrentChannel(dl, 0);
  dl->AddRectFilled(cf.p0, ImVec2(cf.p0.x + cf.width, y1), A(Fade(P().bg, 0.85f)), M::kRadius);
  dl->AddRect(cf.p0, ImVec2(cf.p0.x + cf.width, y1), A(Fade(P().border, 0.9f)), M::kRadius, 0, 1.f);
  cf.split->Merge(dl);
  ImGui::PopID();
  ImGui::Dummy(ImVec2(1.f, M::kGap));
  if (g_cardTop > 0) g_cardTop--;
}

// ============================ клавиша в рамке ============================
static void DrawKeyCap(ImDrawList* dl, ImVec2 p, const char* key, float mul = 0.74f) {
  const ImVec2 ts = TSize(key, nullptr, ImGui::GetFontSize() * mul);
  const ImVec2 a(p.x, p.y - 2.f), b(p.x + ts.x + 12.f, p.y + ts.y + 4.f);
  dl->AddRectFilled(a, b, A(Fade(P().surfaceVar, 0.9f)), 5.f);
  dl->AddRect(a, b, A(Fade(P().borderLight, 0.9f)), 5.f, 0, 1.f);
  dl->AddLine(ImVec2(a.x + 2.f, b.y - 1.f), ImVec2(b.x - 2.f, b.y - 1.f), A(Fade(P().shadow, 0.35f)), 1.f);
  TDraw(dl, ImVec2(p.x + 6.f, p.y + 1.f), A(P().text), key, nullptr, mul);
}
static float KeyCapW(const char* key, float mul = 0.74f) {
  return TSize(key, nullptr, ImGui::GetFontSize() * mul).x + 12.f;
}

void KeyCap(const char* key) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  DrawKeyCap(dl, ImVec2(p.x, p.y + 2.f), key);
  ImGui::Dummy(ImVec2(KeyCapW(key), ImGui::GetFontSize() + 6.f));
}

// ============================ Toggle ============================
static void DrawPill(ImDrawList* dl, ImVec2 p, bool on, float onT, float hoverT) {
  const float w = M::kTrackW, hh = M::kTrackH, r = hh * 0.5f;
  const ImU32 track = on ? LerpCol(P().accent, Hex(0x8CF0AE), hoverT) : LerpCol(P().textFaint, P().borderLight, hoverT);
  const ImU32 trackOff = LerpCol(P().textFaint, P().borderLight, hoverT);
  const ImU32 col = LerpCol(trackOff, LerpCol(P().accent, Hex(0x8CF0AE), hoverT), onT);
  (void)track;
  Pill(dl, p, ImVec2(p.x + w, p.y + hh), A(Fade(col, 0.88f + 0.12f * hoverT)));
  // бегунок: слева -> справа
  const float tx = p.x + r + (w - hh) * EaseOutCubic(onT);
  const float tr = M::kThumb + hoverT * 0.7f;
  dl->AddCircleFilled(ImVec2(tx + 1.f, p.y + r + 1.f), tr, A(Fade(P().shadow, 0.28f)), 20);
  dl->AddCircleFilled(ImVec2(tx, p.y + r), tr, A(P().onPrimary), 20);
}

bool Toggle(const char* label, bool* v, const char* hint, const char* keycap) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool twoLine = hint && *hint;
  const float rowH = twoLine ? 46.f : M::kRow;
  const Hit h = Slot(label, rowH);
  RowBg(dl, h);

  const float onT = Anim(h.id ^ 0xF0u, *v ? 1.f : 0.f, 15.f);
  const ImU32 tc = A(LerpCol(P().text, P().onPrimary, h.hoverT * 0.35f));

  const ImVec2 ls = TSizeL(label, M::kTextSm);
  const float ty = twoLine ? h.p0.y + 7.f : (h.p0.y + h.p1.y) * 0.5f - ls.y * 0.5f;
  TDrawL(dl, ImVec2(h.p0.x + 12.f, ty), tc, label, M::kTextSm);
  if (keycap) DrawKeyCap(dl, ImVec2(h.p0.x + 12.f + ls.x + 9.f, ty + 1.f), keycap);
  if (twoLine) TDraw(dl, ImVec2(h.p0.x + 12.f, ty + ls.y + 3.f), A(P().textDim), hint, nullptr, 0.74f);

  DrawPill(dl, ImVec2(h.p1.x - M::kTrackW - 12.f, (h.p0.y + h.p1.y) * 0.5f - M::kTrackH * 0.5f), *v, onT, h.hoverT);

  if (h.clicked) {
    *v = !*v;
    return true;
  }
  return false;
}

// ============================ Check ============================
bool Check(const char* label, bool* v, const char* hint, const char* keycap) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool twoLine = hint && *hint;
  const float rowH = twoLine ? 44.f : M::kRow;
  const Hit h = Slot(label, rowH);
  RowBg(dl, h);

  const float onT = EaseOutBack(Anim(h.id ^ 0xF1u, *v ? 1.f : 0.f, 17.f));
  const float r = 9.f;
  const ImVec2 c(h.p0.x + 12.f + r, twoLine ? h.p0.y + 15.f : (h.p0.y + h.p1.y) * 0.5f);

  if (onT > 0.02f) {
    dl->AddCircleFilled(c, r * ImClamp(onT, 0.f, 1.15f), A(LerpCol(P().primary, Hex(0x8CF0AE), h.hoverT)), 20);
    const float k = ImClamp(onT, 0.f, 1.f);
    const ImVec2 a(c.x - 4.2f, c.y + 0.2f), b(c.x - 1.2f, c.y + 3.4f), d(c.x + 4.4f, c.y - 3.2f);
    if (k > 0.35f) dl->AddLine(a, ImVec2(a.x + (b.x - a.x) * ImMin(1.f, (k - 0.35f) / 0.3f), a.y + (b.y - a.y) * ImMin(1.f, (k - 0.35f) / 0.3f)), A(P().onPrimary), 2.f);
    if (k > 0.55f) dl->AddLine(b, ImVec2(b.x + (d.x - b.x) * ImMin(1.f, (k - 0.55f) / 0.45f), b.y + (d.y - b.y) * ImMin(1.f, (k - 0.55f) / 0.45f)), A(P().onPrimary), 2.f);
  }
  if (onT < 0.99f)
    dl->AddCircle(c, r, A(Fade(LerpCol(P().borderLight, P().primary, h.hoverT), 1.f - onT * 0.9f)), 20, 1.6f);

  const ImVec2 ls = TSizeL(label, M::kTextSm);
  const float tx = c.x + r + 11.f;
  const float ty = twoLine ? h.p0.y + 7.f : (h.p0.y + h.p1.y) * 0.5f - ls.y * 0.5f;
  TDrawL(dl, ImVec2(tx, ty), A(LerpCol(P().text, P().onPrimary, h.hoverT * 0.3f)), label, M::kTextSm);
  if (keycap) DrawKeyCap(dl, ImVec2(tx + ls.x + 9.f, ty + 1.f), keycap);
  if (twoLine) TDraw(dl, ImVec2(tx, ty + ls.y + 3.f), A(P().textDim), hint, nullptr, 0.74f);

  if (h.clicked) {
    *v = !*v;
    return true;
  }
  return false;
}

// ============================ слайдеры ============================
// Тонкая дорожка, белый кружок. Захват за кружок — относительный (без прыжка),
// клик по дорожке — прыжок в точку и дальше относительно. Так же, как в booster.
static bool SliderCore(const char* label, float* v, float lo, float hi, const char* fmt, const char* hint, bool asInt) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const Hit h = Slot(label, M::kSliderRow);
  RowBg(dl, h, 0.f);

  const float range = (hi - lo) != 0.f ? (hi - lo) : 1.f;
  float val = ImClamp(*v, ImMin(lo, hi), ImMax(lo, hi));
  const float trackX0 = h.p0.x + 12.f, trackX1 = h.p1.x - 12.f;
  const float trackW = ImMax(8.f, trackX1 - trackX0);
  const float trackY = h.p0.y + 33.f;

  // подпись + значение
  const ImVec2 ls = TSizeL(label, M::kTextSm);
  TDrawL(dl, ImVec2(trackX0, h.p0.y + 5.f), A(LerpCol(P().text, P().onPrimary, h.hoverT * 0.3f)), label, M::kTextSm);
  char vtxt[64];
  if (asInt)
    snprintf(vtxt, sizeof vtxt, fmt ? fmt : "%d", int(val + (val < 0 ? -0.5f : 0.5f)));
  else
    snprintf(vtxt, sizeof vtxt, fmt ? fmt : "%.2f", double(val));
  const ImVec2 vs = TSize(vtxt, nullptr, ImGui::GetFontSize() * 0.8f);
  {  // значение в маленькой таблетке справа
    const ImVec2 a(trackX1 - vs.x - 12.f, h.p0.y + 3.f), b(trackX1, h.p0.y + 3.f + vs.y + 5.f);
    Pill(dl, a, b, A(Fade(P().surfaceVar, 0.7f + 0.3f * h.hoverT)));
    TDraw(dl, ImVec2(a.x + 6.f, a.y + 2.5f), A(LerpCol(P().textDim, P().text, h.hoverT)), vtxt, nullptr, 0.8f);
  }
  (void)ls;

  // перетаскивание
  bool changed = false;
  float* anchorX = g_store.GetFloatRef(h.id ^ 0xD1u, 0.f);
  float* anchorV = g_store.GetFloatRef(h.id ^ 0xD2u, 0.f);
  const float thumbX = trackX0 + trackW * ((val - lo) / range);
  ImGuiIO& io = ImGui::GetIO();

  if (h.held) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {  // кадр захвата
      if (ImFabs(io.MousePos.x - thumbX) <= M::kSliderThumb * 2.2f) {
        *anchorX = io.MousePos.x;
        *anchorV = val;
      } else {  // клик по дорожке — прыжок
        *anchorX = io.MousePos.x;
        *anchorV = lo + range * ImClamp((io.MousePos.x - trackX0) / trackW, 0.f, 1.f);
      }
    }
    float nv = *anchorV + (io.MousePos.x - *anchorX) / trackW * range;
    if (io.KeyShift) nv = *anchorV + (io.MousePos.x - *anchorX) / trackW * range * 0.2f;  // точнее
    nv = ImClamp(nv, ImMin(lo, hi), ImMax(lo, hi));
    if (asInt) nv = floorf(nv + 0.5f);
    if (nv != *v) {
      *v = nv;
      val = nv;
      changed = true;
    }
  }
  // колесо мыши над слайдером
  if (h.hovered && io.MouseWheel != 0.f) {
    const float step = asInt ? 1.f : range * (io.KeyShift ? 0.005f : 0.02f);
    float nv = ImClamp(val + io.MouseWheel * step, ImMin(lo, hi), ImMax(lo, hi));
    if (asInt) nv = floorf(nv + 0.5f);
    if (nv != *v) {
      *v = nv;
      val = nv;
      changed = true;
    }
  }

  const float prog = ImClamp((val - lo) / range, 0.f, 1.f);
  const float tx = trackX0 + trackW * prog;
  const float th = 4.f;
  dl->AddRectFilled(ImVec2(trackX0, trackY - th * 0.5f), ImVec2(trackX1, trackY + th * 0.5f), A(P().surfaceVar), th * 0.5f);
  if (prog > 0.004f)
    dl->AddRectFilled(ImVec2(trackX0, trackY - th * 0.5f), ImVec2(tx, trackY + th * 0.5f),
                      A(LerpCol(P().primary, Hex(0x8CF0AE), h.hoverT)), th * 0.5f);
  const float tr = M::kSliderThumb + h.hoverT * 2.f + h.pressT * 1.f;
  dl->AddCircleFilled(ImVec2(tx + 1.f, trackY + 1.5f), tr, A(Fade(P().shadow, 0.3f)), 20);
  dl->AddCircleFilled(ImVec2(tx, trackY), tr, A(P().onPrimary), 20);

  if (hint && *hint) Tooltip(hint);
  return changed;
}

bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt, const char* hint) {
  return SliderCore(label, v, lo, hi, fmt, hint, false);
}
bool SliderI(const char* label, int* v, int lo, int hi, const char* fmt, const char* hint) {
  float f = float(*v);
  if (SliderCore(label, &f, float(lo), float(hi), fmt, hint, true)) {
    *v = int(f + (f < 0 ? -0.5f : 0.5f));
    return true;
  }
  return false;
}

// ============================ кнопки ============================
static bool ButtonCore(const char* label, BtnStyle style, bool enabled, float width, const char* keycap, bool active) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 ls = TSizeL(label, M::kTextSm);
  const float capW = keycap ? KeyCapW(keycap) + 8.f : 0.f;
  const float w = width > 0.f ? width : ls.x + capW + 30.f;
  const float hgt = 32.f;

  PushDisabled(!enabled);
  const Hit h = Slot(label, hgt, w);

  ImU32 bg, fg, br = 0;
  switch (style) {
    case BtnFilled:
      bg = LerpCol(P().primary, Hex(0x8CF0AE), h.hoverT);
      if (h.pressT > 0.f) bg = LerpCol(bg, P().primaryDark, h.pressT);
      fg = P().onPrimary;
      break;
    case BtnDanger:
      bg = LerpCol(Fade(P().danger, 0.18f), Fade(P().danger, 0.32f), h.hoverT);
      fg = P().danger;
      br = Fade(P().danger, 0.45f);
      break;
    case BtnSuccess:
      bg = LerpCol(Fade(P().accent, 0.18f), Fade(P().accent, 0.32f), h.hoverT);
      fg = P().accent;
      br = Fade(P().accent, 0.45f);
      break;
    case BtnGhost:
      bg = Fade(P().text, h.hoverT * 0.08f);
      fg = LerpCol(P().textDim, P().text, h.hoverT);
      break;
    case BtnTinted:
    default:
      bg = LerpCol(P().surface, P().surfaceHi, ImMax(h.hoverT, h.pressT));
      fg = LerpCol(P().text, P().onPrimary, h.hoverT * 0.4f);
      br = Fade(P().border, 0.9f);
      break;
  }
  if (active) {  // «включено» — подсветка акцентом
    bg = LerpCol(Fade(P().accent, 0.22f), Fade(P().accent, 0.34f), h.hoverT);
    fg = P().accent;
    br = Fade(P().accent, 0.55f);
  }

  const float lift = h.pressT * 1.2f;  // легкое «вдавливание»
  const ImVec2 a(h.p0.x, h.p0.y + lift), b(h.p1.x, h.p1.y + lift);
  if (style == BtnFilled && !g_disabled) SoftShadow(dl, a, b, M::kRadiusSm, 0.35f * (1.f - h.pressT * 0.6f));
  dl->AddRectFilled(a, b, A(bg), M::kRadiusSm);
  if (br) dl->AddRect(a, b, A(br), M::kRadiusSm, 0, 1.f);

  float tx = a.x + (w - ls.x - capW) * 0.5f;
  TDrawL(dl, ImVec2(tx, (a.y + b.y) * 0.5f - ls.y * 0.5f), A(fg), label, M::kTextSm);
  if (keycap) DrawKeyCap(dl, ImVec2(tx + ls.x + 8.f, (a.y + b.y) * 0.5f - ImGui::GetFontSize() * 0.37f), keycap);

  PopDisabled();
  return h.clicked && enabled;
}

bool Button(const char* label, BtnStyle style, bool enabled, float width, const char* keycap) {
  return ButtonCore(label, style, enabled, width, keycap, false);
}
bool ToggleButton(const char* label, bool active, bool enabled, float width, const char* keycap) {
  return ButtonCore(label, BtnTinted, enabled, width, keycap, active);
}

// ============================ сегментированный переключатель ============================
bool Segmented(const char* id, const char* const* items, int count, int* current, float width) {
  if (count <= 0) return false;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float hgt = 32.f;
  float w = width > 0.f ? width : 0.f;
  if (w <= 0.f) {
    w = 8.f;
    for (int i = 0; i < count; i++) w += TSizeL(items[i], M::kTextSm).x + 26.f;
  }
  ImGui::PushID(id);
  // подложка — без своей кнопки: ImGui отдаёт наведение первому из перекрытых элементов, и кнопка-подложка забирала
  // клики у сегментов (переключатель не переключался — «English не работает»)
  Hit outer{};
  outer.p0 = ImGui::GetCursorScreenPos();
  outer.p1 = ImVec2(outer.p0.x + w, outer.p0.y + hgt);
  outer.w = w;
  outer.id = ImGui::GetID("##seg");
  outer.hovered = !g_disabled && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(outer.p0, outer.p1);
  outer.hoverT = Anim(outer.id ^ 0x9E3779B9u, outer.hovered ? 1.f : 0.f, 18.f);
  dl->AddRectFilled(outer.p0, outer.p1, A(Fade(P().surfaceVar, 0.55f)), hgt * 0.5f);

  const float segW = (w - 6.f) / count;
  const bool none = *current < 0 || *current >= count;  // ничего не выбрано (например, «ИИ для всех» при разных режимах)
  const float pos = Anim(outer.id ^ 0xEEu, float(ImClamp(*current, 0, count - 1)), 17.f);
  const ImVec2 sa(outer.p0.x + 3.f + pos * segW, outer.p0.y + 3.f);
  const ImVec2 sb(sa.x + segW, outer.p1.y - 3.f);
  if (!none) {
    SoftShadow(dl, sa, sb, (sb.y - sa.y) * 0.5f, 0.28f);
    Pill(dl, sa, sb, A(LerpCol(P().primary, Hex(0x8CF0AE), outer.hoverT * 0.5f)));
  }

  bool changed = false;
  ImGui::SetCursorScreenPos(outer.p0);
  for (int i = 0; i < count; i++) {
    ImGui::SetCursorScreenPos(ImVec2(outer.p0.x + 3.f + i * segW, outer.p0.y + 3.f));
    char sid[24];
    snprintf(sid, sizeof sid, "##s%d", i);
    const Hit h = Slot(sid, hgt - 6.f, segW);
    const bool sel = !none && i == *current;
    const float selT = Anim(h.id ^ 0x3Cu, sel ? 1.f : 0.f, 17.f);
    const ImVec2 ts = TSizeL(items[i], M::kTextSm);
    TDrawL(dl, ImVec2(h.p0.x + (segW - ts.x) * 0.5f, (h.p0.y + h.p1.y) * 0.5f - ts.y * 0.5f),
           A(LerpCol(LerpCol(P().textDim, P().text, h.hoverT), P().onPrimary, selT)), items[i], M::kTextSm);
    if (h.clicked && !sel) {
      *current = i;
      changed = true;
    }
  }
  // весь переключатель — один элемент раскладки (SameLine после него встаёт вровень с ним, а не под ним)
  ImGui::SetCursorScreenPos(outer.p0);
  ImGui::Dummy(ImVec2(w, hgt));
  ImGui::PopID();
  return changed;
}

// ============================ выпадающий список ============================
bool Combo(const char* label, int* current, const char* const* items, int count, const char* hint) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const Hit h = Slot(label, M::kRow + 6.f);
  RowBg(dl, h);

  const ImVec2 ls = TSizeL(label, M::kTextSm);
  const float cy = (h.p0.y + h.p1.y) * 0.5f;
  TDrawL(dl, ImVec2(h.p0.x + 12.f, cy - ls.y * 0.5f), A(P().text), label, M::kTextSm);

  const int cur = ImClamp(*current, 0, ImMax(0, count - 1));
  const char* curTxt = (count > 0 && items) ? items[cur] : "";
  const ImVec2 vs = TSize(Vis(curTxt), VisEnd(curTxt), ImGui::GetFontSize() * M::kTextSm);
  const float boxW = ImMin(ImMax(vs.x + 42.f, 110.f), ImMax(120.f, h.w * 0.5f));
  const ImVec2 a(h.p1.x - 12.f - boxW, cy - 13.f), b(h.p1.x - 12.f, cy + 13.f);
  dl->AddRectFilled(a, b, A(LerpCol(P().surfaceVar, P().surfaceHi, h.hoverT)), M::kRadiusSm);
  dl->AddRect(a, b, A(Fade(P().border, 0.8f)), M::kRadiusSm, 0, 1.f);
  TDraw(dl, ImVec2(a.x + 10.f, cy - vs.y * 0.5f), A(LerpCol(P().text, P().onPrimary, h.hoverT)), Vis(curTxt), VisEnd(curTxt),
        M::kTextSm);

  char popId[80];
  snprintf(popId, sizeof popId, "##combo_%u", unsigned(h.id));
  const float openT = Anim(h.id ^ 0xC0u, ImGui::IsPopupOpen(popId) ? 1.f : 0.f, 20.f);
  Chevron(dl, ImVec2(b.x - 13.f, cy + 1.f), 10.f, 0.5f + openT * 0.5f, A(P().textDim));

  if (h.clicked) ImGui::OpenPopup(popId);
  if (hint && *hint) Tooltip(hint);

  bool changed = false;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, M::kRadius);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, P().surface);
  ImGui::PushStyleColor(ImGuiCol_Border, P().border);
  ImGui::SetNextWindowPos(ImVec2(a.x, b.y + 4.f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(ImMax(boxW, 160.f), 0.f), ImGuiCond_Always);
  if (ImGui::BeginPopup(popId)) {
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; i++) {
      char sid[24];
      snprintf(sid, sizeof sid, "##i%d", i);
      const Hit ih = Slot(sid, 28.f);
      const bool sel = i == cur;
      if (sel)
        pdl->AddRectFilled(ih.p0, ih.p1, A(Fade(P().primary, 0.28f)), 6.f);
      else if (ih.hoverT > 0.01f)
        pdl->AddRectFilled(ih.p0, ih.p1, A(Fade(P().text, ih.hoverT * 0.09f)), 6.f);
      const ImVec2 ts = TSizeL(items[i], M::kTextSm);
      TDrawL(pdl, ImVec2(ih.p0.x + 10.f, (ih.p0.y + ih.p1.y) * 0.5f - ts.y * 0.5f),
             A(sel ? P().onPrimary : LerpCol(P().textDim, P().text, ih.hoverT)), items[i], M::kTextSm);
      if (sel) {
        const ImVec2 c(ih.p1.x - 16.f, (ih.p0.y + ih.p1.y) * 0.5f);
        pdl->AddLine(ImVec2(c.x - 5.f, c.y), ImVec2(c.x - 2.f, c.y + 3.5f), A(P().primary), 2.f);
        pdl->AddLine(ImVec2(c.x - 2.f, c.y + 3.5f), ImVec2(c.x + 5.f, c.y - 4.f), A(P().primary), 2.f);
      }
      if (ih.clicked) {
        *current = i;
        changed = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  return changed;
}

// ============================ биндер клавиш ============================
bool KeyRow(const char* label, const char* keyName, bool listening, const char* hint) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const Hit h = Slot(label, M::kRow);
  RowBg(dl, h);

  const ImVec2 ls = TSizeL(label, M::kTextSm);
  const float cy = (h.p0.y + h.p1.y) * 0.5f;
  TDrawL(dl, ImVec2(h.p0.x + 12.f, cy - ls.y * 0.5f), A(P().text), label, M::kTextSm);

  const char* shown = listening ? "..." : (keyName ? keyName : "?");
  const float capW = ImMax(KeyCapW(shown, 0.78f), 44.f);
  const ImVec2 a(h.p1.x - 12.f - capW, cy - 13.f), b(h.p1.x - 12.f, cy + 13.f);

  float pulse = 1.f;
  if (listening) pulse = 0.55f + 0.45f * sinf(float(ImGui::GetTime()) * 7.f);
  const ImU32 bg = listening ? Fade(P().primary, 0.25f + 0.3f * pulse)
                             : LerpCol(P().surfaceVar, P().surfaceHi, h.hoverT);
  dl->AddRectFilled(a, b, A(bg), 6.f);
  dl->AddRect(a, b, A(listening ? Fade(P().primary, 0.9f) : Fade(P().borderLight, 0.9f)), 6.f, 0, listening ? 1.6f : 1.f);
  const ImVec2 ks = TSize(shown, nullptr, ImGui::GetFontSize() * 0.78f);
  TDraw(dl, ImVec2((a.x + b.x) * 0.5f - ks.x * 0.5f, cy - ks.y * 0.5f), A(listening ? P().onPrimary : P().text), shown,
        nullptr, 0.78f);

  if (hint && *hint) Tooltip(hint);
  return h.clicked;
}

// ============================ ввод текста ============================
// Рамку рисуем сами, само редактирование оставляем ImGui (прозрачный FrameBg).
bool InputText(const char* id, char* buf, size_t bufSize, const char* placeholder, float width) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = width > 0.f ? width : ImMax(60.f, ImGui::GetContentRegionAvail().x);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float hgt = 30.f;
  const ImVec2 b(p.x + w, p.y + hgt);

  const ImGuiID gid = ImGui::GetID(id);
  const bool active = ImGui::GetActiveID() == gid;
  const float focusT = Anim(gid ^ 0xB1u, active ? 1.f : 0.f, 18.f);
  const bool hov = ImGui::IsMouseHoveringRect(p, b);

  dl->AddRectFilled(p, b, A(LerpCol(P().surfaceVar, P().surface, focusT)), M::kRadiusSm);
  dl->AddRect(p, b, A(LerpCol(Fade(P().border, hov ? 1.f : 0.7f), P().primary, focusT)), M::kRadiusSm, 0, 1.f + focusT * 0.6f);

  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, A(P().text));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, (hgt - ImGui::GetFontSize()) * 0.5f));
  ImGui::SetNextItemWidth(w);
  const bool changed = ImGui::InputText(id, buf, bufSize);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);

  if (placeholder && *placeholder && buf[0] == 0 && !active)
    TDraw(dl, ImVec2(p.x + 10.f, p.y + (hgt - ImGui::GetFontSize() * M::kTextSm) * 0.5f), A(P().textFaint), placeholder,
          nullptr, M::kTextSm);
  return changed;
}

bool SearchBox(const char* id, char* buf, size_t bufSize, const char* placeholder) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImMax(120.f, ImGui::GetContentRegionAvail().x);
  ImGui::PushID(id);
  // лупа слева
  ImGui::Indent(26.f);
  const bool changed = InputText("##s", buf, bufSize, placeholder, w - 26.f - (buf[0] ? 26.f : 0.f));
  ImGui::Unindent(26.f);
  const ImVec2 c(p.x + 15.f, p.y + 15.f);
  dl->AddCircle(ImVec2(c.x - 1.f, c.y - 1.f), 5.f, A(P().textDim), 14, 1.6f);
  dl->AddLine(ImVec2(c.x + 2.5f, c.y + 2.5f), ImVec2(c.x + 6.f, c.y + 6.f), A(P().textDim), 1.8f);
  bool cleared = false;
  if (buf[0]) {
    ImGui::SameLine(0.f, 4.f);
    if (Button("x##clr", BtnGhost, true, 22.f)) {
      buf[0] = 0;
      cleared = true;
    }
  }
  ImGui::PopID();
  return changed || cleared;
}

// ============================ текст и мелочи ============================
static void TextV(ImU32 col, float mul, const char* fmt, va_list args) {
  char buf[1024];
  vsnprintf(buf, sizeof buf, fmt, args);
  buf[sizeof buf - 1] = 0;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  TDraw(dl, p, A(col), buf, nullptr, mul);
  const ImVec2 ts = TSize(buf, nullptr, ImGui::GetFontSize() * mul);
  ImGui::Dummy(ImVec2(ImMax(1.f, ts.x), ts.y + 4.f));
}

void Text(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextV(P().text, M::kTextSm, fmt, a);
  va_end(a);
}
void TextDim(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextV(P().textDim, M::kTextSm, fmt, a);
  va_end(a);
}
void TextCol(ImU32 col, const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextV(col, M::kTextSm, fmt, a);
  va_end(a);
}
void Hint(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextV(P().textDim, 0.76f, fmt, a);
  va_end(a);
}

static void TextWrapV(ImU32 col, const char* fmt, va_list a) {
  char buf[2048];
  vsnprintf(buf, sizeof buf, fmt, a);
  buf[sizeof buf - 1] = 0;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImMax(40.f, ImGui::GetContentRegionAvail().x);
  const float sz = ImGui::GetFontSize() * M::kTextSm;
  const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(sz, FLT_MAX, w, buf);
  dl->AddText(ImGui::GetFont(), sz, ImVec2(p.x, p.y + 1.f), A(Fade(P().shadow, 0.5f)), buf, nullptr, w);
  dl->AddText(ImGui::GetFont(), sz, p, A(col), buf, nullptr, w);
  ImGui::Dummy(ImVec2(w, ts.y + 4.f));
}
void TextWrapped(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextWrapV(P().textDim, fmt, a);
  va_end(a);
}
void TextWrappedCol(ImU32 col, const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  TextWrapV(col, fmt, a);
  va_end(a);
}

void Title(const char* text) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(1.f, 4.f));
  const ImVec2 q = ImGui::GetCursorScreenPos();
  TDraw(dl, q, A(P().textDim), text, nullptr, 0.76f, g_titleFont);
  const float sz = ImGui::GetFontSize() * 0.76f;
  ImGui::Dummy(ImVec2(1.f, sz + 6.f));
  (void)p;
}

void Row(const char* label, const char* value, ImU32 valueCol) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = ImMax(60.f, ImGui::GetContentRegionAvail().x);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float hgt = 26.f;
  const ImVec2 ls = TSize(Vis(label), VisEnd(label), ImGui::GetFontSize() * M::kTextSm);
  TDraw(dl, ImVec2(p.x + 12.f, p.y + (hgt - ls.y) * 0.5f), A(P().textDim), Vis(label), VisEnd(label), M::kTextSm);
  if (value && *value) {
    const ImVec2 vs = TSize(value, nullptr, ImGui::GetFontSize() * M::kTextSm);
    TDraw(dl, ImVec2(p.x + w - 12.f - vs.x, p.y + (hgt - vs.y) * 0.5f), A(valueCol ? valueCol : P().text), value, nullptr,
          M::kTextSm);
  }
  ImGui::Dummy(ImVec2(w, hgt));
}

void Status(const char* label, bool ok) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float hgt = 24.f;
  dl->AddCircleFilled(ImVec2(p.x + 17.f, p.y + hgt * 0.5f), 4.f, A(ok ? P().success : P().warning), 14);
  if (ok) dl->AddCircle(ImVec2(p.x + 17.f, p.y + hgt * 0.5f), 7.f, A(Fade(P().success, 0.3f)), 16, 1.f);
  const ImVec2 ls = TSize(Vis(label), VisEnd(label), ImGui::GetFontSize() * M::kTextSm);
  TDraw(dl, ImVec2(p.x + 29.f, p.y + (hgt - ls.y) * 0.5f), A(ok ? P().text : P().textDim), Vis(label), VisEnd(label),
        M::kTextSm);
  ImGui::Dummy(ImVec2(ImMax(1.f, 29.f + ls.x), hgt));
}

void Badge(const char* text, ImU32 col) {
  if (!col) col = P().primary;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 ts = TSize(text, nullptr, ImGui::GetFontSize() * 0.72f);
  Pill(dl, ImVec2(p.x, p.y + 2.f), ImVec2(p.x + ts.x + 14.f, p.y + ts.y + 8.f), A(Fade(col, 0.26f)));
  TDraw(dl, ImVec2(p.x + 7.f, p.y + 5.f), A(col), text, nullptr, 0.72f);
  ImGui::Dummy(ImVec2(ts.x + 14.f, ts.y + 10.f));
}

void Separator(const char* caption) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = ImMax(20.f, ImGui::GetContentRegionAvail().x);
  ImGui::Dummy(ImVec2(1.f, 5.f));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  if (caption && *caption) {
    const ImVec2 ts = TSize(caption, nullptr, ImGui::GetFontSize() * 0.72f);
    TDraw(dl, ImVec2(p.x + 12.f, p.y - ts.y * 0.5f + 1.f), A(P().textFaint), caption, nullptr, 0.72f);
    dl->AddLine(ImVec2(p.x + 20.f + ts.x, p.y + 1.f), ImVec2(p.x + w - 4.f, p.y + 1.f), A(Fade(P().border, 0.9f)), 1.f);
  } else {
    dl->AddLine(ImVec2(p.x + 8.f, p.y + 1.f), ImVec2(p.x + w - 8.f, p.y + 1.f), A(Fade(P().border, 0.9f)), 1.f);
  }
  ImGui::Dummy(ImVec2(1.f, 7.f));
}

void Space(float h) { ImGui::Dummy(ImVec2(1.f, h)); }
void SameLine(float spacing) { ImGui::SameLine(0.f, spacing); }

void ProgressBar(float t, const char* text, ImU32 col) {
  if (!col) col = P().primary;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float w = ImMax(40.f, ImGui::GetContentRegionAvail().x);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float hgt = 8.f;
  t = ImClamp(t, 0.f, 1.f);
  dl->AddRectFilled(ImVec2(p.x, p.y + 4.f), ImVec2(p.x + w, p.y + 4.f + hgt), A(P().surfaceVar), hgt * 0.5f);
  if (t > 0.004f)
    dl->AddRectFilled(ImVec2(p.x, p.y + 4.f), ImVec2(p.x + w * t, p.y + 4.f + hgt), A(col), hgt * 0.5f);
  ImGui::Dummy(ImVec2(w, hgt + 8.f));
  if (text && *text) Hint("%s", text);
}

// ============================ курсор ============================
void DrawCursor() {
  ImGuiIO& io = ImGui::GetIO();
  if (!ImGui::IsMousePosValid()) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 m = io.MousePos;

  // мягкий «пинг» по клику
  static double clickAt = -10.0;
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clickAt = ImGui::GetTime();
  const float e = float(ImGui::GetTime() - clickAt);
  if (e < 0.45f) {
    const float k = e / 0.45f;
    dl->AddCircle(m, 5.f + k * 22.f, Fade(P().primary, (1.f - k) * 0.55f), 24, 2.f * (1.f - k) + 0.6f);
  }

  // стрелка: белая с тёмной окантовкой
  const ImVec2 a(m.x, m.y), b(m.x, m.y + 17.f), c(m.x + 4.6f, m.y + 12.6f), d(m.x + 11.6f, m.y + 12.0f);
  const ImVec2 pts[4] = { a, b, c, d };
  dl->AddConvexPolyFilled(pts, 4, IM_COL32(255, 255, 255, 235));
  dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, 170), ImDrawFlags_Closed, 1.4f);
  dl->AddCircleFilled(ImVec2(m.x + 0.5f, m.y + 0.5f), 1.2f, P().primary, 8);
}

// ============================ базовый стиль ============================
void ApplyStyle() {
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = M::kWinRadius;
  st.ChildRounding = M::kRadius;
  st.FrameRounding = M::kRadiusSm;
  st.PopupRounding = M::kRadius;
  st.GrabRounding = 10.f;
  st.ScrollbarRounding = 8.f;
  st.TabRounding = 8.f;
  st.WindowBorderSize = 0.f;
  st.ChildBorderSize = 0.f;
  st.FrameBorderSize = 0.f;
  st.PopupBorderSize = 1.f;
  st.ItemSpacing = ImVec2(8.f, 5.f);
  st.ItemInnerSpacing = ImVec2(6.f, 4.f);
  st.FramePadding = ImVec2(10.f, 6.f);
  st.WindowPadding = ImVec2(16.f, 12.f);
  st.ScrollbarSize = 10.f;
  st.WindowTitleAlign = ImVec2(0.f, 0.5f);
  st.AntiAliasedLines = true;
  st.AntiAliasedLinesUseTex = true;
  st.AntiAliasedFill = true;
  st.CircleTessellationMaxError = 0.20f;

  auto V = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
  ImVec4* col = st.Colors;
  col[ImGuiCol_Text] = V(P().text);
  col[ImGuiCol_TextDisabled] = V(P().textDim);
  col[ImGuiCol_WindowBg] = V(P().bg);
  col[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  col[ImGuiCol_PopupBg] = V(P().surface);
  col[ImGuiCol_Border] = V(Fade(P().border, 0.9f));
  col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  col[ImGuiCol_FrameBg] = V(P().surfaceVar);
  col[ImGuiCol_FrameBgHovered] = V(P().surfaceHi);
  col[ImGuiCol_FrameBgActive] = V(P().surfaceHi);
  col[ImGuiCol_TitleBg] = V(P().surface);
  col[ImGuiCol_TitleBgActive] = V(P().surface);
  col[ImGuiCol_TitleBgCollapsed] = V(P().bg);
  col[ImGuiCol_MenuBarBg] = V(P().surface);
  col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  col[ImGuiCol_ScrollbarGrab] = V(Fade(P().textFaint, 0.55f));
  col[ImGuiCol_ScrollbarGrabHovered] = V(Fade(P().textDim, 0.8f));
  col[ImGuiCol_ScrollbarGrabActive] = V(P().textDim);
  col[ImGuiCol_CheckMark] = V(P().primary);
  col[ImGuiCol_SliderGrab] = V(P().onPrimary);
  col[ImGuiCol_SliderGrabActive] = V(P().onPrimary);
  col[ImGuiCol_Button] = V(P().surface);
  col[ImGuiCol_ButtonHovered] = V(P().surfaceHi);
  col[ImGuiCol_ButtonActive] = V(P().primaryDark);
  col[ImGuiCol_Header] = V(P().surface);
  col[ImGuiCol_HeaderHovered] = V(P().surfaceHi);
  col[ImGuiCol_HeaderActive] = V(Fade(P().primary, 0.35f));
  col[ImGuiCol_Separator] = V(Fade(P().border, 0.9f));
  col[ImGuiCol_SeparatorHovered] = V(P().primary);
  col[ImGuiCol_SeparatorActive] = V(P().primary);
  col[ImGuiCol_ResizeGrip] = V(Fade(P().textFaint, 0.35f));
  col[ImGuiCol_ResizeGripHovered] = V(Fade(P().primary, 0.7f));
  col[ImGuiCol_ResizeGripActive] = V(P().primary);
  col[ImGuiCol_Tab] = V(P().surface);
  col[ImGuiCol_TabHovered] = V(Fade(P().primary, 0.35f));
  col[ImGuiCol_TabActive] = V(Fade(P().primary, 0.55f));
  col[ImGuiCol_PlotLines] = V(P().primary);
  col[ImGuiCol_PlotHistogram] = V(P().primary);
  col[ImGuiCol_TextSelectedBg] = V(Fade(P().primary, 0.35f));
  col[ImGuiCol_DragDropTarget] = V(P().warning);
  col[ImGuiCol_NavHighlight] = V(Fade(P().primary, 0.6f));
  col[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.45f);
}

}  // namespace BUI
