/*
    GMCA — sign-in form for a Stremio account (see stremio_add.hpp).

    A clickable cell per field opens the IME for THAT field only (no chained IME
    prompts). "Connect" validates the form, then runs stremio::auth::login off
    the UI thread (POST /api/login + /api/addonCollectionGet); on success the
    account is stored as a "stremio" AppServer + AppUser and MainActivity is
    entered, on failure the api.strem.io message is shown in the status label.

    Presented as a content view of the ServerList AppletFrame (View::present
    from ServerTypeChoose), so it keeps the footer; Back is governed by the
    frame's hints/back action (no own BUTTON_B handler — that would override it
    and popActivity the whole ServerList).
*/

#include "tab/stremio_add.hpp"
#include "activity/main_activity.hpp"
#include "api/stremio/auth.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

static std::string trim(const std::string& in) {
    auto a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

StremioAdd::StremioAdd() {
    this->inflateFromXMLRes("xml/view/stremio_add.xml");
    brls::Logger::debug("StremioAdd: create");

    this->header->setText("main/server/stremio/title"_i18n);

    this->cellEmail->init("main/server/stremio/email"_i18n, "", [](std::string) {}, "", "", 128);
    this->cellPasswd->init("main/server/stremio/password"_i18n, "", [](std::string) {}, "", "", 128);
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

StremioAdd::~StremioAdd() { brls::Logger::debug("StremioAdd: delete"); }

brls::View* StremioAdd::getDefaultFocus() { return this->cellEmail; }

void StremioAdd::submit() {
    std::string email = trim(this->cellEmail->getValue());
    std::string pass = this->cellPasswd->getValue();

    if (email.empty() || pass.empty()) {
        this->labelStatus->setText("main/server/stremio/invalid"_i18n);
        return;
    }

    this->labelStatus->setText("main/plex/connecting"_i18n);
    this->btnConnect->setVisibility(brls::Visibility::GONE);
    this->spinner->setVisibility(brls::Visibility::VISIBLE);
    brls::Application::blockInputs();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, email, pass]() {
        std::string error;
        stremio::Account a;
        bool ok = false;
        try {
            a = stremio::login(email, pass);
            ok = true;
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        brls::sync([ASYNC_TOKEN, a, ok, error]() {
            ASYNC_RELEASE
            brls::Application::unblockInputs();
            this->spinner->setVisibility(brls::Visibility::GONE);
            this->btnConnect->setVisibility(brls::Visibility::VISIBLE);
            if (ok)
                this->finish(a);
            else
                this->labelStatus->setText(error);
        });
    });
}

void StremioAdd::finish(const stremio::Account& a) {
    AppServer s;
    s.type = "stremio";
    s.id = a.userId;
    s.name = "Stremio";
    s.access_token = a.authKey;
    s.urls = {"https://api.strem.io"};
    s.addons = a.addons;
    AppConfig::instance().addServer(s);

    AppUser u;
    u.id = a.userId;
    u.name = a.userName;
    u.access_token = a.authKey;
    u.server_id = a.userId;
    u.thumb = "";
    AppConfig::instance().addUser(u, s.urls.front());

    brls::Application::clear();
    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
}
