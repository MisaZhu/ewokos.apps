// Webview widget for litehtml rendering

#pragma once

#include <Widget/Scrollable.h>
#include <litehtml.h>
#include <litehtml/context.h>
#include <memory>
#include <vector>
#include <pthread.h>

class XContainer;

struct HttpTask {
    static const int TASK_HTML   = 0;
    static const int TASK_CSS    = 1;
    static const int TASK_IMAGE  = 2;
    std::string url;
    int type;
    bool loading;
};

struct HttpResult {
    std::string url;
    int type;
    bool ok;
    std::string content;
};

// Forward declaration in global namespace
extern "C" void* _task_thread(void* p);

namespace Ewok {

class WidgetWebview : public Scrollable {
public:
    enum BuildPhase {
        BUILD_IDLE = 0,
        BUILD_PRELOAD_CSS,
        BUILD_CREATE_DOC,
        BUILD_RENDER_DOC,
        BUILD_SWAP_DOC,
        BUILD_FAILED,
    };

    WidgetWebview();
    virtual ~WidgetWebview();

    bool addTask(const HttpTask& task);
    void removeTask(const std::string& url);
    bool getTask(HttpTask& task);

    bool loadHtml(const std::string& url);
    void setDefaultCSS(const std::string& url);
    bool loadCSS(const std::string& url);
    bool loadImage(const std::string& url);

    bool loadCSSContent(const std::string& url, const std::string& content);
    bool loadHtmlContent(const std::string& content);
    bool loadImageContent(const std::string& url, uint8_t* data, int sz);

    friend void* ::_task_thread(void* p);
protected:
    virtual void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) override;
    virtual void onResize() override;
    virtual void setAttr(const string& attr, json_var_t*value) override;
    virtual void onTimer(uint32_t timerFPS, uint32_t timerSteps) override;

    virtual bool onScroll(int step, bool horizontal) override;
    virtual void updateScroller() override;
    virtual bool onMouse(xevent_t* ev) override;

    bool loadHtmlTask(const std::string& url);
    bool loadCSSTask(const std::string& url);
    bool loadImageTask(const std::string& url);

    virtual void onTaskStart(const HttpTask& task) {}
    virtual void onTaskEnd(const HttpTask& task) {}
    virtual void onTaskFailed(const HttpTask& task) {}
    virtual void onTasksEnd() {}
    virtual void onBuildStatus(const std::string& status, int progress) {}
    void pushResult(const HttpResult& result);
    bool getResult(HttpResult& result);
    bool processResults();
    void cleanupBuildResources();
    void setBuildStatus(const std::string& status, int progress);
    void advanceBuildStep();
    bool applyPendingLayoutUpdates();
    void clampScrollLocked(int docWidth, int docHeight);
    bool hasSeenCSS(const std::string& url) const;
    void rememberCSS(const std::string& url);
    void forgetCSS(const std::string& url);
private:
    std::string m_defaultCSSUrl;
    std::string m_currentHtmlUrl;
    XContainer*                   m_container;
    litehtml::document::ptr       m_doc;
    XContainer*                   m_pendingDeleteContainer;
    litehtml::document::ptr       m_pendingDeleteDoc;
    litehtml::context            m_browser_context;
    litehtml::context*           m_activeContext;
    BuildPhase                   m_buildPhase;
    std::string                  m_buildHtmlContent;
    std::string                  m_buildHtmlUrl;
    std::string                  m_buildStatus;
    int                          m_buildProgress;
    XContainer*                  m_buildContainer;
    litehtml::document::ptr      m_buildDoc;
    litehtml::context            m_buildContext;
    litehtml::context*           m_buildTargetContext;

    bool m_task_running;
    bool m_task_ended;
    int m_clientWidth;
    int m_clientHeight;

    // Task queue
    std::vector<HttpTask> m_taskQueue;
    std::vector<std::string> m_seenCssUrls;
    pthread_mutex_t m_taskMutex;
    std::vector<HttpResult> m_resultQueue;
    pthread_mutex_t m_resultMutex;

    // Mutex for protecting shared resources (m_doc, m_browser_context)
    pthread_mutex_t m_renderMutex;

    // Scroll offsets
    int m_scrollX;
    int m_scrollY;
    
    // Dirty flags consumed by onTimer so repaint stays draw-only.
    bool m_needsStyleUpdate;
    bool m_needsLayout;
    bool m_buildNeedsStyleUpdate;
    bool m_buildNeedsLayout;
    bool m_flushDeferredImages;
    bool m_defaultCssPrepared;
    bool m_defaultCssLoading;
    bool m_deferBuildStep;
    uint64_t m_layoutDirtyAt;
    uint64_t m_buildLayoutDirtyAt;
};

}
