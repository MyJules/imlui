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
// Main thread only, except Promise (any thread) and Promise::stopToken(). Unhandled-rejection reports are Lua's: see
// the Async section of src/resources/imlui.lua.
export module imlui.types.future;

import std;

import lua;
import imlui.lua.errors;
import imlui.types.lua_convert;
import imlui.types.lua_function;
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

    /// Settles as failed. Does nothing once settled.
    void fail(std::string message);
    /// Settles as cancelled and stops stopToken(). Returns false (doing nothing) once settled.
    bool cancel();
    /// Calls fn() once settled: now when it is, else when it settles. Counts as handling a rejection.
    void addHandler(std::function<void()> fn);

    /// Pushes the future's Lua object onto `state`: the one made by an earlier push to the same Lua state, or a new one
    /// (ui.__futureLink in the prelude) that this state settles from now on. May raise a Lua error.
    void push(lua::State* state);

protected:
    /// Settles as `status` (the value is set already for Done): runs the C++ handlers, tells Lua (unless `tellLua` is
    /// false: Lua settled it), requests a frame.
    void complete(FutureStatus status, std::string error, bool tellLua = true);

private:
    /// Done: pushes the value (nothing for Future<void>). Returns the number of values pushed.
    virtual int pushValue(lua::State* state) const = 0;
    /// Pushes the settlement arguments of the Lua link (see ui.__futureLink) and returns their number.
    int pushSettlement(lua::State* state) const;
    /// cancel(), without telling Lua (which cancelled it).
    void cancelFromLua();
    /// Calls the Lua link with the settlement, if the future was pushed and its state lives.
    void notifyLua();
    /// The Lua canceller: a C closure whose upvalue holds a weak reference to this state.
    void pushCanceller(lua::State* state);
    static int luaCancel(lua::State* state);
    static int cancellerGc(lua::State* state);

    FutureStatus m_status = FutureStatus::Pending;
    std::string m_error;
    std::vector<std::function<void()>> m_handlers;
    bool m_handled = false; ///< a C++ handler was added: Lua doesn't report a rejection as unhandled
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
    [[nodiscard]] std::stop_token stopToken() const;

private:
    /// The owner's stop callback: posts a cancel.
    struct OwnerStopped {
        std::weak_ptr<Core> core;
        void operator()() const;
    };

    std::shared_ptr<Core> m_core;
    std::stop_token m_owner;
    std::atomic<bool> m_claimed{false};
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
    /// Settles the future as cancelled and stops its promise's stopToken() (runAsync's work sees it). Returns false
    /// once settled.
    bool cancel() const { return m_state && m_state->cancel(); }
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

/// Push only: a Lua future (see the Async section of the Lua API). Pushing the same future again into the same Lua
/// state pushes the same object.
template <class T>
struct LuaConvert<Future<T>> {
    static constexpr std::string_view name = "future";
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

/// Registry key of the canceller userdata's metatable.
constexpr const char* cancellerMetaKey = "imlui.Future.canceller";

using WeakCore = std::weak_ptr<Core>;

} // namespace

Core::~Core() = default;

FutureStatus Core::status() const { return m_status; }

const std::string& Core::error() const { return m_error; }

std::stop_token Core::stopToken() const { return m_stop.get_token(); }

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

void Core::complete(FutureStatus status, std::string error, bool tellLua) {
    // Keep this state alive while the handlers and Lua run: one of them may drop the last Future.
    const std::shared_ptr<Core> self = shared_from_this();
    m_status = status;
    m_error = std::move(error);
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
    if (m_link.expired()) return;
    lua::State* state = m_linkState;
    luaui::protectedCall(state, "Future", [&] {
        lua::checkstack(state, 8);
        m_link.push(state);
        const int nargs = pushSettlement(state);
        luaui::safeCall(state, nargs, 0);
    });
}

void Core::push(lua::State* state) {
    lua::checkstack(state, 8);
    if (!m_link.expired() && lua::mainthread(state) == m_linkState) {
        m_link.push(state);
        lua::call(state, 0, 1); // the link without arguments returns the future
        return;
    }
    // A new Lua future: future, link = require("imlui").__futureLink(canceller, settlement...)
    lua::getglobal(state, "require");
    lua::pushstring(state, "imlui");
    lua::call(state, 1, 1);
    lua::getfield(state, -1, "__futureLink");
    lua::remove(state, -2);
    if (!lua::isfunction(state, -1)) lua::error(state, "Future: the imlui prelude has no __futureLink");
    pushCanceller(state);
    const int nargs = 1 + pushSettlement(state);
    lua::call(state, nargs, 2);
    m_link = LuaFunction{state, -1};
    m_linkState = lua::mainthread(state);
    lua::pop(state);
}

void Core::pushCanceller(lua::State* state) {
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
    lua::pushcclosure(state, &luaCancel, 1);
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

Sender::Sender(std::shared_ptr<Core> core, std::stop_token owner) : m_core{std::move(core)}, m_owner{std::move(owner)} {
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

std::stop_token Sender::stopToken() const { return m_core->stopToken(); }

void Sender::OwnerStopped::operator()() const {
    if (std::shared_ptr<Core> c = core.lock()) imlui::post([c = std::move(c)] { c->cancel(); });
}

} // namespace imlui::future_detail
