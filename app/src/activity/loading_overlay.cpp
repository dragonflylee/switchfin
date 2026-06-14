#include "activity/loading_overlay.hpp"

using namespace brls::literals;

// Probing a reachable server resolves in well under a second; if we are still
// waiting past this, surface a reassuring line so the wait does not read as a
// freeze (thcolin/pleNx#1).
static const brls::Time SLOW_HINT_DELAY = 5000;

LoadingOverlay::LoadingOverlay() { brls::Logger::debug("LoadingOverlay: create"); }

LoadingOverlay::~LoadingOverlay() {
    // Stop before our views tear down so the end callback can't touch them.
    this->slowTimer.stop();
    brls::Logger::debug("LoadingOverlay: delete");
}

void LoadingOverlay::onContentAvailable() {
    this->slowTimer.setEndCallback([this](bool finished) {
        // finished == false means the timer was stopped (overlay dismissed or
        // destroyed): the work completed in time, leave the hint hidden.
        if (!finished) return;
        this->hint->setVisibility(brls::Visibility::VISIBLE);
    });
    this->slowTimer.start(SLOW_HINT_DELAY);
}
