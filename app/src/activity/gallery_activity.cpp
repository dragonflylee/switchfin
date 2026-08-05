//
// Created by fang on 2023/4/26.
//

#include <borealis/views/image.hpp>

#include "activity/gallery_activity.hpp"
#include "view/gallery_view.hpp"
#include "utils/image.hpp"

const std::string ImageGalleryItemXML = R"xml(
    <brls:Box
        width="100%"
        height="100%"
        axis="row"
        grow="1"
        justifyContent="center"
        alignItems="center">

        <brls:Image
                margin="40"
                id="gallery/image"/>
    </brls:Box>
)xml";

class NetImageGalleryItem : public GalleryItem {
public:
    explicit NetImageGalleryItem(const std::string& url, const HTTP::Header& headers = {}) {
        this->inflateFromXMLString(ImageGalleryItemXML);
        if (url.find("://") == std::string::npos) {
            this->image->setImageFromFile(url);
        } else {
            this->image->setImageFromRes("img/video-card-bg.png");
            Image::with(this->image, url, headers);
        }
    }

    ~NetImageGalleryItem() override { Image::cancel(this->image); }

    BRLS_BIND(brls::Image, image, "gallery/image");
};

GalleryActivity::GalleryActivity(const std::string& url, const HTTP::Header& headers) {
    brls::Logger::debug("GalleryActivity: create");

    this->view = new NetImageGalleryItem(url, headers);
}

void GalleryActivity::onContentAvailable() {
    brls::Logger::debug("GalleryActivity: onContentAvailable");
    gallery->setIndicatorPosition(0.97);
    gallery->addCustomView(this->view);
}

GalleryActivity::~GalleryActivity() { brls::Logger::debug("GalleryActivity: delete"); }

bool GalleryActivity::isTranslucent() { return true; }
