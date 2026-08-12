#pragma once

#include <utils/config.hpp>
#include <api/http.hpp>

namespace remote {

class remote_error : public std::exception {
public:
    explicit remote_error(const std::string& arg) : m(arg) {}
    const char* what() const noexcept override { return m.c_str(); }
private:
    std::string m;
};

enum class EntryType {
    FILE,
    DIR,
    DEVICE,
    VIDEO,
    AUDIO,
    IMAGE,
    BOOK,
    PLAYLIST,
    SUBTITLE,
    TEXT,
    UP,
};

struct DirEntry {
    EntryType type = EntryType::FILE;
    std::string name;
    std::string path;
    uint64_t fileSize = 0;
    std::tm modified{};

    const std::string& url() const { return this->path; }
};

class Client {
public:
    virtual ~Client() = default;
    virtual std::vector<DirEntry> list(const std::string& path) = 0;

    // HTTP 后端（Apache/Webdav）用于播放（mpv extra）与图片加载（请求头）的附加信息；
    // 非 HTTP 后端（Local/AVIO）返回空默认值。
    virtual const std::string& extraOption() const {
        static const std::string empty;
        return empty;
    }
    virtual const HTTP::Header& getHeaders() const {
        static const HTTP::Header empty;
        return empty;
    }
};

// HTTP 协议后端（Apache 目录索引 / WebDAV）的公共基类：
// 持有 libcurl 会话、服务端 host，以及播放（mpv extra）与图片加载
// （请求头）所需的附加信息。Local / AVIO 等非 HTTP 后端直接继承 Client。
class HttpClient : public Client {
public:
    const std::string& extraOption() const override { return this->extra; }
    const HTTP::Header& getHeaders() const override { return this->headers; }

protected:
    // 根据 AppRemote 配置 Basic Auth / User-Agent，并生成 mpv 的 extra 选项
    void setup(const AppRemote& conf);

    HTTP c;
    std::string host;
    HTTP::Header headers;
    std::string extra;
};

std::shared_ptr<Client> create(const AppRemote& c);

}  // namespace remote