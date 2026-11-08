//
// Created by fang on 2022/8/21.
//

#include "activity/hint_activity.hpp"
#include "view/gallery_view.hpp"
#include "api/analytics.hpp"
#include "utils/forwarder.hpp"

#ifdef __SWITCH__
#include <switch.h>
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
#ifdef __SWITCH__
                brls::Application::blockInputs();

                brls::async([]() {
                    // 安装到 SD 卡，图标显示在主屏幕
                    u64 title_id = 0;
                    const auto error = forwarder::install(NcmStorageId_SdCard, title_id);

                    brls::sync([error, title_id]() {
                        brls::Application::unblockInputs();
                        if (error.empty()) {
                            // 安装成功：直接启动已安装的 forwarder
                            appletRequestLaunchApplication(title_id, nullptr);
                        } else {
                            auto errDialog = new brls::Dialog(error);
                            errDialog->addButton("hints/ok"_i18n, []() {});
                            errDialog->open();
                        }
                    });
                });
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
    GA("tutorial_begin")
}

void HintActivity::onContentAvailable() {
    brls::Logger::debug("HintActivityActivity: onContentAvailable");

#ifdef __SWITCH__
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
    GA("tutorial_complete")
}