/*
    pleNx — sign-in to Plex.
    PIN flow (only method): PLEX_MIGRATION.md §2.2;
    server/profile discovery: §2.3.
*/

#include "tab/server_add.hpp"
#include "activity/main_activity.hpp"
#include "api/plex.hpp"
#include "api/plex/auth.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

/// "SC4B" -> "S C 4 B": the code breathes at large size
static std::string spaced(const std::string& code) {
    std::string out;
    for (size_t i = 0; i < code.size(); i++) {
        if (i > 0) out += ' ';
        out += code[i];
    }
    return out;
}

ServerAdd::ServerAdd() {
    this->inflateFromXMLRes("xml/tabs/server_add.xml");
    brls::Logger::debug("ServerAdd: create");

    this->btnRetry->registerClickAction([this](...) {
        this->startPin();
        return true;
    });
    this->ticker.setCallback([this]() { this->pollOnce(); });
    this->startPin();
}

ServerAdd::~ServerAdd() {
    brls::Logger::debug("ServerAdd: delete");
    this->ticker.stop();
}

brls::View* ServerAdd::getDefaultFocus() { return this->btnRetry; }

void ServerAdd::startPin() {
    this->ticker.stop();
    this->labelCode->setText("· · · ·");
    this->labelStatus->setText("main/plex/generating"_i18n);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            plex::PinResult fresh = plex::requestPin();
            brls::sync([ASYNC_TOKEN, fresh]() {
                ASYNC_RELEASE
                this->pin = fresh;
                this->labelCode->setText(spaced(fresh.code));
                this->labelStatus->setText("main/plex/waiting"_i18n);
                // PIN expiry on plex.tv side: ~2 minutes (§2.2)
                this->deadline = brls::getCPUTimeUsec() + 120 * 1000000;
                this->ticker.start(2000);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->labelCode->setText("· · · ·");
                this->labelStatus->setText(msg);
            });
        }
    });
}

void ServerAdd::pollOnce() {
    if (brls::getCPUTimeUsec() > this->deadline) {
        this->ticker.stop();
        this->labelStatus->setText("main/plex/expired"_i18n);
        return;
    }
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            std::string account = plex::pollPin(this->pin.id);
            brls::sync([ASYNC_TOKEN, account]() {
                ASYNC_RELEASE
                if (account.empty()) return;
                this->ticker.stop();
                this->labelStatus->setText("main/plex/connected"_i18n);
                this->onAccount(account);
            });
        } catch (const std::exception& ex) {
            // 404/410 = expired PIN
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->ticker.stop();
                this->labelStatus->setText(msg);
            });
        }
    });
}

void ServerAdd::onAccount(const std::string& accountToken) {
    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, accountToken]() {
        try {
            plex::AccountUser account = plex::getUser(accountToken);
            std::vector<plex::ServerResource> servers = plex::getResources(accountToken);
            brls::sync([ASYNC_TOKEN, account, accountToken, servers]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                if (servers.empty()) {
                    Dialog::show("main/plex/no_server"_i18n);
                    return;
                }
                std::vector<std::string> names;
                for (auto& s : servers) names.push_back(s.owned ? s.name : fmt::format("{} 🔗", s.name));
                auto* dropdown = new brls::Dropdown("main/plex/choose_server"_i18n, names,
                    [this, account, accountToken, servers](int selected) {
                        if (selected < 0) return;
                        this->onServerPicked(account, accountToken, servers.at(selected));
                    });
                brls::Application::pushActivity(new brls::Activity(dropdown));
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    });
}

void ServerAdd::onServerPicked(
    const plex::AccountUser& account, const std::string& accountToken, const plex::ServerResource& server) {
    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, account, accountToken, server]() {
        try {
            std::string base = plex::findBestConnection(server);
            if (base.empty()) throw std::runtime_error("main/plex/unreachable"_i18n);

            std::vector<plex::HomeUser> homes;
            try {
                homes = plex::getHomeUsers(accountToken);
            } catch (const std::exception& ex) {
                brls::Logger::warning("getHomeUsers: {}", ex.what());
            }

            brls::sync([ASYNC_TOKEN, account, accountToken, server, base, homes]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                if (homes.size() > 1) {
                    std::vector<std::string> names;
                    for (auto& h : homes) names.push_back(h.isProtected ? fmt::format("{} 🔒", h.title) : h.title);
                    auto* dropdown = new brls::Dropdown("main/plex/choose_profile"_i18n, names,
                        [this, homes, accountToken, server, base](int selected) {
                            if (selected < 0) return;
                            this->onProfilePicked(homes.at(selected), accountToken, server, base);
                        });
                    brls::Application::pushActivity(new brls::Activity(dropdown));
                } else {
                    this->finish(account.uuid, account.username, account.thumb, accountToken, server, base);
                }
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    });
}

void ServerAdd::onProfilePicked(const plex::HomeUser& home, const std::string& accountToken,
    const plex::ServerResource& server, const std::string& baseUrl) {
    if (home.isProtected) {
        brls::Application::getImeManager()->openForText(
            [this, home, accountToken, server, baseUrl](const std::string& pin) {
                this->doSwitch(home, accountToken, server, baseUrl, pin);
            },
            "main/plex/profile_pin"_i18n, "", 4, "");
    } else {
        this->doSwitch(home, accountToken, server, baseUrl, "");
    }
}

void ServerAdd::doSwitch(const plex::HomeUser& home, const std::string& accountToken,
    const plex::ServerResource& server, const std::string& baseUrl, const std::string& pin) {
    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, home, accountToken, server, baseUrl, pin]() {
        try {
            std::string profileToken = plex::switchHomeUser(accountToken, home.uuid, pin);
            // resources WITH the profile token: the server accessToken
            // depends on the active profile (PLEX_MIGRATION.md §2.3, three tokens)
            plex::ServerResource fresh = server;
            for (auto& r : plex::getResources(profileToken)) {
                if (r.clientIdentifier == server.clientIdentifier) fresh = r;
            }
            brls::sync([ASYNC_TOKEN, home, profileToken, fresh, baseUrl]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                this->finish(home.uuid, home.title, home.thumb, profileToken, fresh, baseUrl);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                // 403 = wrong PIN (code 1041)
                Dialog::show(msg.find("403") != std::string::npos ? "main/plex/wrong_pin"_i18n : msg);
            });
        }
    });
}

void ServerAdd::finish(const std::string& uuid, const std::string& name, const std::string& thumb,
    const std::string& plexTvToken, const plex::ServerResource& server, const std::string& baseUrl) {
    AppServer s;
    s.name = server.name;
    s.id = server.clientIdentifier;
    s.access_token = server.accessToken;
    s.urls.push_back(baseUrl);
    for (auto& c : server.connections) {
        if (!c.uri.empty() && c.uri != baseUrl) s.urls.push_back(c.uri);
    }
    AppConfig::instance().addServer(s);

    AppUser u;
    u.id = uuid;
    u.name = name;
    u.access_token = plexTvToken;
    u.server_id = server.clientIdentifier;
    u.thumb = thumb;
    AppConfig::instance().addUser(u, baseUrl);

    brls::Application::clear();
    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
}
