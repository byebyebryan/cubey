#pragma once

#include <imgui.h>

namespace cubey::host {

class ScopedImGuiId {
  public:
    explicit ScopedImGuiId(const char* label) {
        ImGui::PushID(label);
    }
    ~ScopedImGuiId() {
        ImGui::PopID();
    }

    ScopedImGuiId(const ScopedImGuiId&) = delete;
    ScopedImGuiId& operator=(const ScopedImGuiId&) = delete;
};

} // namespace cubey::host
