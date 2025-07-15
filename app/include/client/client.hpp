#pragma once

#include <utils/config.hpp>
#include <api/http.hpp>

namespace remote {

class remote_error : public std::exception {
public:
    explicit remote_error(const std::string& arg) : m(arg) {}
    const char* what() const noexcept override { return m.c_str(); }
private:
    std::string m;
};

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
};

struct DirEntry {
    EntryType type;
    std::string name;
    std::string path;
    uint64_t fileSize;
    std::tm modified;

    const std::string& url() const { return this->path; }
};

class Client {
public:
    virtual ~Client() = default;
    virtual std::vector<DirEntry> list(const std::string& path) = 0;
    virtual void auth(const std::string& user, const std::string& passwd) {}
    const std::string& extraOption() { return this->extra; }

protected:
    std::string extra;

    void init(const AppRemote& conf, HTTP& cilent);
};

std::shared_ptr<Client> create(const AppRemote& c);

}  // namespace remote