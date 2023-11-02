#include "view/media_filter.hpp"
#include "tab/media_collection.hpp"

using namespace brls::literals;

MediaFilter::MediaFilter(MediaCollection *view) {
    this->inflateFromXMLRes("xml/view/media_filter.xml");
    brls::Logger::debug("MediaFilter: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [view](...) {
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [view]() {
            view->startIndex = 0;
            view->doRequest();
        });
        return true;
    });

    this->sortBy->init("main/media/sort_by"_i18n,
        {
            "main/media/name"_i18n,
            "main/media/date_add"_i18n,
            "main/media/date_played"_i18n,
            "main/media/premiere_date"_i18n,
            "main/media/play_count"_i18n,
            "main/media/rating"_i18n,
            "main/media/random"_i18n,
        },
        MediaCollection::selectedSort, [this](int selected) { MediaCollection::selectedSort = selected; });

    this->sortOrder->init("main/media/order"_i18n,
        {
            "main/media/ascending"_i18n,
            "main/media/descending"_i18n,
        },
        MediaCollection::selectedOrder, [this](int selected) { MediaCollection::selectedOrder = selected; });

    this->filterPlayed->init("main/media/played"_i18n, true, [](bool value) {});
    this->filterUnplayed->init("main/media/unplayed"_i18n, true, [](bool value) {});

}

MediaFilter::~MediaFilter() { brls::Logger::debug("MediaFilter: delete"); }
