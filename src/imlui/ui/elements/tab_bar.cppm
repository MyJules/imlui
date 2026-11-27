// TabBar: Material 3 tabs over the content of the current Tab child, as a typed element.
export module imlui.ui.elements.tab_bar;

import std;

import imgui;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.enum_names;
import imlui.ui.props;
import imlui.ui.elements.tab;

export namespace imlui {

/// Material tabs: the visible visual children are the tabs, and only the selected one's content renders.
/// Non-visual children (Timer, Dialog, ...) are not tabs: they render whichever tab is shown.
///
/// Props:   currentIndex = 1 (1-based), variant = "primary" ("secondary")
/// Raw:     width; the `title` and `icon` of a child that isn't a Tab
/// Signals: onCurrentChanged(self, index)
/// Two-way: currentIndex
class TabBar final : public Element {
public:
    static void declare(TypeBuilder<TabBar>& type);

    void render(Frame& frame) override;

    /// 1-based index of the shown tab among the visible ones.
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);
    [[nodiscard]] md::TabsVariant variant() const;
    void setVariant(md::TabsVariant variant);

private:
    int m_currentIndex = 1;
    md::TabsVariant m_variant = md::TabsVariant::Primary;
    /// The headers, kept from frame to frame: their strings are assigned in place, so a render allocates nothing
    /// while the titles fit. Taken while in use (moved out and back), like Row's plan.
    std::vector<md::TabInfo> m_tabs;
};

} // namespace imlui

module :private;

namespace imlui {

void TabBar::declare(TypeBuilder<TabBar>& type) {
    type.doc("Material tabs: the visible children are the tabs (normally `Tab`s; another element gives its header "
             "through raw `title` and `icon`), and only the selected one's content renders.\n"
             "Non-visual children (`Timer`, `Dialog`, ...) aren't tabs: they render whichever tab is shown. Natural "
             "size: fills the available width (raw `width`).");
    type.fillsWidth(); // an unset width fills
    type.property("currentIndex", &TabBar::currentIndex, &TabBar::setCurrentIndex)
        .doc("Two-way. The selected tab among the visible ones, 1-based (1). Out of range: the nearest tab.")
        .property("variant", &TabBar::variant, &TabBar::setVariant)
        .doc("`\"primary\"` (default: with icons, the icon over the label, 64dp) or `\"secondary\"` (a full-width "
             "indicator).")
        .signal<int>("onCurrentChanged", {"index"})
        .doc("Called when the user picks a tab, with its 1-based index.");
}

void TabBar::render(Frame& frame) {
    // Collect the tab headers: the visual children. Non-visual ones (Timer, Dialog, ...) are no tabs; they render
    // in place below, whichever tab is shown.
    std::vector<md::TabInfo> tabs = std::move(m_tabs);
    std::size_t count = 0;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) return;
        if (count == tabs.size()) tabs.emplace_back();
        md::TabInfo& info = tabs[count++];
        if (const Tab* tab = c.as<Tab>()) {
            info.label = tab->title();
            info.icon = tab->icon();
        } else { // another element used as a tab
            info.label = c.node().string("title", "Tab");
            info.icon = c.node().string("icon", "");
        }
    });
    tabs.resize(count);
    const int last = std::max(0, static_cast<int>(tabs.size()) - 1);
    int cur = std::clamp(indexValue(m_currentIndex) - 1, 0, last); // indexValue: no overflow on INT_MIN
    if (skin().tabs("##tabs", tabs, &cur, m_variant == md::TabsVariant::Secondary, frame.size("width"))) {
        m_currentIndex = cur + 1;
        notify("currentIndex");
        emit("onCurrentChanged", m_currentIndex);
    }
    ImGui::Dummy({0, frame.dp(8)});
    int i = 0;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) {
            c.render();
            return;
        }
        if (i++ != cur) return;
        c.pushId();
        frame.renderStack(c.node());
        ImGui::PopID();
    });
    m_tabs = std::move(tabs);
}

int TabBar::currentIndex() const { return m_currentIndex; }
void TabBar::setCurrentIndex(int index) { m_currentIndex = index; }
md::TabsVariant TabBar::variant() const { return m_variant; }
void TabBar::setVariant(md::TabsVariant variant) { m_variant = variant; }

} // namespace imlui
