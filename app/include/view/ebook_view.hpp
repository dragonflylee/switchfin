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

    // 后台线程只做网络下载，文档解析与渲染都在 UI 线程
    struct LoadResult {
        bool ok = false;
        bool local = false;
        std::string error;
        std::string data;
    };

    void zoomIn();
    void zoomOut();
    void togglePageMode();
    void renderCurrent();
    void applyZoom();
    int pageStep() const;
    void goToPage(int target);
    void computeLayout(float& dispW, float& dispH, float& groupW, float& groupH);
    void updateLayout();
    void clampPan();
    void loadOutline();
    void showOutline();
    static LoadResult loadInBackground(const std::string& url, const HTTP::Header& headers);
    void applyLoadResult(const std::string& url, LoadResult&& result, float percent);

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