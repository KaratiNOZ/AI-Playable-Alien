// standalone.cpp — AlienMod.dll без DevTools: внедряется AlienInjector.exe, сама рисует меню ImGui
// (хук IDXGISwapChain::Present) и сама меняет уровни (GAME_LEVEL_MANAGER игры, без Detours).
// Логика мода и окна управления — те же файлы AlienMod/*.cpp, что и в сборке DevTools.
//
// ВАЖНО: не используй одновременно с AlienIsolation.DevTools.asi, в который мод уже встроен, — будет две копии мода.
#include "ModApi.h"
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include "booster_ui.h"
#include "MinHook.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================ уровни (как GAME_LEVEL_MANAGER в DevTools) ============================
// Менеджер: *(*(0x016f0c88) + 0x3c). +0x40 текущий уровень, +0x50 массив уровней, +0x2d0 их число;
// имя уровня = **(0x01be1c4c) + (уровень+4) * 0x46. Функции: get_level_from_name 0x0077b340,
// queue_level 0x0077b320, request_next_level 0x0079a650 (так же делает сама игра в 0x00806070).
namespace lv {
typedef int(__thiscall* GetFn)(addr mgr, const char* name);
typedef void(__thiscall* QueueFn)(addr mgr, addr level);
typedef void(__thiscall* RequestFn)(addr mgr, bool playlist);
constexpr addr GetLevelFromName = 0x0077b340, NameTable = 0x01be1c4c;

struct Entry { addr level; std::string name; };

static addr Manager() {
  addr m = 0;
  Safe([&] {
    addr g = rp(VA(va::GameGlobals));
    if (g) m = rp(g + 0x3c);
  });
  return m;
}
static std::string Name(addr level) {
  char buf[72] = {};
  bool ok = level && Safe([&] {
    int idx = rd<int>(level + 4);
    addr table = rp(rp(VA(NameTable)));
    if (!table || idx < 0 || idx > 4096) return;
    const char* s = reinterpret_cast<const char*>(table + addr(idx) * 0x46);
    for (int i = 0; i < 0x45 && s[i] >= 32 && s[i] < 127; i++) buf[i] = s[i];
  });
  return ok ? std::string(buf) : std::string();
}
static std::vector<Entry> List() {
  std::vector<Entry> out;
  addr m = Manager();
  if (!m) return out;
  int n = 0;
  if (!Safe([&] { n = rd<int>(m + 0x2d0); }) || n < 0 || n > 160) return out;
  for (int i = 0; i < n; i++) {
    addr l = 0;
    if (!SafeRead(m + 0x50 + i * 4, l) || !l) continue;
    std::string s = Name(l);
    if (!s.empty()) out.push_back({ l, s });
  }
  return out;
}
static std::string Current() {
  addr m = Manager(), l = 0;
  return m && SafeRead(m + 0x40, l) ? Name(l) : std::string();
}
static bool Load(addr level) {
  addr m = Manager();
  if (!m || !level) return false;
  AlienMod::OnLevelChange("menu: load level");
  Fn<QueueFn>(va::QueueLevel)(m, level);
  Fn<RequestFn>(va::RequestNextLevel)(m, false);
  Log("level requested: %s", Name(level).c_str());
  return true;
}
static bool LoadByName(std::string name) {
  addr m = Manager();
  if (!m || name.empty()) return false;
  if (_strnicmp(name.c_str(), "Production\\", 11) != 0) name = "Production\\" + name;
  int l = Fn<GetFn>(GetLevelFromName)(m, name.c_str());
  return l != 0 && Load(addr(l));  // 0 — движок не знает такого уровня
}
static std::string Short(const std::string& n) { return _strnicmp(n.c_str(), "Production\\", 11) == 0 ? n.substr(11) : n; }
// Задания — по данным игры: LEVEL_TEXT_DATABASES.XML (какие тексты заданий Mxx грузит уровень) + цели из OBJECTIVES.TXT
// (M01 -> задание 1, M04 -> 2, M06/M07 -> 4, M09/M10 -> 6, M11 -> 7, M12 -> 8, M13 -> 9, M14 -> 10, M16 -> 11, M21 -> 12,
// M22 -> 13, M25 -> 14, M27 -> 15, M30 -> 16, M32 -> 17, M33 -> 18, M34 -> 19). Задания 3 и 5 своих текстов не имеют
static const char* Hint(const std::string& n) {
  static const char* const h[][3] = {
    { "BSP_Torrens", "задания 1, 19 · Торренс", "missions 1, 19 · the Torrens" },
    { "HAB_Airport", "задания 2, 11, 17 · космопорт", "missions 2, 11, 17 · spaceflight terminal" },
    { "Tech_Hub", "задания 4, 6, 10 · башня Лоренц", "missions 4, 6, 10 · Lorenz SysTech spire" },
    { "Tech_Comms", "задания 4, 16 · центр связи Сигсон", "missions 4, 16 · Seegson Communications" },
    { "SCI_Hub", "задания 6, 8, 12, 14, 15 · Сан-Кристобаль, хаб", "missions 6, 8, 12, 14, 15 · San Cristobal hub" },
    { "SCI_HospitalUpper", "задание 6 · госпиталь, верх — на нём тестировали мод", "mission 6 · hospital, upper — the mod was tested here" },
    { "SCI_HospitalLower", "задания 6, 15 · госпиталь, низ", "missions 6, 15 · hospital, lower" },
    { "SCI_AndroidLab", "задания 7, 12 · Сигсон синтетикс", "missions 7, 12 · Seegson Synthetics" },
    { "HAB_ShoppingCentre", "задания 8, 10, 11 · галерея Соломонс", "missions 8, 10, 11 · Solomons Galleria" },
    { "BSP_LV426_Pt01", "задание 9 · LV-426, часть 1", "mission 9 · LV-426, part 1" },
    { "BSP_LV426_Pt02", "задание 9 · LV-426, часть 2", "mission 9 · LV-426, part 2" },
    { "Tech_RnD", "задание 10 · R&D Лоренц", "mission 10 · Lorenz R&D" },
    { "Tech_RnD_HzdLab", "задание 10 · R&D, лаборатория опасных материалов", "mission 10 · R&D, hazard lab" },
    { "Tech_MuthrCore", "задание 13 · ядро APOLLO", "mission 13 · APOLLO core" },
    { "ENG_ReactorCore", "задание 14 · реактор", "mission 14 · reactor core" },
    { "ENG_Alien_Nest", "задание 14 · гнездо", "mission 14 · the nest" },
    { "Solace", "задание 15 · «Анесидора»", "mission 15 · the Anesidora" },
    { "HAB_CorporatePent", "задание 17 · корпоративный пентхаус", "mission 17 · corporate penthouse" },
    { "ENG_TowPlatform", "задание 18 · буксировочная платформа", "mission 18 · tow platform" } };
  for (auto& e : h) {
    size_t k = strlen(e[0]);
    // имя целиком: перед ним начало строки или «\» (Production\Tech_Hub), чтобы короткое имя не совпало с хвостом длинного
    if (n.size() >= k && _stricmp(n.c_str() + n.size() - k, e[0]) == 0 && (n.size() == k || n[n.size() - k - 1] == '\\'))
      return g_lang ? e[2] : e[1];
  }
  return nullptr;
}
}  // namespace lv

// для мультиплеера (mp.cpp): текущий уровень («Production\Frontend» — главное меню)
std::string LevelCurrentName() { return lv::Current(); }

static void DrawLevelsBody() {
  static std::vector<lv::Entry> levels;
  static double at = -100;
  static int sel = -1;
  static char custom[128] = "";
  static const char* status = nullptr;
  if (ImGui::GetTime() - at > 2.0) {
    levels = lv::List();
    at = ImGui::GetTime();
    if (sel >= int(levels.size())) sel = -1;
  }
  const std::string cur = lv::Current();
  const char* curHint = cur.empty() ? nullptr : lv::Hint(lv::Short(cur));
  BUI::Row(TR("Текущий уровень", "Current level"), cur.empty() ? "—" : lv::Short(cur).c_str(), BUI::P().primary);
  if (curHint) BUI::Hint("%s", curHint);
  if (levels.empty()) {
    BUI::Hint("%s", TR("Список появится, когда игра создаст менеджер уровней.", "The list appears once the game creates its level manager."));
  } else {
    static char lfilter[64] = "";
    BUI::SearchBox("##lvfind", lfilter, sizeof lfilter, TR("Поиск уровня...", "Search levels..."));
    BUI::Space(4.f);
    ImGui::BeginChild("##lvlist", ImVec2(0, std::max(200.f, ImGui::GetContentRegionAvail().y - 150.f)), false, ImGuiWindowFlags_NoBackground);
    for (int i = 0; i < int(levels.size()); i++) {
      const std::string sh = lv::Short(levels[i].name);
      const char* h = lv::Hint(sh);
      if (lfilter[0]) {  // по имени и по подписи (номер задания, место), без учёта регистра латиницы
        std::string a = sh + " " + (h ? h : ""), b = lfilter;
        for (char& c : a) c = char(tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = char(tolower(static_cast<unsigned char>(c)));
        if (a.find(b) == std::string::npos) continue;
      }
      const std::string label = (h ? sh + "   —   " + h : sh) + "###l" + std::to_string(i);
      if (BUI::ToggleButton(label.c_str(), i == sel, true, ImGui::GetContentRegionAvail().x)) sel = i;
    }
    ImGui::EndChild();
    BUI::Space(4.f);
    if (BUI::Button(TR("Загрузить выбранный###ls", "Load selected###ls"), BUI::BtnFilled, sel >= 0) && sel >= 0)
      status = lv::Load(levels[sel].level) ? nullptr : TR("Менеджер уровней ещё не готов", "The level manager is not ready yet");
  }
  BUI::Separator(TR("По имени", "By name"));
  BUI::InputText("###ln", custom, IM_ARRAYSIZE(custom), TR("например TECH_RnD_HzdLab", "e.g. TECH_RnD_HzdLab"), 320.f);
  BUI::SameLine();
  if (BUI::Button(TR("Загрузить по имени###lb", "Load by name###lb"), BUI::BtnTinted, custom[0] != 0))
    status = lv::LoadByName(custom) ? nullptr : TR("Движок не знает такого уровня", "The engine doesn't know this level");
  if (status) BUI::TextCol(BUI::P().warning, "%s", status);
}

// ============================ хуки смены уровня (в DevTools их ставит Detours) ============================
static void OnQueueLevel(Invocation&) { HardReset("queue_level"); }
static void OnRequestNextLevel(Invocation&) { HardReset("request_next_level"); }

// ============================ ImGui поверх D3D11 ============================
typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
static PresentFn s_present = nullptr;
static IDXGISwapChain* s_sc = nullptr;
static ID3D11Device* s_dev = nullptr;
static ID3D11DeviceContext* s_ctx = nullptr;
static HWND s_hwnd = nullptr;
static WNDPROC s_origWndProc = nullptr;
static bool s_show = false, s_failed = false;
static PresentFn s_vtOrig = nullptr;  // Present через vtable swapchain (основной путь после первого кадра)
static void* s_presentTarget = nullptr;
static ms_t s_initAt = 0;
static bool s_devtools = false;  // загружен AlienIsolation.DevTools.asi — второе ImGui поверх того же Present

// клавиша меню (меню «Клавиши»; по умолчанию Delete) и Insert — открывают/закрывают меню всегда
static bool IsMenuKey(WPARAM vk) { return vk == WPARAM(AlienMod::MenuKey()) || vk == VK_INSERT; }
static const char* MenuKeyName() {
  static char name[32];
  static int was = -1;
  const int vk = AlienMod::MenuKey();
  if (vk != was) {
    was = vk;
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    if (vk == VK_DELETE || vk == VK_INSERT || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT) sc |= 0x100;
    if (!sc || GetKeyNameTextA(LONG(sc << 16), name, sizeof name) <= 0) snprintf(name, sizeof name, "0x%02X", vk);
  }
  return name;
}
static bool Swallow(UINT m, WPARAM w, LPARAM l) {
  const ImGuiIO& io = ImGui::GetIO();
  switch (m) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
      return io.WantCaptureMouse || GameInputBlocked();  // за Чужого клик мимо окна меню — не Аманде
    case WM_KEYDOWN: case WM_CHAR:
      return io.WantCaptureKeyboard && w != VK_ESCAPE && !IsMenuKey(w) && w != VK_F12 && w != VK_SNAPSHOT;
    case WM_INPUT: {
      RAWINPUT raw{};
      UINT size = sizeof raw;
      if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return false;
      if (raw.header.dwType == RIM_TYPEMOUSE) {
        const USHORT ups = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
        return io.WantCaptureMouse && (raw.data.mouse.usButtonFlags & ups) == 0;
      }
      if (raw.header.dwType == RIM_TYPEKEYBOARD) {
        USHORT vk = raw.data.keyboard.VKey;
        return io.WantCaptureKeyboard && !(raw.data.keyboard.Flags & RI_KEY_BREAK) && vk != VK_ESCAPE && !IsMenuKey(vk) &&
               vk != VK_F12 && vk != VK_SNAPSHOT;  // F12 / Print Screen — скриншоты (Steam) и при открытом меню
      }
      return false;
    }
  }
  return false;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (g_uiUnload != 0) return CallWindowProcW(s_origWndProc, h, m, w, l);  // мод отцепляется / отцеплен — мимо
  // клавиша меню / Insert; пока печатают в поле (поиск, имя уровня), они работают в поле, а не закрывают меню
  const bool typing = s_show && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
  if (m == WM_KEYUP && IsMenuKey(w) && !typing) {
    s_show = !s_show;
    Log("меню %s", s_show ? "открыто" : "закрыто");
  }
  if (s_show && ImGui::GetCurrentContext()) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    if (Swallow(m, w, l)) return m == WM_INPUT ? DefWindowProcW(h, m, w, l) : 0;
  }
  return CallWindowProcW(s_origWndProc, h, m, w, l);
}

static std::string SystemFont() {  // для недостающих символов (— … → ●) и если шрифтов игры нет
  char win[MAX_PATH] = {};
  GetWindowsDirectoryA(win, MAX_PATH);
  for (const char* f : { "segoeui.ttf", "arial.ttf", "tahoma.ttf" }) {
    std::string p = std::string(win) + "\\Fonts\\" + f;
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
  }
  return {};
}
static std::vector<unsigned char> s_fontBody, s_fontTitle;  // TTF из UI.PAK игры (gamefont.cpp) — живут всё время
// Из шрифта игры берём только ASCII, «» и кириллицу: остальные знаки в нём пустые или чужие (тестер: «—» не рисуется —
// двойные пробелы, «·» рисуется как «û»/«й»). Их добирает системный шрифт (merge добавляет только недостающие)
static const ImWchar kGameRanges[] = { 0x0020, 0x007E, 0x00AB, 0x00AB, 0x00BB, 0x00BB, 0x0400, 0x052F, 0 };
static ImFont* AddGameFont(ImGuiIO& io, std::vector<unsigned char>& ttf, float px, const ImWchar* ranges, const std::string& sys) {
  ImFontConfig c;
  c.FontDataOwnedByAtlas = false;
  c.OversampleH = 3, c.OversampleV = 2;
  ImFont* f = io.Fonts->AddFontFromMemoryTTF(ttf.data(), int(ttf.size()), px, &c, sys.empty() ? ranges : kGameRanges);
  if (f && !sys.empty()) {  // добрать символы, которых нет в шрифте игры
    ImFontConfig m;
    m.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(sys.c_str(), px - 1.f, &m, ranges);
  }
  return f;
}
static void LoadFont(ImGuiIO& io) {
  static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0400, 0x052F, 0x2000, 0x206F, 0x2190, 0x21FF, 0x2200, 0x22FF,
                                    0x2500, 0x25FF, 0 };
  const std::string sys = SystemFont();
  ImFont* body = nullptr;
  ImFont* title = nullptr;
  if (AlienMod::GameFontTTF("Isolation_Bold_RU", true, s_fontBody)) body = AddGameFont(io, s_fontBody, 18.f, ranges, sys);
  if (body && AlienMod::GameFontTTF("JixellationRU", false, s_fontTitle)) title = AddGameFont(io, s_fontTitle, 21.f, ranges, sys);
  if (body) {
    io.FontDefault = body;
    Log("ImGui: шрифты игры (Isolation%s)", title ? " + Jixellation" : "");
  } else {
    if (!sys.empty()) body = io.Fonts->AddFontFromFileTTF(sys.c_str(), 17.f, nullptr, ranges);
    if (!body) body = io.Fonts->AddFontDefault();
    Log("ImGui: шрифты игры не нашлись (DATA\\UI.PAK) — системный шрифт");
  }
  AlienMod::SetFonts(body, title);
}

static bool InitImGui(IDXGISwapChain* sc) {
  DXGI_SWAP_CHAIN_DESC d;
  if (FAILED(sc->GetDesc(&d)) || FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_dev)))) return false;
  s_dev->GetImmediateContext(&s_ctx);
  s_sc = sc;
  s_hwnd = d.OutputWindow;
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // не писать imgui.ini в папку игры
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // курсор рисуем сами (AlienMod::DrawCursor)
  ImGui::StyleColorsDark();
  AlienMod::ApplyStyle();
  LoadFont(io);
  ImGui_ImplWin32_Init(s_hwnd);
  ImGui_ImplDX11_Init(s_dev, s_ctx);
  s_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
  Log("ImGui: окно %p, %s / Insert — меню", static_cast<void*>(s_hwnd), MenuKeyName());
  return true;
}

// ============================ меню: одно окно, слева страницы ============================
// Оформление — booster_ui (меню друга по мотивам ZDS-Booster) в палитре терминала Seegson. Окно появляется и
// прячется плавно, индикатор страницы скользит, разделы внутри раскрываются с анимацией
static void DrawMain(float openT) {
  // страницы управления (ui.cpp) + фоторежим, уровни, лог — каждая своя, чтобы не листать одну длинную простыню
  enum { P_PHOTO = AlienMod::PP_COUNT, P_LEVELS, P_LOG, P_COUNT };
  BUI::NavItem nav[P_COUNT];
  for (int i = 0; i < AlienMod::PP_COUNT; i++) {
    const AlienMod::PanelPageInfo pi = AlienMod::PanelPage(i);
    nav[i] = { pi.name, BUI::Icon(pi.icon), pi.badge };
  }
  nav[P_PHOTO] = { TR("Фоторежим", "Photo mode"), BUI::ICO_APERTURE, photo.on ? TR("вкл", "on") : nullptr };
  nav[P_LEVELS] = { TR("Уровни", "Levels"), BUI::ICO_LAYERS, nullptr };
  nav[P_LOG] = { TR("Лог", "Log"), BUI::ICO_TERMINAL, nullptr };
  static int page = 0;
  bool open = true;
  char sub[128];
  snprintf(sub, sizeof sub, TR("SEEGSON TERMINAL  ·  %s / Insert — меню", "SEEGSON TERMINAL  ·  %s / Insert — menu"), MenuKeyName());
  if (BUI::BeginWindow("###AlienPlayable", "ALIEN PLAYABLE", sub, ImVec2(900.f, 700.f), &open, openT)) {
    BUI::Sidebar("##nav", nav, P_COUNT, &page, 190.f);
    if (BUI::BeginContent("##page")) {
      if (s_devtools)
        BUI::TextWrappedCol(BUI::P().danger, "%s",
                            TR("Загружен старый AlienIsolation.DevTools.asi — убери его из папки игры, это второе меню.",
                               "An old AlienIsolation.DevTools.asi is loaded — remove it from the game folder, it's a second menu."));
      if (page < AlienMod::PP_COUNT) {
        const AlienMod::PanelPageInfo pi = AlienMod::PanelPage(page);
        BUI::PageTitle(pi.name, pi.sub, BUI::Icon(pi.icon));
        AlienMod::DrawPanelPage(page);
      } else if (page == P_PHOTO) {
        BUI::PageTitle(TR("Фоторежим", "Photo mode"), TR("свободная камера, машинима, постобработка",
                                                        "free camera, machinima, post-processing"),
                       BUI::ICO_APERTURE);
        AlienMod::DrawPhotoBody();
      } else if (page == P_LEVELS) {
        BUI::PageTitle(TR("Уровни", "Levels"), TR("загрузить уровень прямо из движка", "load a level straight from the engine"),
                       BUI::ICO_LAYERS);
        DrawLevelsBody();
      } else {
        BUI::PageTitle(TR("Лог", "Log"), TR("то же, что в AlienModUI.log", "the same as in AlienModUI.log"), BUI::ICO_TERMINAL);
        AlienMod::DrawLogBody();
      }
    }
    BUI::EndContent();
  }
  BUI::EndWindow();
  if (!open && s_show) {  // крестик в заголовке
    s_show = false;
    Log("меню закрыто");
  }
}

// видно, что наше ImGui живо, даже когда меню закрыто и уровня с Чужим нет
static void DrawWatermark() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 14, io.DisplaySize.y - 14), ImGuiCond_Always, ImVec2(1.f, 1.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, BUI::Fade(BUI::P().surface, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_Border, BUI::Fade(BUI::P().borderLight, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_Text, BUI::P().text);
  const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##alienmark", nullptr, f)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    BUI::DrawIcon(dl, ImVec2(p.x + 9.f, p.y + 9.f), 17.f, BUI::ICO_ALIEN, BUI::P().primary);
    ImGui::Dummy(ImVec2(22.f, 18.f));
    ImGui::SameLine();
    ImGui::Text(TR("Alien Playable — %s / Insert: меню", "Alien Playable — %s / Insert: menu"), MenuKeyName());
  }
  ImGui::End();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
}

static HRESULT __stdcall VtPresent(IDXGISwapChain* sc, UINT sync, UINT flags);

// Переход с inline-хука (MinHook на начале функции Present) на подмену в vtable swapchain: начало Present правят
// и другие (Steam overlay, DevTools/Detours), и наш прыжок могли перезаписать — vtable этим не задевается.
static void SwitchToVtable(IDXGISwapChain* sc) {
  void** vt = *reinterpret_cast<void***>(sc);
  if (vt[8] == reinterpret_cast<void*>(&VtPresent)) return;
  MH_DisableHook(s_presentTarget);  // иначе оригинал из vtable снова зайдёт в наш inline-хук
  DWORD old;
  if (!VirtualProtect(&vt[8], sizeof(void*), PAGE_READWRITE, &old)) {
    MH_EnableHook(s_presentTarget);
    Log("ImGui: vtable не открылась на запись — остаюсь на inline-хуке");
    return;
  }
  s_vtOrig = reinterpret_cast<PresentFn>(vt[8]);
  vt[8] = reinterpret_cast<void*>(&VtPresent);
  VirtualProtect(&vt[8], sizeof(void*), old, &old);
  Log("ImGui: Present перехвачен через vtable swapchain");
}

static void Sharpen(ID3D11Texture2D* bb, float k);  // ниже: резкость фоторежима
static void SharpenRelease();
// отцепление мода (reload.cpp): в потоке рендера — выключить ImGui, вернуть Present в vtable и WndProc окна
static void UiShutdown() {
  if (s_sc && s_vtOrig) {
    void** vt = *reinterpret_cast<void***>(s_sc);
    DWORD old;
    if (vt[8] == reinterpret_cast<void*>(&VtPresent) && VirtualProtect(&vt[8], sizeof(void*), PAGE_READWRITE, &old)) {
      vt[8] = reinterpret_cast<void*>(s_vtOrig);
      VirtualProtect(&vt[8], sizeof(void*), old, &old);
    }
  }
  // WndProc возвращаем, только если сверху наш (иначе кто-то подцепился после нас — наш просто пропускает всё)
  if (s_hwnd && s_origWndProc && GetWindowLongPtrW(s_hwnd, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&WndProc))
    SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_origWndProc));
  SharpenRelease();
  if (ImGui::GetCurrentContext()) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
  }
  s_show = false;
  s_failed = true;  // Present дальше идёт мимо (vtable мог перехватить кто-то ещё поверх нас)
  Log("ImGui: menu off, Present and window procedure given back");
}
// ---- отладка: кадр игры в dev\shot_N.bmp (в 2 раза меньше), без меню мода ----
const std::wstring& ModBase();
static void CaptureFrame(ID3D11Texture2D* bb) {
  D3D11_TEXTURE2D_DESC d;
  bb->GetDesc(&d);
  if (d.SampleDesc.Count != 1) return Log("shot: multisampled back buffer — skipped");
  D3D11_TEXTURE2D_DESC sd = d;
  sd.Usage = D3D11_USAGE_STAGING, sd.BindFlags = 0, sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ, sd.MiscFlags = 0;
  sd.MipLevels = 1, sd.ArraySize = 1;
  ID3D11Texture2D* st = nullptr;
  if (FAILED(s_dev->CreateTexture2D(&sd, nullptr, &st)) || !st) return Log("shot: no staging texture");
  s_ctx->CopyResource(st, bb);
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(s_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
    const unsigned W = d.Width / 2, H = d.Height / 2, row = (W * 3 + 3) & ~3u;
    std::vector<uint8_t> px(size_t(row) * H);
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const bool r10 = d.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    for (unsigned y = 0; y < H; y++) {
      const uint32_t* src = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(m.pData) + size_t(y * 2) * m.RowPitch);
      uint8_t* dst = &px[size_t(H - 1 - y) * row];
      for (unsigned x = 0; x < W; x++) {
        const uint32_t v = src[x * 2];
        uint8_t r, g, b;
        if (r10) r = uint8_t((v & 0x3ff) >> 2), g = uint8_t(((v >> 10) & 0x3ff) >> 2), b = uint8_t(((v >> 20) & 0x3ff) >> 2);
        else if (bgra) b = uint8_t(v), g = uint8_t(v >> 8), r = uint8_t(v >> 16);
        else r = uint8_t(v), g = uint8_t(v >> 8), b = uint8_t(v >> 16);
        dst[x * 3] = b, dst[x * 3 + 1] = g, dst[x * 3 + 2] = r;
      }
    }
    s_ctx->Unmap(st, 0);
    const std::wstring base = ModBase();
    const size_t sl = base.find_last_of(L'\\');
    wchar_t name[32];
    swprintf(name, 32, L"\\dev\\shot_%d.bmp", g_shotIdx.load());
    const std::wstring path = (sl == std::wstring::npos ? std::wstring(L".") : base.substr(0, sl)) + name;
    if (FILE* f = _wfopen(path.c_str(), L"wb")) {
      const uint32_t fsz = 54 + uint32_t(px.size());
      uint8_t h[54] = { 'B', 'M' };
      auto put = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
      put(2, fsz), put(10, 54), put(14, 40), put(18, W), put(22, H);
      h[26] = 1, h[28] = 24;
      put(34, uint32_t(px.size()));
      fwrite(h, 1, 54, f);
      fwrite(px.data(), 1, px.size(), f);
      fclose(f);
      Log("shot: dev\\shot_%d.bmp (%ux%u, format %d)", g_shotIdx.load(), W, H, int(d.Format));
    }
  }
  st->Release();
}
static void Frame(IDXGISwapChain* sc) {
  static bool inFrame = false;
  static unsigned frames = 0;
  if (g_uiUnload == 1 && !inFrame && s_sc && sc == s_sc) {
    UiShutdown();
    g_uiUnload = 2;
    return;
  }
  if (s_failed || inFrame || g_uiUnload != 0) return;
  inFrame = true;
  if (!s_sc) {
    if (!InitImGui(sc)) {
      s_failed = true;
      Log("ImGui: не удалось взять устройство D3D11 у swapchain");
      inFrame = false;
      return;
    }
    s_initAt = NowMs();
    s_devtools = GetModuleHandleW(L"AlienIsolation.DevTools.asi") != nullptr;
    if (s_devtools) Log("ВНИМАНИЕ: загружен AlienIsolation.DevTools.asi — два ImGui на одном Present, лучше убрать .asi из папки игры");
    SwitchToVtable(sc);
  }
  if (sc != s_sc) {  // другой swapchain (например видео) — рисуем только в основной
    inFrame = false;
    return;
  }
  frames++;
  if (frames == 1 || frames == 100 || frames == 1000) Log("ImGui: кадр %u", frames);
  ImGuiIO& io = ImGui::GetIO();
  io.MouseDrawCursor = false;  // свой курсор — AlienMod::DrawCursor
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  BUI::NewFrame();
  AlienMod::SetUiInput(s_show, s_show && io.WantTextInput);
  AlienMod::DrawHud();
  // открытие/закрытие меню — плавно: пока прогресс не догас, окно ещё рисуется (ввод при этом уже у игры)
  const float menuT = BUI::Anim(ImGui::GetID("##menuOpen"), s_show ? 1.f : 0.f, 13.f);
  if (s_show || menuT > 0.004f) {
    DrawMain(menuT);
    if (s_show) AlienMod::DrawCursor();
  } else if (NowMs() - s_initAt < 30000) {
    DrawWatermark();
  }
  ImGui::Render();
  ID3D11Texture2D* bb = nullptr;
  bool drawn = false;
  if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) && bb) {
    if (const float k = PhotoSharpen(); k > 0.001f) Sharpen(bb, k);  // резкость фоторежима — под меню и HUD
    ID3D11RenderTargetView* rtv = nullptr;  // каждый кадр заново: так не нужен хук ResizeBuffers
    if (SUCCEEDED(s_dev->CreateRenderTargetView(bb, nullptr, &rtv)) && rtv) {
      s_ctx->OMSetRenderTargets(1, &rtv, nullptr);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
      rtv->Release();
      drawn = true;
      static ms_t shotAt = 0;  // отладка: кадр вместе с HUD мода
      if (g_shotLeft > 0 && NowMs() >= shotAt) {
        shotAt = NowMs() + ms_t(g_shotEvery.load());
        CaptureFrame(bb);
        g_shotIdx++, g_shotLeft--;
      }
    }
    bb->Release();
  }
  static bool warned = false;
  if (!drawn && !warned) {
    warned = true;
    Log("ImGui: не удалось создать render target из back buffer — рисовать некуда");
  }
  inFrame = false;
}

// ============================ резкость фоторежима ============================
// В движке своей резкости на ПК нет (local_contrast_factor никуда не идёт), поэтому — свой проход по готовому кадру.
static ID3D11VertexShader* s_shVs = nullptr;
static ID3D11PixelShader* s_shPs = nullptr;
static ID3D11Buffer* s_shCb = nullptr;
static ID3D11SamplerState* s_shSmp = nullptr;
static ID3D11Texture2D* s_shCopy = nullptr;
static ID3D11ShaderResourceView* s_shSrv = nullptr;
static D3D11_TEXTURE2D_DESC s_shDesc{};
static bool s_shFailed = false;
static const char kSharpenHlsl[] =
    "struct VO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VO VS(uint id : SV_VertexID) { VO o; o.uv = float2((id << 1) & 2, id & 2);"
    " o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
    "Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "cbuffer C : register(b0) { float4 p; };\n"
    "float4 PS(VO i) : SV_Target { float3 c = t0.Sample(s0, i.uv).rgb;"
    " float3 n = t0.Sample(s0, i.uv + float2(p.y, 0)).rgb + t0.Sample(s0, i.uv - float2(p.y, 0)).rgb"
    " + t0.Sample(s0, i.uv + float2(0, p.z)).rgb + t0.Sample(s0, i.uv - float2(0, p.z)).rgb;"
    " float3 d = c - n * 0.25; d = clamp(d, -0.2, 0.2);"
    " return float4(saturate(c + d * p.x), 1); }\n";
static DXGI_FORMAT ViewFormat(DXGI_FORMAT f) {
  switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
  }
}
// компилятор шейдеров — из системы по имени, а не импортом: без d3dcompiler_47.dll (старые Windows) мод всё равно
// грузится, просто без резкости фоторежима
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                                      UINT, ID3DBlob**, ID3DBlob**);
static D3DCompileFn LoadD3DCompile() {
  for (const wchar_t* n : { L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll" })
    if (HMODULE m = LoadLibraryW(n))
      if (auto f = reinterpret_cast<D3DCompileFn>(GetProcAddress(m, "D3DCompile"))) return f;
  return nullptr;
}
static bool SharpenInit() {
  if (s_shPs) return true;
  if (s_shFailed) return false;
  s_shFailed = true;
  const D3DCompileFn D3DCompile = LoadD3DCompile();
  if (!D3DCompile) return Log("sharpen: no d3dcompiler_47/46/43.dll — photo sharpness is off"), false;
  ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
  if (FAILED(D3DCompile(kSharpenHlsl, sizeof kSharpenHlsl - 1, "sharpen", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vs, &err)) ||
      FAILED(D3DCompile(kSharpenHlsl, sizeof kSharpenHlsl - 1, "sharpen", nullptr, nullptr, "PS", "ps_4_0", 0, 0, &ps, &err))) {
    Log("sharpen: shader compile failed: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
    if (err) err->Release();
    if (vs) vs->Release();
    return false;
  }
  bool ok = SUCCEEDED(s_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &s_shVs)) &&
            SUCCEEDED(s_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &s_shPs));
  vs->Release(), ps->Release();
  D3D11_BUFFER_DESC bd{ 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
  ok = ok && SUCCEEDED(s_dev->CreateBuffer(&bd, nullptr, &s_shCb));
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  ok = ok && SUCCEEDED(s_dev->CreateSamplerState(&sd, &s_shSmp));
  if (!ok) return Log("sharpen: D3D11 objects failed"), false;
  s_shFailed = false;
  Log("sharpen: ready");
  return true;
}
static void SharpenRelease() {
  for (IUnknown* o : std::initializer_list<IUnknown*>{ s_shVs, s_shPs, s_shCb, s_shSmp, s_shSrv, s_shCopy })
    if (o) o->Release();
  s_shVs = nullptr, s_shPs = nullptr, s_shCb = nullptr, s_shSmp = nullptr, s_shSrv = nullptr, s_shCopy = nullptr;
}
static void Sharpen(ID3D11Texture2D* bb, float k) {
  D3D11_TEXTURE2D_DESC d;
  bb->GetDesc(&d);
  if (d.SampleDesc.Count > 1 || !SharpenInit()) return;
  if (!s_shCopy || d.Width != s_shDesc.Width || d.Height != s_shDesc.Height || d.Format != s_shDesc.Format) {
    if (s_shSrv) s_shSrv->Release(), s_shSrv = nullptr;
    if (s_shCopy) s_shCopy->Release(), s_shCopy = nullptr;
    D3D11_TEXTURE2D_DESC c = d;
    c.BindFlags = D3D11_BIND_SHADER_RESOURCE, c.CPUAccessFlags = 0, c.MiscFlags = 0, c.Usage = D3D11_USAGE_DEFAULT;
    c.MipLevels = 1, c.ArraySize = 1;
    D3D11_SHADER_RESOURCE_VIEW_DESC v{};
    v.Format = ViewFormat(d.Format), v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, v.Texture2D.MipLevels = 1;
    if (FAILED(s_dev->CreateTexture2D(&c, nullptr, &s_shCopy)) || FAILED(s_dev->CreateShaderResourceView(s_shCopy, &v, &s_shSrv))) {
      if (s_shCopy) s_shCopy->Release(), s_shCopy = nullptr;
      s_shFailed = true;
      Log("sharpen: copy texture failed (format %d)", int(d.Format));
      return;
    }
    s_shDesc = d;
  }
  ID3D11RenderTargetView* rtv = nullptr;
  D3D11_RENDER_TARGET_VIEW_DESC rd{};
  rd.Format = ViewFormat(d.Format), rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  if (FAILED(s_dev->CreateRenderTargetView(bb, &rd, &rtv)) || !rtv) return;
  s_ctx->CopyResource(s_shCopy, bb);
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(s_ctx->Map(s_shCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    float* f = static_cast<float*>(m.pData);
    f[0] = k * 1.5f, f[1] = 1.f / float(d.Width), f[2] = 1.f / float(d.Height), f[3] = 0;
    s_ctx->Unmap(s_shCb, 0);
  }
  // сохранить то, что трогаем (ImGui дальше сохраняет уже наше состояние — возвращаем игре её собственное)
  ID3D11RenderTargetView* oRtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* oDsv = nullptr;
  s_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oRtv, &oDsv);
  UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_VIEWPORT oVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  s_ctx->RSGetViewports(&nvp, oVp);
  ID3D11RasterizerState* oRs = nullptr;
  s_ctx->RSGetState(&oRs);
  ID3D11BlendState* oBs = nullptr;
  FLOAT oBf[4];
  UINT oMask = 0;
  s_ctx->OMGetBlendState(&oBs, oBf, &oMask);
  ID3D11DepthStencilState* oDs = nullptr;
  UINT oRef = 0;
  s_ctx->OMGetDepthStencilState(&oDs, &oRef);
  ID3D11ShaderResourceView* oSrv = nullptr;
  s_ctx->PSGetShaderResources(0, 1, &oSrv);
  ID3D11SamplerState* oSmp = nullptr;
  s_ctx->PSGetSamplers(0, 1, &oSmp);
  ID3D11Buffer* oCb = nullptr;
  s_ctx->PSGetConstantBuffers(0, 1, &oCb);
  ID3D11PixelShader* oPs = nullptr;
  ID3D11VertexShader* oVs = nullptr;
  ID3D11ClassInstance* oPsI[256];
  ID3D11ClassInstance* oVsI[256];
  UINT nPsI = 256, nVsI = 256;
  s_ctx->PSGetShader(&oPs, oPsI, &nPsI);
  s_ctx->VSGetShader(&oVs, oVsI, &nVsI);
  ID3D11InputLayout* oIl = nullptr;
  s_ctx->IAGetInputLayout(&oIl);
  D3D11_PRIMITIVE_TOPOLOGY oTopo;
  s_ctx->IAGetPrimitiveTopology(&oTopo);
  ID3D11GeometryShader* oGs = nullptr;
  s_ctx->GSGetShader(&oGs, nullptr, nullptr);

  D3D11_VIEWPORT vp{ 0, 0, float(d.Width), float(d.Height), 0, 1 };
  s_ctx->OMSetRenderTargets(1, &rtv, nullptr);
  s_ctx->RSSetViewports(1, &vp);
  s_ctx->RSSetState(nullptr);
  s_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
  s_ctx->OMSetDepthStencilState(nullptr, 0);
  s_ctx->IASetInputLayout(nullptr);
  s_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  s_ctx->VSSetShader(s_shVs, nullptr, 0);
  s_ctx->GSSetShader(nullptr, nullptr, 0);
  s_ctx->PSSetShader(s_shPs, nullptr, 0);
  s_ctx->PSSetShaderResources(0, 1, &s_shSrv);
  s_ctx->PSSetSamplers(0, 1, &s_shSmp);
  s_ctx->PSSetConstantBuffers(0, 1, &s_shCb);
  s_ctx->Draw(3, 0);
  ID3D11ShaderResourceView* none = nullptr;
  s_ctx->PSSetShaderResources(0, 1, &none);

  s_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oRtv, oDsv);
  s_ctx->RSSetViewports(nvp, oVp);
  s_ctx->RSSetState(oRs);
  s_ctx->OMSetBlendState(oBs, oBf, oMask);
  s_ctx->OMSetDepthStencilState(oDs, oRef);
  s_ctx->PSSetShaderResources(0, 1, &oSrv);
  s_ctx->PSSetSamplers(0, 1, &oSmp);
  s_ctx->PSSetConstantBuffers(0, 1, &oCb);
  s_ctx->PSSetShader(oPs, oPsI, nPsI);
  s_ctx->VSSetShader(oVs, oVsI, nVsI);
  s_ctx->GSSetShader(oGs, nullptr, 0);
  s_ctx->IASetInputLayout(oIl);
  s_ctx->IASetPrimitiveTopology(oTopo);
  for (auto* r : oRtv)
    if (r) r->Release();
  if (oDsv) oDsv->Release();
  if (oRs) oRs->Release();
  if (oBs) oBs->Release();
  if (oDs) oDs->Release();
  if (oSrv) oSrv->Release();
  if (oSmp) oSmp->Release();
  if (oCb) oCb->Release();
  if (oPs) oPs->Release();
  if (oVs) oVs->Release();
  for (UINT i = 0; i < nPsI; i++)
    if (oPsI[i]) oPsI[i]->Release();
  for (UINT i = 0; i < nVsI; i++)
    if (oVsI[i]) oVsI[i]->Release();
  if (oIl) oIl->Release();
  if (oGs) oGs->Release();
  rtv->Release();
}

static HRESULT __stdcall HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
  Frame(sc);
  return s_present(sc, sync, flags);
}
static HRESULT __stdcall VtPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
  Frame(sc);
  return s_vtOrig(sc, sync, flags);
}

// адрес Present — из vtable временного swapchain на скрытом окне
static bool InstallPresentHook() {
  HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
  if (!d3d) d3d = LoadLibraryW(L"d3d11.dll");
  auto create = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain"));
  if (!create) return Log("ImGui: нет D3D11CreateDeviceAndSwapChain"), false;
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AlienModDummyD3D";
  RegisterClassW(&wc);
  HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 1;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = w;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  IDXGISwapChain* sc = nullptr;
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
  if (FAILED(hr)) hr = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
  void* present = nullptr;
  if (SUCCEEDED(hr) && sc) present = (*reinterpret_cast<void***>(sc))[8];
  if (sc) sc->Release();
  if (ctx) ctx->Release();
  if (dev) dev->Release();
  DestroyWindow(w);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  if (!present) return Log("ImGui: временный swapchain не создался (hr %08lX)", static_cast<unsigned long>(hr)), false;
  s_presentTarget = present;
  MH_STATUS s = MH_CreateHook(present, reinterpret_cast<void*>(&HookPresent), reinterpret_cast<void**>(&s_present));
  if (s != MH_OK) return Log("ImGui: MH_CreateHook(Present): %s", MH_StatusToString(s)), false;
  return true;  // включится вместе со всеми хуками мода (Interceptor::Commit)
}

static void ExtraInstall() {
  Interceptor::Attach(VA(va::QueueLevel), OnQueueLevel, nullptr);
  Interceptor::Attach(VA(va::RequestNextLevel), OnRequestNextLevel, nullptr);
  InstallPresentHook();
}

// Инжектор зовёт это в игре (CreateRemoteThread) перед внедрением новой версии: 1 — отцепили, 2 — уже был отцеплен.
extern "C" __declspec(dllexport) DWORD WINAPI AlienModUnload(LPVOID) { return AlienMod::Unload() ? 1 : 2; }
#ifdef _MSC_VER
#pragma comment(linker, "/EXPORT:AlienModUnload=_AlienModUnload@4")
#endif

extern "C" BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    // старая сборка (AlienMod.dll) уже в игре — не лезем: две копии мода = вылет. Перезапусти игру.
    if (GetModuleHandleW(L"AlienMod.dll") && GetModuleHandleW(L"AlienMod.dll") != self) return FALSE;
    if (!AlienMod::Start(self, ExtraInstall)) return FALSE;
  }
  return TRUE;
}
