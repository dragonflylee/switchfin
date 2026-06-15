#pragma once

#include <borealis.hpp>
#include <fmt/format.h>

#include <optional>
#include <string>

#include "view/svg_image.hpp"

/// Plex exposes a rating "source" per item — ratingImage (critic) and
/// audienceRatingImage (audience) — as opaque URIs (imdb://…,
/// rottentomatoes://image.rating.ripe, …). This maps them to the official
/// icon + a display value, mirroring the plezy reference (rating_utils.dart).
namespace rating {

/// Reference icon height (pt). The width is derived from the icon aspect:
/// the RT/IMDb/TMDb glyphs are NOT square, and SVGImage renders at the view
/// size, so the pill must size the view to the icon aspect to avoid a
/// squashed/letterboxed glyph (cf. SVGImage::updateBitmap).
inline constexpr float ICON_HEIGHT = 15.0f;

struct RatingInfo {
    std::string icon;   // resources-relative SVG path
    std::string value;  // formatted display value (e.g. "85%" or "8.5")
    float aspect;       // source width / height
};

/// nullopt for an empty/unknown URI — the caller then falls back to a star.
inline std::optional<RatingInfo> parseRatingImage(const std::string& uri, double value) {
    if (uri.empty() || value <= 0.0) return std::nullopt;

    static const std::string rt = "rottentomatoes://image.rating.";
    if (uri.rfind(rt, 0) == 0) {
        const std::string suffix = uri.substr(rt.size());
        const std::string pct = fmt::format("{:.0f}%", value * 10);
        // aspect = viewBox width / height of each asset (resources/icon/rating)
        if (suffix == "ripe") return RatingInfo{"icon/rating/rt_fresh.svg", pct, 0.982f};
        if (suffix == "rotten") return RatingInfo{"icon/rating/rt_rotten.svg", pct, 1.036f};
        if (suffix == "upright") return RatingInfo{"icon/rating/rt_upright.svg", pct, 0.759f};
        if (suffix == "spilled") return RatingInfo{"icon/rating/rt_spilled.svg", pct, 1.322f};
        return std::nullopt;
    }
    if (uri.rfind("imdb://", 0) == 0)  // 575 x 289.83
        return RatingInfo{"icon/rating/imdb.svg", fmt::format("{:.1f}", value), 1.984f};
    if (uri.rfind("themoviedb://", 0) == 0)  // 185.04 x 133.4
        return RatingInfo{"icon/rating/tmdb.svg", fmt::format("{:.0f}%", value * 10), 1.387f};

    return std::nullopt;
}

/// Fills one rating pill (icon + value) and toggles its enclosing Box (the
/// shared parent of icon + label). A recognised URI shows the official icon
/// sized to its aspect; a value with no/unknown URI (e.g. the watchlist
/// provider omits ratingImage) shows the generic gold star; value <= 0 hides
/// the pill. Visibility is set last so the first SVG render happens while the
/// view is still collapsed (getWidth()==0 → lunasvg renders at the SVG's own
/// aspect), keeping the glyph undistorted.
inline void applyPill(SVGImage* icon, brls::Label* label, const std::string& uri, double value) {
    brls::View* box = label->getParent();
    if (value <= 0.0) {
        box->setVisibility(brls::Visibility::GONE);
        return;
    }
    if (auto info = parseRatingImage(uri, value)) {
        icon->setWidth(ICON_HEIGHT * info->aspect);
        icon->setHeight(ICON_HEIGHT);
        icon->setImageFromSVGRes(info->icon);
        label->setText(info->value);
    } else {
        icon->setWidth(ICON_HEIGHT);
        icon->setHeight(ICON_HEIGHT);
        icon->setImageFromSVGRes("icon/ico-star.svg");
        label->setText(fmt::format("{:.1f}", value));
    }
    box->setVisibility(brls::Visibility::VISIBLE);
}

}  // namespace rating
