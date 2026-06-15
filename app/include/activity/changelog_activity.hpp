/*
    Copyright 2026 thcolin
*/

#pragma once

#include <borealis.hpp>

/// Full-screen, scrollable view of the embedded CHANGELOG.md (whole history),
/// reached from Settings ▸ Others ▸ Changelog. The release popup shows only
/// the new version's notes (version.cpp); this screen is the full log.
class Changelog : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/changelog.xml");

    void onContentAvailable() override;

private:
    BRLS_BIND(brls::Label, label, "changelog/text");
};
