// Row: children side by side, as a typed element.
//
// A flex layout: children that fill (Frame::Child::fillsWidth(): a fill width, a type that fills by default, wrapping
// or aligned Text, Spacer { fill }, a Column holding a fill, a Row with fills) share the width the others leave, each
// rendered in a layout box of its share. The others' widths and the heights (for `align`) are their intrinsic sizes
// (a fill's with its share), else the ones of the previous frame (Frame::Child::expectedWidth / expectedHeight,
// immediate mode); when a child renders at another size, the layout cache asks for another frame, so the row
// settles. When every child has an intrinsic size, so has the row (intrinsicSize: the same plan).
//
// A child's first frame without either size: when the row needs its width to place the others (fills share what it
// leaves, or `justify` isn't "start"), a wrong guess would shift its siblings far, so the row renders unseen for that
// frame. When only its height is unknown (`align` center or bottom), the child renders visibly at the top and the
// others are aligned among the known heights; the requested next frame moves them by a few pixels at most, which is
// far less noticeable than a row that blinks in.
export module imlui.ui.elements.row;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// Children side by side. Children that fill the width share what the others leave.
///
/// Props:   spacing = unset (ImGui item spacing), align = "center" ("top", "bottom"),
///          justify = "start" ("center", "end", "spaceBetween"; ignored while there are fills)
/// Raw:     width (unset: the available width; it bounds the children)
class Row final : public Element {
public:
    static void declare(TypeBuilder<Row>& type);

    void render(Frame& frame) override;
    /// Its children side by side (all with intrinsic sizes), as render() places them in `maxWidth`.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;
    /// It has fills, or justifies its children: its width is the available width.
    [[nodiscard]] bool contentFillsWidth(const Frame& frame) override;

    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);
    [[nodiscard]] const std::string& justify() const;
    void setJustify(std::string justify);

private:
    std::optional<double> m_spacing;
    std::string m_align = "center";
    std::string m_justify = "start";
};

} // namespace imlui

module :private;

namespace imlui {

void Row::declare(TypeBuilder<Row>& type) {
    type.doc("Children side by side, as a flex row: the children that fill (`fillWidth`, a fill-minus `width`, an "
             "element that fills by default, wrapping text, `Spacer { fill = true }`) share in equal parts the width "
             "the others leave.\n"
             "`Row { Spacer { fill = true }, Button { \"OK\" } }` right-aligns the button. Raw `width`: the row's "
             "width (unset: the available width).")
        .property("spacing", &Row::spacing, &Row::setSpacing)
        .doc("The horizontal gap between children (dp). Unset: ImGui's item spacing.")
        .property("align", &Row::align, &Row::setAlign)
        .doc("The children's vertical alignment against the tallest: `\"center\"` (default), `\"top\"` or "
             "`\"bottom\"`.")
        .property("justify", &Row::justify, &Row::setJustify)
        .doc("The horizontal distribution: `\"start\"` (default), `\"center\"`, `\"end\"` or `\"spaceBetween\"`. "
             "Ignored while the row has fills.");
}

namespace {

/// A visual child as the row sees it before rendering.
struct Slot {
    bool fills; ///< takes a share of the free width
    bool known; ///< its size is known (intrinsic; for the render also last frame's)
    float w;    ///< its expected width (a fill: the width it takes in its share, at least the share)
    float h;    ///< its expected height
};

/// Where the row puts its children: what render() and intrinsicSize() share.
struct Plan {
    std::vector<Slot> slots;
    float spacing = 0.0f;
    float share = 0.0f;    ///< each fill's width
    float offset = 0.0f;   ///< `justify` center / end: where the first child starts
    float extraGap = 0.0f; ///< `justify` spaceBetween: added to each gap
    float rowH = 0.0f;     ///< the tallest known height
    int fills = 0;
};

/// How a plan learns the children's sizes.
enum class Sizes {
    /// Intrinsic sizes only (Row::intrinsicSize): a child without one makes the plan unknown.
    Intrinsic,
    /// Intrinsic, else last frame's (render): placing by them tells the layout cache, which asks for another frame
    /// when a child then renders at another size.
    Expected,
};

/// Where `row` puts its children in a row `rowW` wide, with their heights when `heights`. nullopt only for
/// Sizes::Intrinsic, when a child has no intrinsic size. The fills are asked last, with their share.
std::optional<Plan> plan(const Row& row, const Frame& frame, float rowW, bool heights, Sizes sizes) {
    Plan plan;
    plan.spacing = row.spacing() ? frame.dp(static_cast<float>(*row.spacing())) : ImGui::GetStyle().ItemSpacing.x;
    const bool expected = sizes == Sizes::Expected;
    // A child's size with `maxWidth` available; in Sizes::Expected the reads tell the layout cache.
    const auto measure = [&](const Frame::Child& c, float maxWidth, Slot& slot) {
        const std::optional<ImVec2> size = expected ? c.expectedSize(maxWidth) : c.intrinsicSize(maxWidth);
        slot.known = size.has_value();
        if (!slot.known) {
            if (expected && !slot.fills) slot.w = c.expectedWidth(maxWidth); // 0, and the cache knows it was assumed
            return;
        }
        slot.w = expected && !slot.fills ? c.expectedWidth(maxWidth) : size->x;
        if (heights) slot.h = expected ? c.expectedHeight(maxWidth) : size->y;
    };
    bool unknown = false;
    float fixedW = 0.0f;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual() || unknown) return;
        Slot slot{c.fillsWidth(), false, 0.0f, 0.0f};
        if (slot.fills) {
            ++plan.fills;
        } else {
            measure(c, rowW, slot);
            fixedW += slot.w;
            unknown = !expected && !slot.known;
        }
        plan.slots.push_back(slot);
    });
    if (unknown) return std::nullopt;
    const int count = static_cast<int>(plan.slots.size());
    const float gaps = count > 1 ? plan.spacing * static_cast<float>(count - 1) : 0.0f;
    const float free = std::max(0.0f, rowW - fixedW - gaps);
    plan.share = plan.fills > 0 ? free / static_cast<float>(plan.fills) : 0.0f;
    if (plan.fills == 0) {
        if (row.justify() == "center")
            plan.offset = free * 0.5f;
        else if (row.justify() == "end")
            plan.offset = free;
        else if (row.justify() == "spaceBetween" && count > 1)
            plan.extraGap = free / static_cast<float>(count - 1);
    } else if (heights || !expected) {
        std::size_t i = 0;
        frame.forEachChild([&](Frame::Child& c) {
            if (!c.isVisual() || unknown || i >= plan.slots.size()) return;
            Slot& slot = plan.slots[i++];
            if (!slot.fills) return;
            measure(c, plan.share, slot);
            slot.w = std::max(plan.share, slot.w);
            unknown = !expected && !slot.known;
        });
        if (unknown) return std::nullopt;
    }
    for (const Slot& slot : plan.slots)
        if (slot.known) plan.rowH = std::max(plan.rowH, slot.h);
    return plan;
}

} // namespace

void Row::render(Frame& frame) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowW = frame.resolvedWidth(frame.available().x);
    const float rowRight = start.x + rowW;

    // Pass 1: which children fill, how wide the others are, and (to align them) how tall the tallest is: their
    // intrinsic sizes, else last frame's (Frame::Child::expectedWidth / expectedHeight).
    const bool alignsHeights = m_align == "center" || m_align == "bottom";
    const Plan plan = *imlui::plan(*this, frame, rowW, alignsHeights, Sizes::Expected);
    const std::vector<Slot>& slots = plan.slots;
    const int count = static_cast<int>(slots.size());
    bool unknownWidth = false, unknownHeight = false; // a child's first frame
    for (const Slot& s : slots) {
        unknownWidth = unknownWidth || (!s.fills && !s.known);
        unknownHeight = unknownHeight || (alignsHeights && !s.known); // it renders at the top
    }
    // What the children after each one need: their widths (the shares of fills) and the gaps before them.
    std::vector<float> after(slots.size(), 0.0f);
    for (int i = count - 2; i >= 0; --i) {
        const auto next = static_cast<std::size_t>(i + 1);
        after[static_cast<std::size_t>(i)] =
            after[next] + plan.spacing + plan.extraGap + (slots[next].fills ? 0.0f : slots[next].w);
    }
    // On a frame where a width the horizontal placement depends on is unknown (a child's first), the children render
    // unseen: measured, not drawn in the wrong place; the next frame (requested) places them. An unknown height only
    // offsets the alignment by a few pixels for a frame: the row renders seen, and asks for the frame that aligns it.
    const bool unseen = unknownWidth && (plan.fills > 0 || m_justify != "start");
    if (unknownHeight && !unseen) frame.requestFrame();

    // Pass 2: place and render. A fill renders in a box of its share; any other child's box ends where the children
    // after it start, so a fill inside it (a Column with a filling child) stops there. Aligned children start on a
    // whole pixel, like ImGui's own lines, so a stack inside one takes the size it composes.
    const float top = start.y;
    float maxH = 0.0f;
    float x = start.x + plan.offset;
    std::size_t i = 0;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) {
            c.render();
            return;
        }
        if (i >= slots.size()) return; // the children changed between the passes (a binding with side effects)
        const Slot& slot = slots[i];
        if (i > 0) x += plan.spacing + plan.extraGap;
        float y = top;
        if (alignsHeights && plan.rowH > 0 && slot.known)
            y = top + std::floor(std::max(0.0f, (plan.rowH - slot.h) * (m_align == "center" ? 0.5f : 1.0f)));
        ImGui::SetCursorScreenPos({x, y});
        const float right = slot.fills ? x + plan.share : std::max(x, rowRight - after[i]);
        bool rendered = false;
        if (unseen) {
            const layout::BoxScope box{layout::boxTo(right)};
            rendered = c.renderUnseen();
        } else {
            rendered = c.render(right);
        }
        // Not rendered (no instance, an unknown type, past maxDepth): no item was submitted, so the last item's rect
        // is the previous sibling's and would shift the ones after it. It takes no width (a fill keeps its share),
        // and an empty item ends the cursor move, which ImGui asserts on otherwise.
        if (!rendered) ImGui::Dummy({0.0f, 0.0f});
        const ImVec2 sz = rendered ? ImGui::GetItemRectSize() : ImVec2{0.0f, 0.0f};
        maxH = std::max(maxH, sz.y);
        x += slot.fills ? std::max(plan.share, sz.x) : sz.x;
        ++i;
    });
    // Make the row's extent explicit (fills may end the row).
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, x - start.x), std::max(1.0f, maxH)});
}

std::optional<ImVec2> Row::intrinsicSize(const Frame& frame, float maxWidth) {
    const std::optional<Plan> p = plan(*this, frame, frame.widthIn(maxWidth, maxWidth), true, Sizes::Intrinsic);
    if (!p) return std::nullopt;
    // render()'s pass 2 with the planned sizes: the children side by side from the offset, as tall as the tallest.
    float x = p->offset;
    for (std::size_t i = 0; i < p->slots.size(); ++i)
        x += (i > 0 ? p->spacing + p->extraGap : 0.0f) + p->slots[i].w;
    return ImVec2{std::max(1.0f, x), std::max(1.0f, p->rowH)};
}

bool Row::contentFillsWidth(const Frame& frame) {
    // Justified children spread over the available width, and fills take what the others leave.
    if (m_justify != "start") return true;
    bool fills = false;
    frame.forEachChild([&](Frame::Child& c) { fills = fills || (c.isVisual() && c.fillsWidth()); });
    return fills;
}

std::optional<double> Row::spacing() const { return m_spacing; }
void Row::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Row::align() const { return m_align; }
void Row::setAlign(std::string align) { m_align = std::move(align); }
const std::string& Row::justify() const { return m_justify; }
void Row::setJustify(std::string justify) { m_justify = std::move(justify); }

} // namespace imlui
