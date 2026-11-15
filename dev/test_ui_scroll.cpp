// Headless regression: scrolling must not change the measured height of offscreen sections.
#include "booster_ui.h"
#include "imgui.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1000, 700);
  io.DeltaTime = 1.f / 60.f;
  unsigned char* pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  float reference = 0, minHeight = 1e9f, maxHeight = 0;
  for (int frame = 0; frame < 600; ++frame) {
    ImGui::NewFrame();
    BUI::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 550));
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginChild("page", ImVec2(700, 450));
    const float maxScroll = ImGui::GetScrollMaxY();
    if (frame < 240) ImGui::SetScrollY(ImGui::GetScrollY() + 40.f);
    else ImGui::SetScrollY(((frame / 45) & 1) ? maxScroll : 0.f);
    for (int section = 0; section < 8; ++section) {
      char id[32];
      std::snprintf(id, sizeof id, "section%d", section);
      if (BUI::BeginSection(id, id, nullptr, true)) {
        for (int row = 0; row < 8; ++row) {
          char label[32];
          std::snprintf(label, sizeof label, "row%d", row);
          BUI::Button(label);
        }
        BUI::EndSection();
      }
    }
    if (frame == 260) reference = maxScroll;
    if (frame > 260) {
      minHeight = std::fmin(minHeight, maxScroll);
      maxHeight = std::fmax(maxHeight, maxScroll);
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
  }
  std::printf("Scroll extent: initial %.1f, range %.1f..%.1f\n", reference, minHeight, maxHeight);
  if (reference <= 1000.f || maxHeight - minHeight >= 4.f) return 1;
  ImGui::DestroyContext();
  std::puts("PASS: section heights remain stable while scrolling offscreen and back");
}
