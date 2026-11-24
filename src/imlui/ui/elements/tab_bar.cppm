// TabBar: Material 3 tabs over the content of the current Tab child, as a typed element (TypeBuilder).
module;

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

export module imlui.ui.elements.tab_bar;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.tab;

export namespace imlui {

/// TabBar { currentIndex = 1 (two-way), variant = "primary" | "secondary", width = fill,
///          onCurrentChanged = function(self, index), Tab { ... }, ... }
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
    type.property("currentIndex", &TabBar::currentIndex, &TabBar::setCurrentIndex)
        .property("variant", &TabBar::variant, &TabBar::setVariant)
        .signal("onCurrentChanged");
}

void TabBar::render(Frame& frame) {
    // Collect the tab headers.
    std::vector<md::TabInfo> tabs;
    frame.forEachChild([&](Frame::Child& c) {
        if (const Tab* tab = c.as<Tab>())
            tabs.push_back({tab->title(), tab->icon()});
        else // another element used as a tab
            tabs.push_back({c.node().string("title", "Tab"), c.node().string("icon", "")});
    });
    int cur = std::clamp(m_currentIndex - 1, 0, std::max(0, static_cast<int>(tabs.size()) - 1));
    if (md::tabs("##tabs", tabs, &cur, m_variant == "secondary", frame.size("width", -1.0f))) {
        m_currentIndex = cur + 1;
        notify("currentIndex");
        emit("onCurrentChanged", m_currentIndex);
    }
    ImGui::Dummy({0, frame.dp(8)});
    int i = 0;
    frame.forEachChild([&](Frame::Child& c) {
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
