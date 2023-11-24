#pragma once

#include <view/recycling_grid.hpp>

class HRecyclerContentBox;

class HRecyclerFrame : public brls::HScrollingFrame, public RecyclingView {
public:
    HRecyclerFrame();
    ~HRecyclerFrame();

    View* getNextCellFocus(brls::FocusDirection direction, View* currentView);
    void onChildFocusLost(View* directChild, View* focusedView) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;
    void onLayout() override;
    void setPadding(float padding) override;
    void setPadding(float top, float right, float bottom, float left) override;
    void setPaddingTop(float top) override;
    void setPaddingRight(float right) override;
    void setPaddingBottom(float bottom) override;
    void setPaddingLeft(float left) override;

    void setDataSource(RecyclingGridDataSource* source);

    // 重新加载数据
    void reloadData();
    void notifyDataChanged();
    void selectRowAt(size_t index, bool animated);
    float getWidthByCellIndex(size_t index, size_t start = 0);

    /// 导航到页面尾部时触发回调函数
    void onNextPage(const std::function<void()>& callback = nullptr);

    static brls::View* create();

     /// 元素间距
    float estimatedRowSpace = 10;

    /// 默认宽度 (元素实际宽度 = 默认宽度 - 元素间隔)
    float estimatedRowWidth = 240;

private:
    bool layouted = false;
    float oldHeight = -1;

    uint32_t visibleMin, visibleMax;
    size_t defaultCellFocus = 0;

    float paddingTop = 0;
    float paddingRight = 0;
    float paddingBottom = 0;
    float paddingLeft = 0;

    bool requestNextPage = false;
    std::function<void()> nextPageCallback = nullptr;

    HRecyclerContentBox* contentBox = nullptr;
    brls::Rect renderedFrame;

    // 回收列表项
    void cellsRecyclingLoop();
    void addCellAt(size_t index, int downSide);
    bool checkHeight();
};