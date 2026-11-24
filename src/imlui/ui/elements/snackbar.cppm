// Snackbar: the Material 3 snackbar, as a typed element. Normally there is one, driven by
// ui.snackbar() and rendered by the engine as an overlay above everything (ui.__overlays).
module;

#include <algorithm>
#include <string>
#include <utility>

export module imlui.ui.elements.snackbar;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material snackbar: the app-wide overlay behind ui.snackbar(). Non-visual.
///
/// Props:   text = "", actionText = "", shownAt = -1 (ImGui time in s; < 0 hidden), duration = 4000 (ms),
///          closable = false
/// Signals: onAction(self)
/// Two-way: shownAt
///
/// It slides in at `shownAt`, fades out after `duration` and then writes shownAt = -1. The action or the close
/// icon start the fade-out (they move `shownAt` back).
class Snackbar final : public Element {
public:
    static void declare(TypeBuilder<Snackbar>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& actionText() const;
    void setActionText(std::string actionText);
    [[nodiscard]] double shownAt() const; ///< s
    void setShownAt(double shownAt);
    [[nodiscard]] double duration() const; ///< ms
    void setDuration(double duration);
    [[nodiscard]] bool closable() const;
    void setClosable(bool closable);

private:
    std::string m_text;
    std::string m_actionText;
    double m_shownAt = -1.0;
    double m_duration = 4000.0;
    bool m_closable = false;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr double slideInSeconds = 0.18;
constexpr double fadeOutSeconds = 0.15;

} // namespace

void Snackbar::declare(TypeBuilder<Snackbar>& type) {
    type.property("text", &Snackbar::text, &Snackbar::setText)
        .property("actionText", &Snackbar::actionText, &Snackbar::setActionText)
        .property("shownAt", &Snackbar::shownAt, &Snackbar::setShownAt)
        .property("duration", &Snackbar::duration, &Snackbar::setDuration)
        .property("closable", &Snackbar::closable, &Snackbar::setClosable)
        .signal("onAction")
        .nonVisual();
}

void Snackbar::render(Frame& frame) {
    if (m_shownAt < 0) return;
    const double now = ImGui::GetTime();
    const double dur = m_duration / 1000.0;
    const double age = now - m_shownAt;
    float t = static_cast<float>(std::min(1.0, age / slideInSeconds));
    if (age > dur) t = static_cast<float>(std::max(0.0, 1.0 - (age - dur) / fadeOutSeconds));
    if (age > dur + fadeOutSeconds) {
        m_shownAt = -1.0;
        notify("shownAt");
        return;
    }
    // Animate the slide-in and the fade-out; in between, wake up when the fade starts.
    if (age < slideInSeconds || age > dur)
        frame.requestFrame();
    else
        frame.requestFrameAfter(dur - age);
    bool closed = false;
    if (md::snackbar(m_text, m_actionText, t, m_closable, &closed)) {
        emit("onAction");
        closed = true;
    }
    if (closed) {
        // fade out now
        m_shownAt = now - dur;
        notify("shownAt");
    }
}

const std::string& Snackbar::text() const { return m_text; }
void Snackbar::setText(std::string text) { m_text = std::move(text); }
const std::string& Snackbar::actionText() const { return m_actionText; }
void Snackbar::setActionText(std::string actionText) { m_actionText = std::move(actionText); }
double Snackbar::shownAt() const { return m_shownAt; }
void Snackbar::setShownAt(double shownAt) { m_shownAt = shownAt; }
double Snackbar::duration() const { return m_duration; }
void Snackbar::setDuration(double duration) { m_duration = duration; }
bool Snackbar::closable() const { return m_closable; }
void Snackbar::setClosable(bool closable) { m_closable = closable; }

} // namespace imlui
