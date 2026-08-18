#pragma once

#include <borealis.hpp>
#include <api/http.hpp>

struct fz_context;
struct fz_document;

class EBookView : public brls::Box {
public:
    EBookView();
    ~EBookView() override;

    void onLayout() override;

    void open(const std::string& input, float percent = 0, const HTTP::Header& headers = {});
    void render(brls::Image* view, int n);

private:
    struct Bookmark {
        std::string title;
        int page = 0;
    };

    void zoomIn();
    void zoomOut();
    void togglePageMode();
    void renderCurrent();
    void applyZoom();
    void computeLayout(float& dispW, float& dispH, float& groupW, float& groupH);
    void updateLayout();
    void clampPan();
    void loadOutline();
    void showOutline();

    brls::Image* left = nullptr;
    brls::Image* right = nullptr;
    brls::ProgressSpinner* loading = nullptr;
    fz_context* ctx = nullptr;
    fz_document* doc = nullptr;
    int page = 0;
    int count = 0;
    float zoom = 1.0f;
    bool singlePage = false;
    float pageWidth = 0;
    float pageHeight = 0;
    float panX = 0;
    float panY = 0;
    float lastW = -1;
    float lastH = -1;
    float lastPanX = 1;
    float lastPanY = 1;
    bool layouting = false;
    bool outlineLoaded = false;
    std::vector<Bookmark> outline;
};