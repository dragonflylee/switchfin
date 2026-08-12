#pragma once

#include "client.hpp"

namespace remote {

class Apache : public HttpClient {
public:
    Apache(const AppRemote &conf);
    std::vector<DirEntry> list(const std::string &path) override;
};

}  // namespace remote