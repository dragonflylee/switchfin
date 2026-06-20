/*
    Copyright 2023 dragonflylee
*/

#include "tab/search_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "tab/search_result.hpp"
#include "utils/dialog.hpp"
#include "utils/keybind.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include <fstream>

using namespace brls::literals;  // for _i18n

/// Persistent history (search.json): unchanged storage (JSON array,
/// dedupe, insert at head) — only the display moves to chips.
class SearchHistory {
public:
    SearchHistory() {
        this->path = AppConfig::instance().configDir() + "/search.json";
        std::ifstream readFile(this->path);
        if (readFile.is_open()) {
            try {
                this->list = nlohmann::json::parse(readFile);
            } catch (const std::exception& e) {
                brls::Logger::error("load search history: {}", e.what());
            }
        }
    }

    const std::vector<std::string>& items() const { return this->list; }

    void append(const std::string& searchTerm) {
        for (auto& item : this->list) {
            if (item == searchTerm) return;
        }
        this->list.insert(this->list.begin(), searchTerm);
        this->save();
    }

    void clear() {
        this->list.clear();
        this->save();
    }

private:
    void save() {
        std::ofstream writeFile(this->path);
        if (writeFile.is_open()) {
            nlohmann::json j(this->list);
            writeFile << j.dump(2);
            writeFile.close();
        }
    }

    std::string path;
    std::vector<std::string> list;
};

/// Removes the last UTF-8 code point: the IME can input multi-byte
/// characters, a bare pop_back would cut a sequence in the middle.
static void utf8PopBack(std::string& text) {
    if (text.empty()) return;
    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) i--;
    text.erase(i);
}

SearchTab::SearchTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/search_tv.xml");
    brls::Logger::debug("SearchTab: create");

    this->history = std::make_unique<SearchHistory>();

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->searchSVG->setImageFromSVGRes("img/header-search-dark.svg");
    } else {
        this->searchSVG->setImageFromSVGRes("img/header-search.svg");
    }

    // field: click = full input through the system IME
    this->searchBox->registerClickAction([this](brls::View* view) {
        brls::Application::getImeManager()->openForText(
            [this](const std::string& text) {
                this->currentSearch = text;
                this->updateInput();
            },
            "main/search/hint"_i18n, "", 32, this->currentSearch, 0);
        return true;
    });
    this->searchBox->addGestureRecognizer(new brls::TapGestureRecognizer(this->searchBox));

    // icon action row: the label lives in the A button hint
    this->actionClear->registerAction(
        "main/search/clear"_i18n, brls::BUTTON_A,
        [this](brls::View* view) {
            this->currentSearch.clear();
            this->updateInput();
            return true;
        },
        false, false, brls::SOUND_CLICK);
    this->actionClear->addGestureRecognizer(new brls::TapGestureRecognizer(this->actionClear));

    this->actionDelete->registerAction(
        "main/search/delete"_i18n, brls::BUTTON_A,
        [this](brls::View* view) {
            if (this->currentSearch.empty()) return true;
            utf8PopBack(this->currentSearch);
            this->updateInput();
            return true;
        },
        false, true, brls::SOUND_CLICK);
    this->actionDelete->addGestureRecognizer(new brls::TapGestureRecognizer(this->actionDelete));

    this->actionSpace->registerAction(
        "main/search/space"_i18n, brls::BUTTON_A,
        [this](brls::View* view) {
            if (this->currentSearch.empty()) return true;
            this->currentSearch += ' ';
            this->updateInput();
            return true;
        },
        false, false, brls::SOUND_CLICK);
    this->actionSpace->addGestureRecognizer(new brls::TapGestureRecognizer(this->actionSpace));

    this->actionSearch->registerAction(
        "main/tabs/search"_i18n, brls::BUTTON_A,
        [this](brls::View* view) {
            this->launchSearch();
            return true;
        },
        false, false, brls::SOUND_CLICK);
    this->actionSearch->addGestureRecognizer(new brls::TapGestureRecognizer(this->actionSearch));

    // gamepad shortcut: X = backspace from the whole keyboard column
    this->leftBox->registerAction(
        "main/search/delete"_i18n, brls::BUTTON_X,
        [this](brls::View* view) {
            if (this->currentSearch.empty()) return false;
            utf8PopBack(this->currentSearch);
            this->updateInput();
            return true;
        },
        false, true);

    this->buildKeyboard();

    // B from the right area: back to the keyboard (otherwise B climbs to
    // the sidebar via the action set by AutoSidebarItem on the tab)
    this->rightBox->registerAction(
        "main/search/keyboard"_i18n, brls::BUTTON_B,
        [this](brls::View* view) {
            brls::Application::giveFocus(this->keyboardBox);
            return true;
        },
        false, false, brls::SOUND_FOCUS_CHANGE);

    // X on the chips: clear the history (with confirmation)
    this->historyChips->registerAction("main/search/clear"_i18n, brls::BUTTON_X, [this](brls::View* view) {
        Dialog::cancelable("main/search/clear_history"_i18n, [this]() {
            this->history->clear();
            this->buildHistoryChips();
            brls::sync([this]() { brls::Application::giveFocus(this->searchBox); });
        });
        return true;
    });

    this->searchSuggest->registerCell("Cell", VideoCardCell::create);
}

void SearchTab::onCreate() {
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->updateInput();
        return true;
    });
    this->registerAction(KeyBind::getRefresh(), [this](...) {
        this->updateInput();
        return true;
    });
    // + / START: launch the search from anywhere in the tab
    this->registerAction("main/tabs/search"_i18n, brls::BUTTON_START, [this](...) {
        this->launchSearch();
        return true;
    });
    this->updateInput();
}

SearchTab::~SearchTab() { brls::Logger::debug("SearchTab: deleted"); }

brls::View* SearchTab::create() { return new SearchTab(); }

/// Static 6x6 keyboard (A-Z then 1-9 and 0): focusable brls::Box in fixed
/// rows — everything is visible, nothing scrolls. Row width:
/// 6x50 + 5x8 = 340 (the column width), total height 6x46 + 5x8 = 316.
void SearchTab::buildKeyboard() {
    static const std::string layout = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    auto theme = brls::Application::getTheme();

    brls::Box* grid[6][6];
    for (int row = 0; row < 6; row++) {
        auto* line = new brls::Box();
        line->setHeight(46);
        if (row > 0) line->setMarginTop(8);
        for (int col = 0; col < 6; col++) {
            const char key = layout[row * 6 + col];
            auto* cell = new brls::Box();
            cell->setFocusable(true);
            cell->setDimensions(50, 46);
            if (col > 0) cell->setMarginLeft(8);
            cell->setCornerRadius(8);
            cell->setHighlightCornerRadius(8);
            cell->setBackgroundColor(theme.getColor("color/grey_2"));
            cell->setAlignItems(brls::AlignItems::CENTER);
            cell->setJustifyContent(brls::JustifyContent::CENTER);

            auto* label = new brls::Label();
            label->setText(std::string(1, key));
            label->setFontSize(18);
            cell->addView(label);

            cell->registerClickAction([this, key](brls::View* view) {
                this->currentSearch += key;
                this->updateInput();
                return true;
            });
            cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));

            grid[row][col] = cell;
            line->addView(cell);
        }
        this->keyboardBox->addView(line);
    }

    // vertical navigation column by column: borealis navigates by child
    // order (not by geometry), without routes DOWN would always land on
    // the first key of the next row
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 6; col++) {
            if (row > 0) grid[row][col]->setCustomNavigationRoute(brls::FocusDirection::UP, grid[row - 1][col]);
            if (row < 5) grid[row][col]->setCustomNavigationRoute(brls::FocusDirection::DOWN, grid[row + 1][col]);
        }
    }

    // keyboard <-> action row junction: button closest to the column
    brls::Box* actions[4] = {this->actionClear, this->actionDelete, this->actionSpace, this->actionSearch};
    const int actionForCol[6] = {0, 0, 1, 2, 2, 3};
    const int colForAction[4] = {0, 2, 3, 5};
    for (int col = 0; col < 6; col++)
        grid[0][col]->setCustomNavigationRoute(brls::FocusDirection::UP, actions[actionForCol[col]]);
    for (int btn = 0; btn < 4; btn++)
        actions[btn]->setCustomNavigationRoute(brls::FocusDirection::DOWN, grid[0][colForAction[btn]]);
}

/// (Re)builds the history chips; hides the whole section when there is
/// nothing to show (the suggestions grid then occupies the area).
void SearchTab::buildHistoryChips() {
    auto theme = brls::Application::getTheme();

    // if the focus is in the chips, move it out before destroying the views
    // (otherwise ghost halo on a freed view — cf. borealis pitfalls)
    brls::View* focus = brls::Application::getCurrentFocus();
    bool focusInside = false;
    for (brls::View* v = focus; v != nullptr; v = v->getParent()) {
        if (v == this->historyChips.getView()) {
            focusInside = true;
            break;
        }
    }
    if (focusInside) brls::Application::giveFocus(this->searchBox);

    this->historyChips->clearViews();

    const auto& items = this->history->items();
    if (items.empty()) {
        this->historyBox->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->historyBox->setVisibility(brls::Visibility::VISIBLE);

    for (const std::string& term : items) {
        auto* chip = new brls::Box();
        chip->setFocusable(true);
        chip->setHeight(36);
        chip->setCornerRadius(18);
        chip->setHighlightCornerRadius(18);
        chip->setBackgroundColor(theme.getColor("color/pill"));
        chip->setAlignItems(brls::AlignItems::CENTER);
        chip->setPaddingLeft(16);
        chip->setPaddingRight(16);
        chip->setMarginRight(10);

        auto* label = new brls::Label();
        label->setText(term);
        label->setFontSize(15);
        chip->addView(label);

        // click = rerun the search in the right area
        chip->registerClickAction([this, term](brls::View* view) {
            this->currentSearch = term;
            this->updateInput();
            // the history section was just hidden: move the focus out of
            // the hidden chip (the field reflects the rerun query)
            brls::sync([this]() { brls::Application::giveFocus(this->searchBox); });
            return true;
        });
        chip->addGestureRecognizer(new brls::TapGestureRecognizer(chip));

        this->historyChips->addView(chip);
    }
}

/// Explicit validation: remembers the term then opens the detailed results
/// page (paginated movies / shows).
void SearchTab::launchSearch() {
    if (this->currentSearch.empty()) return;
    this->history->append(this->currentSearch);
    this->present(new SearchResult(this->currentSearch));
}

void SearchTab::doSuggest() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getRecentlyAdded(0, 24,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            // poster grid: the suggestions are complete items
            this->searchSuggest->setDataSource(new VideoDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->searchSuggest->setError(ex);
        });
}

void SearchTab::doSearch(const std::string& searchTerm) {
    ASYNC_RETAIN
    // a single page: search does not paginate reliably
    AppConfig::instance().backend().search(searchTerm, media::MediaKind::Any, 40,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->searchSuggest->setEmpty(
                    "main/search/no_results"_i18n, "main/search/no_results_sub"_i18n, "icon/ico-search.svg");
            } else {
                this->searchSuggest->setDataSource(new VideoDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}

void SearchTab::updateInput() {
    auto theme = brls::Application::getTheme();
    if (this->currentSearch.empty()) {
        this->inputLabel->setText("main/search/placeholder"_i18n);
        this->inputLabel->setTextColor(theme.getColor("font/grey"));
        this->buildHistoryChips();
        this->suggestHeader->setTitle("main/search/suggest"_i18n);
        this->searchSuggest->showSkeleton();
        this->doSuggest();
    } else {
        this->inputLabel->setText(this->currentSearch);
        this->inputLabel->setTextColor(theme.getColor("brls/text"));
        if (this->historyBox->getVisibility() == brls::Visibility::VISIBLE) {
            this->historyBox->setVisibility(brls::Visibility::GONE);
        }
        this->suggestHeader->setTitle("main/search/results"_i18n);
        this->searchSuggest->showSkeleton();
        this->doSearch(this->currentSearch);
    }
}
