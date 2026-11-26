#include "view/media_filter.hpp"

using namespace brls::literals;

MediaFilter::MediaFilter(const FilterState& state) : state(state) {
    this->inflateFromXMLRes("xml/view/media_filter.xml");
    brls::Logger::debug("MediaFilter: create");

    auto actionClose = [this](...){
        if (this->closed) return true;
        this->closed = true;
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
        return true;
    };

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, actionClose);
    this->cancel->registerClickAction(actionClose);
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
        this->state.sort, [this](int selected) { this->state.sort = selected; });

    this->sortOrder->init("main/media/order"_i18n,
        {
            "main/media/ascending"_i18n,
            "main/media/descending"_i18n,
        },
        this->state.order, [this](int selected) { this->state.order = selected; });

    this->filterPlayed->init(
        "main/media/played"_i18n, this->state.played, [this](bool value) { this->state.played = value; });
    this->filterUnplayed->init(
        "main/media/unplayed"_i18n, this->state.unplayed, [this](bool value) { this->state.unplayed = value; });
    this->filterFavorite->init(
        "main/media/favorite"_i18n, this->state.favorite, [this](bool value) { this->state.favorite = value; });
}

MediaFilter::~MediaFilter() { brls::Logger::debug("MediaFilter: delete"); }