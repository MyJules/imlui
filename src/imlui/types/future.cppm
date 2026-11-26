// Future<T> and Promise<T>: a result that arrives later, on the main thread, and that a script can await.
//
// A method returns a Future like any value; it converts (LuaConvert) to a Lua future, which a script awaits inside an
// async function or chains with :next(). The usual way to make one is ScriptObject::runAsync(work), which settles it
// with what `work` returns on the background pool. A result that comes from elsewhere (a callback API) goes through a
// Promise:
//
//   imlui::Future<Report> Backend::fetch(std::string url) {
//       return runAsync([url](std::stop_token stop) { return download(url, stop); });
//   }
//   imlui::Future<int> Backend::ask() {
//       auto [promise, future] = imlui::makePromise<int>();
//       m_dialog.open([promise](int answer) { promise.resolve(answer); }); // any thread
//       return future;
//   }
//
// A future settles once: done (a value), failed (a message) or cancelled. Settling runs on the main thread: a
// Promise's resolve()/reject() post it (imlui::post), and a promise destroyed without settling cancels its future, so a
// dropped job never leaves a script waiting. The value is converted with LuaConvert<T> then, into the Lua state the
// future was pushed to; a state closed since (hot reload) is left alone (the link is a LuaFunction, which expires with
// its state). C++ code can wait for it too, on the main thread: onSettled(fn).
//
// Cancelling (Future::cancel(), or fut:cancel() in Lua) settles the future as cancelled and stops the promise's
// stopToken(), which runAsync passes on to the work. A promise made with an owner token (makePromise<T>(owner); runAsync
// uses its object's handle) cancels its future once the owner stops, and a value that arrives after that is dropped.
//
// Progress. Before it settles, a future may get progress reports (a double, by convention 0..1): Promise::progress(v)
// or a Progress reporter (Promise::reporter(); runAsync hands one to work that takes it), from any thread. They are
// coalesced like Live<T>: the latest value wins, and the main thread applies at most one per loop iteration
// (imlui::post) to Future::progress(), the onProgress handlers and the Lua future's `progress` field. A report after
// the promise settled (or after the future did) is dropped.
//
// A Lua future as an argument. LuaConvert<Future<T>> also reads: a method may take an imlui::Future<T>. A future that
// came from C++ reads back as the same future (when T matches); any other Lua future (future(), an async function,
// sleep, all, next) is adopted: the C++ future settles when it does, its first value converted with LuaConvert<T>
// then (a value that doesn't convert fails it, "bad future value (<mismatch>)"), and follows its progress. Cancelling
// either side cancels the other: they are one future. While the Lua future is pending it keeps the C++ state alive
// (like a promise), so onSettled handlers run although the method dropped its Future. If the Lua state closes first,
// the C++ future is cancelled.
//
// Main thread only, except Promise (any thread), Promise::stopToken() and Progress. Unhandled-rejection reports are
// Lua's: see the Async section of src/resources/imlui.lua.
export module imlui.types.future;

import std;

import lua;
import imlui.lua.errors;
import imlui.types.lua_convert;
import imlui.types.lua_function;
import imlui.types.lua_types;
import imlui.types.progress;
import imlui.ui.frame_request;
import imlui.ui.main_thread;

export namespace imlui {

/// Where a future is: still waiting, or settled one of three ways.
enum class FutureStatus { Pending, Done, Failed, Cancelled };

template <class T>
class Future;
template <class T>
class Promise;

/// A connected promise and future. With an `owner` token, the future is cancelled once `owner` stops (a value or error
/// that arrives later is dropped): runAsync passes its object's handle().stopToken().
template <class T>
std::pair<Promise<T>, Future<T>> makePromise(std::stop_token owner = {});

} // namespace imlui

// The shared state behind Future and Promise. Exported because their templates use it; not an API of its own.
export namespace imlui::future_detail {

/// The untyped part of a future's state: status, error, handlers and the Lua link. Main thread only, except
/// stopToken().
class Core : public std::enable_shared_from_this<Core> {
public:
    Core() = default;
    virtual ~Core();
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    [[nodiscard]] FutureStatus status() const;
    /// Failed: the message; cancelled: "cancelled"; otherwise empty.
    [[nodiscard]] const std::string& error() const;
    /// Thread-safe: stopped when the future is cancelled.
    [[nodiscard]] std::stop_token stopToken() const;
    /// The last progress applied, or nullopt before the first.
    [[nodiscard]] std::optional<double> progress() const;

    /// Settles as failed. Does nothing once settled.
    void fail(std::string message);
    /// Settles as cancelled and stops stopToken(). Returns false (doing nothing) once settled.
    bool cancel();
    /// Calls fn() once settled: now when it is, else when it settles. Counts as handling a rejection.
    void addHandler(std::function<void()> fn);
    /// While pending: stores `value` as the progress, runs the progress handlers, tells Lua, requests a frame.
    void setProgress(double value);
    /// Calls fn(progress) at each progress applied from now until the future settles.
    void addProgressHandler(std::function<void(double)> fn);

    /// Pushes the future's Lua object onto `state`: the one made by an earlier push to the same Lua state, or a new one
    /// (ui.__futureLink in the prelude) that this state settles from now on. May raise a Lua error.
    void push(lua::State* state);

    /// Whether the value at `idx` is a Lua future (the prelude's). Runs no Lua code, never raises.
    [[nodiscard]] static bool isLuaFuture(lua::State* state, int idx);
    /// The state of the Lua future at `idx` (a future: isLuaFuture): the one it was pushed from or adopted by, if that
    /// still lives, else nullptr.
    [[nodiscard]] static std::shared_ptr<Core> linked(lua::State* state, int idx);
    /// Makes this pending state follow the Lua future at `idx` (ui.__futureAdopt): it settles and reports progress
    /// when that future does, and cancelling this one cancels that one. May raise a Lua error.
    void adopt(lua::State* state, int idx);

protected:
    /// Settles as `status` (the value is set already for Done): runs the C++ handlers, tells Lua (unless `tellLua` is
    /// false: Lua settled it), requests a frame.
    void complete(FutureStatus status, std::string error, bool tellLua = true);

private:
    /// Done: pushes the value (nothing for Future<void>). Returns the number of values pushed.
    virtual int pushValue(lua::State* state) const = 0;
    /// An adopted Lua future is done with the value at `idx` (nil for none): resolves with it converted, or fails.
    virtual void resolveFromLua(lua::State* state, int idx) = 0;
    /// What the receiver of an adopted Lua future got: (kind, ...) at stack index 1 on.
    void receive(lua::State* state);
    /// Pushes the settlement arguments of the Lua link (see ui.__futureLink) and returns their number.
    int pushSettlement(lua::State* state) const;
    /// cancel(), without telling Lua (which cancelled it).
    void cancelFromLua();
    /// Calls the Lua link with the settlement, if the future was pushed and its state lives.
    void notifyLua();
    /// Calls the Lua link with ("progress", value), likewise.
    void notifyProgress();
    /// Pushes a weak reference to this state (a userdata, the Lua future's __core).
    void pushWeak(lua::State* state);
    /// Pushes the Lua canceller (a C closure whose upvalue holds a weak reference to this state), then the reference.
    void pushCanceller(lua::State* state);
    /// Pushes the receiver of an adopted Lua future: a C closure whose upvalue holds a strong reference to this state,
    /// and whose finalizer (the future settled, or the state closing) cancels it if pending and releases it.
    void pushReceiver(lua::State* state);
    static int luaCancel(lua::State* state);
    static int luaReceive(lua::State* state);
    static int cancellerGc(lua::State* state);
    static int receiverGc(lua::State* state);

    FutureStatus m_status = FutureStatus::Pending;
    std::string m_error;
    std::vector<std::function<void()>> m_handlers;
    std::optional<double> m_progress;
    std::vector<std::function<void(double)>> m_progressHandlers;
    bool m_handled = false; ///< a C++ handler was added: Lua doesn't report a rejection as unhandled
    bool m_adopted = false; ///< the link is an adopted Lua future's: Lua settles it, the link only takes a cancel
    LuaFunction m_link;     ///< settles the Lua future (or, called without arguments, returns it)
    lua::State* m_linkState = nullptr; ///< the link's main thread; valid while the link isn't expired
    std::stop_source m_stop;
};

/// A Future<T>'s state: the Core plus the value.
template <class T>
class State final : public Core {
public:
    using Value = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    /// Settles as done with `value`. Does nothing once settled.
    void resolve(Value value) {
        if (status() != FutureStatus::Pending) return;
        m_value.emplace(std::move(value));
        complete(FutureStatus::Done, {});
    }
    [[nodiscard]] const Value* value() const { return m_value ? &*m_value : nullptr; }

private:
    int pushValue(lua::State* state) const override {
        if constexpr (std::is_void_v<T>) {
            return 0;
        } else if constexpr (LuaPushable<T>) {
            LuaConvert<T>::push(state, *m_value);
            return 1;
        } else {
            // A C++-only future (LuaConvert<Future<T>> doesn't compile for it): never pushed.
            static_cast<void>(state);
            return 0;
        }
    }
    void resolveFromLua(lua::State* state, int idx) override {
        if constexpr (std::is_void_v<T>) {
            static_cast<void>(state);
            static_cast<void>(idx);
            resolve(Value{});
        } else if constexpr (LuaReadable<T>) {
            if (LuaConvert<T>::check(state, idx))
                resolve(LuaConvert<T>::get(state, idx));
            else
                fail("bad future value (" + luaMismatchOf(LuaConvert<T>::name, luaMismatch<T>(state, idx)) + ")");
        } else {
            // A future LuaConvert<Future<T>> can't read (get() doesn't compile for it): never adopted.
            static_cast<void>(state);
            static_cast<void>(idx);
        }
    }

    std::optional<Value> m_value;
};

/// What the copies of one Promise share. Thread-safe. The first resolve/reject claims the promise; its destructor hands
/// the state back to the main thread (where a LuaFunction may be released) and cancels a future nobody settled.
class Sender {
public:
    Sender(std::shared_ptr<Core> core, std::stop_token owner);
    ~Sender();
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&&) = delete;
    Sender& operator=(Sender&&) = delete;

    /// The first call posts `settle(core)` to the main thread, where it runs unless the future settled meanwhile (it
    /// cancels instead when the owner stopped). Later calls do nothing.
    void settle(std::function<void(Core&)> settle);
    /// Until settle() was called: stores `value` (a NaN is dropped) as the latest progress and, unless one is queued,
    /// posts an apply (Core::setProgress) of the latest.
    void progress(double value);
    [[nodiscard]] std::stop_token stopToken() const;

private:
    /// The latest progress not applied yet, shared with the posted apply.
    struct ProgressSlot;

    /// The owner's stop callback: posts a cancel.
    struct OwnerStopped {
        std::weak_ptr<Core> core;
        void operator()() const;
    };

    std::shared_ptr<Core> m_core;
    std::stop_token m_owner;
    std::atomic<bool> m_claimed{false};
    std::shared_ptr<ProgressSlot> m_progress;
    std::optional<std::stop_callback<OwnerStopped>> m_ownerStopped;
};

} // namespace imlui::future_detail

export namespace imlui {

/// A result that arrives later (see the module comment). Copies share one state. Main thread only. An empty future
/// (default-constructed) is cancelled.
template <class T>
class Future {
public:
    /// The stored value type: T, or std::monostate for Future<void>.
    using Value = typename future_detail::State<T>::Value;

    Future() = default;

    [[nodiscard]] FutureStatus status() const { return m_state ? m_state->status() : FutureStatus::Cancelled; }
    [[nodiscard]] bool pending() const { return status() == FutureStatus::Pending; }
    /// The value once done, else nullptr.
    [[nodiscard]] const T* value() const
        requires(!std::is_void_v<T>)
    {
        return m_state ? m_state->value() : nullptr;
    }
    /// Failed: the message; cancelled: "cancelled"; otherwise empty.
    [[nodiscard]] const std::string& error() const {
        static const std::string cancelled{"cancelled"};
        return m_state ? m_state->error() : cancelled;
    }
    /// Settles the future as cancelled and stops its promise's stopToken() (runAsync's work sees it); an adopted Lua
    /// future is cancelled too. Returns false once settled.
    bool cancel() const { return m_state && m_state->cancel(); }
    /// The last progress applied (Promise::progress, a Progress reporter, an adopted Lua future's), or nullopt.
    [[nodiscard]] std::optional<double> progress() const { return m_state ? m_state->progress() : std::nullopt; }
    /// Calls fn(progress) on the main thread at each progress applied from now until the future settles. An exception
    /// fn throws is reported as a script error.
    void onProgress(std::function<void(double)> fn) const {
        if (m_state && fn) m_state->addProgressHandler(std::move(fn));
    }
    /// Calls fn(*this) once settled: now when it is, otherwise on the main thread when it settles. An exception fn
    /// throws is reported as a script error. Lua then doesn't report a rejection as unhandled.
    void onSettled(std::function<void(const Future&)> fn) const {
        if (!m_state || !fn) return;
        m_state->addHandler([weak = std::weak_ptr<future_detail::State<T>>{m_state}, fn = std::move(fn)] {
            if (auto state = weak.lock()) fn(Future{std::move(state)});
        });
    }

private:
    friend class Promise<T>;
    friend struct LuaConvert<Future>;
    template <class U>
    friend std::pair<Promise<U>, Future<U>> makePromise(std::stop_token owner);

    explicit Future(std::shared_ptr<future_detail::State<T>> state) : m_state{std::move(state)} {}

    std::shared_ptr<future_detail::State<T>> m_state;
};

/// The settling side of a Future (makePromise). Copy it to any thread: resolve() and reject() are thread-safe and post
/// the settlement to the main thread; the first one wins. When the last copy is destroyed without either, the future
/// is cancelled. An empty promise (default-constructed) does nothing.
template <class T>
class Promise {
public:
    using Value = typename Future<T>::Value;

    Promise() = default;

    /// Settles the future as done with `value` (on the main thread, at its next loop iteration).
    void resolve(Value value) const
        requires(!std::is_void_v<T>)
    {
        settleWith(std::move(value));
    }
    /// Settles the future as done (Future<void>).
    void resolve() const
        requires std::is_void_v<T>
    {
        settleWith(Value{});
    }
    /// Settles the future as failed with `message`.
    void reject(std::string message) const {
        if (!m_sender) return;
        m_sender->settle([message = std::move(message)](future_detail::Core& core) mutable {
            core.fail(std::move(message));
        });
    }
    /// Thread-safe: stopped when the future is cancelled (by a script, C++ or the owner). The work producing the
    /// value polls it. Never stops for an empty promise.
    [[nodiscard]] std::stop_token stopToken() const { return m_sender ? m_sender->stopToken() : std::stop_token{}; }
    /// Thread-safe: reports progress (by convention 0..1; a NaN is dropped). Coalesced: the main thread applies the
    /// latest at its next loop iteration. Dropped once resolve() or reject() was called.
    void progress(double value) const {
        if (m_sender) m_sender->progress(value);
    }
    /// A Progress reporter for this promise's future, for work that shouldn't get the promise itself. It doesn't keep
    /// the promise alive: once the last Promise copy is gone, its reports are dropped.
    [[nodiscard]] Progress reporter() const {
        if (!m_sender) return {};
        return Progress{[weak = std::weak_ptr<future_detail::Sender>{m_sender}](double value) {
            if (const auto sender = weak.lock()) sender->progress(value);
        }};
    }

private:
    template <class U>
    friend std::pair<Promise<U>, Future<U>> makePromise(std::stop_token owner);

    explicit Promise(std::shared_ptr<future_detail::Sender> sender) : m_sender{std::move(sender)} {}

    void settleWith(Value value) const {
        if (!m_sender) return;
        // std::function needs a copyable function: share the (possibly move-only) value.
        m_sender->settle([value = std::make_shared<Value>(std::move(value))](future_detail::Core& core) {
            static_cast<future_detail::State<T>&>(core).resolve(std::move(*value));
        });
    }

    std::shared_ptr<future_detail::Sender> m_sender;
};

template <class T>
std::pair<Promise<T>, Future<T>> makePromise(std::stop_token owner) {
    auto state = std::make_shared<future_detail::State<T>>();
    auto sender = std::make_shared<future_detail::Sender>(state, std::move(owner));
    return {Promise<T>{std::move(sender)}, Future<T>{std::move(state)}};
}

/// A Lua future (see the Async section of the Lua API). Pushing the same future again into the same Lua state pushes
/// the same object. Reading (T void, or with a LuaConvert<T> that reads) takes any Lua future: the one a C++ future
/// was pushed as gives that future back; another is adopted (see the module comment).
template <class T>
struct LuaConvert<Future<T>> {
    static constexpr std::string_view name = "future";
    /// The generic `Future<T>` class of the prelude definitions; `Future<nil>` for Future<void>.
    static std::string luaType(LuaTypes& types) {
        if constexpr (std::is_void_v<T>)
            return "Future<nil>";
        else
            return "Future<" + luaTypeOf<T>(types) + ">";
    }
    static bool check(lua::State* s, int idx)
        requires(std::is_void_v<T> || LuaReadable<T>)
    {
        return future_detail::Core::isLuaFuture(s, idx);
    }
    static Future<T> get(lua::State* s, int idx)
        requires(std::is_void_v<T> || LuaReadable<T>)
    {
        if (auto state = std::dynamic_pointer_cast<future_detail::State<T>>(future_detail::Core::linked(s, idx)))
            return Future<T>{std::move(state)};
        auto state = std::make_shared<future_detail::State<T>>();
        state->adopt(s, idx);
        return Future<T>{std::move(state)};
    }
    static void push(lua::State* s, const Future<T>& f) {
        static_assert(std::is_void_v<T> || LuaPushable<T>, "Future<T> to Lua: T needs a LuaConvert<T> with push()");
        if (f.m_state) {
            f.m_state->push(s);
            return;
        }
        // An empty future: a cancelled one.
        Future<T> cancelled{std::make_shared<future_detail::State<T>>()};
        cancelled.cancel();
        cancelled.m_state->push(s);
    }
};

} // namespace imlui

module :private;

namespace imlui::future_detail {

namespace {

/// Registry key of the metatable of the weak references to a state: a Lua future's __core, the canceller's upvalue.
constexpr const char* cancellerMetaKey = "imlui.Future.canceller";
/// Registry key of the metatable of an adopted Lua future's receiver upvalue (its __gc cancels the state).
constexpr const char* receiverMetaKey = "imlui.Future.receiver";
/// Registry key of the prelude's future metatable (set by the Async section of imlui.lua).
constexpr const char* futureMetaKey = "imlui.future";

/// Pushes require("imlui")[name], which must be a function (raises otherwise).
void pushPreludeFunction(lua::State* state, const char* name) {
    lua::getglobal(state, "require");
    lua::pushstring(state, "imlui");
    lua::call(state, 1, 1);
    lua::getfield(state, -1, name);
    lua::remove(state, -2);
    if (!lua::isfunction(state, -1)) lua::error(state, std::format("Future: the imlui prelude has no {}", name));
}

using WeakCore = std::weak_ptr<Core>;
using StrongCore = std::shared_ptr<Core>;

} // namespace

Core::~Core() = default;

FutureStatus Core::status() const { return m_status; }

const std::string& Core::error() const { return m_error; }

std::stop_token Core::stopToken() const { return m_stop.get_token(); }

std::optional<double> Core::progress() const { return m_progress; }

void Core::fail(std::string message) {
    if (m_status != FutureStatus::Pending) return;
    complete(FutureStatus::Failed, std::move(message));
}

bool Core::cancel() {
    if (m_status != FutureStatus::Pending) return false;
    complete(FutureStatus::Cancelled, "cancelled");
    return true;
}

void Core::cancelFromLua() {
    if (m_status != FutureStatus::Pending) return;
    complete(FutureStatus::Cancelled, "cancelled", false); // the Lua future settled itself
}

void Core::addHandler(std::function<void()> fn) {
    m_handled = true;
    if (m_status == FutureStatus::Pending)
        m_handlers.push_back(std::move(fn));
    else
        luaui::reportCxx(fn, "Future::onSettled");
}

void Core::setProgress(double value) {
    if (m_status != FutureStatus::Pending) return;
    // Keep this state alive while the handlers and Lua run: one of them may drop the last Future.
    const std::shared_ptr<Core> self = shared_from_this();
    m_progress = value;
    const std::vector<std::function<void(double)>> handlers = m_progressHandlers; // a handler may add one
    for (const auto& fn : handlers) {
        if (m_status != FutureStatus::Pending) break; // a handler settled it
        luaui::reportCxx([&] { fn(value); }, "Future::onProgress");
    }
    if (m_status == FutureStatus::Pending && !m_adopted) notifyProgress();
    requestFrame();
}

void Core::addProgressHandler(std::function<void(double)> fn) {
    if (m_status == FutureStatus::Pending) m_progressHandlers.push_back(std::move(fn));
}

void Core::complete(FutureStatus status, std::string error, bool tellLua) {
    // Keep this state alive while the handlers and Lua run: one of them may drop the last Future.
    const std::shared_ptr<Core> self = shared_from_this();
    m_status = status;
    m_error = std::move(error);
    m_progressHandlers.clear();
    if (status == FutureStatus::Cancelled) m_stop.request_stop();
    const std::vector<std::function<void()>> handlers = std::exchange(m_handlers, {});
    for (const auto& fn : handlers)
        luaui::reportCxx(fn, "Future::onSettled");
    if (tellLua) notifyLua();
    requestFrame();
}

int Core::pushSettlement(lua::State* state) const {
    switch (m_status) {
    case FutureStatus::Done:
        lua::pushstring(state, "done");
        return 1 + pushValue(state);
    case FutureStatus::Failed:
        lua::pushstring(state, "failed");
        lua::pushstring(state, m_error);
        lua::pushboolean(state, m_handled);
        return 3;
    case FutureStatus::Cancelled:
        lua::pushstring(state, "cancelled");
        return 1;
    case FutureStatus::Pending:
        break;
    }
    return 0;
}

void Core::notifyLua() {
    // An adopted future's link only takes a cancel: its other outcomes came from Lua.
    if (m_link.expired() || (m_adopted && m_status != FutureStatus::Cancelled)) return;
    lua::State* state = m_linkState;
    luaui::protectedCall(state, "Future", [&] {
        lua::checkstack(state, 8);
        m_link.push(state);
        const int nargs = pushSettlement(state);
        luaui::safeCall(state, nargs, 0);
    });
}

void Core::notifyProgress() {
    if (m_link.expired() || !m_progress) return;
    lua::State* state = m_linkState;
    luaui::protectedCall(state, "Future", [&] {
        lua::checkstack(state, 4);
        m_link.push(state);
        lua::pushstring(state, "progress");
        lua::pushnumber(state, *m_progress);
        luaui::safeCall(state, 2, 0);
    });
}

void Core::push(lua::State* state) {
    lua::checkstack(state, 8);
    if (!m_link.expired() && lua::mainthread(state) == m_linkState) {
        m_link.push(state);
        lua::call(state, 0, 1); // the link without arguments returns the future
        return;
    }
    // A new Lua future: future, link = require("imlui").__futureLink(canceller, core, settlement...)
    pushPreludeFunction(state, "__futureLink");
    pushCanceller(state);
    const int nargs = 2 + pushSettlement(state);
    lua::call(state, nargs, 2);
    m_link = LuaFunction{state, -1};
    m_linkState = lua::mainthread(state);
    m_adopted = false;
    lua::pop(state);
    if (m_status == FutureStatus::Pending && m_progress) notifyProgress();
}

void Core::adopt(lua::State* state, int idx) {
    idx = lua::absindex(state, idx);
    lua::checkstack(state, 8);
    // link = require("imlui").__futureAdopt(future, core, receiver)
    pushPreludeFunction(state, "__futureAdopt");
    lua::pushvalue(state, idx);
    pushWeak(state);
    pushReceiver(state);
    m_adopted = true; // before the call: a future that settled already settles this one inside it
    lua::call(state, 3, 1);
    m_link = LuaFunction{state, -1};
    m_linkState = lua::mainthread(state);
    lua::pop(state);
}

bool Core::isLuaFuture(lua::State* state, int idx) {
    if (!lua::istable(state, idx) || !lua::checkstack(state, 2)) return false;
    idx = lua::absindex(state, idx);
    if (lua::getmetatable(state, idx) == 0) return false;
    lua::getfield(state, lua::registryIndex, futureMetaKey); // the registry has no metamethods
    const bool future = lua::rawequal(state, -1, -2);
    lua::pop(state, 2);
    return future;
}

std::shared_ptr<Core> Core::linked(lua::State* state, int idx) {
    lua::checkstack(state, 3);
    idx = lua::absindex(state, idx);
    std::shared_ptr<Core> core;
    if (lua::rawgetfield(state, idx, "__core") == lua::Type::UserData && lua::getmetatable(state, -1) != 0) {
        lua::getfield(state, lua::registryIndex, cancellerMetaKey);
        if (lua::rawequal(state, -1, -2)) core = static_cast<const WeakCore*>(lua::touserdata(state, -3))->lock();
        lua::pop(state, 2);
    }
    lua::pop(state);
    return core;
}

void Core::receive(lua::State* state) {
    const std::string kind{lua::isstring(state, 1) ? lua::tostring(state, 1) : std::string_view{}};
    if (kind == "done") {
        if (lua::gettop(state) < 2) lua::settop(state, 2); // no value: nil
        resolveFromLua(state, 2);
    } else if (kind == "failed") {
        fail(std::string{lua::isstring(state, 2) ? lua::tostring(state, 2) : std::string_view{"(error object)"}});
    } else if (kind == "cancelled") {
        cancelFromLua();
    } else if (kind == "progress" && lua::isnumber(state, 2)) {
        setProgress(lua::tonumber(state, 2));
    }
}

void Core::pushWeak(lua::State* state) {
    void* memory = lua::newuserdata(state, sizeof(WeakCore));
    std::construct_at(static_cast<WeakCore*>(memory), weak_from_this());
    if (lua::getfield(state, lua::registryIndex, cancellerMetaKey) != lua::Type::Table) {
        lua::pop(state);
        lua::createtable(state, 0, 1);
        lua::pushcfunction(state, &cancellerGc);
        lua::setfield(state, -2, "__gc");
        lua::pushvalue(state, -1);
        lua::setfield(state, lua::registryIndex, cancellerMetaKey);
    }
    lua::setmetatable(state, -2);
}

void Core::pushCanceller(lua::State* state) {
    pushWeak(state);
    lua::pushvalue(state, -1);
    lua::pushcclosure(state, &luaCancel, 1);
    lua::insert(state, -2); // canceller, core
}

void Core::pushReceiver(lua::State* state) {
    // The metatable first: once the userdata holds the reference it must get its finalizer without another allocation
    // (which could raise) in between.
    if (lua::getfield(state, lua::registryIndex, receiverMetaKey) != lua::Type::Table) {
        lua::pop(state);
        lua::createtable(state, 0, 1);
        lua::pushcfunction(state, &receiverGc);
        lua::setfield(state, -2, "__gc");
        lua::pushvalue(state, -1);
        lua::setfield(state, lua::registryIndex, receiverMetaKey);
    }
    void* memory = lua::newuserdata(state, sizeof(StrongCore));
    std::construct_at(static_cast<StrongCore*>(memory), shared_from_this());
    lua::insert(state, -2);
    lua::setmetatable(state, -2);
    lua::pushcclosure(state, &luaReceive, 1);
}

int Core::luaReceive(lua::State* state) {
    const auto* strong = static_cast<const StrongCore*>(lua::touserdata(state, lua::upvalueindex(1)));
    if (!strong || !*strong) return 0;
    const std::shared_ptr<Core> core = *strong; // a handler may drop the receiver
    luaui::catchCxx(state, [&] { core->receive(state); }, "Future");
    return 0;
}

// The receiver keeps the state alive (like a Promise) while its Lua future is pending: it is reachable from that
// future's handlers until it settles. It is collected then, or when the Lua state closes, which cancels the state. The
// state is released in a posted function: its link must not be released inside a finalizer of a closing state.
int Core::receiverGc(lua::State* state) {
    auto* strong = static_cast<StrongCore*>(lua::touserdata(state, 1));
    if (!strong) return 0;
    if (std::shared_ptr<Core> core = std::move(*strong))
        imlui::post([core = std::move(core)] { core->cancel(); }); // does nothing once settled
    std::destroy_at(strong);
    return 0;
}

int Core::luaCancel(lua::State* state) {
    const auto* weak = static_cast<const WeakCore*>(lua::touserdata(state, lua::upvalueindex(1)));
    if (!weak) return 0;
    if (const std::shared_ptr<Core> core = weak->lock())
        luaui::catchCxx(state, [&] { core->cancelFromLua(); }, "Future:cancel");
    return 0;
}

int Core::cancellerGc(lua::State* state) {
    if (auto* weak = static_cast<WeakCore*>(lua::touserdata(state, 1))) std::destroy_at(weak);
    return 0;
}

struct Sender::ProgressSlot {
    std::mutex mutex;
    double latest = 0;   ///< guarded by `mutex`
    bool queued = false; ///< guarded by `mutex`: an apply is posted and hasn't run yet
};

Sender::Sender(std::shared_ptr<Core> core, std::stop_token owner)
    : m_core{std::move(core)}, m_owner{std::move(owner)}, m_progress{std::make_shared<ProgressSlot>()} {
    // Runs at once when the owner stopped already.
    if (m_owner.stop_possible()) m_ownerStopped.emplace(m_owner, OwnerStopped{m_core});
}

Sender::~Sender() {
    m_ownerStopped.reset();
    // The state may hold a LuaFunction, which only the main thread may release: hand it over there.
    imlui::post([core = std::move(m_core), unsettled = !m_claimed.load()] {
        if (unsettled) core->cancel();
    });
}

void Sender::settle(std::function<void(Core&)> settle) {
    if (m_claimed.exchange(true)) return;
    imlui::post([core = m_core, owner = m_owner, settle = std::move(settle)] {
        if (core->status() != FutureStatus::Pending) return;
        if (owner.stop_requested())
            core->cancel();
        else
            settle(*core);
    });
}

void Sender::progress(double value) {
    if (std::isnan(value) || m_claimed.load()) return;
    {
        const std::scoped_lock lock{m_progress->mutex};
        m_progress->latest = value;
        if (m_progress->queued) return;
        m_progress->queued = true;
    }
    imlui::post([core = m_core, slot = m_progress] {
        double latest = 0;
        {
            const std::scoped_lock lock{slot->mutex};
            slot->queued = false;
            latest = slot->latest;
        }
        core->setProgress(latest);
    });
}

std::stop_token Sender::stopToken() const { return m_core->stopToken(); }

void Sender::OwnerStopped::operator()() const {
    if (std::shared_ptr<Core> c = core.lock()) imlui::post([c = std::move(c)] { c->cancel(); });
}

} // namespace imlui::future_detail
