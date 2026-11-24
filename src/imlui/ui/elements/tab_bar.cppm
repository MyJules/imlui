// TabBar: Material 3 tabs over the content of the current Tab child, as a typed element.
export module imlui.ui.elements.tab_bar;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
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
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);

private:
    int m_currentIndex = 1;
    std::string m_variant = "primary";
};

} // namespace imlui

module :private;

namespace imlui {

void TabBar::declare(TypeBuilder<TabBar>& type) {
    type.fillsWidth(); // an unset width fills
    type.property("currentIndex", &TabBar::currentIndex, &TabBar::setCurrentIndex)
        .property("variant", &TabBar::variant, &TabBar::setVariant)
        .signal("onCurrentChanged");
}

void TabBar::render(Frame& frame) {
    // Collect the tab headers: the visual children. Non-visual ones (Timer, Dialog, ...) are no tabs; they render
    // in place below, whichever tab is shown.
    std::vector<md::TabInfo> tabs;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) return;
        if (const Tab* tab = c.as<Tab>())
            tabs.push_back({tab->title(), tab->icon()});
        else // another element used as a tab
            tabs.push_back({c.node().string("title", "Tab"), c.node().string("icon", "")});
    });
    const int last = std::max(0, static_cast<int>(tabs.size()) - 1);
    int cur = std::clamp(indexValue(m_currentIndex) - 1, 0, last); // indexValue: no overflow on INT_MIN
    if (md::tabs("##tabs", tabs, &cur, m_variant == "secondary", frame.size("width"))) {
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
        ImGui::PushID(c.node().ptr());
        frame.renderStack(c.node());
        ImGui::PopID();
    });
}

int TabBar::currentIndex() const { return m_currentIndex; }
void TabBar::setCurrentIndex(int index) { m_currentIndex = index; }
const std::string& TabBar::variant() const { return m_variant; }
void TabBar::setVariant(std::string variant) { m_variant = std::move(variant); }

} // namespace imlui
