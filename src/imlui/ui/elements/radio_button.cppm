// RadioButton: the Material 3 radio button with an optional label, as a typed element.
// Buttons sharing a `group` value act as one group: src/resources/imlui.lua wraps the constructor and puts the group's
// member set in `__radioGroup`; a click unchecks the other members through their bindings.
export module imlui.ui.elements.radio_button;

import std;

import imgui;
import lua;
import imlui.widgets;
import imlui.lua.node;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.types.type_info;

export namespace imlui {

/// A Material radio button with a label. Clicking it sets `checked`; RadioButtons with the same Lua-side `group`
/// uncheck each other.
///
/// Props:   text = "" (positional), checked = false
/// Raw:     group (read by the Lua constructor)
/// Signals: onClicked(self)
/// Two-way: checked
class RadioButton final : public Element {
public:
    static void declare(TypeBuilder<RadioButton>& type);

    void render(Frame& frame) override;
    /// The ring and its label.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);

private:
    std::string m_text;
    bool m_checked = false;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

// Unchecks the other members of the button's `group`. src/resources/imlui.lua puts the
// group's member set (a weak-keyed table) in `__radioGroup`.
void uncheckRadioPeers(const luaui::Node& n) {
    lua::State* state = n.state();
    const int base = lua::gettop(state);
    if (n.pushRaw("__radioGroup") == lua::Type::Table) {
        // Collect first: Node::set may run Lua (bindings) that changes the group.
        const int group = lua::gettop(state);
        lua::pushnil(state);
        while (lua::next(state, group) != 0) {
            lua::pop(state); // value; the key stays for lua::next
            // A copy of the key becomes next()'s cursor, leaving the original collected below it.
            if (lua::istable(state, -1) && lua::topointer(state, -1) != n.ptr() && lua::checkstack(state, 2))
                lua::pushvalue(state, -1);
        }
        for (int i = group + 1; i <= lua::gettop(state); ++i)
            luaui::Node{state, i}.set("checked", false);
    }
    lua::settop(state, base);
}

} // namespace

void RadioButton::declare(TypeBuilder<RadioButton>& type) {
    type.doc("A Material 3 radio button with an optional label. A click sets `checked` to `true` (two-way); "
             "only a `group` sets it back to `false`. Or derive `checked` from state and set that state in "
             "`onClicked`.\n"
             "`RadioButton { \"Small\" }` sets `text`. Natural size: the ring and its label, 40dp high.")
        .property("text", &RadioButton::text, &RadioButton::setText)
        .doc("The label. The positional value: `RadioButton { \"Small\" }`.")
        .property("checked", &RadioButton::checked, &RadioButton::setChecked)
        .doc("**Two-way.** Set to `true` when clicked; a click on another button of the `group` sets it to `false`.")
        .defaultProperty("text")
        .signal<>("onClicked")
        .doc("Called when the button is clicked (after `checked` and the group were updated).");
    type.rawProperty("group", "string|number|boolean|table", RawRead::ValueOnly)
        .doc("Lua side, read when the element is created. The RadioButtons with the same `group` value act as one: a "
             "click writes `checked = false` to the others (through their `bind`s; a function binding is left alone).");
}

void RadioButton::render(Frame& frame) {
    const std::string label = Frame::labelId(m_text, "rb");
    if (md::radio(label.c_str(), m_checked)) {
        m_checked = true;
        notify("checked");
        uncheckRadioPeers(frame.node());
        emit("onClicked");
    }
}

const std::string& RadioButton::text() const { return m_text; }
void RadioButton::setText(std::string text) { m_text = std::move(text); }
bool RadioButton::checked() const { return m_checked; }
void RadioButton::setChecked(bool checked) { m_checked = checked; }

std::optional<ImVec2> RadioButton::intrinsicSize(const Frame&, float) {
    return md::radioSize(Frame::labelId(m_text, "rb").c_str());
}

} // namespace imlui
