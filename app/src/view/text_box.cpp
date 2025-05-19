#include <borealis/core/application.hpp>
#include <yoga/YGNode.h>
#include "view/text_box.hpp"

static YGSize textBoxMeasureFunc(
    YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    auto* textBox = (TextBox*)YGNodeGetContext(node);
    auto fullText = textBox->getFullText();

    YGSize size = {.width = width, .height = height};
    if (heightMode == YGMeasureMode::YGMeasureModeExactly) return size;
    if (fullText.empty() || std::isnan(width)) return size;

    size.height = textBox->cutText(width);
    textBox->setParsedDone(true);
    return size;
}

TextBox::TextBox() {
    this->setAutoAnimate(false);

    this->registerFloatXMLAttribute("maxRows", [this](float value) { this->maxRows = (size_t)value; });

    YGNodeSetMeasureFunc(this->ygNode, textBoxMeasureFunc);
}

TextBox::~TextBox() = default;

brls::View* TextBox::create() { return new TextBox(); }

void TextBox::onLayout() {
    float width = this->getWidth();
    if (std::isnan(width) || width == 0) return;
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
    float lineh = 0;
    // Setup nvg state for the measurements
    nvgFontSize(vg, this->fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontFaceId(vg, this->font);
    nvgTextLineHeight(vg, this->lineHeight);
    nvgTextMetrics(vg, nullptr, nullptr, &lineh);

    float requiredHeight = this->fontSize;

    std::vector<NVGtextRow> rows(this->maxRows);
    const char* stringStart = this->fullText.c_str();
    int nrows = nvgTextBreakLines(vg, stringStart, nullptr, width, rows.data(), rows.size());
    if (nrows > 0) {
        this->cuttedText = this->fullText.substr(0, rows[nrows - 1].end - rows[0].start);
        requiredHeight += nrows * this->lineHeight * lineh;
    }
    return requiredHeight;
}