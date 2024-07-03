#if defined(__SWITCH__)

#include "client/avio.hpp"
extern "C" {
#include <libavutil/error.h>
#include <libavformat/avio.h>
}

namespace remote {

AVIO::AVIO(const std::string& path) { root = path; }

std::vector<DirEntry> AVIO::list(const std::string& path) {
    std::vector<DirEntry> s;
    AVIODirContext* ctx = nullptr;
    AVIODirEntry* next = nullptr;
    int ret = avio_open_dir(&ctx, path.c_str(), nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        throw std::runtime_error(errbuf);
    }

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

}  // namespace remote

#endif