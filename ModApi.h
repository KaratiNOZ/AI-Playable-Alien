// ModApi.h — то, что видит DevTools (dllmain.cpp, Menu.cpp, GAME_LEVEL_MANAGER.cpp) от мода «Alien Playable».
// Специально без windows.h и внутренних заголовков мода: их макросы min/max и глобалы в Menu.cpp не нужны.
#pragma once
#include <vector>
struct ImFont;

namespace AlienMod
{
	// DllMain(DLL_PROCESS_ATTACH), ПОСЛЕ DetourTransactionCommit DevTools: лог + поток, который ставит хуки мода.
	// extraInstall (необязательно) — дополнительные хуки, которые ставятся вместе с хуками мода (до их включения):
	// отдельная AlienMod.dll ставит так свой D3D11 Present (ImGui) и хуки смены уровня.
	// false — в этой игре мод уже запущен (другая DLL или DevTools со встроенным модом): вторую копию не запускаем.
	bool Start(void* hModule, void (*extraInstall)() = nullptr);

	// смена уровня (хуки queue_level / request_next_level в DevTools и кнопка загрузки уровня в ImGui):
	// сбросить все указатели мода, пока игра разрушает деревья поведения старого уровня.
	void OnLevelChange(const char* why);

	// каждый кадр из Menu::DrawMenu: что сейчас делает ImGui (чтобы клавиши мода и блок ввода Аманды ему не мешали).
	void SetUiInput(bool menuShown, bool wantTextInput);

	// окна мода в ImGui (зовутся внутри кадра ImGui, между NewFrame и Render)
	void DrawPanel(bool* open);  // «Alien Playable — управление»
	void DrawHud();              // маленький статус поверх игры (вместо старого окна-оверлея)
	void DrawLog(bool* open);    // лог мода (то же, что пишется в AlienMod.log)

	bool HudEnabled();

	// отцепить мод (reload.cpp): вернуть игре всё, выключить хуки и ImGui, передать узлы следующей версии.
	// Unload — синхронно, не из потока рендера; RequestUnload — из меню (в своём потоке). DLL остаётся в памяти.
	bool Unload();
	void RequestUnload();
	bool Unloaded();

	// содержимое тех же окон без Begin/End — чтобы встроить во вкладки своего окна
	void DrawPanelBody();   // все страницы управления подряд
	// страницы управления для бокового меню (standalone.cpp): иконка — BUI::Icon, badge — метка справа (может быть nullptr)
	enum PanelPageId { PP_MAIN, PP_CAMERA, PP_MOVE, PP_ALIEN, PP_AI, PP_HUGGERS, PP_VENTS, PP_SPAWN, PP_MP, PP_SCREEN, PP_KEYS, PP_MOD, PP_COUNT };
	struct PanelPageInfo { const char* name; const char* sub; int icon; const char* badge; };
	PanelPageInfo PanelPage(int i);
	void DrawPanelPage(int i);
	int MenuKey();          // клавиша меню (переназначается; Insert — всегда)
	void DrawLogBody();
	void DrawPhotoBody();   // вкладка «Фоторежим»
	void DrawLanguage();    // выбор языка (Русский / English)

	// оформление в стиле терминалов Alien: Isolation (после ImGui::CreateContext / StyleColorsDark)
	void ApplyStyle();
	void DrawCrt();         // полосы развёртки ЭЛТ поверх текущего окна (перед ImGui::End)
	void DrawCursor();      // свой курсор (io.MouseDrawCursor = false), в конце кадра

	// шрифты самой игры (DATA/UI.PAK -> FONTS_RU.GFX): TTF в памяти для ImGui (gamefont.cpp); false — не вышло
	bool GameFontTTF(const char* name, bool bold, std::vector<unsigned char>& out);
	// основной шрифт и шрифт заголовков (Jixellation, может быть nullptr) — после того как их добавили в атлас
	void SetFonts(ImFont* body, ImFont* title);
}
