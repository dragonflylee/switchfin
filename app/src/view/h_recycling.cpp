#include "view/h_recycling.hpp"

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

#ifdef PS5_NATIVE_GPU
    while (!currentFocus && currentFocusIndex < this->dataSource->getItemCount()) {
#else
    while (!currentFocus && currentFocusIndex >= 0 && currentFocusIndex < this->dataSource->getItemCount()) {
#endif
        for (auto it : this->contentBox->getChildren()) {
            if (*((size_t*)it->getParentUserData()) == currentFocusIndex) {
                currentFocus = it->getDefaultFocus();
                break;
            }
        }
        currentFocusIndex += offset;
    }

    if (!currentFocus && hasParent()) currentFocus = getParent()->getNextFocus(direction, this);
    return currentFocus;
}

HRecyclerFrame::HRecyclerFrame() {
    brls::Logger::debug("View HRecyclerFrame: create");

    this->setFocusable(false);
    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    // Create content box
    this->contentBox = new RecyclingGridContentBox(this);
    this->setContentView(this->contentBox);

    this->registerFloatXMLAttribute("itemWidth", [this](float value) {
        this->estimatedRowWidth = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("itemSpace", [this](float value) {
        this->estimatedRowSpace = value;
        this->reloadData();
    });

    this->registerCell("Skeleton", []() { return SkeletonCell::create(); });
    this->showSkeleton();
}

HRecyclerFrame::~HRecyclerFrame() {
    brls::Logger::debug("View HRecyclerFrame: delete");

    if (this->dataSource) delete dataSource;

    for (auto it : queueMap) {
        for (auto item : *it.second) delete item;
        delete it.second;
    }
}

brls::View* HRecyclerFrame::getDefaultFocus() {
    if (this->dataSource && this->dataSource->getItemCount() > 0) return HScrollingFrame::getDefaultFocus();
    return nullptr;
}

void HRecyclerFrame::setDataSource(RecyclingGridDataSource* source) {
#ifdef PS5_NATIVE_GPU
    // Read selection when the response is applied, after any intervening D-pad input.
    size_t selected = selectedIndex();
    const auto key = dataSource && selected < dataSource->getItemCount()
        ? dataSource->getItemKey(selected) : std::string();
    if (source && !key.empty()) {
        for (size_t i = 0; i < source->getItemCount(); ++i) {
            if (source->getItemKey(i) == key) {
                selected = i;
                break;
            }
        }
    }
#endif
    if (this->dataSource) delete this->dataSource;

    // 允许自动加载下一页
    this->requestNextPage = false;
    this->dataSource = source;
#ifdef PS5_NATIVE_GPU
    if (layouted) reloadData(selected);
}

brls::View* HRecyclerFrame::focusedCell() const {
    for (auto* view = brls::Application::getCurrentFocus(); view; view = view->getParent()) {
        if (view->getParent() == contentBox) {
            for (auto* child : contentBox->getChildren())
                if (child == view) return child;
            break;
        }
    }
    return nullptr;
}

size_t HRecyclerFrame::selectedIndex() const {
    auto* selected = focusedCell();
    if (!selected) selected = contentBox->getLastFocusedView();
    // Recycled cells can remain in Borealis' focus history. Only trust live children.
    for (auto* child : contentBox->getChildren()) {
        if (child == selected && child->getParentUserData())
            return *static_cast<size_t*>(child->getParentUserData());
    }
    return defaultCellFocus;
#else
    if (layouted) reloadData();
#endif
}

void HRecyclerFrame::clearData() {
    if (dataSource) {
        dataSource->clearData();
        this->reloadData();
    }
}

void HRecyclerFrame::reloadData() {
#ifdef PS5_NATIVE_GPU
    reloadData(selectedIndex());
}

void HRecyclerFrame::reloadData(size_t selected) {
#endif
    if (!layouted) return;
#ifdef PS5_NATIVE_GPU

    const bool restoreFocus = focusedCell() != nullptr;
    reloading = true;
    contentBox->setLastFocusedView(nullptr);
#endif

    auto children = this->contentBox->getChildren();
    for (auto const& child : children) {
        queueReusableCell((RecyclingGridItem*)child);
        this->removeCell(child);
    }

    visibleMin = UINT_MAX;
    visibleMax = 0;

    renderedFrame = brls::Rect();
    renderedFrame.size.height = getHeight();

    setContentOffsetX(0, false);

#ifdef PS5_NATIVE_GPU
    const size_t count = dataSource ? dataSource->getItemCount() : 0;
    defaultCellFocus = count ? std::min(selected, count - 1) : 0;
    if (count) {
#else
    if (this->dataSource) {
#endif
        contentBox->setWidth(
            (estimatedRowWidth + estimatedRowSpace) * dataSource->getItemCount() + paddingLeft + paddingRight);
#ifdef PS5_NATIVE_GPU
        // Seed at the selected item; fill only the new visible window.
        renderedFrame.origin.x = getWidthByCellIndex(defaultCellFocus);
        addCellAt(defaultCellFocus, true);
        this->selectRowAt(this->defaultCellFocus, false);
    } else {
        contentBox->setWidth(0);
    }
    reloading = false;

    if (restoreFocus) {
        if (count) {
            brls::Application::giveFocus(contentBox);
        } else {
            // giveFocus(nullptr) does not clear Borealis' current focus. Find a
            // surviving row or sidebar before leaving the old cell in the pool.
            for (auto* parent = getParent(); parent; parent = parent->getParent()) {
                if (auto* next = parent->getDefaultFocus()) {
                    brls::Application::giveFocus(next);
                    break;
                }
            }
#else
        // 填充足够多的cell到屏幕上
        brls::Rect frame = getLocalFrame();
        for (size_t row = 0; row < dataSource->getItemCount(); row++) {
            addCellAt(row, true);
            if (renderedFrame.getMaxX() > frame.getMaxX()) break;
#endif
        }
#ifdef PS5_NATIVE_GPU
#else
        this->selectRowAt(this->defaultCellFocus, false);
#endif
    }
}

void HRecyclerFrame::notifyDataChanged() {
    // todo: 目前仅能处理data在原本的基础上增加的情况，需要考虑data减少或更换时的情况
    if (!layouted) return;

    if (this->dataSource) {
        this->contentBox->setWidth(
            (estimatedRowWidth + estimatedRowSpace) * dataSource->getItemCount() + paddingLeft + paddingRight);
#ifdef PS5_NATIVE_GPU
        // Appending a page does not change the position of existing cards.
        // Keep the viewport still, including when this row is not focused.
#else
        this->setContentOffsetX(this->getContentOffsetX() + estimatedRowSpace, true);
#endif
    }
}

void HRecyclerFrame::selectRowAt(size_t index, bool animated) {
#ifdef PS5_NATIVE_GPU
    if (!dataSource || !dataSource->getItemCount()) return;
    index = std::min(index, dataSource->getItemCount() - 1);
    defaultCellFocus = index;
#endif
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
    float cellWidth = estimatedRowWidth + estimatedRowSpace;

    // 左侧元素自动销毁
    while (true) {
        RecyclingGridItem* minCell = nullptr;
        for (auto it : contentBox->getChildren())
            if (*((size_t*)it->getParentUserData()) == visibleMin) minCell = (RecyclingGridItem*)it;

        if (!minCell || minCell->getDetachedPosition().x + cellWidth >= visibleFrame.getMinX()) break;

        renderedFrame.origin.x += cellWidth;
        renderedFrame.size.width -= cellWidth;

        queueReusableCell(minCell);
        this->removeCell(minCell);

        brls::Logger::verbose("HRecyclerFrame Cell #{} - destroyed", visibleMin);

        visibleMin++;
    }

    // 右侧元素自动销毁
    while (true) {
        RecyclingGridItem* maxCell = nullptr;
        for (auto it : contentBox->getChildren())
            if (*((size_t*)it->getParentUserData()) == visibleMax) maxCell = (RecyclingGridItem*)it;

        if (!maxCell || maxCell->getDetachedPosition().x - cellWidth <= visibleFrame.getMaxX()) break;

        renderedFrame.size.width -= cellWidth;

        queueReusableCell(maxCell);
        this->removeCell(maxCell);

        brls::Logger::verbose("HRecyclerFrame Cell #{} - destroyed", visibleMax);

        visibleMax--;
    }

    // 左侧元素自动添加
    while (visibleMin - 1 < dataSource->getItemCount()) {
#ifdef PS5_NATIVE_GPU
        // Test the candidate at the same position/boundary used by removal.
        // Otherwise a stationary row can remove and re-add it every frame.
        const float candidateX = (visibleMin - 1) * cellWidth + paddingLeft;
        if (candidateX + cellWidth < visibleFrame.getMinX()) break;
#else
        if (renderedFrame.getMinX() + cellWidth < visibleFrame.getMinX() - paddingLeft) break;
#endif
        addCellAt(visibleMin - 1, false);
    }

    // 右侧元素自动添加
    while (visibleMax + 1 < dataSource->getItemCount()) {
#ifdef PS5_NATIVE_GPU
        const float candidateX = (visibleMax + 1) * cellWidth + paddingLeft;
        if (candidateX - cellWidth > visibleFrame.getMaxX()) {
#else
        if (renderedFrame.getMaxX() - cellWidth > visibleFrame.getMaxX() - paddingRight) {
#endif
            requestNextPage = false;  // 允许加载下一页
            break;
        }
        brls::Logger::debug("HRecyclerFrame Cell #{} - added right", visibleMax + 1);
        addCellAt(visibleMax + 1, true);
    }

    if (this->visibleMax + 1 >= dataSource->getItemCount() && dataSource->getItemCount() > 0) {
        // 只有当 requestNextPage 为false时，才可以请求下一页，避免多次重复请求
#ifdef PS5_NATIVE_GPU
        if (!reloading && !this->requestNextPage && this->nextPageCallback) {
#else
        if (!this->requestNextPage && this->nextPageCallback) {
#endif
            brls::Logger::debug("HRecyclerFrame request next page");
            requestNextPage = true;
            this->nextPageCallback();
        }
    }
}

void HRecyclerFrame::addCellAt(size_t index, int downSide) {
    //获取到一个填充好数据的cell
    RecyclingGridItem* cell = dataSource->cellForRow(this, index);

    float cellWidth = estimatedRowWidth + estimatedRowSpace;

    cell->setHeight(renderedFrame.getHeight() - paddingTop - paddingBottom);
    cell->setWidth(estimatedRowWidth);

    cell->setDetachedPositionX(index * cellWidth + paddingLeft);
    cell->setDetachedPositionY(renderedFrame.getMinY() + paddingTop);
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

    if (!downSide) renderedFrame.origin.x -= cellWidth;
    renderedFrame.size.width += cellWidth;

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
