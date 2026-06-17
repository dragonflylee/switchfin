/*
    pleNx — add/edit form for a remote file server.

    Produced URL schemes (routing: app/src/client/client.cpp):
      webdav://host:port/path/      -> Webdav client (PROPFIND, basic auth via
      webdavs://host:port/path/        the AppRemote user/passwd fields)
      ftp://user:pass@host:port/... -> AVIO client (ffmpeg): credentials
      sftp://user:pass@host:port/...   MUST be embedded in the URL (AVIO
                                       ignores the user/passwd fields)
      http(s)://host:port/path/     -> Apache client (directory index)

    No save without a successful connection test (listing of the start
    path); on failure: error dialog with "Retry".
*/

#include "tab/remote_add.hpp"
#include "client/client.hpp"
#include "utils/dialog.hpp"
#include <curl/curl.h>

using namespace brls::literals;

struct RemoteScheme {
    const char* scheme;
    long port;
    const char* label;
};

/// Offered types: the order fixes the selector index.
/// HTTP/HTTPS (Apache index) cover existing Switchfin configs.
static const std::vector<RemoteScheme> remoteSchemes = {
    {"webdav", 80, "WebDAV"},
    {"webdavs", 443, "WebDAV (HTTPS)"},
    {"ftp", 21, "FTP"},
    {"sftp", 22, "SFTP"},
    {"http", 80, "HTTP"},
    {"https", 443, "HTTPS"},
};

static int typeFromScheme(const std::string& scheme) {
    for (size_t i = 0; i < remoteSchemes.size(); i++) {
        if (scheme == remoteSchemes[i].scheme) return (int)i;
    }
    return 0;
}

/// Credentials embedded in the URL (AVIO/ffmpeg) vs separate fields
static bool inlineCredentials(int type) {
    std::string scheme = remoteSchemes[type].scheme;
    return scheme == "ftp" || scheme == "sftp";
}

static std::string urlEscape(const std::string& in) {
    if (in.empty()) return in;
    char* e = curl_escape(in.c_str(), (int)in.size());
    std::string out = e ? e : "";
    curl_free(e);
    return out;
}

static std::string urlUnescape(const std::string& in) {
    if (in.empty()) return in;
    char* u = curl_unescape(in.c_str(), (int)in.size());
    std::string out = u ? u : "";
    curl_free(u);
    return out;
}

static std::string trim(const std::string& in) {
    auto a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

/// Form fields extracted from an existing AppRemote (edit mode).
/// NB: no handling of literal IPv6 hosts ([::1]).
struct ParsedRemote {
    int type = 0;
    std::string name, host, user, passwd, path;
    long port = 0;
};

static ParsedRemote parseRemote(const AppRemote& r) {
    ParsedRemote p;
    p.name = r.name;
    p.user = r.user;
    p.passwd = r.passwd;

    std::string rest = r.url;
    auto pos = rest.find("://");
    if (pos != std::string::npos) {
        p.type = typeFromScheme(rest.substr(0, pos));
        rest = rest.substr(pos + 3);
    }

    auto slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    if (slash != std::string::npos) p.path = rest.substr(slash);

    // credentials embedded in the URL (ftp/sftp): take priority over the fields
    auto at = authority.rfind('@');
    if (at != std::string::npos) {
        std::string userinfo = authority.substr(0, at);
        authority = authority.substr(at + 1);
        auto colon = userinfo.find(':');
        p.user = urlUnescape(userinfo.substr(0, colon));
        if (colon != std::string::npos) p.passwd = urlUnescape(userinfo.substr(colon + 1));
    }

    auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        try {
            p.port = std::stol(authority.substr(colon + 1));
            authority = authority.substr(0, colon);
        } catch (const std::exception&) {
            // not a port: keep the authority as is
        }
    }
    p.host = authority;
    if (p.port <= 0) p.port = remoteSchemes[p.type].port;
    return p;
}

RemoteAdd::RemoteAdd(std::function<void()> onDone, int editIndex) : onDone(onDone), editIndex(editIndex) {
    this->inflateFromXMLRes("xml/view/remote_add.xml");
    brls::Logger::debug("RemoteAdd: create (edit {})", editIndex);

    auto& remotes = AppConfig::instance().getRemotes();
    if (editIndex >= (int)remotes.size()) this->editIndex = editIndex = -1;

    ParsedRemote p;
    if (editIndex >= 0)
        p = parseRemote(remotes[editIndex]);
    else
        p.port = remoteSchemes[0].port;
    this->typeIndex = p.type;

    this->header->setTitle(editIndex >= 0 ? "hints/edit"_i18n : "main/remote/add"_i18n);

    std::vector<std::string> labels;
    for (auto& s : remoteSchemes) labels.push_back(s.label);
    this->cellType->init("main/remote/type"_i18n, labels, p.type, [this](int sel) {
        if (sel < 0 || sel >= (int)remoteSchemes.size()) return;
        // only replace the port if it still holds the previous type's default
        if (this->cellPort->getValue() == remoteSchemes[this->typeIndex].port)
            this->cellPort->setValue(remoteSchemes[sel].port);
        this->typeIndex = sel;
    });

    this->cellName->init("main/remote/name"_i18n, p.name, [](std::string) {}, "", "", 64);
    this->cellHost->init("main/remote/host"_i18n, p.host, [](std::string) {}, "", "", 256);
    this->cellPort->init("main/remote/port"_i18n, p.port, [](long) {}, "", 5);
    this->cellUser->init("main/remote/username"_i18n, p.user, [](std::string) {}, "main/remote/optional"_i18n, "", 64);
    this->cellPasswd->init(
        "main/remote/password"_i18n, p.passwd, [](std::string) {}, "main/remote/optional"_i18n, "", 128);
    this->cellPath->init("main/remote/path"_i18n, p.path, [](std::string) {}, "/", "", 256);

    this->btnSave->registerClickAction([this](...) {
        this->submit();
        return true;
    });

    this->btnDelete->setVisibility(editIndex >= 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->btnDelete->registerClickAction([this](...) {
        Dialog::cancelable("main/setting/server/delete"_i18n, [this]() {
            auto& conf = AppConfig::instance();
            conf.removeRemote((size_t)this->editIndex);
            auto cb = this->onDone;
            brls::Application::popActivity();
            if (cb) cb();
        });
        return true;
    });

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));
}

RemoteAdd::~RemoteAdd() { brls::Logger::debug("RemoteAdd: delete"); }

void RemoteAdd::open(std::function<void()> onDone, int editIndex) {
    brls::Application::pushActivity(new brls::Activity(new RemoteAdd(onDone, editIndex)));
}

AppRemote RemoteAdd::build() {
    int type = this->typeIndex;
    std::string name = trim(this->cellName->getValue());
    std::string host = trim(this->cellHost->getValue());

    // the user may have pasted a full URL in "Host": extract host, port
    // and path from it (the type stays the selector's)
    auto pos = host.find("://");
    if (pos != std::string::npos) host = host.substr(pos + 3);
    std::string pathFromHost;
    auto slash = host.find('/');
    if (slash != std::string::npos) {
        pathFromHost = host.substr(slash);
        host = host.substr(0, slash);
    }
    long port = this->cellPort->getValue();
    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        try {
            port = std::stol(host.substr(colon + 1));
            host = host.substr(0, colon);
        } catch (const std::exception&) {
            // not a port pasted in the host
        }
    }
    if (port <= 0 || port > 65535) port = remoteSchemes[type].port;

    if (name.empty() || host.empty()) throw std::runtime_error("main/remote/required"_i18n);

    std::string path = trim(this->cellPath->getValue());
    if (path.empty()) path = pathFromHost;
    if (path.empty()) path = "/";
    if (path.front() != '/') path = "/" + path;

    std::string user = this->cellUser->getValue();
    std::string passwd = this->cellPasswd->getValue();

    AppRemote r;
    r.name = name;
    if (this->editIndex >= 0) {
        // field without UI: preserved as is when editing
        r.user_agent = AppConfig::instance().getRemotes()[this->editIndex].user_agent;
    }

    std::string userinfo;
    if (inlineCredentials(type)) {
        // AVIO (ffmpeg) does not read the user/passwd fields: credentials
        // percent-encoded in the URL; avio joins `path + "/" + name`,
        // so no trailing slash
        if (!user.empty()) {
            userinfo = urlEscape(user);
            if (!passwd.empty()) userinfo += ":" + urlEscape(passwd);
            userinfo += "@";
        }
        if (path.size() > 1 && path.back() == '/') path.pop_back();
    } else {
        // Webdav/Apache: basic auth via the fields; the Apache client joins
        // relative links to the current path -> trailing slash required
        r.user = user;
        r.passwd = passwd;
        if (path.back() != '/') path += '/';
    }
    r.url = fmt::format("{}://{}{}:{}{}", remoteSchemes[type].scheme, userinfo, host, port, path);
    return r;
}

void RemoteAdd::submit() {
    AppRemote r;
    try {
        r = this->build();
    } catch (const std::exception& ex) {
        Dialog::show(ex.what());
        return;
    }

    this->btnSave->setVisibility(brls::Visibility::GONE);
    this->spinner->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::blockInputs();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, r]() {
        try {
            // connection test: same code path as the real navigation
            // (remote::create routes the scheme, list() lists the root)
            auto client = remote::create(r);
            client->list(r.url);
            brls::sync([ASYNC_TOKEN, r]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                this->spinner->setVisibility(brls::Visibility::GONE);
                this->btnSave->setVisibility(brls::Visibility::VISIBLE);

                auto& conf = AppConfig::instance();
                if (this->editIndex < 0)
                    conf.addRemote(r);
                else
                    conf.updateRemote((size_t)this->editIndex, r);
                auto cb = this->onDone;
                brls::Application::popActivity();
                if (cb) cb();
            });
        } catch (const std::exception& ex) {
            std::string error = fmt::format(fmt::runtime("main/remote/test_failed"_i18n), ex.what());
            brls::sync([ASYNC_TOKEN, error]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                this->spinner->setVisibility(brls::Visibility::GONE);
                this->btnSave->setVisibility(brls::Visibility::VISIBLE);
                Dialog::cancelable(fmt::format("{}\n\n{}", error, "hints/retry"_i18n), [this]() { this->submit(); });
            });
        }
    });
}
