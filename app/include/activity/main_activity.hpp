/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <borealis.hpp>
#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>

/// Main sidebar: the static tabs (home, search, downloads, settings) come
/// from activity/main.xml, plus one tab per Plex library inserted after
/// the home tab once /library/sections answers. A server/profile switch
/// recreates the whole MainActivity, so the tabs follow the active server.
class MainTabFrame : public AutoTabFrame {
public:
    /// fetch the server libraries and insert their sidebar tabs
    void loadLibraries();

    static brls::View* create();

private:
    void addLibraryTabs(const std::vector<plex::Section>& sections);

    bool librariesLoaded = false;
};

class MainActivity : public brls::Activity {
public:
    // Declare that the content of this activity is the given XML file
    CONTENT_FROM_XML_RES("activity/main.xml");

    MainActivity();

    void onContentAvailable() override;

private:
    BRLS_BIND(MainTabFrame, tabFrame, "main/tabFrame");
};
