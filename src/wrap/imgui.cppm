// Module wrapper around Dear ImGui (imgui.h).
//
// Re-exports the subset of the ImGui API that imlui (and apps written against
// it) use: types, flag enumerators and the ImGui:: functions. Macros such as
// IM_COL32 are replaced with constexpr functions. If you need something that
// is not exported yet, add a `using` line below.
module;

#include "imgui.h"

export module imgui;

// ------------------------------------------------------------------- types
export using ::ImVec2;
export using ::ImVec4;
export using ::ImU32;
export using ::ImGuiID;
export using ::ImWchar;
export using ::ImTextureID;
export using ::ImFont;
export using ::ImFontBaked;
export using ::ImTextureRef;
export using ::ImFontAtlas;
export using ::ImFontConfig;
export using ::ImDrawList;
export using ::ImDrawData;
export using ::ImGuiIO;
export using ::ImGuiStyle;
export using ::ImGuiStorage;
export using ::ImGuiViewport;
export using ::ImGuiContext;
export using ::ImGuiInputTextCallback;
export using ::ImGuiInputTextCallbackData;

// flag typedefs
export using ::ImGuiCol;
export using ::ImGuiCond;
export using ::ImGuiStyleVar;
export using ::ImGuiWindowFlags;
export using ::ImGuiChildFlags;
export using ::ImGuiInputTextFlags;
export using ::ImGuiSliderFlags;
export using ::ImGuiTreeNodeFlags;
export using ::ImGuiSelectableFlags;
export using ::ImGuiTableFlags;
export using ::ImGuiTableColumnFlags;
export using ::ImGuiTabBarFlags;
export using ::ImGuiTabItemFlags;
export using ::ImGuiComboFlags;
export using ::ImGuiColorEditFlags;
export using ::ImGuiHoveredFlags;
export using ::ImGuiPopupFlags;
export using ::ImGuiConfigFlags;
export using ::ImGuiMouseButton;
export using ::ImDrawFlags;
export using ::ImGuiDataType;
export using ::ImGuiKey;
export using ::ImGuiTableBgTarget;
export using ::ImGuiItemFlags;

// ------------------------------------------------------------------- enums
export using ::ImGuiCol_;
export using ::ImGuiCol_Text;
export using ::ImGuiCol_TextDisabled;
export using ::ImGuiCol_WindowBg;
export using ::ImGuiCol_ChildBg;
export using ::ImGuiCol_PopupBg;
export using ::ImGuiCol_Border;
export using ::ImGuiCol_BorderShadow;
export using ::ImGuiCol_FrameBg;
export using ::ImGuiCol_FrameBgHovered;
export using ::ImGuiCol_FrameBgActive;
export using ::ImGuiCol_TitleBg;
export using ::ImGuiCol_TitleBgActive;
export using ::ImGuiCol_TitleBgCollapsed;
export using ::ImGuiCol_MenuBarBg;
export using ::ImGuiCol_ScrollbarBg;
export using ::ImGuiCol_ScrollbarGrab;
export using ::ImGuiCol_ScrollbarGrabHovered;
export using ::ImGuiCol_ScrollbarGrabActive;
export using ::ImGuiCol_CheckMark;
export using ::ImGuiCol_CheckboxSelectedBg;
export using ::ImGuiCol_SliderGrab;
export using ::ImGuiCol_SliderGrabActive;
export using ::ImGuiCol_Button;
export using ::ImGuiCol_ButtonHovered;
export using ::ImGuiCol_ButtonActive;
export using ::ImGuiCol_Header;
export using ::ImGuiCol_HeaderHovered;
export using ::ImGuiCol_HeaderActive;
export using ::ImGuiCol_Separator;
export using ::ImGuiCol_SeparatorHovered;
export using ::ImGuiCol_SeparatorActive;
export using ::ImGuiCol_ResizeGrip;
export using ::ImGuiCol_ResizeGripHovered;
export using ::ImGuiCol_ResizeGripActive;
export using ::ImGuiCol_InputTextCursor;
export using ::ImGuiCol_TabHovered;
export using ::ImGuiCol_Tab;
export using ::ImGuiCol_TabSelected;
export using ::ImGuiCol_TabSelectedOverline;
export using ::ImGuiCol_TabDimmed;
export using ::ImGuiCol_TabDimmedSelected;
export using ::ImGuiCol_TabDimmedSelectedOverline;
export using ::ImGuiCol_PlotLines;
export using ::ImGuiCol_PlotLinesHovered;
export using ::ImGuiCol_PlotHistogram;
export using ::ImGuiCol_PlotHistogramHovered;
export using ::ImGuiCol_TableHeaderBg;
export using ::ImGuiCol_TableBorderStrong;
export using ::ImGuiCol_TableBorderLight;
export using ::ImGuiCol_TableRowBg;
export using ::ImGuiCol_TableRowBgAlt;
export using ::ImGuiCol_TextLink;
export using ::ImGuiCol_TextSelectedBg;
export using ::ImGuiCol_TreeLines;
export using ::ImGuiCol_DragDropTarget;
export using ::ImGuiCol_DragDropTargetBg;
export using ::ImGuiCol_UnsavedMarker;
export using ::ImGuiCol_NavCursor;
export using ::ImGuiCol_NavWindowingHighlight;
export using ::ImGuiCol_NavWindowingDimBg;
export using ::ImGuiCol_ModalWindowDimBg;
export using ::ImGuiCol_COUNT;

export using ::ImGuiCond_;
export using ::ImGuiCond_None;
export using ::ImGuiCond_Always;
export using ::ImGuiCond_Once;
export using ::ImGuiCond_FirstUseEver;
export using ::ImGuiCond_Appearing;

export using ::ImGuiStyleVar_;
export using ::ImGuiStyleVar_Alpha;
export using ::ImGuiStyleVar_WindowPadding;
export using ::ImGuiStyleVar_WindowRounding;
export using ::ImGuiStyleVar_WindowBorderSize;
export using ::ImGuiStyleVar_ChildRounding;
export using ::ImGuiStyleVar_ChildBorderSize;
export using ::ImGuiStyleVar_PopupRounding;
export using ::ImGuiStyleVar_FramePadding;
export using ::ImGuiStyleVar_FrameRounding;
export using ::ImGuiStyleVar_FrameBorderSize;
export using ::ImGuiStyleVar_ItemSpacing;
export using ::ImGuiStyleVar_ItemInnerSpacing;
export using ::ImGuiStyleVar_IndentSpacing;
export using ::ImGuiStyleVar_CellPadding;
export using ::ImGuiStyleVar_GrabRounding;
export using ::ImGuiStyleVar_TabRounding;
export using ::ImGuiStyleVar_ButtonTextAlign;
export using ::ImGuiStyleVar_SelectableTextAlign;
export using ::ImGuiStyleVar_ScrollbarSize;
export using ::ImGuiStyleVar_ScrollbarRounding;
export using ::ImGuiStyleVar_PopupBorderSize;
export using ::ImGuiStyleVar_TabBarBorderSize;

export using ::ImGuiWindowFlags_;
export using ::ImGuiWindowFlags_None;
export using ::ImGuiWindowFlags_NoTitleBar;
export using ::ImGuiWindowFlags_NoResize;
export using ::ImGuiWindowFlags_NoMove;
export using ::ImGuiWindowFlags_NoScrollbar;
export using ::ImGuiWindowFlags_NoCollapse;
export using ::ImGuiWindowFlags_AlwaysAutoResize;
export using ::ImGuiWindowFlags_NoBackground;
export using ::ImGuiWindowFlags_NoSavedSettings;
export using ::ImGuiWindowFlags_MenuBar;
export using ::ImGuiWindowFlags_HorizontalScrollbar;
export using ::ImGuiWindowFlags_NoBringToFrontOnFocus;
export using ::ImGuiWindowFlags_NoDecoration;
export using ::ImGuiWindowFlags_NoNav;
export using ::ImGuiWindowFlags_NoFocusOnAppearing;
export using ::ImGuiWindowFlags_NoInputs;

export using ::ImGuiChildFlags_;
export using ::ImGuiChildFlags_None;
export using ::ImGuiChildFlags_Borders;
export using ::ImGuiChildFlags_AlwaysUseWindowPadding;
export using ::ImGuiChildFlags_AutoResizeX;
export using ::ImGuiChildFlags_AutoResizeY;

export using ::ImGuiInputTextFlags_;
export using ::ImGuiInputTextFlags_None;
export using ::ImGuiInputTextFlags_EnterReturnsTrue;
export using ::ImGuiInputTextFlags_Password;
export using ::ImGuiInputTextFlags_ReadOnly;
export using ::ImGuiInputTextFlags_CallbackResize;

export using ::ImGuiSliderFlags_;
export using ::ImGuiSliderFlags_None;
export using ::ImGuiSliderFlags_AlwaysClamp;

export using ::ImGuiTreeNodeFlags_;
export using ::ImGuiTreeNodeFlags_None;
export using ::ImGuiTreeNodeFlags_DefaultOpen;
export using ::ImGuiTreeNodeFlags_SpanAvailWidth;
export using ::ImGuiTreeNodeFlags_Framed;

export using ::ImGuiSelectableFlags_;
export using ::ImGuiSelectableFlags_None;
export using ::ImGuiSelectableFlags_AllowOverlap;
export using ::ImGuiSelectableFlags_NoAutoClosePopups;
export using ::ImGuiSelectableFlags_SpanAllColumns;

export using ::ImGuiTableFlags_;
export using ::ImGuiTableFlags_None;
export using ::ImGuiTableFlags_SizingStretchSame;
export using ::ImGuiTableFlags_Borders;
export using ::ImGuiTableFlags_RowBg;
export using ::ImGuiTableFlags_PadOuterX;
export using ::ImGuiTableFlags_BordersInnerH;
export using ::ImGuiTableFlags_BordersOuterH;
export using ::ImGuiTableFlags_BordersH;
export using ::ImGuiTableFlags_NoPadOuterX;
export using ::ImGuiTableFlags_ScrollY;

export using ::ImGuiTableBgTarget_;
export using ::ImGuiTableBgTarget_RowBg0;
export using ::ImGuiTableBgTarget_RowBg1;
export using ::ImGuiTableBgTarget_CellBg;

export using ::ImGuiDataType_;
export using ::ImGuiDataType_S32;
export using ::ImGuiDataType_Double;

export using ::ImGuiPopupFlags_;
export using ::ImGuiPopupFlags_None;

export using ::ImGuiItemFlags_;
export using ::ImGuiItemFlags_Disabled;
export using ::ImGuiItemFlags_NoTabStop;

export using ::ImGuiKey_Escape;
export using ::ImGuiKey_Enter;
export using ::ImGuiKey_UpArrow;
export using ::ImGuiKey_DownArrow;

export using ::ImGuiTabBarFlags_;
export using ::ImGuiTabBarFlags_None;
export using ::ImGuiTabBarFlags_FittingPolicyScroll;

export using ::ImGuiColorEditFlags_;
export using ::ImGuiColorEditFlags_None;
export using ::ImGuiColorEditFlags_NoAlpha;
export using ::ImGuiColorEditFlags_NoInputs;
export using ::ImGuiColorEditFlags_AlphaBar;
export using ::ImGuiColorEditFlags_NoSidePreview;
export using ::ImGuiColorEditFlags_NoSmallPreview;
export using ::ImGuiColorEditFlags_NoLabel;
export using ::ImGuiColorEditFlags_PickerHueWheel;
export using ::ImGuiColorEditFlags_PickerHueBar;
export using ::ImGuiColorEditFlags_DisplayHex;
export using ::ImGuiColorEditFlags_NoTooltip;

export using ::ImGuiHoveredFlags_;
export using ::ImGuiHoveredFlags_None;
export using ::ImGuiHoveredFlags_AllowWhenDisabled;
export using ::ImGuiHoveredFlags_ForTooltip;

export using ::ImGuiConfigFlags_;
export using ::ImGuiConfigFlags_NavEnableKeyboard;

export using ::ImGuiMouseButton_;
export using ::ImGuiMouseButton_Left;

export using ::ImDrawFlags_;
export using ::ImDrawFlags_None;
export using ::ImDrawFlags_RoundCornersAll;
export using ::ImDrawFlags_RoundCornersLeft;
export using ::ImDrawFlags_RoundCornersRight;
export using ::ImDrawFlags_RoundCornersTop;
export using ::ImDrawFlags_RoundCornersNone;
export using ::ImDrawFlags_Closed;

// --------------------------------------------------------------- functions
export namespace ImGui {
// context / frame
using ImGui::CreateContext;
using ImGui::DestroyContext;
using ImGui::GetIO;
using ImGui::GetStyle;
using ImGui::NewFrame;
using ImGui::Render;
using ImGui::GetDrawData;
using ImGui::GetTime;
using ImGui::GetFrameCount;
using ImGui::ShowDemoWindow;
using ImGui::ShowMetricsWindow;
using ImGui::GetVersion;
// styles
using ImGui::StyleColorsDark;
using ImGui::StyleColorsLight;
using ImGui::StyleColorsClassic;
using ImGui::GetStyleColorName;
using ImGui::GetStyleColorVec4;
using ImGui::GetColorU32;
using ImGui::PushStyleColor;
using ImGui::PopStyleColor;
using ImGui::PushStyleVar;
using ImGui::PopStyleVar;
using ImGui::PushFont;
using ImGui::PopFont;
using ImGui::GetFont;
using ImGui::GetFontSize;
using ImGui::GetFontBaked;
using ImGui::GetFontTexUvWhitePixel;
using ImGui::ColorConvertFloat4ToU32;
using ImGui::ColorConvertU32ToFloat4;
// windows
using ImGui::Begin;
using ImGui::End;
using ImGui::BeginChild;
using ImGui::EndChild;
using ImGui::SetNextWindowPos;
using ImGui::SetNextWindowSize;
using ImGui::SetNextWindowSizeConstraints;
using ImGui::SetNextWindowBgAlpha;
using ImGui::GetMainViewport;
using ImGui::GetWindowDrawList;
using ImGui::GetForegroundDrawList;
using ImGui::GetBackgroundDrawList;
using ImGui::GetWindowPos;
using ImGui::GetWindowSize;
using ImGui::GetWindowWidth;
using ImGui::IsWindowAppearing;
using ImGui::GetScrollY;
using ImGui::GetScrollMaxY;
using ImGui::SetScrollHereY;
using ImGui::GetScrollX;
using ImGui::SetScrollY;
using ImGui::IsWindowHovered;
using ImGui::PushClipRect;
using ImGui::PopClipRect;
using ImGui::SetNextWindowFocus;
// layout
using ImGui::GetContentRegionAvail;
using ImGui::GetCursorScreenPos;
using ImGui::SetCursorScreenPos;
using ImGui::GetCursorPos;
using ImGui::SetCursorPos;
using ImGui::GetCursorPosX;
using ImGui::SetCursorPosX;
using ImGui::GetCursorPosY;
using ImGui::SetCursorPosY;
using ImGui::PushItemWidth;
using ImGui::PopItemWidth;
using ImGui::SetNextItemWidth;
using ImGui::CalcItemWidth;
using ImGui::PushTextWrapPos;
using ImGui::PopTextWrapPos;
using ImGui::Separator;
using ImGui::SeparatorText;
using ImGui::SameLine;
using ImGui::NewLine;
using ImGui::Spacing;
using ImGui::Dummy;
using ImGui::Indent;
using ImGui::Unindent;
using ImGui::BeginGroup;
using ImGui::EndGroup;
using ImGui::AlignTextToFramePadding;
using ImGui::GetTextLineHeight;
using ImGui::GetTextLineHeightWithSpacing;
using ImGui::GetFrameHeight;
using ImGui::GetFrameHeightWithSpacing;
using ImGui::CalcTextSize;
// ids
using ImGui::PushID;
using ImGui::PopID;
using ImGui::GetID;
using ImGui::GetStateStorage;
// widgets
using ImGui::TextUnformatted;
using ImGui::Text;
using ImGui::TextColored;
using ImGui::TextWrapped;
using ImGui::TextDisabled;
using ImGui::TextLinkOpenURL;
using ImGui::BulletText;
using ImGui::Button;
using ImGui::SmallButton;
using ImGui::InvisibleButton;
using ImGui::Checkbox;
using ImGui::RadioButton;
using ImGui::ProgressBar;
using ImGui::BeginCombo;
using ImGui::EndCombo;
using ImGui::Combo;
using ImGui::InputText;
using ImGui::InputTextMultiline;
using ImGui::InputTextWithHint;
using ImGui::InputInt;
using ImGui::InputFloat;
using ImGui::InputDouble;
using ImGui::SliderFloat;
using ImGui::SliderInt;
using ImGui::DragFloat;
using ImGui::ColorEdit4;
using ImGui::ColorEdit3;
using ImGui::ColorPicker3;
using ImGui::ColorPicker4;
using ImGui::InputScalar;
using ImGui::TreeNode;
using ImGui::TreeNodeEx;
using ImGui::TreePop;
using ImGui::CollapsingHeader;
using ImGui::SetNextItemOpen;
using ImGui::Selectable;
using ImGui::PlotLines;
using ImGui::PlotHistogram;
// menus / popups / tooltips
using ImGui::BeginMenuBar;
using ImGui::EndMenuBar;
using ImGui::BeginMainMenuBar;
using ImGui::EndMainMenuBar;
using ImGui::BeginMenu;
using ImGui::EndMenu;
using ImGui::MenuItem;
using ImGui::BeginTooltip;
using ImGui::EndTooltip;
using ImGui::SetTooltip;
using ImGui::SetItemTooltip;
using ImGui::BeginPopup;
using ImGui::BeginPopupModal;
using ImGui::EndPopup;
using ImGui::OpenPopup;
using ImGui::CloseCurrentPopup;
using ImGui::IsPopupOpen;
// tables / tabs
using ImGui::BeginTable;
using ImGui::EndTable;
using ImGui::TableNextColumn;
using ImGui::TableNextRow;
using ImGui::TableSetupColumn;
using ImGui::TableHeadersRow;
using ImGui::TableHeader;
using ImGui::TableSetBgColor;
using ImGui::TableGetRowIndex;
using ImGui::BeginTabBar;
using ImGui::EndTabBar;
using ImGui::BeginTabItem;
using ImGui::EndTabItem;
// state queries
using ImGui::BeginDisabled;
using ImGui::EndDisabled;
using ImGui::IsItemHovered;
using ImGui::IsItemActive;
using ImGui::IsItemClicked;
using ImGui::IsItemFocused;
using ImGui::IsItemEdited;
using ImGui::IsItemActivated;
using ImGui::IsItemDeactivated;
using ImGui::IsItemDeactivatedAfterEdit;
using ImGui::GetItemRectMin;
using ImGui::GetItemRectMax;
using ImGui::GetItemRectSize;
using ImGui::IsMouseDown;
using ImGui::IsMouseClicked;
using ImGui::GetMousePos;
using ImGui::SetKeyboardFocusHere;
using ImGui::SetNextItemAllowOverlap;
using ImGui::IsMouseHoveringRect;
using ImGui::IsMouseReleased;
using ImGui::IsKeyPressed;
using ImGui::IsAnyItemActive;
using ImGui::SetItemDefaultFocus;
using ImGui::GetItemID;
using ImGui::GetItemFlags;
using ImGui::PushItemFlag;
using ImGui::PopItemFlag;
} // namespace ImGui

// ------------------------------------------------------- macro replacements
export namespace imgui {

constexpr ImU32 col32(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return IM_COL32(r, g, b, a);
}

/// ImGui's `-FLT_MIN` idiom: "fill the remaining width" for item widths.
inline constexpr float fillWidth = -1.175494351e-38f;

/// IM_ARRAYSIZE replacement.
template <class T, unsigned N>
constexpr int arraySize(const T (&)[N]) { return static_cast<int>(N); }

} // namespace imgui
