//
// Created by fang on 2022/5/30.
//

#pragma once

#include <borealis/views/label.hpp>

class TextBox : public brls::Label {
public:
    TextBox();
    ~TextBox() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;
    void onLayout() override;
    void setText(const std::string& text) override;

    static brls::View* create();

    float cutText(float width);

    void setParsedDone(bool value) { this->parsedDone = value; }

private:
    // 最大的行数
    size_t maxRows = 5;

    bool parsedDone = false;
    std::string cuttedText;
};