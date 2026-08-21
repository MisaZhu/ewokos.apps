// Main entry point using widget++

#include <x++/X.h>
#include <Widget/WidgetWin.h>
#include <Widget/RootWidget.h>
#include <Widget/WidgetX.h>
#include <Widget/EditLine.h>
#include <Widget/Label.h>
#include <Widget/Scroller.h>
#include <Widget/Container.h>
#include <ewoksys/keydef.h>


#include "WidgetWebview/WidgetWebview.h"

using namespace Ewok;
class StatusBar: public Label {
protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
		graph_fill_3d(g, r.x, r.y, r.w, r.h, theme->basic.docBGColor, true);
		font_t* font = theme->getFont();
		int y = r.y + (r.h-font_get_height(font, theme->basic.fontSize))/2;
		graph_draw_text_font(g, r.x+4, y, label.c_str(), font, theme->basic.fontSize, theme->basic.docFGColor);
	}
public:
	StatusBar(const std::string& label) : Label(label) {}
};

class BrowserWidget: public WidgetWebview {
public:
    BrowserWidget() : WidgetWebview() {
        statusBar = nullptr;
        m_statusDirty = false;
        m_initialLoadStarted = false;
        pthread_mutex_init(&m_statusMutex, NULL);
    }

    ~BrowserWidget() {
        pthread_mutex_destroy(&m_statusMutex);
    }

    void setStatusBar(StatusBar* statusBar) {
        this->statusBar = statusBar;
    }

    void setInitialUrl(const std::string& url) {
        m_initialUrl = url;
    }
protected:
    StatusBar* statusBar;
    std::string m_pendingStatus;
    std::string m_initialUrl;
    pthread_mutex_t m_statusMutex;
    bool m_statusDirty;
    bool m_initialLoadStarted;

    void queueStatus(const std::string& msg) {
        pthread_mutex_lock(&m_statusMutex);
        m_pendingStatus = msg;
        m_statusDirty = true;
        pthread_mutex_unlock(&m_statusMutex);
    }

    void onTaskStart(const HttpTask& task) override {
        queueStatus("loading " + task.url);
    }
    void onTaskEnd(const HttpTask& task) override {
        queueStatus(task.url + " loaded");
    }
    void onTaskFailed(const HttpTask& task) override {
        queueStatus(task.url + " failed");
    }

    void onTasksEnd() override {
        queueStatus("");
    }

    void onBuildStatus(const std::string& status, int progress) override {
        if(status.empty()) {
            queueStatus("");
            return;
        }
        queueStatus(status + " (" + std::to_string(progress) + "%)");
    }

    void onTimer(uint32_t timerFPS, uint32_t timerSteps) override {
        WidgetWebview::onTimer(timerFPS, timerSteps);
        (void)timerFPS;
        (void)timerSteps;

        if(!m_initialLoadStarted && !m_initialUrl.empty()) {
            m_initialLoadStarted = true;
            loadHtml(m_initialUrl);
        }

        if(statusBar == nullptr)
            return;

        std::string msg;
        bool dirty = false;
        pthread_mutex_lock(&m_statusMutex);
        if(m_statusDirty) {
            msg = m_pendingStatus;
            m_statusDirty = false;
            dirty = true;
        }
        pthread_mutex_unlock(&m_statusMutex);

        if(dirty) {
            statusBar->setLabel(msg);
        }
    }
};

static void onInputFunc(Widget* wd, uint32_t key, void* arg) {
    WidgetWebview* webview = (WidgetWebview*)arg;
    EditLine* editline = (EditLine*)wd;
    if(webview == NULL || editline->getContent().empty())
        return;
    if(key != KEY_ENTER)
        return;
    std::string full_url = editline->getContent();
    if(full_url.find("://") == std::string::npos) {
        full_url = "https://" + editline->getContent();
        editline->setContent(full_url);
    }

    webview->loadHtml(full_url);
}

int main(int argc, char* argv[])
{
    X x;
    WidgetWin win;
    RootWidget* root = new RootWidget();
    win.setRoot(root);
    root->setType(Container::VERTICAL);

    EditLine* editline = new EditLine();
    root->add(editline);
    editline->fix(0, 24);
    root->focus(editline);

    // Create a horizontal container for webview and vertical scroller
    Container* c = new Container();
    c->setType(Container::HORIZONTAL);
    root->add(c);

    BrowserWidget* webview = new BrowserWidget();
    c->add(webview);

    editline->setOnInputFunc(onInputFunc, webview);

    // Add vertical scroller
    Scroller* sr = new Scroller();
    sr->fix(8, 0);
    webview->setScrollerV(sr);
    c->add(sr);

    editline->setOnInputFunc(onInputFunc, webview);

    win.open(&x, -1, -1, -1, 0, 0, "HTML Browser", XWIN_STYLE_NORMAL);

    webview->setDefaultCSS("res://html/default.css");
    std::string initialUrl;
    if(argc > 1) {
        editline->disable();
        editline->setContent(argv[1]);
        initialUrl = argv[1];
    }
    else {
        editline->enable();
        initialUrl = "res://html/default.html";
    }

    StatusBar* statusBar = new StatusBar("");
    webview->setStatusBar(statusBar);
    webview->setInitialUrl(initialUrl);
    statusBar->fix(0, 24);
    root->add(statusBar);

    widgetXRun(&x, &win);
    return 0;
}
