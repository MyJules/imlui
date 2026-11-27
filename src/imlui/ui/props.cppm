// Shared property readers for element handlers.
//
// `visible`, `width`, `fillWidth` and `height` are read on every pass a layout makes over its children (a Row: which
// children fill, their sizes, the render; the parent stack's own passes, an intrinsic size its parent asks for), so a
// binding among them is memoized: it runs once a frame, like the property sync (TypeRegistry). The memo is per element
// table and binding, and holds while the ImGui frame and luaui::changes() stand still: a handler, a write-back or a
// script between frames makes the next read run the binding again. A plain value is read directly.
export module imlui.ui.props;

import std;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.value;

export namespace imlui {

/// Reads a size property in dp and returns pixels: > 0 a fixed size, -1 "fill",
/// < -1 "fill minus |v| dp", 0 auto. Fixed and fill-minus sizes are scaled by dp();
/// -1 stays -1. For width keys, `fillWidth = true` means "fill" (-1). A binding of `width`, `fillWidth` or `height`
/// runs once a frame (see above).
[[nodiscard]] float sizeProp(const luaui::Node& node, const char* key);

/// The element's `visible` (true when unset): whether it renders. A binding runs once a frame (see above).
[[nodiscard]] bool shown(const luaui::Node& node);

/// How many tables the per-frame memo of shown() and sizeProp() holds (tests: it doesn't grow while elements come and
/// go).
[[nodiscard]] std::size_t propMemoEntries();

/// "Label###id": the visible text may change every frame without changing the ImGui ID.
[[nodiscard]] std::string labelId(std::string_view text, const char* id);

/// Where a stack places each child across its width (a Column's, a Window's `align`), and a Text its line: its Lua
/// names ("start", "center", "end") are in imlui.ui.enum_names.
enum class Align { Start, Center, End };

/// Start -> 0, Center -> 0.5, End -> 1: the share of the free space before the item.
[[nodiscard]] float alignFactor(Align align);

/// A 1-based index prop as an int that is safe to do arithmetic on: values below 1, NaN and -infinity become 0
/// (none), values past the int range (and +infinity) become the largest int.
[[nodiscard]] int indexValue(double index);
[[nodiscard]] int indexValue(int index);

/// A progress fraction for the widgets: clamped to [0, 1], 0 when not finite (`done / total` with total == 0).
[[nodiscard]] float progressFraction(double value);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The memoized props, by their place in a Memo.
enum class Slot : std::uint8_t { Visible, FillWidth, Width, Height };
constexpr std::size_t slotCount = 4;

/// A binding's value on a frame.
struct Memoized {
    const void* source = nullptr; ///< the binding (a function or a bind object) it is the value of
    int frame = -1;
    std::uint64_t changes = 0; ///< luaui::changes() when it ran
    double value = 0.0;
};
using Memo = std::array<Memoized, slotCount>;

/// Tables are dropped when the memo has grown to twice its size after the last sweep (at least this many): a sweep
/// walks it all, so it is amortized over as many new tables.
constexpr std::size_t minSweep = 1024;

struct Memos {
    ImGuiContext* context = nullptr;
    std::unordered_map<const void*, Memo> byTable;
    std::size_t sweepAt = minSweep;
};
Memos memos; // main thread only, like all rendering

/// Drops the tables none of whose bindings ran on `frame`.
void sweep(int frame) {
    std::erase_if(memos.byTable, [&](const auto& e) {
        return std::ranges::none_of(e.second, [&](const Memoized& m) { return m.frame == frame; });
    });
    memos.sweepAt = std::max(minSweep, memos.byTable.size() * 2);
}

/// Property `key` of `node` through `convert` (the resolved value on top of the stack, of the type it is given ->
/// the result). A binding's result is memoized in `slot` for the frame while luaui::changes() stands still; outside a
/// frame (no ImGui context) nothing is.
template <class Convert>
double read(const luaui::Node& node, const lua::Key& key, Slot slot, Convert convert) {
    lua::State* state = node.state();
    const lua::Type raw = node.pushRaw(key);
    ImGuiContext* context = ImGui::GetCurrentContext();
    const bool binding = raw == lua::Type::Function || (raw == lua::Type::Table && luaui::isBind(state, -1));
    if (!binding || !context) {
        const double v = convert(node.resolveTop());
        lua::pop(state);
        return v;
    }
    if (memos.context != context) {
        memos.byTable.clear();
        memos.context = context;
        memos.sweepAt = minSweep;
    }
    const void* source = lua::topointer(state, -1);
    const void* table = node.ptr();
    const int frame = ImGui::GetFrameCount();
    const std::uint64_t changes = luaui::changes();
    const auto i = static_cast<std::size_t>(slot);
    if (const auto it = memos.byTable.find(table); it != memos.byTable.end()) {
        const Memoized& m = it->second[i];
        if (m.frame == frame && m.changes == changes && m.source == source) {
            lua::pop(state);
            return m.value;
        }
    }
    const double v = convert(node.resolveTop());
    lua::pop(state);
    // A binding that wrote what it reads (counted) gets no memo: the next read runs it again, as the sync would.
    if (luaui::changes() != changes) return v;
    if (memos.byTable.size() >= memos.sweepAt) sweep(frame);
    memos.byTable[table][i] = {source, frame, changes, v};
    return v;
}

double readBoolean(const luaui::Node& node, const lua::Key& key, Slot slot, bool def) {
    lua::State* state = node.state();
    return read(node, key, slot,
                [&](lua::Type t) { return (t == lua::Type::Nil ? def : lua::toboolean(state, -1)) ? 1.0 : 0.0; });
}

float readSize(const luaui::Node& node, const lua::Key& key, Slot slot) {
    lua::State* state = node.state();
    return luaui::toFloat(read(node, key, slot, [&](lua::Type t) {
        return t == lua::Type::Number || lua::isnumber(state, -1) ? lua::tonumber(state, -1) : 0.0;
    }));
}

} // namespace

bool shown(const luaui::Node& node) { return readBoolean(node, luaui::keys::visible, Slot::Visible, true) != 0.0; }

std::size_t propMemoEntries() { return memos.byTable.size(); }

float sizeProp(const luaui::Node& node, const char* key) {
    float v = 0.0f;
    // `width` and `height` are read for every child a layout places, every frame: through their lua::Keys, and a
    // binding once a frame.
    if (std::strcmp(key, "width") == 0) {
        if (readBoolean(node, luaui::keys::fillWidth, Slot::FillWidth, false) != 0.0) return -1.0f;
        v = readSize(node, luaui::keys::width, Slot::Width);
    } else if (std::strcmp(key, "height") == 0) {
        v = readSize(node, luaui::keys::height, Slot::Height);
    } else {
        if (key[0] == 'w' && node.boolean("fillWidth", false)) return -1.0f;
        v = node.numf(key, 0.0f);
    }
    return (v > 0 || v < -1.0f) ? dp(v) : v;
}

std::string labelId(std::string_view text, const char* id) {
    std::string s{text};
    s += "###";
    s += id;
    return s;
}

float alignFactor(Align align) {
    switch (align) {
    case Align::Center: return 0.5f;
    case Align::End: return 1.0f;
    case Align::Start: break;
    }
    return 0.0f;
}

int indexValue(double index) {
    if (!(index >= 1.0)) return 0; // also NaN
    if (index >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(index);
}

int indexValue(int index) { return std::max(index, 0); }

float progressFraction(double value) {
    if (!std::isfinite(value)) return 0.0f;
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

} // namespace imlui
