// Buttons and selection controls: Button, IconButton, Checkbox, Switch,
// RadioButton, RadioGroup, Chip, SegmentedButton, ListItem.
module;

#include <cstddef>
#include <string>

export module imlui.ui.elements.controls;

import imgui;
import imlui.theme.dpi;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

void registerControlElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void button(Renderer& r, const Node& n) {
    std::string t = labelId(n.text(), "btn");
    bool clicked = md::button(t.c_str(), md::buttonVariant(n.string("variant", "filled")),
                              {sizeProp(n, "width"), sizeProp(n, "height")}, n.string("icon", ""));
    if (clicked) n.emit("onClicked");
    r.attachMenu(n, clicked);
}

void iconButton(Renderer& r, const Node& n) {
    const bool checkable = n.has("checked");
    const bool checked = n.boolean("checked", false);
    std::string icon = n.string("icon", n.text());
    if (checkable && checked && n.has("checkedIcon")) icon = n.string("checkedIcon", icon);
    bool clicked = md::iconButton("##ib", icon, md::iconButtonVariant(n.string("variant", "standard")), checkable,
                                  checked, n.color("color"));
    if (clicked) {
        if (checkable) n.set("checked", !checked);
        n.emit("onClicked");
    }
    r.attachMenu(n, clicked);
}

void checkbox(Renderer&, const Node& n) {
    std::string t = labelId(n.text(), "cb");
    bool v = n.boolean("checked", false);
    if (md::checkbox(t.c_str(), &v)) {
        n.set("checked", v);
        n.emit("onToggled", {v});
    }
}

void switchToggle(Renderer&, const Node& n) {
    std::string t = labelId(n.text(), "sw");
    bool v = n.boolean("checked", false);
    if (md::switchToggle(t.c_str(), &v, n.boolean("icons", true))) {
        n.set("checked", v);
        n.emit("onToggled", {v});
    }
}

void radioButton(Renderer&, const Node& n) {
    std::string t = labelId(n.text(), "rb");
    if (md::radio(t.c_str(), n.boolean("checked", false))) {
        n.set("checked", true);
        n.emit("onClicked");
    }
}

void radioGroup(Renderer&, const Node& n) {
    auto items = n.strings("model");
    int cur = static_cast<int>(n.number("currentIndex", 1));
    const bool horizontal = n.boolean("horizontal", false);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0});
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (horizontal && i > 0) ImGui::SameLine(0, dp(16));
        ImGui::PushID(i);
        std::string t = labelId(items[static_cast<std::size_t>(i)], "r");
        if (md::radio(t.c_str(), cur == i + 1) && cur != i + 1) {
            n.set("currentIndex", static_cast<double>(i + 1));
            n.emit("onActivated", {static_cast<double>(i + 1), items[static_cast<std::size_t>(i)]});
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

void chip(Renderer&, const Node& n) {
    std::string t = labelId(n.text(), "chip");
    bool v = n.boolean("checked", false);
    auto variant = md::chipVariant(n.string("variant", "filter"));
    if (md::chip(t.c_str(), v, variant, n.string("icon", ""))) {
        if (variant == md::ChipVariant::Filter && n.boolean("checkable", true)) n.set("checked", !v);
        n.emit("onClicked", {!v});
    }
}

void segmentedButton(Renderer&, const Node& n) {
    auto items = n.strings("model");
    auto icons = n.strings("icons");
    int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
    if (md::segmented("##seg", items, &cur, sizeProp(n, "width"), icons)) {
        n.set("currentIndex", static_cast<double>(cur + 1));
        n.emit("onActivated", {static_cast<double>(cur + 1), items[static_cast<std::size_t>(cur)]});
    }
}

void listItem(Renderer&, const Node& n) {
    std::string headline = n.text(), supporting = n.string("supportingText", ""), lead = n.string("icon", ""),
                tt = n.string("trailingText", ""), ti = n.string("trailingIcon", "");
    md::ListItemOptions o;
    o.headline = headline;
    o.supporting = supporting;
    o.leadingIcon = lead;
    o.trailingText = tt;
    o.trailingIcon = ti;
    o.selected = n.boolean("selected", false);
    o.width = sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width");
    if (md::listItem("##li", o)) n.emit("onClicked");
}

} // namespace

void registerControlElements(Renderer& renderer) {
    renderer.registerElement("Button", button);
    renderer.registerElement("IconButton", iconButton);
    renderer.registerElement("Checkbox", checkbox);
    renderer.registerElement("Switch", switchToggle);
    renderer.registerElement("RadioButton", radioButton);
    renderer.registerElement("RadioGroup", radioGroup);
    renderer.registerElement("Chip", chip);
    renderer.registerElement("SegmentedButton", segmentedButton);
    renderer.registerElement("ListItem", listItem);
}

} // namespace imlui
