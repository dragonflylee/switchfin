/*
    Switchlex — connexion à Plex.
    Flux PIN : PLEX_MIGRATION.md §2.2 ; découverte serveurs/profils : §2.3.
*/

#include "tab/server_add.hpp"
#include "activity/main_activity.hpp"
#include "api/plex.hpp"
#include "api/plex/auth.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

/// Dialogue d'association : affiche le code et interroge plex.tv jusqu'à
/// validation (même mécanique que l'ancien Quick Connect Jellyfin).
class PlexLinkView : public brls::Box {
public:
    PlexLinkView(const plex::PinResult& pin, std::function<void(std::string)> onToken)
        : pin(pin), onToken(onToken) {
        brls::Logger::debug("View PlexLinkView: create");
        this->inflateFromXMLRes("xml/view/plex_pin.xml");
        this->isCancel = std::make_shared<std::atomic_bool>(false);
        this->labelCode->setText(this->pin.code);
        this->ticker.setCallback([this]() { this->query(); });
    }

    void open() {
        auto dialog = new brls::Dialog(this);
        dialog->addButton("hints/cancel"_i18n, [this]() {
            this->isCancel->store(true);
            this->ticker.stop();
        });
        dialog->open();
        // expiration du PIN côté plex.tv : ~2 minutes (§2.2)
        this->deadline = brls::getCPUTimeUsec() + 120 * 1000000;
        this->ticker.start(2000);
    }

    void query() {
        if (brls::getCPUTimeUsec() > this->deadline) {
            this->ticker.stop();
            this->labelCode->setText("main/plex/expired"_i18n);
            return;
        }
        ASYNC_RETAIN
        brls::async([ASYNC_TOKEN]() {
            try {
                std::string account = plex::pollPin(this->pin.id);
                brls::sync([ASYNC_TOKEN, account]() {
                    ASYNC_RELEASE
                    if (account.empty() || this->isCancel->load()) return;
                    this->ticker.stop();
                    auto cb = this->onToken;
                    this->dismiss([cb, account]() { cb(account); });
                });
            } catch (const std::exception& ex) {
                // 404/410 = PIN expiré (plex_auth_service.dart:151-167)
                std::string msg = ex.what();
                brls::sync([ASYNC_TOKEN, msg]() {
                    ASYNC_RELEASE
                    this->ticker.stop();
                    if (!this->isCancel->load()) this->labelCode->setText(msg);
                });
            }
        });
    }

    ~PlexLinkView() override {
        brls::Logger::debug("View PlexLinkView: delete");
        this->ticker.stop();
    }

private:
    BRLS_BIND(brls::Label, labelCode, "plex/label/code");

    HTTP::Cancel isCancel;
    brls::RepeatingTimer ticker;
    brls::Time deadline = 0;
    plex::PinResult pin;
    std::function<void(std::string)> onToken;
};

ServerAdd::ServerAdd() {
    this->inflateFromXMLRes("xml/tabs/server_add.xml");
    brls::Logger::debug("ServerAdd: create");

    btnLink->registerClickAction([this](...) { return this->onLink(); });
    inputUrl->init("URL", "", [](std::string) {}, "http://<IP du serveur>:32400", "", 255);
    inputToken->init("main/plex/token"_i18n, "", [](std::string) {}, "X-Plex-Token", "", 255);
    btnConnect->registerClickAction([this](...) { return this->onManual(); });
}

ServerAdd::~ServerAdd() { brls::Logger::debug("ServerAdd Activity: delete"); }

brls::View* ServerAdd::getDefaultFocus() { return this->btnLink; }

bool ServerAdd::onLink() {
    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            plex::PinResult pin = plex::requestPin();
            brls::sync([ASYNC_TOKEN, pin]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                auto* view = new PlexLinkView(pin, [this](const std::string& token) { this->onAccount(token); });
                view->open();
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
    return true;
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
            // resources AVEC le token du profil : l'accessToken serveur dépend
            // du profil actif (PLEX_MIGRATION.md §2.3, trois tokens)
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
                // 403 = mauvais PIN (code 1041 ; plex_home_switch.dart:74-81)
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

bool ServerAdd::onManual() {
    std::string baseUrl = this->inputUrl->getValue();
    std::string serverToken = this->inputToken->getValue();
    if (baseUrl.length() < 10 || baseUrl.substr(0, 4).compare("http")) {
        Dialog::show("main/setting/server/invalid"_i18n);
        return false;
    }
    while (baseUrl.back() == '/') baseUrl.pop_back();

    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, baseUrl, serverToken]() {
        try {
            // /identity est accessible sans auth : machine id + nom (§2.3)
            nlohmann::json identity = plex::getSync(baseUrl + std::string(plex::apiIdentity), "");
            const nlohmann::json& mc = identity.contains("MediaContainer") ? identity.at("MediaContainer") : identity;
            plex::ServerResource server;
            server.clientIdentifier = plex::jstr(mc, "machineIdentifier");
            server.name = plex::jstr(mc, "friendlyName", baseUrl);
            server.accessToken = serverToken;
            if (server.clientIdentifier.empty()) throw std::runtime_error("machineIdentifier absent");
            if (!plex::probeConnection(baseUrl, serverToken)) throw std::runtime_error("main/plex/unreachable"_i18n);

            // Le token saisi est le plus souvent un token de compte : tenter de
            // récupérer le profil ; sinon identité locale de repli.
            std::string uuid = "manual-" + server.clientIdentifier;
            std::string name = server.name;
            std::string thumb;
            try {
                plex::AccountUser account = plex::getUser(serverToken);
                uuid = account.uuid;
                name = account.username;
                thumb = account.thumb;
            } catch (const std::exception& ex) {
                brls::Logger::warning("manual getUser: {}", ex.what());
            }

            brls::sync([ASYNC_TOKEN, uuid, name, thumb, serverToken, server, baseUrl]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                this->finish(uuid, name, thumb, serverToken, server, baseUrl);
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
    return true;
}
