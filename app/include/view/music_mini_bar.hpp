/*
    GMCA — persistent "now playing" mini-bar (SPEC.md T11 — issue #11).
    Mounted under the main frame (re-created with MainActivity on each
    server-switch); visible only while the AudioPlayer has a queue. Shows the
    current track and opens the full Now Playing (click, or the global Y action
    in MainActivity), so the user can keep browsing while listening.
*/

#pragma once

#include <borealis.hpp>
#include <api/media/types.hpp>
#include <utils/event.hpp>

class SVGImage;

class MusicMiniBar : public brls::Box {
public:
    MusicMiniBar();
    ~MusicMiniBar() override;

private:
    void refresh();  // show/hide + cover/title/artist from the controller

    BRLS_BIND(brls::Image, cover, "mini/cover");
    BRLS_BIND(brls::Label, labelTitle, "mini/title");
    BRLS_BIND(brls::Label, labelArtist, "mini/artist");
    BRLS_BIND(SVGImage, toggleIcon, "mini/toggle");

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription trackSubscribeID;
};
