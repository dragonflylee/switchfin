#pragma once

#include "client.hpp"

namespace remote {

class AVIO : public Client {
public:
    AVIO(const std::string& path);
    std::vector<DirEntry> list(const std::string &path) override;
};

}  // namespace remote