#include "tab/remote_tab.hpp"
#include "tab/remote_view.hpp"
#include "tab/remote_add.hpp"
#include "tab/download_tab.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;

RemoteTab::RemoteTab() {
    this->inflateFromXMLRes("xml/tabs/remote.xml");
    brls::Logger::debug("RemoteTab: create");
    this->tabFrame->registerTabAction(this);

    // ajout d'un serveur WebDAV/FTP/SFTP : accessible depuis toutes les pills
    // (enregistré ici et non dans onCreate : refresh() rejoue onCreate)
    this->registerAction("main/remote/add_server"_i18n, brls::BUTTON_X, [this](...) {
        RemoteAdd::open([this]() { this->refresh(); });
        return true;
    });
}

RemoteTab::~RemoteTab() { brls::Logger::debug("RemoteTab: deleted"); }

brls::View* RemoteTab::create() { return new RemoteTab(); }

void RemoteTab::onCreate() {
    AutoSidebarItem* item;
    auto& conf = AppConfig::instance();
    auto& remotes = conf.getRemotes();
    for (size_t i = 0; i < remotes.size(); i++) {
        const AppRemote r = remotes[i];
        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        // 18 : même corps que les onglets des bibliothèques (collection)
        item->setFontSize(18);
        item->setLabel(r.name);

        // gérer (modifier/supprimer) depuis la pill comme depuis le contenu
        auto manage = [this, i, r](brls::View*) {
            this->manageRemote(i, r);
            return true;
        };
        item->registerAction("main/remote/manage"_i18n, brls::BUTTON_Y, manage);

        try {
            auto c = remote::create(r);
            auto view = new RemoteView(c);
            view->registerAction("main/remote/manage"_i18n, brls::BUTTON_Y, manage);
            this->tabFrame->addTab(item, [view, r]() {
                view->push(r.url);
                return view;
            });
        } catch (const std::exception& ex) {
            // schéma invalide ou non supporté : la pill reste affichée pour
            // pouvoir éditer/supprimer le serveur, le contenu montre l'erreur
            brls::Logger::warning("remote {} create {}", r.name, ex.what());
            std::string error = ex.what();
            this->tabFrame->addTab(item, [error]() {
                auto* box = new AttachedView();
                box->setJustifyContent(brls::JustifyContent::CENTER);
                box->setAlignItems(brls::AlignItems::CENTER);
                auto* label = new brls::Label();
                label->setFontSize(14);
                label->setTextColor(brls::Application::getTheme().getColor("font/grey"));
                label->setText(error);
                box->addView(label);
                return box;
            });
        }
    }

    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(18);
    item->setLabel("main/tabs/downloads"_i18n);
    this->tabFrame->addTab(item, []() { return new DownloadView(); });

    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(18);
    item->setLabel("main/remote/local"_i18n);
    this->tabFrame->addTab(item, [this]() {
        // état vide : bouton « Ajouter un serveur » sous le placeholder
        return new UmsView([this]() { RemoteAdd::open([this]() { this->refresh(); }); });
    });
}

void RemoteTab::refresh() {
    // le focus peut être dans une vue sur le point d'être détruite :
    // on le replace d'abord sur la sidebar du frame parent (qui survit)
    AutoTabFrame::focus2Sidebar(this);
    this->tabFrame->clearTabs();
    this->onCreate();
}

void RemoteTab::manageRemote(size_t index, const AppRemote& r) {
    std::vector<std::string> options{"main/remote/edit"_i18n, "main/remote/delete"_i18n};
    auto* dropdown = new brls::Dropdown(r.name, options, [this, index, r](int selected) {
        if (selected == 0) {
            RemoteAdd::open([this]() { this->refresh(); }, (int)index);
        } else if (selected == 1) {
            Dialog::cancelable(
                fmt::format(fmt::runtime("main/remote/delete_confirm"_i18n), r.name), [this, index]() {
                    AppConfig::instance().removeRemote(index);
                    this->refresh();
                });
        }
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}
