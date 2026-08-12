#pragma once

#include "client.hpp"

namespace remote {

class Webdav : public HttpClient {
public:
    Webdav(const std::string& url, const AppRemote &conf);
    std::vector<DirEntry> list(const std::string &path) override;
};

}  // namespace remote