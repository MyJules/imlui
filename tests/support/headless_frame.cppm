// HeadlessFrame: a test harness that renders imlui elements in real ImGui frames without a window, a GPU or
// an ImGui backend. Test-only (built into imlui_tests).
//
// It owns an ImGui context, a Renderer, a TypeRegistry and, once started, a Lua state set up like the
// Engine's: the registered types installed, the real `imlui.native` (with empty host hooks) and painter
// libraries, and lua/imlui.lua loaded. Lua code runs in the UI script environment (ui.env), so element
// constructors, `state`, `bind`, ids and the registered types are free names, as in a UI script:
//
//   test::HeadlessFrame h;
//   h.registerType<Gauge>("Gauge");
//   ASSERT_EQ(h.run("root = Column { Gauge { value = 0.5 } }"), "");
//   h.renderFrame("root");
//   Gauge* g = h.instance<Gauge>("gauge");
//
// Errors the scripts report go to luaui::errors() (cleared when the harness is created and destroyed).
// Main thread only, one harness at a time (ImGui and the error list are global).
module;

#include <concepts>
#include <string>
#include <string_view>
#include <utility>

export module imlui.test.headless_frame;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.native;
import imlui.lua.node;
import imlui.lua.painter;
import imlui.theme.typography;
import imlui.ui.frame_request;
import imlui.ui.renderer;
import imlui.ui.elements.builtin;
import imlui.types.element;
import imlui.types.script_object;
import imlui.types.type_info;
import imlui.types.registry;

export namespace imlui::test {

class HeadlessFrame {
public:
    struct Options {
        float width = 800.0f; ///< display size in pixels
        float height = 600.0f;
        float deltaTime = 1.0f / 60.0f; ///< seconds between frames
        /// Registers the built-in element types (Column, Dialog, ModalNavigationDrawer, ...) first,
        /// like the Engine does; types registered afterwards replace built-ins of the same name.
        bool builtins = false;
    };

    HeadlessFrame();
    explicit HeadlessFrame(Options options);
    ~HeadlessFrame();
    HeadlessFrame(const HeadlessFrame&) = delete;
    HeadlessFrame& operator=(const HeadlessFrame&) = delete;
    HeadlessFrame(HeadlessFrame&&) = delete;
    HeadlessFrame& operator=(HeadlessFrame&&) = delete;

    /// Registers element type T as `name`. Call before start() (or the first run()).
    template <class T>
        requires std::derived_from<T, Element> && std::default_initializable<T>
    TypeInfo& registerType(std::string name) {
        return m_types.registerType<T>(std::move(name));
    }

    [[nodiscard]] Renderer& renderer();
    [[nodiscard]] TypeRegistry& types();

    /// Creates the Lua state, once (run() calls it). Types registered later need a new harness.
    void start();
    /// The Lua state (started on demand).
    [[nodiscard]] lua::State* state();

    /// Runs `code` in the UI script environment. Returns the error message, or "" on success. Assignments
    /// to free names (`root = Column { ... }`) stay in the environment, where renderFrame/instance look.
    std::string run(std::string_view code);

    /// Renders one ImGui frame: advances the time by Options::deltaTime, runs ui.__tick (timers, onFrame),
    /// and renders the element held by `root` in the script environment inside a full-display window.
    /// Key and mouse events queued before (native.injectKey / injectMouse, or ImGui::GetIO()) are seen.
    void renderFrame(std::string_view root);
    void renderFrames(std::string_view root, int count);
    /// Frames rendered so far.
    [[nodiscard]] int frames() const;

    /// The C++ instance of the typed element held by `name` in the script environment, synced (like a
    /// parent's Frame::Child::as<T>()); nullptr when it isn't a T.
    template <class T>
        requires std::derived_from<T, ScriptObject>
    [[nodiscard]] T* instance(std::string_view name) {
        return dynamic_cast<T*>(syncElement(name));
    }
    /// The same, untyped.
    Element* syncElement(std::string_view name);

private:
    /// Pushes ui.env[name] (resolved like a free name in a UI script).
    lua::Type pushName(std::string_view name);

    Options m_options;
    ImGuiContext* m_context = nullptr;
    TypeRegistry m_types;
    Renderer m_renderer{m_types};
    luaui::NativeHooks m_hooks;
    lua::State* m_state = nullptr;
    int m_runRef = lua::noRef;
    int m_envRef = lua::noRef;
    int m_tickRef = lua::noRef;
    int m_frames = 0;
};

} // namespace imlui::test

module :private;

namespace imlui::test {

HeadlessFrame::HeadlessFrame() : HeadlessFrame(Options{}) {}

HeadlessFrame::HeadlessFrame(Options options) : m_options{options} {
    luaui::clearErrors();
    clearFrameRequests();
    m_context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {m_options.width, m_options.height};
    io.DeltaTime = m_options.deltaTime;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // like the platform window
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // no backend: fonts needn't be prebuilt
    // Roboto in this context's atlas, like the Engine: the Material widgets measure text with it. Every harness
    // loads them, so the font pointers never outlive their context into the next harness.
    loadFonts(IMLUI_FONT_DIR);
    if (m_options.builtins) {
        registerBuiltinTypes(m_types);
    }
}

HeadlessFrame::~HeadlessFrame() {
    // The Lua state first: closing it destroys the element instances and detaches the singletons.
    if (m_state) lua::close(m_state);
    ImGui::DestroyContext(m_context);
    luaui::clearErrors();
    clearFrameRequests();
}

Renderer& HeadlessFrame::renderer() { return m_renderer; }
TypeRegistry& HeadlessFrame::types() { return m_types; }
int HeadlessFrame::frames() const { return m_frames; }

void HeadlessFrame::start() {
    if (m_state) return;
    m_state = lua::newstate();
    lua::openlibs(m_state);
    luaui::registerNative(m_state, &m_hooks);
    luaui::registerPainter(m_state);
    m_types.install(m_state);
    // Returns the code runner, the script environment and ui.__tick.
    const std::string boot = std::string{"package.path = '"} + IMLUI_LUA_DIR + "/?.lua;' .. package.path\n" + R"(
        local ui = require("imlui")
        local function run(code)
            local chunk, err = load(code, "=test", "t", ui.env)
            if not chunk then error(err, 0) end
            chunk()
        end
        return run, ui.env, ui.__tick)";
    if (lua::loadbuffer(m_state, boot, "=headless_frame") != lua::ok || lua::pcall(m_state, 0, 3, 0) != lua::ok) {
        luaui::reportError("HeadlessFrame: loading lua/imlui.lua failed: " + lua::toStringAny(m_state, -1));
        lua::settop(m_state, 0);
        return;
    }
    m_tickRef = lua::ref(m_state);
    m_envRef = lua::ref(m_state);
    m_runRef = lua::ref(m_state);
}

lua::State* HeadlessFrame::state() {
    start();
    return m_state;
}

std::string HeadlessFrame::run(std::string_view code) {
    start();
    if (m_runRef == lua::noRef) return "the Lua state failed to start";
    const int top = lua::gettop(m_state);
    lua::rawgeti(m_state, lua::registryIndex, m_runRef);
    lua::pushstring(m_state, code);
    std::string error;
    if (lua::pcall(m_state, 1, 0, 0) != lua::ok) error = lua::toStringAny(m_state, -1);
    lua::settop(m_state, top);
    return error;
}

lua::Type HeadlessFrame::pushName(std::string_view name) {
    start();
    if (m_envRef == lua::noRef) {
        lua::pushnil(m_state);
        return lua::Type::Nil;
    }
    lua::rawgeti(m_state, lua::registryIndex, m_envRef);
    const std::string key{name};
    lua::getfield(m_state, -1, key.c_str());
    lua::remove(m_state, -2);
    return lua::type(m_state, -1);
}

void HeadlessFrame::renderFrame(std::string_view root) {
    start();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {m_options.width, m_options.height};
    io.DeltaTime = m_options.deltaTime;
    beginFrameRequests();
    ImGui::NewFrame();
    const int top = lua::gettop(m_state);
    if (m_tickRef != lua::noRef) {
        lua::rawgeti(m_state, lua::registryIndex, m_tickRef);
        lua::pushnumber(m_state, ImGui::GetTime());
        luaui::safeCall(m_state, 1, 0);
    }
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##headless", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    if (pushName(root) == lua::Type::Table) m_renderer.render(m_state, lua::gettop(m_state));
    lua::settop(m_state, top);
    ImGui::End();
    ImGui::Render();
    ++m_frames;
}

void HeadlessFrame::renderFrames(std::string_view root, int count) {
    for (int i = 0; i < count; ++i)
        renderFrame(root);
}

Element* HeadlessFrame::syncElement(std::string_view name) {
    Element* e = nullptr;
    if (pushName(name) == lua::Type::Table) e = m_types.sync(luaui::Node{m_state, -1});
    lua::pop(m_state);
    return e;
}

} // namespace imlui::test
