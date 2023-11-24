#include <view/presenter.hpp>
#include <view/video_view.hpp>

Presenter::Presenter() {
    auto mpvce = MPVCore::instance().getCustomEvent();
    this->customEventSubscribeID = mpvce->subscribe([this](const std::string& event, void* data) {
        if (event == VideoView::VIDEO_CLOSE) {
            this->doRequest();
        }
    });
}

Presenter::~Presenter() {
    auto mpvce = MPVCore::instance().getCustomEvent();
    mpvce->unsubscribe(this->customEventSubscribeID);
}