#pragma once

#include "client.hpp"
#include <api/http.hpp>

namespace remote {

class Webdav : public Client {
public:
    Webdav(const std::string& url, const AppRemote &conf);
    std::vector<DirEntry> list(const std::string &path) override;

private:
    HTTP c;
    std::string host;
};

}  // namespace remote