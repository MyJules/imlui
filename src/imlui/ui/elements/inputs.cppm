// Value inputs: Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker.
module;

#include <cstddef>
#include <string>

export module imlui.ui.elements.inputs;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;
import imlui.types.registry;

export namespace imlui {

void registerInputElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerInputTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void slider(Renderer&, const Node& n) {
    float v = n.numf("value", 0.0f);
    float from = n.numf("from", 0.0f), to = n.numf("to", 1.0f), step = n.numf("stepSize", 0.0f);
    std::string fmt = n.string("format", step >= 1.0f ? "%.0f" : "%.2f");
    if (md::slider("##slider", &v, from, to, step, sizeProp(n, "width"), fmt.c_str())) {
        n.set("value", static_cast<double>(v));
        n.emit("onMoved", {static_cast<double>(v)});
    }
    if (ImGui::IsItemDeactivated()) n.emit("onReleased", {static_cast<double>(v)});
}

void spinBox(Renderer&, const Node& n) {
    double v = n.number("value", 0);
    double from = n.number("from", -1e9), to = n.number("to", 1e9), step = n.number("stepSize", 1);
    if (md::spinBox("##spin", &v, from, to, step, static_cast<int>(n.number("decimals", 0)), n.string("label", ""),
                    sizeProp(n, "width"), n.string("variant", "filled") == "outlined")) {
        n.set("value", v);
        n.emit("onValueModified", {v});
    }
}

void textField(const Node& n, bool multiline) {
    std::string text = n.string("text", "");
    std::string lbl = n.string("label", ""), ph = n.string("placeholderText", ""), li = n.string("leadingIcon", ""),
                ti = n.string("trailingIcon", ""), sup = n.string("supportingText", "");
    md::TextFieldOptions o;
    o.label = lbl;
    o.placeholder = ph;
    o.leadingIcon = li;
    o.trailingIcon = ti;
    o.supporting = sup;
    o.outlined = n.string("variant", "filled") == "outlined";
    o.password = n.boolean("password", false) || n.string("echoMode", "") == "password";
    o.multiline = multiline;
    o.readOnly = n.boolean("readOnly", false);
    o.error = n.boolean("error", false);
    o.width = sizeProp(n, "width");
    o.height = sizeProp(n, "height");
    auto res = md::textField("##tf", text, o);
    if (res.changed) {
        n.set("text", text);
        n.emit("onTextChanged", {text});
    }
    if (res.accepted) n.emit("onAccepted", {text});
    if (res.trailingClicked) n.emit("onTrailingClicked");
}

void comboBox(Renderer&, const Node& n) {
    auto items = n.strings("model");
    auto icons = n.strings("icons");
    int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
    std::string lbl = n.string("label", ""), li = n.string("leadingIcon", ""), sup = n.string("supportingText", "");
    md::DropdownOptions o;
    o.label = lbl;
    o.leadingIcon = li;
    o.supporting = sup;
    o.outlined = n.string("variant", "filled") == "outlined";
    o.error = n.boolean("error", false);
    o.width = sizeProp(n, "width");
    if (md::dropdown("##dd", items, &cur, o, icons)) {
        n.set("currentIndex", static_cast<double>(cur + 1));
        n.emit("onActivated", {static_cast<double>(cur + 1), items[static_cast<std::size_t>(cur)]});
    }
}

void colorPicker(Renderer&, const Node& n) {
    auto c = n.color("color").value_or(Color{1, 1, 1, 1});
    if (md::colorField("##cf", &c, n.string("label", ""), n.boolean("alpha", false), sizeProp(n, "width"))) {
        std::string hex = toHex(c);
        n.set("color", hex);
        n.emit("onColorChanged", {hex});
    }
}

} // namespace

void registerInputElements(Renderer& renderer) {
    renderer.registerElement("Slider", slider);
    renderer.registerElement("SpinBox", spinBox);
    renderer.registerElement("TextField", [](Renderer&, const Node& n) { textField(n, false); });
    renderer.registerElement("TextArea", [](Renderer&, const Node& n) { textField(n, true); });
    renderer.registerElement("ComboBox", comboBox);
    renderer.registerElement("ColorPicker", colorPicker);
}

void registerInputTypes(TypeRegistry&) {}

} // namespace imlui
