#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <x++/X.h>
#include <unistd.h>
#include <stdlib.h>
#include <font/font.h>
#include <graph/graph_png.h>
#include <ewoksys/ewokdef.h>
#include <ewoksys/sys.h>
#include <ewoksys/session.h>
#include <ewoksys/syscall.h>
#include <procinfo.h>
#include <openlibm.h>

using namespace Ewok;

class Calculator : public Widget {
private:
    char displayText[256];
    double currentValue;
    double previousValue;
    char operation;
    bool isNewInput;
    bool isScientificMode;

public:
    inline Calculator() {
        reset();
        isScientificMode = false;
    }

    inline ~Calculator() {
    }

    void reset() {
        strcpy(displayText, "0");
        currentValue = 0;
        previousValue = 0;
        operation = 0;
        isNewInput = true;
    }

    void toggleMode() {
        isScientificMode = !isScientificMode;
        update();
    }

    void inputDigit(char digit) {
        if (isNewInput) {
            displayText[0] = digit;
            displayText[1] = '\0';
            isNewInput = false;
        } else {
            int len = strlen(displayText);
            if (len < 20) { // 限制显示长度
                displayText[len] = digit;
                displayText[len + 1] = '\0';
            }
        }
        update();
    }

    void inputOperator(char op) {
        if (operation != 0 && !isNewInput) {
            calculate();
        }
        previousValue = atof(displayText);
        operation = op;
        isNewInput = true;
    }

    void calculate() {
        if (operation == 0) return;

        currentValue = atof(displayText);
        switch (operation) {
            case '+':
                currentValue = previousValue + currentValue;
                break;
            case '-':
                currentValue = previousValue - currentValue;
                break;
            case '*':
                currentValue = previousValue * currentValue;
                break;
            case '/':
                if (currentValue != 0) {
                    currentValue = previousValue / currentValue;
                } else {
                    strcpy(displayText, "Error");
                    update();
                    return;
                }
                break;
            case '%':
                if (currentValue != 0) {
                    currentValue = fmod(previousValue, currentValue);
                } else {
                    strcpy(displayText, "Error");
                    update();
                    return;
                }
                break;
        }
        
        // 格式化结果
        if (currentValue == (int)currentValue) {
            sprintf(displayText, "%d", (int)currentValue);
        } else {
            sprintf(displayText, "%.8g", currentValue);
        }
        
        operation = 0;
        isNewInput = true;
        update();
    }

    void inputFunction(const char* func) {
        currentValue = atof(displayText);
        
        if (strcmp(func, "sin") == 0) {
            currentValue = sin(currentValue);
        } else if (strcmp(func, "cos") == 0) {
            currentValue = cos(currentValue);
        } else if (strcmp(func, "tan") == 0) {
            currentValue = tan(currentValue);
        } else if (strcmp(func, "log") == 0) {
            if (currentValue > 0) {
                currentValue = log10(currentValue);
            } else {
                strcpy(displayText, "Error");
                update();
                return;
            }
        } else if (strcmp(func, "ln") == 0) {
            if (currentValue > 0) {
                currentValue = log(currentValue);
            } else {
                strcpy(displayText, "Error");
                update();
                return;
            }
        } else if (strcmp(func, "sqrt") == 0) {
            if (currentValue >= 0) {
                currentValue = sqrt(currentValue);
            } else {
                strcpy(displayText, "Error");
                update();
                return;
            }
        }
        
        // 格式化结果
        if (currentValue == (int)currentValue) {
            sprintf(displayText, "%d", (int)currentValue);
        } else {
            sprintf(displayText, "%.8g", currentValue);
        }
        
        isNewInput = true;
        update();
    }

    void clear() {
        reset();
        update();
    }

    void toggleSign() {
        if (displayText[0] != '0' || strlen(displayText) > 1) {
            if (displayText[0] == '-') {
                // 移除负号
                for (int i = 0; i < strlen(displayText); i++) {
                    displayText[i] = displayText[i + 1];
                }
            } else {
                // 添加负号
                for (int i = strlen(displayText); i >= 0; i--) {
                    displayText[i + 1] = displayText[i];
                }
                displayText[0] = '-';
            }
            update();
        }
    }

    void onResize(uint32_t w, uint32_t h) {
        update();
    }

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        // 绘制背景
        graph_fill(g, r.x, r.y, r.w, r.h, theme->basic.bgColor);
        
        // 绘制显示区域
        grect_t displayRect = {r.x + 10, r.y + 10, r.w - 20, 40};
        graph_fill(g, displayRect.x, displayRect.y, displayRect.w, displayRect.h, 0xFFCCCCCC);
        graph_box(g, displayRect.x, displayRect.y, displayRect.w, displayRect.h, 0xFF000000);
        
        // 绘制显示文本
        font_t* font = theme->getFont();
        uint32_t textWidth, textHeight;
        font_text_size(displayText, font, 24, &textWidth, &textHeight);
        
        int textX = displayRect.x + displayRect.w - textWidth - 5;
        int textY = displayRect.y + (displayRect.h - textHeight) / 2;
        
        graph_draw_text_font(g, textX, textY, displayText, font, 24, 0xFF000000);
        
        // 绘制模式指示器
        const char* modeText = isScientificMode ? "SCI" : "STD";
        font_text_size(modeText, font, 16, &textWidth, &textHeight);
        textX = displayRect.x + 5;
        textY = displayRect.y + (displayRect.h - textHeight) / 2;
        graph_draw_text_font(g, textX, textY, modeText, font, 16, 0xFF000000);
        
        // 绘制按钮
        int buttonWidth = (r.w - 50) / 4;
        // 根据窗口高度动态计算按钮高度，标准模式5行按钮(4个间隔)，科学模式6行按钮(5个间隔)
        int buttonHeight = (r.h - 100) / (isScientificMode ? 8 : 6);
        int fontSize = buttonHeight / 3; // 根据按钮高度动态计算字体大小
        if (fontSize < 12) fontSize = 12; // 设置最小字体大小
        if (fontSize > 24) fontSize = 24; // 设置最大字体大小
        int buttonX = r.x + 10;
        int buttonY = r.y + 60;
        
        // 第一行按钮
        const char* buttons1[] = {"C", "±", "mod", "/"};
        for (int i = 0; i < 4; i++) {
            grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
            graph_fill(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFFEEEEEE);
            graph_box(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFF000000);
            
            uint32_t textWidth, textHeight;
            font_text_size(buttons1[i], font, fontSize, &textWidth, &textHeight);
            int textX = btnRect.x + (btnRect.w - textWidth) / 2;
            int textY = btnRect.y + (btnRect.h - textHeight) / 2;
            graph_draw_text_font(g, textX, textY, buttons1[i], font, fontSize, 0xFF000000);
        }
        
        // 第二行按钮
        buttonY += buttonHeight + 5;
        const char* buttons2[] = {"7", "8", "9", "*"};
        for (int i = 0; i < 4; i++) {
            grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
            graph_fill(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFFEEEEEE);
            graph_box(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFF000000);
            
            uint32_t textWidth, textHeight;
            font_text_size(buttons2[i], font, fontSize, &textWidth, &textHeight);
            int textX = btnRect.x + (btnRect.w - textWidth) / 2;
            int textY = btnRect.y + (btnRect.h - textHeight) / 2;
            graph_draw_text_font(g, textX, textY, buttons2[i], font, fontSize, 0xFF000000);
        }
        
        // 第三行按钮
        buttonY += buttonHeight + 5;
        const char* buttons3[] = {"4", "5", "6", "-"};
        for (int i = 0; i < 4; i++) {
            grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
            graph_fill(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFFEEEEEE);
            graph_box(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFF000000);
            
            uint32_t textWidth, textHeight;
            font_text_size(buttons3[i], font, fontSize, &textWidth, &textHeight);
            int textX = btnRect.x + (btnRect.w - textWidth) / 2;
            int textY = btnRect.y + (btnRect.h - textHeight) / 2;
            graph_draw_text_font(g, textX, textY, buttons3[i], font, fontSize, 0xFF000000);
        }
        
        // 第四行按钮
        buttonY += buttonHeight + 5;
        const char* buttons4[] = {"1", "2", "3", "+"};
        for (int i = 0; i < 4; i++) {
            grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
            graph_fill(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFFEEEEEE);
            graph_box(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFF000000);
            
            uint32_t textWidth, textHeight;
            font_text_size(buttons4[i], font, fontSize, &textWidth, &textHeight);
            int textX = btnRect.x + (btnRect.w - textWidth) / 2;
            int textY = btnRect.y + (btnRect.h - textHeight) / 2;
            graph_draw_text_font(g, textX, textY, buttons4[i], font, fontSize, 0xFF000000);
        }
        
        // 第五行按钮
        buttonY += buttonHeight + 5;
        grect_t btn0Rect = {buttonX, buttonY, buttonWidth * 2 + 5, buttonHeight};
        graph_fill(g, btn0Rect.x, btn0Rect.y, btn0Rect.w, btn0Rect.h, 0xFFEEEEEE);
        graph_box(g, btn0Rect.x, btn0Rect.y, btn0Rect.w, btn0Rect.h, 0xFF000000);
        
        font_text_size("0", font, fontSize, &textWidth, &textHeight);
        textX = btn0Rect.x + (btn0Rect.w - textWidth) / 2;
        textY = btn0Rect.y + (btn0Rect.h - textHeight) / 2;
        graph_draw_text_font(g, textX, textY, "0", font, fontSize, 0xFF000000);
        
        grect_t btnDotRect = {buttonX + buttonWidth * 2 + 10, buttonY, buttonWidth, buttonHeight};
        graph_fill(g, btnDotRect.x, btnDotRect.y, btnDotRect.w, btnDotRect.h, 0xFFEEEEEE);
        graph_box(g, btnDotRect.x, btnDotRect.y, btnDotRect.w, btnDotRect.h, 0xFF000000);
        
        font_text_size(".", font, fontSize, &textWidth, &textHeight);
        textX = btnDotRect.x + (btnDotRect.w - textWidth) / 2;
        textY = btnDotRect.y + (btnDotRect.h - textHeight) / 2;
        graph_draw_text_font(g, textX, textY, ".", font, fontSize, 0xFF000000);
        
        grect_t btnEqualRect = {buttonX + buttonWidth * 3 + 15, buttonY, buttonWidth, buttonHeight};
        graph_fill(g, btnEqualRect.x, btnEqualRect.y, btnEqualRect.w, btnEqualRect.h, 0xFF4A90E2);
        graph_box(g, btnEqualRect.x, btnEqualRect.y, btnEqualRect.w, btnEqualRect.h, 0xFF000000);
        
        font_text_size("=", font, fontSize, &textWidth, &textHeight);
        textX = btnEqualRect.x + (btnEqualRect.w - textWidth) / 2;
        textY = btnEqualRect.y + (btnEqualRect.h - textHeight) / 2;
        graph_draw_text_font(g, textX, textY, "=", font, fontSize, 0xFFFFFFFF);
        
        // 科学计算器按钮
        if (isScientificMode) {
            buttonY += buttonHeight + 10;
            const char* sciButtons[] = {"sin", "cos", "tan", "log", "ln", "sqrt"};
            int sciButtonWidth = (r.w - 70) / 3;
            
            for (int i = 0; i < 6; i++) {
                int row = i / 3;
                int col = i % 3;
                grect_t btnRect = {buttonX + col * (sciButtonWidth + 5), buttonY + row * (buttonHeight + 5), sciButtonWidth, buttonHeight};
                graph_fill(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFFDDDDDD);
                graph_box(g, btnRect.x, btnRect.y, btnRect.w, btnRect.h, 0xFF000000);
                
                uint32_t textWidth, textHeight;
                font_text_size(sciButtons[i], font, fontSize, &textWidth, &textHeight);
                int textX = btnRect.x + (btnRect.w - textWidth) / 2;
                int textY = btnRect.y + (btnRect.h - textHeight) / 2;
                graph_draw_text_font(g, textX, textY, sciButtons[i], font, fontSize, 0xFF000000);
            }
        }
    }

    bool onMouse(xevent_t* xev) {
        gpos_t pos = getInsidePos(xev->value.mouse.x, xev->value.mouse.y);
        if (xev->state == MOUSE_STATE_CLICK) {
            // 检查按钮点击
            int buttonWidth = (area.w - 50) / 4;
            // 根据窗口高度动态计算按钮高度，标准模式5行按钮(4个间隔)，科学模式6行按钮(5个间隔)
            int buttonHeight = (area.h - 100) / (isScientificMode ? 8 : 6);
            int buttonX = area.x + 10;
            int buttonY = area.y + 60;
            
            // 第一行按钮
            for (int i = 0; i < 4; i++) {
                grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
                if (pos.x >= btnRect.x && pos.x < btnRect.x + btnRect.w &&
                    pos.y >= btnRect.y && pos.y < btnRect.y + btnRect.h) {
                    switch (i) {
                        case 0: // C
                            clear();
                            return true;
                        case 1: // ±
                            toggleSign();
                            return true;
                        case 2: // mod
                            inputOperator('%');
                            return true;
                        case 3: // /
                            inputOperator('/');
                            return true;
                    }
                }
            }
            
            // 第二行按钮
            buttonY += buttonHeight + 5;
            for (int i = 0; i < 4; i++) {
                grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
                if (pos.x >= btnRect.x && pos.x < btnRect.x + btnRect.w &&
                    pos.y >= btnRect.y && pos.y < btnRect.y + btnRect.h) {
                    if (i < 3) { // 数字7,8,9
                        inputDigit('7' + i);
                    } else { // *
                        inputOperator('*');
                    }
                    return true;
                }
            }
            
            // 第三行按钮
            buttonY += buttonHeight + 5;
            for (int i = 0; i < 4; i++) {
                grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
                if (pos.x >= btnRect.x && pos.x < btnRect.x + btnRect.w &&
                    pos.y >= btnRect.y && pos.y < btnRect.y + btnRect.h) {
                    if (i < 3) { // 数字4,5,6
                        inputDigit('4' + i);
                    } else { // -
                        inputOperator('-');
                    }
                    return true;
                }
            }
            
            // 第四行按钮
            buttonY += buttonHeight + 5;
            for (int i = 0; i < 4; i++) {
                grect_t btnRect = {buttonX + i * (buttonWidth + 5), buttonY, buttonWidth, buttonHeight};
                if (pos.x >= btnRect.x && pos.x < btnRect.x + btnRect.w &&
                    pos.y >= btnRect.y && pos.y < btnRect.y + btnRect.h) {
                    if (i < 3) { // 数字1,2,3
                        inputDigit('1' + i);
                    } else { // +
                        inputOperator('+');
                    }
                    return true;
                }
            }
            
            // 第五行按钮
            buttonY += buttonHeight + 5;
            grect_t btn0Rect = {buttonX, buttonY, buttonWidth * 2 + 5, buttonHeight};
            if (pos.x >= btn0Rect.x && pos.x < btn0Rect.x + btn0Rect.w &&
                pos.y >= btn0Rect.y && pos.y < btn0Rect.y + btn0Rect.h) {
                inputDigit('0');
                return true;
            }
            
            grect_t btnDotRect = {buttonX + buttonWidth * 2 + 10, buttonY, buttonWidth, buttonHeight};
            if (pos.x >= btnDotRect.x && pos.x < btnDotRect.x + btnDotRect.w &&
                pos.y >= btnDotRect.y && pos.y < btnDotRect.y + btnDotRect.h) {
                // TODO: 实现小数点功能
                return true;
            }
            
            grect_t btnEqualRect = {buttonX + buttonWidth * 3 + 15, buttonY, buttonWidth, buttonHeight};
            if (pos.x >= btnEqualRect.x && pos.x < btnEqualRect.x + btnEqualRect.w &&
                pos.y >= btnEqualRect.y && pos.y < btnEqualRect.y + btnEqualRect.h) {
                calculate();
                return true;
            }
            
            // 科学计算器按钮
            if (isScientificMode) {
                buttonY += buttonHeight + 10;
                const char* sciButtons[] = {"sin", "cos", "tan", "log", "ln", "sqrt"};
                int sciButtonWidth = (area.w - 70) / 3;
                
                for (int i = 0; i < 6; i++) {
                    int row = i / 3;
                    int col = i % 3;
                    grect_t btnRect = {buttonX + col * (sciButtonWidth + 5), buttonY + row * (buttonHeight + 5), sciButtonWidth, buttonHeight};
                    if (pos.x >= btnRect.x && pos.x < btnRect.x + btnRect.w &&
                        pos.y >= btnRect.y && pos.y < btnRect.y + btnRect.h) {
                        inputFunction(sciButtons[i]);
                        return true;
                    }
                }
            }
            
            // 检查模式切换按钮
            grect_t displayRect = {10, 10, area.w - 20, 40};
            if (pos.x >= displayRect.x && pos.x < displayRect.x + 50 &&
                pos.y >= displayRect.y && pos.y < displayRect.y + displayRect.h) {
                toggleMode();
                return true;
            }
        }
        return false;
    }
};

int main(int argc, char** argv) {
    X x;
    WidgetWin win;
    RootWidget* root = new RootWidget();
    win.setRoot(root);
    
    Calculator* calculator = new Calculator();
    root->add(calculator);
    
    win.open(&x, -1, -1, -1, 300, 400, "Calculator", XWIN_STYLE_NORMAL);
    win.setTimer(60);
    
    widgetXRun(&x, &win);
    return 0;
}