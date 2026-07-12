#include "activity/hint_activity.hpp"

#ifdef BUILTIN_NSP
#include <nspmini.hpp>
#include <filesystem>
#include "utils/config.hpp"
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
            // The forwarder launches sdmc:/switch/GMCA.nro. pleNx users who
            // self-updated in place still have the binary at the old path
            // (AppVersion::nro_path, e.g. .../pleNx.nro), so stage it under the
            // canonical name first — otherwise the freshly installed GMCA tile
            // falls through to hbmenu. Best-effort; the forwarder also falls back
            // to the legacy pleNx path.
            try {
                const std::string& src = AppVersion::nro_path;
                const std::string dst = "sdmc:/switch/GMCA.nro";
                if (src.size() > 4 && src != dst && std::filesystem::exists(src))
                    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
            } catch (const std::exception& e) {
                brls::Logger::warning("forwarder: could not stage GMCA.nro: {}", e.what());
            }
            mini::InstallSD("romfs:/forwarder.nsp");
            unsigned long long AppTitleID = mini::GetTitleID();
            appletRequestLaunchApplication(AppTitleID, NULL);
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
