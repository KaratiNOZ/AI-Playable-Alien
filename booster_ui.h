// booster_ui — своё оформление меню поверх Dear ImGui: всё рисуется руками через ImDrawList.
// Разметка и повадки — как в меню ZDS-Booster (github.com/raildriver1/zds-booster); сделано другом для Alien Playable,
// палитра переделана под игру: терминал Seegson — почти чёрный фон, фосфорно-зелёный акцент, янтарные предупреждения.
// Сверху добавлено то, чего в booster не было: анимированные раскрывающиеся секции, боковое
// меню категорий со скользящим индикатором, поиск по разделам, «развернуть/свернуть всё».
//
// От ImGui берём только низкий уровень: раскладку курсора, стек ID и попадание мыши
// (InvisibleButton). Ни один стандартный виджет ImGui в меню больше не используется.
#pragma once

#include <cstddef>

#include "imgui.h"

namespace BUI {

// ============================ палитра ============================
// Терминал Seegson (у друга была «Apple Dark Mode» из CheatMenu.pas — сетка та же, цвета свои).
struct Palette {
  ImU32 bg;           // #090E0B  фон окна
  ImU32 surface;      // #101813  панель/карточка
  ImU32 surfaceVar;   // #18241D  дорожка слайдера, вдавленное
  ImU32 surfaceHi;    // #213228  наведение
  ImU32 primary;      // #5FDC8A  акцент — зелёный фосфор
  ImU32 primaryDark;  // #379E5C  акцент нажатый
  ImU32 accent;       // #5FDC8A  тумблер «вкл»
  ImU32 success;      // #6BE895
  ImU32 warning;      // #FFB547  янтарь
  ImU32 danger;       // #FF5A48
  ImU32 text;         // #D6F0DE  основной
  ImU32 textDim;      // #85A792  вторичный
  ImU32 textFaint;    // #4F685A  почти невидимый
  ImU32 onPrimary;    // #05100A  на акценте
  ImU32 border;       // #1D3326
  ImU32 borderLight;  // #2B4B38
  ImU32 shadow;       // #000000
};
const Palette& P();

// ============================ анимация ============================
// Экспоненциальное сглаживание, как AnimateWindow в booster: v += (target - v) * speed * dt.
// Состояние живёт в ImGuiStorage по id, так что вызывать можно из любого места.
float Anim(ImGuiID id, float target, float speed = 14.f);
float AnimFor(const char* id, float target, float speed = 14.f);

float EaseOutCubic(float t);
float EaseOutBack(float t);
float EaseInOutQuart(float t);
float EaseOutElastic(float t);

ImU32 LerpCol(ImU32 a, ImU32 b, float t);
ImU32 Fade(ImU32 c, float mul);  // умножить альфу

// Общая прозрачность поддерева (появление окна, свёртывание секции).
void PushAlpha(float a);
void PopAlpha();
float CurAlpha();

// Блокировка: виджеты гаснут и не реагируют, раскладка не ломается.
void PushDisabled(bool disabled);
void PopDisabled();
bool IsDisabled();

// Раз в кадр до первого виджета.
void NewFrame();

// ============================ шрифты ============================
// body — основной, title — для заголовков (может быть nullptr).
void SetFonts(ImFont* body, ImFont* title);
ImFont* TitleFont();

// ============================ иконки ============================
enum Icon {
  ICO_NONE = 0,
  ICO_GAMEPAD,   // управление
  ICO_CAMERA,    // камера
  ICO_APERTURE,  // фоторежим
  ICO_LAYERS,    // уровни
  ICO_TERMINAL,  // лог
  ICO_KEY,       // клавиши
  ICO_EYE,       // экран
  ICO_WIND,      // вентиляция
  ICO_TARGET,    // атака и ИИ
  ICO_SPAWN,     // спавн
  ICO_ALIEN,     // Чужой
  ICO_BUG,       // лицехваты
  ICO_MOVE,      // движение
  ICO_GEAR,      // настройки
  ICO_NET,       // мультиплеер
};
void DrawIcon(ImDrawList* dl, ImVec2 center, float size, Icon ico, ImU32 col);

// ============================ окно ============================
// openT 0..1 — прогресс появления (альфа + подъём). Рисует тень, скругление, хедер, границу.
// Заголовок можно тащить мышью. Возвращает false, если окно свёрнуто/скрыто.
bool BeginWindow(const char* id, const char* title, const char* subtitle, ImVec2 firstSize, bool* open, float openT);
void EndWindow();

// Полоска-хедер с крупным заголовком внутри контента (не путать с хедером окна).
void PageTitle(const char* title, const char* subtitle = nullptr, Icon ico = ICO_NONE);

// ============================ боковое меню ============================
struct NavItem {
  const char* label;
  Icon ico;
  const char* badge;  // маленькая метка справа, может быть nullptr
};
// Вертикальный список категорий. Индикатор выбранного плавно переезжает.
// Возвращает true, если категорию сменили.
bool Sidebar(const char* id, const NavItem* items, int count, int* current, float width = 186.f);

// Область контента справа от сайдбара: свой скролл, отступы, мягкая подложка.
bool BeginContent(const char* id);
void EndContent();

// ============================ раскрывающиеся секции ============================
// Высота анимируется, шеврон поворачивается, содержимое гаснет. defaultOpen — только при первом показе.
bool BeginSection(const char* id, const char* label, const char* hint = nullptr, bool defaultOpen = false,
                  const char* badge = nullptr, Icon ico = ICO_NONE);
void EndSection();

void SetSectionOpen(const char* id, bool open);
bool IsSectionOpen(const char* id);
// Развернуть/свернуть все секции: применится к каждой BeginSection в следующем кадре.
void RequestExpandAll(bool open);
// Поиск: показываются только секции, чьё название содержит строку (регистр не важен).
// Пустая строка — фильтр выключен. Совпавшие секции раскрываются сами.
void SetFilter(const char* text);
const char* GetFilter();

// Карточка — просто сгруппировать несколько строк подложкой.
void BeginCard(const char* id, const char* caption = nullptr);
void EndCard();

// ============================ виджеты ============================
// Все возвращают true в кадр, когда значение изменилось.

// iOS-таблетка: текст слева, переключатель справа. keycap — клавиша в рамке после текста.
bool Toggle(const char* label, bool* v, const char* hint = nullptr, const char* keycap = nullptr);
// Круглая галочка (синий круг с check).
bool Check(const char* label, bool* v, const char* hint = nullptr, const char* keycap = nullptr);
// Тонкая дорожка + белый кружок; значение справа сверху.
bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f", const char* hint = nullptr);
bool SliderI(const char* label, int* v, int lo, int hi, const char* fmt = "%d", const char* hint = nullptr);

enum BtnStyle { BtnFilled, BtnTinted, BtnGhost, BtnDanger, BtnSuccess };
// width 0 — по тексту. Кнопки можно ставить в ряд через SameLine().
bool Button(const char* label, BtnStyle style = BtnTinted, bool enabled = true, float width = 0.f, const char* keycap = nullptr);
// Кнопка «вкл/выкл»: подсвечена, когда active.
bool ToggleButton(const char* label, bool active, bool enabled = true, float width = 0.f, const char* keycap = nullptr);

// Сегментированный переключатель (таблетка с ползающей подсветкой).
bool Segmented(const char* id, const char* const* items, int count, int* current, float width = 0.f);
// Выпадающий список.
bool Combo(const char* label, int* current, const char* const* items, int count, const char* hint = nullptr);

// Строка биндера: «действие — [клавиша]». listening — ждём нажатия (клавиша мигает).
bool KeyRow(const char* label, const char* keyName, bool listening, const char* hint = nullptr);

// Поле ввода (одна строка).
bool InputText(const char* id, char* buf, size_t bufSize, const char* placeholder = nullptr, float width = 0.f);
bool SearchBox(const char* id, char* buf, size_t bufSize, const char* placeholder = nullptr);

// ============================ текст и мелочи ============================
void Text(const char* fmt, ...) IM_FMTARGS(1);
void TextDim(const char* fmt, ...) IM_FMTARGS(1);
void TextCol(ImU32 col, const char* fmt, ...) IM_FMTARGS(2);
void TextWrapped(const char* fmt, ...) IM_FMTARGS(1);
void TextWrappedCol(ImU32 col, const char* fmt, ...) IM_FMTARGS(2);  // то же, своим цветом (статусы, предупреждения)
void Hint(const char* fmt, ...) IM_FMTARGS(1);   // мелкий серый
void Title(const char* text);                     // подзаголовок внутри секции
void Row(const char* label, const char* value, ImU32 valueCol = 0);  // «ключ — значение»
void Status(const char* label, bool ok);          // зелёная/оранжевая точка + текст
void Badge(const char* text, ImU32 col);          // метка-таблетка (inline)
void KeyCap(const char* key);                     // клавиша в рамке (inline)
void Separator(const char* caption = nullptr);
void Space(float h = 8.f);
void SameLine(float spacing = 8.f);
void ProgressBar(float t, const char* text = nullptr, ImU32 col = 0);
void Tooltip(const char* text);                   // подсказка на последний виджет

// Свой курсор (io.MouseDrawCursor = false) — в самом конце кадра.
void DrawCursor();

// Базовый ImGuiStyle под это оформление — после ImGui::CreateContext().
void ApplyStyle();

}  // namespace BUI
