// Combinators over Future<T> (imlui.types.future): futures made from other futures, on the main thread.
//
//   imlui::Future<Report> Backend::fetchWithin(std::string url, int ms) {
//       return imlui::withTimeout(fetch(std::move(url)), std::chrono::milliseconds{ms}); // fails "timed out after N ms"
//   }
//   imlui::Future<void> Lab::later(int ms) { return imlui::delay(std::chrono::milliseconds{ms}); }
//   imlui::Future<std::vector<Report>> Backend::fetchAll(std::vector<std::string> urls); // all(...) of fetch(url)
//
// - delay(ms) -> Future<void>: done after `ms` on the app clock, a main-thread timer (imlui.ui.main_timers) the loop
//   runs without a Lua state and wakes for. Cancelling it removes the timer; a timer dropped without ringing
//   (clearMainTimers: the Engine's shutdown) cancels it.
// - withTimeout(f, ms) -> Future<T>: settles like `f` (with its progress) or, if `f` is still pending after `ms`, fails
//   with "timed out after <ms> ms" and cancels `f`.
// - race(f1, f2, ...) / race(vector) -> Future<T>: settles like the first input to settle, whichever way (done, failed
//   or cancelled); the losers are cancelled. An input settled already wins at once (the first in order). No inputs:
//   cancelled.
// - all(vector<Future<T>>) -> Future<std::vector<T>> (Future<void> for void): done with the values in order once all are
//   done; failed or cancelled like the first input that fails or is cancelled, and then the others are cancelled.
//
// Shared rules. The result settles synchronously when the input that decides it settles (inside its settlement, on the
// main thread), so a chain of combinators adds no loop iteration. Cancelling the result, or its settling any other
// way, cancels the inputs still pending (Future::cancel: an adopted Lua future is cancelled outright), so nothing
// keeps running for a result nobody can get; the timers of delay and withTimeout are removed. Watching an input counts
// as handling its rejection; the result's own rejection is reported by Lua as usual once pushed. An empty
// (default-constructed) input counts as cancelled. A value is copied from the input (T must be copyable). While an
// input is pending, it keeps the result alive (its handler holds it, like a promise), and the result keeps the inputs:
// every such link is a handler, released when its future settles (see the guarantees in imlui.types.future), and every
// input is settled by the time the result is, so nothing outlives the settlement.
//
// Lua futures as inputs. A Future<T> argument adopts any Lua future (see imlui.types.future); Future<void> takes any
// one, ignoring its values (sleep(ms), an async function), and Future<std::optional<T>> one with a value or none. So a
// method `Future<void> timeout(Future<void> f, int ms)` takes every Lua future (but returns no value); a typed one keeps
// the value. Scripts that want both have the prelude's timeout(f, ms) and race{...}.
// Main thread only.
export module imlui.types.future_combinators;

import std;

import imlui.types.future;
import imlui.ui.main_timers;

export namespace imlui {

/// A future done after `ms` (<= 0: at the next loop iteration) on the app clock. Cancelling it removes its timer.
[[nodiscard]] Future<void> delay(std::chrono::milliseconds ms);

} // namespace imlui

// The templates' helpers. Exported because the templates use them; not an API of their own.
export namespace imlui::future_combinators_detail {

template <class T>
using StatePtr = std::shared_ptr<future_detail::State<T>>;

template <class T>
concept Copyable = std::is_void_v<T> || std::copy_constructible<T>;

/// Settles pending `out` the way settled `in` did (cancelled for none). Does nothing once `out` settled.
template <class T>
void settleLike(future_detail::State<T>& out, const future_detail::State<T>* in) {
    if (out.status() != FutureStatus::Pending) return;
    switch (in ? in->status() : FutureStatus::Cancelled) {
    case FutureStatus::Done:
        out.resolve(*in->value());
        break;
    case FutureStatus::Failed:
        out.fail(in->error());
        break;
    case FutureStatus::Pending: // not settled: never called so
    case FutureStatus::Cancelled:
        out.cancel();
        break;
    }
}

/// Once `out` settles (any way), cancels each input still pending. Doesn't count as handling `out`'s rejection.
template <class T, class U>
void cancelInputsOnSettle(const StatePtr<T>& out, std::vector<StatePtr<U>> inputs) {
    out->addHandler(
        [inputs = std::move(inputs)] {
            for (const auto& in : inputs)
                if (in) in->cancel(); // does nothing once settled
        },
        false);
}

} // namespace imlui::future_combinators_detail

export namespace imlui {

/// A future that settles like `f` (and reports its progress), unless `f` is still pending after `ms`: then it fails
/// with "timed out after <ms> ms" and `f` is cancelled. Cancelling it cancels `f`.
template <class T>
    requires future_combinators_detail::Copyable<T>
[[nodiscard]] Future<T> withTimeout(Future<T> f, std::chrono::milliseconds ms) {
    using Access = future_detail::Access<T>;
    namespace d = future_combinators_detail;
    auto out = std::make_shared<future_detail::State<T>>();
    const d::StatePtr<T>& in = Access::state(f);
    if (!in || in->status() != FutureStatus::Pending) {
        d::settleLike(*out, in.get());
        return Access::wrap(std::move(out));
    }
    const Future<void> clock = delay(ms);
    const d::StatePtr<void>& timer = future_detail::Access<void>::state(clock);
    d::cancelInputsOnSettle(out, std::vector{in});
    d::cancelInputsOnSettle(out, std::vector{timer});
    if (const auto progress = in->progress()) out->setProgress(*progress);
    in->addProgressHandler([weak = std::weak_ptr{out}](double value) {
        if (const auto o = weak.lock()) o->setProgress(value);
    });
    // The handlers' raw pointers are their own future's state: alive whenever it runs them.
    in->addHandler([out, from = in.get()] { d::settleLike(*out, from); });
    timer->addHandler([out, rang = timer.get(), ms] {
        if (out->status() != FutureStatus::Pending) return;
        // Out's handler then cancels `f`. A timer cancelled otherwise was dropped (shutdown): it will never ring.
        if (rang->status() == FutureStatus::Done)
            out->fail(std::format("timed out after {} ms", ms.count()));
        else
            out->cancel();
    });
    return Access::wrap(std::move(out));
}

/// A future that settles like the first of `futures` to settle (done, failed or cancelled; one settled already wins at
/// once, the first in order). The others are cancelled then. Cancelling it cancels them all. No futures: cancelled.
template <class T>
    requires future_combinators_detail::Copyable<T>
[[nodiscard]] Future<T> race(std::vector<Future<T>> futures) {
    using Access = future_detail::Access<T>;
    namespace d = future_combinators_detail;
    auto out = std::make_shared<future_detail::State<T>>();
    std::vector<d::StatePtr<T>> inputs;
    inputs.reserve(futures.size());
    for (const Future<T>& f : futures)
        inputs.push_back(Access::state(f));
    d::cancelInputsOnSettle(out, inputs);
    if (inputs.empty()) out->cancel();
    for (const d::StatePtr<T>& in : inputs) {
        if (out->status() != FutureStatus::Pending) break; // decided: the rest were cancelled
        if (!in) {
            out->cancel(); // an empty future: cancelled already
            break;
        }
        in->addHandler([out, from = in.get()] { d::settleLike(*out, from); });
    }
    return Access::wrap(std::move(out));
}

/// race({first, more...}).
template <class T, class... More>
    requires future_combinators_detail::Copyable<T> && (std::same_as<std::remove_cvref_t<More>, Future<T>> && ...)
[[nodiscard]] Future<T> race(Future<T> first, More&&... more) {
    return race(std::vector<Future<T>>{std::move(first), std::forward<More>(more)...});
}

/// A future done with the values of `futures`, in order, once all are done (Future<void> for void ones; at once for
/// none). Fails or is cancelled like the first of them that fails or is cancelled; the others are cancelled then.
/// Cancelling it cancels them all.
template <class T>
    requires future_combinators_detail::Copyable<T>
[[nodiscard]] auto all(std::vector<Future<T>> futures)
    -> Future<std::conditional_t<std::is_void_v<T>, void, std::vector<T>>> {
    using R = std::conditional_t<std::is_void_v<T>, void, std::vector<T>>;
    namespace d = future_combinators_detail;
    // What the inputs' handlers share: how many aren't done yet and, for values, the values so far.
    struct Gather {
        std::size_t left = 0;
        std::conditional_t<std::is_void_v<T>, std::monostate, std::vector<std::optional<T>>> values;
    };
    auto out = std::make_shared<future_detail::State<R>>();
    std::vector<d::StatePtr<T>> inputs;
    inputs.reserve(futures.size());
    for (const Future<T>& f : futures)
        inputs.push_back(future_detail::Access<T>::state(f));
    d::cancelInputsOnSettle(out, inputs);
    auto gather = std::make_shared<Gather>();
    gather->left = inputs.size();
    if constexpr (!std::is_void_v<T>) gather->values.resize(inputs.size());
    const auto finish = [](future_detail::State<R>& o, Gather& g) {
        if constexpr (std::is_void_v<T>) {
            o.resolve({});
        } else {
            std::vector<T> values;
            values.reserve(g.values.size());
            for (std::optional<T>& v : g.values)
                values.push_back(std::move(*v));
            o.resolve(std::move(values));
        }
    };
    if (inputs.empty()) finish(*out, *gather);
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (out->status() != FutureStatus::Pending) break; // decided: the rest were cancelled
        const d::StatePtr<T>& in = inputs[i];
        if (!in) {
            out->cancel(); // an empty future: cancelled already
            break;
        }
        in->addHandler([out, gather, finish, i, from = in.get()] {
            if (out->status() != FutureStatus::Pending) return;
            if (from->status() != FutureStatus::Done) {
                if (from->status() == FutureStatus::Failed)
                    out->fail(from->error());
                else
                    out->cancel();
                return;
            }
            if constexpr (std::is_void_v<T>)
                static_cast<void>(i);
            else
                gather->values[i].emplace(*from->value());
            if (--gather->left == 0) finish(*out, *gather);
        });
    }
    return future_detail::Access<R>::wrap(std::move(out));
}

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// What a delay's timer holds: its state, like a promise. Dropped without ringing (clearMainTimers: the Engine's
/// shutdown, a test harness), it cancels the future, so what waits on it isn't left pending.
struct DelayTimer {
    std::shared_ptr<future_detail::State<void>> state;

    DelayTimer(const DelayTimer&) = delete;
    DelayTimer& operator=(const DelayTimer&) = delete;
    DelayTimer(DelayTimer&&) = delete;
    DelayTimer& operator=(DelayTimer&&) = delete;
    explicit DelayTimer(std::shared_ptr<future_detail::State<void>> s) : state{std::move(s)} {}
    ~DelayTimer() {
        if (state) state->cancel(); // does nothing once settled
    }
    void ring() {
        if (const auto s = std::exchange(state, nullptr)) s->resolve({});
    }
};

} // namespace

Future<void> delay(std::chrono::milliseconds ms) {
    auto state = std::make_shared<future_detail::State<void>>();
    const double seconds = std::chrono::duration<double>(ms).count();
    const MainTimerId id =
        addMainTimer(seconds, [timer = std::make_shared<DelayTimer>(state)] { timer->ring(); });
    state->addHandler([id] { removeMainTimer(id); }, false); // settled another way: the timer goes (no-op once it rang)
    return future_detail::Access<void>::wrap(std::move(state));
}

} // namespace imlui
