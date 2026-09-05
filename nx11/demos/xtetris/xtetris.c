/*
 * xtetris -- a pure-Xlib Tetris clone for NX11 on EwokOS.
 *
 * Like x11demo this is ordinary X11 code: libNX11 turns every call into a
 * Nano-X server call and the top-level window becomes a real EwokOS xwin
 * window (NANOWM=0), so the desktop window manager owns the frame and the
 * move/resize/close gestures.  Nothing here knows about EwokOS except the two
 * timing primitives used to drive the gravity tick - Xlib has no portable
 * timer, and select() on ConnectionNumber() does not work because NX11 is a
 * single-process (NONETWORK) server whose display fd is not a real socket.
 *
 * The loop is a non-blocking poll: XCheckWindowEvent() -> GrGetTypedEvent()
 * calls GsSelect(GR_TIMEOUT_POLL) on every invocation, which runs the screen
 * driver PreSelect and therefore flushes and presents whatever was drawn on
 * the previous iteration.  proc_usleep() between polls yields the CPU, so this
 * is not a busy spin.
 *
 * Controls:  Left/Right move, Down soft drop, Up/X rotate CW, Z rotate CCW,
 *            Space hard drop, P pause, R restart, Q/Esc quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep   */
#include <ewoksys/kernel_tic.h>		/* kernel_tic_ms */

#define TITLE		"Tetris (NX11)"
#define FONT_NAME	"fixed"		/* mapped to the builtin SystemFixed font*/

#define COLS		10
#define ROWS		20
#define PANEL_W		176
#define MARGIN		12
#define DEF_W		484
#define DEF_H		560
#define TICK_US		10000		/* 10 ms poll slice -> ~100 Hz*/
#define NPIECE		7

/* the seven tetrominoes, I O T S Z J L, each in its own bounding box*/
static const int		piece_n[NPIECE] = { 4, 2, 3, 3, 3, 3, 3 };
static const unsigned char	piece_spawn[NPIECE][4][4] = {
	/*I*/ {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
	/*O*/ {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
	/*T*/ {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
	/*S*/ {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
	/*Z*/ {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
	/*J*/ {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
	/*L*/ {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
};
static const unsigned char piece_rgb[NPIECE][3] = {
	{0x00,0xf0,0xf0}, {0xf0,0xf0,0x00}, {0xa0,0x00,0xf0}, {0x00,0xf0,0x00},
	{0xf0,0x00,0x00}, {0x00,0x00,0xf0}, {0xf0,0xa0,0x00},
};

/* piece_rot[type][rotation][row][col], generated from piece_spawn at init*/
static unsigned char	piece_rot[NPIECE][4][4][4];

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_board, c_slot, c_text, c_dim, c_gold, c_border;
static unsigned long	pc_base[NPIECE], pc_light[NPIECE], pc_dark[NPIECE];

static int		win_w = DEF_W, win_h = DEF_H;
static int		cell, board_x, board_y, board_w, board_h, panel_x;

static int		board[ROWS][COLS];	/* 0 empty, else piece type + 1*/
static int		cur_type, cur_rot, cur_x, cur_y;
static int		next_type;
static long		score, lines;
static int		level;
static int		drop_ms;
static int		gameover, paused, running = 1, need_draw = 1;
static uint64_t		last_drop;

static int		bag[NPIECE], bag_i;

/*----------------------------------------------------------------------------*/

static unsigned long
alloc_rgb(int r, int g, int b)
{
	XColor	c;

	memset(&c, 0, sizeof(c));
	c.red   = (unsigned short)(r << 8);
	c.green = (unsigned short)(g << 8);
	c.blue  = (unsigned short)(b << 8);
	c.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(dpy, cmap, &c))
		return c.pixel;
	return BlackPixel(dpy, scr);
}

static void
rotate_mat(int n, const unsigned char src[4][4], unsigned char dst[4][4])
{
	int i, j;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			dst[i][j] = 0;
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			dst[j][n - 1 - i] = src[i][j];
}

static void
init_pieces(void)
{
	int t, r;

	for (t = 0; t < NPIECE; t++) {
		memcpy(piece_rot[t][0], piece_spawn[t], sizeof(piece_spawn[t]));
		for (r = 1; r < 4; r++)
			rotate_mat(piece_n[t], piece_rot[t][r - 1], piece_rot[t][r]);
	}
}

static int
rnd(int n)
{
	if (n <= 1)
		return 0;
	return rand() % n;
}

/* 7-bag randomiser: every seven pieces contain each tetromino exactly once*/
static int
next_from_bag(void)
{
	int i, j, t;

	if (bag_i >= NPIECE) {
		for (i = 0; i < NPIECE; i++)
			bag[i] = i;
		for (i = NPIECE - 1; i > 0; i--) {
			j = rnd(i + 1);
			t = bag[i]; bag[i] = bag[j]; bag[j] = t;
		}
		bag_i = 0;
	}
	return bag[bag_i++];
}

/*----------------------------------------------------------------------------*/
/* game rules                                                                   */
/*----------------------------------------------------------------------------*/

static int
collides(int type, int rot, int px, int py)
{
	int n = piece_n[type];
	int i, j, bx, by;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			if (!piece_rot[type][rot][i][j])
				continue;
			bx = px + j;
			by = py + i;
			if (bx < 0 || bx >= COLS || by >= ROWS)
				return 1;
			if (by >= 0 && board[by][bx])
				return 1;
		}
	return 0;
}

static void
spawn(void)
{
	cur_type = next_type;
	next_type = next_from_bag();
	cur_rot = 0;
	cur_x = (COLS - piece_n[cur_type]) / 2;
	cur_y = 0;
	if (collides(cur_type, cur_rot, cur_x, cur_y))
		gameover = 1;
	last_drop = kernel_tic_ms(0);
	need_draw = 1;
}

static void
lock_piece(void)
{
	int n = piece_n[cur_type];
	int i, j, bx, by, cleared, r, c;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++) {
			if (!piece_rot[cur_type][cur_rot][i][j])
				continue;
			bx = cur_x + j;
			by = cur_y + i;
			if (by < 0) {		/* locked above the ceiling: topped out*/
				gameover = 1;
				return;
			}
			board[by][bx] = cur_type + 1;
		}

	/* scan for full rows, bottom up*/
	cleared = 0;
	for (by = ROWS - 1; by >= 0; by--) {
		int full = 1;
		for (bx = 0; bx < COLS; bx++)
			if (!board[by][bx]) { full = 0; break; }
		if (!full)
			continue;
		cleared++;
		for (r = by; r > 0; r--)
			for (c = 0; c < COLS; c++)
				board[r][c] = board[r - 1][c];
		for (c = 0; c < COLS; c++)
			board[0][c] = 0;
		by++;				/* re-test the row that shifted down*/
	}

	if (cleared > 0) {
		static const long pts[5] = { 0, 100, 300, 500, 800 };
		score += pts[cleared] * level;
		lines += cleared;
		level = (int)(lines / 10) + 1;
		drop_ms = 800 - (level - 1) * 70;
		if (drop_ms < 80)
			drop_ms = 80;
	}
	spawn();
}

static void
try_move(int dx, int dy)
{
	if (!collides(cur_type, cur_rot, cur_x + dx, cur_y + dy)) {
		cur_x += dx;
		cur_y += dy;
		need_draw = 1;
	}
}

static void
try_rotate(int dir)
{
	int nrot = (cur_rot + dir) & 3;
	static const int kickx[] = { 0, -1, 1, -2, 2 };
	int k;

	for (k = 0; k < 5; k++) {
		if (!collides(cur_type, nrot, cur_x + kickx[k], cur_y)) {
			cur_rot = nrot;
			cur_x += kickx[k];
			need_draw = 1;
			return;
		}
	}
}

static void
soft_drop(void)
{
	if (!collides(cur_type, cur_rot, cur_x, cur_y + 1)) {
		cur_y++;
		score += 1;
		last_drop = kernel_tic_ms(0);
		need_draw = 1;
	} else {
		lock_piece();
	}
}

static void
hard_drop(void)
{
	int d = 0;

	while (!collides(cur_type, cur_rot, cur_x, cur_y + 1)) {
		cur_y++;
		d++;
	}
	score += 2 * d;
	lock_piece();
}

static int
ghost_y(void)
{
	int gy = cur_y;

	while (!collides(cur_type, cur_rot, cur_x, gy + 1))
		gy++;
	return gy;
}

static void
new_game(void)
{
	memset(board, 0, sizeof(board));
	score = 0;
	lines = 0;
	level = 1;
	drop_ms = 800;
	gameover = 0;
	paused = 0;
	bag_i = NPIECE;
	next_type = next_from_bag();
	spawn();
	need_draw = 1;
}

/*----------------------------------------------------------------------------*/
/* layout + drawing                                                             */
/*----------------------------------------------------------------------------*/

static void
layout(void)
{
	int avail_w = win_w - PANEL_W - MARGIN * 3;
	int avail_h = win_h - MARGIN * 2;
	int cellh;

	if (avail_w < COLS * 6)
		avail_w = COLS * 6;
	if (avail_h < ROWS * 6)
		avail_h = ROWS * 6;

	cell = avail_w / COLS;
	cellh = avail_h / ROWS;
	if (cellh < cell)
		cell = cellh;
	if (cell < 6)
		cell = 6;
	if (cell > 34)
		cell = 34;

	board_w = cell * COLS;
	board_h = cell * ROWS;
	board_x = MARGIN + (avail_w - board_w) / 2;
	board_y = MARGIN + (avail_h - board_h) / 2;
	panel_x = win_w - PANEL_W - MARGIN;
}

static void
draw_str(int x, int y, const char *s, unsigned long fg)
{
	XSetForeground(dpy, gc, fg);
	XDrawString(dpy, win, gc, x, y, s, (int)strlen(s));
}

static void
draw_str_c(int cx, int y, const char *s, unsigned long fg)
{
	int tw = XTextWidth(xfont, s, (int)strlen(s));
	draw_str(cx - tw / 2, y, s, fg);
}

static void
draw_cell_at(int px, int py, int sz, int type)
{
	XSetForeground(dpy, gc, pc_base[type]);
	XFillRectangle(dpy, win, gc, px, py, (unsigned)sz, (unsigned)sz);
	XSetForeground(dpy, gc, pc_light[type]);
	XDrawLine(dpy, win, gc, px, py, px + sz - 1, py);
	XDrawLine(dpy, win, gc, px, py, px, py + sz - 1);
	XSetForeground(dpy, gc, pc_dark[type]);
	XDrawLine(dpy, win, gc, px + sz - 1, py, px + sz - 1, py + sz - 1);
	XDrawLine(dpy, win, gc, px, py + sz - 1, px + sz - 1, py + sz - 1);
}

static void
draw_board(void)
{
	int x, y, gy;

	XSetForeground(dpy, gc, c_board);
	XFillRectangle(dpy, win, gc, board_x, board_y, (unsigned)board_w, (unsigned)board_h);

	for (y = 0; y < ROWS; y++)
		for (x = 0; x < COLS; x++) {
			int px = board_x + x * cell;
			int py = board_y + y * cell;
			if (board[y][x])
				draw_cell_at(px, py, cell, board[y][x] - 1);
			else {
				XSetForeground(dpy, gc, c_slot);
				XFillRectangle(dpy, win, gc, px + 1, py + 1,
						(unsigned)(cell - 2), (unsigned)(cell - 2));
			}
		}

	/* ghost (landing preview) then the live piece*/
	if (!gameover) {
		gy = ghost_y();
		XSetForeground(dpy, gc, pc_dark[cur_type]);
		for (y = 0; y < piece_n[cur_type]; y++)
			for (x = 0; x < piece_n[cur_type]; x++) {
				if (!piece_rot[cur_type][cur_rot][y][x])
					continue;
				if (gy + y < 0)
					continue;
				XDrawRectangle(dpy, win, gc,
						board_x + (cur_x + x) * cell + 2,
						board_y + (gy + y) * cell + 2,
						(unsigned)(cell - 5), (unsigned)(cell - 5));
			}
		for (y = 0; y < piece_n[cur_type]; y++)
			for (x = 0; x < piece_n[cur_type]; x++) {
				if (!piece_rot[cur_type][cur_rot][y][x])
					continue;
				if (cur_y + y < 0)
					continue;
				draw_cell_at(board_x + (cur_x + x) * cell,
						board_y + (cur_y + y) * cell, cell, cur_type);
			}
	}

	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, board_x - 1, board_y - 1,
			(unsigned)(board_w + 1), (unsigned)(board_h + 1));
}

static void
draw_preview(int cx, int cy, int type, int psz)
{
	int n = piece_n[type];
	int i, j, minx = 9, maxx = -9, miny = 9, maxy = -9, w, h, ox, oy;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			if (piece_rot[type][0][i][j]) {
				if (j < minx) minx = j;
				if (j > maxx) maxx = j;
				if (i < miny) miny = i;
				if (i > maxy) maxy = i;
			}
	w = (maxx - minx + 1) * psz;
	h = (maxy - miny + 1) * psz;
	ox = cx - w / 2;
	oy = cy - h / 2;
	for (i = miny; i <= maxy; i++)
		for (j = minx; j <= maxx; j++)
			if (piece_rot[type][0][i][j])
				draw_cell_at(ox + (j - minx) * psz, oy + (i - miny) * psz, psz, type);
}

static void
draw_panel(void)
{
	char	buf[64];
	int	th = xfont->ascent + xfont->descent;
	int	y = board_y + xfont->ascent + 4;
	int	box, psz;

	draw_str_c(panel_x + PANEL_W / 2, y, "T E T R I S", c_gold);
	y += th + 10;

	XSetForeground(dpy, gc, c_dim);
	XDrawLine(dpy, win, gc, panel_x, y - th / 2, panel_x + PANEL_W, y - th / 2);
	y += 6;

	draw_str(panel_x, y, "SCORE", c_dim); y += th;
	snprintf(buf, sizeof(buf), "%ld", score);
	draw_str(panel_x, y, buf, c_text); y += th + 8;

	draw_str(panel_x, y, "LINES", c_dim); y += th;
	snprintf(buf, sizeof(buf), "%ld", lines);
	draw_str(panel_x, y, buf, c_text); y += th + 8;

	draw_str(panel_x, y, "LEVEL", c_dim); y += th;
	snprintf(buf, sizeof(buf), "%d", level);
	draw_str(panel_x, y, buf, c_text); y += th + 12;

	draw_str(panel_x, y, "NEXT", c_dim); y += 6;
	box = PANEL_W;
	psz = cell * 3 / 5;
	if (psz < 8) psz = 8;
	if (psz > 18) psz = 18;
	XSetForeground(dpy, gc, c_board);
	XFillRectangle(dpy, win, gc, panel_x, y, (unsigned)box, (unsigned)(psz * 3 + 12));
	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, panel_x, y, (unsigned)box, (unsigned)(psz * 3 + 12));
	draw_preview(panel_x + box / 2, y + (psz * 3 + 12) / 2, next_type, psz);
	y += psz * 3 + 12 + 14;

	draw_str(panel_x, y, "Move  <- ->", c_dim); y += th;
	draw_str(panel_x, y, "Rot   Up/Z/X", c_dim); y += th;
	draw_str(panel_x, y, "Soft  Down", c_dim); y += th;
	draw_str(panel_x, y, "Hard  Space", c_dim); y += th;
	draw_str(panel_x, y, "Pause P", c_dim); y += th;
	draw_str(panel_x, y, "New   R", c_dim); y += th;
	draw_str(panel_x, y, "Quit  Q/Esc", c_dim);
}

static void
draw_overlay(const char *l1, const char *l2)
{
	int	th = xfont->ascent + xfont->descent;
	int	bw = board_w - 20, bh = th * 3 + 12;
	int	bx = board_x + 10, by = board_y + (board_h - bh) / 2;

	XSetForeground(dpy, gc, c_board);
	XFillRectangle(dpy, win, gc, bx, by, (unsigned)bw, (unsigned)bh);
	XSetForeground(dpy, gc, c_gold);
	XDrawRectangle(dpy, win, gc, bx, by, (unsigned)bw, (unsigned)bh);
	draw_str_c(board_x + board_w / 2, by + th + 6, l1, c_gold);
	draw_str_c(board_x + board_w / 2, by + th * 2 + 8, l2, c_text);
}

static void
redraw(void)
{
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, (unsigned)win_w, (unsigned)win_h);
	layout();
	draw_board();
	draw_panel();
	if (gameover)
		draw_overlay("GAME OVER", "press R to restart");
	else if (paused)
		draw_overlay("PAUSED", "press P to resume");
	need_draw = 0;
}

/*----------------------------------------------------------------------------*/

static void
handle_key(XKeyEvent *ke)
{
	KeySym	ks;
	char	buf[8];

	XLookupString(ke, buf, (int)sizeof(buf), &ks, NULL);

	if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
		running = 0;
		return;
	}
	if (ks == XK_r || ks == XK_R) {
		new_game();
		return;
	}
	if (gameover)
		return;
	if (ks == XK_p || ks == XK_P) {
		paused = !paused;
		last_drop = kernel_tic_ms(0);
		need_draw = 1;
		return;
	}
	if (paused)
		return;

	switch (ks) {
	case XK_Left:	try_move(-1, 0);	break;
	case XK_Right:	try_move(1, 0);		break;
	case XK_Down:	soft_drop();		break;
	case XK_Up:
	case XK_x:
	case XK_X:	try_rotate(1);		break;
	case XK_z:
	case XK_Z:	try_rotate(-1);		break;
	case XK_space:	hard_drop();		break;
	default:	break;
	}
}

static void
handle_event(XEvent *ev)
{
	switch (ev->type) {
	case Expose:
		if (ev->xexpose.count == 0)
			need_draw = 1;
		break;
	case ConfigureNotify:
		if (ev->xconfigure.width != win_w || ev->xconfigure.height != win_h) {
			win_w = ev->xconfigure.width;
			win_h = ev->xconfigure.height;
			need_draw = 1;
		}
		break;
	case KeyPress:
		handle_key(&ev->xkey);
		break;
	case ClientMessage:
		if ((Atom)ev->xclient.data.l[0] == wm_delete)
			running = 0;
		break;
	default:
		break;
	}
}

static void
tick(void)
{
	uint64_t now;

	if (gameover || paused)
		return;
	now = kernel_tic_ms(0);
	if ((long)(now - last_drop) >= drop_ms) {
		if (!collides(cur_type, cur_rot, cur_x, cur_y + 1)) {
			cur_y++;
			need_draw = 1;
		} else {
			lock_piece();
		}
		last_drop = now;
	}
}

int
main(int argc, char **argv)
{
	XEvent	ev;
	int	i;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xtetris: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg     = alloc_rgb(0x1e, 0x1e, 0x26);
	c_board  = alloc_rgb(0x0a, 0x0a, 0x0e);
	c_slot   = alloc_rgb(0x16, 0x16, 0x1e);
	c_text   = alloc_rgb(0xe0, 0xe0, 0xe8);
	c_dim    = alloc_rgb(0x8a, 0x8a, 0x96);
	c_gold   = alloc_rgb(0xf0, 0xc8, 0x40);
	c_border = alloc_rgb(0x50, 0x50, 0x60);

	for (i = 0; i < NPIECE; i++) {
		int r = piece_rgb[i][0], g = piece_rgb[i][1], b = piece_rgb[i][2];
		pc_base[i]  = alloc_rgb(r, g, b);
		pc_light[i] = alloc_rgb(r + (255 - r) / 2, g + (255 - g) / 2, b + (255 - b) / 2);
		pc_dark[i]  = alloc_rgb(r / 2, g / 2, b / 2);
	}

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xtetris: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - DEF_W) / 2,
			(DisplayHeight(dpy, scr) - DEF_H) / 2,
			DEF_W, DEF_H, 0, c_border, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | KeyPressMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	srand((unsigned int)kernel_tic_ms(0));
	init_pieces();
	new_game();

	while (running) {
		/* drains input and, through GsSelect(POLL), presents last frame*/
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask, &ev))
			handle_event(&ev);

		tick();
		if (need_draw)
			redraw();
		proc_usleep(TICK_US);
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
