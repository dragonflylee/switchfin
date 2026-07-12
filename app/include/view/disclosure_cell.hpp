/*
    GMCA — a settings cell that opens a sub-screen.
*/

#pragma once

#include <borealis.hpp>

#include "view/svg_image.hpp"

/// A settings cell that drills into a sub-screen: a title on the left and a
/// right-pointing chevron on the right (the "disclosure" affordance).
///
/// It extends Borealis' DetailCell — keeping its focus / highlight / click
/// behavior — but renders the chevron as an SVG instead of the Material glyph
/// (U+E5CC) in DetailCell's value Label. A Label aligns text with
/// NVG_ALIGN_MIDDLE, which centers on the *text* font's metrics (Inter); an icon
/// glyph pulled from the Material fallback font has a different baseline and so
/// lands a few pixels above center. An SVG in the Box (alignItems="center") is
/// centered geometrically.
class DisclosureCell : public brls::DetailCell {
  public:
    DisclosureCell();

    static brls::View* create();

  private:
    SVGImage* chevron = nullptr;
};
