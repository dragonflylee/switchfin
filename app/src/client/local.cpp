#include "client/local.hpp"

#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "Failed to include <filesystem> header!"
#endif

namespace remote {

Local::Local(const std::string& path) { root = path; }

std::vector<DirEntry> Local::list(const std::string& path) {
    std::vector<DirEntry> s;
    auto it = fs::directory_iterator(path);
    for (const auto& fp : it) {
        DirEntry item;
        auto& p = fp.path();
        item.name = p.filename().string();
        item.path = p.string();
        if (fp.is_directory()) {
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