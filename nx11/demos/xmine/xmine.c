/*
 * xmine -- a pure-Xlib Minesweeper clone for NX11 on EwokOS.
 *
 * Like x11demo/xtetris this is ordinary X11 code: libNX11 turns every call
 * into a Nano-X server call and the top-level window becomes a real EwokOS
 * xwin window (NANOWM=0), so the desktop window manager owns the frame and the
 * move/resize/close gestures.  Nothing here knows about EwokOS except the two
 * timing primitives that drive the 1 Hz game clock - Xlib has no portable
 * timer and select() on ConnectionNumber() does not work because NX11 is a
 * single-process (NONETWORK) server whose display fd is not a real socket.
 *
 * The loop is a non-blocking poll: XCheckWindowEvent() -> GrGetTypedEvent()
 * calls GsSelect(GR_TIMEOUT_POLL) on every invocation, which runs the screen
 * driver PreSelect and therefore flushes and presents whatever was drawn on the
 * previous iteration.  proc_usleep() between polls yields the CPU.
 *
 * Controls:  Button1 reveal / chord, Button3 (or Button2) flag, click the
 *            smiley to restart.  Keys 1/2/3 pick Beginner/Intermediate/Expert,
 *            R restarts, Q/Esc quits.  The first click is always safe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep   */
#include <ewoksys/kernel_tic.h>		/* kernel_tic_ms */

#define TITLE		"Minesweeper (NX11)"
#define FONT_NAME	"fixed"		/* mapped to the builtin SystemFixed font*/

#define MAXR		16
#define MAXC		30
#define OUTER		6
#define PAD		6
#define MIN_HEADER	40
#define FOOTER_H	18
#define POLL_US		50000		/* 50 ms poll slice -> 20 Hz*/
#define FSTACK_MAX	4096
#define NDIFF		3

/* cell states*/
#define ST_HIDDEN	0
#define ST_REVEALED	1
#define ST_FLAG		2
#define ST_QUESTION	3

/* game status*/
#define GS_FRESH	0
#define GS_PLAYING	1
#define GS_WON		2
#define GS_LOST		3

/* smiley faces*/
#define FACE_NORMAL	0
#define FACE_SURPRISED	1
#define FACE_DEAD	2
#define FACE_COOL	3

static const int	diff_cols[NDIFF]  = { 9, 16, 30 };
static const int	diff_rows[NDIFF]  = { 9, 16, 16 };
static const int	diff_mines[NDIFF] = { 10, 40, 99 };
static const char *	diff_name[NDIFF]  = { "Beginner", "Intermediate", "Expert" };

/* 7-segment bitmaps: bit0=a(top) 1=b(top-right) 2=c(bot-right) 3=d(bottom)
   4=e(bot-left) 5=f(top-left) 6=g(middle)*/
static const unsigned char seg_map[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_face, c_white, c_gray, c_dkgray, c_black;
static unsigned long	c_red, c_ledoff, c_yellow, c_dim;
static unsigned long	c_num[9];

static int		win_w, win_h;
static int		cell;
static int		board_x, board_y, board_w, board_h;
static int		header_y, header_h;
static int		digit_w, digit_h, led_w, led_h, smile_sz;
static int		led_mines_x, led_mines_y, led_time_x, led_time_y;
static int		smile_cx, smile_cy, footer_y;

static int		cols, rows, total_mines, cur_diff;
static unsigned char	mine_[MAXR][MAXC];
static unsigned char	adj_[MAXR][MAXC];
static unsigned char	st_[MAXR][MAXC];
static int		mines_left, revealed_count, elapsed, status;
static int		dead_r = -1, dead_c = -1;
static uint64_t		start_tic;
static int		running = 1, need_draw = 1, mouse_down = 0;
static int		press_button, press_x = -1, press_y = -1;
static int		fstack[FSTACK_MAX];

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

/*----------------------------------------------------------------------------*/
/* game rules                                                                   */
/*----------------------------------------------------------------------------*/

static void
compute_adj(void)
{
	int r, c, dr, dc, n;

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			if (mine_[r][c]) { adj_[r][c] = 0; continue; }
			n = 0;
			for (dr = -1; dr <= 1; dr++)
				for (dc = -1; dc <= 1; dc++) {
					int nr = r + dr, nc = c + dc;
					if (dr == 0 && dc == 0) continue;
					if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
					if (mine_[nr][nc]) n++;
				}
			adj_[r][c] = (unsigned char)n;
		}
}

/* mines are laid only after the first reveal, and never in the 3x3 area around
   it, so the opening click always cascades into a safe region*/
static void
generate_mines(int safe_r, int safe_c)
{
	int placed = 0, guard = 0, r, c;

	memset(mine_, 0, sizeof(mine_));
	while (placed < total_mines) {
		guard++;
		r = rand() % rows;
		c = rand() % cols;
		if (mine_[r][c])
			continue;
		/* after a long struggle give up on the safe zone (never happens
		   for the built-in levels, but keeps the loop bounded)*/
		if (guard < total_mines * 100 &&
		    r >= safe_r - 1 && r <= safe_r + 1 &&
		    c >= safe_c - 1 && c <= safe_c + 1)
			continue;
		mine_[r][c] = 1;
		placed++;
	}
	compute_adj();
}

static void
lose(int r, int c)
{
	int rr, cc;

	status = GS_LOST;
	dead_r = r;
	dead_c = c;
	st_[r][c] = ST_REVEALED;		/* the mine that exploded*/
	for (rr = 0; rr < rows; rr++)
		for (cc = 0; cc < cols; cc++)
			if (mine_[rr][cc] && st_[rr][cc] != ST_FLAG)
				st_[rr][cc] = ST_REVEALED;
}

static void
check_win(void)
{
	int r, c;

	if (status != GS_PLAYING)
		return;
	if (revealed_count == rows * cols - total_mines) {
		status = GS_WON;
		mines_left = 0;
		for (r = 0; r < rows; r++)
			for (c = 0; c < cols; c++)
				if (mine_[r][c] && st_[r][c] != ST_FLAG)
					st_[r][c] = ST_FLAG;
	}
}

/* iterative flood fill; reveals the seed and, through every blank cell, all
   connected blanks and their numbered rim.  Hitting a mine ends the game.*/
static void
flood_fill(int sr, int sc)
{
	int top = 0, dr, dc;

	fstack[top++] = sr * MAXC + sc;
	while (top > 0) {
		int code = fstack[--top];
		int r = code / MAXC, c = code % MAXC;

		if (st_[r][c] != ST_HIDDEN && st_[r][c] != ST_QUESTION)
			continue;
		st_[r][c] = ST_REVEALED;
		revealed_count++;
		if (mine_[r][c]) {
			lose(r, c);
			return;
		}
		if (adj_[r][c] != 0)
			continue;
		for (dr = -1; dr <= 1; dr++)
			for (dc = -1; dc <= 1; dc++) {
				int nr = r + dr, nc = c + dc;
				if (dr == 0 && dc == 0) continue;
				if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
				if (st_[nr][nc] != ST_HIDDEN && st_[nr][nc] != ST_QUESTION)
					continue;
				if (top < FSTACK_MAX)
					fstack[top++] = nr * MAXC + nc;
			}
	}
}

static void
reveal_click(int r, int c)
{
	if (status == GS_WON || status == GS_LOST)
		return;
	if (st_[r][c] != ST_HIDDEN)
		return;
	if (status == GS_FRESH) {
		generate_mines(r, c);
		status = GS_PLAYING;
		start_tic = kernel_tic_ms(0);
		elapsed = 0;
	}
	flood_fill(r, c);
	check_win();
}

/* chording: on a satisfied number, open the still-hidden neighbours*/
static void
chord(int r, int c)
{
	int dr, dc, flags = 0;

	if (status == GS_WON || status == GS_LOST)
		return;
	if (st_[r][c] != ST_REVEALED || adj_[r][c] == 0)
		return;
	for (dr = -1; dr <= 1; dr++)
		for (dc = -1; dc <= 1; dc++) {
			int nr = r + dr, nc = c + dc;
			if (dr == 0 && dc == 0) continue;
			if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
			if (st_[nr][nc] == ST_FLAG) flags++;
		}
	if (flags != adj_[r][c])
		return;
	for (dr = -1; dr <= 1; dr++)
		for (dc = -1; dc <= 1; dc++) {
			int nr = r + dr, nc = c + dc;
			if (status == GS_LOST) return;
			if (dr == 0 && dc == 0) continue;
			if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
			if (st_[nr][nc] == ST_HIDDEN || st_[nr][nc] == ST_QUESTION)
				flood_fill(nr, nc);
		}
	check_win();
}

static void
flag_click(int r, int c)
{
	if (status == GS_WON || status == GS_LOST)
		return;
	switch (st_[r][c]) {
	case ST_HIDDEN:	st_[r][c] = ST_FLAG;     mines_left--;	break;
	case ST_FLAG:	st_[r][c] = ST_QUESTION; mines_left++;	break;
	case ST_QUESTION: st_[r][c] = ST_HIDDEN;			break;
	default:	break;
	}
}

static void
new_game(void)
{
	int r, c;

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			mine_[r][c] = 0;
			adj_[r][c] = 0;
			st_[r][c] = ST_HIDDEN;
		}
	mines_left = total_mines;
	revealed_count = 0;
	elapsed = 0;
	status = GS_FRESH;
	dead_r = dead_c = -1;
	mouse_down = 0;
	press_button = 0;
	need_draw = 1;
}

static void
compute_want_size(int *w, int *h)
{
	int base = 26;
	int dw = base * 3 / 5;
	int hh;

	if (dw < 9) dw = 9;
	if (dw > 20) dw = 20;
	hh = (dw * 2 + 8) + 10;			/* led_h + slack*/
	*w = cols * base + 2 * (OUTER + PAD);
	*h = rows * base + hh + FOOTER_H + 2 * (OUTER + PAD);
}

static void
set_difficulty(int d)
{
	int w, h;

	if (d < 0) d = 0;
	if (d >= NDIFF) d = NDIFF - 1;
	cur_diff = d;
	cols = diff_cols[d];
	rows = diff_rows[d];
	total_mines = diff_mines[d];
	compute_want_size(&w, &h);
	win_w = w;
	win_h = h;
	XResizeWindow(dpy, win, (unsigned)w, (unsigned)h);
	new_game();
}

/*----------------------------------------------------------------------------*/
/* layout + drawing                                                             */
/*----------------------------------------------------------------------------*/

static void
layout(void)
{
	int cw, ch;

	cw = (win_w - 2 * OUTER - 2 * PAD) / cols;
	ch = (win_h - 2 * OUTER - 2 * PAD - MIN_HEADER - FOOTER_H) / rows;
	cell = cw < ch ? cw : ch;
	if (cell < 16) cell = 16;
	if (cell > 42) cell = 42;

	digit_w = cell * 3 / 5;
	if (digit_w < 9) digit_w = 9;
	if (digit_w > 20) digit_w = 20;
	digit_h = digit_w * 2;
	led_w = digit_w * 3 + 8;
	led_h = digit_h + 8;
	smile_sz = led_h;
	header_h = led_h + 10;

	board_w = cell * cols;
	board_h = cell * rows;
	board_x = (win_w - board_w) / 2;
	if (board_x < OUTER + PAD) board_x = OUTER + PAD;
	header_y = OUTER;
	board_y = header_y + header_h + PAD;
	footer_y = board_y + board_h + 4;

	led_mines_x = board_x + 4;
	led_mines_y = header_y + (header_h - led_h) / 2;
	led_time_x = board_x + board_w - led_w - 4;
	led_time_y = led_mines_y;
	smile_cx = board_x + board_w / 2;
	smile_cy = header_y + header_h / 2;
}

/* raised 3-D bevel: white top/left, gray bottom/right over a silver face*/
static void
bevel_raised(int x, int y, int w, int h)
{
	XSetForeground(dpy, gc, c_face);
	XFillRectangle(dpy, win, gc, x, y, (unsigned)w, (unsigned)h);
	XSetForeground(dpy, gc, c_white);
	XDrawLine(dpy, win, gc, x, y, x + w - 1, y);
	XDrawLine(dpy, win, gc, x, y, x, y + h - 1);
	XSetForeground(dpy, gc, c_gray);
	XDrawLine(dpy, win, gc, x, y + h - 1, x + w - 1, y + h - 1);
	XDrawLine(dpy, win, gc, x + w - 1, y, x + w - 1, y + h - 1);
}

/* recessed frame: gray top/left, white bottom/right (drawn over whatever is
   already there, used around the LEDs and the board)*/
static void
bevel_sunken(int x, int y, int w, int h)
{
	XSetForeground(dpy, gc, c_gray);
	XDrawLine(dpy, win, gc, x, y, x + w - 1, y);
	XDrawLine(dpy, win, gc, x, y, x, y + h - 1);
	XSetForeground(dpy, gc, c_white);
	XDrawLine(dpy, win, gc, x, y + h - 1, x + w - 1, y + h - 1);
	XDrawLine(dpy, win, gc, x + w - 1, y, x + w - 1, y + h - 1);
}

static void
text_center(int x, int y, int w, int h, const char *s, unsigned long fg)
{
	int len = (int)strlen(s);
	int tw = XTextWidth(xfont, s, len);
	int th = xfont->ascent + xfont->descent;

	XSetForeground(dpy, gc, fg);
	XDrawString(dpy, win, gc, x + (w - tw) / 2,
			y + (h - th) / 2 + xfont->ascent, s, len);
}

/* one 7-segment digit in a box whose top-left is (x,y)*/
static void
draw_seg(int x, int y, int seg, int on)
{
	int t = digit_w / 4;
	int hw = digit_w, hh = digit_h;
	unsigned long col;

	if (t < 2) t = 2;
	col = on ? c_red : c_ledoff;
	XSetForeground(dpy, gc, col);
	switch (seg) {
	case 0:	XFillRectangle(dpy, win, gc, x + t, y, (unsigned)(hw - 2*t), (unsigned)t); break;
	case 1:	XFillRectangle(dpy, win, gc, x + hw - t, y + t, (unsigned)t, (unsigned)(hh/2 - t)); break;
	case 2:	XFillRectangle(dpy, win, gc, x + hw - t, y + hh/2, (unsigned)t, (unsigned)(hh/2 - t)); break;
	case 3:	XFillRectangle(dpy, win, gc, x + t, y + hh - t, (unsigned)(hw - 2*t), (unsigned)t); break;
	case 4:	XFillRectangle(dpy, win, gc, x, y + hh/2, (unsigned)t, (unsigned)(hh/2 - t)); break;
	case 5:	XFillRectangle(dpy, win, gc, x, y + t, (unsigned)t, (unsigned)(hh/2 - t)); break;
	case 6:	XFillRectangle(dpy, win, gc, x + t, y + hh/2 - t/2, (unsigned)(hw - 2*t), (unsigned)t); break;
	default: break;
	}
}

static void
draw_digit(int x, int y, int d, int only_mid)
{
	unsigned m;
	int i;

	if (only_mid)
		m = 0x40;				/* the '-' minus bar*/
	else
		m = (d >= 0 && d <= 9) ? seg_map[d] : 0;
	for (i = 0; i < 7; i++)
		draw_seg(x, y, i, (m >> i) & 1);
}

/* a three-digit counter with the classic black background; negatives show a
   leading minus, values are clamped to -99..999*/
static void
draw_led(int x, int y, int value)
{
	int v = value, neg = 0, d0, d1, d2, dx, dy;

	XSetForeground(dpy, gc, c_black);
	XFillRectangle(dpy, win, gc, x, y, (unsigned)led_w, (unsigned)led_h);
	bevel_sunken(x, y, led_w, led_h);

	if (v < 0) { neg = 1; v = -v; }
	if (v > 999) v = 999;
	d0 = v / 100; d1 = (v / 10) % 10; d2 = v % 10;
	dx = x + 4;
	dy = y + (led_h - digit_h) / 2;

	if (neg)
		draw_digit(dx, dy, 0, 1);
	else
		draw_digit(dx, dy, d0, 0);
	draw_digit(dx + digit_w, dy, d1, 0);
	draw_digit(dx + digit_w * 2, dy, d2, 0);
}

static void
draw_mine(int x, int y, int sz)
{
	int cx = x + sz / 2, cy = y + sz / 2;
	int r = sz * 3 / 10, e, d, hr;

	if (r < 2) r = 2;
	e = r + sz / 8;
	d = e * 7 / 10;
	XSetForeground(dpy, gc, c_black);
	XDrawLine(dpy, win, gc, cx - e, cy, cx + e, cy);
	XDrawLine(dpy, win, gc, cx, cy - e, cx, cy + e);
	XDrawLine(dpy, win, gc, cx - d, cy - d, cx + d, cy + d);
	XDrawLine(dpy, win, gc, cx - d, cy + d, cx + d, cy - d);
	XFillArc(dpy, win, gc, cx - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
	hr = r / 3;
	if (hr < 1) hr = 1;
	XSetForeground(dpy, gc, c_white);
	XFillArc(dpy, win, gc, cx - r/2 - hr, cy - r/2 - hr, 2*hr, 2*hr, 0, 360*64);
}

static void
draw_flag(int x, int y, int sz)
{
	int px = x + sz / 2;
	int top = y + sz / 5;
	int bot = y + sz * 4 / 5;
	int fw = sz / 4, fh = sz / 4;
	XPoint tri[3];

	if (fw < 3) fw = 3;
	if (fh < 3) fh = 3;
	tri[0].x = (short)px;       tri[0].y = (short)top;
	tri[1].x = (short)px;       tri[1].y = (short)(top + fh);
	tri[2].x = (short)(px - fw); tri[2].y = (short)(top + fh / 2);
	XSetForeground(dpy, gc, c_red);
	XFillPolygon(dpy, win, gc, tri, 3, Nonconvex, CoordModeOrigin);
	XSetForeground(dpy, gc, c_black);
	XDrawLine(dpy, win, gc, px, top, px, bot);
	XDrawLine(dpy, win, gc, x + sz/4, bot, x + sz*3/4, bot);
}

static void
draw_smiley(void)
{
	int sz = smile_sz;
	int x = smile_cx - sz / 2, y = smile_cy - sz / 2;
	int inset = 3, face, ex, ey, er, mx1, mx2, my, md;

	if (status == GS_LOST)		face = FACE_DEAD;
	else if (status == GS_WON)	face = FACE_COOL;
	else if (mouse_down)		face = FACE_SURPRISED;
	else				face = FACE_NORMAL;

	bevel_raised(x, y, sz, sz);
	XSetForeground(dpy, gc, c_yellow);
	XFillArc(dpy, win, gc, x + inset, y + inset,
			sz - 2*inset, sz - 2*inset, 0, 360*64);
	XSetForeground(dpy, gc, c_black);
	XDrawArc(dpy, win, gc, x + inset, y + inset,
			sz - 2*inset, sz - 2*inset, 0, 360*64);

	ex = sz * 3 / 8;			/* eye offset from left*/
	ey = sz * 3 / 8;			/* eye offset from top*/
	er = sz / 12;
	if (er < 1) er = 1;
	mx1 = x + sz / 4;
	mx2 = x + sz * 3 / 4;
	my = y + sz * 5 / 8;
	md = sz / 8;

	XSetForeground(dpy, gc, c_black);
	switch (face) {
	case FACE_DEAD:
		/* two X eyes*/
		XDrawLine(dpy, win, gc, x+ex-er, y+ey-er, x+ex+er, y+ey+er);
		XDrawLine(dpy, win, gc, x+ex-er, y+ey+er, x+ex+er, y+ey-er);
		XDrawLine(dpy, win, gc, x+sz-ex-er, y+ey-er, x+sz-ex+er, y+ey+er);
		XDrawLine(dpy, win, gc, x+sz-ex-er, y+ey+er, x+sz-ex+er, y+ey-er);
		/* frown*/
		XDrawLine(dpy, win, gc, mx1, my + md, x + sz/2, my);
		XDrawLine(dpy, win, gc, x + sz/2, my, mx2, my + md);
		break;
	case FACE_COOL:
		/* sunglasses*/
		XFillRectangle(dpy, win, gc, x + sz/5, y + ey - er,
				(unsigned)(sz - 2*(sz/5)), (unsigned)(er*2 + 2));
		XDrawLine(dpy, win, gc, mx1, my, x + sz/2, my + md);
		XDrawLine(dpy, win, gc, x + sz/2, my + md, mx2, my);
		break;
	case FACE_SURPRISED:
		XFillArc(dpy, win, gc, x+ex-er, y+ey-er, 2*er, 2*er, 0, 360*64);
		XFillArc(dpy, win, gc, x+sz-ex-er, y+ey-er, 2*er, 2*er, 0, 360*64);
		XFillArc(dpy, win, gc, x+sz/2-er, my-er, 2*er, 2*er+2, 0, 360*64);
		break;
	default:				/* FACE_NORMAL*/
		XFillArc(dpy, win, gc, x+ex-er, y+ey-er, 2*er, 2*er, 0, 360*64);
		XFillArc(dpy, win, gc, x+sz-ex-er, y+ey-er, 2*er, 2*er, 0, 360*64);
		XDrawLine(dpy, win, gc, mx1, my, x + sz/2, my + md);
		XDrawLine(dpy, win, gc, x + sz/2, my + md, mx2, my);
		break;
	}
}

static void
draw_cell(int r, int c)
{
	int x = board_x + c * cell;
	int y = board_y + r * cell;
	int st = st_[r][c];

	if (st == ST_REVEALED) {
		XSetForeground(dpy, gc, c_face);
		XFillRectangle(dpy, win, gc, x, y, (unsigned)cell, (unsigned)cell);
		if (mine_[r][c]) {
			if (r == dead_r && c == dead_c) {
				XSetForeground(dpy, gc, c_red);
				XFillRectangle(dpy, win, gc, x, y,
						(unsigned)cell, (unsigned)cell);
			}
			draw_mine(x, y, cell);
		} else if (adj_[r][c] > 0) {
			char buf[2];
			buf[0] = (char)('0' + adj_[r][c]);
			buf[1] = '\0';
			text_center(x, y, cell, cell, buf, c_num[adj_[r][c]]);
		}
		/* thin grid lines give the revealed field its flat look*/
		XSetForeground(dpy, gc, c_gray);
		XDrawLine(dpy, win, gc, x + cell - 1, y, x + cell - 1, y + cell - 1);
		XDrawLine(dpy, win, gc, x, y + cell - 1, x + cell - 1, y + cell - 1);
	} else {
		bevel_raised(x, y, cell, cell);
		if (st == ST_FLAG) {
			draw_flag(x, y, cell);
			if (status == GS_LOST && !mine_[r][c]) {
				/* a flag on a safe cell: cross it out in red*/
				int m = cell / 4;
				XSetForeground(dpy, gc, c_red);
				XDrawLine(dpy, win, gc, x+m, y+m, x+cell-m, y+cell-m);
				XDrawLine(dpy, win, gc, x+m, y+cell-m, x+cell-m, y+m);
			}
		} else if (st == ST_QUESTION) {
			text_center(x, y, cell, cell, "?", c_black);
		}
	}
}

static void
redraw(void)
{
	int r, c;

	layout();

	XSetForeground(dpy, gc, c_face);
	XFillRectangle(dpy, win, gc, 0, 0, (unsigned)win_w, (unsigned)win_h);

	/* header: mines-left LED, smiley, timer LED, in a recessed strip*/
	draw_led(led_mines_x, led_mines_y, mines_left);
	draw_led(led_time_x, led_time_y, elapsed);
	draw_smiley();

	/* board sits in a recessed frame*/
	bevel_sunken(board_x - 3, board_y - 3, board_w + 6, board_h + 6);
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
			draw_cell(r, c);

	/* footer hint line*/
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%s  -  1/2/3 level  R new  Q quit",
				diff_name[cur_diff]);
		text_center(board_x, footer_y, board_w, FOOTER_H, buf, c_dim);
	}

	need_draw = 0;
}

/*----------------------------------------------------------------------------*/
/* input                                                                        */
/*----------------------------------------------------------------------------*/

static int
cell_at(int px, int py, int *r, int *c)
{
	int cc, rr;

	cc = (px - board_x) / cell;
	rr = (py - board_y) / cell;
	if (px < board_x || py < board_y)
		return 0;
	if (cc < 0 || cc >= cols || rr < 0 || rr >= rows)
		return 0;
	*r = rr;
	*c = cc;
	return 1;
}

static int
over_smiley(int px, int py)
{
	int h = smile_sz / 2;

	return px >= smile_cx - h && px < smile_cx + h &&
	       py >= smile_cy - h && py < smile_cy + h;
}

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
	if (ks == XK_1) { set_difficulty(0); return; }
	if (ks == XK_2) { set_difficulty(1); return; }
	if (ks == XK_3) { set_difficulty(2); return; }
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

	case ButtonPress:
		press_button = ev->xbutton.button;
		press_x = ev->xbutton.x;
		press_y = ev->xbutton.y;
		/* the classic "surprised" face only shows for a left press that
		   could still become a move*/
		mouse_down = (press_button == Button1 &&
			      !over_smiley(press_x, press_y) &&
			      status != GS_WON && status != GS_LOST);
		need_draw = 1;
		break;

	case ButtonRelease: {
		int btn = ev->xbutton.button;
		int rx = ev->xbutton.x, ry = ev->xbutton.y;
		int r, c;

		mouse_down = 0;
		if (btn == Button1 && press_button == Button1) {
			if (over_smiley(rx, ry)) {
				new_game();
			} else if (cell_at(rx, ry, &r, &c)) {
				/* only commit if released on the pressed cell, so
				   dragging off cancels the move*/
				int pr, pc;
				if (cell_at(press_x, press_y, &pr, &pc) &&
				    pr == r && pc == c) {
					if (st_[r][c] == ST_REVEALED)
						chord(r, c);
					else
						reveal_click(r, c);
				}
			}
		} else if (btn == Button3) {
			if (cell_at(rx, ry, &r, &c))
				flag_click(r, c);
		} else if (btn == Button2) {
			if (cell_at(rx, ry, &r, &c))
				chord(r, c);
		}
		press_button = 0;
		press_x = press_y = -1;
		need_draw = 1;
		break;
	}

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
	int e;

	if (status != GS_PLAYING)
		return;
	now = kernel_tic_ms(0);
	e = (int)((now - start_tic) / 1000);
	if (e > 999) e = 999;
	if (e != elapsed) {
		elapsed = e;
		need_draw = 1;
	}
}

/*----------------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
	XEvent	ev;
	int	w, h;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xmine: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_face   = alloc_rgb(0xc0, 0xc0, 0xc0);
	c_white  = alloc_rgb(0xff, 0xff, 0xff);
	c_gray   = alloc_rgb(0x80, 0x80, 0x80);
	c_dkgray = alloc_rgb(0x40, 0x40, 0x40);
	c_black  = alloc_rgb(0x00, 0x00, 0x00);
	c_red    = alloc_rgb(0xff, 0x00, 0x00);
	c_ledoff = alloc_rgb(0x40, 0x00, 0x00);
	c_yellow = alloc_rgb(0xff, 0xff, 0x00);
	c_dim    = alloc_rgb(0x50, 0x50, 0x58);

	/* the classic number colours, indexed 1..8*/
	c_num[1] = alloc_rgb(0x00, 0x00, 0xff);
	c_num[2] = alloc_rgb(0x00, 0x80, 0x00);
	c_num[3] = alloc_rgb(0xff, 0x00, 0x00);
	c_num[4] = alloc_rgb(0x00, 0x00, 0x80);
	c_num[5] = alloc_rgb(0x80, 0x00, 0x00);
	c_num[6] = alloc_rgb(0x00, 0x80, 0x80);
	c_num[7] = alloc_rgb(0x00, 0x00, 0x00);
	c_num[8] = alloc_rgb(0x80, 0x80, 0x80);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xmine: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	/* start at Beginner; set_difficulty() resizes for the other levels*/
	cur_diff = 0;
	cols = diff_cols[0];
	rows = diff_rows[0];
	total_mines = diff_mines[0];
	compute_want_size(&w, &h);
	win_w = w;
	win_h = h;

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - w) / 2,
			(DisplayHeight(dpy, scr) - h) / 2,
			(unsigned)w, (unsigned)h, 0, c_dkgray, c_face);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | KeyPressMask |
		ButtonPressMask | ButtonReleaseMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	srand((unsigned int)kernel_tic_ms(0));
	new_game();

	while (running) {
		/* drains input and, through GsSelect(POLL), presents last frame*/
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask |
				ButtonPressMask | ButtonReleaseMask, &ev))
			handle_event(&ev);

		tick();
		if (need_draw)
			redraw();
		proc_usleep(POLL_US);
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
