// Compiled into the imgui library: see imlui_imconfig.h.
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstdlib>

void imlui_imgui_assert(const char* expr, const char* file, int line) {
    const char* window = "(none)";
    if (ImGuiContext* g = ImGui::GetCurrentContext())
        if (g->CurrentWindow) window = g->CurrentWindow->Name;
    std::fprintf(stderr, "[imgui] assertion failed: %s\n  at %s:%d\n  in window: %s\n", expr, file, line, window);
    std::fflush(stderr);
    std::abort();
}
