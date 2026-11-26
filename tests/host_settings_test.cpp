// Tests for imlui.app.host_settings: what a script sets on its host through imlui.native, and the reload transaction
// over it (Engine::reloadNow runs every load through HostSettings::reload).
#include <gtest/gtest.h>

import std;
import lua;
import imlui.app.host_settings;
import imlui.lua.errors;
import imlui.lua.native;
import imlui.test.headless_frame;
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.ui.culling;

using namespace imlui;

namespace {

class HostSettingsTest : public ::testing::Test {
protected:
    HostSettingsTest() {
        m_settings.install(m_hooks);
        luaui::clearErrors();
    }

    ~HostSettingsTest() override {
        // Leave the globals as the other tests expect them: the default theme in effect, culling on.
        setTheme(Theme{});
        applyPendingTheme();
        cull::setEnabled(true);
        luaui::clearErrors();
    }

    /// A reload like the Engine's: a new Lua state with imlui.native on the hooks runs `code` (`native` is the
    /// library) and is closed again. True when the script ran without an error.
    bool load(std::string_view code) {
        return m_settings.reload([&] {
            lua::State* state = lua::newstate();
            lua::openlibs(state);
            luaui::registerNative(state, &m_hooks);
            const std::string chunk = "local native = require('imlui.native')\n" + std::string{code};
            const bool ok = lua::loadbuffer(state, chunk, "=main") == lua::ok && luaui::safeCall(state, 0, 0);
            lua::close(state);
            return ok;
        });
    }

    test::HeadlessFrame m_frame; ///< the ImGui context and fonts a theme is applied with; its renderer holds the title
    luaui::NativeHooks m_hooks;
    HostSettings m_settings{m_frame.renderer()};
};

TEST_F(HostSettingsTest, TopLevelZoomShortcutsOffSurvivesTheLoad) {
    EXPECT_TRUE(m_settings.zoomShortcuts());
    ASSERT_TRUE(load("native.setZoomShortcuts(false)"));
    EXPECT_FALSE(m_settings.zoomShortcuts());
    // Reloading the same script: it turns them off again.
    ASSERT_TRUE(load("native.setZoomShortcuts(false)"));
    EXPECT_FALSE(m_settings.zoomShortcuts());
    // A script that doesn't turn them off gets them back.
    ASSERT_TRUE(load("local x = 1"));
    EXPECT_TRUE(m_settings.zoomShortcuts());
}

TEST_F(HostSettingsTest, FailedLoadLeavesTheSettingsAsTheyWere) {
    // What the running script set: shortcuts off, a title, culling off, a pending zoom and window size.
    ASSERT_TRUE(load("native.setZoomShortcuts(false); native.setTitle('Old'); native.setCulling(false)\n"
                     "native.setZoom(1.5); native.setWindowSize(400, 300)"));
    Theme light;
    light.dark = false;
    setTheme(light);
    applyPendingTheme();

    EXPECT_FALSE(load("native.setZoomShortcuts(true); native.setTitle('New'); native.setCulling(true)\n"
                      "native.setZoom(2); native.setWindowSize(800, 600)\n"
                      "native.setTheme({ dark = true })\n"
                      "error('boom')"));
    EXPECT_FALSE(luaui::errors().empty());
    EXPECT_FALSE(m_settings.zoomShortcuts());
    EXPECT_EQ(m_frame.renderer().windowTitle(), "Old");
    EXPECT_FALSE(cull::enabled());
    EXPECT_FLOAT_EQ(m_settings.zoom(), 1.5f);
    EXPECT_EQ(m_settings.takePendingZoom(), std::optional{1.5f});
    EXPECT_EQ(m_settings.takePendingWindowSize(), (std::optional{std::pair{400, 300}}));
    applyPendingTheme();
    EXPECT_FALSE(theme().dark);
}

TEST_F(HostSettingsTest, FailedLoadLeavesNothingPendingWhenNothingWas) {
    const std::string title = m_frame.renderer().windowTitle(); // the default
    EXPECT_FALSE(load("native.setZoom(2); native.setWindowSize(800, 600); native.setTitle('New')\n"
                      "native.setZoomShortcuts(false)\n"
                      "error('boom')"));
    EXPECT_TRUE(m_settings.zoomShortcuts());
    EXPECT_EQ(m_frame.renderer().windowTitle(), title);
    EXPECT_EQ(m_settings.takePendingZoom(), std::nullopt);
    EXPECT_EQ(m_settings.takePendingWindowSize(), std::nullopt);
    EXPECT_FLOAT_EQ(m_settings.zoom(), zoom());
}

TEST_F(HostSettingsTest, SuccessfulLoadKeepsWhatTheScriptSet) {
    ASSERT_TRUE(load("native.setZoom(2); native.setWindowSize(800, 600); native.setTitle('New')"));
    EXPECT_EQ(m_frame.renderer().windowTitle(), "New");
    EXPECT_FLOAT_EQ(m_settings.zoom(), 2.0f);
    EXPECT_EQ(m_settings.takePendingZoom(), std::optional{2.0f});
    EXPECT_EQ(m_settings.takePendingZoom(), std::nullopt); // taken once
    EXPECT_EQ(m_settings.takePendingWindowSize(), (std::optional{std::pair{800, 600}}));
    EXPECT_EQ(m_settings.takePendingWindowSize(), std::nullopt);
}

TEST_F(HostSettingsTest, ZoomIsClamped) {
    m_settings.setZoom(100.0f);
    EXPECT_FLOAT_EQ(m_settings.zoom(), maxZoom);
    m_settings.setZoom(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(m_settings.zoom(), 1.0f);
}

TEST_F(HostSettingsTest, LoadStartsFromTheDefaultTheme) {
    Theme light;
    light.dark = false;
    setTheme(light);
    applyPendingTheme();
    ASSERT_TRUE(load("local x = 1"));
    applyPendingTheme();
    EXPECT_TRUE(theme().dark);
}

} // namespace
