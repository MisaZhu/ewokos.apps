#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// 假设这些是游戏所需的图形库和系统库
#include <ewoksys/proc.h>
#include <graph/graph_png.h>
#include <ewoksys/basic_math.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/ipc.h>
#include <ewoksys/klog.h>
#include <ewoksys/keydef.h>
#include <font/font.h>
#include <x++/X.h>
#include <ewoksys/timer.h>

using namespace Ewok;

// 定义方向枚举
enum Direction {
    UP,
    DOWN,
    LEFT,
    RIGHT
};

// 定义蛇的节点结构体
struct SnakeNode {
    int x;
    int y;
};

class SnakeGame : public XWin {
private:
    std::vector<SnakeNode> snake;
    Direction direction;
    SnakeNode food;
    int score;
    bool gameOver;
    int gameOverAnimationStep; // 新增：游戏失败动画步骤

    // 生成食物
    void generateFood() {
        xinfo_t info;
        this->getInfo(info);
        int maxX = info.wsr.w;
        int maxY = info.wsr.h;
        food.x = rand() % maxX;
        food.y = rand() % maxY;
    }

    // 绘制蛇和食物
    void drawSnakeAndFood(graph_t* g) {
        // 绘制蛇
        if (!snake.empty()) {
            // 绘制蛇头，使用椭圆表示，添加眼睛
            SnakeNode head = snake.front();
            // 绘制蛇头椭圆
            graph_fill_circle(g, head.x, head.y, 15, 0xFF008000); // 深绿色作为蛇头颜色

            // 根据方向绘制眼睛
            switch (direction) {
                case UP:
                    // 向上时，眼睛在上方
                    graph_fill(g, head.x - 5, head.y - 5, 3, 3, 0xFF000000); // 左眼
                    graph_fill(g, head.x + 5, head.y - 5, 3, 3, 0xFF000000); // 右眼
                    break;
                case DOWN:
                    // 向下时，眼睛在下方
                    graph_fill(g, head.x - 5, head.y + 2, 3, 3, 0xFF000000); // 左眼
                    graph_fill(g, head.x + 5, head.y + 2, 3, 3, 0xFF000000); // 右眼
                    break;
                case LEFT:
                    // 向左时，眼睛在左边
                    graph_fill(g, head.x - 5, head.y - 2, 3, 3, 0xFF000000); // 左眼
                    graph_fill(g, head.x - 5, head.y + 2, 3, 3, 0xFF000000); // 右眼
                    break;
                case RIGHT:
                    // 向右时，眼睛在右边
                    graph_fill(g, head.x + 2, head.y - 2, 3, 3, 0xFF000000); // 左眼
                    graph_fill(g, head.x + 2, head.y + 2, 3, 3, 0xFF000000); // 右眼
                    break;
            }

            // 绘制蛇身，使用椭圆添加渐变颜色
            int bodyLength = snake.size() - 1;
            for (int i = 1; i <= bodyLength; ++i) {
                SnakeNode body = snake[i];
                // 根据蛇身位置计算渐变颜色
                int greenValue = 128 + (i * 64) / bodyLength;
                if (greenValue > 255) greenValue = 255;
                int color = 0xFF000000 | (greenValue << 8);
                // 绘制蛇身椭圆
                graph_fill_circle(g, body.x, body.y, 10, color);
            }
        }
        // 绘制食物
        graph_fill(g, food.x, food.y, 10, 10, 0xFFFF0000); // 红色
    }

    // 移动蛇
    void moveSnake() {
        SnakeNode head = snake.front();
        switch (direction) {
            case UP:
                head.y -= 10;
                break;
            case DOWN:
                head.y += 10;
                break;
            case LEFT:
                head.x -= 10;
                break;
            case RIGHT:
                head.x += 10;
                break;
        }
        snake.insert(snake.begin(), head);

        // 假设食物有宽度和高度
        int foodWidth = 10;
        int foodHeight = 10;

        // 检查是否吃到食物，考虑食物大小
        if (head.x < food.x + foodWidth &&
            head.x + 10 > food.x &&
            head.y < food.y + foodHeight &&
            head.y + 10 > food.y) {
            score += 10;
            generateFood();
        } else {
            snake.pop_back();
        } 

        // 检查是否撞到边界或自己
        if (head.x < 0 || head.x >= this->xwin->xinfo->wsr.w || head.y < 0 || head.y >= this->xwin->xinfo->wsr.h) {
            gameOver = true;
        }
        for (size_t i = 1; i < snake.size(); ++i) {
            if (head.x == snake[i].x && head.y == snake[i].y) {
                gameOver = true;
            }
        }
    }

public:
    inline SnakeGame() {
        // 初始化蛇
        snake.push_back({100, 100});
        snake.push_back({90, 100});
        snake.push_back({80, 100});
        direction = RIGHT;
        score = 0;
        gameOver = false;
        gameOverAnimationStep = 0;
    }

    inline void start() {
        generateFood();
    }    

    // 重置游戏
    void reset() {
        snake.clear();
        snake.push_back({100, 100});
        snake.push_back({90, 100});
        snake.push_back({80, 100});
        direction = RIGHT;
        score = 0;
        gameOver = false;
        generateFood();
    }

    inline ~SnakeGame() {
        // 析构函数
    }

protected:
    void onEvent(xevent_t* ev) {
        if (ev->type == XEVT_IM) {
            int key = ev->value.im.value;
            switch (key) {
                case 'w':
                case KEY_UP:
                    if (direction != DOWN) direction = UP;
                    break;
                case 's':
                case KEY_DOWN:
                    if (direction != UP) direction = DOWN;
                    break;
                case 'a':
                case KEY_LEFT:
                    if (direction != RIGHT) direction = LEFT;
                    break;
                case 'd':
                case KEY_RIGHT:
                    if (direction != LEFT) direction = RIGHT;
                    break;
                case 27: // ESC
                case JOYSTICK_START:
                    reset();
                    break;
            }
        }
    }

     // 新增方法，处理窗口大小改变事件
     void onResize(void) {
        xinfo_t info;
        this->getInfo(info);

        int width = info.wsr.w;
        int height = info.wsr.h;
        // 确保蛇在新窗口内
        SnakeNode& head = snake.front();
        if (head.x >= width) head.x = width - 10;
        if (head.y >= height) head.y = height - 10;
        // 重新生成食物
        generateFood();
    }


    void onRepaint(graph_t* g) {
        if (gameOver) {
            if (gameOverAnimationStep < snake.size()) {
                graph_fill(g, 0, 0, g->w, g->h, 0xFF000000); // 清屏
                // 绘制剩余的蛇身
                for (size_t i = gameOverAnimationStep; i < snake.size(); ++i) {
                    graph_fill(g, snake[i].x, snake[i].y, 10, 10, 0xFF00FF00); // 绿色
                }
                // 绘制食物
                graph_fill(g, food.x, food.y, 10, 10, 0xFFFF0000); // 红色
                gameOverAnimationStep++;
            } else {
                // 绘制烟火效果
                for (int i = 0; i < 50; ++i) {
                    int x = rand() % g->w;
                    int y = rand() % g->h;
                    int color = 0xFF000000 | (rand() % 0xFFFFFF); // 随机颜色
                    graph_fill(g, x, y, 2, 2, color); // 绘制烟火粒子
                }
                char str[32];
                snprintf(str, 31, "Game Over! Score: %d", score);
                graph_draw_text_font(g, 100, 100, str, theme.getFont(), theme.basic.fontSize, 0xFFFFFFFF);
            }
            return;
        }

        gameOverAnimationStep = 0; // 重置动画步骤

        graph_fill(g, 0, 0, g->w, g->h, 0xFF000000); // 清屏
        moveSnake();
        drawSnakeAndFood(g);

        char str[32];
        snprintf(str, 31, "Score: %d", score);
        graph_draw_text_font(g, 10, 10, str, theme.getFont(), theme.basic.fontSize, 0xFFFFFFFF);
    }
};

static void loop(void* p) {
    XWin* xwin = (XWin*)p;
    if (!xwin->getX()->terminated()) {
        xwin->repaint();
        proc_usleep(100000); // 控制游戏速度
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    grect_t desk;

    X x;
    SnakeGame snakeGame;
    snakeGame.open(&x, -1, -1, -1, 0, 0, "Snake Game", XWIN_STYLE_NORMAL);
    snakeGame.start();
    x.run(loop, &snakeGame);
    return 0;
}