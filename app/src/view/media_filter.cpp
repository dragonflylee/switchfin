#include "view/media_filter.hpp"

using namespace brls::literals;

MediaFilter::MediaFilter() {
    this->inflateFromXMLRes("xml/view/media_filter.xml");
    brls::Logger::debug("MediaFilter: create");

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
            "main/media/date_add"_i18n,
            "main/media/date_played"_i18n,
            "main/media/premiere_date"_i18n,
            "main/media/play_count"_i18n,
            "main/media/rating"_i18n,
            "main/media/random"_i18n,
        },
        selectedSort, [](int selected) { selectedSort = selected; });

    this->sortOrder->init("main/media/order"_i18n,
        {
            "main/media/ascending"_i18n,
            "main/media/descending"_i18n,
        },
        selectedOrder, [](int selected) { selectedOrder = selected; });

    // Plex cannot filter "watched only" on /all -> only the "unwatched"
    // option (unwatched=1) is kept (cf. PLEX_MIGRATION.md §2.5)
    this->filterUnplayed->init(
        "main/media/unplayed"_i18n, selectedUnplayed, [](bool value) { selectedUnplayed = value; });
}

MediaFilter::~MediaFilter() { brls::Logger::debug("MediaFilter: delete"); }