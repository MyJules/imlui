// ScriptObject: the base class of every C++ object exposed to Lua (registered element types and singletons).
//
// A ScriptObject is linked to at most one Lua state at a time. The link is the Lua value that stands for
// the object there: the element table for an element instance, the global userdata for a singleton.
// It is held weakly (a weak-valued registry table keyed by the object's id), so it never keeps
// the element table alive.
//
//  * notify("value") tells Lua that property `value` changed on the C++ side: it is written back to
//    the element table (honouring two-way binds, like a widget edit), `onValueChanged` is emitted, and
//    a frame is requested.
//  * emit("onPeak", args...) calls the `onPeak` handler.
//  * Both call the signal's listeners after the handler (`connect(obj, "onPeak", fn)` in Lua; imlui.lua.listeners),
//    once one was connected to the object (noteConnection()).
//  * scriptReloading() tells a singleton that a new script is about to replace the one it served.
//
// Every object has an id (objectId()): a number no other object of the process has had or will have, so unlike its
// address it is never reused after the object dies. It is the identity of an element (its ImGui ID scope, its key in
// the layout cache, its place in the ticking list).
//
// Main thread only, like everything that touches Lua or ImGui. The exception is a Handle (handle()): a weak,
// copyable token a worker thread holds to post work back to the object, which runs only if the object is still
// alive by then (Lua may collect an element, or close its state on hot reload, at any time).
//
// runAsync(work, done[, fail]) is the usual way to use one: `work` runs on the background pool (imlui.ui.background)
// with a stop token that stops when the object dies, and `done(result)` runs on the main thread while it lives; the
// Task it returns (imlui.types.task) cancels it. runAsync(work) returns a Future of the result instead
// (imlui.types.future), which a method can return to Lua; work taking an imlui::Progress reports progress to it.
// runAsyncThen(work, then) hands the result to `then` on the main thread first, and the future gets what `then`
// returns: big data (rows for a model) stays in C++, and Lua gets a small report; it settles right after `then`.
// whenSettled(f, fn) runs `fn` once a future settles, only while the object lives (an onSettled handler that the
// object's death drops), and notifyWhenSettled(f, "filling") notifies a property backed by a member future
// (m_fill.pending()).
export module imlui.types.script_object;

import std;

import lua;
import imlui.lua.element_scope;
import imlui.lua.errors;
import imlui.lua.listeners;
import imlui.lua.node;
import imlui.types.future;
import imlui.types.lua_convert;
import imlui.types.progress;
import imlui.types.task;
import imlui.ui.background;
import imlui.ui.frame_request;
import imlui.ui.main_thread;

export namespace imlui {

class ScriptObject {
public:
    /// A weak, thread-safe reference to a ScriptObject for worker threads. Copy it freely, to any thread. It
    /// never keeps the object alive and never gives access to it off the main thread: it only says whether the
    /// object still lives, and posts functions that run on the main thread while it does.
    class Handle {
    public:
        /// An empty handle: never alive, posts are dropped.
        Handle() = default;

        /// Thread-safe. True until the object is destroyed: until cancelHandles(), which an element gets before its
        /// destroyed() hook, or else ScriptObject's destructor (after a derived destructor). False for an empty
        /// handle. A worker polls it as its "cancelled" flag.
        [[nodiscard]] bool alive() const;

        /// Thread-safe: queues `fn` on the main thread (imlui::post, which wakes the loop). There, `fn` runs only if
        /// the object is still alive; otherwise it is destroyed without running. So `fn` may capture the object's
        /// `this` and call notify()/emit(). Returns false (and drops `fn`) when the object is already gone.
        bool post(std::function<void()> fn) const;

        /// Thread-safe: a token that is stopped when the object dies (when alive() turns false; already stopped for an
        /// empty handle). Stop callbacks registered on it run on the main thread, inside cancelHandles() or the
        /// destructor.
        [[nodiscard]] std::stop_token stopToken() const;

    private:
        friend class ScriptObject;
        explicit Handle(std::stop_source state);
        /// The object's: stopped when it dies. No state for an empty handle.
        std::stop_source m_state{std::nostopstate};
    };

    /// Takes the next object id.
    ScriptObject();
    virtual ~ScriptObject();
    ScriptObject(const ScriptObject&) = delete;
    ScriptObject& operator=(const ScriptObject&) = delete;
    ScriptObject(ScriptObject&&) = delete;
    ScriptObject& operator=(ScriptObject&&) = delete;

    /// Property `property` changed in C++: writes it back to Lua (through Node::set, so a two-way bind
    /// writes its source and a function binding is left alone), emits `on<Property>Changed` with the new
    /// value (the handler, then the listeners), and requests a frame. Without a Lua link it only requests a frame.
    void notify(std::string_view property);

    /// Calls the handler `signal` (e.g. "onPeak"), then the listeners connected to it, in connect order. Element
    /// handlers and listeners get `(self, args...)`, singleton ones `(args...)`. Errors go to the error overlay (one
    /// listener's error doesn't stop the others). Requests a frame (a handler usually changes state). Returns true
    /// when the handler or a listener ran without an error: false without either, or without a Lua link.
    template <class... Args>
    bool emit(std::string_view signal, const Args&... args) {
        if (!m_state) return false;
        bool called = false;
        // Protected: emit() may run outside every Lua call (a posted function, a tick), where a Lua error raised by
        // an API call below (a script's __index, out of memory) would abort instead of being reported.
        luaui::protectedCall(m_state, "emit", [&] { called = callHandler(signal, args...); });
        requestFrame();
        return called;
    }

    /// Main thread: a new script is about to replace the one this singleton served (a hot reload). Called by
    /// TypeRegistry::install() for every singleton an earlier state had, before the new state links it and before the
    /// new script runs; the object is still linked to the old script's state (if it is still open), so notify() and
    /// emit() reach the old script. Override it to stop work started for the old script, so that the new one starts
    /// fresh. A reload whose script then fails keeps the old script running, and the hook isn't called again: the work
    /// stays stopped until the next successful reload starts it. An exception is reported. Elements never get it
    /// (they die with their state). The default does nothing.
    virtual void scriptReloading();

    /// Main thread: a Handle that worker threads use to post back to this object safely (see Handle::post).
    [[nodiscard]] Handle handle();

    /// Main thread: runs `work` on the background pool (runInBackground) and hands its result to `done` on the main
    /// thread, through handle(): `done` runs only while this object lives, so it may use `this`, notify() and emit().
    ///  * `work` is called as work(std::stop_token) or work(), on a worker thread, and returns R (or void). The token
    ///    stops when this object dies (its handles go dead) or the pool shuts down; work stopped before it starts is
    ///    skipped. It must not touch this object: give it copies (and Live<T>::writer()s).
    ///  * `done` is called as done(R&&) (done() for void) on the main thread, after `work` returned.
    ///  * `fail` gets the message of an exception `work` threw ("unknown C++ exception" for a non-std one), on the main
    ///    thread, also only while this object lives. Without `fail`, the exception is reported as a script error
    ///    ("runAsync (<type>): <what>").
    /// `work`, `done` and `fail` may be move-only. They are destroyed on whichever thread drops them last (a worker,
    /// or the main thread), so their captures must not need the main thread to be destroyed (no Lua references).
    /// The returned Task (ignore it if you don't need it) cancels the job: Task::cancel() stops `work`'s token too,
    /// and `done`/`fail` are then never called.
    template <class Work, class Done>
    Task runAsync(Work work, Done done) {
        return startJob(std::move(work), std::move(done), ReportFailure{});
    }
    template <class Work, class Done, class Fail>
        requires std::invocable<Fail&, std::string>
    Task runAsync(Work work, Done done, Fail fail) {
        return startJob(std::move(work), std::move(done), std::move(fail));
    }

    /// Main thread: runs `work` on the background pool, like runAsync(work, done), and returns a Future of its result
    /// (Future<void> for work returning void), which a method may return to Lua to be awaited:
    ///  * `work` is called as work(std::stop_token, imlui::Progress), work(std::stop_token) or work(); the Progress
    ///    reports the future's progress (Promise::progress: coalesced, any rate);
    ///  * the future is done with what `work` returned, or failed with the message of what it threw;
    ///  * cancelling it (Future::cancel(), fut:cancel() in Lua) stops the token `work` gets, like this object's death;
    ///  * this object's death cancels it (makePromise with handle().stopToken()): a result that arrives later is
    ///    dropped. Work stopped before it started is skipped, and its future cancelled.
    /// Settling runs on the main thread, in the one function the worker posts with the result (Promise::resolve): there
    /// is no main-thread step before it. `work` may be move-only; it is destroyed on a worker or the main thread, so
    /// its captures must not need the main thread.
    template <class Work>
    auto runAsync(Work work) {
        using Result = std::remove_cvref_t<decltype(invokeWithProgress(
            std::declval<Work&>(), std::declval<std::stop_token>(), std::declval<Progress>()))>;
        auto made = makePromise<Result>(handle().stopToken());
        startAsync([work = std::make_shared<Work>(std::move(work)),
                    promise = made.first](std::stop_token stop) -> std::function<void()> {
            // One token for the work: stopped by this object's death or the pool's shutdown (`stop`), or a cancel.
            std::stop_source both;
            const std::stop_callback onStop{std::move(stop), [&both] { both.request_stop(); }};
            const std::stop_callback onCancel{promise.stopToken(), [&both] { both.request_stop(); }};
            if (both.stop_requested()) return {}; // the promise is dropped unsettled: the future is cancelled
            try {
                if constexpr (std::is_void_v<Result>) {
                    invokeWithProgress(*work, both.get_token(), promise.reporter());
                    promise.resolve();
                } else {
                    promise.resolve(invokeWithProgress(*work, both.get_token(), promise.reporter()));
                }
            } catch (const std::exception& e) {
                promise.reject(e.what());
            } catch (...) {
                promise.reject("unknown C++ exception");
            }
            return {}; // settled through the promise: nothing to run here
        });
        return std::move(made.second);
    }

    /// Main thread: runAsync(work) with a main-thread step between the work and the future: `then` gets what `work`
    /// returned, on the main thread, and the future is done with what `then` returns. So `work` may build something big
    /// (the rows of a model) and `then` apply it (replace()) and return a small report, which is all Lua converts:
    ///  * `work` is called as in runAsync(work): work(std::stop_token, imlui::Progress), work(std::stop_token) or
    ///    work(), on the pool; its token stops when the future is cancelled or this object dies;
    ///  * `then` is called as then(W&&) (then() for work returning void) on the main thread, only while this object
    ///    lives and the future isn't cancelled, so it may use `this`, edit models, notify() and emit(); it returns the
    ///    future's value R (Future<void> for void);
    ///  * the future fails with the message of what `work` or `then` threw (`then` isn't called when `work` threw);
    ///  * the future settles synchronously right after `then` returns, in the same main-thread step: inside `then` it
    ///    is still pending, and once `then` returned it is done (its onSettled handlers, then Lua, run before anything
    ///    else). So a cancel, or this object's death, wins only before `then` starts: `then` never runs, and the
    ///    future is cancelled. A cancel from inside `then` cancels it too (what `then` returns is dropped).
    /// `work` and `then` may be move-only; they are destroyed on a worker or the main thread, so their captures must
    /// not need the main thread to be destroyed (no Lua references).
    template <class Work, class Then>
    auto runAsyncThen(Work work, Then then) {
        using Produced = std::remove_cvref_t<decltype(invokeWithProgress(
            std::declval<Work&>(), std::declval<std::stop_token>(), std::declval<Progress>()))>;
        using Result = std::remove_cvref_t<decltype(invokeThen<Produced>(std::declval<Then&>(), nullptr))>;
        struct Job {
            Work work;
            Then then;
        };
        auto made = makePromise<Result>(handle().stopToken());
        // The main-thread step settles the state itself, not through the promise (which posts). Weak: the closures
        // may die on a worker, where the state (it may hold a LuaFunction) must not be released; the promise keeps it.
        startAsync([job = std::make_shared<Job>(std::move(work), std::move(then)), promise = made.first,
                    settled = std::weak_ptr{future_detail::Access<Result>::state(made.second)}](
                       std::stop_token stop) -> std::function<void()> {
            // One token for the work: stopped by this object's death or the pool's shutdown (`stop`), or a cancel.
            std::stop_source both;
            const std::stop_callback onStop{std::move(stop), [&both] { both.request_stop(); }};
            const std::stop_callback onCancel{promise.stopToken(), [&both] { both.request_stop(); }};
            if (both.stop_requested()) return {}; // the promise is dropped unsettled: the future is cancelled
            std::shared_ptr<Produced> produced; // stays empty for work returning void
            try {
                if constexpr (std::is_void_v<Produced>)
                    invokeWithProgress(job->work, both.get_token(), promise.reporter());
                else
                    produced = std::make_shared<Produced>(
                        invokeWithProgress(job->work, both.get_token(), promise.reporter()));
            } catch (const std::exception& e) {
                promise.reject(e.what());
                return {};
            } catch (...) {
                promise.reject("unknown C++ exception");
                return {};
            }
            // Posted through the handle: runs only while this object lives (else the promise goes: cancelled). The
            // promise it holds keeps the state alive while it runs.
            return [job, promise, produced, settled] {
                const std::shared_ptr<future_detail::State<Result>> state = settled.lock();
                if (!state || state->status() != FutureStatus::Pending) return; // cancelled meanwhile: nothing applied
                try {
                    if constexpr (std::is_void_v<Result>) {
                        invokeThen<Produced>(job->then, produced.get());
                        state->resolve({});
                    } else {
                        state->resolve(invokeThen<Produced>(job->then, produced.get()));
                    }
                } catch (const std::exception& e) {
                    state->fail(e.what());
                } catch (...) {
                    state->fail("unknown C++ exception");
                }
            };
        });
        return std::move(made.second);
    }

    /// Main thread: calls `fn` once `f` settles (any way), on the main thread, only while this object lives: a
    /// Future::onSettled handler tied to this object's life, so `fn` may use `this`, notify() and emit().
    ///  * `fn` is called as fn(const Future<T>&) or fn(); at once when `f` is settled already (an empty future is
    ///    cancelled), else synchronously inside its settlement (after the handlers added before);
    ///  * once this object's handles die (cancelHandles(), which an element gets before destroyed(), or else
    ///    ScriptObject's destructor), `fn` is never called and is destroyed then, with what it captured; and so after
    ///    cancelHandles() (or from an object being destroyed) whenSettled does nothing.
    /// A derived destructor that cancels member futures calls cancelHandles() first, or `fn` runs inside it; it needn't
    /// cancel runAsync futures at all (this object's death cancels them). `fn` may be move-only.
    template <class T, class Fn>
        requires std::invocable<Fn&, const Future<T>&> || std::invocable<Fn&>
    void whenSettled(const Future<T>& f, Fn fn) {
        const Handle self = handle();
        if (!self.alive()) return;
        if (!f.pending()) { // like onSettled's at once (which an empty future skips)
            luaui::reportCxx([&] { invokeSettled(fn, f); }, "Future::onSettled");
            return;
        }
        // The handler holds `fn` through a slot that this object's death empties.
        struct Slot {
            struct Drop {
                Slot* slot;
                void operator()() const { slot->fn.reset(); }
            };
            std::optional<Fn> fn;
            std::optional<std::stop_callback<Drop>> dropOnDeath;
        };
        auto slot = std::make_shared<Slot>();
        slot->fn.emplace(std::move(fn));
        slot->dropOnDeath.emplace(self.stopToken(), typename Slot::Drop{slot.get()});
        f.onSettled([slot](const Future<T>& settledFuture) {
            if (!slot->fn) return; // this object died first
            // Out of the slot first: `fn` may kill this object (cancelHandles), which would empty the slot under it.
            Fn run = std::move(*slot->fn);
            slot->fn.reset();
            slot->dropOnDeath.reset();
            invokeSettled(run, settledFuture);
        });
    }

    /// Main thread: notify(property) once `f` settles, while this object lives (whenSettled). For a property backed by
    /// a member future (`filling` is m_fill.pending()): the getter reads the settled state then.
    template <class T>
    void notifyWhenSettled(const Future<T>& f, std::string_view property) {
        whenSettled(f, [this, name = std::string{property}] { notify(name); });
    }

    /// Main thread: makes every handle of this object dead now (alive() false, posts dropped), before the destructor
    /// gets to it. The registry calls it before an element's destroyed(); a derived class's destructor that waits
    /// for its workers, or a host destroying a singleton, calls it first, so the workers see the cancellation while
    /// the derived members still live. Handles taken afterwards are dead too.
    void cancelHandles();

    /// This object's id: unique for the process's lifetime, never 0 and never reused (a monotonic 64-bit count), so
    /// a stale id can't name a newer object that happens to live at a dead one's address.
    [[nodiscard]] std::uint64_t objectId() const;

    /// The Lua state this object is linked to, or nullptr.
    [[nodiscard]] lua::State* luaState() const;
    [[nodiscard]] bool isSingleton() const;

    /// Pushes the linked Lua value (element table or singleton userdata). Pushes nothing and returns
    /// false without a link, or when the element table was already collected.
    bool pushSelf() const;

    // ---- linking, used by TypeRegistry

    /// Links the object to the value at `idx` in `state`.
    void attach(lua::State* state, int idx, bool singleton);
    /// Points the link at `state` again, where attach() was already called (a reload that failed).
    void relink(lua::State* state);
    /// A listener was connected to one of this object's signals in its linked state: emit() and notify() call the
    /// listeners from now on (until attach() links it to a new state).
    void noteConnection();
    /// Drops the link (the state is closing or the instance is being destroyed).
    void detach();

private:
    /// runAsync() without a `fail`: the exception is reported.
    struct ReportFailure {};

    /// Calls `work` with the stop token, or without it.
    template <class Work>
    static decltype(auto) invokeWork(Work& work, std::stop_token stop) {
        if constexpr (std::invocable<Work&, std::stop_token>)
            return std::invoke(work, std::move(stop));
        else
            return std::invoke(work);
    }
    /// Calls `work` with the stop token and the progress reporter, or like invokeWork().
    template <class Work>
    static decltype(auto) invokeWithProgress(Work& work, std::stop_token stop, Progress progress) {
        if constexpr (std::invocable<Work&, std::stop_token, Progress>)
            return std::invoke(work, std::move(stop), std::move(progress));
        else
            return invokeWork(work, std::move(stop));
    }
    /// runAsyncThen()'s main-thread step: then(std::move(*produced)), or then() for work returning void.
    template <class Produced, class Then>
    static decltype(auto) invokeThen(Then& then, [[maybe_unused]] Produced* produced) {
        if constexpr (std::is_void_v<Produced>) {
            static_assert(std::invocable<Then&>, "runAsyncThen: then must be callable as then() for work returning void");
            return std::invoke(then);
        } else {
            static_assert(std::invocable<Then&, Produced&&>, "runAsyncThen: then must be callable as then(result)");
            return std::invoke(then, std::move(*produced));
        }
    }

    /// whenSettled()'s call: fn(f), or fn().
    template <class Fn, class T>
    static void invokeSettled(Fn& fn, const Future<T>& f) {
        if constexpr (std::invocable<Fn&, const Future<T>&>)
            std::invoke(fn, f);
        else
            std::invoke(fn);
    }

    /// runAsync()'s typed part: wraps work, done and fail into a job that runs `work` on a worker and returns the
    /// main-thread continuation (done or fail), which runs only if it ends the task (Task::finish).
    template <class Work, class Done, class Fail>
    Task startJob(Work work, Done done, Fail fail) {
        using Result =
            std::remove_cvref_t<decltype(invokeWork(std::declval<Work&>(), std::declval<std::stop_token>()))>;
        if constexpr (std::is_void_v<Result>)
            static_assert(std::invocable<Done&>, "runAsync: done must be callable as done() for work returning void");
        else
            static_assert(std::invocable<Done&, Result&&>, "runAsync: done must be callable as done(result)");
        struct Job {
            Work work;
            Done done;
            Fail fail;
            Task task;
            /// A job dropped without its continuation running (the object died, the pool shut down first) ends too.
            ~Job() { task.finish(); }
        };
        const Task task = Task::make();
        // Shared by the worker's function and the continuation: std::function needs copyable functions, and the
        // callables may be move-only.
        auto job = std::make_shared<Job>(std::move(work), std::move(done), std::move(fail), task);
        startAsync([job, context = asyncContext()](std::stop_token stop) -> std::function<void()> {
            // One token for the work: stopped by this object's death or the pool's shutdown (`stop`), or a cancel.
            std::stop_source both;
            const std::stop_callback onStop{std::move(stop), [&both] { both.request_stop(); }};
            const std::stop_callback onCancel{job->task.stopToken(), [&both] { both.request_stop(); }};
            if (job->task.cancelled()) return {};
            std::optional<std::string> what; // stays empty for an exception that isn't a std::exception
            try {
                if constexpr (std::is_void_v<Result>) {
                    invokeWork(job->work, both.get_token());
                    return [job] {
                        if (job->task.finish()) std::invoke(job->done);
                    };
                } else {
                    auto result = std::make_shared<Result>(invokeWork(job->work, both.get_token()));
                    return [job, result] {
                        if (job->task.finish()) std::invoke(job->done, std::move(*result));
                    };
                }
            } catch (const std::exception& e) {
                what = e.what();
            } catch (...) {
            }
            return [job, context, what = std::move(what)] {
                if (!job->task.finish()) return;
                if constexpr (std::is_same_v<Fail, ReportFailure>)
                    luaui::reportException("runAsync", context, what ? what->c_str() : nullptr);
                else
                    std::invoke(job->fail, what.value_or("unknown C++ exception"));
            };
        });
        return task;
    }

    /// runAsync()'s untyped core: queues `job` on the background pool with a token that stops when this object dies or
    /// the pool shuts down, and posts the continuation it returns through handle().
    void startAsync(std::function<std::function<void()>(std::stop_token)> job);
    /// " (<dynamic type>)": the context of a runAsync() error report.
    [[nodiscard]] std::string asyncContext() const;

    /// emit()'s body, inside its protected call. Returns whether the handler or a listener ran without an error.
    template <class... Args>
    bool callHandler(std::string_view signal, const Args&... args) {
        lua::State* state = m_state;
        lua::checkstack(state, static_cast<int>(sizeof...(Args)) + 8);
        const int top = lua::gettop(state);
        if (!pushSelf()) return false;
        const std::string key{signal};
        // A singleton's handlers are behind its userdata's __index; an element's are fields of its table.
        const lua::Type handler =
            m_singleton ? lua::getfield(state, top + 1, key.c_str()) : luaui::Node{state, top + 1}.pushRaw(key.c_str());
        if (handler != lua::Type::Function && !m_connected) {
            lua::settop(state, top);
            return false;
        }
        if (!m_singleton) lua::pushvalue(state, top + 1);
        (LuaConvert<LuaArgType<Args>>::push(state, args), ...);
        const bool ok = callSignal(top + 1, top + 2, key.c_str());
        lua::settop(state, top);
        return ok;
    }

    /// Calls a signal of the linked value at `self`: the handler slot's value is at `handler`, the arguments above it
    /// to the top (`self` first for an element). Calls the handler (when it is a function), then the listeners (once
    /// connected). Returns whether one of them ran without an error. Leaves the stack above `handler` undefined.
    bool callSignal(int self, int handler, const char* signal);

    /// notify()'s Lua part, inside its protected call.
    void writeBack(std::string_view property);

    std::uint64_t m_objectId;
    lua::State* m_state = nullptr;
    bool m_singleton = false;
    bool m_connected = false; ///< noteConnection() ran for the linked state: signals call the listeners too
    /// Shared with every Handle, created by the first handle(). Stopped by cancelHandles() or the destructor.
    std::stop_source m_handles{std::nostopstate};
    bool m_cancelled = false; ///< cancelHandles() ran: a handle taken later starts dead
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Registry key of the weak-valued table { [objectId()] = linked value }. Keyed by id, not address: an entry
/// a dead object left behind can't link a new object at its address.
constexpr const char* linksKey = "imlui.objects";

/// Pushes the links table, creating it on first use.
void pushLinks(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, linksKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::createtable(state, 0, 8);
    lua::createtable(state, 0, 1);
    lua::pushstring(state, "v");
    lua::setfield(state, -2, "__mode");
    lua::setmetatable(state, -2);
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, linksKey);
}

/// The last object id handed out. Atomic: a host may construct a singleton on any thread.
std::atomic<std::uint64_t> lastObjectId{0};

/// The links table key of an object id (Lua integers are 64-bit; ids stay far below 2^63).
lua::Integer linkKey(std::uint64_t id) { return static_cast<lua::Integer>(id); }

} // namespace

ScriptObject::ScriptObject() : m_objectId{lastObjectId.fetch_add(1, std::memory_order_relaxed) + 1} {}

std::uint64_t ScriptObject::objectId() const { return m_objectId; }

ScriptObject::~ScriptObject() {
    // Destructors run on the main thread, like the posted functions that check it: none can run past this point.
    m_handles.request_stop(); // no-op without a state
    if (!m_state) return;
    // Destroyed while still linked (e.g. a singleton that dies before the engine): forget the link.
    pushLinks(m_state);
    lua::pushnil(m_state);
    lua::rawseti(m_state, -2, linkKey(m_objectId));
    lua::pop(m_state);
}

ScriptObject::Handle::Handle(std::stop_source state) : m_state{std::move(state)} {}

bool ScriptObject::Handle::alive() const { return m_state.stop_possible() && !m_state.stop_requested(); }

bool ScriptObject::Handle::post(std::function<void()> fn) const {
    if (!fn || !alive()) return false;
    imlui::post([state = m_state, fn = std::move(fn)] {
        // On the main thread, where the object is destroyed too: it can't die between this check and fn().
        if (!state.stop_requested()) fn();
    });
    return true;
}

std::stop_token ScriptObject::Handle::stopToken() const {
    if (m_state.stop_possible()) return m_state.get_token();
    std::stop_source stopped;
    stopped.request_stop();
    return stopped.get_token();
}

ScriptObject::Handle ScriptObject::handle() {
    if (!m_handles.stop_possible()) {
        m_handles = std::stop_source{};
        if (m_cancelled) m_handles.request_stop();
    }
    return Handle{m_handles};
}

void ScriptObject::cancelHandles() {
    m_cancelled = true;
    m_handles.request_stop(); // no-op without a state
}

void ScriptObject::startAsync(std::function<std::function<void()>(std::stop_token)> job) {
    const Handle self = handle();
    if (!self.alive()) return; // cancelled already: nothing could receive the result
    runInBackground([self, job = std::move(job)](std::stop_token poolStop) {
        // One token for the work: stopped by the pool's shutdown or the object's death, whichever comes first. The
        // callbacks run on the thread that stops (a shutdown, the main thread), or right here when it already happened.
        std::stop_source stop;
        const std::stop_callback onShutdown{std::move(poolStop), [&stop] { stop.request_stop(); }};
        const std::stop_callback onDeath{self.stopToken(), [&stop] { stop.request_stop(); }};
        if (stop.stop_requested()) return; // nobody waits for this result any more
        self.post(job(stop.get_token()));
    });
}

std::string ScriptObject::asyncContext() const { return std::format(" ({})", typeid(*this).name()); }

lua::State* ScriptObject::luaState() const { return m_state; }
bool ScriptObject::isSingleton() const { return m_singleton; }

bool ScriptObject::pushSelf() const {
    if (!m_state) return false;
    pushLinks(m_state);
    const lua::Type t = lua::rawgeti(m_state, -1, linkKey(m_objectId));
    lua::remove(m_state, -2);
    if (t == lua::Type::Nil) {
        lua::pop(m_state);
        return false;
    }
    return true;
}

void ScriptObject::attach(lua::State* state, int idx, bool singleton) {
    idx = lua::absindex(state, idx);
    m_state = state;
    m_singleton = singleton;
    m_connected = false; // a new state has no listeners yet
    pushLinks(state);
    lua::pushvalue(state, idx);
    lua::rawseti(state, -2, linkKey(m_objectId));
    lua::pop(state);
}

void ScriptObject::relink(lua::State* state) {
    m_state = state;
    m_connected = true; // the state may hold listeners from before the reload: let the dispatch look
}

void ScriptObject::noteConnection() { m_connected = true; }

void ScriptObject::scriptReloading() {}

bool ScriptObject::callSignal(int self, int handler, const char* signal) {
    lua::State* state = m_state;
    const int fn = handler;
    const int nargs = lua::gettop(state) - fn;
    const bool connected = m_connected; // a listener the handler connects doesn't run in this emit
    bool ok = false;
    if (lua::isfunction(state, fn)) {
        if (connected) { // the listeners need the arguments too: the handler gets copies
            lua::checkstack(state, nargs + 3);
            for (int i = 0; i <= nargs; ++i)
                lua::pushvalue(state, fn + i);
        }
        const int n = m_singleton ? nargs : luaui::scopedArgs(state, self, nargs);
        ok = luaui::safeCall(state, n, 0);
    }
    if (connected) ok = luaui::callListeners(state, self, signal, fn + 1, nargs) || ok;
    return ok;
}

void ScriptObject::detach() { m_state = nullptr; }

void ScriptObject::notify(std::string_view property) {
    requestFrame();
    if (!m_state) return;
    // Protected: notify() may run outside every Lua call (a posted function, a tick), where a Lua error raised by an
    // API call (a script's __index/__newindex on the element table, out of memory) would abort instead of being
    // reported.
    luaui::protectedCall(m_state, "notify", [&] { writeBack(property); });
}

void ScriptObject::writeBack(std::string_view property) {
    lua::State* state = m_state;
    lua::checkstack(state, 16);
    const int top = lua::gettop(state);
    if (!pushSelf()) return;
    const int self = top + 1;
    const std::string name{property};

    // The current value comes from the getter, through the box metatable's __get(box, name) (installed
    // by TypeRegistry), so this module needs no compile-time knowledge of the type.
    if (m_singleton) {
        lua::pushvalue(state, self);
    } else {
        lua::pushstring(state, "__native");
        lua::rawget(state, self);
    }
    const int box = lua::gettop(state);
    bool ok = lua::isuserdata(state, box) && lua::getmetatable(state, box) != 0;
    if (ok) {
        lua::getfield(state, -1, "__get");
        lua::remove(state, -2);
        lua::pushvalue(state, box);
        lua::pushstring(state, name);
        ok = luaui::safeCall(state, 2, 1);
    }
    if (!ok) {
        lua::settop(state, top);
        return;
    }
    const int value = lua::gettop(state);

    if (!m_singleton) {
        lua::pushvalue(state, value);
        luaui::Node{state, self}.setTop(name.c_str());
    }

    const std::string signal = luaui::changedSignal(property);
    const lua::Type handler =
        m_singleton ? lua::getfield(state, self, signal.c_str()) : luaui::Node{state, self}.pushRaw(signal.c_str());
    if (handler == lua::Type::Function || m_connected) {
        if (!m_singleton) lua::pushvalue(state, self);
        lua::pushvalue(state, value);
        callSignal(self, value + 1, signal.c_str());
    }
    lua::settop(state, top);
}

} // namespace imlui
