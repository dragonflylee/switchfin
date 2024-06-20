#pragma once

#include "client.hpp"

namespace remote {

class Local : public Client {
public:
    Local(const std::string& path);
    std::vector<DirEntry> list(const std::string &path) override;
};

}  // namespace remote