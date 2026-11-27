// Box: children layered over each other, each placed by its own `anchor` and margins, as a typed element.
//
// The children share the box's area instead of stacking: each is placed in it by its `anchor` (a corner, an edge's
// middle, or the center) inset by its margins, at the size it will render at (its intrinsic size, else last frame's,
// like a Row's children). Later children draw over earlier ones and take the mouse first.
//
// Layers: the first visual child renders inline, in the box's window. Every later one renders in a child window of
// its own (transparent, as large as the child), because ImGui draws a window's child windows after the window's own
// content: a badge drawn inline after a Card (a child window) would show under the card. Sibling child windows draw
// in the order they were begun, and ImGui sends the mouse to the topmost window under it, so a layer is above the
// ones before it for drawing and input alike. Inside a layer's window the box's ID seed is pushed again, so the
// child's ImGui IDs (and its key in the layout cache) are the ones it would have inline.
//
// A child whose size isn't known yet (its first frame, without an intrinsic size) renders unseen, inline, and the
// next frame (requested) places it.
export module imlui.ui.elements.box;

import std;

import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.item_size;
import imlui.ui.layout_box;

export namespace imlui {

/// Where a Box child sits in the box: a corner, the middle of an edge, or the center.
enum class Anchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

/// A Box child's anchor and margins, read from its own table (`anchor`, `margin`, `marginLeft`, ...).
struct BoxPlacement {
    Anchor anchor = Anchor::TopLeft;
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f; ///< pixels
};

/// The anchor named `name` ("topLeft", "center", ...), or nullopt.
[[nodiscard]] std::optional<Anchor> anchorNamed(std::string_view name);

/// Children layered over each other, each placed by its `anchor` and margins.
///
/// Props:   (none of its own)
/// Raw:     width (unset: fills the available width), height (fixed; -1 fills the height left below; unset: the
///          tallest child with its margins)
/// Child:   anchor = "topLeft" ("top", "topRight", "left", "center", "right", "bottomLeft", "bottom",
///          "bottomRight"), margin = 0 (dp on every side), marginLeft, marginTop, marginRight, marginBottom (each
///          overrides `margin` on its side)
class Box final : public Element {
public:
    static void declare(TypeBuilder<Box>& type);

    void render(Frame& frame) override;
    /// Its width (the available width unless set), and its height: fixed, or the tallest child with its margins when
    /// every visual child has an intrinsic size. None for a fill height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr std::pair<Anchor, std::string_view> anchorNames[] = {
    {Anchor::TopLeft, "topLeft"},       {Anchor::Top, "top"},       {Anchor::TopRight, "topRight"},
    {Anchor::Left, "left"},             {Anchor::Center, "center"}, {Anchor::Right, "right"},
    {Anchor::BottomLeft, "bottomLeft"}, {Anchor::Bottom, "bottom"}, {Anchor::BottomRight, "bottomRight"},
};

/// The share of the free space before the child, across (x) and down (y): 0, 0.5 or 1.
ImVec2 anchorFactors(Anchor a) {
    const auto i = static_cast<int>(a);
    return {static_cast<float>(i % 3) * 0.5f, static_cast<float>(i / 3) * 0.5f};
}

BoxPlacement placementOf(const Frame& frame, const luaui::Node& child) {
    BoxPlacement p;
    if (child.has("anchor")) {
        const std::string name = child.string("anchor");
        if (const std::optional<Anchor> a = anchorNamed(name))
            p.anchor = *a;
        else
            luaui::reportError("Box: unknown anchor '" + name +
                               "' (topLeft, top, topRight, left, center, right, bottomLeft, bottom, bottomRight)");
    }
    const float all = child.numf("margin", 0.0f);
    const auto side = [&](const char* key) { return std::max(0.0f, frame.dp(child.has(key) ? child.numf(key, 0.0f) : all)); };
    p.left = side("marginLeft");
    p.top = side("marginTop");
    p.right = side("marginRight");
    p.bottom = side("marginBottom");
    return p;
}

/// The box's height prop in pixels: > 0 fixed, < 0 a fill, 0 automatic.
float heightProp(const Frame& frame) { return frame.size("height"); }

} // namespace

std::optional<Anchor> anchorNamed(std::string_view name) {
    for (const auto& [anchor, n] : anchorNames)
        if (n == name) return anchor;
    return std::nullopt;
}

void Box::declare(TypeBuilder<Box>& type) {
    type.doc("Children layered over each other instead of stacked: each is placed in the box by its own `anchor` "
             "(`\"topLeft\"` by default, `\"center\"`, `\"bottomRight\"`, ...) and margins (`margin`, `marginLeft`, "
             "`marginTop`, `marginRight`, `marginBottom`, in dp). Later children draw over earlier ones and take the "
             "mouse first.\n"
             "Raw `width`: unset, the box fills the available width. Raw `height`: fixed, `-1` for the height left "
             "below, or unset for the tallest child with its margins.\n"
             "`Box { height = -1, Text { \"Hello\", anchor = \"center\" } }` centers the text in what is left of the "
             "window.")
        .fillsWidth();
}

void Box::render(Frame& frame) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float boxW = frame.resolvedWidth(0.0f); // the type fills by default: unset is the available width
    const float hp = heightProp(frame);
    const bool autoHeight = hp == 0.0f;

    // The height first: fixed, a fill, or the tallest child with its margins (expected sizes, read before rendering:
    // the layout cache gets another frame when one was stale).
    float boxH = autoHeight ? 0.0f : frame.resolvedHeight(0.0f);
    if (autoHeight) {
        frame.forEachChild([&](Frame::Child& c) {
            if (!c.isVisual()) return;
            const BoxPlacement p = placementOf(frame, c.node());
            const float areaW = std::max(0.0f, boxW - p.left - p.right);
            if (const std::optional<ImVec2> s = c.expectedSize(areaW)) boxH = std::max(boxH, s->y + p.top + p.bottom);
        });
    }

    const ImGuiChildFlags childFlags = ImGuiChildFlags_NavFlattened;
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
    int layer = 0;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) {
            c.render();
            return;
        }
        const BoxPlacement p = placementOf(frame, c.node());
        const float areaX = start.x + p.left, areaY = start.y + p.top;
        const float areaW = std::max(0.0f, boxW - p.left - p.right);
        // Without a height of its own, the area has no bottom: a fill height inside reaches the window's.
        const std::optional<float> areaBottom =
            autoHeight ? std::nullopt : std::optional<float>{std::max(areaY, start.y + boxH - p.bottom)};
        const float areaH = areaBottom ? *areaBottom - areaY : std::max(0.0f, boxH - p.top - p.bottom);

        const std::optional<ImVec2> size = c.expectedSize(areaW);
        layout::Box area = layout::boxTo(areaX + areaW);
        area.bottom = areaBottom;
        if (!size) {
            // Its first frame: measured unseen at the area's top-left, placed on the next (requested) frame.
            ImGui::SetCursorScreenPos({areaX, areaY});
            const layout::BoxScope box{area};
            c.renderUnseen();
            ++layer;
            return;
        }
        const ImVec2 f = anchorFactors(p.anchor);
        const ImVec2 pos{areaX + std::floor(std::max(0.0f, areaW - size->x) * f.x),
                         areaY + std::floor(std::max(0.0f, areaH - size->y) * f.y)};
        ImGui::SetCursorScreenPos(pos);
        if (layer++ == 0) {
            const layout::BoxScope box{area};
            if (!c.render()) ImGui::Dummy({0.0f, 0.0f});
            return;
        }
        // A later layer: in a child window of its own, as large as the child, so it draws (and is hovered) above
        // the layers before it, child windows among them. The box's ID seed inside keeps the child's IDs.
        const ImGuiID seed = imgui::currentIdSeed();
        ImGui::PushOverrideID(c.id());
        const ImGuiID windowId = ImGui::GetID("##imlui.box.layer");
        ImGui::PopID();
        // What the child draws past its rect (an outline stroked across its fractional edge: ImGui truncates a child
        // window's size; a focus ring, a shadow) isn't cut at the window's edge: its clip rect reaches `bleed` further,
        // within the box window's own clip. The window itself stays as large as the child, as it takes the mouse.
        const ImDrawList* outer = ImGui::GetWindowDrawList();
        const ImVec2 outerMin = outer->GetClipRectMin(), outerMax = outer->GetClipRectMax();
        const float bleed = frame.dp(16.0f);
        const ImVec2 windowSize{std::max(1.0f, std::ceil(size->x)), std::max(1.0f, std::ceil(size->y))};
        if (ImGui::BeginChild(windowId, windowSize, childFlags, windowFlags)) {
            ImGui::PushClipRect({std::max(outerMin.x, pos.x - bleed), std::max(outerMin.y, pos.y - bleed)},
                                {std::min(outerMax.x, pos.x + windowSize.x + bleed),
                                 std::min(outerMax.y, pos.y + windowSize.y + bleed)},
                                false);
            ImGui::PushOverrideID(seed);
            {
                const layout::BoxScope box{area};
                if (!c.render()) ImGui::Dummy({0.0f, 0.0f});
            }
            ImGui::PopID();
            ImGui::PopClipRect();
        }
        ImGui::EndChild();
    });

    // The box's extent: the layers overlap, so the cursor goes back to its top-left.
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, boxW), std::max(1.0f, boxH)});
}

std::optional<ImVec2> Box::intrinsicSize(const Frame& frame, float maxWidth) {
    const float hp = heightProp(frame);
    if (hp < 0.0f) return std::nullopt; // a fill height depends on where it renders
    const float boxW = frame.widthIn(maxWidth, 0.0f);
    float boxH = hp;
    bool known = true;
    if (hp == 0.0f) {
        frame.forEachChild([&](Frame::Child& c) {
            if (!c.isVisual() || !known) return;
            const BoxPlacement p = placementOf(frame, c.node());
            const std::optional<ImVec2> s = c.intrinsicSize(std::max(0.0f, boxW - p.left - p.right));
            if (!s) {
                known = false;
                return;
            }
            boxH = std::max(boxH, s->y + p.top + p.bottom);
        });
    }
    if (!known) return std::nullopt;
    return ImVec2{std::max(1.0f, boxW), std::max(1.0f, boxH)};
}

} // namespace imlui
