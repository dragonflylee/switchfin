#include "activity/hint_activity.hpp"

#ifdef BUILTIN_NSP
#include <nspmini.hpp>
#endif

using namespace brls::literals;

HintActivity::HintActivity() { brls::Logger::debug("HintActivity: create"); }

void HintActivity::onContentAvailable() {
    brls::Logger::debug("HintActivity: onContentAvailable");

    // The same screen serves both contexts: at applet boot (insufficient
    // memory) and from settings / first launch in application mode
    // (HOME tile installation). Only the text changes.
    if (brls::Application::getPlatform()->isApplicationMode()) {
        this->labelText->setText("main/hints/text_app"_i18n);
    }

#ifdef BUILTIN_NSP
    this->btnInstall->registerClickAction([](...) -> bool {
        auto dialog = new brls::Dialog("main/hints/hint_confirm"_i18n);
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->addButton("hints/ok"_i18n, []() {
            brls::Application::blockInputs();
            mini::InstallSD("romfs:/forwarder.nsp");
            unsigned long long AppTitleID = mini::GetTitleID();
            brls::Application::unblockInputs();
            // The old pleNx HOME tile is a separate installed title that stays
            // until removed by hand (updating the app can't touch it). Point the
            // user at it, then launch the freshly installed GMCA tile.
            auto info = new brls::Dialog("main/hints/remove_old"_i18n);
            info->addButton("hints/ok"_i18n, [AppTitleID]() { appletRequestLaunchApplication(AppTitleID, NULL); });
            info->open();
        });
        dialog->open();
        return true;
    });
    this->btnInstall->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnInstall));
#else
    // no embedded NSP in this build: only the title takeover option remains
    this->boxInstall->setVisibility(brls::Visibility::GONE);
#endif
}

HintActivity::~HintActivity() { brls::Logger::debug("HintActivity: delete"); }
