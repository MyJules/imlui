// Live<T>: a property value that any thread sets and the UI sees at most once per main-loop iteration, latest value
// wins (telemetry, progress, status text a worker produces at any rate).
//
//   class Backend : public imlui::ScriptObject {
//       imlui::Live<double> m_load{*this, "load"};   // the owner and its declared property
//   public:
//       static void declare(imlui::TypeBuilder<Backend>& t) { t.property("load", &Backend::load); }
//       double load() const { return m_load.get(); }
//   };
//   m_load.set(0.73);                  // any thread, while the owner lives
//   auto load = m_load.writer();       // a copy for a worker that may outlive the owner
//   load.set(0.74);                    // any thread, any time
//
// set() stores the value under a lock and, unless an apply is already queued, posts one through the owner's handle
// (ScriptObject::Handle::post). The apply runs on the main thread, only while the owner lives: it moves the latest
// value to the main-thread side (get()) and calls owner.notify(property), which writes it back to Lua, emits
// on<Property>Changed and requests a frame. Sets from the main thread are deferred the same way.
export module imlui.types.live;

import std;

import imlui.types.script_object;

export namespace imlui {

template <class T>
class Live {
    struct State;

public:
    /// A thread-safe, copyable setter for a Live, for a worker that may outlive the owner: once the owner is gone, its
    /// set() does nothing.
    class Writer {
    public:
        /// An empty writer: set() does nothing.
        Writer() = default;
        /// Thread-safe: like Live::set().
        void set(T value) const {
            if (m_state) Live::store(m_state, std::move(value));
        }

    private:
        friend class Live;
        explicit Writer(std::shared_ptr<State> state) : m_state{std::move(state)} {}
        std::shared_ptr<State> m_state;
    };

    /// Main thread, in the owner's constructor (a member initializer): the value of `owner`'s declared property
    /// `property`, starting at `initial`.
    Live(ScriptObject& owner, std::string property, T initial = T{})
        : m_state{std::make_shared<State>(owner, owner.handle(), std::move(property), std::move(initial))} {}
    ~Live() = default;
    Live(const Live&) = delete;
    Live& operator=(const Live&) = delete;
    Live(Live&&) = delete;
    Live& operator=(Live&&) = delete;

    /// Any thread, while the owner lives (otherwise use a writer()): stores `value` as the latest; the main thread
    /// applies it at its next loop iteration, once for all the sets made until then.
    void set(T value) { store(m_state, std::move(value)); }

    /// Main thread: the last applied value (what Lua sees).
    [[nodiscard]] const T& get() const { return m_state->current; }

    /// A setter that stays safe to use after the owner died.
    [[nodiscard]] Writer writer() const { return Writer{m_state}; }

private:
    struct State {
        State(ScriptObject& o, ScriptObject::Handle h, std::string p, T initial)
            : owner{&o}, handle{std::move(h)}, property{std::move(p)}, current{std::move(initial)} {}
        ScriptObject* owner;         ///< used only by the apply, which runs only while it lives
        ScriptObject::Handle handle; ///< the owner's
        std::string property;
        T current; ///< main thread only
        std::mutex mutex;
        std::optional<T> pending; ///< guarded by `mutex`: set since the last apply
        bool queued = false;      ///< guarded by `mutex`: an apply is posted and hasn't run yet
    };

    static void store(const std::shared_ptr<State>& state, T value) {
        {
            std::scoped_lock lock{state->mutex};
            state->pending = std::move(value);
            if (state->queued) return;
            state->queued = true;
        }
        // A dead owner drops the post, and `queued` stays set: no set() posts again.
        state->handle.post([state] { apply(*state); });
    }

    /// Main thread, while the owner lives (Handle::post).
    static void apply(State& state) {
        {
            std::scoped_lock lock{state.mutex};
            state.queued = false;
            if (!state.pending) return;
            state.current = std::move(*state.pending);
            state.pending.reset();
        }
        state.owner->notify(state.property);
    }

    std::shared_ptr<State> m_state;
};

} // namespace imlui
