// Standalone logic test — offline image cache key (T5, SPEC AC17).
//
//   c++ -std=gnu++17 -arch x86_64 \
//       -Iapp/include -Ilibrary/borealis/library/include/borealis/extern \
//       tests/test_image_cache.cpp -o /tmp/t && /tmp/t
//
// The cache key MUST be stable across runs (it names files persisted to disk),
// so it cannot use std::hash. Exercises the pure key() only.

#include <cstdio>
#include <utils/image_cache.hpp>

static int failures = 0;
#define CHECK(cond)                                          \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                      \
        }                                                    \
    } while (0)

int main() {
    std::string a = "/library/metadata/1/thumb/1700000000";
    std::string b = "/library/metadata/2/thumb/1700000000";

    CHECK(ImageCache::key(a) == ImageCache::key(a));  // deterministic
    CHECK(ImageCache::key(a) != ImageCache::key(b));  // distinct inputs
    CHECK(ImageCache::key(a).size() == 16);           // 64-bit FNV-1a as hex
    CHECK(!ImageCache::key("https://image.tmdb.org/x.jpg").empty());
    // known FNV-1a-64 vector: hash of "" is cbf29ce484222325
    CHECK(ImageCache::key("") == "cbf29ce484222325");

    if (failures == 0) {
        printf("test_image_cache: OK\n");
        return 0;
    }
    printf("test_image_cache: %d FAILURE(S)\n", failures);
    return 1;
}
