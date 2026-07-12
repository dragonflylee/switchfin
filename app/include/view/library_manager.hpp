/*
    GMCA — libraries manager (issue #9).

    A full-content detail view (presented over the main content area while the
    sidebar stays visible, so every change previews live) that reorders and
    shows/hides the reorderable sidebar tabs: server libraries + Playlists +
    Watchlist. The layout is saved per server.

    Gamepad reorder uses a "grab" mode (the sidebar list can't use D-pad
    up/down for both navigation and moving, and LB/RB are already the tab
    switchers on the frame):
      - A       : grab / drop the focused row
      - D-pad ▲▼: move the row while grabbed
      - Y       : show / hide the focused row (when not grabbed)
      - B       : back (registered by the detail-view host)
*/

#pragma once

#include <borealis.hpp>
#include "activity/main_activity.hpp"
#include <set>
#include <string>
#include <vector>

class LibraryManager : public brls::Box {
public:
    explicit LibraryManager(MainTabFrame* frame);

    brls::View* getDefaultFocus() override;

    // --- called by the rows ---
    bool isGrabbed(int index) const { return this->grabbedIndex == index; }
    bool anyGrabbed() const { return this->grabbedIndex >= 0; }
    void toggleGrab(int index);
    void moveGrabbed(int delta);
    void toggleVisible(int index);

private:
    void rebuild();
    void commit();  // persist the working order/visibility + apply live

    MainTabFrame* frame;
    std::vector<MainTabFrame::SidebarEntry> entries;  // working copy
    int grabbedIndex = -1;
    int pendingFocus = 0;  // row index to focus after the next rebuild
    bool live = false;     // false during the initial (pre-mount) build

    brls::Box* rowsBox = nullptr;
    brls::View* focusTarget = nullptr;
};
