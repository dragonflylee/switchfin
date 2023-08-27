#include "view/h_recycling.hpp"

class HRecyclerContentBox : public brls::Box {
public:
    HRecyclerContentBox(HRecyclerFrame* recycler) : Box(brls::Axis::ROW), recycler(recycler) {}

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override {
        return this->recycler->getNextCellFocus(direction, currentView);
    }

private:
    HRecyclerFrame* recycler;
};

brls::View* HRecyclerFrame::getNextCellFocus(brls::FocusDirection direction, brls::View* currentView) {
    void* parentUserData = currentView->getParentUserData();

    // Return nullptr immediately if focus direction mismatches the box axis (clang-format refuses to split it in multiple lines...)
    if ((this->contentBox->getAxis() == brls::Axis::ROW && direction != brls::FocusDirection::LEFT &&
            direction != brls::FocusDirection::RIGHT) ||
        (this->contentBox->getAxis() == brls::Axis::COLUMN && direction != brls::FocusDirection::UP &&
            direction != brls::FocusDirection::DOWN)) {
        View* next = getParentNavigationDecision(this, nullptr, direction);
        if (!next && hasParent()) next = getParent()->getNextFocus(direction, this);
        return next;
    }

    // Traverse the children
    size_t offset = 1;  // which way we are going in the children list
    if ((this->contentBox->getAxis() == brls::Axis::ROW && direction == brls::FocusDirection::LEFT) ||
        (this->contentBox->getAxis() == brls::Axis::COLUMN && direction == brls::FocusDirection::UP)) {
        offset = -1;
    }

    size_t currentFocusIndex = *((size_t*)parentUserData) + offset;
    View* currentFocus = nullptr;

    while (!currentFocus && currentFocusIndex >= 0 && currentFocusIndex < this->dataSource->getItemCount()) {
        for (auto it : this->contentBox->getChildren()) {
            if (*((size_t*)it->getParentUserData()) == currentFocusIndex) {
                currentFocus = it->getDefaultFocus();
                break;
            }
        }
        currentFocusIndex += offset;
    }

    currentFocus = getParentNavigationDecision(this, currentFocus, direction);
    if (!currentFocus && hasParent()) currentFocus = getParent()->getNextFocus(direction, this);
    return currentFocus;
}

void HRecyclerFrame::onChildFocusLost(View* directChild, View* focusedView) {
    HScrollingFrame::onChildFocusLost(directChild, focusedView);
    Box::onChildFocusLost(directChild, focusedView);
}

HRecyclerFrame::HRecyclerFrame() {
    brls::Logger::debug("View HRecyclerFrame: create");

    this->setFocusable(false);
    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Create content box
    this->contentBox = new HRecyclerContentBox(this);
    this->setContentView(this->contentBox);

    this->registerFloatXMLAttribute("itemWidth", [this](float value) {
        this->estimatedRowWidth = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("itemSpace", [this](float value) {
        this->estimatedRowSpace = value;
        this->reloadData();
    });
}

HRecyclerFrame::~HRecyclerFrame() {
    brls::Logger::debug("View HRecyclerFrame: delete");

    if (this->dataSource) delete dataSource;

    for (auto it : queueMap) {
        for (auto item : *it.second) delete item;
        delete it.second;
    }
}

void HRecyclerFrame::setDataSource(RecyclingGridDataSource* source) {
    if (this->dataSource) delete this->dataSource;

    this->dataSource = source;
    if (layouted) reloadData();
}

void HRecyclerFrame::reloadData() {
    if (!layouted) return;

    auto children = this->contentBox->getChildren();
    for (auto const& child : children) {
        queueReusableCell((RecyclingGridItem*)child);
        this->contentBox->removeView(child, false);
    }

    visibleMin = UINT_MAX;
    visibleMax = 0;

    renderedFrame = brls::Rect();
    renderedFrame.size.height = getHeight();

    setContentOffsetX(0, false);

    if (this->dataSource) {
        contentBox->setWidth(
            (estimatedRowWidth + estimatedRowSpace) * dataSource->getItemCount() + paddingLeft + paddingRight);
        // 填充足够多的cell到屏幕上
        brls::Rect frame = getLocalFrame();
        for (size_t row = 0; row < dataSource->getItemCount(); row++) {
            addCellAt(row, true);
            if (renderedFrame.getMaxX() > frame.getMaxX()) break;
        }
        this->selectRowAt(this->defaultCellFocus, false);
    }
}

void HRecyclerFrame::notifyDataChanged() {
    // todo: 目前仅能处理data在原本的基础上增加的情况，需要考虑data减少或更换时的情况
    if (!layouted) return;

    if (this->dataSource) {
        this->contentBox->setWidth(
            (estimatedRowWidth + estimatedRowSpace) * dataSource->getItemCount() + paddingLeft + paddingRight);
    }
}

void HRecyclerFrame::selectRowAt(size_t index, bool animated) {
    this->setContentOffsetX(getWidthByCellIndex(index), animated);
    this->cellsRecyclingLoop();

    for (View* view : contentBox->getChildren()) {
        if (*((size_t*)view->getParentUserData()) == index) {
            contentBox->setLastFocusedView(view);
            break;
        }
    }
}

float HRecyclerFrame::getWidthByCellIndex(size_t index, size_t start) {
    if (index <= start) return 0;
    return (estimatedRowWidth + estimatedRowSpace) * (index - start);
}

void HRecyclerFrame::cellsRecyclingLoop() {
    if (!dataSource) return;
    brls::Rect visibleFrame = getVisibleFrame();

    // 左侧元素自动销毁
    while (true) {
        RecyclingGridItem* minCell = nullptr;
        for (auto it : contentBox->getChildren())
            if (*((size_t*)it->getParentUserData()) == visibleMin) minCell = (RecyclingGridItem*)it;

        if (!minCell ||
            minCell->getDetachedPosition().x + estimatedRowWidth + estimatedRowSpace >= visibleFrame.getMinX())
            break;

        float cellWidth = estimatedRowWidth;

        renderedFrame.origin.x += cellWidth + estimatedRowSpace;
        renderedFrame.size.width -= cellWidth + estimatedRowSpace;

        queueReusableCell(minCell);
        this->contentBox->removeView(minCell, false);

        brls::Logger::verbose("HRecyclerFrame Cell #{} - destroyed", visibleMin);

        visibleMin++;
    }

    // 右侧元素自动销毁
    while (true) {
        RecyclingGridItem* maxCell = nullptr;
        for (auto it : contentBox->getChildren())
            if (*((size_t*)it->getParentUserData()) == visibleMax) maxCell = (RecyclingGridItem*)it;

        if (!maxCell || maxCell->getDetachedPosition().x <= visibleFrame.getMaxX()) break;

        float cellWidth = estimatedRowWidth;

        renderedFrame.size.width -= cellWidth + estimatedRowSpace;

        queueReusableCell(maxCell);
        this->contentBox->removeView(maxCell, false);

        brls::Logger::verbose("HRecyclerFrame Cell #{} - destroyed", visibleMax);

        visibleMax--;
    }

    // 左侧元素自动添加
    while (visibleMin - 1 < dataSource->getItemCount()) {
        if (renderedFrame.getMinX() < visibleFrame.getMinX() - paddingLeft) break;
        addCellAt(visibleMin - 1, false);
    }

    // 右侧元素自动添加
    while (visibleMax + 1 < dataSource->getItemCount()) {
        if (renderedFrame.getMaxX() > visibleFrame.getMaxX() - paddingRight) {
            requestNextPage = false;  // 允许加载下一页
            break;
        }
        brls::Logger::debug("HRecyclerFrame Cell #{} - added right", visibleMax + 1);
        addCellAt(visibleMax + 1, true);
    }

    if (this->visibleMax + 1 >= dataSource->getItemCount() && dataSource->getItemCount() > 0) {
        // 只有当 requestNextPage 为false时，才可以请求下一页，避免多次重复请求
        if (!this->requestNextPage && this->nextPageCallback) {
            brls::Logger::debug("HRecyclerFrame request next page");
            requestNextPage = true;
            this->nextPageCallback();
        }
    }
}

void HRecyclerFrame::addCellAt(size_t index, int downSide) {
    //获取到一个填充好数据的cell
    RecyclingGridItem* cell = dataSource->cellForRow(this, index);

    float cellWidth = estimatedRowWidth;

    cell->setHeight(renderedFrame.getHeight() - paddingTop - paddingBottom);
    cell->setWidth(cellWidth - estimatedRowSpace);

    cell->setDetachedPositionX(downSide ? renderedFrame.getMaxX() : renderedFrame.getMinX() + paddingLeft - cellWidth);
    cell->setDetachedPositionY(paddingTop);
    cell->setIndex(index);

    this->contentBox->getChildren().insert(this->contentBox->getChildren().end(), cell);

    // Allocate and set parent userdata
    size_t* userdata = (size_t*)malloc(sizeof(size_t));
    *userdata = index;
    cell->setParent(this->contentBox, userdata);

    // Layout and events
    this->contentBox->invalidate();
    cell->View::willAppear();

    if (index < visibleMin) visibleMin = index;

    if (index > visibleMax) visibleMax = index;

    if (!downSide) renderedFrame.origin.x -= cellWidth + estimatedRowSpace;
    renderedFrame.size.width += cellWidth + estimatedRowSpace;

    brls::Logger::verbose("HRecyclerFrame Cell #{} - added", index);
}

void HRecyclerFrame::onLayout() {
    HScrollingFrame::onLayout();
    this->contentBox->setHeight(this->getHeight());
    if (checkHeight()) {
        brls::Logger::debug("HRecyclerFrame::onLayout reloadData()");
        layouted = true;
        reloadData();
    }
}

void HRecyclerFrame::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    this->cellsRecyclingLoop();
    HScrollingFrame::draw(vg, x, y, width, height, style, ctx);
}

bool HRecyclerFrame::checkHeight() {
    float height = getHeight();
    if (oldHeight == -1) {
        oldHeight = height;
    }
    if ((int)oldHeight != (int)height && height != 0) {
        oldHeight = height;
        return true;
    }
    oldHeight = height;
    return false;
}

void HRecyclerFrame::setPadding(float padding) { this->setPadding(padding, padding, padding, padding); }

void HRecyclerFrame::setPadding(float top, float right, float bottom, float left) {
    paddingTop = top;
    paddingRight = right;
    paddingBottom = bottom;
    paddingLeft = left;

    this->reloadData();
}

void HRecyclerFrame::setPaddingTop(float top) {
    paddingTop = top;
    this->reloadData();
}

void HRecyclerFrame::setPaddingRight(float right) {
    paddingRight = right;
    this->reloadData();
}

void HRecyclerFrame::setPaddingBottom(float bottom) {
    paddingBottom = bottom;
    this->reloadData();
}

void HRecyclerFrame::setPaddingLeft(float left) {
    paddingLeft = left;
    this->reloadData();
}

void HRecyclerFrame::onNextPage(const std::function<void()>& callback) { this->nextPageCallback = callback; }

brls::View* HRecyclerFrame::create() { return new HRecyclerFrame(); }
