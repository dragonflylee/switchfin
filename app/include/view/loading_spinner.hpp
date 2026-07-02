/*
    GMCA — centered loading spinner overlay.

    For views that fill their content ASYNCHRONOUSLY into a plain brls::Box
    (Home, Suggestions…): the box stays empty while the backend request is in
    flight, leaving a blank screen with no feedback. Add one LoadingSpinner over
    the view and toggle it around the request.

    RecyclingGrid-based views (library grids, search, playlists, watchlist, hub
    pages) already render skeleton cells while loading, so they do NOT need this.
*/

#pragma once

#include <borealis.hpp>

class LoadingSpinner : public brls::Box {
public:
    LoadingSpinner() {
        // full-size, non-focusable overlay centered over the host view
        this->setPositionType(brls::PositionType::ABSOLUTE);
        this->setPositionTop(0);
        this->setPositionLeft(0);
        this->setWidthPercentage(100);
        this->setHeightPercentage(100);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setFocusable(false);

        this->spinner = new brls::ProgressSpinner();
        this->spinner->setWidth(60);
        this->spinner->setHeight(60);
        this->addView(this->spinner);

        this->setVisibility(brls::Visibility::GONE);
    }

    /// Show + spin while loading; hide (GONE, out of layout) when content is ready.
    void setSpinning(bool on) {
        this->setVisibility(on ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->spinner->animate(on);
    }

private:
    brls::ProgressSpinner* spinner = nullptr;
};
