// NavigationScope: the navigation context NavigationItem reads to pick its style.
//
// NavigationItem is the one destination element for every navigation container. The container opens a
// NavigationScope (style and destination width) while it renders its children, and NavigationItem draws itself
// in that style: a drawer pill by default, a bar or rail destination inside a NavigationBar or NavigationRail.
// The scope applies to everything the container renders, Repeater delegates included, and the outer context
// comes back when it ends. The rail header and the modal drawer's sheet open a default scope (drawer style).
// Main thread only, like rendering.
export module imlui.ui.navigation_scope;

export namespace imlui {

class NavigationScope {
public:
    enum class Style { Drawer, Bar, Rail };

    /// How NavigationItems render right now.
    struct Context {
        Style style = Style::Drawer;
        float itemWidth = 0.0f; ///< pixels; bar and rail destinations take the width their container gives them
    };

    /// Makes `context` current for the scope's lifetime and restores the outer one afterwards.
    explicit NavigationScope(Context context);
    ~NavigationScope();
    NavigationScope(const NavigationScope&) = delete;
    NavigationScope& operator=(const NavigationScope&) = delete;
    NavigationScope(NavigationScope&&) = delete;
    NavigationScope& operator=(NavigationScope&&) = delete;

    /// The innermost open scope's context (drawer style outside any scope).
    [[nodiscard]] static const Context& current();

private:
    Context m_saved;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

NavigationScope::Context currentContext;

} // namespace

NavigationScope::NavigationScope(Context context) : m_saved{currentContext} { currentContext = context; }
NavigationScope::~NavigationScope() { currentContext = m_saved; }

const NavigationScope::Context& NavigationScope::current() { return currentContext; }

} // namespace imlui
