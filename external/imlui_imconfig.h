// ImGui user config for imlui (passed via IMGUI_USER_CONFIG).
#pragma once

// Route ImGui assertions through a handler that reports the failing
// expression together with the ImGui window it happened in, then aborts.
void imlui_imgui_assert(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : imlui_imgui_assert(#_EXPR, __FILE__, __LINE__))
