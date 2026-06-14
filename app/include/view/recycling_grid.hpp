//
// Created by fang on 2022/6/15.
//

#pragma once

#include <borealis.hpp>

class RecyclingView;
class SVGImage;

class RecyclingGridItem : public brls::Box {
public:
    RecyclingGridItem();
    ~RecyclingGridItem() override;

    /*
     * Cell's position inside recycler frame
     */
    size_t getIndex() const;

    /*
     * DO NOT USE! FOR INTERNAL USAGE ONLY!
     */
    void setIndex(size_t value);

    /*
     * A string used to identify a cell that is reusable.
     */
    std::string reuseIdentifier;

    /*
     * Prepares a reusable cell for reuse by the recycler frame's data source.
     */
    virtual void prepareForReuse() {}

    /*
     * 表单项回收
     */
    virtual void cacheForReuse() {}

private:
    size_t index;
};

class RecyclingGridDataSource {
public:
    virtual ~RecyclingGridDataSource() = default;

    /*
     * Tells the data source to return the number of items in a recycler frame.
     */
    virtual size_t getItemCount() { return 0; }

    /*
     * Asks the data source for a cell to insert in a particular location of the recycler frame.
     */
    virtual RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) { return nullptr; }

    /*
     * Asks the data source for the height to use for a row in a specified location.
     * Return -1 to use autoscaling.
     */
    virtual float heightForRow(brls::View* recycler, size_t index) { return -1; }

    /*
     * Tells the data source a row is selected.
     */
    virtual void onItemSelected(brls::Box* recycler, size_t index) {}

    /// Targeted "watched" toggle: update the cached item's played state in
    /// place and return its row index (-1 if not found / unsupported). Lets a
    /// single card refresh without re-fetching the whole view.
    virtual int setPlayed(const std::string& itemId, bool played) { return -1; }

    virtual void clearData() = 0;
};

class RecyclingGridContentBox;

class RecyclingView {
public:
    virtual ~RecyclingView() = default;

    virtual brls::View* getNextCellFocus(brls::FocusDirection direction, brls::View* currentView) = 0;

    virtual void setDataSource(RecyclingGridDataSource* source) = 0;

    void registerCell(std::string identifier, std::function<RecyclingGridItem*()> allocation);

    RecyclingGridItem* dequeueReusableCell(std::string identifier);

    RecyclingGridDataSource* getDataSource() const;

    /// Cell currently bound to `index`, or nullptr if it is off-screen / not
    /// materialized. Generic over the vertical grid and the horizontal rows
    /// (both keep their cells in `contentBox`).
    RecyclingGridItem* getGridItemByIndex(size_t index);

    void showSkeleton(unsigned int num = 12);
protected:
    // 回收列表项
    void queueReusableCell(RecyclingGridItem* cell);

    void removeCell(brls::View* view);

    RecyclingGridDataSource* dataSource = nullptr;
    RecyclingGridContentBox* contentBox = nullptr;

    std::map<std::string, std::vector<RecyclingGridItem*>*> queueMap;
    std::map<std::string, std::function<RecyclingGridItem*(void)>> allocationMap;
};

class RecyclingGrid : public brls::ScrollingFrame, public RecyclingView {
public:
    RecyclingGrid();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

    void setDefaultCellFocus(size_t index);

    size_t getDefaultCellFocus() const;

    void setDataSource(RecyclingGridDataSource* source) override;

    // 重新加载数据
    void reloadData();

    void notifyDataChanged();

    std::vector<RecyclingGridItem*>& getGridItems();

    void clearData();

    /// NON-focusable header that scrolls with the content (playlist title,
    /// meta...): child detached from the contentBox, laid above the cells —
    /// all offset by `height`. Call before the first layout;
    /// `view` must be a plain Box (NOT a RecyclingGridItem), non-focusable,
    /// destroyed with the grid.
    void setHeaderView(brls::View* view, float height);

    /// Empty state: icon + title + explanatory subtitle (centered).
    /// Without title: generic label; icon = res path ("icon/ico-….svg").
    void setEmpty(std::string title = "", std::string subtitle = "", std::string icon = "");

    void setError(std::string error = "");

    void selectRowAt(size_t index, bool animated);

    // 计算从start元素的顶点到index (不包含index) 元素顶点的距离
    float getHeightByCellIndex(size_t index, size_t start = 0);

    View* getNextCellFocus(brls::FocusDirection direction, View* currentView) override;

    void forceRequestNextPage();

    void onLayout() override;

    /// 当前数据总数量
    size_t getItemCount();

    /// 当前数据总行数
    size_t getRowCount();

    /// 导航到页面尾部时触发回调函数
    void onNextPage(const std::function<void()>& callback = nullptr);

    void setPadding(float padding) override;
    void setPadding(float top, float right, float bottom, float left) override;
    void setPaddingTop(float top) override;
    void setPaddingRight(float right) override;
    void setPaddingBottom(float bottom) override;
    void setPaddingLeft(float left) override;

    brls::View* getDefaultFocus() override;

    ~RecyclingGrid() override;

    static View* create();

    /// 元素间距
    float estimatedRowSpace = 20;

    /// 默认行高(元素实际高度 = 默认行高 - 元素间隔)
    float estimatedRowHeight = 240;

    /// 列数
    int spanCount = 4;

    /// 预取的行数
    int preFetchLine = 1;

    /// 瀑布流模式，每一项高度不固定（仅在spanCount为1时可用）
    bool isFlowMode = false;

    /// Row height derived from the real cell width at layout time:
    /// itemHeight = cellWidth x itemImageRatio + itemExtraHeight.
    /// 0 = disabled (fixed XML itemHeight). Guarantees the poster ratio
    /// whatever the container width (UI_REDESIGN.md §3.3).
    float itemImageRatio = 0;
    float itemExtraHeight = 0;

private:
    bool layouted = false;
    float oldWidth = -1;

    bool requestNextPage = false;
    // true表示正在请求下一页，此时不会再次触发下一页请求
    // 数据为空时不请求下一页，因为有些时候首页和下一页请求的内容或方式不同
    // 当列表元素有变动时（添加或修改数据源，会重置为false，这是将允许请求下一页）

    uint32_t visibleMin, visibleMax;
    size_t defaultCellFocus = 0;

    float paddingTop = 0;
    float paddingRight = 0;
    float paddingBottom = 0;
    float paddingLeft = 0;

    std::function<void()> nextPageCallback = nullptr;

    SVGImage* hintImage;
    brls::Label* hintLabel;
    brls::Label* hintSub;
    brls::Rect renderedFrame;
    std::vector<float> cellHeightCache;

    /// scrolled header (cf. setHeaderView); headerHeight adds to
    /// paddingTop in all position computations via contentTop()
    brls::View* headerView = nullptr;
    float headerHeight = 0;

    float contentTop() const { return paddingTop + headerHeight; }

    // 检查宽度是否有变化
    bool checkWidth();

    void itemsRecyclingLoop();

    /**
     * 在指定位置添加一个列表项
     * 内部更新 renderedFrame 的值，假设有一个每一项都绘制的超长列表，renderedFrame 的 y 表示当前截取绘制的顶部坐标，height 表示当前绘制的高度
     * 当添加一个列表项时，renderedFrame 的 height 增加一项的高度（注意，只在每行的第一个列表项添加时才更新列表项的高度）
     * @param index 指定的位置
     * @param downSide 是向下添加还是向上添加，当向上添加时 将 renderedFrame 的 y 减去当前列表项的高度。（y 的值只在向上添加或移除时候改变）
     */
    void addCellAt(size_t index, bool downSide);
};

class RecyclingGridContentBox : public brls::Box {
public:
    RecyclingGridContentBox(RecyclingView* recycler);
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

private:
    RecyclingView* recycler;
};

class SkeletonCell : public RecyclingGridItem {
public:
    SkeletonCell();

    static RecyclingGridItem* create();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;

private:
    NVGcolor background = brls::Application::getTheme()["color/grey_3"];
};