#include "mini_test.h"
#include "tinysearch/cache.h"

using tinysearch::LruCache;

TEST_CASE(cache_miss_then_hit) {
    LruCache<int> cache(10);
    EXPECT_FALSE(cache.get("a").has_value());
    cache.put("a", 42);
    auto v = cache.get("a");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST_CASE(cache_hit_miss_counters) {
    LruCache<int> cache(10);
    cache.put("a", 1);
    cache.get("a");   // hit
    cache.get("b");   // miss
    cache.get("a");   // hit
    EXPECT_EQ(cache.hits(), static_cast<uint64_t>(2));
    EXPECT_EQ(cache.misses(), static_cast<uint64_t>(1));
}

TEST_CASE(cache_evicts_least_recently_used) {
    LruCache<int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.get("a");       // "a" is now most-recently-used, "b" is LRU
    cache.put("c", 3);    // should evict "b"
    EXPECT_TRUE(cache.get("a").has_value());
    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_TRUE(cache.get("c").has_value());
}

TEST_CASE(cache_overwrite_updates_value) {
    LruCache<int> cache(10);
    cache.put("a", 1);
    cache.put("a", 2);
    EXPECT_EQ(*cache.get("a"), 2);
    EXPECT_EQ(cache.size(), static_cast<size_t>(1));
}

TEST_CASE(cache_invalidate_all_clears_everything) {
    LruCache<int> cache(10);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.invalidate_all();
    EXPECT_EQ(cache.size(), static_cast<size_t>(0));
    EXPECT_FALSE(cache.get("a").has_value());
}
