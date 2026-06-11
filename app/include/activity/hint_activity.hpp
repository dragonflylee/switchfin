/*
    "Launch as full application" screen: pushed at boot in applet mode
    (insufficient memory for video playback) and reachable in application
    mode via Settings -> "How to install desktop icon" or the first-launch
    prompt (main.cpp). Guides towards the embedded NSP tile (BUILTIN_NSP)
    or title takeover.
*/

#pragma once

#include <borealis.hpp>

class HintActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/hint.xml");

    HintActivity();

    void onContentAvailable() override;

    ~HintActivity();

private:
    BRLS_BIND(brls::Label, labelText, "hint/text");
    BRLS_BIND(brls::Box, boxInstall, "hint/install");
    BRLS_BIND(brls::Button, btnInstall, "hint/install/button");
};
