/*
    pleNx — connection switcher grid (see connection_switcher.hpp).
*/

#include "view/connection_switcher.hpp"
#include "activity/main_activity.hpp"
#include "activity/loading_activity.hpp"
#include "tab/server_type_choose.hpp"
#include "tab/media_collection.hpp"
#include "utils/config.hpp"
#include "utils/image.hpp"
#include "utils/dialog.hpp"
#include "utils/theme_palette.hpp"
#include "view/svg_image.hpp"
#include "api/plex/auth.hpp"
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstdio>

using namespace brls::literals;  // for _i18n

namespace {

/// Middle-elides an overly long URL: "https://90-105-213-…plex.direct:32400".
std::string elideMiddle(const std::string& s, size_t budget) {
    if (s.size() <= budget) return s;
    size_t keep = budget - 1;  // 1 slot for the "…" ellipsis
    size_t head = (keep + 1) / 2;
    size_t tail = keep - head;
    return s.substr(0, head) + "…" + s.substr(s.size() - tail);
}

/// linear RGBA blend (t=0 -> a, t=1 -> b). NVGcolor is 4 floats in [0,1].
NVGcolor mix(NVGcolor a, NVGcolor b, float t) {
    NVGcolor c;
    for (int i = 0; i < 4; i++) c.rgba[i] = a.rgba[i] * (1.f - t) + b.rgba[i] * t;
    return c;
}

std::string displayType(const std::string& t) {
    if (t == "jellyfin") return "Jellyfin";
    if (t == "emby") return "Emby";
    if (t == "stremio") return "Stremio";
    return "Plex";
}

std::string toUpper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

/// "#RRGGBB" for an NVGcolor — used to bake a fill into an inline SVG string.
std::string hex(NVGcolor c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", (int)(c.r * 255 + 0.5f), (int)(c.g * 255 + 0.5f),
        (int)(c.b * 255 + 0.5f));
    return buf;
}

/// A bare white check (Material "done" outline), for the connected badge.
const char* kCheckSVG =
    R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z" fill="#FFFFFF"/></svg>)";

/// A rounded plus (the ico-plus glyph) filled with `color` — for the add tile.
std::string plusSVG(const std::string& color) {
    return std::string(
               R"(<svg width="24" height="24" viewBox="0 0 24 24"><path d="M12 4C12.69 4 13.25 4.56 13.25 5.25V10.75H18.75C19.44 10.75 20 11.31 20 12C20 12.69 19.44 13.25 18.75 13.25H13.25V18.75C13.25 19.44 12.69 20 12 20C11.31 20 10.75 19.44 10.75 18.75V13.25H5.25C4.56 13.25 4 12.69 4 12C4 11.31 4.56 10.75 5.25 10.75H10.75V5.25C10.75 4.56 11.31 4 12 4Z" fill=")") +
           color + R"(" /></svg>)";
}

/// A server "name" is a real identity only when it is not empty and not a bare
/// machine id (a long pure-hex string, e.g. Jellyfin's "db942b3f334d").
bool isReadableName(const std::string& s) {
    if (s.empty()) return false;
    if (s.size() >= 8 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return false;
    return true;
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

}  // namespace

/// Connect with a remembered user (a connection tile, or the add-flow result):
/// refresh the token, find a reachable URL, then open the app. Shows a loading
/// screen during the (multi-second) URL probing. Type-aware (mirror checkLogin).
static void connectWithUser(const AppUser& u) {
    brls::Application::blockInputs();
    brls::Application::pushActivity(new LoadingActivity(), brls::TransitionAnimation::NONE);
    std::string unreachable = "main/plex/unreachable"_i18n;

    brls::async([u, unreachable]() {
        try {
            AppServer target;
            bool found = false;
            for (auto& s : AppConfig::instance().getServers()) {
                if (s.id == u.server_id) {
                    target = s;
                    found = true;
                }
            }
            if (!found) throw std::runtime_error(unreachable);

            // Plex only: refresh this profile's server token from plex.tv.
            std::optional<plex::ServerResource> fresh;
            if (target.type == "plex") {
                try {
                    for (auto& r : plex::getResources(u.access_token)) {
                        if (r.clientIdentifier != target.id) continue;
                        if (!r.accessToken.empty()) target.access_token = r.accessToken;
                        fresh = r;
                    }
                } catch (const std::exception& ex) {
                    brls::Logger::warning("refresh resources: {}", ex.what());
                }
            }

            // Stremio has no single reachable endpoint: accept the stored url.
            std::string base;
            for (auto& url : target.urls) {
                if (target.type != "stremio" && !plex::probeConnection(url, target.access_token)) continue;
                base = url;
                break;
            }
            if (base.empty() && fresh) base = plex::findBestConnection(*fresh);
            if (base.empty()) throw std::runtime_error(unreachable);

            brls::sync([u, target, base]() {
                AppServer s = target;
                s.urls = {base};
                AppConfig::instance().addServer(s);
                AppConfig::instance().addUser(u, base);
                brls::Application::unblockInputs();
                brls::Application::clear();
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                MediaCollection::clearPref();
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([msg]() {
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                Dialog::show(msg);
            });
        }
    });
}

/// One connection (server + profile), tinted by its backend brand. Three clearly
/// distinct states render so the screen always answers "which one am I connected
/// to?" and "which one has focus?":
///   - resting   : faint brand tint, hairline border
///   - connected : stronger tint + brand border + a check badge on the avatar
///                 (persists regardless of focus — the unambiguous "active" mark)
///   - focused   : brightest tint, full brand border, avatar ring lit, a drop
///                 shadow lifts the card (custom — the default highlight is hidden
///                 so focus never washes out the brand colors).
class ConnectionTile : public brls::Box {
public:
    ConnectionTile(const AppUser& u, const AppServer& srv, ConnectionSwitcher* parent) {
        this->inflateFromXMLRes("xml/view/connection_tile.xml");
        auto theme = brls::Application::getTheme();

        const plenx::AccentRGB& a = plenx::backendPalette(AppConfig::backendTypeFromString(srv.type)).dark.accent;
        this->accent = nvgRGB(a.r, a.g, a.b);
        this->surface = theme.getColor("color/surface");
        this->active = (u.id == AppConfig::instance().getUserId());

        // fully custom focus state: hide the Borealis highlight (its glow/wash
        // reads as "bizarre" over the tinted cards).
        this->setHideHighlight(true);

        this->ring->setBackgroundColor(mix(theme.getColor("color/grey_1"), this->accent, 0.22f));
        if (!u.thumb.empty()) Image::with(this->avatar, u.thumb);

        // title = server name if it's a real custom name, else the profile name.
        std::string typeName = displayType(srv.type);
        bool customServer = isReadableName(srv.name) && !iequals(srv.name, typeName);
        std::string url = srv.urls.empty() ? "" : srv.urls.front();
        this->title->setText(customServer ? srv.name : u.name);
        // the brand stamp: always shown, brand-colored, uppercased.
        this->type->setText(toUpper(typeName));
        this->type->setTextColor(this->accent);
        // the profile name matters more than the url: show it as the detail when
        // the title is the server; otherwise the title already is the profile, so
        // fall back to the url (the only locator for machine-id servers).
        this->detail->setText(customServer ? u.name : elideMiddle(url, 24));

        if (this->active) {
            this->badge->setVisibility(brls::Visibility::VISIBLE);
            this->badge->setBackgroundColor(this->accent);
            this->check->setImageFromSVGString(kCheckSVG);
        }

        this->applyVisual(false);

        this->registerClickAction([u](brls::View*) {
            connectWithUser(u);
            return true;
        });
        this->registerAction("hints/delete"_i18n, brls::BUTTON_X, [u, parent](brls::View*) {
            Dialog::cancelable("main/setting/server/delete"_i18n, [u, parent]() {
                AppConfig::instance().removeUser(u.id);
                parent->rebuild();
                // the deleted tile is gone: refocus a remaining tile / the "+".
                brls::Application::giveFocus(parent->getDefaultFocus());
            });
            return true;
        });
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->applyVisual(true);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->applyVisual(false);
    }

private:
    void applyVisual(bool focused) {
        bool strong = focused || this->active;
        float tint = focused ? 0.24f : (this->active ? 0.14f : 0.05f);
        NVGcolor bg = mix(this->surface, this->accent, tint);

        this->setBackgroundColor(bg);
        this->setBorderColor(strong ? this->accent : mix(this->surface, this->accent, 0.28f));
        this->setBorderThickness(focused ? 2.5f : (this->active ? 2.f : 1.f));

        this->ring->setBorderColor(strong ? this->accent : mix(this->surface, this->accent, 0.45f));
        this->ring->setBorderThickness(strong ? 2.5f : 2.f);

        this->title->setTextColor(strong ? this->accent : brls::Application::getTheme().getColor("brls/text"));

        // the badge's outline matches the card fill so the disc reads as cut into
        // the avatar; track the fill as it brightens on focus.
        if (this->active) this->badge->setBorderColor(bg);

        this->setShadowType(focused ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
        this->setShadowVisibility(focused);
    }

    NVGcolor accent {};
    NVGcolor surface {};
    bool active = false;

    BRLS_BIND(brls::Box, ring, "tile/ring");
    BRLS_BIND(brls::Image, avatar, "tile/avatar");
    BRLS_BIND(brls::Box, badge, "tile/badge");
    BRLS_BIND(SVGImage, check, "tile/check");
    BRLS_BIND(brls::Label, title, "tile/title");
    BRLS_BIND(brls::Label, type, "tile/type");
    BRLS_BIND(brls::Label, detail, "tile/detail");
};

/// The trailing "+" tile: starts the add-connection flow (server type chooser).
/// Neutral (pleNx accent, not a backend brand) and dashed-quiet at rest; on focus
/// it lights up exactly like a connection tile (bright border, fill, lift) so the
/// focus state is unmistakable everywhere on the grid. The "+" is an SVG glyph so
/// it sits perfectly centered in its ring (a Label baseline does not).
class AddTile : public brls::Box {
public:
    AddTile() {
        auto theme = brls::Application::getTheme();
        this->grey = theme.getColor("font/grey");
        this->line = theme.getColor("color/line");
        this->surface = theme.getColor("color/surface");
        this->appAccent = theme.getColor("color/app");

        this->setAxis(brls::Axis::COLUMN);
        this->setWidth(184);
        this->setHeight(220);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setCornerRadius(18);
        this->setBorderThickness(1.5f);
        this->setFocusable(true);
        this->setHideHighlight(true);

        this->plusRing = new brls::Box();
        this->plusRing->setWidth(66);
        this->plusRing->setHeight(66);
        this->plusRing->setCornerRadius(33);
        this->plusRing->setBorderThickness(1.5f);
        this->plusRing->setAlignItems(brls::AlignItems::CENTER);
        this->plusRing->setJustifyContent(brls::JustifyContent::CENTER);
        this->plusRing->setMarginBottom(16);
        this->plus = new SVGImage();
        this->plus->setWidth(30);
        this->plus->setHeight(30);
        this->plusRing->addView(this->plus);
        this->addView(this->plusRing);

        this->lbl = new brls::Label();
        this->lbl->setText("main/server/add"_i18n);
        this->lbl->setFontSize(15);
        this->addView(this->lbl);

        this->applyVisual(false);

        this->registerClickAction([](brls::View* view) {
            view->present(new ServerTypeChoose());
            return true;
        });
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        this->applyVisual(true);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->applyVisual(false);
    }

private:
    void applyVisual(bool focused) {
        NVGcolor fg = focused ? this->appAccent : this->grey;
        this->setBackgroundColor(focused ? mix(this->surface, this->appAccent, 0.14f) : nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(focused ? this->appAccent : this->line);
        this->setBorderThickness(focused ? 2.5f : 1.5f);
        this->plusRing->setBorderColor(fg);
        this->plus->setImageFromSVGString(plusSVG(hex(fg)));
        this->lbl->setTextColor(fg);
        this->setShadowType(focused ? brls::ShadowType::GENERIC : brls::ShadowType::NONE);
        this->setShadowVisibility(focused);
    }

    NVGcolor grey {}, line {}, surface {}, appAccent {};
    brls::Box* plusRing = nullptr;
    SVGImage* plus = nullptr;
    brls::Label* lbl = nullptr;
};

ConnectionSwitcher::ConnectionSwitcher() {
    this->setAxis(brls::Axis::COLUMN);
    this->setGrow(1.0f);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(40, 40, 40, 40);

    auto* title = new brls::Label();
    title->setText("main/server/switch_title"_i18n);
    title->setFontSize(34);
    title->setMarginBottom(36);
    this->addView(title);

    this->tilesBox = new brls::Box();
    this->tilesBox->setAxis(brls::Axis::ROW);
    this->tilesBox->setJustifyContent(brls::JustifyContent::CENTER);
    this->tilesBox->setAlignItems(brls::AlignItems::CENTER);
    this->addView(this->tilesBox);

    this->rebuild();
}

void ConnectionSwitcher::rebuild() {
    this->tilesBox->clearViews();
    this->firstFocus = nullptr;

    const auto& servers = AppConfig::instance().getServers();
    auto serverFor = [&](const std::string& id) -> const AppServer* {
        for (auto& s : servers)
            if (s.id == id) return &s;
        return nullptr;
    };

    for (auto& u : AppConfig::instance().getUsers()) {
        const AppServer* srv = serverFor(u.server_id);
        if (!srv) continue;  // orphan profile (server removed) — skip
        auto* tile = new ConnectionTile(u, *srv, this);
        tile->setMarginRight(18);
        this->tilesBox->addView(tile);
        if (u.id == AppConfig::instance().getUserId()) this->firstFocus = tile;
    }

    auto* add = new AddTile();
    this->tilesBox->addView(add);
    if (!this->firstFocus) this->firstFocus = this->tilesBox->getChildren().front();
}

brls::View* ConnectionSwitcher::getDefaultFocus() {
    return this->firstFocus ? this->firstFocus : brls::Box::getDefaultFocus();
}
