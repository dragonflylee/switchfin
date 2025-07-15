#include "client/avio.hpp"

namespace remote {

#if __has_include(<libavformat/avio.h>)

extern "C" {
#include <libavformat/avio.h>
}

AVIO::AVIO(const std::string& path) {
    if (!avio_find_protocol_name(path.c_str())) {
        throw remote_error("unsupport protocol");
    }
}

std::vector<DirEntry> AVIO::list(const std::string& path) {
    AVIODirContext* ctx = nullptr;
    AVIODirEntry* next = nullptr;
    int ret = avio_open_dir(&ctx, path.c_str(), nullptr);
    if (ret < 0) throw remote_error(fmt::format("avio_open_dir {:#x}", ret));

    std::vector<DirEntry> s = {{EntryType::UP}};
    while (avio_read_dir(ctx, &next) >= 0) {
        if (!next) break;

        DirEntry item;
        item.name = next->name;
        item.path = path + "/" + item.name;
        if (next->type == AVIO_ENTRY_DIRECTORY) {
            item.type = EntryType::DIR;
        } else {
            item.type = EntryType::FILE;
            item.fileSize = next->size;
        }
        s.push_back(item);
        avio_free_directory_entry(&next);
    }

    avio_close_dir(&ctx);
    return s;
}

#else

AVIO::AVIO(const std::string& path) { throw remote_error("unsupport protocol"); }

std::vector<DirEntry> AVIO::list(const std::string& path) { return {}; }

#endif

}  // namespace remote
