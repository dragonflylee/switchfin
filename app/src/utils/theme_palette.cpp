/*
    GMCA — per-backend theme palettes (see theme_palette.hpp).

    Values are { accent, accentGlowTop, onAccentText, listValue }, dark then
    light. Hex are the verified brand colors. The light variant flips the accent
    toward a darker shade so it stays legible on a light background; the dark
    variant is the primary "dark theater" experience.
*/

#include "utils/theme_palette.hpp"

namespace plenx {

// ---- DEFAULT (pleNx neutral) ------------------------------------------------
// Off-white accent #E8E8E8 reads as "white" on the near-black #0D0E11 chrome
// without the glare of pure #FFFFFF as a large fill. Light variant inverts to a
// dark-grey accent so it stays visible on a light background.
static const ThemeColors kDefault = {
    /* dark  */ {{0xE8, 0xE8, 0xE8}, {0xFF, 0xFF, 0xFF}, {0x10, 0x12, 0x16}, {0xC9, 0xC9, 0xC9}},
    /* light */ {{0x3A, 0x3A, 0x3A}, {0x5A, 0x5A, 0x5A}, {0xFF, 0xFF, 0xFF}, {0x77, 0x77, 0x77}},
};

// ---- PLEX -------------------------------------------------------------------
// Current brand gold "Corn" #EBAF00 (Aug-2024 logo redesign), #F7C600 as the
// brighter glow top-stop, verified Plex black #191919 on the gold.
static const ThemeColors kPlex = {
    /* dark  */ {{0xEB, 0xAF, 0x00}, {0xF7, 0xC6, 0x00}, {0x19, 0x19, 0x19}, {0xC9, 0xA8, 0x6A}},
    /* light */ {{0xCC, 0x7C, 0x19}, {0xEB, 0xAF, 0x00}, {0x19, 0x19, 0x19}, {0xA6, 0x75, 0x1D}},
};

// ---- JELLYFIN ---------------------------------------------------------------
// Official accent #00A4DC; the logo-gradient purple #AA5CC3 tops the glow as a
// nod to the brand's blue->purple gradient.
static const ThemeColors kJellyfin = {
    /* dark  */ {{0x00, 0xA4, 0xDC}, {0xAA, 0x5C, 0xC3}, {0x00, 0x10, 0x18}, {0x5F, 0xC3, 0xE6}},
    /* light */ {{0x00, 0x83, 0xB0}, {0x00, 0xA4, 0xDC}, {0x00, 0x10, 0x18}, {0x1B, 0x7E, 0x9E}},
};

// ---- EMBY -------------------------------------------------------------------
// Official Emby green #52B54B (verified in Emby's own dark skin); #6FCF68 is a
// lighter synthesized tint for the glow top-stop (Emby publishes no 2nd color).
static const ThemeColors kEmby = {
    /* dark  */ {{0x52, 0xB5, 0x4B}, {0x6F, 0xCF, 0x68}, {0x0A, 0x14, 0x09}, {0x8F, 0xD0, 0x89}},
    /* light */ {{0x3E, 0x84, 0x37}, {0x52, 0xB5, 0x4B}, {0x0A, 0x14, 0x09}, {0x35, 0x7A, 0x2E}},
};

// ---- STREMIO ----------------------------------------------------------------
// Vivid logo-gradient purple #7B5BF5 (the recognizable accent, not the muted
// system token #664181); #A970CD purple-pink tops the glow. White on-accent
// text — the only theme dark enough to need it instead of a dark label.
static const ThemeColors kStremio = {
    /* dark  */ {{0x7B, 0x5B, 0xF5}, {0xA9, 0x70, 0xCD}, {0xFF, 0xFF, 0xFF}, {0xA8, 0x8E, 0xF7}},
    /* light */ {{0x5E, 0x45, 0xC9}, {0x7B, 0x5B, 0xF5}, {0xFF, 0xFF, 0xFF}, {0x6B, 0x53, 0xC9}},
};

const ThemeColors& defaultPalette() { return kDefault; }

const ThemeColors& backendPalette(media::BackendType type) {
    switch (type) {
        case media::BackendType::Plex: return kPlex;
        case media::BackendType::Jellyfin: return kJellyfin;
        case media::BackendType::Emby: return kEmby;
        case media::BackendType::Stremio: return kStremio;
    }
    return kPlex;
}

}  // namespace plenx
