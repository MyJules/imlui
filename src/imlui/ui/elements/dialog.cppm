// Dialog: the Material 3 basic dialog (a modal popup), as a typed element.
export module imlui.ui.elements.dialog;

import std;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material basic dialog: modal and centered over a scrim; it stacks its children. Non-visual.
///
/// Props:   open = false, title = "", icon = ""
/// Raw:     width (unset: 280-560dp, fitting the content), spacing, align
/// Signals: onClosed(self)
/// Two-way: open
///
/// Escape (or anything else that closes the popup) writes open = false, then emits onClosed.
class Dialog final : public Element {
public:
    static void declare(TypeBuilder<Dialog>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool open() const;
    void setOpen(bool open);
    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);
    /// Hero icon above the title ("" for none).
    [[nodiscard]] const std::string& icon() const;
    void setIcon(std::string icon);

private:
    bool m_open = false;
    std::string m_title;
    std::string m_icon;
};

} // namespace imlui

module :private;

namespace imlui {

void Dialog::declare(TypeBuilder<Dialog>& type) {
    type.property("open", &Dialog::open, &Dialog::setOpen)
        .property("title", &Dialog::title, &Dialog::setTitle)
        .property("icon", &Dialog::icon, &Dialog::setIcon)
        .signal("onClosed")
        .nonVisual();
}

void Dialog::render(Frame& frame) {
    bool open = m_open;
    const std::string title = md::scopedWindowName(m_title, "dlg");
    if (md::beginDialog(title.c_str(), &open, frame.size("width"), m_icon)) {
        frame.renderStack();
        md::endDialog();
    }
    if (open != m_open) {
        m_open = open;
        notify("open");
        if (!open) emit("onClosed");
    }
}

bool Dialog::open() const { return m_open; }
void Dialog::setOpen(bool open) { m_open = open; }
const std::string& Dialog::title() const { return m_title; }
void Dialog::setTitle(std::string title) { m_title = std::move(title); }
const std::string& Dialog::icon() const { return m_icon; }
void Dialog::setIcon(std::string icon) { m_icon = std::move(icon); }

} // namespace imlui
