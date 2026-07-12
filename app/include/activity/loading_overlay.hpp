/*
    Translucent loading overlay: a dimmed scrim drawn OVER the current screen
    (the pairing screen stays visible underneath) with a centered spinner +
    "Connecting…" card. Used by the Plex pairing flow (PlexAdd) while probing
    server connections, which can take several seconds — see thcolin/pleNx#1.

    Unlike LoadingActivity (the full-screen splash used at startup), this never
    hides what the user was looking at; it just signals "work in progress".
    After a few seconds without resolving, an extra reassurance line fades in.
*/

#pragma once

#include <borealis.hpp>

class LoadingOverlay : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/loading_overlay.xml");

    LoadingOverlay();

    ~LoadingOverlay() override;

    // Translucent so borealis keeps drawing the activity underneath rather than
    // stopping at this one (application.cpp:772-784): the screen behind shows
    // through the backdrop, turning a full-screen activity into an overlay.
    bool isTranslucent() override { return true; }

    void onContentAvailable() override;

private:
    // One-shot timer: reveal the "taking longer than usual" hint if we are
    // still here after this delay (the happy path resolves in well under a sec).
    brls::Timer slowTimer;

    BRLS_BIND(brls::Label, hint, "loading/hint");
};
