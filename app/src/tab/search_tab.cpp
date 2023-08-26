/*
    Copyright 2023 dragonflylee
*/

#include "tab/search_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "tab/search_result.hpp"
#include "api/jellyfin.hpp"
#include <fstream>

using namespace brls::literals;  // for _i18n

typedef brls::Event<char> KeyboardEvent;

class KeyboardButton : public RecyclingGridItem {
public:
    KeyboardButton() {
        key = new brls::Label();
        key->setVerticalAlign(brls::VerticalAlign::CENTER);
        key->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        this->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_2"));
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setCornerRadius(4);
        this->addView(key);
    }

    void setValue(const std::string& value) { key->setText(value); }

    static RecyclingGridItem* create() { return new KeyboardButton(); }

private:
    brls::Label* key = nullptr;
};

class DataSourceKeyboard : public RecyclingGridDataSource {
public:
    explicit DataSourceKeyboard(KeyboardEvent::Callback cb) {
        for (int i = 'A'; i <= 'Z'; i++) list.push_back(i);
        for (int i = '1'; i <= '9'; i++) list.push_back(i);
        list.push_back('0');

        this->event.subscribe(cb);
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<KeyboardButton*>(recycler->dequeueReusableCell("Cell"));
        cell->setValue(std::string{this->list[index]});
        return cell;
    }

    size_t getItemCount() override { return list.size(); }

    void onItemSelected(brls::View* recycler, size_t index) override { this->event.fire(list[index]); }

    void clearData() override { this->list.clear(); }

private:
    std::vector<char> list;
    KeyboardEvent event;
};

class SearchCard : public RecyclingGridItem {
public:
    SearchCard() { this->inflateFromXMLRes("xml/view/search_card.xml"); }

    static RecyclingGridItem* create() { return new SearchCard(); }

    BRLS_BIND(brls::Label, order, "search/card/order");
    BRLS_BIND(brls::Label, content, "search/card/content");
};

class SuggestDataSource : public VideoDataSource {
public:
    explicit SuggestDataSource(const MediaList& r) : VideoDataSource(r) {}

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<SearchCard*>(recycler->dequeueReusableCell("Card"));
        cell->order->setText(std::to_string(index + 1));
        cell->content->setText(this->list[index].Name);
        return cell;
    }
};

class HistoryDataSource : public RecyclingGridDataSource {
public:
    HistoryDataSource() {
        this->path = AppConfig::instance().configDir() + "/history.json";
        std::ifstream readFile(this->path);
        if (readFile.is_open()) {
            try {
                this->list = nlohmann::json::parse(readFile);
            } catch (const std::exception& e) {
                brls::Logger::error("load search history: {}", e.what());
            }
        }
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<SearchCard*>(recycler->dequeueReusableCell("Card"));
        cell->order->setText(std::to_string(index + 1));
        cell->content->setText(this->list[index]);
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        recycler->present(new SearchResult(this->list[index]));
    }

    void clearData() override { this->list.clear(); }

    void appendData(const std::string& searchTerm) {
        for (auto& item : this->list) {
            if (item == searchTerm) return;
        }
        this->list.push_back(searchTerm);

        std::ofstream writeFile(this->path);
        if (writeFile.is_open()) {
            nlohmann::json j(this->list);
            writeFile << j.dump(2);
            writeFile.close();
        }
    }

private:
    std::string path;
    std::vector<std::string> list;
};

SearchTab::SearchTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/search_tv.xml");
    brls::Logger::debug("SearchTab: create");

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->searchSVG->setImageFromSVGRes("img/header-search-dark.svg");
    } else {
        this->searchSVG->setImageFromSVGRes("img/header-search.svg");
    }

    this->searchBox->registerClickAction([this](...) {
        brls::Application::getImeManager()->openForText(
            [this](const std::string& text) {
                this->currentSearch = text;
                this->updateInput();
            },
            "main/search/tv/hint"_i18n, "", 32, this->currentSearch, 0);
        return true;
    });
    this->searchBox->addGestureRecognizer(new brls::TapGestureRecognizer(this->searchBox));

    this->recyclingKeyboard->registerCell("Cell", KeyboardButton::create);
    this->recyclingKeyboard->setDataSource(new DataSourceKeyboard([this](char key) {
        this->currentSearch += key;
        this->updateInput();
    }));
    this->searchSuggest->registerCell("Card", SearchCard::create);
    this->searchSuggest->registerCell("Cell", VideoCardCell::create);
    this->searchSuggest->onNextPage([this]() {
        if (this->currentSearch.size() > 0) this->doSearch(this->currentSearch);
    });

    HistoryDataSource* history = new HistoryDataSource();
    this->searchHistory->registerCell("Card", SearchCard::create);
    this->searchHistory->setDataSource(history);

    this->clearLabel->registerClickAction([this](...) {
        this->currentSearch.clear();
        this->updateInput();
        return true;
    });
    this->clearLabel->addGestureRecognizer(new brls::TapGestureRecognizer(clearLabel));

    this->deleteLabel->registerClickAction([this](...) {
        if (!currentSearch.empty()) {
            this->currentSearch.pop_back();
            this->updateInput();
        }
        return true;
    });
    this->deleteLabel->addGestureRecognizer(new brls::TapGestureRecognizer(deleteLabel));

    this->searchLabel->registerClickAction([this, history](...) {
        if (this->currentSearch.size() > 0) {
            this->present(new SearchResult(this->currentSearch));
            history->appendData(this->currentSearch);
        }
        return true;
    });
    this->searchLabel->addGestureRecognizer(new brls::TapGestureRecognizer(searchLabel));

    this->updateInput();
    this->doSuggest();
}

SearchTab::~SearchTab() { brls::Logger::debug("SearchTab: deleted"); }

brls::View* SearchTab::create() { return new SearchTab(); }

void SearchTab::doSuggest() {
    std::string query = HTTP::encode_form({
        {"sortBy", "IsFavoriteOrLiked,Random"},
        {"includeItemTypes", "Movie,Series,MusicArtist"},
        {"limit", "24"},
        {"Recursive", "true"},
        {"EnableImages", "false"},
        {"EnableTotalRecordCount", "false"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->searchSuggest->spanCount = 1;
            this->searchSuggest->estimatedRowHeight = 30;
            this->searchSuggest->setDataSource(new SuggestDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->searchSuggest->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}

void SearchTab::doSearch(const std::string& searchTerm) {
    std::string query = HTTP::encode_form({
        {"searchTerm", searchTerm},
        {"IncludeItemTypes", "Movie,Series"},
        {"Recursive", "true"},
        {"IncludeMedia", "true"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->searchIndex)},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->searchIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->searchSuggest->setEmpty();
            } else if (r.StartIndex == 0) {
                this->searchSuggest->spanCount = 5;
                this->searchSuggest->estimatedRowHeight = 240;
                this->searchSuggest->setDataSource(new VideoDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->searchSuggest->getDataSource());
                if (dataSrc != nullptr) dataSrc->appendData(r.Items);
                this->searchSuggest->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}

void SearchTab::updateInput() {
    if (this->currentSearch.empty()) {
        this->inputLabel->setText("main/search/tv/hint"_i18n);
        this->inputLabel->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        if (this->historyBox->getVisibility() == brls::Visibility::GONE) {
            this->historyBox->setVisibility(brls::Visibility::VISIBLE);
            this->searchSuggest->setEmpty("hints/loading"_i18n);
            this->doSuggest();
        }
    } else {
        this->inputLabel->setText(this->currentSearch);
        this->inputLabel->setTextColor(brls::Application::getTheme().getColor("brls/text"));
        if (this->historyBox->getVisibility() == brls::Visibility::VISIBLE) {
            this->historyBox->setVisibility(brls::Visibility::GONE);
            this->searchSuggest->setEmpty("hints/loading"_i18n);
        }
        this->doSearch(this->currentSearch);
    }
}