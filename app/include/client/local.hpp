#pragma once

#include "client.hpp"

namespace remote {

class Local : public Client {
public:
    Local() = default;
    std::vector<DirEntry> list(const std::string &path) override;
};

}  // namespace remote