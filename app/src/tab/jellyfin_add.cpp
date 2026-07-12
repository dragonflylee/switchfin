/*
    GMCA — sign-in form for a Jellyfin or Emby server.

    A clickable cell per field opens the IME for THAT field only (no chained
    IME prompts — the chaining used to flicker open then close at once on
    desktop GLFW). "Connect" validates the URL, then runs jellyfin::getServerName
    + jellyfin::login off the UI thread; on success the server/user are stored
    and MainActivity is entered, on failure the message is shown in the status
    label.

    Presented as a content view of the ServerList AppletFrame (View::present
    from ServerTypeChoose), so it keeps the footer; Back is governed by the
    frame's hints/back action (no own BUTTON_B handler — that would override it
    and popActivity the whole ServerList).
*/

#include "tab/jellyfin_add.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin/auth.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

static std::string trim(const std::string& in) {
    auto a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

JellyfinAdd::JellyfinAdd(const std::string& type) : type(type) {
    this->inflateFromXMLRes("xml/view/jellyfin_add.xml");
    brls::Logger::debug("JellyfinAdd: create ({})", type);

    this->header->setText(type == "emby" ? "main/server/jellyfin/title_emby"_i18n
                                          : "main/server/jellyfin/title_jellyfin"_i18n);

    this->cellUrl->init("main/server/jellyfin/url"_i18n, "", [](std::string) {}, "http://host:8096", "", 256);
    this->cellUser->init("main/server/jellyfin/username"_i18n, "", [](std::string) {}, "", "", 64);
    this->cellPasswd->init("main/server/jellyfin/password"_i18n, "", [](std::string) {}, "", "", 128);
    this->cellPasswd->setType(brls::InputCellType::PASSWORD);

    this->btnConnect->registerClickAction([this](...) {
        this->submit();
        return true;
    });
    // No BUTTON_B handler here: Back is owned by the ServerList AppletFrame's
    // hints/back action, which dismiss()es this content view back to the type
    // chooser. A local handler would be found first (it sits below the frame)
    // and popActivity the whole ServerList instead.
}

JellyfinAdd::~JellyfinAdd() { brls::Logger::debug("JellyfinAdd: delete"); }

brls::View* JellyfinAdd::getDefaultFocus() { return this->cellUrl; }

void JellyfinAdd::submit() {
    std::string url = trim(this->cellUrl->getValue());
    // strip any trailing slashes so baseUrl + "/path" never doubles up
    while (!url.empty() && url.back() == '/') url.pop_back();

    if (url.empty() || (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)) {
        this->labelStatus->setText("main/setting/server/invalid"_i18n);
        return;
    }

    std::string user = this->cellUser->getValue();
    std::string pass = this->cellPasswd->getValue();

    this->labelStatus->setText("main/plex/connecting"_i18n);
    this->btnConnect->setVisibility(brls::Visibility::GONE);
    this->spinner->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::blockInputs();

    std::string type = this->type;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, type, url, user, pass]() {
        std::string error;
        jellyfin::LoginResult r;
        bool ok = false;
        try {
            std::string name = jellyfin::getServerName(url);
            r = jellyfin::login(url, user, pass);
            r.serverName = name;
            ok = true;
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        brls::sync([ASYNC_TOKEN, url, r, ok, error]() {
            ASYNC_RELEASE
            brls::Application::unblockInputs();
            this->spinner->setVisibility(brls::Visibility::GONE);
            this->btnConnect->setVisibility(brls::Visibility::VISIBLE);
            if (ok)
                this->finish(url, r);
            else
                this->labelStatus->setText(error);
        });
    });
}

void JellyfinAdd::finish(const std::string& url, const jellyfin::LoginResult& r) {
    AppServer s;
    s.name = r.serverName.empty() ? url : r.serverName;
    s.id = r.serverId.empty() ? url : r.serverId;
    s.access_token = r.token;
    s.urls.push_back(url);
    s.type = this->type;
    AppConfig::instance().addServer(s);

    AppUser u;
    u.id = r.userId;
    u.name = r.userName;
    u.access_token = r.token;
    u.server_id = s.id;
    AppConfig::instance().addUser(u, url);

    brls::Application::clear();
    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
}
