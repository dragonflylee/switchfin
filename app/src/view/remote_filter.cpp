#include "view/remote_filter.hpp"

using namespace brls::literals;

RemoteFilter::RemoteFilter() {
    this->inflateFromXMLRes("xml/view/remote_filter.xml");
    brls::Logger::debug("RemoteFilter: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](...) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
        return true;
    });

    this->cancel->registerClickAction([this](...) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    this->sortBy->init("main/media/sort_by"_i18n,
        {
            "main/media/name"_i18n,
            "main/remote/date"_i18n,
            "main/remote/size"_i18n,
        },
        selectedSort, [](int selected) { selectedSort = selected; });

    this->sortOrder->init("main/media/order"_i18n,
        {
            "main/media/ascending"_i18n,
            "main/media/descending"_i18n,
        },
        selectedOrder, [](int selected) { selectedOrder = selected; });
}

RemoteFilter::~RemoteFilter() { brls::Logger::debug("RemoteFilter: delete"); }
