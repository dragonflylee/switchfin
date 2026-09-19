#include "client/local.hpp"
#include "utils/misc.hpp"
#ifdef PS5_NATIVE_GPU
#include <dirent.h>
#include <sys/stat.h>
#include "utils/download.hpp"
#endif

namespace remote {

std::vector<DirEntry> Local::list(const std::string& path) {
    std::vector<DirEntry> s = {{EntryType::UP}};
    std::string p = path.rfind("file://") == 0 ? path.substr(7) : path;
#ifdef PS5_NATIVE_GPU
    // std::filesystem::directory_iterator returned nothing for directories that
    // opendir/readdir enumerate correctly on this image, so the browser listed
    // /download0 and the downloads folder as empty. This is the same raw walk
    // the removal and index-scan paths already rely on (opendir/readdir/lstat),
    // which bind here where the at-family the iterator can reach does not.
    std::string base = p;
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    DIR* dir = ::opendir(base.c_str());
    if (!dir) {
        // The sandboxed Local Storage browser lists empty; capture why opendir
        // fails (errno) rather than silently returning an empty listing.
        brls::Logger::error("Local::list opendir failed ({}) errno={}", base, errno);
        // opendir returns EPERM in the sandbox: the pfs denies directory
        // enumeration regardless of mode bits, so the Downloads node lists empty
        // when jailed. The download index is still readable via raw IO, so
        // when the browser targets the download directory, populate it from the
        // index -- each completed item as its playable file.
        {
            auto trimSlash = [](std::string v) {
                while (v.size() > 1 && v.back() == '/') v.pop_back();
                return v;
            };
            DownloadManager& dm = DownloadManager::instance();
            const std::string dlDir  = trimSlash(dm.downloadDir());
            const std::string idxDir = trimSlash(dm.indexDir());
            if (base == dlDir || base == idxDir) {
                for (const DownloadItem& it : dm.getItems()) {
                    if (it.status != DownloadStatus::Completed) continue;
                    const std::string lp = dm.getLocalPath(it.itemId);
                    if (lp.empty()) continue;
                    DirEntry item;
                    // A downloaded item is playable media; classify it directly
                    // as VIDEO (the browser only reclassifies FILE entries by
                    // extension, and the display name is a title with none) so it
                    // keeps its title and opens in the player when selected.
                    item.type     = EntryType::VIDEO;
                    item.name     = it.name.empty() ? it.itemId : it.name;
                    item.path     = lp;
                    item.fileSize = it.totalBytes > 0 ? static_cast<uint64_t>(it.totalBytes) : 0;
                    s.push_back(item);
                }
            }
        }
        return s;
    }
    while (const dirent* entry = ::readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        // Hide dotfile bookkeeping (.switchfin-removing-*, probe temporaries).
        if (name[0] == '.') continue;
#else
    auto it = fs::directory_iterator(p);
    for (const auto& fp : it) {
#endif
        DirEntry item;
#ifdef PS5_NATIVE_GPU
        item.name = name;
        item.path = base + "/" + name;
        struct stat st {};
        if (::lstat(item.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
#else
        auto& p = fp.path();
        item.name = p.filename().string();
        item.path = p.string();
        if (fs::is_directory(fp)) {
#endif
            item.type = EntryType::DIR;
        } else {
            item.type = EntryType::FILE;
#ifdef PS5_NATIVE_GPU
            item.fileSize = (st.st_size > 0) ? static_cast<uint64_t>(st.st_size) : 0;
#else
            item.fileSize = fs::file_size(p);
#endif
        }
        s.push_back(item);
    }
#ifdef PS5_NATIVE_GPU
    ::closedir(dir);
#endif
    return s;
}

#ifdef PS5_NATIVE_GPU
}  // namespace remote
#else
}  // namespace remote
#endif
