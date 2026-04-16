#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <Widget/LabelButton.h>
#include <Widget/Image.h>
#include <Widget/Container.h>
#include <Widget/Blank.h>
#include <WidgetEx/ColorDialog.h>
#include <x++/X.h>
#include <graph/graph.h>
#include <ewoksys/keydef.h>
#include <ewoksys/basic_math.h>
#include <cmath>
#include <algorithm>

using namespace Ewok;

class ColorButton : public LabelButton {
private:
    uint32_t color;
    bool selected;

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        // 绘制按钮背景
        if(selected)
            graph_fill_round(g, r.x, r.y, r.w, r.h, 4, theme->basic.selectBGColor);
        else
            graph_fill_round(g, r.x, r.y, r.w, r.h, 4, theme->basic.bgColor);

        // 绘制颜色指示器
        int colorSize = r.h / 2;
        int colorX = r.x + (r.w - colorSize) / 2;
        int colorY = r.y + (r.h - colorSize) / 2;
        graph_fill_round(g, colorX, colorY, colorSize, colorSize, 2, color);

        // 绘制边框
        graph_box(g, r.x, r.y, r.w, r.h, theme->basic.fgColor);
    }

public:
    ColorButton(uint32_t c) : LabelButton(""), color(c), selected(false) {
        setMarginH(4);
        setMarginV(4);
    }

    void setSelected(bool s) {
        selected = s;
        update();
    }

    uint32_t getColor() {
        return color;
    }
};

class ToolButton : public LabelButton {
private:
    bool selected;

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        // 绘制按钮背景
        if(selected)
            graph_fill_round(g, r.x, r.y, r.w, r.h, 4, theme->basic.selectBGColor);
        else
            graph_fill_round(g, r.x, r.y, r.w, r.h, 4, theme->basic.bgColor);

        // 绘制文本
        const string& text = getLabel();
        uint32_t tw, th;
        font_text_size(text.c_str(), theme->getFont(), theme->basic.fontSize, &tw, &th);
        int textX = r.x + (r.w - (int32_t)tw) / 2;
        int textY = r.y + (r.h - (int32_t)th) / 2;
        graph_draw_text_font(g, textX, textY, text.c_str(), theme->getFont(), theme->basic.fontSize, theme->basic.fgColor);

        // 绘制边框
        graph_box(g, r.x, r.y, r.w, r.h, theme->basic.fgColor);
    }

public:
    ToolButton(const char* label) : LabelButton(label), selected(false) {
        setMarginH(4);
        setMarginV(4);
    }

    void setSelected(bool s) {
        selected = s;
        update();
    }
};

class DrawArea : public Widget {
public:
    enum ToolType {
        TOOL_PEN,
        TOOL_LINE,
        TOOL_RECT,
        TOOL_ROUND_RECT,
        TOOL_CIRCLE,
        TOOL_FILL_CIRCLE,
        TOOL_FILL_RECT,
        TOOL_FILL_ROUND_RECT
    };

private:
    bool isDrawing;
    int lastX, lastY;
    uint32_t color;
    int penSize;
    graph_t* canvas;
    graph_t* backupCanvas;
    ToolType toolType;

public:
    DrawArea() : Widget() {
        isDrawing = false;
        color = 0xff000000; // 黑色
        penSize = 2;
        canvas = NULL;
        backupCanvas = NULL;
        toolType = TOOL_PEN;
    }

    ~DrawArea() {
        if(canvas != NULL)
            graph_free(canvas);
        if(backupCanvas != NULL)
            graph_free(backupCanvas);
    }

    void setColor(uint32_t c) {
        color = c;
    }

    void setPenSize(int size) {
        penSize = size;
    }

    void setToolType(ToolType type) {
        toolType = type;
    }

    uint32_t getColor() {
        return color;
    }

    void clear() {
        if(canvas != NULL) {
            graph_free(canvas);
        }
        if(backupCanvas != NULL) {
            graph_free(backupCanvas);
            backupCanvas = NULL;
        }
        canvas = graph_new(NULL, area.w, area.h);
        graph_fill(canvas, 0, 0, area.w, area.h, 0xffffffff); // 白色背景
        update();
    }

protected:
    void onResize() {
        if(canvas != NULL) {
            graph_free(canvas);
        }
        if(backupCanvas != NULL) {
            graph_free(backupCanvas);
            backupCanvas = NULL;
        }
        canvas = graph_new(NULL, area.w, area.h);
        graph_fill(canvas, 0, 0, area.w, area.h, 0xffffffff); // 白色背景
    }

    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        if(canvas != NULL) {
            graph_blt(canvas, 0, 0, canvas->w, canvas->h, g, r.x, r.y, r.w, r.h);
        }
    }

    bool onMouse(xevent_t* ev) {
        // 将屏幕绝对坐标转换为画布内相对坐标
        gpos_t pos;
        pos = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);
        int x = pos.x;
        int y = pos.y;

        // 边界检查
        if(canvas != NULL) {
            if(x < 0) x = 0;
            if(y < 0) y = 0;
            if(x >= canvas->w) x = canvas->w - 1;
            if(y >= canvas->h) y = canvas->h - 1;
        }

        if(ev->state == MOUSE_STATE_DOWN) {
            isDrawing = true;
            lastX = x;
            lastY = y;
            // 创建备份画布用于预览
            if(backupCanvas != NULL) {
                graph_free(backupCanvas);
                backupCanvas = NULL;
            }
            if(canvas != NULL && canvas->w > 0 && canvas->h > 0) {
                backupCanvas = graph_dup(canvas);
            }
        }
        else if(ev->state == MOUSE_STATE_DRAG && isDrawing) {
            if(canvas != NULL) {
                // Pen工具直接绘制，不恢复备份
                if(toolType == TOOL_PEN) {
                    if(lastX >= 0 && lastY >= 0 && lastX < canvas->w && lastY < canvas->h) {
                        graph_line(canvas, lastX, lastY, x, y, color);
                    }
                    lastX = x;
                    lastY = y;
                    update();
                }
                // 其他工具需要恢复备份并绘制预览
                else if(backupCanvas != NULL && 
                        canvas->w == backupCanvas->w && canvas->h == backupCanvas->h) {
                    // 恢复备份
                    graph_blt(backupCanvas, 0, 0, backupCanvas->w, backupCanvas->h, canvas, 0, 0, canvas->w, canvas->h);
                    
                    // 根据当前工具类型执行预览绘制
                switch(toolType) {
                case TOOL_PEN:
                    // Pen工具已在外面处理
                    break;
                case TOOL_LINE:
                    graph_wline(canvas, lastX, lastY, x, y, 5, color);
                    break;
                case TOOL_RECT:
                    graph_box(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), color);
                    break;
                case TOOL_ROUND_RECT:
                    graph_round(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), 18, penSize, color);
                    break;
                case TOOL_CIRCLE: {
                    int radius = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2));
                    graph_circle(canvas, lastX, lastY, radius, penSize, color);
                    break;
                }
                case TOOL_FILL_CIRCLE: {
                    int radius = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2));
                    graph_fill_circle(canvas, lastX, lastY, radius, color);
                    break;
                }
                case TOOL_FILL_RECT:
                    graph_fill(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), color);
                    break;
                case TOOL_FILL_ROUND_RECT:
                    graph_fill_round(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), 18, color);
                    break;
                    }
                    update();
                }
            }
        }
        else if(ev->state == MOUSE_STATE_UP) {
            if(canvas != NULL && isDrawing) {
                // Pen工具不需要恢复备份，直接保留绘制内容
                // 其他工具需要恢复备份并执行最终绘制
                if(toolType != TOOL_PEN) {
                    // 恢复备份，确保最终绘制在原始画布上
                    if(backupCanvas != NULL && 
                       canvas->w == backupCanvas->w && canvas->h == backupCanvas->h) {
                        graph_blt(backupCanvas, 0, 0, backupCanvas->w, backupCanvas->h, canvas, 0, 0, canvas->w, canvas->h);
                    }
                    
                    // 根据当前工具类型执行最终的绘图操作
                switch(toolType) {
                case TOOL_PEN:
                    break;
                case TOOL_LINE:
                    graph_wline(canvas, lastX, lastY, x, y, 5, color);
                    break;
                case TOOL_RECT:
                    graph_box(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), color);
                    break;
                case TOOL_ROUND_RECT:
                    graph_round(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), 18, penSize, color);
                    break;
                case TOOL_CIRCLE: {
                    int radius = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2));
                    graph_circle(canvas, lastX, lastY, radius, penSize, color);
                    break;
                }
                case TOOL_FILL_CIRCLE: {
                    int radius = sqrt(pow(x - lastX, 2) + pow(y - lastY, 2));
                    graph_fill_circle(canvas, lastX, lastY, radius, color);
                    break;
                }
                case TOOL_FILL_RECT:
                    graph_fill(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), color);
                    break;
                case TOOL_FILL_ROUND_RECT:
                    graph_fill_round(canvas, std::min(lastX, x), std::min(lastY, y), std::abs(x - lastX), std::abs(y - lastY), 18, color);
                    break;
                    }
                    update();
                }
                // Pen工具也需要更新显示
                else if(toolType == TOOL_PEN) {
                    update();
                }
            }
            isDrawing = false;
            // 清理备份画布
            if(backupCanvas != NULL) {
                graph_free(backupCanvas);
                backupCanvas = NULL;
            }
        }
        return true;
    }
};

class xDraw : public WidgetWin {
private:
    DrawArea* drawArea;
    ToolButton* btnPen;
    ToolButton* btnLine;
    ToolButton* btnRect;
    ToolButton* btnRoundRect;
    ToolButton* btnCircle;
    ToolButton* btnFillCircle;
    ToolButton* btnFillRect;
    ToolButton* btnFillRoundRect;
    ToolButton* btnEraser;
    ToolButton* btnColor;
    ToolButton* btnClear;
    ToolButton* selectedToolBtn;
    ColorDialog* colorDialog;

    void resetToolButtons() {
        if(selectedToolBtn != NULL)
            selectedToolBtn->setSelected(false);
    }

public:
    xDraw() : WidgetWin() {
        RootWidget* root = new RootWidget();
        setRoot(root);
        root->setType(Container::HORIZONTAL);

        // 创建工具栏
        Container* toolbar = new Container();
        toolbar->setType(Container::VERTICAL);
        toolbar->fix(80, 0);
        root->add(toolbar);

        // 添加颜色选择按钮
        btnColor = new ToolButton("Color");
        btnColor->setEventFunc(onColorBtnClick, this);
        toolbar->add(btnColor);

        // 添加分隔线
        Blank* separator1 = new Blank();
        separator1->fix(0, 10);
        toolbar->add(separator1);

        // 创建绘图工具按钮
        btnPen = new ToolButton("Pen");
        btnPen->setEventFunc(onToolClick, this);
        toolbar->add(btnPen);

        btnLine = new ToolButton("Line");
        btnLine->setEventFunc(onToolClick, this);
        toolbar->add(btnLine);

        btnRect = new ToolButton("Rect");
        btnRect->setEventFunc(onToolClick, this);
        toolbar->add(btnRect);

        btnRoundRect = new ToolButton("Round");
        btnRoundRect->setEventFunc(onToolClick, this);
        toolbar->add(btnRoundRect);

        btnCircle = new ToolButton("Circle");
        btnCircle->setEventFunc(onToolClick, this);
        toolbar->add(btnCircle);

        btnFillCircle = new ToolButton("FCircle");
        btnFillCircle->setEventFunc(onToolClick, this);
        toolbar->add(btnFillCircle);

        btnFillRect = new ToolButton("FRect");
        btnFillRect->setEventFunc(onToolClick, this);
        toolbar->add(btnFillRect);

        btnFillRoundRect = new ToolButton("FRound");
        btnFillRoundRect->setEventFunc(onToolClick, this);
        toolbar->add(btnFillRoundRect);

        // 添加分隔线
        Blank* separator2 = new Blank();
        separator2->fix(0, 10);
        toolbar->add(separator2);

        // 创建工具按钮
        btnEraser = new ToolButton("Eraser");
        btnEraser->setEventFunc(onEraserClick, this);
        toolbar->add(btnEraser);

        btnClear = new ToolButton("Clear");
        btnClear->setEventFunc(onClearClick, this);
        toolbar->add(btnClear);

        // 创建绘图区
        drawArea = new DrawArea();
        root->add(drawArea);
        root->focus(drawArea);

        // 默认选择黑色和画笔工具
        selectedToolBtn = btnPen;
        selectedToolBtn->setSelected(true);
        drawArea->setToolType(DrawArea::TOOL_PEN);
        colorDialog = NULL;
    }

    ~xDraw() {
        if(colorDialog != NULL)
            delete colorDialog;
    }

    void onDialoged(XWin* from, int res, void* arg) override {
        if(from == colorDialog) {
            if(res == Dialog::RES_OK) {
                ColorDialog* colorDlg = (ColorDialog*)from;
                drawArea->setColor(colorDlg->getColor());
            }
            //delete colorDialog;
            //colorDialog = NULL;
        }
    }

    static void onColorBtnClick(Widget* wd, xevent_t* evt, void* data) {
        xDraw* self = (xDraw*)data;
        if(evt->state == MOUSE_STATE_DOWN) {
            // 打开颜色对话框
            if(self->colorDialog == NULL) {
                self->colorDialog = new ColorDialog();
            }
            self->colorDialog->setColor(self->drawArea->getColor());
            self->colorDialog->popup((XWin*)self, 300, 200, "Color", XWIN_STYLE_NORMAL);
        }
    }

    static void onToolClick(Widget* wd, xevent_t* evt, void* data) {
        xDraw* self = (xDraw*)data;
        if(evt->state == MOUSE_STATE_DOWN) {
            self->resetToolButtons();
            ToolButton* btn = (ToolButton*)wd;
            btn->setSelected(true);
            self->selectedToolBtn = btn;

            // 设置工具类型
            if(btn == self->btnPen)
                self->drawArea->setToolType(DrawArea::TOOL_PEN);
            else if(btn == self->btnLine)
                self->drawArea->setToolType(DrawArea::TOOL_LINE);
            else if(btn == self->btnRect)
                self->drawArea->setToolType(DrawArea::TOOL_RECT);
            else if(btn == self->btnRoundRect)
                self->drawArea->setToolType(DrawArea::TOOL_ROUND_RECT);
            else if(btn == self->btnCircle)
                self->drawArea->setToolType(DrawArea::TOOL_CIRCLE);
            else if(btn == self->btnFillCircle)
                self->drawArea->setToolType(DrawArea::TOOL_FILL_CIRCLE);
            else if(btn == self->btnFillRect)
                self->drawArea->setToolType(DrawArea::TOOL_FILL_RECT);
            else if(btn == self->btnFillRoundRect)
                self->drawArea->setToolType(DrawArea::TOOL_FILL_ROUND_RECT);

            self->drawArea->setPenSize(2); // 重置笔刷大小
        }
    }

    static void onEraserClick(Widget* wd, xevent_t* evt, void* data) {
        xDraw* self = (xDraw*)data;
        if(evt->state == MOUSE_STATE_DOWN) {
            self->resetToolButtons();
            ToolButton* btn = (ToolButton*)wd;
            btn->setSelected(true);
            self->selectedToolBtn = btn;
            self->drawArea->setColor(0xffffffff); // 白色作为橡皮擦
            self->drawArea->setPenSize(10);
        }
    }

    static void onClearClick(Widget* wd, xevent_t* evt, void* data) {
        xDraw* self = (xDraw*)data;
        if(evt->state == MOUSE_STATE_DOWN) {
            self->drawArea->clear();
        }
    }
};

int main(int argc, char** argv) {
    X x;
    xDraw win;

    grect_t desk;
    x.getDesktopSpace(desk, 0);

    grect_t wr;
    wr.x = 100;
    wr.y = 100;
    wr.w = desk.w - 200;
    wr.h = desk.h - 200;

    win.open(&x, -1, wr, "xDraw", XWIN_STYLE_NORMAL);
    widgetXRun(&x, &win);
    return 0;
}

