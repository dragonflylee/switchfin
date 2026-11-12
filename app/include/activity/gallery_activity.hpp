//
// Created by fang on 2023/4/26.
//

#pragma once

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>
#include <api/http.hpp>

class GalleryView;

class GalleryActivity : public brls::Activity {
public:
    // Declare that the content of this activity is the given XML file
    CONTENT_FROM_XML_RES("activity/gallery.xml");

    explicit GalleryActivity(const std::string& url, const HTTP::Header& headers = {});

    // Browse a list of images starting at index; L/R switch between them.
    GalleryActivity(std::vector<std::string> urls, size_t index, const HTTP::Header& headers = {});

    bool isTranslucent() override;

    void onContentAvailable() override;

    ~GalleryActivity() override;

private:
    BRLS_BIND(GalleryView, gallery, "hint/gallery");

    std::vector<std::string> urls;
    size_t startIndex = 0;
    HTTP::Header headers;
};