#include "client/local.hpp"
#include "client/webdav.hpp"
#include "client/apache.hpp"
#include "client/avio.hpp"
#include <utils/misc.hpp>
#include <algorithm>
#include <sstream>

namespace remote {

std::shared_ptr<Client> create(const AppRemote& c) {
    auto pos = c.url.find("://");
    if (pos == std::string::npos) {
        throw remote_error(fmt::format("invalid url: {}", c.url));
    }
    // 统一转小写后再匹配 scheme，避免大小写不一致导致无法路由
    std::string scheme = c.url.substr(0, pos);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (scheme == "webdav" || scheme == "webdavs") {
        // webdav(s):// 映射为 http(s)://，交给 WebDAV 客户端处理
        std::string url = fmt::format("{}://{}", (scheme == "webdavs" ? "https" : "http"), c.url.substr(pos + 3));
        return std::make_shared<Webdav>(url, c);
    }
    if (scheme == "file") {
        return std::make_shared<Local>();
    }
    if (scheme == "http" || scheme == "https") {
        return std::make_shared<Apache>(c);
    }
    // 其余协议（ftp/sftp 等）交给 ffmpeg AVIO
    brls::Logger::debug("remote::create unknown scheme {}, fallback to AVIO", scheme);
    return std::make_shared<AVIO>(c.url);
}

void HttpClient::setup(const AppRemote& conf) {
    std::stringstream ssextra;
    ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);
    if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";

    if (conf.user.size() > 0 || conf.passwd.size() > 0) {
        std::string auth = base64::encode(fmt::format("{}:{}", conf.user, conf.passwd));
        ssextra << fmt::format(",http-header-fields=\"Authorization: Basic {}\"", auth);
        this->c.set_basic_auth(conf.user, conf.passwd);
        this->headers.emplace_back("Authorization: Basic " + auth);
    }
    if (conf.user_agent.size() > 0) {
        ssextra << fmt::format(",user_agent=\"{}\"", conf.user_agent);
        this->c.set_user_agent(conf.user_agent);
        this->headers.emplace_back("User-Agent: " + conf.user_agent);
    }
    this->extra = ssextra.str();
}

}  // namespace remote