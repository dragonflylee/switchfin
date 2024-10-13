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
    SUBTITLE,
    TEXT,
    UP,
};

struct DirEntry {
    std::string name;
    std::string path;
    std::string url;
    uint64_t fileSize;
    EntryType type;
    std::tm modified;
};

class Client {
public:
    virtual ~Client() = default;
    virtual std::vector<DirEntry> list(const std::string& path) = 0;
    virtual void auth(const std::string& user, const std::string& passwd) {}
    const std::string& rootPath() { return this->root; }
    const std::string& extraOption() { return this->extra; }

protected:
    std::string root;
    std::string extra;
};

std::shared_ptr<Client> create(const AppRemote& c);

}  // namespace remote