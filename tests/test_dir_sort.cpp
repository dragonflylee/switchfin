// Standalone logic test — file-browser sort (issue #23).
//
//   c++ -std=gnu++17 -arch x86_64 -Iapp/include tests/test_dir_sort.cpp -o /tmp/t && /tmp/t
//
// Exercises the PURE sort logic (client/dir_entry.hpp): ".." first, folders
// before files, then by the chosen key/order within each group.

#include <cstdio>
#include <client/dir_entry.hpp>

using remote::DirEntry;
using remote::EntryType;
using remote::SortKey;

static int failures = 0;
#define CHECK(cond)                                          \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                      \
        }                                                    \
    } while (0)

static DirEntry mk(EntryType t, const std::string& name, uint64_t size = 0, uint64_t mtime = 0) {
    DirEntry e;
    e.type = t;
    e.name = name;
    e.path = name;
    e.fileSize = size;
    e.mtime = mtime;
    return e;
}

int main() {
    // ".." first, folders before files, alphabetical case-insensitive.
    // The ticket's own case: "media" renamed to "A_Media" must climb to top.
    {
        std::vector<DirEntry> v = {
            mk(EntryType::FILE, "zebra.mkv"),
            mk(EntryType::DIR, "media"),
            mk(EntryType::FILE, "alpha.mp4"),
            mk(EntryType::DIR, "A_Media"),
            mk(EntryType::UP, ".."),
        };
        remote::sortEntries(v, SortKey::NAME, false);
        CHECK(v[0].type == EntryType::UP);       // ".." pinned on top
        CHECK(v[1].name == "A_Media");           // folders first, A_ before media
        CHECK(v[2].name == "media");
        CHECK(v[3].name == "alpha.mp4");         // then files, alphabetical
        CHECK(v[4].name == "zebra.mkv");
    }

    // descending by name: intra-group order reverses, but ".." stays first and
    // folders still precede files
    {
        std::vector<DirEntry> v = {
            mk(EntryType::UP, ".."),
            mk(EntryType::DIR, "apple"),
            mk(EntryType::DIR, "banana"),
            mk(EntryType::FILE, "a.mp4"),
            mk(EntryType::FILE, "b.mp4"),
        };
        remote::sortEntries(v, SortKey::NAME, true);
        CHECK(v[0].type == EntryType::UP);
        CHECK(v[1].name == "banana");   // folders group, reversed
        CHECK(v[2].name == "apple");
        CHECK(v[3].name == "b.mp4");    // files group, reversed
        CHECK(v[4].name == "a.mp4");
    }

    // by size (ascending) among files; folders (size 0) stay grouped first
    {
        std::vector<DirEntry> v = {
            mk(EntryType::FILE, "big.mp4", 900),
            mk(EntryType::FILE, "small.mp4", 100),
            mk(EntryType::DIR, "folder"),
            mk(EntryType::FILE, "mid.mp4", 500),
        };
        remote::sortEntries(v, SortKey::SIZE, false);
        CHECK(v[0].name == "folder");   // folder first regardless of size
        CHECK(v[1].name == "small.mp4");
        CHECK(v[2].name == "mid.mp4");
        CHECK(v[3].name == "big.mp4");
    }

    // by date (descending = newest first) among files
    {
        std::vector<DirEntry> v = {
            mk(EntryType::FILE, "old.mp4", 0, 100),
            mk(EntryType::FILE, "new.mp4", 0, 300),
            mk(EntryType::FILE, "mid.mp4", 0, 200),
        };
        remote::sortEntries(v, SortKey::DATE, true);
        CHECK(v[0].name == "new.mp4");
        CHECK(v[1].name == "mid.mp4");
        CHECK(v[2].name == "old.mp4");
    }

    // devices are treated as folders (grouped before files)
    {
        std::vector<DirEntry> v = {
            mk(EntryType::FILE, "movie.mkv"),
            mk(EntryType::DEVICE, "USB Drive"),
        };
        remote::sortEntries(v, SortKey::NAME, false);
        CHECK(v[0].type == EntryType::DEVICE);
        CHECK(v[1].name == "movie.mkv");
    }

    if (failures == 0) {
        printf("test_dir_sort: OK\n");
        return 0;
    }
    printf("test_dir_sort: %d FAILURE(S)\n", failures);
    return 1;
}
