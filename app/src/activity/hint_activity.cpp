//
// Created by fang on 2022/8/21.
//

#include "activity/hint_activity.hpp"
#include "view/gallery_view.hpp"
#include "api/analytics.hpp"

#ifdef BUILTIN_NSP
#include <nspmini.hpp>
#endif

using namespace brls::literals;

const std::string galleryItemInstallNSPXML = R"xml(
    <brls:Box
        width="100%"
        height="100%"
        axis="column"
        grow="1"
        wireframe="false"
        justifyContent="center"
        alignItems="center">

        <brls:Image
                maxWidth="90%"
                maxHeight="80%"
                image="@res/img/hint_app.png"
                id="gallery/image"/>
        <brls:Label
                focusable="true"
                positionType="absolute"
                positionBottom="12%"
                id="gallery/label"
                text="@i18n/main/hints/hint_nsp"
                fontSize="24"/>
        <brls:Label
                positionType="absolute"
                positionBottom="4%"
                id="gallery/label"
                text="@i18n/main/hints/hint4"
                fontSize="24"/>
    </brls:Box>
)xml";

class GalleryItemInstallNSP : public GalleryItem {
public:
    GalleryItemInstallNSP() {
        this->inflateFromXMLString(galleryItemInstallNSPXML);

        button->registerClickAction([](...) -> bool {
            auto dialog = new brls::Dialog("main/hints/hint_confirm"_i18n);
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->addButton("hints/ok"_i18n, []() {
#ifdef BUILTIN_NSP
                brls::Application::blockInputs();

                mini::InstallSD("romfs:/forwarder.nsp");
                unsigned long long AppTitleID = mini::GetTitleID();
                appletRequestLaunchApplication(AppTitleID, NULL);
#endif
            });
            dialog->open();
            return true;
        });

        button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
    }

private:
    BRLS_BIND(brls::Label, button, "gallery/label");
};

HintActivity::HintActivity() {
    brls::Logger::debug("HintActivityActivity: create");
    GA("open_hint")
}

void HintActivity::onContentAvailable() {
    brls::Logger::debug("HintActivityActivity: onContentAvailable");

#ifdef BUILTIN_NSP
    gallery->setData({
        {"img/hint_game_1.png", "main/hints/hint1"_i18n},
        {"img/hint_game_2.png", "main/hints/hint2"_i18n},
        {"img/hint_hbmenu.png", "main/hints/hint3"_i18n},
    });
    gallery->addCustomView(new GalleryItemInstallNSP());
#else
    gallery->setData({
        {"img/hint_game_1.png", "main/hints/hint1"_i18n},
        {"img/hint_game_2.png", "main/hints/hint2"_i18n},
        {"img/hint_hbmenu.png", "main/hints/hint3"_i18n},
        {"img/hint_app.png", "main/hints/hint4"_i18n},
    });
#endif
}

HintActivity::~HintActivity() {
    brls::Logger::debug("HintActivityActivity: delete");
}