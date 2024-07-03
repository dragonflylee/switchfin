#include "client/local.hpp"
#include "client/webdav.hpp"
#include "client/avio.hpp"

namespace remote {

std::shared_ptr<Client> create(const AppRemote& c) {
    auto pos = c.url.find_first_of("://");
    if (pos == std::string::npos) return nullptr;
    std::string scheme = c.url.substr(0, pos);
    if (scheme == "webdav") {
        std::string url = "http" + c.url.substr(pos);
        return std::make_shared<Webdav>(url, c.user, c.passwd);
    }
    if (scheme == "file") {
        return std::make_shared<Local>(c.url.substr(pos + 3));
    }
#if defined(__SWITCH__)
    return std::make_shared<AVIO>(c.url);
#else
    return nullptr;
#endif
}

}  // namespace remote