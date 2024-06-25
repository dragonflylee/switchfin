#pragma once

#include <utils/config.hpp>

namespace remote {

enum class EntryType {
    FILE,
    DIR,
    VIDEO,
    AUDIO,
    IMAGE,
    PLAYLIST,
    TEXT,
};

struct DirEntry {
    std::string name;
    std::string path;
    uint64_t fileSize;
    EntryType type;
    time_t modified;
};

class Client {
public:
    virtual ~Client() = default;
    virtual std::vector<DirEntry> list(const std::string& path) = 0;
    const std::string& rootPath() { return this->root; }
    const std::string& extraOption() { return this->extra; }

protected:
    std::string root;
    std::string extra;
};

std::shared_ptr<Client> create(const AppRemote& c);

}  // namespace remote