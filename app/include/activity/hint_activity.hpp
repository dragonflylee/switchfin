/*
    Écran « lancement en application complète » : poussé au boot en mode
    applet (mémoire insuffisante pour la lecture vidéo) et accessible en
    mode application via Réglages → « How to install desktop icon » ou la
    proposition du premier lancement (main.cpp). Guide vers la tuile NSP
    intégrée (BUILTIN_NSP) ou le title takeover.
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
