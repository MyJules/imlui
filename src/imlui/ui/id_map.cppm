// IdMap: a hash map from 64-bit ids (table addresses, ImGui IDs) to values, for the renderer's per-container memos
// (the children memo, the culling memo), looked up on every walk over a container's children, a few thousand times a
// frame on a big page. std::unordered_map hashes a key byte by byte (FNV-1a in the MSVC STL) and keeps each entry in
// a node of its own; this one is open addressing with linear probing over one array (a multiplicative hash of the key,
// at most half full), so a lookup is a multiply and, mostly, one cache line (on the `nested` bench, ~1.5% of a frame
// against std::unordered_map). Like any open-addressing table, an insertion may move every value: don't keep a
// reference to a value across an insertion (store a std::unique_ptr when values must stay put).
export module imlui.ui.id_map;

import std;

export namespace imlui {

template <class V>
class IdMap {
public:
    /// The value of `key`, or nullptr.
    [[nodiscard]] V* find(std::uint64_t key) {
        if (m_size == 0) return nullptr;
        for (std::size_t i = slotOf(key);; i = (i + 1) & m_mask) {
            Slot& s = m_slots[i];
            if (!s.used) return nullptr;
            if (s.key == key) return &s.value;
        }
    }
    [[nodiscard]] const V* find(std::uint64_t key) const { return const_cast<IdMap*>(this)->find(key); }

    /// The value of `key`, a default-constructed one inserted if there was none (which may move every value).
    V& operator[](std::uint64_t key) {
        if ((m_size + 1) * 2 > m_slots.size()) grow();
        for (std::size_t i = slotOf(key);; i = (i + 1) & m_mask) {
            Slot& s = m_slots[i];
            if (!s.used) {
                s.used = true;
                s.key = key;
                s.value = V{};
                ++m_size;
                return s.value;
            }
            if (s.key == key) return s.value;
        }
    }

    /// Removes `key` (and its value), if present.
    void erase(std::uint64_t key) {
        if (m_size == 0) return;
        std::size_t i = slotOf(key);
        for (;; i = (i + 1) & m_mask) {
            if (!m_slots[i].used) return;
            if (m_slots[i].key == key) break;
        }
        // Backward-shift deletion: the entries after it in its run move up into the hole when their own slot allows,
        // so every key stays reachable from its slot without tombstones.
        for (std::size_t j = i;;) {
            j = (j + 1) & m_mask;
            if (!m_slots[j].used) break;
            const std::size_t home = slotOf(m_slots[j].key);
            // j's entry may fill the hole at i unless its home lies cyclically in (i, j].
            if (i <= j ? (i < home && home <= j) : (i < home || home <= j)) continue;
            m_slots[i].key = m_slots[j].key;
            m_slots[i].value = std::move(m_slots[j].value);
            i = j;
        }
        m_slots[i].used = false;
        m_slots[i].value = V{};
        --m_size;
    }

    /// Removes every entry for which pred(key, value) holds.
    template <class Pred>
    void eraseIf(Pred pred) {
        std::vector<Slot> old = std::exchange(m_slots, std::vector<Slot>(m_slots.size()));
        m_size = 0;
        for (Slot& s : old)
            if (s.used && !pred(s.key, s.value)) place(s.key, std::move(s.value));
    }

    /// Calls f(key, value) for every entry.
    template <class F>
    void forEach(F f) {
        for (Slot& s : m_slots)
            if (s.used) f(s.key, s.value);
    }

    void clear() {
        m_slots.clear();
        m_mask = 0;
        m_size = 0;
    }
    [[nodiscard]] std::size_t size() const { return m_size; }
    [[nodiscard]] bool empty() const { return m_size == 0; }

private:
    struct Slot {
        std::uint64_t key = 0;
        bool used = false;
        V value{};
    };

    [[nodiscard]] std::size_t slotOf(std::uint64_t key) const {
        // Fibonacci hashing: the multiply spreads every bit of the key into the high bits, which pick the slot.
        const std::uint64_t h = (key ^ (key >> 32)) * 0x9E3779B97F4A7C15ull;
        return static_cast<std::size_t>(h >> 32) & m_mask;
    }

    void place(std::uint64_t key, V&& value) {
        std::size_t i = slotOf(key);
        while (m_slots[i].used)
            i = (i + 1) & m_mask;
        m_slots[i].used = true;
        m_slots[i].key = key;
        m_slots[i].value = std::move(value);
        ++m_size;
    }

    void grow() {
        const std::size_t capacity = m_slots.empty() ? 16 : m_slots.size() * 2;
        std::vector<Slot> old = std::exchange(m_slots, std::vector<Slot>(capacity));
        m_mask = capacity - 1;
        m_size = 0;
        for (Slot& s : old)
            if (s.used) place(s.key, std::move(s.value));
    }

    std::vector<Slot> m_slots;
    std::size_t m_mask = 0;
    std::size_t m_size = 0;
};

} // namespace imlui
