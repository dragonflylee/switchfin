#include <borealis/core/application.hpp>
#include "view/text_box.hpp"

TextBox::TextBox() {
    this->setAutoAnimate(false);

    this->registerFloatXMLAttribute("maxRows", [this](float value) { this->maxRows = (size_t)value; });
}

TextBox::~TextBox() = default;

brls::View* TextBox::create() { return new TextBox(); }

void TextBox::onLayout() {
    float width = this->getWidth();
    if (isnan(width) || width == 0) return;
    if (this->fullText.empty()) return;
    if (!this->parsedDone) this->cutText(width);
}

void TextBox::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    if (width == 0) return;

    enum NVGalign horizAlign = this->getNVGHorizontalAlign();
    enum NVGalign vertAlign = this->getNVGVerticalAlign();

    nvgFontSize(vg, this->fontSize);
    nvgTextAlign(vg, horizAlign | vertAlign);
    nvgFontFaceId(vg, this->font);
    nvgFontQuality(vg, this->fontQuality);
    nvgTextLineHeight(vg, this->lineHeight);
    nvgFillColor(vg, a(this->textColor));

    nvgTextAlign(vg, horizAlign | NVG_ALIGN_TOP);
    nvgTextBox(vg, x, y, width, this->cuttedText.c_str(), nullptr);
}

void TextBox::setText(const std::string& text) {
    this->fullText = text;
    this->setParsedDone(false);
    this->invalidate();
}

float TextBox::cutText(float width) {
    NVGcontext* vg = brls::Application::getNVGContext();
    // Setup nvg state for the measurements
    nvgFontSize(vg, this->getFontSize());
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontFaceId(vg, this->getFont());
    nvgTextLineHeight(vg, this->getLineHeight());

    float boxBounds[4];
    nvgTextBoxBounds(vg, 0, 0, width, fullText.c_str(), nullptr, boxBounds);
    float requiredHeight = boxBounds[3] - boxBounds[1];

    std::vector<NVGtextRow> rows(this->maxRows);
    const char* stringStart = this->fullText.c_str();
    int nrows = nvgTextBreakLines(vg, stringStart, nullptr, width, rows.data(), rows.size());
    if (nrows > 0) {
        this->cuttedText = this->fullText.substr(0, rows[nrows - 1].end - rows[0].start);
    }
    return requiredHeight;
}