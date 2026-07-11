#pragma once

#include <utils/config.hpp>
#include <api/http.hpp>
#include <client/dir_entry.hpp>

namespace remote {

class remote_error : public std::exception {
public:
    explicit remote_error(const std::string& arg) : m(arg) {}
    const char* what() const noexcept override { return m.c_str(); }
private:
    std::string m;
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