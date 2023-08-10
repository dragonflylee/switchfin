/*
    Copyright 2023 dragonflylee
*/

#include "tab/search_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/suggest_list.hpp"
#include "view/video_source.hpp"
#include "tab/search_result.hpp"
#include "api/jellyfin.hpp"

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

class SuggestDataSource : public VideoDataSource {
public:
    explicit SuggestDataSource(const MediaList& r) : VideoDataSource(r) {}

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<SuggestCard*>(recycler->dequeueReusableCell("Card"));
        cell->content->setText(list[index].Name);
        return cell;
    }
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

    this->recyclingKeyboard->registerCell("Cell", &KeyboardButton::create);
    this->recyclingKeyboard->setDataSource(new DataSourceKeyboard([this](char key) {
        this->currentSearch += key;
        this->updateInput();
    }));

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

    this->searchLabel->registerClickAction([this](...) {
        if (this->currentSearch.size() > 0) {
            this->present(new SearchResult(this->currentSearch));
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
        {"limit", "20"},
        {"Recursive", "true"},
        {"EnableImages", "false"},
        {"EnableTotalRecordCount", "false"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->searchSuggest->setSource(new SuggestDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->searchSuggest->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}

void SearchTab::doSearch(const std::string& searchTerm) {

}

void SearchTab::updateInput() {
    brls::View* history = this->searchHistory->getParent();
    if (this->currentSearch.empty()) {
        this->inputLabel->setText("main/search/tv/hint"_i18n);
        this->inputLabel->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        if (history->getVisibility() == brls::Visibility::GONE) {
            history->setVisibility(brls::Visibility::VISIBLE);
        }
    } else {
        this->inputLabel->setText(this->currentSearch);
        this->inputLabel->setTextColor(brls::Application::getTheme().getColor("brls/text"));
        if (history->getVisibility() == brls::Visibility::VISIBLE) {
            history->setVisibility(brls::Visibility::GONE);
        }
    }
}