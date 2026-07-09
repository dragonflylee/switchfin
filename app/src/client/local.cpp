#include "client/local.hpp"
#include "utils/misc.hpp"
#include <algorithm>
#include <cctype>

namespace remote {

static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

// Immediate sub-directory of `parent` named `name`, matched case-insensitively.
// The Blu-ray spec mandates uppercase BDMV/STREAM, so the exact path is tried
// first (a single stat); the scan only kicks in for backups sitting on a
// case-sensitive filesystem. Returns an empty path when there is no match.
static fs::path childDir(const fs::path& parent, const std::string& name) {
    fs::path exact = parent / name;
    if (fs::is_directory(exact)) return exact;
    for (const auto& fp : fs::directory_iterator(parent)) {
        if (fs::is_directory(fp) && iequals(fp.path().filename().string(), name)) return fp.path();
    }
    return {};
}

// If `dir` is the root of a Blu-ray backup (BDMV/STREAM holding at least one
// .m2ts/.mts stream) returns the absolute STREAM path, otherwise "". Any
// filesystem error simply means "not a Blu-ray" — detection must never break
// a plain directory listing.
static std::string findBlurayStream(const std::string& dir) {
    try {
        fs::path bdmv = childDir(dir, "BDMV");
        if (bdmv.empty()) return "";
        fs::path stream = childDir(bdmv, "STREAM");
        if (stream.empty()) return "";
        for (const auto& fp : fs::directory_iterator(stream)) {
            std::string ext = fp.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".m2ts" || ext == ".mts") return stream.string();
        }
    } catch (const std::exception&) {
    }
    return "";
}

std::vector<DirEntry> Local::list(const std::string& path) {
    std::vector<DirEntry> s = {{EntryType::UP}};
    std::string p = path.rfind("file://") == 0 ? path.substr(7) : path;

    // Blu-ray backup: surface a single "Play Blu-ray" action so the movie can
    // be started from the disc root without diving into BDMV/STREAM (issue
    // #18). Full bd:// navigation would need libbluray, which none of the mpv
    // builds bundle, so the .m2ts streams are played directly instead.
    std::string stream = findBlurayStream(p);
    if (!stream.empty()) {
        DirEntry bd;
        bd.type = EntryType::BLURAY;
        bd.path = stream;
        s.push_back(bd);
    }

    auto it = fs::directory_iterator(p);
    for (const auto& fp : it) {
        DirEntry item;
        auto& p = fp.path();
        item.name = p.filename().string();
        item.path = p.string();
        if (fs::is_directory(fp)) {
            item.type = EntryType::DIR;
        } else {
            item.type = EntryType::FILE;
            item.fileSize = fs::file_size(p);
        }
        s.push_back(item);
    }
    return s;
}

}  // namespace remote
