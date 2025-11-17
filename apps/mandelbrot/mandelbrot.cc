#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <x++/X.h>
#include <unistd.h>
#include <stdlib.h>
#include <font/font.h>
#include <graph/graph_png.h>
#include <ewoksys/ewokdef.h>
#include <openlibm.h>
#include <cmath> // 添加头文件用于数学计算

using namespace Ewok;

class Mandelbrot : public Widget {
private:
    double xMin = -2.0;
    double xMax = 1.0;
    double yMin = -1.5;
    double yMax = 1.5;
    int maxIterations = 30; // 去掉 const，使其可以动态调整
    const double zoomFactor = 2.0; // 放大倍数
    double initialWidth = 3.0; // 初始复平面宽度

public:
    inline Mandelbrot() {
    }
    
    inline ~Mandelbrot() {
    }

    // 处理鼠标点击事件
    bool onMouse(xevent_t* xev) {
        if (xev->state == MOUSE_STATE_CLICK) {
            grect_t r = this->getRootArea(); 
            gpos_t pos = getInsidePos(xev->value.mouse.x, xev->value.mouse.y);
            // 将屏幕坐标转换为复平面坐标
            double clickRe = xMin + (pos.x) * (xMax - xMin) / r.w;
            double clickIm = yMin + (pos.y) * (yMax - yMin) / r.h;

            // 计算新的范围
            double newWidth = (xMax - xMin) / zoomFactor;
            double newHeight = (yMax - yMin) / zoomFactor;

            xMin = clickRe - newWidth / 2.0;
            xMax = clickRe + newWidth / 2.0;
            yMin = clickIm - newHeight / 2.0;
            yMax = clickIm + newHeight / 2.0;

            // 根据放大倍数动态调整 maxIterations
            double currentWidth = xMax - xMin;
            double scaleFactor = initialWidth / currentWidth;
            maxIterations = static_cast<int>(30 * log(scaleFactor + 1) + 30);

            update(); // 触发重绘
            return true;
        }
        return false;
    }

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        for (int y = r.y; y < r.y + r.h; y++) {
            for (int x = r.x; x < r.x + r.w; x++) {
                // 将屏幕坐标转换为复平面坐标
                double cRe = xMin + (x - r.x) * (xMax - xMin) / r.w;
                double cIm = yMin + (y - r.y) * (yMax - yMin) / r.h;

                double zRe = 0.0;
                double zIm = 0.0;
                int iteration = 0;

                // 迭代计算
                while (zRe * zRe + zIm * zIm <= 4.0 && iteration < maxIterations) {
                    double temp = zRe * zRe - zIm * zIm + cRe;
                    zIm = 2.0 * zRe * zIm + cIm;
                    zRe = temp;
                    iteration++;
                }

                // 根据迭代次数设置颜色
                uint8_t colorValue = (iteration == maxIterations) ? 0 : (255 * iteration / maxIterations);
                uint32_t color = 0xff000000|(colorValue << 16) | (0 << 8) | colorValue;
                graph_pixel(g, x, y, color);
            }
        }
    }
};


int main(int argc, char** argv) {
    X x;
    WidgetWin win;
    RootWidget* root = new RootWidget();
    win.setRoot(root);
    root->setType(Container::HORIZONTAL);
    

    Mandelbrot* mandelbrot = new Mandelbrot();
    root->add(mandelbrot);

    win.open(&x, -1, -1, -1, 300, 200, "Mandelbrot Set", XWIN_STYLE_NORMAL);
    widgetXRun(&x, &win);    
    return 0;
}