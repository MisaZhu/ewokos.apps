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

const int BOARD_SIZE = 15;
const int CELL_SIZE = 16;

class GobangGame : public Widget {
    int board[BOARD_SIZE][BOARD_SIZE];
    bool isBlackTurn;
    bool gameOver;
    bool blackWins;

public:
    inline GobangGame() {
        resetBoard();
        isBlackTurn = true;
        gameOver = false;
        blackWins = false;
    }

    inline ~GobangGame() {
    }

    void resetBoard() {
        for (int i = 0; i < BOARD_SIZE; ++i) {
            for (int j = 0; j < BOARD_SIZE; ++j) {
                board[i][j] = 0;
            }
        }
    }

    bool placePiece(int x, int y) {
        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE || board[x][y] != 0) {
            return false;
        }
        board[x][y] = isBlackTurn ? 1 : 2;
        if (checkWin(x, y)) {
            gameOver = true;
            blackWins = isBlackTurn;
        }
        isBlackTurn = !isBlackTurn;
        update();
        return true;
    }

    bool checkWin(int x, int y) {
        int piece = board[x][y];
        // 检查横向
        int count = 1;
        for (int i = x - 1; i >= 0 && board[i][y] == piece; --i) ++count;
        for (int i = x + 1; i < BOARD_SIZE && board[i][y] == piece; ++i) ++count;
        if (count >= 5) return true;

        // 检查纵向
        count = 1;
        for (int i = y - 1; i >= 0 && board[x][i] == piece; --i) ++count;
        for (int i = y + 1; i < BOARD_SIZE && board[x][i] == piece; ++i) ++count;
        if (count >= 5) return true;

        // 检查正斜向
        count = 1;
        for (int i = 1; x - i >= 0 && y - i >= 0 && board[x - i][y - i] == piece; ++i) ++count;
        for (int i = 1; x + i < BOARD_SIZE && y + i < BOARD_SIZE && board[x + i][y + i] == piece; ++i) ++count;
        if (count >= 5) return true;

        // 检查反斜向
        count = 1;
        for (int i = 1; x - i >= 0 && y + i < BOARD_SIZE && board[x - i][y + i] == piece; ++i) ++count;
        for (int i = 1; x + i < BOARD_SIZE && y - i >= 0 && board[x + i][y - i] == piece; ++i) ++count;
        if (count >= 5) return true;

        return false;
    }

protected:
    void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
        // 绘制棋盘背景
        graph_fill(g, r.x, r.y, r.w, r.h, theme->basic.bgColor);

        // 绘制棋盘网格
        for (int i = 0; i < BOARD_SIZE; ++i) {
            int startX = r.x + CELL_SIZE;
            int startY = r.y + CELL_SIZE + i * CELL_SIZE;
            int endX = r.x + CELL_SIZE + (BOARD_SIZE - 1) * CELL_SIZE;
            int endY = startY;
            graph_line(g, startX, startY, endX, endY, 0xFF000000);

            startX = r.x + CELL_SIZE + i * CELL_SIZE;
            startY = r.y + CELL_SIZE;
            endX = startX;
            endY = r.y + CELL_SIZE + (BOARD_SIZE - 1) * CELL_SIZE;
            graph_line(g, startX, startY, endX, endY, 0xFF000000);
        }

        // 绘制棋子
        for (int i = 0; i < BOARD_SIZE; ++i) {
            for (int j = 0; j < BOARD_SIZE; ++j) {
                if (board[i][j] == 1) { // 黑子
                    int centerX = r.x + CELL_SIZE + i * CELL_SIZE;
                    int centerY = r.y + CELL_SIZE + j * CELL_SIZE;
                    graph_fill_circle(g, centerX, centerY, CELL_SIZE / 2 - 2, 0xFF000000);
                } else if (board[i][j] == 2) { // 白子
                    int centerX = r.x + CELL_SIZE + i * CELL_SIZE;
                    int centerY = r.y + CELL_SIZE + j * CELL_SIZE;
                    graph_fill_circle(g, centerX, centerY, CELL_SIZE / 2 - 2, 0xFFFFFFFF);
                }
            }
        }

        // 绘制输赢结果
        if (gameOver) {
            // 绘制半透明覆盖层
            graph_fill(g, r.x, r.y, r.w, r.h, 0x80FFFFFF);

            const char* resultText;
            if (blackWins) {
                resultText = "Black Wins!";
            } else {
                resultText = "White Wins!";
            }

            font_t* font = theme->getFont();
            uint32_t textWidth, textHeight;
            font_text_size(resultText, font, 32, &textWidth, &textHeight);

            int textX = r.x + (r.w - textWidth) / 2;
            int textY = r.y + (r.h - textHeight) / 2;

            graph_draw_text_font(g, textX, textY, resultText, font, 32, 0xFF000000);
        }
    }

    bool onMouse(xevent_t* xev) {
        gpos_t pos = getInsidePos(xev->value.mouse.x, xev->value.mouse.y);
        if (xev->state == MOUSE_STATE_CLICK) {
            if (gameOver) {
                // 游戏结束时，点击鼠标重启游戏
                resetBoard();
                isBlackTurn = true;
                gameOver = false;
                blackWins = false;
                update();
                return true;
            } else {
                int col = (pos.x - CELL_SIZE + CELL_SIZE/2) / CELL_SIZE;
                int row = (pos.y - CELL_SIZE + CELL_SIZE/2) / CELL_SIZE;
                if (placePiece(col, row)) {
                    if (checkWin(col, row)) {
                        // 游戏结束逻辑已在 placePiece 中处理
                    }
                }
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
    root->setType(Container::HORIZONTAL);
    

    GobangGame* gobangGame = new GobangGame();
    root->add(gobangGame);

    int windowWidth = (BOARD_SIZE + 1) * CELL_SIZE;
    int windowHeight = (BOARD_SIZE + 1) * CELL_SIZE;
    win.open(&x, -1, -1, -1, windowWidth, windowHeight, "Gobang Game", XWIN_STYLE_NORMAL);
    win.setTimer(60);

    widgetXRun(&x, &win);    
    return 0;
}