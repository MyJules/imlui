// Dialog: the Material 3 basic dialog (a modal popup), as a typed element.
export module imlui.ui.elements.dialog;

import std;

import imgui;
import imlui.widgets;
import imlui.skin;
import imlui.ui.frame_request;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;
import imlui.ui.enum_names;

export namespace imlui {

/// The Material basic dialog: modal and centered over a scrim; it stacks its children. Non-visual.
///
/// Props:   open = false, title = "", icon = "", align = "start" ("center", "end"; the children's, StackAlign)
/// Raw:     width (unset: 280-560dp, fitting the content; a fill inside is the minimum or the title's width),
///          spacing, cull
/// Signals: onClosed(self)
/// Two-way: open
///
/// Escape (or anything else that closes the popup) writes open = false, then emits onClosed.
///
/// ImGui shows one popup per level, so of sibling dialogs (at the same popup level) open at the same time, one is
/// shown: a dialog whose `open` turns true while another is up waits, and opens the frame after that one closes.
/// A dialog opened from inside another (one of its children) is a level up and shows over it.
class Dialog final : public Element, public StackAlign {
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
    /// Open but waiting for a sibling dialog to close (see above).
    bool m_waiting = false;
    /// The popup's window name (md::scopedWindowName: the title, and an ID from the element's scope), made again
    /// only when the title or the scope (m_nameScope, the ID it hashes) changed: a dialog renders, closed too, on
    /// every frame, and formatting the name allocates.
    std::string m_name;
    std::string m_nameTitle;
    ImGuiID m_nameScope = 0;
};

} // namespace imlui

module :private;

namespace imlui {

void Dialog::declare(TypeBuilder<Dialog>& type) {
    type.doc("A Material basic dialog: modal, centered over a scrim, with `extraLarge` corners and a `headlineSmall` "
             "title; Escape closes it. It stacks its children. Non-visual.\n"
             "Raw `width`: a fixed width; unset, 280-560dp fitting the content. A dialog opened from another's content "
             "shows above it; of sibling dialogs one shows at a time (the others wait, still `open`).")
        .property("open", &Dialog::open, &Dialog::setOpen)
        .doc("Two-way. Shows the dialog (`false` by default). Closing it (Escape) writes `false`.")
        .property("title", &Dialog::title, &Dialog::setTitle)
        .doc("The headline; `\"\"` for none.")
        .property("icon", &Dialog::icon, &Dialog::setIcon)
        .doc("A Material icon name shown above the title, which is then centered; `\"\"` for none.")
        .signal<>("onClosed")
        .doc("Called when the dialog closes (after `open = false` is written back).")
        .nonVisual();
    stackProps(type);
}

namespace {

/// Whether a modal popup other than `id` (another Dialog) is shown at the popup level a popup begun now would
/// open at. ImGui keeps one popup per level: OpenPopup() there would close it, and its next OpenPopup() ours.
/// A popup that wasn't shown last frame nor this one (its Dialog left the tree while open) doesn't count.
bool otherModalAtThisLevel(ImGuiID id) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const int level = g.BeginPopupStack.Size;
    if (level >= g.OpenPopupStack.Size) return false;
    const ImGuiPopupData& open = g.OpenPopupStack[level];
    const ImGuiWindow* w = open.Window;
    return open.PopupId != id && w && (w->Flags & ImGuiWindowFlags_Modal) != 0 && (w->Active || w->WasActive);
}

} // namespace

void Dialog::render(Frame& frame) {
    bool open = m_open;
    if (const ImGuiID scope = ImGui::GetID("dlg"); m_name.empty() || scope != m_nameScope || m_title != m_nameTitle) {
        m_name = md::scopedWindowName(m_title, "dlg"); // its ID part is this same GetID("dlg")
        m_nameScope = scope;
        m_nameTitle = m_title;
    }
    const std::string& title = m_name;
    if (!open) {
        m_waiting = false;
    } else if (!ImGui::IsPopupOpen(title.c_str())) {
        if (otherModalAtThisLevel(ImGui::GetID(title.c_str()))) {
            m_waiting = true;
            return;
        }
        if (m_waiting) {
            // The other dialog closed this frame, maybe on an Escape press this one would see too: open on the next.
            m_waiting = false;
            requestFrame();
            return;
        }
    }
    if (skin().beginDialog(title.c_str(), &open, frame.size("width"), m_icon)) {
        frame.renderStack(); // in the dialog's shrink-to-fit box (Skin::beginDialog)
        skin().endDialog();
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
