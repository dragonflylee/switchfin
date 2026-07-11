#pragma once

/*
    Lightweight file-browser entry model + pure sort logic (issue #23).

    Extracted from client.hpp so the sort can be unit-tested without pulling in
    Borealis/curl/config. Depends only on the STL.
*/

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace remote {

enum class EntryType {
    FILE,
    DIR,
    DEVICE,
    VIDEO,
    AUDIO,
    IMAGE,
    PLAYLIST,
    SUBTITLE,
    TEXT,
    UP,
    // virtual "Play Blu-ray" entry synthesised for a folder that holds a
    // BDMV/STREAM backup (issue #18); path points at the STREAM directory
    BLURAY,
};

struct DirEntry {
    EntryType type = EntryType::FILE;
    std::string name;
    std::string path;
    uint64_t fileSize = 0;
    /// Sortable modification time. Its absolute base is implementation-defined
    /// (filesystem clock epoch); only relative order within one listing is
    /// used, so it needs no calendar conversion. 0 = unknown (sorts oldest).
    uint64_t mtime = 0;
    /// Shortcut shown on the Files root screen (issue #24) — never persisted
    /// on the entry itself, derived from the pinned-paths config at build time.
    bool pinned = false;
    /// Human-readable modification date (WebDAV getlastmodified); kept for
    /// display, distinct from `mtime` which is only for sorting.
    std::tm modified{};

    const std::string& url() const { return this->path; }
};

/// User-facing sort criteria for the file browser (issue #23).
enum class SortKey { NAME, DATE, SIZE };

/// Case-insensitive lexicographic compare: <0, 0, >0.
inline int ciCompare(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)std::tolower((unsigned char)a[i]);
        unsigned char cb = (unsigned char)std::tolower((unsigned char)b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

/// Grouping rank kept ABOVE the sort key: the ".." entry always stays first,
/// then the synthesised "Play Blu-ray" action (issue #18) which is not a real
/// file and must stay at the top of a BDMV folder, then folders (DIR/DEVICE),
/// then files — a folder never sinks below a file whatever the criteria/order
/// (standard file-manager behaviour).
inline int sortGroup(const DirEntry& e) {
    if (e.type == EntryType::UP) return 0;
    if (e.type == EntryType::BLURAY) return 1;
    if (e.type == EntryType::DIR || e.type == EntryType::DEVICE) return 2;
    return 3;
}

/// Sort a directory listing in place: ".." first, folders before files, then
/// by `key` within each group (name as tie-breaker), `desc` reversing only the
/// intra-group order (issue #23). Stable so equal entries keep their order.
inline void sortEntries(std::vector<DirEntry>& v, SortKey key, bool desc) {
    std::stable_sort(v.begin(), v.end(), [key, desc](const DirEntry& a, const DirEntry& b) {
        int ga = sortGroup(a), gb = sortGroup(b);
        if (ga != gb) return ga < gb;  // group order is never reversed
        if (ga == 0) return false;     // both ".." : preserve
        int c;
        switch (key) {
        case SortKey::SIZE:
            c = a.fileSize != b.fileSize ? (a.fileSize < b.fileSize ? -1 : 1) : ciCompare(a.name, b.name);
            break;
        case SortKey::DATE:
            c = a.mtime != b.mtime ? (a.mtime < b.mtime ? -1 : 1) : ciCompare(a.name, b.name);
            break;
        default:
            c = ciCompare(a.name, b.name);
        }
        return desc ? c > 0 : c < 0;
    });
}

}  // namespace remote
