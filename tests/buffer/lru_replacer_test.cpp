#include <gtest/gtest.h>

#include "buffer/lru_replacer.hpp"

namespace querycraft::buffer {

TEST(LRUReplacerTest, EmptyReplacerReturnsFalse) {
    LRUReplacer replacer(5);
    EXPECT_EQ(replacer.Size(), 0);
    EXPECT_EQ(replacer.Capacity(), 5);

    frame_id_t victim_fid = -999;
    EXPECT_FALSE(replacer.Victim(&victim_fid));
    EXPECT_EQ(victim_fid, INVALID_FRAME_ID);

    // Victim with nullptr should safely return false without crashing
    EXPECT_FALSE(replacer.Victim(nullptr));
}

TEST(LRUReplacerTest, BasicUnpinAndVictimOrder) {
    LRUReplacer replacer(5);

    // Unpin frames 1, 2, 3
    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    EXPECT_EQ(replacer.Size(), 3);

    // Should evict in LRU order: 1, 2, 3
    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
    EXPECT_EQ(replacer.Size(), 2);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);
    EXPECT_EQ(replacer.Size(), 1);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 3);
    EXPECT_EQ(replacer.Size(), 0);

    // Replacer is now empty
    EXPECT_FALSE(replacer.Victim(&victim));
    EXPECT_EQ(victim, INVALID_FRAME_ID);
}

TEST(LRUReplacerTest, PinRemovesFrameFromEviction) {
    LRUReplacer replacer(5);

    replacer.Unpin(10);
    // 10 is >= 5, so out of range check
    LRUReplacer r(10);
    r.Unpin(1);
    r.Unpin(2);
    r.Unpin(3);
    r.Unpin(4);
    EXPECT_EQ(r.Size(), 4);

    // Pin frame 2 (pinned frames must never be evicted)
    r.Pin(2);
    EXPECT_EQ(r.Size(), 3);

    // Evict: should be 1, 3, 4 (2 is skipped because it's pinned)
    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 1);

    EXPECT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 3);

    EXPECT_TRUE(r.Victim(&victim));
    EXPECT_EQ(victim, 4);

    EXPECT_FALSE(r.Victim(&victim));
}

TEST(LRUReplacerTest, UnpinMovesToMRU) {
    LRUReplacer replacer(5);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);

    // Pin 1 (removes from replacer), then Unpin 1 (adds back to MRU position)
    replacer.Pin(1);
    replacer.Unpin(1);

    // Eviction order should now be: 2, 3, 1
    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 3);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
}

TEST(LRUReplacerTest, RecordAccessUpdatesRecency) {
    LRUReplacer replacer(5);

    replacer.Unpin(0);
    replacer.Unpin(1);
    replacer.Unpin(2);

    // Access frame 0, moving it to MRU
    replacer.RecordAccess(0);

    // Eviction order should now be: 1, 2, 0
    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 0);
}

TEST(LRUReplacerTest, DuplicateUnpinIgnored) {
    LRUReplacer replacer(5);

    replacer.Unpin(1);
    replacer.Unpin(1);
    replacer.Unpin(1);
    EXPECT_EQ(replacer.Size(), 1);

    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
    EXPECT_EQ(replacer.Size(), 0);

    EXPECT_FALSE(replacer.Victim(&victim));
}

TEST(LRUReplacerTest, OutOfRangeFrameIdIgnored) {
    LRUReplacer replacer(4);

    replacer.Unpin(-1);
    replacer.Unpin(4);
    replacer.Unpin(99);
    EXPECT_EQ(replacer.Size(), 0);

    replacer.Pin(-1);
    replacer.Pin(4);
    replacer.RecordAccess(-1);
    replacer.RecordAccess(4);
    EXPECT_EQ(replacer.Size(), 0);

    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_FALSE(replacer.Victim(&victim));
}

TEST(LRUReplacerTest, InterleavedOperations) {
    LRUReplacer replacer(6);

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    replacer.Unpin(4);
    replacer.Unpin(5);
    EXPECT_EQ(replacer.Size(), 5);

    // Pin 3 and 4
    replacer.Pin(3);
    replacer.Pin(4);
    EXPECT_EQ(replacer.Size(), 3);

    // Unpin 4 again (becomes MRU)
    replacer.Unpin(4);
    EXPECT_EQ(replacer.Size(), 4);

    // Record access to 1 (becomes MRU)
    replacer.RecordAccess(1);

    // Current order should be: 2, 5, 4, 1
    frame_id_t victim = INVALID_FRAME_ID;
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 5);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 4);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);

    EXPECT_EQ(replacer.Size(), 0);
    EXPECT_FALSE(replacer.Victim(&victim));
}

} // namespace querycraft::buffer
