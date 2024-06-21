#pragma once

#include "client.hpp"
#include <api/http.hpp>
#include <tinyxml2/tinyxml2.h>

namespace remote {

class Webdav : public Client {
public:
    Webdav(const std::string &url, const std::string &user, const std::string &passwd);
    std::vector<DirEntry> list(const std::string &path) override;

private:
    HTTP c;
    std::string host;

    std::string getNamespacePrefix(tinyxml2::XMLElement *root, const std::string &nsURI);
};

}  // namespace remote