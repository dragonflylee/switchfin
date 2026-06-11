//
// Created by fang on 2022/6/15.
//

#include <utility>
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"

/// RecyclingGridItem

RecyclingGridItem::RecyclingGridItem() {
    this->setFocusable(true);
    this->registerClickAction([this](View*) {
        brls::Box* view = this->getParent()->getParent();
        RecyclingView* recycler = dynamic_cast<RecyclingView*>(view);
        if (recycler) recycler->getDataSource()->onItemSelected(view, index);
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

size_t RecyclingGridItem::getIndex() const { return this->index; }

void RecyclingGridItem::setIndex(size_t value) { this->index = value; }

RecyclingGridItem::~RecyclingGridItem() = default;

/// Skeleton cell

SkeletonCell::SkeletonCell() { this->setFocusable(false); }

RecyclingGridItem* SkeletonCell::create() { return new SkeletonCell(); }

void SkeletonCell::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    brls::Time curTime = brls::getCPUTimeUsec() / 1000;
    float p = (curTime % 1000) * 1.0 / 1000;
    p = std::fabs(0.5 - p) + 0.25;

    NVGcolor end = background;
    end.a = p;

    NVGpaint paint = nvgLinearGradient(vg, x, y, x + width, y + height, a(background), a(end));
    auto bar = [&](float bx, float by, float bw, float bh, float radius) {
        nvgBeginPath(vg);
        nvgFillPaint(vg, paint);
        nvgRoundedRect(vg, bx, by, bw, bh, radius);
        nvgFill(vg);
    };

    // small cells (lists, chips): a single block
    if (height < 120) {
        bar(x, y, width, height, 6);
        return;
    }

    // structure of a media card: poster + title bar + subtitle
    // (same metrics as video_card.xml: label area = 55)
    float labels = 55;
    bar(x, y, width, height - labels, 10);
    float top = y + height - labels + 12;
    bar(x + width * 0.15f, top, width * 0.70f, 14, 7);
    bar(x + width * 0.275f, top + 22, width * 0.45f, 10, 5);
}

/// Skeleton DataSource

class DataSourceSkeleton : public RecyclingGridDataSource {
public:
    DataSourceSkeleton(unsigned int n) : num(n) {}

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) {
        SkeletonCell* item = dynamic_cast<SkeletonCell*>(recycler->dequeueReusableCell("Skeleton"));
        RecyclingGrid* view = dynamic_cast<RecyclingGrid*>(recycler);
        if (view) item->setHeight(view->estimatedRowHeight);
        return item;
    }

    size_t getItemCount() { return this->num; }

    void clearData() { this->num = 0; }

private:
    unsigned int num;
};

/// RecyclingView

void RecyclingView::registerCell(std::string identifier, std::function<RecyclingGridItem*()> allocation) {
    queueMap.insert(std::make_pair(identifier, new std::vector<RecyclingGridItem*>()));
    allocationMap.insert(std::make_pair(identifier, allocation));
}

RecyclingGridItem* RecyclingView::dequeueReusableCell(std::string identifier) {
    brls::Logger::verbose("RecyclingView::dequeueReusableCell: {}", identifier);
    RecyclingGridItem* cell = nullptr;
    auto it = queueMap.find(identifier);

    if (it != queueMap.end()) {
        std::vector<RecyclingGridItem*>* vector = it->second;
        if (!vector->empty()) {
            cell = vector->back();
            vector->pop_back();
        } else {
            cell = allocationMap.at(identifier)();
            cell->reuseIdentifier = identifier;
            cell->detach();
        }
    }

    if (cell) cell->prepareForReuse();

    return cell;
}

void RecyclingView::queueReusableCell(RecyclingGridItem* cell) {
    queueMap.at(cell->reuseIdentifier)->push_back(cell);
    cell->cacheForReuse();
}

void RecyclingView::removeCell(brls::View* view) {
    if (!view) return;
    // Find the index of the view
    size_t index;
    bool found = false;
    auto& children = this->contentBox->getChildren();
    for (size_t i = 0; i < children.size(); i++) {
        brls::View* child = children[i];
        if (child == view) {
            found = true;
            index = i;
            break;
        }
    }
    if (!found) return;
    // Remove it
    children.erase(children.begin() + index);
    view->willDisappear(true);
    this->contentBox->invalidate();
}


RecyclingGridDataSource* RecyclingView::getDataSource() const { return this->dataSource; }

void RecyclingView::showSkeleton(unsigned int num) { this->setDataSource(new DataSourceSkeleton(num)); }

/// RecyclingGrid

RecyclingGrid::RecyclingGrid() {
    brls::Logger::debug("View RecyclingGrid: create");

    // Empty/error states: icon + title + explanatory subtitle
    this->hintImage = new SVGImage();
    this->hintImage->detach();
    this->hintImage->setDimensions(56, 56);
    this->hintImage->setImageFromSVGRes("icon/ico-list.svg");
    this->hintLabel = new brls::Label();
    this->hintLabel->detach();
    this->hintLabel->setFontSize(17);
    this->hintLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->hintSub = new brls::Label();
    this->hintSub->detach();
    this->hintSub->setFontSize(14);
    this->hintSub->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    this->hintSub->setHorizontalAlign(brls::HorizontalAlign::CENTER);

    this->setFocusable(false);

    this->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    // Create content box
    this->contentBox = new RecyclingGridContentBox(this);
    this->setContentView(this->contentBox);

    this->registerFloatXMLAttribute("itemHeight", [this](float value) {
        this->estimatedRowHeight = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("spanCount", [this](float value) {
        if (value != 1) {
            isFlowMode = false;
        }
        this->spanCount = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("itemSpace", [this](float value) {
        this->estimatedRowSpace = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("preFetchLine", [this](float value) {
        this->preFetchLine = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("itemImageRatio", [this](float value) {
        this->itemImageRatio = value;
        this->reloadData();
    });

    this->registerFloatXMLAttribute("itemExtraHeight", [this](float value) {
        this->itemExtraHeight = value;
        this->reloadData();
    });

    this->registerBoolXMLAttribute("flowMode", [this](bool value) {
        this->spanCount = 1;
        this->isFlowMode = value;
        this->reloadData();
    });

    this->registerCell("Skeleton", []() { return SkeletonCell::create(); });
    this->showSkeleton();
}

RecyclingGrid::~RecyclingGrid() {
    brls::Logger::debug("View RecyclingGrid: delete");
    if (this->hintImage) this->hintImage->freeView();
    this->hintImage = nullptr;
    if (this->hintLabel) this->hintLabel->freeView();
    this->hintLabel = nullptr;
    if (this->hintSub) this->hintSub->freeView();
    this->hintSub = nullptr;
    delete this->dataSource;
    for (const auto& it : queueMap) {
        for (auto item : *it.second) {
            item->setParent(nullptr);
            if (item->isPtrLocked())
                item->freeView();
            else
                delete item;
        }
        delete it.second;
    }
}

void RecyclingGrid::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    // 触摸或鼠标滑动时会导致屏幕元素位置变更
    // 简单地在draw函数中调用itemsRecyclingLoop 实现动态的增删元素
    // todo：只在滑动过程中调用 itemsRecyclingLoop 以节省静止时的计算消耗
    itemsRecyclingLoop();

    ScrollingFrame::draw(vg, x, y, width, height, style, ctx);

    if (!this->dataSource || this->dataSource->getItemCount() == 0) {
        if (!this->hintImage) return;
        // truly centered icon + title + subtitle block: detached labels
        // have no laid-out height, we impose a box on them
        float w1 = hintImage->getWidth(), w2 = hintLabel->getWidth(), w3 = hintSub->getWidth();
        float h1 = hintImage->getHeight();
        float gap = 18, h2 = hintLabel->getFontSize() * 1.5f;
        bool hasSub = !hintSub->getFullText().empty();
        float h3 = hasSub ? hintSub->getFontSize() * 1.5f + 6 : 0;
        float top = y + (height - (h1 + gap + h2 + h3)) / 2;
        this->hintImage->setAlpha(this->getAlpha());
        this->hintImage->draw(vg, x + (width - w1) / 2, top, w1, h1, style, ctx);
        this->hintLabel->setAlpha(this->getAlpha());
        this->hintLabel->draw(vg, x + (width - w2) / 2, top + h1 + gap, w2, h2, style, ctx);
        if (hasSub) {
            this->hintSub->setAlpha(this->getAlpha());
            this->hintSub->draw(vg, x + (width - w3) / 2, top + h1 + gap + h2 + 6, w3, h3 - 6, style, ctx);
        }
    }
}

void RecyclingGrid::addCellAt(size_t index, bool downSide) {
    RecyclingGridItem* cell;
    //获取到一个填充好数据的cell
    cell = dataSource->cellForRow(this, index);

    float cellHeight = estimatedRowHeight;
    float cellWidth = (renderedFrame.getWidth() - paddingLeft - paddingRight) / spanCount - cell->getMarginLeft() -
                      cell->getMarginRight();
    float cellX = renderedFrame.getMinX() + paddingLeft;

    if (isFlowMode) {
        // 必须在 getHeight 前设置宽度，否则会影响到cell自定义高度的判定
        cell->setWidth(cellWidth);
        if (cellHeightCache[index] == -1) {
            // 没有预定义cell的高度，使用cell默认的高度
            cellHeight = cell->getHeight();

            if (cellHeight > estimatedRowHeight) {
                cellHeight = estimatedRowHeight;
            }
            cellHeightCache[index] = cellHeight;
        } else {
            // dataSource 中指定了cell的高度，使用预定义的值
            cellHeight = cellHeightCache[index];
        }

        brls::Logger::verbose("Add cell at: y {} height {}", getHeightByCellIndex(index) + contentTop(), cellHeight);
    } else {
        cell->setWidth(cellWidth - estimatedRowSpace);
        cellX += (renderedFrame.getWidth() - paddingLeft - paddingRight) / spanCount * (index % spanCount);
    }

    cell->setHeight(cellHeight);
    cell->setDetachedPositionX(cellX);
    cell->setDetachedPositionY(getHeightByCellIndex(index) + contentTop());
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

    // 只有元素出现在首列时才需要考虑修改 renderedFrame
    if (index % spanCount == 0) {
        if (!downSide) renderedFrame.origin.y -= cellHeight + estimatedRowSpace;

        renderedFrame.size.height += cellHeight + estimatedRowSpace;
    }

    // 瀑布流模式需要不断修正高度
    if (isFlowMode)
        contentBox->setHeight(getHeightByCellIndex(this->dataSource->getItemCount()) + contentTop() + paddingBottom);

    brls::Logger::verbose("RecyclingGrid Cell #{} - added", index);
}

void RecyclingGrid::setDataSource(RecyclingGridDataSource* source) {
    if (this->dataSource) delete this->dataSource;

    // 允许自动加载下一页
    this->requestNextPage = false;
    this->dataSource = source;
    if (layouted) reloadData();
}

void RecyclingGrid::setHeaderView(brls::View* view, float height) {
    if (this->headerView) return;  // a single header, set once
    this->headerView = view;
    this->headerHeight = height;
    view->detach();
    view->setHeight(height);
    // child of the contentBox WITHOUT index userdata: every recycler loop
    // ignores it (the getParentUserData() == nullptr guard)
    this->contentBox->getChildren().push_back(view);
    view->setParent(this->contentBox);
    view->willAppear();
    if (layouted) reloadData();
}

void RecyclingGrid::reloadData() {
    if (!layouted) return;

    // guaranteed image ratio: the row height follows the cell width
    // (reloadData is the mandatory path of every geometry change)
    if (this->itemImageRatio > 0) {
        float width = getWidth();
        if (width != width) width = oldWidth;
        if (width > 0) {
            float cellWidth = (width - paddingLeft - paddingRight) / spanCount - estimatedRowSpace;
            this->estimatedRowHeight = cellWidth * itemImageRatio + itemExtraHeight;
        }
    }

    // 将所有节点从屏幕上移除放入重复利用的列表中
    auto& children = this->contentBox->getChildren();
    for (auto const& child : children) {
        if (child == this->headerView) continue;  // the header is not a cell
        queueReusableCell((RecyclingGridItem*)child);
        child->willDisappear(true);
    }
    children.clear();
    if (this->headerView) children.push_back(this->headerView);

    visibleMin = UINT_MAX;
    visibleMax = 0;

    renderedFrame = brls::Rect();
    renderedFrame.size.width = getWidth();
    if (renderedFrame.size.width != renderedFrame.size.width) {
        // 当列表在展示骨架屏后被隐藏，这时获取到 width 的值为 NAN
        // 使用历史宽度值避免后续计算错误
        renderedFrame.size.width = oldWidth;
    }

    setContentOffsetY(0, false);

    if (dataSource == nullptr) return;
    if (dataSource->getItemCount() <= 0) {
        contentBox->setHeight(contentTop() + paddingBottom);
        return;
    }
    size_t cellFocusIndex = this->defaultCellFocus;
    if (cellFocusIndex >= dataSource->getItemCount()) cellFocusIndex = dataSource->getItemCount() - 1;

    // 设置列表的高度（真实高度，非显示的高度）
    if (!isFlowMode || spanCount != 1) {
        // 设置了固定的高度
        contentBox->setHeight(
            (estimatedRowHeight + estimatedRowSpace) * (float)getRowCount() + contentTop() + paddingBottom);
        // 添加当前焦点 cell 所在行的第一项到屏幕，其余项通过 selectRowAt 内的 itemsRecyclingLoop 自动添加
        // 这里添加首项是因为添加首项时会变更 renderedFrame 的 height 值，包括 itemsRecyclingLoop 内的计算也都是以首项为基准进行的
        // 原则上这里的 addCellAt 任意添加一项即可（比如添加第零项），但最好能添加到 cellFocusIndex 附近，这有助于提升首屏性能
        size_t lineHeadIndex = cellFocusIndex / spanCount * spanCount;
        // 更新 renderedFrame 数据，设置y的值，伪装成已经移除了 lineHeadIndex 项之前的列表项
        // y 值表示当前列表渲染的顶部，低于 y 值高度的列表项不会被渲染
        renderedFrame.origin.y = getHeightByCellIndex(lineHeadIndex);

        // 添加 lineHeadIndex 项到需要渲染的列表项中，因为 lineHeadIndex 为一行的首项，在执行 addCellAt 之后会更新 renderedFrame 数据
        // renderedFrame 的 height 调整为第 lineHeadIndex 项的高度
        this->addCellAt(lineHeadIndex, true);
    } else {
        // 获取每个cell的高度并缓存起来
        cellHeightCache.clear();
        for (size_t section = 0; section < dataSource->getItemCount(); section++) {
            float height = dataSource->heightForRow(this, section);
            cellHeightCache.push_back(height);
        }
        contentBox->setHeight(getHeightByCellIndex(dataSource->getItemCount()) + contentTop() + paddingBottom);
        // 流式布局无法准确确定焦点cell的位置，因此暂时只添加第一项，在 itemsRecyclingLoop 中会逐渐添加到 cellFocusIndex，再按需删除
        // 当 cellFocusIndex 过于大时，会导致添加的元素过多，因此需要考虑优化
        this->addCellAt(0, true);
    }

    // 在前面的操作中，列表增加了一项，通过 selectRowAt 再精确地显示出具体选中项
    selectRowAt(cellFocusIndex, this->isFlowMode);
}

void RecyclingGrid::notifyDataChanged() {
    // todo: 目前仅能处理data在原本的基础上增加的情况，需要考虑data减少或更换时的情况
    if (!layouted) return;

    if (dataSource) {
        if (isFlowMode) {
            for (size_t i = cellHeightCache.size(); i < dataSource->getItemCount(); i++) {
                float height = dataSource->heightForRow(this, i);
                cellHeightCache.push_back(height);
            }
            contentBox->setHeight(getHeightByCellIndex(this->dataSource->getItemCount()) + contentTop() + paddingBottom);
        } else {
            contentBox->setHeight(
                (estimatedRowHeight + estimatedRowSpace) * this->getRowCount() + contentTop() + paddingBottom);
        }
    }
    // 数据增多后重新允许加载下一页
    requestNextPage = false;
}

RecyclingGridItem* RecyclingGrid::getGridItemByIndex(size_t index) {
    for (brls::View* i : contentBox->getChildren()) {
        RecyclingGridItem* v = dynamic_cast<RecyclingGridItem*>(i);
        if (!v) continue;
        if (v->getIndex() == index) return v;
    }
    // 当前索引数据没有绑定列表项
    return nullptr;
}

std::vector<RecyclingGridItem*>& RecyclingGrid::getGridItems() {
    return (std::vector<RecyclingGridItem*>&)contentBox->getChildren();
}

void RecyclingGrid::clearData() {
    if (dataSource) {
        dataSource->clearData();
        this->reloadData();
    }
}

void RecyclingGrid::setEmpty(std::string title, std::string subtitle, std::string icon) {
    this->hintImage->setImageFromSVGRes(icon.empty() ? "icon/ico-list.svg" : icon);
    this->hintLabel->setText(title.empty() ? brls::getStr("main/empty/title") : title);
    this->hintSub->setText(subtitle);
    this->clearData();
}

void RecyclingGrid::setError(std::string error) {
    this->hintImage->setImageFromSVGRes("icon/ico-cloud.svg");
    this->hintLabel->setText(brls::getStr("main/empty/error"));
    this->hintSub->setText(error);
    this->clearData();
}

void RecyclingGrid::setDefaultCellFocus(size_t index) { this->defaultCellFocus = index; }

size_t RecyclingGrid::getDefaultCellFocus() const { return this->defaultCellFocus; }

size_t RecyclingGrid::getItemCount() { return this->dataSource->getItemCount(); }

size_t RecyclingGrid::getRowCount() { return (this->dataSource->getItemCount() - 1) / this->spanCount + 1; }

void RecyclingGrid::onNextPage(const std::function<void()>& callback) { this->nextPageCallback = callback; }

void RecyclingGrid::itemsRecyclingLoop() {
    if (!dataSource) return;

    brls::Rect visibleFrame = getVisibleFrame();

    // 上方元素自动销毁
    while (true) {
        RecyclingGridItem* minCell = nullptr;
        for (auto it : contentBox->getChildren()) {
            if (!it->getParentUserData()) continue;  // scrolled header: not a cell
            if (*((size_t*)it->getParentUserData()) == visibleMin) minCell = (RecyclingGridItem*)it;
        }

        // 当第一个cell的顶部 与 组件顶部的距离大于 preFetchLine 行元素的距离时结束
        if (!minCell || (minCell->getDetachedPosition().y +
                                getHeightByCellIndex(visibleMin + (preFetchLine + 1) * spanCount, visibleMin) >=
                            visibleFrame.getMinY()))
            break;

        float cellHeight = estimatedRowHeight;
        if (isFlowMode) cellHeight = cellHeightCache[visibleMin];

        renderedFrame.origin.y += minCell->getIndex() % spanCount == 0 ? cellHeight + estimatedRowSpace : 0;
        renderedFrame.size.height -= minCell->getIndex() % spanCount == 0 ? cellHeight + estimatedRowSpace : 0;

        queueReusableCell(minCell);
        this->removeCell(minCell);

        brls::Logger::verbose("Cell #{} - destroyed", visibleMin);

        visibleMin++;
    }

    // 下方元素自动销毁
    while (true) {
        RecyclingGridItem* maxCell = nullptr;
        for (auto it : contentBox->getChildren()) {
            if (!it->getParentUserData()) continue;  // scrolled header: not a cell
            if (*((size_t*)it->getParentUserData()) == visibleMax) maxCell = (RecyclingGridItem*)it;
        }

        // 当最后一个cell的顶部 与 组件底部间的距离 小于 preFetchLine 行元素的距离时结束
        if (!maxCell || (maxCell->getDetachedPosition().y -
                                getHeightByCellIndex(visibleMax, visibleMax - preFetchLine * spanCount) <=
                            visibleFrame.getMaxY()))
            break;
        if (visibleMax == 0) {
            break;
        }

        float cellHeight = estimatedRowHeight;
        if (isFlowMode) cellHeight = cellHeightCache[visibleMax];

        renderedFrame.size.height -= maxCell->getIndex() % spanCount == 0 ? cellHeight + estimatedRowSpace : 0;

        queueReusableCell(maxCell);
        this->removeCell(maxCell);

        brls::Logger::verbose("Cell #{} - destroyed", visibleMax);

        visibleMax--;
    }

    // 上方元素自动添加
    while (visibleMin - 1 < dataSource->getItemCount()) {
        if ((visibleMin) % spanCount == 0)
            // 当 renderedFrame 顶部 与 组件顶部的距离小于 preFetchLine 行cell的距离时结束
            if (renderedFrame.getMinY() + getHeightByCellIndex(visibleMin + preFetchLine * spanCount, visibleMin) <
                visibleFrame.getMinY() - contentTop()) {
                break;
            }
        addCellAt(visibleMin - 1, false);
    }

    // 下方元素自动添加
    while (visibleMax + 1 < dataSource->getItemCount()) {
        // 当即将被添加的元素为新一行的开始时结束，否则填充满一整行
        if ((visibleMax + 1) % spanCount == 0)
            // 如果 renderedFrame 底部 与 组件底部 距离超过了preFetchLine 行cell的距离时结束
            if (renderedFrame.getMaxY() -
                    getHeightByCellIndex(visibleMax + 1, visibleMax + 1 - preFetchLine * spanCount) >
                visibleFrame.getMaxY() - paddingBottom) {
                requestNextPage = false;  // 允许加载下一页
                break;
            }
        addCellAt(visibleMax + 1, true);
    }

    if (visibleMax + 1 >= this->getItemCount()) {
        // 只有当 requestNextPage 为false时，才可以请求下一页，避免多次重复请求
        if (!requestNextPage && nextPageCallback) {
            // 有数据、不是骨架屏数据、数据不为空
            if (dataSource && !dynamic_cast<DataSourceSkeleton*>(dataSource) && dataSource->getItemCount() > 0) {
                brls::Logger::debug("RecyclingGrid request next page");
                requestNextPage = true;
                this->nextPageCallback();
            }
        }
    }
}

void RecyclingGrid::selectRowAt(size_t index, bool animated) {
    this->setContentOffsetY(getHeightByCellIndex(index), animated);
    this->itemsRecyclingLoop();

    for (View* view : contentBox->getChildren()) {
        if (!view->getParentUserData()) continue;  // scrolled header: not a cell
        if (*((size_t*)view->getParentUserData()) == index) {
            contentBox->setLastFocusedView(view);
            break;
        }
    }
}

float RecyclingGrid::getHeightByCellIndex(size_t index, size_t start) {
    if (index <= start) return 0;
    if (!isFlowMode) return (estimatedRowHeight + estimatedRowSpace) * (size_t)((index - start) / spanCount);

    if (cellHeightCache.size() == 0) {
        brls::Logger::error("cellHeightCache.size() cannot be zero in flow mode {} {}", start, index);
        return 0;
    }

    if (start < 0) start = 0;
    if (index > this->cellHeightCache.size()) index = this->cellHeightCache.size();

    float res = 0;
    for (size_t i = start; i < index && i < cellHeightCache.size(); i++) {
        if (cellHeightCache[i] != -1)
            res += cellHeightCache[i] + estimatedRowSpace;
        else
            res += estimatedRowHeight + estimatedRowSpace;
    }
    return res;
}

void RecyclingGrid::forceRequestNextPage() { this->requestNextPage = false; }

brls::View* RecyclingGrid::getNextCellFocus(brls::FocusDirection direction, brls::View* currentView) {
    void* parentUserData = currentView->getParentUserData();

    // Allow up and down when axis is ROW
    if ((this->contentBox->getAxis() == brls::Axis::ROW && direction != brls::FocusDirection::LEFT &&
            direction != brls::FocusDirection::RIGHT)) {
        int row_offset = spanCount;
        if (direction == brls::FocusDirection::UP) row_offset = -spanCount;
        View* row_currentFocus = nullptr;
        size_t row_currentFocusIndex = *((size_t*)parentUserData) + row_offset;

        if (row_currentFocusIndex >= this->dataSource->getItemCount()) {
            row_currentFocusIndex -= *((size_t*)parentUserData) % spanCount;
        }

        while (!row_currentFocus && row_currentFocusIndex >= 0 &&
               row_currentFocusIndex < this->dataSource->getItemCount()) {
            for (auto it : this->contentBox->getChildren()) {
                if (!it->getParentUserData()) continue;  // scrolled header: not a cell
                if (*((size_t*)it->getParentUserData()) == row_currentFocusIndex) {
                    row_currentFocus = it->getDefaultFocus();
                    break;
                }
            }
            row_currentFocusIndex += row_offset;
        }
        if (row_currentFocus) {
            // 按键(上或下)可以导航过去的情况
            itemsRecyclingLoop();

            return row_currentFocus;
        }
    }

    if (this->contentBox->getAxis() == brls::Axis::ROW) {
        int position = *((size_t*)parentUserData) % spanCount;
        if ((direction == brls::FocusDirection::LEFT && position == 0) ||
            (direction == brls::FocusDirection::RIGHT && position == (spanCount - 1))) {
            View* next = getParentNavigationDecision(this, nullptr, direction);
            if (!next && hasParent()) next = getParent()->getNextFocus(direction, this);
            return next;
        }
    }

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
            if (!it->getParentUserData()) continue;  // scrolled header: not a cell
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

void RecyclingGrid::onLayout() {
    ScrollingFrame::onLayout();
    auto rect = this->getFrame();
    float width = rect.getWidth();
    // check NAN
    if (width != width) return;

    if (!this->contentBox) return;
    this->contentBox->setWidth(width);
    if (this->headerView) {
        this->headerView->setWidth(width - paddingLeft - paddingRight);
        this->headerView->setDetachedPosition(paddingLeft, paddingTop);
    }
    if (checkWidth()) {
        brls::Logger::debug("RecyclingGrid::onLayout reloadData()");
        layouted = true;
        reloadData();
    }
}

bool RecyclingGrid::checkWidth() {
    float width = getWidth();
    if (oldWidth == -1) {
        oldWidth = width;
    }
    // 1px hysteresis: fractional widths oscillate across yoga roundings
    // (131.5 <-> 132 observed) — the truncated (int) comparison then
    // triggered a reloadData PER FRAME: permanently empty grid (cells
    // recycled in a loop), dead navigation, spammed pages.
    // oldWidth is NOT tracked outside reload: a real cumulative drift
    // > 1px re-triggers, a sub-pixel oscillation dampens out.
    if (std::fabs(oldWidth - width) > 1.0f && width != 0) {
        brls::Logger::debug("RecyclingGrid::checkWidth from {} to {}", oldWidth, width);
        oldWidth = width;
        return true;
    }
    return false;
}

void RecyclingGrid::setPadding(float padding) { this->setPadding(padding, padding, padding, padding); }

void RecyclingGrid::setPadding(float top, float right, float bottom, float left) {
    paddingTop = top;
    paddingRight = right;
    paddingBottom = bottom;
    paddingLeft = left;

    this->reloadData();
}

void RecyclingGrid::setPaddingTop(float top) {
    paddingTop = top;
    this->reloadData();
}

void RecyclingGrid::setPaddingRight(float right) {
    paddingRight = right;
    this->reloadData();
}

void RecyclingGrid::setPaddingBottom(float bottom) {
    paddingBottom = bottom;
    this->reloadData();
}

void RecyclingGrid::setPaddingLeft(float left) {
    paddingLeft = left;
    this->reloadData();
}

brls::View* RecyclingGrid::getDefaultFocus() {
    if (!this->dataSource || this->dataSource->getItemCount() == 0) return nullptr;
    brls::View* cell = ScrollingFrame::getDefaultFocus();
    if (cell) return cell;
    // giveFocus via a navigation route: target the first already attached
    // cell (focusable — skeletons are not). NO materialization here:
    // getDefaultFocus is probed by navigation traversals, mutating the
    // content at that point wreaks havoc.
    for (auto* child : this->contentBox->getChildren()) {
        brls::View* focus = child->getDefaultFocus();
        if (focus) return focus;
    }
    return nullptr;
}

brls::View* RecyclingGrid::create() { return new RecyclingGrid(); }

/// RecyclingGridContentBox

RecyclingGridContentBox::RecyclingGridContentBox(RecyclingView* recycler) : Box(brls::Axis::ROW), recycler(recycler) {}

brls::View* RecyclingGridContentBox::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    return this->recycler->getNextCellFocus(direction, currentView);
}
