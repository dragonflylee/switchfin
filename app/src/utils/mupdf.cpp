#include "utils/misc.hpp"

#ifdef USE_MUPDF

#include <borealis.hpp>
#include <mupdf/fitz.h>
#include <mupdf/ucdn.h>

namespace {

fz_font* newFont(fz_context* ctx, const std::string& name) {
    int fontId = brls::Application::getFont(name);
    if (fontId < 0) return nullptr;

    fz_font* f = nullptr;
    int fontSize = 0;
    auto fontData = nvgGetFontData(brls::Application::getNVGContext(), fontId, &fontSize);
    fz_try(ctx) f = fz_new_font_from_memory(ctx, name.c_str(), fontData, fontSize, 0, 0);
    fz_catch(ctx) return nullptr;
    return f;
}

fz_font* loadSystemFont(fz_context* ctx, const char* name, int bold, int italic, int) {
    return newFont(ctx, brls::FONT_REGULAR);
}

fz_font* loadCjkFont(fz_context* ctx, const char*, int, int) {
    fz_font* font = newFont(ctx, brls::FONT_CHINESE_SIMPLIFIED);
    if (font) font->flags.cjk = 1;
    return font;
}

const std::string& fallbackFontName(int script, int language) {
    switch (language) {
    case FZ_LANG_zh:
    case FZ_LANG_zh_Hans:
        return brls::FONT_CHINESE_SIMPLIFIED;
    case FZ_LANG_ja:  // borealis has no Japanese font; the CJK font covers the glyphs
    case FZ_LANG_zh_Hant:
        return brls::FONT_CHINESE_TRADITIONAL;
    case FZ_LANG_ko:
        return brls::FONT_KOREAN_REGULAR;
    default:  // language unset: fall back to the text script
        if (script == UCDN_SCRIPT_HANGUL) return brls::FONT_KOREAN_REGULAR;
        if (script == UCDN_SCRIPT_HAN || script == UCDN_SCRIPT_HIRAGANA || script == UCDN_SCRIPT_KATAKANA)
            return brls::FONT_CHINESE_SIMPLIFIED;
        return brls::FONT_REGULAR;
    }
}

fz_font* loadFallbackFont(fz_context* ctx, int script, int language, int, int bold, int italic) {
    const std::string& name = fallbackFontName(script, language);
    fz_font* font = newFont(ctx, name);
    if (font && name != brls::FONT_REGULAR) font->flags.cjk = 1;
    return font;
}

}  // namespace

namespace mupdf {

void install_font(fz_context* ctx) {
    fz_install_load_system_font_funcs(ctx, loadSystemFont, loadCjkFont, loadFallbackFont);
}

}  // namespace mupdf

#endif
