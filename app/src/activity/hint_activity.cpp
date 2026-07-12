#include "activity/hint_activity.hpp"

#ifdef BUILTIN_NSP
#include <nspmini.hpp>
#include <switch.h>

// The pleNx HOME-tile forwarder shipped under two title ids over its lifetime;
// GMCA replaces it, so on tile install we offer to remove whichever is present.
// IMPORTANT: Switchfin's own forwarder id (010ff000ffff0003) is one hex digit
// from the early-pleNx one and must NEVER be matched — only these two exact ids.
static const u64 PLENX_FORWARDER_IDS[] = {
    0x0104201312000000ULL,  // pleNx v0.1.3 .. v0.1.15
    0x010FF000FFFF1003ULL,  // pleNx v0.1.0 .. v0.1.2
};

/// Returns the installed legacy pleNx forwarder title id (0 if none). Exact
/// match only, so a Switchfin install is never touched.
static u64 installedLegacyPlenxTile() {
    if (R_FAILED(nsInitialize())) return 0;
    u64 found = 0;
    NsApplicationRecord record;
    s32 count = 0;
    for (s32 offset = 0; found == 0 && R_SUCCEEDED(nsListApplicationRecord(&record, 1, offset, &count)) && count > 0;
         offset++) {
        for (size_t i = 0; i < sizeof(PLENX_FORWARDER_IDS) / sizeof(PLENX_FORWARDER_IDS[0]); i++) {
            if (record.application_id == PLENX_FORWARDER_IDS[i]) {
                found = PLENX_FORWARDER_IDS[i];
                break;
            }
        }
    }
    nsExit();
    return found;
}
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

            // Migration: if the old pleNx tile the GMCA one replaces is still on
            // the HOME menu, offer to remove it. Skip straight to launch otherwise.
            u64 legacy = installedLegacyPlenxTile();
            if (legacy == 0) {
                appletRequestLaunchApplication(AppTitleID, NULL);
                return;
            }
            brls::Application::unblockInputs();
            auto rm = new brls::Dialog("main/hints/remove_old"_i18n);
            rm->addButton("hints/cancel"_i18n, [AppTitleID]() { appletRequestLaunchApplication(AppTitleID, NULL); });
            rm->addButton("hints/ok"_i18n, [legacy, AppTitleID]() {
                if (R_SUCCEEDED(nsInitialize())) {
                    nsDeleteApplicationCompletely(legacy);
                    nsExit();
                }
                appletRequestLaunchApplication(AppTitleID, NULL);
            });
            rm->open();
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
