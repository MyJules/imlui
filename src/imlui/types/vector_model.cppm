// VectorModel<T>: a ListModel (imlui.types.list_model) over a std::vector<T>, for the usual case: rows that are values
// with a LuaConvert<T>, typically a LuaFields struct. Every edit calls the matching notification, so a ListView or
// Repeater on it rebuilds only the delegates of the rows that changed.
//
//   struct LogLine { std::int64_t id = 0; std::string text; bool operator==(const LogLine&) const = default; };
//   template <> struct imlui::LuaFields<LogLine> { ... };
//
//   imlui::VectorModel<LogLine> logs{[](const LogLine& l) { return l.id; }}; // optional: keys (integer or string)
//   logs.append({1, "started"});
//   logs.set(0, {1, "started (2 s)"});      // rowsChanged(0, 1), only when the value differs
//   logs.replace(std::move(fresh));          // keeps the rows (and delegates) whose key and value are the same
//   logs.setMaxRows(1000);                   // a cap: edits that add rows erase the oldest (first) rows beyond it
//
// Main thread only, like every ListModel. appender() gives worker threads a thread-safe appender whose rows reach the
// model in one batch per main-loop iteration (one rowsInserted), like Live<T>; appender(onApplied) one whose applies
// also call onApplied(appended, erased) in the same main-thread step. A template, so it lives in the interface
// (CODE_STYLE).
export module imlui.types.vector_model;

import std;

import lua;
import imlui.types.list_model;
import imlui.types.lua_convert;
import imlui.types.script_object;

export namespace imlui {

template <class T>
    requires LuaPushable<T>
class VectorModel : public ListModel {
    struct Batch;

public:
    /// The rows' type (LuaConvert<ListModel>: `ListModel<T>` in the Lua definitions).
    using Item = T;

    /// What appender(onApplied) calls on the main thread after each apply: `appended` rows arrived in the batch, and
    /// `erased` rows went to honor maxRows() (the oldest rows, and the batch's own first rows when it alone is over the
    /// cap), so the model's size grew by appended - erased.
    using OnApplied = std::function<void(std::size_t appended, std::size_t erased)>;

    /// A thread-safe, copyable way to append rows from a worker. Its rows reach the model on the main thread, all that
    /// were appended since the last apply in one appendRows (honoring maxRows()), while the model lives; afterwards
    /// they are dropped. An empty appender (default-constructed) drops them too. With maxRows() set, rows waiting for
    /// the apply are kept to about twice the cap (the oldest go first, as the apply would erase them anyway), so a
    /// worker faster than the main loop doesn't pile up rows that never show.
    class Appender {
    public:
        Appender() = default;
        /// Any thread.
        void append(T row) const {
            if (m_batch) store(m_batch, [&](std::vector<T>& rows) { rows.push_back(std::move(row)); });
        }
        /// Any thread.
        void appendRows(std::vector<T> rows) const {
            if (m_batch && !rows.empty())
                store(m_batch, [&](std::vector<T>& pending) {
                    if (pending.empty())
                        pending = std::move(rows);
                    else
                        pending.insert(pending.end(), std::make_move_iterator(rows.begin()),
                                       std::make_move_iterator(rows.end()));
                });
        }

    private:
        friend class VectorModel;
        explicit Appender(std::shared_ptr<Batch> batch) : m_batch{std::move(batch)} {}
        std::shared_ptr<Batch> m_batch;
    };

    /// A model without keys: delegates are kept per index.
    VectorModel() = default;
    /// A model whose rows are identified by `keyOf(row)`, an integer or a string (anything convertible to
    /// std::string): delegates follow their rows across inserts, removals, moves and replace().
    template <class KeyOf>
        requires std::invocable<const KeyOf&, const T&>
    explicit VectorModel(KeyOf keyOf)
        : m_keyOf{[keyOf = std::move(keyOf)](const T& row) -> Key {
              using R = std::remove_cvref_t<std::invoke_result_t<const KeyOf&, const T&>>;
              if constexpr (std::is_integral_v<R>)
                  return static_cast<std::int64_t>(std::invoke(keyOf, row));
              else
                  return std::string{std::invoke(keyOf, row)};
          }} {}
    ~VectorModel() override { cancelHandles(); } // a queued batch never applies to a half-destroyed model
    VectorModel(const VectorModel&) = delete;
    VectorModel& operator=(const VectorModel&) = delete;
    VectorModel(VectorModel&&) = delete;
    VectorModel& operator=(VectorModel&&) = delete;

    // ---- ListModel

    [[nodiscard]] std::size_t count() const override { return m_rows.size(); }
    void pushItem(lua::State* state, std::size_t row) const override { LuaConvert<T>::push(state, m_rows.at(row)); }
    [[nodiscard]] bool hasKeys() const override { return static_cast<bool>(m_keyOf); }
    [[nodiscard]] std::optional<Key> key(std::size_t row) const override {
        if (!m_keyOf) return std::nullopt;
        return m_keyOf(m_rows.at(row));
    }

    // ---- reading

    [[nodiscard]] std::size_t size() const { return m_rows.size(); }
    [[nodiscard]] bool empty() const { return m_rows.empty(); }
    /// Row `row` (0-based); throws std::out_of_range past the end.
    [[nodiscard]] const T& at(std::size_t row) const { return m_rows.at(row); }
    [[nodiscard]] const T& operator[](std::size_t row) const { return m_rows[row]; }
    [[nodiscard]] const std::vector<T>& rows() const { return m_rows; }

    // ---- editing (each calls its notification). Indices are 0-based; one past the end throws std::out_of_range.

    // With maxRows() set, the edits that add rows keep the model within the cap by erasing its oldest (first) rows:
    // append and appendRows erase them first (one rowsRemoved, then rowsInserted, so count() never exceeds the cap,
    // even between the two), insert and insertRows after inserting; replace keeps the last maxRows() rows.

    /// Appends `row` (erasing the first row first when the model is at maxRows()).
    void append(T row) {
        if (m_maxRows > 0 && m_rows.size() >= m_maxRows) erase(0, m_rows.size() - m_maxRows + 1);
        m_rows.push_back(std::move(row));
        rowsInserted(m_rows.size() - 1, 1);
    }
    /// Appends `rows`, erasing the oldest rows first (one rowsRemoved) to stay within maxRows(); when `rows` alone is
    /// over the cap, only its last maxRows() rows are appended.
    void appendRows(std::vector<T> rows) { appendCapped(std::move(rows)); }
    void insert(std::size_t row, T value) {
        checkIndex(row, m_rows.size());
        m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(row), std::move(value));
        rowsInserted(row, 1);
        trim();
    }
    void insertRows(std::size_t row, std::vector<T> rows) {
        checkIndex(row, m_rows.size());
        if (rows.empty()) return;
        const std::size_t n = rows.size();
        m_rows.insert(m_rows.begin() + static_cast<std::ptrdiff_t>(row), std::make_move_iterator(rows.begin()),
                      std::make_move_iterator(rows.end()));
        rowsInserted(row, n);
        trim();
    }
    /// Removes `count` rows from `row` (fewer at the end).
    void erase(std::size_t row, std::size_t count = 1) {
        checkIndex(row, m_rows.size());
        count = std::min(count, m_rows.size() - row);
        if (count == 0) return;
        const auto begin = m_rows.begin() + static_cast<std::ptrdiff_t>(row);
        m_rows.erase(begin, begin + static_cast<std::ptrdiff_t>(count));
        rowsRemoved(row, count);
    }
    /// Replaces row `row`; nothing happens when it holds `value` already (T with operator==).
    void set(std::size_t row, T value) {
        checkIndex(row, m_rows.size() - 1, m_rows.empty());
        if constexpr (std::equality_comparable<T>) {
            if (m_rows[row] == value) return;
        }
        m_rows[row] = std::move(value);
        rowsChanged(row, 1);
    }
    /// Edits row `row` in place: fn(T&), then rowsChanged.
    template <class Fn>
        requires std::invocable<Fn&, T&>
    void update(std::size_t row, Fn fn) {
        checkIndex(row, m_rows.size() - 1, m_rows.empty());
        std::invoke(fn, m_rows[row]);
        rowsChanged(row, 1);
    }
    /// Moves `count` rows from `from` so that they start at `to` (an index after the move): rowsMoved.
    void move(std::size_t from, std::size_t count, std::size_t to) {
        if (count == 0) return;
        if (from > m_rows.size() || count > m_rows.size() - from || to > m_rows.size() - count)
            throw std::out_of_range{"VectorModel::move: rows out of range"};
        const auto at = [this](std::size_t i) { return m_rows.begin() + static_cast<std::ptrdiff_t>(i); };
        if (to < from)
            std::rotate(at(to), at(from), at(from + count));
        else if (to > from)
            std::rotate(at(from), at(from + count), at(to + count));
        else
            return;
        rowsMoved(from, count, to);
    }
    /// Sorts the rows (stable) by `less`: rows keep their identity, so their items and delegates are kept.
    template <class Less = std::less<>>
    void sort(Less less = {}) {
        std::vector<std::size_t> order(m_rows.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return less(m_rows[a], m_rows[b]); });
        std::vector<T> sorted;
        sorted.reserve(m_rows.size());
        std::vector<std::optional<std::size_t>> previous(order.begin(), order.end());
        for (const std::size_t i : order)
            sorted.push_back(std::move(m_rows[i]));
        m_rows = std::move(sorted);
        rowsRemapped(previous);
    }
    /// Replaces every row with `rows` (a refreshed query). A new row that equals an old one (T with operator==) keeps
    /// that row's identity, so its item and delegate are kept: matched by key when the model has keys (the first old
    /// row with that key), else at the same index.
    void replace(std::vector<T> rows) {
        if (m_maxRows > 0 && rows.size() > m_maxRows)
            rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - m_maxRows));
        std::vector<std::optional<std::size_t>> previous(rows.size());
        if constexpr (std::equality_comparable<T>) {
            if (m_keyOf) {
                std::unordered_map<Key, std::vector<std::size_t>> byKey; // old rows per key, last first
                for (std::size_t i = m_rows.size(); i-- > 0;)
                    byKey[m_keyOf(m_rows[i])].push_back(i);
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    const auto it = byKey.find(m_keyOf(rows[r]));
                    if (it == byKey.end() || it->second.empty()) continue;
                    const std::size_t old = it->second.back();
                    it->second.pop_back();
                    if (m_rows[old] == rows[r]) previous[r] = old;
                }
            } else {
                for (std::size_t r = 0; r < std::min(rows.size(), m_rows.size()); ++r)
                    if (m_rows[r] == rows[r]) previous[r] = r;
            }
        }
        m_rows = std::move(rows);
        rowsRemapped(previous);
    }
    void clear() {
        if (m_rows.empty()) return;
        const std::size_t n = m_rows.size();
        m_rows.clear();
        rowsRemoved(0, n);
    }

    /// The most rows the model keeps (0, the default: no cap). See the editing members.
    [[nodiscard]] std::size_t maxRows() const { return m_maxRows; }
    /// Sets the cap (0: none). Lowering it below size() erases the oldest rows now (one rowsRemoved).
    void setMaxRows(std::size_t maxRows) {
        m_maxRows = maxRows;
        if (m_sharedMaxRows) m_sharedMaxRows->store(maxRows, std::memory_order_relaxed);
        trim();
    }

    /// Main thread: an appender for worker threads (see Appender). The appenders it returns share one batch: one apply
    /// per main-loop iteration for all of them.
    [[nodiscard]] Appender appender() {
        if (!m_batch) m_batch = std::make_shared<Batch>(*this, handle(), sharedMaxRows(), OnApplied{});
        return Appender{m_batch};
    }
    /// Main thread: an appender with a batch of its own (shared by its copies), whose every apply calls
    /// `onApplied(appended, erased)` on the main thread right after the append, in the same step (no frame comes
    /// between them), while the model lives: count the applies, keep stats, notify(). An exception it throws is
    /// reported as a script error. `onApplied` is destroyed with the appender's last copy, on any thread, so its
    /// captures must not need the main thread to be destroyed (no Lua references). It may use the model's owner
    /// (`this`) when the owner outlives the model: it runs only while the model lives.
    [[nodiscard]] Appender appender(OnApplied onApplied) {
        return Appender{std::make_shared<Batch>(*this, handle(), sharedMaxRows(), std::move(onApplied))};
    }

private:
    struct Batch {
        Batch(VectorModel& m, ScriptObject::Handle h, std::shared_ptr<const std::atomic<std::size_t>> cap,
              OnApplied applied)
            : model{&m}, handle{std::move(h)}, maxRows{std::move(cap)}, onApplied{std::move(applied)} {}
        VectorModel* model;          ///< used only by the apply, which runs only while it lives
        ScriptObject::Handle handle; ///< the model's
        std::shared_ptr<const std::atomic<std::size_t>> maxRows; ///< the model's maxRows(), readable from any thread
        OnApplied onApplied;                                     ///< called by the apply (main thread); may be empty
        std::mutex mutex;
        std::vector<T> pending;  ///< guarded by `mutex`
        std::size_t dropped = 0; ///< guarded by `mutex`: rows taken off the front of `pending` for the cap
        bool queued = false;     ///< guarded by `mutex`: an apply is posted and hasn't run yet
    };

    /// The cap the batches read (created with the first appender).
    std::shared_ptr<const std::atomic<std::size_t>> sharedMaxRows() {
        if (!m_sharedMaxRows) m_sharedMaxRows = std::make_shared<std::atomic<std::size_t>>(m_maxRows);
        return m_sharedMaxRows;
    }

    /// Any thread: adds to the batch with `add(pending)` and posts its apply unless one is queued already.
    template <class Add>
    static void store(const std::shared_ptr<Batch>& batch, const Add& add) {
        if (!batch->handle.alive()) return; // the model is gone: nothing would apply the rows
        {
            const std::scoped_lock lock{batch->mutex};
            add(batch->pending);
            // Beyond the cap, the apply keeps only the last maxRows rows: once twice as many wait, the oldest go now
            // (once per maxRows appends, so an append stays O(1) on average). Counted, so the apply still reports them.
            const std::size_t cap = batch->maxRows->load(std::memory_order_relaxed);
            std::vector<T>& pending = batch->pending;
            if (cap > 0 && pending.size() > cap && pending.size() - cap >= cap) {
                const std::size_t extra = pending.size() - cap;
                pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(extra));
                batch->dropped += extra;
            }
            if (batch->queued) return;
            batch->queued = true;
        }
        // A dead model drops the post, and `queued` stays set: nothing posts again.
        batch->handle.post([batch] {
            std::vector<T> rows;
            std::size_t dropped = 0;
            {
                const std::scoped_lock lock{batch->mutex};
                batch->queued = false;
                rows = std::move(batch->pending);
                batch->pending.clear();
                dropped = std::exchange(batch->dropped, 0);
            }
            // The rows trimmed while waiting arrived and went, like the batch's own first rows over the cap.
            const std::size_t appended = rows.size() + dropped;
            const std::size_t erased = batch->model->appendCapped(std::move(rows)) + dropped;
            if (batch->onApplied) batch->onApplied(appended, erased);
        });
    }

    /// Appends `rows`, first erasing what maxRows() needs gone: the oldest rows (one rowsRemoved), and the first rows
    /// of `rows` when it alone is over the cap. Returns how many rows that dropped (old and new).
    std::size_t appendCapped(std::vector<T> rows) {
        if (rows.empty()) return 0;
        std::size_t dropped = 0;
        if (m_maxRows > 0) {
            if (rows.size() > m_maxRows) {
                dropped = rows.size() - m_maxRows;
                rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(dropped));
            }
            const std::size_t room = m_maxRows - rows.size();
            if (m_rows.size() > room) {
                const std::size_t old = m_rows.size() - room;
                m_rows.erase(m_rows.begin(), m_rows.begin() + static_cast<std::ptrdiff_t>(old));
                rowsRemoved(0, old);
                dropped += old;
            }
        }
        const std::size_t first = m_rows.size();
        const std::size_t n = rows.size();
        if (m_rows.empty())
            m_rows = std::move(rows);
        else
            m_rows.insert(m_rows.end(), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
        rowsInserted(first, n);
        return dropped;
    }

    /// Erases the oldest rows beyond maxRows() (one rowsRemoved).
    void trim() {
        if (m_maxRows > 0 && m_rows.size() > m_maxRows) erase(0, m_rows.size() - m_maxRows);
    }

    /// Throws std::out_of_range unless row <= last (and the model isn't `empty` for a row that must exist).
    static void checkIndex(std::size_t row, std::size_t last, bool none = false) {
        if (none || row > last) throw std::out_of_range{"VectorModel: row " + std::to_string(row) + " out of range"};
    }

    std::vector<T> m_rows;
    std::function<Key(const T&)> m_keyOf;
    std::size_t m_maxRows = 0;      ///< 0: no cap
    std::shared_ptr<Batch> m_batch; ///< appender()'s shared batch
    /// m_maxRows for the batches, which read it on the appending threads; made by the first appender.
    std::shared_ptr<std::atomic<std::size_t>> m_sharedMaxRows;
};

} // namespace imlui
