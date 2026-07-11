#pragma once

/*
    pleNx — file-browser sort side panel (Y action, issue #23).

    Same "quick panel" pattern as MediaFilter (media_filter.cpp): a translucent
    side sheet with SelectorCells. The choice is session-static; the actual sort
    is client-side (dir_entry.hpp) and re-applied to the current listing on close.
*/

#include <borealis.hpp>
#include <client/dir_entry.hpp>

class RemoteFilter : public brls::Box {
public:
    RemoteFilter();
    ~RemoteFilter() override;

    bool isTranslucent() override { return true; }

    brls::VoidEvent* getEvent() { return &this->event; }

    inline static int selectedSort = 0;   // 0 name, 1 date, 2 size
    inline static int selectedOrder = 0;  // 0 ascending, 1 descending

    static remote::SortKey key() {
        switch (selectedSort) {
        case 1:
            return remote::SortKey::DATE;
        case 2:
            return remote::SortKey::SIZE;
        default:
            return remote::SortKey::NAME;
        }
    }
    static bool desc() { return selectedOrder == 1; }

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "remote/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "remote/sort/order");

    brls::VoidEvent event;
};
