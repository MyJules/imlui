// Tests for the memory of a state made by lua::newstate(): its pool gives the chunks a peak left empty back to the OS
// (lua::trimMemory), keeps its free lists right across trims, and doesn't walk a small or fragmented pool for nothing.
// Under AddressSanitizer newstate() keeps Lua's plain allocator: trimMemory does nothing, and the tests that need the
// pool are skipped after checking that.
#include <gtest/gtest.h>

import std;

import lua;

namespace {

constexpr std::size_t mib = std::size_t{1} << 20;

class LuaMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_state = lua::newstate();
        ASSERT_NE(m_state, nullptr);
        lua::openlibs(m_state);
        m_pooled = lua::memoryStats(m_state).reserved > 0;
    }

    void TearDown() override { lua::close(m_state); }

    /// Runs `code` and leaves its first result on the stack.
    void run(std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok) << lua::tostring(m_state, -1);
        ASSERT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok) << lua::tostring(m_state, -1);
    }

    /// Blocks taken from the state's allocator directly, filled with a byte of their own to tell when one was handed
    /// out twice or kept on a free list after its chunk went.
    struct Block {
        std::byte* p = nullptr;
        std::size_t size = 0;
        std::byte fill{};
    };

    Block allocate(std::size_t size, std::byte fill) {
        Block b{static_cast<std::byte*>(m_alloc(m_ud, nullptr, 0, size)), size, fill};
        EXPECT_NE(b.p, nullptr);
        std::fill_n(b.p, size, fill);
        return b;
    }
    void release(const Block& b) { m_alloc(m_ud, b.p, b.size, 0); }
    static bool intact(const Block& b) {
        return std::all_of(b.p, b.p + b.size, [&](std::byte x) { return x == b.fill; });
    }

    void useAllocator() { m_alloc = lua::getallocf(m_state, &m_ud); }

    lua::State* m_state = nullptr;
    bool m_pooled = false;
    lua::Alloc m_alloc = nullptr;
    void* m_ud = nullptr;
};

} // namespace

// A big tree of tables and strings, dropped and collected: the trim gives back most of the peak, and the state goes on
// working (allocates again, runs code, closes).
TEST_F(LuaMemoryTest, TrimGivesBackWhatABigTreeLeft) {
    run(R"(
        big = {}
        for i = 1, 100000 do big[i] = { name = "item " .. i, value = i, tags = { "tag" .. i % 7, i } } end
        return #big)");
    EXPECT_EQ(lua::tointeger(m_state, -1), 100000);
    lua::pop(m_state);
    const lua::MemoryStats peak = lua::memoryStats(m_state);
    run("big = nil; collectgarbage(); collectgarbage(); return true");
    lua::pop(m_state);

    const std::size_t released = lua::trimMemory(m_state);
    const lua::MemoryStats after = lua::memoryStats(m_state);
    if (!m_pooled) {
        EXPECT_EQ(released, 0u);
        EXPECT_EQ(after.reserved, 0u);
    } else {
        EXPECT_GE(peak.reserved, 16 * mib);
        EXPECT_EQ(after.trims, 1u);
        EXPECT_EQ(released, peak.reserved - after.reserved);
        EXPECT_LT(after.reserved, peak.reserved / 5) << "peak " << peak.reserved << ", after " << after.reserved;
        EXPECT_LE(after.free, after.reserved);
        // Right after, nothing more to do: the free bytes didn't grow, and a second hasn't passed.
        EXPECT_EQ(lua::trimMemory(m_state), 0u);
        EXPECT_EQ(lua::memoryStats(m_state).trims, 1u);
    }

    run(R"(
        local t, sum = {}, 0
        for i = 1, 20000 do t[i] = { s = ("x"):rep(i % 600), i } end
        for i = 1, #t do sum = sum + t[i][1] + #t[i].s end
        return sum)");
    std::int64_t expected = 0;
    for (std::int64_t i = 1; i <= 20000; ++i)
        expected += i + i % 600;
    EXPECT_EQ(lua::tointeger(m_state, -1), expected);
}

// Random allocations, frees and resizes across the small/large boundary, straight through the state's allocator, with
// big drops and forced trims in between: every live block keeps its contents (none is handed out twice, none is left
// on a free list of a chunk given back), and at the end the pool is back to about what Lua itself holds.
TEST_F(LuaMemoryTest, FreeListsStayConsistentAcrossTrims) {
    useAllocator();
    const lua::MemoryStats before = lua::memoryStats(m_state);
    std::mt19937 rng{20261127};
    const auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    const auto randomSize = [&]() -> std::size_t {
        switch (pick(4)) {
        case 0: return 1 + pick(64);
        case 1: return 1 + pick(512);
        case 2: return 480 + pick(64); // both sides of smallMax (512)
        default: return 1 + pick(1500);
        }
    };
    std::uint8_t nextFill = 0;
    const auto fill = [&] { return std::byte{++nextFill}; };

    std::vector<Block> blocks;
    std::size_t released = 0;
    for (int round = 0; round < 12; ++round) {
        for (int op = 0; op < 20000; ++op) {
            const std::size_t what = pick(10);
            if (what < 5 || blocks.empty()) {
                blocks.push_back(allocate(randomSize(), fill()));
                continue;
            }
            const std::size_t i = pick(blocks.size());
            Block& b = blocks[i];
            ASSERT_TRUE(intact(b)) << "round " << round << ", op " << op;
            if (what < 8) {
                release(b);
                b = blocks.back();
                blocks.pop_back();
                continue;
            }
            const std::size_t size = randomSize();
            auto* moved = static_cast<std::byte*>(m_alloc(m_ud, b.p, b.size, size));
            ASSERT_NE(moved, nullptr);
            ASSERT_TRUE(intact({moved, std::min(b.size, size), b.fill})) << "resized " << b.size << " -> " << size;
            b = {moved, size, fill()};
            std::fill_n(b.p, b.size, b.fill);
        }
        // A big drop, then a trim.
        std::ranges::shuffle(blocks, rng);
        const std::size_t keep = blocks.size() / 10;
        for (std::size_t i = keep; i < blocks.size(); ++i) {
            ASSERT_TRUE(intact(blocks[i]));
            release(blocks[i]);
        }
        blocks.resize(keep);
        released += lua::trimMemory(m_state, {.force = true});
        const lua::MemoryStats stats = lua::memoryStats(m_state);
        EXPECT_LE(stats.free, stats.reserved);
        for (const Block& b : blocks)
            ASSERT_TRUE(intact(b)) << "after the trim of round " << round;
    }
    for (const Block& b : blocks) {
        ASSERT_TRUE(intact(b));
        release(b);
    }
    released += lua::trimMemory(m_state, {.force = true});
    const lua::MemoryStats after = lua::memoryStats(m_state);
    if (m_pooled) {
        EXPECT_GT(released, 0u);
        EXPECT_EQ(after.trims, 13u);
        // What Lua holds, the chunk being carved and the newest slab's chunks not carved yet (at most 1 MiB).
        EXPECT_LE(after.reserved, before.reserved + mib);
    } else {
        EXPECT_EQ(released, 0u);
    }

    run(R"(
        local parts = {}
        for i = 1, 3000 do parts[i] = ("%d:%s"):format(i, ("y"):rep(i % 700)) end
        local n = 0
        for _, s in ipairs(parts) do n = n + #s end
        return n)");
    std::int64_t expected = 0;
    for (int i = 1; i <= 3000; ++i)
        expected += static_cast<std::int64_t>(std::to_string(i).size()) + 1 + i % 700;
    EXPECT_EQ(lua::tointeger(m_state, -1), expected);
}

// A small pool isn't walked at all, and a fragmented one (lots free, but a live block in every chunk) is walked once,
// then not again until its free bytes grow by another `minFree`, and never twice within `minInterval`.
TEST_F(LuaMemoryTest, TrimLeavesASmallOrFragmentedPoolAlone) {
    EXPECT_EQ(lua::trimMemory(m_state), 0u);
    EXPECT_EQ(lua::memoryStats(m_state).trims, 0u);
    if (!m_pooled) {
        EXPECT_EQ(lua::trimMemory(m_state, {.force = true}), 0u);
        EXPECT_EQ(lua::memoryStats(m_state).trims, 0u);
        GTEST_SKIP() << "no pool (AddressSanitizer): newstate() keeps Lua's plain allocator";
    }
    useAllocator();
    const lua::TrimOptions eager{.minFree = 8 * mib, .minInterval = std::chrono::steady_clock::duration::zero()};

    // 12 MiB of 64-byte blocks (about a thousand to a chunk), every 64th kept.
    std::vector<Block> blocks;
    for (std::size_t i = 0; i < 12 * mib / 64; ++i)
        blocks.push_back(allocate(64, std::byte{static_cast<std::uint8_t>(i)}));
    const auto dropAllBut = [&](std::size_t every) {
        std::vector<Block> kept;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (i % every == 0)
                kept.push_back(blocks[i]);
            else
                release(blocks[i]);
        }
        blocks = std::move(kept);
    };
    dropAllBut(64);
    ASSERT_GE(lua::memoryStats(m_state).free, 8 * mib);
    EXPECT_EQ(lua::trimMemory(m_state, eager), 0u); // walked: every chunk keeps blocks
    EXPECT_EQ(lua::memoryStats(m_state).trims, 1u);
    EXPECT_EQ(lua::trimMemory(m_state, eager), 0u); // not walked again: the free bytes didn't grow
    EXPECT_EQ(lua::memoryStats(m_state).trims, 1u);

    // The free blocks taken again, then dropped: the free bytes grew by more than minFree from their low.
    std::vector<Block> more;
    for (std::size_t i = 0; i < 10 * mib / 64; ++i)
        more.push_back(allocate(64, std::byte{0x5a}));
    EXPECT_EQ(lua::trimMemory(m_state, eager), 0u); // too little free now; the low is noted
    EXPECT_EQ(lua::memoryStats(m_state).trims, 1u);
    for (const Block& b : more)
        release(b);
    EXPECT_EQ(lua::trimMemory(m_state, eager), 0u);
    EXPECT_EQ(lua::memoryStats(m_state).trims, 2u);

    // The same with a minimum interval: grown again, but the last walk was just now.
    more.clear();
    for (std::size_t i = 0; i < 10 * mib / 64; ++i)
        more.push_back(allocate(64, std::byte{0x5b}));
    lua::TrimOptions patient = eager;
    patient.minInterval = std::chrono::hours{1};
    EXPECT_EQ(lua::trimMemory(m_state, patient), 0u);
    for (const Block& b : more)
        release(b);
    EXPECT_EQ(lua::trimMemory(m_state, patient), 0u);
    EXPECT_EQ(lua::memoryStats(m_state).trims, 2u);

    for (const Block& b : blocks) {
        ASSERT_TRUE(intact(b));
        release(b);
    }
    EXPECT_GE(lua::trimMemory(m_state, {.force = true}), 8 * mib); // nothing left in those chunks: they go
}
