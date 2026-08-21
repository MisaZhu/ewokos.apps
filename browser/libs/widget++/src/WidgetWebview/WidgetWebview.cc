// Webview widget implementation

#include "WidgetWebview/WidgetWebview.h"
#include "Widget/WidgetWin.h"
#include "XContainer.h"

#include <iostream>
#include <fstream>
#include <string>
#include <graph/graph.h>
#include <font/font.h>
#include <x++/X.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/klog.h>
#include <ewoksys/proc.h>
#include <deque>
#include <ctype.h>
#include <pthread.h>

using namespace Ewok;

static const uint32_t kLayoutDebounceMs = 30;

static std::string strip_script_blocks(const std::string& html, int* removed_count)
{
    if(removed_count) {
        *removed_count = 0;
    }
    if(html.empty()) {
        return html;
    }

    std::string lower = html;
    for(char& ch : lower) {
        ch = (char)::tolower((unsigned char)ch);
    }

    std::string out;
    out.reserve(html.size());
    size_t pos = 0;
    int removed = 0;
    while(pos < html.size()) {
        size_t script_open = lower.find("<script", pos);
        if(script_open == std::string::npos) {
            out.append(html, pos, html.size() - pos);
            break;
        }

        out.append(html, pos, script_open - pos);
        size_t script_close = lower.find("</script>", script_open);
        if(script_close == std::string::npos) {
            removed++;
            break;
        }

        pos = script_close + 9;
        removed++;
    }

    if(removed_count) {
        *removed_count = removed;
    }
    return removed > 0 ? out : html;
}

static uint32_t debug_hash_text(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < text.size(); ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 16777619u;
    }
    return hash;
}

// Explicit template instantiation for deque<HttpTask>
template class std::deque<HttpTask>;

WidgetWebview::WidgetWebview()
    : m_clientWidth(640)
    , m_clientHeight(480)
    , m_doc(nullptr)
    , m_container(nullptr)
    , m_activeContext(&m_browser_context)
    , m_buildPhase(BUILD_IDLE)
    , m_buildProgress(0)
    , m_buildContainer(nullptr)
    , m_buildDoc(nullptr)
    , m_buildTargetContext(nullptr)
    , m_pendingDeleteContainer(nullptr)
    , m_pendingDeleteDoc(nullptr)
    , m_scrollX(0)
    , m_scrollY(0)
    , m_needsStyleUpdate(false)
    , m_needsLayout(false)
    , m_buildNeedsStyleUpdate(false)
    , m_buildNeedsLayout(false)
    , m_flushDeferredImages(false)
    , m_defaultCssPrepared(false)
    , m_defaultCssLoading(false)
    , m_deferBuildStep(false)
    , m_layoutDirtyAt(0)
    , m_buildLayoutDirtyAt(0)
{
    m_container = new XContainer(&m_browser_context, this);
    m_task_running = false;
    m_task_ended = false;
    pthread_mutex_init(&m_taskMutex, NULL);
    pthread_mutex_init(&m_resultMutex, NULL);
    pthread_mutex_init(&m_renderMutex, NULL);
}

WidgetWebview::~WidgetWebview()
{
    // Signal task thread to end
    m_task_ended = true;

    // Wait for task thread to finish
    while(m_task_running) {
        proc_usleep(10000);
    }

    pthread_mutex_lock(&m_renderMutex);
    cleanupBuildResources();
    if(m_doc)
        delete m_doc;
    if(m_container)
        delete m_container;
    if(m_pendingDeleteDoc) { delete m_pendingDeleteDoc; m_pendingDeleteDoc = nullptr; }
    if(m_pendingDeleteContainer) { delete m_pendingDeleteContainer; m_pendingDeleteContainer = nullptr; }
    pthread_mutex_unlock(&m_renderMutex);

    pthread_mutex_destroy(&m_taskMutex);
    pthread_mutex_destroy(&m_resultMutex);
    pthread_mutex_destroy(&m_renderMutex);
}

void WidgetWebview::cleanupBuildResources()
{
    m_buildHtmlContent.clear();
    m_buildHtmlUrl.clear();
    m_buildStatus.clear();
    m_buildProgress = 0;
    m_buildNeedsStyleUpdate = false;
    m_buildNeedsLayout = false;
    m_needsLayout = false;
    m_needsStyleUpdate = false;
    m_flushDeferredImages = false;
    m_defaultCssPrepared = false;
    m_defaultCssLoading = false;
    m_deferBuildStep = false;
    m_layoutDirtyAt = 0;
    m_buildLayoutDirtyAt = 0;
    m_seenCssUrls.clear();
    m_buildPhase = BUILD_IDLE;
    m_buildTargetContext = nullptr;
    if(m_buildDoc) {
        delete m_buildDoc;
        m_buildDoc = nullptr;
    }
    if(m_buildContainer) {
        delete m_buildContainer;
        m_buildContainer = nullptr;
    }
}

void WidgetWebview::setBuildStatus(const std::string& status, int progress)
{
    m_buildStatus = status;
    m_buildProgress = progress;
    onBuildStatus(status, progress);
}

void WidgetWebview::clampScrollLocked(int docWidth, int docHeight)
{
    int maxX = docWidth - area.w;
    int maxY = docHeight - area.h;
    if(maxX < 0) {
        maxX = 0;
    }
    if(maxY < 0) {
        maxY = 0;
    }
    if(m_scrollX < 0) {
        m_scrollX = 0;
    } else if(m_scrollX > maxX) {
        m_scrollX = maxX;
    }
    if(m_scrollY < 0) {
        m_scrollY = 0;
    } else if(m_scrollY > maxY) {
        m_scrollY = maxY;
    }
}

void WidgetWebview::setDefaultCSS(const std::string& url)
{
    m_defaultCSSUrl = XContainer::normalizeURL(url, "");
}

bool WidgetWebview::hasSeenCSS(const std::string& url) const
{
    for(const auto& seen_url : m_seenCssUrls) {
        if(seen_url == url) {
            return true;
        }
    }
    return false;
}

void WidgetWebview::rememberCSS(const std::string& url)
{
    if(url.empty() || hasSeenCSS(url)) {
        return;
    }
    m_seenCssUrls.push_back(url);
}

void WidgetWebview::forgetCSS(const std::string& url)
{
    for(size_t i = 0; i < m_seenCssUrls.size(); ++i) {
        if(m_seenCssUrls[i] == url) {
            m_seenCssUrls.erase(m_seenCssUrls.begin() + i);
            break;
        }
    }
}

void* _task_thread(void* p)
{
    WidgetWebview* widget = (WidgetWebview*)p;
    HttpTask task;
    
    bool havetask = false;
    while(!widget->m_task_ended) {
        if (widget->getTask(task)) {
            havetask = true;
            bool res = false;
            widget->onTaskStart(task);
            // Process task
            if (task.type == HttpTask::TASK_HTML) {
                res = widget->loadHtmlTask(task.url);
            } else if (task.type == HttpTask::TASK_CSS) {
                res = widget->loadCSSTask(task.url);
            } else if (task.type == HttpTask::TASK_IMAGE) {
                res = widget->loadImageTask(task.url);
            }

            if(res) {
                widget->onTaskEnd(task);
            }
            else {
                widget->onTaskFailed(task);
            }
        }
        else {
            // No task left: notify once, then tear down the worker.
            // addTask() already recreates the thread on demand, so keeping
            // an idle worker parked in proc_usleep() only exposes a fragile
            // sleep/restore path for detached child threads.
            if(havetask) {
                havetask = false;
                klog("[xBrowser] task thread idle: queue drained\n");
                widget->onTasksEnd();
            }
            pthread_mutex_lock(&widget->m_taskMutex);
            bool queue_empty = widget->m_taskQueue.empty();
            if(queue_empty) {
                widget->m_task_running = false;
            }
            pthread_mutex_unlock(&widget->m_taskMutex);
            if(queue_empty) {
                return nullptr;
            }
        }
    }
    
    widget->m_task_running = false;
    return nullptr;
}

bool WidgetWebview::addTask(const HttpTask& task)
{
    pthread_mutex_lock(&m_taskMutex);

    for (auto& t : m_taskQueue) {
        if (t.url == task.url) {
            if(task.type == HttpTask::TASK_IMAGE) {
                klog("[xBrowser] queue image skipped: duplicate loading=%d queue=%d running=%d\n",
                    t.loading ? 1 : 0, (int)m_taskQueue.size(), m_task_running ? 1 : 0);
            }
            pthread_mutex_unlock(&m_taskMutex);
            return false;
        }
    }

    m_taskQueue.push_back(task);
    if(task.type == HttpTask::TASK_IMAGE) {
        klog("[xBrowser] queue image added: pending=%d running=%d\n",
            (int)m_taskQueue.size(), m_task_running ? 1 : 0);
    }
    pthread_mutex_unlock(&m_taskMutex);

    if (!m_task_running) {
        pthread_t tid;
        m_task_running = true;
        if (pthread_create(&tid, NULL, _task_thread, this) != 0) {
            m_task_running = false;
            return false;
        }
        pthread_detach(tid);
    }
    return true;
}

void WidgetWebview::removeTask(const std::string& url)
{
    pthread_mutex_lock(&m_taskMutex);
    for (size_t i = 0; i < m_taskQueue.size(); i++) {
        if (m_taskQueue[i].url == url) {
            m_taskQueue.erase(m_taskQueue.begin() + i);
            break;
        }
    }
    pthread_mutex_unlock(&m_taskMutex);
}

bool WidgetWebview::getTask(HttpTask& task)
{
    pthread_mutex_lock(&m_taskMutex);

    // Check if we should exit
    if (m_task_ended && m_taskQueue.empty()) {
        pthread_mutex_unlock(&m_taskMutex);
        return false;
    }

    // Get task from queue
    for (size_t i = 0; i < m_taskQueue.size(); i++) {
        if (!m_taskQueue[i].loading) {
            task = m_taskQueue[i];
            m_taskQueue[i].loading = true;
            if(task.type == HttpTask::TASK_IMAGE) {
                klog("[xBrowser] take image task: queue=%d\n",
                    (int)m_taskQueue.size());
            }
            pthread_mutex_unlock(&m_taskMutex);
            return true;
        }
    }

    pthread_mutex_unlock(&m_taskMutex);
    return false;
}

bool WidgetWebview::loadHtml(const std::string& url)
{
    klog("[xBrowser] queue html: %s\n", url.c_str());
    m_scrollX = 0;
    m_scrollY = 0;

    pthread_mutex_lock(&m_renderMutex);
    cleanupBuildResources();
    m_buildTargetContext = (m_activeContext == &m_browser_context) ? &m_buildContext : &m_browser_context;
    m_buildTargetContext->master_css().clear();
    pthread_mutex_unlock(&m_renderMutex);

    addTask({url, HttpTask::TASK_HTML, false});
    return true;
}

bool WidgetWebview::loadCSS(const std::string& url)
{
    std::string full_url = XContainer::normalizeURL(url, "");
    if(full_url.empty()) {
        return false;
    }
    if(hasSeenCSS(full_url)) {
        return false;
    }
    rememberCSS(full_url);
    if(!addTask({full_url, HttpTask::TASK_CSS, false})) {
        forgetCSS(full_url);
        return false;
    }
    return true;
}

bool WidgetWebview::loadHtmlTask(const std::string& url)
{
    HttpResult result = {url, HttpTask::TASK_HTML, false, ""};
    int sz = 0;
    uint64_t fetch_start = kernel_tic_ms(0);
    uint8_t* content = XContainer::loadURL(url, &sz);
    if(content != NULL) {
        if(sz > 0)
            result.content.assign((char*)content, sz);
        else
            result.content = (char*)content;
        free(content);
        result.ok = true;
    }
    klog("[xBrowser] fetched html: url=%s ok=%d size=%d cost=%u ms\n",
        url.c_str(), result.ok ? 1 : 0, sz, (uint32_t)(kernel_tic_ms(0) - fetch_start));
    klog("[xBrowser] html handoff: push begin url=%s\n", url.c_str());
    pushResult(result);
    klog("[xBrowser] html handoff: push done url=%s\n", url.c_str());
    removeTask(url);
    return result.ok;
}

bool WidgetWebview::loadCSSTask(const std::string& url)
{
    HttpResult result = {url, HttpTask::TASK_CSS, false, ""};
    int sz = 0;
    uint64_t fetch_start = kernel_tic_ms(0);
    uint8_t* content = XContainer::loadURL(url, &sz);
    if(content != NULL) {
        if(sz > 0)
            result.content.assign((char*)content, sz);
        else
            result.content = (char*)content;
        free(content);
        result.ok = true;
    }
    pushResult(result);
    removeTask(url);
    klog("[xBrowser] fetched css: url=%s ok=%d size=%d cost=%u ms\n",
        url.c_str(), result.ok ? 1 : 0, sz, (uint32_t)(kernel_tic_ms(0) - fetch_start));
    return result.ok;
}

bool WidgetWebview::loadImageTask(const std::string& url)
{
    HttpResult result = {url, HttpTask::TASK_IMAGE, false, ""};
    int sz = 0;
    uint64_t fetch_start = kernel_tic_ms(0);
    uint8_t* content = XContainer::loadURL(url, &sz);
    if(content != NULL && sz > 0) {
        result.content.assign((char*)content, sz);
        free(content);
        result.ok = true;
    }
    klog("[xBrowser] image result ready: url=%s hash=%08x ok=%d size=%d\n",
        url.c_str(), debug_hash_text(url), result.ok ? 1 : 0, sz);
    pushResult(result);
    removeTask(url);
    klog("[xBrowser] fetched image: ok=%d size=%d cost=%u ms\n",
        result.ok ? 1 : 0, sz, (uint32_t)(kernel_tic_ms(0) - fetch_start));
    return result.ok;
}

bool WidgetWebview::loadCSSContent(const std::string& url, const std::string& content)
{
    bool res = false;
    if (!content.empty()) {
        uint64_t parse_start = kernel_tic_ms(0);
        litehtml::context* ctx = m_activeContext;
        litehtml::document::ptr target_doc = m_doc;
        bool target_build = false;
        pthread_mutex_lock(&m_renderMutex);
        if (m_buildPhase != BUILD_IDLE || m_buildDoc != nullptr || m_defaultCssLoading) {
            ctx = m_buildTargetContext ? m_buildTargetContext : &m_buildContext;
            target_doc = m_buildDoc;
            target_build = true;
        }
        ctx->load_master_stylesheet(content.c_str());
        uint32_t parse_ms = (uint32_t)(kernel_tic_ms(0) - parse_start);
        bool is_default_css = (!m_defaultCSSUrl.empty() && url == m_defaultCSSUrl);
        if (is_default_css) {
            m_defaultCssPrepared = true;
            m_defaultCssLoading = false;
        }
        klog("[xBrowser] parse css: url=%s size=%d cost=%u ms\n", url.c_str(), (int)content.size(), parse_ms);
        if (target_doc) {
            if (target_build) {
                m_buildNeedsStyleUpdate = true;
                m_buildNeedsLayout = true;
                m_buildLayoutDirtyAt = kernel_tic_ms(0);
            } else {
                m_needsStyleUpdate = true;
                m_needsLayout = true;
                m_layoutDirtyAt = kernel_tic_ms(0);
            }
            res = true;
        } else if (is_default_css) {
            if (m_buildPhase == BUILD_PRELOAD_CSS) {
                m_buildPhase = BUILD_CREATE_DOC;
            }
            res = true;
        }
        pthread_mutex_unlock(&m_renderMutex);
    }
    if(res)
        update();
    return res;
}

bool WidgetWebview::loadImageContent(const std::string& url, uint8_t* content, int sz)
{
    bool res = false;
    pthread_mutex_lock(&m_renderMutex);
    if (m_doc == nullptr && m_buildDoc == nullptr) {
        klog("[xBrowser] image content deferred: no-doc url=%s hash=%08x size=%d\n",
            url.c_str(), debug_hash_text(url), sz);
        pthread_mutex_unlock(&m_renderMutex);
        return false;
    }
    XContainer* target_container = m_container;
    litehtml::document::ptr target_doc = m_doc;
    bool target_build = false;
    if (m_buildDoc != nullptr && m_buildContainer != NULL) {
        target_container = m_buildContainer;
        target_doc = m_buildDoc;
        target_build = true;
    }
    klog("[xBrowser] image content target: url=%s hash=%08x build=%d doc=%p container=%p size=%d\n",
        url.c_str(), debug_hash_text(url), target_build ? 1 : 0, target_doc, target_container, sz);
    if (content != NULL && sz > 0 && target_container != NULL && target_doc != nullptr) {
        uint64_t decode_start = kernel_tic_ms(0);
        res = target_container->loadImageData(url, content, sz);
        uint32_t decode_ms = (uint32_t)(kernel_tic_ms(0) - decode_start);
        klog("[xBrowser] decode image: ok=%d size=%d cost=%u ms build=%d\n",
            res ? 1 : 0, sz, decode_ms, target_build ? 1 : 0);
        if (res && target_doc) {
            if (target_build) {
                m_buildNeedsLayout = true;
                m_buildLayoutDirtyAt = kernel_tic_ms(0);
            } else {
                m_needsLayout = true;
                m_layoutDirtyAt = kernel_tic_ms(0);
            }
        }
    }
    pthread_mutex_unlock(&m_renderMutex);
    if(res)
        update();
    else
        klog("[xBrowser] image content failed: url=%s hash=%08x size=%d\n",
            url.c_str(), debug_hash_text(url), sz);
    return res;
}

bool WidgetWebview::loadHtmlContent(const std::string& content)
{
    update();
    pthread_mutex_lock(&m_renderMutex);
    cleanupBuildResources();
    m_buildTargetContext = (m_activeContext == &m_browser_context) ? &m_buildContext : &m_browser_context;
    m_buildTargetContext->master_css().clear();
    int stripped_scripts = 0;
    m_buildHtmlContent = strip_script_blocks(content, &stripped_scripts);
    if(stripped_scripts > 0) {
        klog("[xBrowser] sanitized html: removed_scripts=%d old_size=%d new_size=%d\n",
            stripped_scripts, (int)content.size(), (int)m_buildHtmlContent.size());
    }
    m_buildHtmlUrl = m_currentHtmlUrl;
    if(!m_defaultCSSUrl.empty()) {
        m_buildPhase = BUILD_PRELOAD_CSS;
    } else {
        m_defaultCssPrepared = true;
        m_buildPhase = BUILD_CREATE_DOC;
    }
    pthread_mutex_unlock(&m_renderMutex);

    pthread_mutex_lock(&m_taskMutex);
    m_taskQueue.clear();
    pthread_mutex_unlock(&m_taskMutex);

    if(!m_defaultCSSUrl.empty()) {
        bool queued = false;
        pthread_mutex_lock(&m_renderMutex);
        if(!m_defaultCssPrepared && !m_defaultCssLoading) {
            rememberCSS(m_defaultCSSUrl);
            m_defaultCssLoading = true;
            queued = true;
        }
        pthread_mutex_unlock(&m_renderMutex);
        if(queued) {
            if(!addTask({m_defaultCSSUrl, HttpTask::TASK_CSS, false})) {
                pthread_mutex_lock(&m_renderMutex);
                m_defaultCssLoading = false;
                m_defaultCssPrepared = true;
                if(m_buildPhase == BUILD_PRELOAD_CSS) {
                    m_buildPhase = BUILD_CREATE_DOC;
                }
                pthread_mutex_unlock(&m_renderMutex);
            }
        }
    }

    setBuildStatus("preparing document", 5);
    klog("[xBrowser] build queued: content_size=%d client=%dx%d\n",
        (int)content.size(), m_clientWidth, m_clientHeight);
    update();
    return true;
}

void WidgetWebview::pushResult(const HttpResult& result)
{
    pthread_mutex_lock(&m_resultMutex);
    m_resultQueue.push_back(result);
    if(result.type == HttpTask::TASK_IMAGE) {
        klog("[xBrowser] push image result: queue=%d url=%s hash=%08x ok=%d size=%d\n",
            (int)m_resultQueue.size(), result.url.c_str(), debug_hash_text(result.url),
            result.ok ? 1 : 0, (int)result.content.size());
    }
    pthread_mutex_unlock(&m_resultMutex);
}

bool WidgetWebview::getResult(HttpResult& result)
{
    pthread_mutex_lock(&m_resultMutex);
    if(m_resultQueue.empty()) {
        pthread_mutex_unlock(&m_resultMutex);
        return false;
    }
    result = m_resultQueue.front();
    m_resultQueue.erase(m_resultQueue.begin());
    if(result.type == HttpTask::TASK_IMAGE) {
        klog("[xBrowser] pop image result: remain=%d url=%s hash=%08x ok=%d size=%d\n",
            (int)m_resultQueue.size(), result.url.c_str(), debug_hash_text(result.url),
            result.ok ? 1 : 0, (int)result.content.size());
    }
    pthread_mutex_unlock(&m_resultMutex);
    return true;
}

bool WidgetWebview::processResults()
{
    HttpResult result;
    bool drive_build = false;
    if(!getResult(result)) {
        return false;
    }

    if(result.type == HttpTask::TASK_IMAGE && m_buildPhase != BUILD_IDLE) {
        klog("[xBrowser] process image deferred by build: size=%d phase=%d\n",
            (int)result.content.size(), (int)m_buildPhase);
        pthread_mutex_lock(&m_resultMutex);
        m_resultQueue.insert(m_resultQueue.begin(), result);
        pthread_mutex_unlock(&m_resultMutex);
        return false;
    }
    if(result.type == HttpTask::TASK_CSS &&
            m_buildPhase != BUILD_IDLE &&
            !(m_buildPhase == BUILD_PRELOAD_CSS && result.url == m_defaultCSSUrl)) {
        pthread_mutex_lock(&m_resultMutex);
        m_resultQueue.insert(m_resultQueue.begin(), result);
        pthread_mutex_unlock(&m_resultMutex);
        return false;
    }

    uint64_t process_start = kernel_tic_ms(0);
    klog("[xBrowser] process result: type=%d ok=%d size=%d\n",
        result.type, result.ok ? 1 : 0, (int)result.content.size());
    if(!result.ok) {
        if(result.type == HttpTask::TASK_IMAGE) {
            klog("[xBrowser] image load failed\n");
        }
        if(result.type == HttpTask::TASK_CSS) {
            forgetCSS(result.url);
            if(result.url == m_defaultCSSUrl) {
                pthread_mutex_lock(&m_renderMutex);
                m_defaultCssLoading = false;
                m_defaultCssPrepared = true;
                if(m_buildPhase == BUILD_PRELOAD_CSS) {
                    m_buildPhase = BUILD_CREATE_DOC;
                }
                pthread_mutex_unlock(&m_renderMutex);
                drive_build = true;
            }
        }
    } else if(result.type == HttpTask::TASK_HTML) {
        klog("[xBrowser] html handoff: process begin url=%s\n", result.url.c_str());
        m_currentHtmlUrl = result.url;
        loadHtmlContent(result.content);
        klog("[xBrowser] html handoff: process done url=%s\n", result.url.c_str());
        drive_build = true;
    }
    else if(result.type == HttpTask::TASK_CSS) {
        loadCSSContent(result.url, result.content);
    }
    else if(result.type == HttpTask::TASK_IMAGE) {
        if(!loadImageContent(result.url, (uint8_t*)result.content.data(), result.content.size())) {
            pthread_mutex_lock(&m_renderMutex);
            bool retry_later = (m_doc == nullptr && m_buildDoc == nullptr);
            pthread_mutex_unlock(&m_renderMutex);
            if(retry_later) {
                pthread_mutex_lock(&m_resultMutex);
                m_resultQueue.insert(m_resultQueue.begin(), result);
                pthread_mutex_unlock(&m_resultMutex);
                klog("[xBrowser] image result requeued: url=%s hash=%08x size=%d\n",
                    result.url.c_str(), debug_hash_text(result.url), (int)result.content.size());
            }
        }
    }
    klog("[xBrowser] process result total: type=%d cost=%u ms\n",
        result.type, (uint32_t)(kernel_tic_ms(0) - process_start));

    if(drive_build) {
        m_deferBuildStep = true;
    }

    pthread_mutex_lock(&m_resultMutex);
    bool has_more_results = !m_resultQueue.empty();
    pthread_mutex_unlock(&m_resultMutex);
    return has_more_results;
}

void WidgetWebview::onTimer(uint32_t timerFPS, uint32_t timerSteps)
{
    (void)timerFPS;
    (void)timerSteps;

    // Flush deferred deletions from previous tick (safe - no operations in flight)
    if(m_pendingDeleteDoc) {
        delete m_pendingDeleteDoc;
        m_pendingDeleteDoc = nullptr;
    }
    if(m_pendingDeleteContainer) {
        delete m_pendingDeleteContainer;
        m_pendingDeleteContainer = nullptr;
    }

    bool has_more_results = processResults();
    if (applyPendingLayoutUpdates()) {
        update();
    }
    if (m_buildPhase != BUILD_IDLE) {
        if (m_deferBuildStep) {
            m_deferBuildStep = false;
            if(has_more_results) {
                update();
            }
            return;
        }
        advanceBuildStep();
    }
    if(has_more_results) {
        update();
    }
}

bool WidgetWebview::applyPendingLayoutUpdates()
{
    bool updated = false;
    uint64_t now = kernel_tic_ms(0);
    pthread_mutex_lock(&m_renderMutex);

    if (m_buildDoc) {
        if ((m_buildNeedsStyleUpdate || m_buildNeedsLayout) &&
                m_buildLayoutDirtyAt != 0 &&
                (now - m_buildLayoutDirtyAt) < kLayoutDebounceMs) {
            pthread_mutex_unlock(&m_renderMutex);
            return false;
        }
        if (m_buildNeedsStyleUpdate) {
            uint64_t style_start = kernel_tic_ms(0);
            m_buildDoc->update_master_styles();
            uint32_t style_ms = (uint32_t)(kernel_tic_ms(0) - style_start);
            klog("[xBrowser] apply build styles: %u ms\n", style_ms);
            m_buildNeedsStyleUpdate = false;
            updated = true;
        }
        if (m_buildNeedsLayout) {
            uint64_t render_start = kernel_tic_ms(0);
            m_buildDoc->render(m_clientWidth);
            uint32_t render_ms = (uint32_t)(kernel_tic_ms(0) - render_start);
            klog("[xBrowser] render(build-pending): %u ms\n", render_ms);
            m_buildNeedsLayout = false;
            m_buildLayoutDirtyAt = 0;
            updated = true;
        }
    } else {
        m_buildNeedsStyleUpdate = false;
        m_buildNeedsLayout = false;
        m_buildLayoutDirtyAt = 0;
    }

    if (m_doc) {
        if ((m_needsStyleUpdate || m_needsLayout) &&
                m_layoutDirtyAt != 0 &&
                (now - m_layoutDirtyAt) < kLayoutDebounceMs) {
            pthread_mutex_unlock(&m_renderMutex);
            return updated;
        }
        if (m_needsStyleUpdate) {
            uint32_t text_width_calls = 0, text_width_ms = 0, draw_text_calls = 0, draw_text_ms = 0;
            uint32_t text_width_hits = 0, text_width_misses = 0;
            uint32_t char_width_hits = 0, char_width_misses = 0;
            uint32_t create_font_calls = 0, create_font_ms = 0;
            if (m_container) {
                m_container->resetPerfStats();
            }
            uint64_t style_start = kernel_tic_ms(0);
            m_doc->update_master_styles();
            uint32_t style_ms = (uint32_t)(kernel_tic_ms(0) - style_start);
            if (m_container) {
                m_container->getPerfStats(text_width_calls, text_width_ms, draw_text_calls, draw_text_ms,
                                          text_width_hits, text_width_misses,
                                          char_width_hits, char_width_misses,
                                          create_font_calls, create_font_ms);
            }
            klog("[xBrowser] apply css-update: style=%u ms text_width=%u/%u ms hit=%u miss=%u char_hit=%u char_miss=%u create_font=%u/%u ms\n",
                style_ms, text_width_calls, text_width_ms, text_width_hits, text_width_misses,
                char_width_hits, char_width_misses,
                create_font_calls, create_font_ms);
            m_needsStyleUpdate = false;
            updated = true;
        }
        if (m_needsLayout) {
            uint64_t render_start = kernel_tic_ms(0);
            m_doc->render(m_clientWidth);
            uint32_t layout_ms = (uint32_t)(kernel_tic_ms(0) - render_start);
            klog("[xBrowser] render(pending): %u ms\n", layout_ms);
            m_needsLayout = false;
            m_layoutDirtyAt = 0;
            updated = true;
        }
    } else {
        m_needsStyleUpdate = false;
        m_needsLayout = false;
        m_layoutDirtyAt = 0;
    }

    pthread_mutex_unlock(&m_renderMutex);
    return updated;
}

void WidgetWebview::advanceBuildStep()
{
    if (m_buildPhase == BUILD_IDLE) {
        return;
    }

    if (m_buildPhase == BUILD_PRELOAD_CSS) {
        setBuildStatus("loading styles", 15);
        pthread_mutex_lock(&m_renderMutex);
        bool ready = m_defaultCssPrepared || m_defaultCSSUrl.empty();
        bool waiting = m_defaultCssLoading;
        pthread_mutex_unlock(&m_renderMutex);
        if(ready) {
            m_buildPhase = BUILD_CREATE_DOC;
            update();
        } else if(!waiting) {
            m_buildPhase = BUILD_CREATE_DOC;
            update();
        }
        return;
    }

    if (m_buildPhase == BUILD_CREATE_DOC) {
        setBuildStatus("building document", 45);
        pthread_mutex_lock(&m_renderMutex);
        if(!m_buildTargetContext) {
            m_buildTargetContext = (m_activeContext == &m_browser_context) ? &m_buildContext : &m_browser_context;
            m_buildTargetContext->master_css().clear();
        }
        if (m_buildContainer) {
            delete m_buildContainer;
            m_buildContainer = nullptr;
        }
        if (m_buildDoc) {
            delete m_buildDoc;
            m_buildDoc = nullptr;
        }
        litehtml::context* build_ctx = m_buildTargetContext ? m_buildTargetContext : &m_buildContext;
        build_ctx->set_fast_mode(true);
        m_buildContainer = new XContainer(build_ctx, this);
        m_buildContainer->set_client_size(m_clientWidth, m_clientHeight);
        m_buildContainer->setDeferImageLoad(true);
        m_buildContainer->resetPerfStats();
        if(m_buildHtmlContent.empty()) {
            klog("[xBrowser] BUILD_CREATE_DOC: empty content, aborting build\n");
            pthread_mutex_unlock(&m_renderMutex);
            m_buildPhase = BUILD_FAILED;
            setBuildStatus("no content to render", 100);
            update();
            return;
        }
        uint64_t create_start = kernel_tic_ms(0);
        m_buildDoc = litehtml::document::createFromString(m_buildHtmlContent.c_str(), m_buildContainer, build_ctx);
        uint32_t create_ms = (uint32_t)(kernel_tic_ms(0) - create_start);
        if (!m_buildDoc) {
            pthread_mutex_unlock(&m_renderMutex);
            m_buildPhase = BUILD_FAILED;
            setBuildStatus("document build failed", 100);
            update();
            return;
        }
        klog("[xBrowser] create dom: %u ms\n", create_ms);
        pthread_mutex_unlock(&m_renderMutex);
        m_buildPhase = BUILD_RENDER_DOC;
        update();
        return;
    }

    if (m_buildPhase == BUILD_RENDER_DOC) {
        setBuildStatus("layout and first paint", 80);
        pthread_mutex_lock(&m_renderMutex);
        if (m_buildDoc) {
            uint64_t render_start = kernel_tic_ms(0);
            m_buildDoc->render(m_clientWidth);
            uint32_t render_ms = (uint32_t)(kernel_tic_ms(0) - render_start);
            uint32_t text_width_calls = 0, text_width_ms = 0, draw_text_calls = 0, draw_text_ms = 0;
            uint32_t text_width_hits = 0, text_width_misses = 0;
            uint32_t char_width_hits = 0, char_width_misses = 0;
            uint32_t create_font_calls = 0, create_font_ms = 0;
            m_buildContainer->getPerfStats(text_width_calls, text_width_ms, draw_text_calls, draw_text_ms,
                                          text_width_hits, text_width_misses,
                                          char_width_hits, char_width_misses,
                                          create_font_calls, create_font_ms);
            klog("[xBrowser] doc ready: width=%d height=%d\n", m_buildDoc->width(), m_buildDoc->height());
            klog("[xBrowser] render(initial): %u ms\n", render_ms);
            klog("[xBrowser] build perf: text_width=%u/%u ms hit=%u miss=%u char_hit=%u char_miss=%u create_font=%u/%u ms\n",
                text_width_calls, text_width_ms, text_width_hits, text_width_misses,
                char_width_hits, char_width_misses,
                create_font_calls, create_font_ms);
        }
        pthread_mutex_unlock(&m_renderMutex);
        m_buildPhase = BUILD_SWAP_DOC;
        update();
        return;
    }

    if (m_buildPhase == BUILD_SWAP_DOC) {
        setBuildStatus("displaying page", 100);
        pthread_mutex_lock(&m_renderMutex);
        // Save old pointers
        XContainer* old_container = m_container;
        litehtml::document::ptr old_doc = m_doc;

        // Install new pointers atomically (while lock is held)
        m_doc = m_buildDoc;
        m_container = m_buildContainer;
        m_buildDoc = nullptr;
        m_buildContainer = nullptr;

        if (m_buildTargetContext) {
            m_activeContext = m_buildTargetContext;
            m_activeContext->set_fast_mode(false);
        }
        if(m_container) {
            m_container->setDeferImageLoad(false);
        }
        m_buildHtmlContent.clear();
        m_buildHtmlUrl.clear();
        m_buildPhase = BUILD_IDLE;
        m_defaultCssPrepared = false;
        m_defaultCssLoading = false;
        m_buildTargetContext = nullptr;
        m_flushDeferredImages = true;
        m_scrollX = 0;
        m_scrollY = 0;
        pthread_mutex_unlock(&m_renderMutex);

        // Defer deletion to next timer tick to prevent re-entrant corruption
        if(m_pendingDeleteDoc) delete m_pendingDeleteDoc;
        if(m_pendingDeleteContainer) delete m_pendingDeleteContainer;
        m_pendingDeleteDoc = old_doc;
        m_pendingDeleteContainer = old_container;

        updateScroller();
        onBuildStatus("", 0);
        update();
        return;
    }

    if (m_buildPhase == BUILD_FAILED) {
        pthread_mutex_lock(&m_renderMutex);
        cleanupBuildResources();
        pthread_mutex_unlock(&m_renderMutex);
        update();
    }
}

void WidgetWebview::onRepaint(graph_t* g, XTheme* theme, const grect_t& r)
{
    (void)theme;
    static int repaint_log_count = 0;

    if (g == NULL)
        return;

    // Clear background to white
    graph_fill_rect(g, r.x, r.y, r.w, r.h, 0xFFFFFFFF);

    litehtml::position pos(r.x, r.y, r.w, r.h);
    bool show_build_overlay = false;
    bool has_doc = false;
    bool flush_deferred_images = false;
    std::string build_status;
    int build_progress = 0;
    XContainer* deferred_image_container = nullptr;

    pthread_mutex_lock(&m_renderMutex);
    if (m_container) {
        m_container->setGraph(g);
        m_container->resetPerfStats();
    }
    if (m_doc) {
        clampScrollLocked(m_doc->width(), m_doc->height());
    } else {
        m_scrollX = 0;
        m_scrollY = 0;
    }
    if (repaint_log_count < 8) {
        int doc_width = -1;
        int doc_height = -1;
        if (m_doc) {
            doc_width = m_doc->width();
            doc_height = m_doc->height();
        }
        klog("[xBrowser] repaint: area=%dx%d doc=%p doc_size=%dx%d scroll=%d,%d\n",
            r.w, r.h, m_doc, doc_width, doc_height, m_scrollX, m_scrollY);
        repaint_log_count++;
    }
    if (m_doc) {
        has_doc = true;
        uint64_t draw_start = kernel_tic_ms(0);
        m_doc->draw((litehtml::uint_ptr)g, r.x - m_scrollX, r.y - m_scrollY, &pos);
        uint32_t draw_ms = (uint32_t)(kernel_tic_ms(0) - draw_start);
        if (m_container != NULL && (draw_ms >= 20 || repaint_log_count <= 8)) {
            uint32_t text_width_calls = 0, text_width_ms = 0, draw_text_calls = 0, draw_text_ms = 0;
            uint32_t text_width_hits = 0, text_width_misses = 0;
            uint32_t char_width_hits = 0, char_width_misses = 0;
            uint32_t create_font_calls = 0, create_font_ms = 0;
            m_container->getPerfStats(text_width_calls, text_width_ms, draw_text_calls, draw_text_ms,
                                      text_width_hits, text_width_misses,
                                      char_width_hits, char_width_misses,
                                      create_font_calls, create_font_ms);
            klog("[xBrowser] draw: %u ms text_width=%u/%u ms hit=%u miss=%u char_hit=%u char_miss=%u draw_text=%u/%u ms create_font=%u/%u ms\n",
                draw_ms, text_width_calls, text_width_ms, text_width_hits, text_width_misses,
                char_width_hits, char_width_misses,
                draw_text_calls, draw_text_ms, create_font_calls, create_font_ms);
        }
    }
    if (m_buildPhase != BUILD_IDLE) {
        show_build_overlay = true;
        build_status = m_buildStatus;
        build_progress = m_buildProgress;
    }
    if (m_flushDeferredImages && m_doc && m_container) {
        deferred_image_container = m_container;
        m_flushDeferredImages = false;
        flush_deferred_images = true;
    }
    pthread_mutex_unlock(&m_renderMutex);

    if (show_build_overlay) {
        font_t* font = theme ? theme->getFont() : nullptr;
        uint32_t fg = theme ? theme->basic.docFGColor : 0xFF000000;
        uint32_t bg = 0xFFE8E8E8;
        int box_w = has_doc ? (r.w / 3) : 260;
        if (box_w < 220) box_w = 220;
        int box_h = 48;
        int box_x = r.x + (r.w - box_w) / 2;
        int box_y = r.y + (r.h - box_h) / 2;
        graph_fill_rect(g, box_x, box_y, box_w, box_h, bg);
        graph_rect(g, box_x, box_y, box_w, box_h, 0xFF808080);
        int bar_x = box_x + 8;
        int bar_y = box_y + box_h - 14;
        int bar_w = box_w - 16;
        graph_rect(g, bar_x, bar_y, bar_w, 8, 0xFF909090);
        int fill_w = (bar_w - 2) * build_progress / 100;
        if (fill_w < 0) fill_w = 0;
        graph_fill_rect(g, bar_x + 1, bar_y + 1, fill_w, 6, 0xFF4A90E2);
        if (font != NULL && !build_status.empty()) {
            int text_y = box_y + 8;
            graph_draw_text_font(g, box_x + 8, text_y, build_status.c_str(), font,
                theme ? theme->basic.fontSize : 16, fg);
        }
    }

    if (flush_deferred_images && deferred_image_container) {
        pthread_mutex_lock(&m_renderMutex);
        if(m_container == deferred_image_container) {  // Still valid?
            deferred_image_container->flushPendingImages();
        }
        pthread_mutex_unlock(&m_renderMutex);
    }
}

void WidgetWebview::onResize()
{
    m_clientWidth = area.w;
    m_clientHeight = area.h;
    dragStep = area.h / 4;

    pthread_mutex_lock(&m_renderMutex);
    if (m_container) {
        m_container->set_client_size(m_clientWidth, m_clientHeight);
    }
    if (m_doc) {
        uint64_t render_start = kernel_tic_ms(0);
        m_doc->render(m_clientWidth);
        uint32_t render_ms = (uint32_t)(kernel_tic_ms(0) - render_start);
        klog("[xBrowser] render(resize): %u ms\n", render_ms);
    }
    pthread_mutex_unlock(&m_renderMutex);

    updateScroller();
}

bool WidgetWebview::onScroll(int step, bool horizontal)
{
    pthread_mutex_lock(&m_renderMutex);
    if (!m_doc) {
        pthread_mutex_unlock(&m_renderMutex);
        return false;
    }

    int docWidth = m_doc->width();
    int docHeight = m_doc->height();
    pthread_mutex_unlock(&m_renderMutex);

    if (horizontal) {
        m_scrollX -= step * dragStep;
        if (m_scrollX < 0 || (docWidth - area.w) < 0)
            m_scrollX = 0;
        else if (m_scrollX > (docWidth - area.w))
            m_scrollX = docWidth - area.w;
    } else {
        m_scrollY -= step * dragStep;
        if (m_scrollY < 0 || (docHeight - area.h) < 0)
            m_scrollY = 0;
        else if (m_scrollY > (docHeight - area.h))
            m_scrollY = docHeight - area.h;
    }
    return true;
}

void WidgetWebview::updateScroller()
{
	pthread_mutex_lock(&m_renderMutex);
	if (!m_doc) {
		pthread_mutex_unlock(&m_renderMutex);
		return;
	}

	int docWidth = m_doc->width();
	int docHeight = m_doc->height();
    clampScrollLocked(docWidth, docHeight);
    int scrollX = m_scrollX;
    int scrollY = m_scrollY;
	pthread_mutex_unlock(&m_renderMutex);

	setScrollerInfo(docWidth, scrollX, area.w, true);
	setScrollerInfo(docHeight, scrollY, area.h, false);
}

bool WidgetWebview::onMouse(xevent_t* ev)
{
	// Call parent class onMouse for drag scrolling
	bool handled = Scrollable::onMouse(ev);
	if (handled)
		return true;

	// Handle mouse wheel scrolling
	if (ev->state == MOUSE_STATE_MOVE) {
		if (ev->value.mouse.button == MOUSE_BUTTON_SCROLL_UP) {
			scroll(-1, false);
			return true;
		}
		else if (ev->value.mouse.button == MOUSE_BUTTON_SCROLL_DOWN) {
			scroll(1, false);
			return true;
		}
	}
	return false;
}

void WidgetWebview::setAttr(const string& attr, json_var_t*value) {
	Scrollable::setAttr(attr, value);
	if(attr == "url") {
		const char* url = json_var_get_str(value);
		loadHtml(url);
	}	
    else if(attr == "css") {
		const char* css = json_var_get_str(value);
		loadCSS(css);
	}
}
