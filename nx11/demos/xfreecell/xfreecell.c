/*
 * xfreecell -- a pure-Xlib FreeCell solitaire clone for NX11 on EwokOS.
 *
 * Like x11demo this is ordinary X11 code: libNX11 turns every call into a
 * Nano-X server call and the top-level window becomes a real EwokOS xwin window
 * (NANOWM=0), so the desktop window manager owns the frame and the move/resize/
 * close gestures.  Nothing here knows about EwokOS.
 *
 * FreeCell is turn based and needs no clock, so the loop is the simple blocking
 * one x11demo uses: XNextEvent() -> GrGetNextEvent() spins GsSelect() while the
 * queue is empty, which runs the screen driver PreSelect and therefore flushes
 * and presents whatever the previous turn drew.  No select() on
 * ConnectionNumber() (NX11 is a single-process NONETWORK server whose display
 * fd is not a real socket) and no timer primitive are needed.
 *
 * The card faces are drawn with vectors: the builtin "fixed" font is Latin-1
 * only and has no club/diamond/heart/spade glyphs.
 *
 * Controls:  click a card (or a whole valid run) to select, click a destination
 *            to move.  N/R deal a new game, U undo, A auto-collect to the
 *            foundations, Q/Esc quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define TITLE		"FreeCell (NX11)"
#define FONT_NAME	"fixed"		/* mapped to the builtin SystemFixed font*/

#define NCOLS		8
#define NCELLS		4
#define NFOUND		4
#define NSUIT		4
#define NRANK		13
#define NCARD		52
#define MAXH		32		/* max tableau column height in valid play*/
#define MAXHIST		256

#define MARGIN		10
#define COLGAP		8
#define TOPGAP		18
#define FOOTER_H	20
#define VSTACK		16		/* tableau cards we try to keep visible*/

#define DEF_W		580
#define DEF_H		500

/* suits: 0 club(black) 1 diamond(red) 2 heart(red) 3 spade(black)*/
#define SUIT(id)	((id) / NRANK)
#define RANK(id)	((id) % NRANK + 1)	/* 1..13, A=1 J=11 Q=12 K=13*/
#define CARD(s, r)	((s) * NRANK + (r) - 1)

/* move locations*/
#define LOC_TAB(c)	(c)			/* 0..7   */
#define LOC_FREE(i)	(NCOLS + (i))		/* 8..11  */
#define LOC_FOUND(s)	(NCOLS + NCELLS + (s))	/* 12..15 */

typedef struct { int src, dst, n; } Move;

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_card, c_line, c_sel, c_red, c_black;
static unsigned long	c_slot, c_faint, c_text;

static int		win_w = DEF_W, win_h = DEF_H;
static int		card_w, card_h, fan, top_y, tableau_y, footer_y;
static int		col_x[NCOLS];

/* game state*/
static int		tab_n[NCOLS];
static int		tab[NCOLS][MAXH];
static int		freecell[NCELLS];		/* card id or -1*/
static int		found[NFOUND];			/* 0..13 per suit*/
static Move		hist[MAXHIST];
static int		hist_n;

static int		sel_active, sel_loc, sel_js, sel_n;
static int		won, running = 1, need_draw = 1;

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

static int
card_color(int id)
{
	int s = SUIT(id);
	return (s == 0 || s == 3) ? 0 : 1;	/* 0 black, 1 red*/
}

static const char *
rank_str(int id)
{
	static const char *rs[NRANK] = {
		"A","2","3","4","5","6","7","8","9","10","J","Q","K"
	};
	return rs[RANK(id) - 1];
}

/*----------------------------------------------------------------------------*/
/* card art                                                                     */
/*----------------------------------------------------------------------------*/

static void
fill_tri(int x0, int y0, int x1, int y1, int x2, int y2)
{
	XPoint	p[3];

	p[0].x = (short)x0; p[0].y = (short)y0;
	p[1].x = (short)x1; p[1].y = (short)y1;
	p[2].x = (short)x2; p[2].y = (short)y2;
	XFillPolygon(dpy, win, gc, p, 3, Convex, CoordModeOrigin);
}

static void
fill_diamond(int cx, int cy, int sz)
{
	XPoint	p[4];

	p[0].x = (short)cx;         p[0].y = (short)(cy - sz/2);
	p[1].x = (short)(cx + sz/2); p[1].y = (short)cy;
	p[2].x = (short)cx;         p[2].y = (short)(cy + sz/2);
	p[3].x = (short)(cx - sz/2); p[3].y = (short)cy;
	XFillPolygon(dpy, win, gc, p, 4, Convex, CoordModeOrigin);
}

/* vector suit glyph centred on (cx,cy) inside a sz x sz box*/
static void
draw_suit(int cx, int cy, int sz, int suit, unsigned long col)
{
	int r = sz / 4;

	if (sz < 4)
		return;
	XSetForeground(dpy, gc, col);
	switch (suit) {
	case 1:					/* diamond*/
		fill_diamond(cx, cy, sz);
		break;
	case 2:					/* heart*/
		XFillArc(dpy, win, gc, cx - sz/2, cy - sz/2, sz/2, sz/2, 0, 360*64);
		XFillArc(dpy, win, gc, cx,      cy - sz/2, sz/2, sz/2, 0, 360*64);
		fill_tri(cx - sz/2 + 1, cy - sz/8, cx + sz/2 - 1, cy - sz/8, cx, cy + sz/2);
		break;
	case 3:					/* spade*/
		fill_tri(cx, cy - sz/2, cx - sz/2, cy + sz/8, cx + sz/2, cy + sz/8);
		XFillArc(dpy, win, gc, cx - sz/2, cy - sz/8, sz/3, sz/3, 0, 360*64);
		XFillArc(dpy, win, gc, cx + sz/2 - sz/3, cy - sz/8, sz/3, sz/3, 0, 360*64);
		XFillRectangle(dpy, win, gc, cx - sz/12, cy + sz/8,
				(unsigned)(sz/6), (unsigned)(sz/3));
		break;
	default:				/* club*/
		XFillArc(dpy, win, gc, cx - r, cy - sz/4 - r, 2*r, 2*r, 0, 360*64);
		XFillArc(dpy, win, gc, cx - sz/4 - r, cy + sz/8 - r, 2*r, 2*r, 0, 360*64);
		XFillArc(dpy, win, gc, cx + sz/4 - r, cy + sz/8 - r, 2*r, 2*r, 0, 360*64);
		XFillRectangle(dpy, win, gc, cx - sz/12, cy + sz/8,
				(unsigned)(sz/6), (unsigned)(sz/3));
		break;
	}
}

static void
draw_card(int x, int y, int id, int sel)
{
	unsigned long col = card_color(id) ? c_red : c_black;
	int	suit = SUIT(id);
	int	ssz = card_w / 4;
	int	bsz = card_w * 3 / 5;
	const char *rs = rank_str(id);
	int	len = (int)strlen(rs);

	XSetForeground(dpy, gc, c_card);
	XFillRectangle(dpy, win, gc, x, y, (unsigned)card_w, (unsigned)card_h);
	XSetForeground(dpy, gc, sel ? c_sel : c_line);
	XDrawRectangle(dpy, win, gc, x, y, (unsigned)card_w, (unsigned)card_h);
	if (sel)
		XDrawRectangle(dpy, win, gc, x + 1, y + 1,
				(unsigned)(card_w - 2), (unsigned)(card_h - 2));

	if (ssz < 6) ssz = 6;
	XSetForeground(dpy, gc, col);
	XDrawString(dpy, win, gc, x + 3, y + 3 + xfont->ascent, rs, len);
	draw_suit(x + 3 + ssz/2, y + 5 + xfont->ascent + xfont->descent + ssz/2,
			ssz, suit, col);
	draw_suit(x + card_w/2, y + card_h/2 + card_h/10, bsz, suit, col);
}

static void
draw_slot(int x, int y, int faint_suit)
{
	XSetForeground(dpy, gc, c_slot);
	XDrawRectangle(dpy, win, gc, x, y, (unsigned)card_w, (unsigned)card_h);
	XDrawRectangle(dpy, win, gc, x + 1, y + 1,
			(unsigned)(card_w - 2), (unsigned)(card_h - 2));
	if (faint_suit >= 0)
		draw_suit(x + card_w/2, y + card_h/2, card_w*3/5, faint_suit, c_faint);
}

/*----------------------------------------------------------------------------*/
/* layout + redraw                                                              */
/*----------------------------------------------------------------------------*/

static void
layout(void)
{
	int cw_w, ch_h, cw_h, c, avail_h;

	cw_w = (win_w - 2*MARGIN - (NCOLS-1)*COLGAP) / NCOLS;
	avail_h = win_h - 2*MARGIN - TOPGAP - FOOTER_H;
	ch_h = avail_h / 5;			/* card_h + (VSTACK-1)*fan ~ 4.75 card_h*/
	cw_h = ch_h * 5 / 7;
	card_w = cw_w < cw_h ? cw_w : cw_h;
	if (card_w < 24) card_w = 24;
	if (card_w > 96) card_w = 96;
	card_h = card_w * 7 / 5;
	fan = card_h / 4;
	if (fan < 10) fan = 10;

	top_y = MARGIN;
	tableau_y = MARGIN + card_h + TOPGAP;
	footer_y = win_h - FOOTER_H;
	for (c = 0; c < NCOLS; c++)
		col_x[c] = MARGIN + c * (card_w + COLGAP);
}

static int
is_sel(int loc, int j)
{
	if (!sel_active || loc != sel_loc)
		return 0;
	if (loc < NCOLS)			/* tableau: whole run highlighted*/
		return j >= sel_js;
	return 1;
}

static void
redraw(void)
{
	char	buf[96];
	int	c, i, s, j;

	layout();

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, (unsigned)win_w, (unsigned)win_h);

	for (i = 0; i < NCELLS; i++) {
		int x = col_x[i];
		if (freecell[i] >= 0)
			draw_card(x, top_y, freecell[i], is_sel(LOC_FREE(i), 0));
		else
			draw_slot(x, top_y, -1);
	}
	for (s = 0; s < NFOUND; s++) {
		int x = col_x[NCOLS/2 + s];
		if (found[s] > 0)
			draw_card(x, top_y, CARD(s, found[s]), is_sel(LOC_FOUND(s), 0));
		else
			draw_slot(x, top_y, s);
	}
	for (c = 0; c < NCOLS; c++) {
		int x = col_x[c];
		if (tab_n[c] == 0) {
			draw_slot(x, tableau_y, -1);
			continue;
		}
		for (j = 0; j < tab_n[c]; j++)
			draw_card(x, tableau_y + j*fan, tab[c][j],
					is_sel(LOC_TAB(c), j));
	}

	if (won) {
		int bw = 8 * card_w, bh = card_h;
		int bx = (win_w - bw)/2, by = (win_h - bh)/2;
		XSetForeground(dpy, gc, c_card);
		XFillRectangle(dpy, win, gc, bx, by, (unsigned)bw, (unsigned)bh);
		XSetForeground(dpy, gc, c_sel);
		XDrawRectangle(dpy, win, gc, bx, by, (unsigned)bw, (unsigned)bh);
		snprintf(buf, sizeof(buf), "YOU WIN!  press N for a new game");
		{
			int tw = XTextWidth(xfont, buf, (int)strlen(buf));
			int th = xfont->ascent + xfont->descent;
			XSetForeground(dpy, gc, c_black);
			XDrawString(dpy, win, gc, bx + (bw-tw)/2,
					by + (bh-th)/2 + xfont->ascent,
					buf, (int)strlen(buf));
		}
	} else {
		snprintf(buf, sizeof(buf), "N new   U undo   A auto   Q quit");
		{
			int tw = XTextWidth(xfont, buf, (int)strlen(buf));
			XSetForeground(dpy, gc, c_text);
			XDrawString(dpy, win, gc, (win_w-tw)/2,
					footer_y + xfont->ascent, buf, (int)strlen(buf));
		}
	}

	need_draw = 0;
}

/*----------------------------------------------------------------------------*/
/* rules                                                                        */
/*----------------------------------------------------------------------------*/

static int
count_empty_cells(void)
{
	int i, n = 0;
	for (i = 0; i < NCELLS; i++)
		if (freecell[i] < 0) n++;
	return n;
}

static int
count_empty_cols(void)
{
	int c, n = 0;
	for (c = 0; c < NCOLS; c++)
		if (tab_n[c] == 0) n++;
	return n;
}

/* is tab[c][js..top] a valid descending, alternating-colour run?*/
static int
valid_run(int c, int js)
{
	int k;
	for (k = js; k < tab_n[c] - 1; k++) {
		int a = tab[c][k], b = tab[c][k+1];
		if (RANK(b) != RANK(a) - 1) return 0;
		if (card_color(a) == card_color(b)) return 0;
	}
	return 1;
}

/* how many cards may be moved onto column cd right now (the supermove limit)*/
static int
tab_capacity(int cd)
{
	int free = count_empty_cells();
	int ec = count_empty_cols();
	int temps = (tab_n[cd] == 0) ? ec - 1 : ec;

	if (temps < 0) temps = 0;
	if (temps > 8) temps = 8;
	return (free + 1) * (1 << temps);
}

static int
can_place_tab(int card0, int n, int cd)
{
	if (tab_n[cd] > 0) {
		int top = tab[cd][tab_n[cd]-1];
		if (RANK(card0) != RANK(top) - 1) return 0;
		if (card_color(card0) == card_color(top)) return 0;
	}
	if (n > tab_capacity(cd)) return 0;
	if (tab_n[cd] + n > MAXH) return 0;
	return 1;
}

static int
get_src_card(int src, int idx)
{
	if (src < NCOLS)			return tab[src][idx];
	if (src < NCOLS + NCELLS)		return freecell[src - NCOLS];
	{ int s = src - NCOLS - NCELLS;		return CARD(s, found[s]); }
}

static void
remove_from_src(int src, int js, int n)
{
	(void)n;
	if (src < NCOLS)			tab_n[src] = js;
	else if (src < NCOLS + NCELLS)		freecell[src - NCOLS] = -1;
	else					found[src - NCOLS - NCELLS]--;
}

static void
push_move(int src, int dst, int n)
{
	if (hist_n >= MAXHIST) {
		memmove(hist, hist + 1, (MAXHIST-1) * sizeof(Move));
		hist_n = MAXHIST - 1;
	}
	hist[hist_n].src = src;
	hist[hist_n].dst = dst;
	hist[hist_n].n = n;
	hist_n++;
}

static void
check_win(void)
{
	int s;
	won = 1;
	for (s = 0; s < NFOUND; s++)
		if (found[s] != NRANK) { won = 0; break; }
}

/* move the selected run/card from (src,sjs) onto dst; returns 1 on success*/
static int
do_move(int src, int sjs, int dst)
{
	int n, card0, i;

	if (src == dst)
		return 0;
	if (src < NCOLS) {
		n = tab_n[src] - sjs;
		if (n <= 0) return 0;
	} else {
		n = 1;
		sjs = 0;
	}
	card0 = get_src_card(src, sjs);

	if (dst < NCOLS) {					/* to a tableau column*/
		if (src < NCOLS && src == dst) return 0;
		if (!can_place_tab(card0, n, dst)) return 0;
		push_move(src, dst, n);
		for (i = 0; i < n; i++) {
			if (tab_n[dst] >= MAXH) break;
			tab[dst][tab_n[dst]++] = get_src_card(src, sjs + i);
		}
		remove_from_src(src, sjs, n);
	} else if (dst < NCOLS + NCELLS) {			/* to a free cell*/
		int fi = dst - NCOLS;
		if (n != 1 || freecell[fi] >= 0) return 0;
		push_move(src, dst, 1);
		freecell[fi] = card0;
		remove_from_src(src, sjs, 1);
	} else {						/* to a foundation*/
		int fs = dst - NCOLS - NCELLS;
		if (n != 1) return 0;
		if (SUIT(card0) != fs) return 0;
		if (RANK(card0) != found[fs] + 1) return 0;
		push_move(src, dst, 1);
		found[fs]++;
		remove_from_src(src, sjs, 1);
	}

	sel_active = 0;
	check_win();
	return 1;
}

static void
undo(void)
{
	Move	m;
	int	cards[MAXH];
	int	i, n;

	if (hist_n == 0)
		return;
	m = hist[--hist_n];
	n = m.n;
	if (n > MAXH) n = MAXH;

	if (m.dst < NCOLS) {
		for (i = n - 1; i >= 0; i--)
			cards[i] = (tab_n[m.dst] > 0) ? tab[m.dst][--tab_n[m.dst]] : -1;
	} else if (m.dst < NCOLS + NCELLS) {
		cards[0] = freecell[m.dst - NCOLS];
		freecell[m.dst - NCOLS] = -1;
	} else {
		int s = m.dst - NCOLS - NCELLS;
		found[s]--;
		cards[0] = CARD(s, found[s] + 1);
	}

	if (m.src < NCOLS) {
		for (i = 0; i < n; i++) {
			if (tab_n[m.src] < MAXH && cards[i] >= 0)
				tab[m.src][tab_n[m.src]++] = cards[i];
		}
	} else if (m.src < NCOLS + NCELLS) {
		freecell[m.src - NCOLS] = cards[0];
	} else {
		found[m.src - NCOLS - NCELLS]++;
	}

	if (won) { won = 0; }
	sel_active = 0;
	need_draw = 1;
}

/* a card may be auto-collected when every lower card of the opposite colour is
   already home - the standard safe rule*/
static int
safe_to_home(int card)
{
	int r = RANK(card);
	int col = card_color(card);
	int minopp = NRANK, s;

	for (s = 0; s < NFOUND; s++)
		if (card_color(CARD(s, 1)) != col && found[s] < minopp)
			minopp = found[s];
	return r <= minopp + 1;
}

static void
auto_collect(void)
{
	int moved = 1, any = 0;

	while (moved) {
		int best = -1, best_src = -1, best_js = 0, best_rank = NRANK + 1;
		int i, c;

		moved = 0;
		for (i = 0; i < NCELLS; i++) {
			int cd = freecell[i];
			if (cd < 0) continue;
			if (RANK(cd) != found[SUIT(cd)] + 1) continue;
			if (!safe_to_home(cd)) continue;
			if (RANK(cd) < best_rank) {
				best_rank = RANK(cd); best = cd;
				best_src = LOC_FREE(i); best_js = 0;
			}
		}
		for (c = 0; c < NCOLS; c++) {
			int cd;
			if (tab_n[c] == 0) continue;
			cd = tab[c][tab_n[c]-1];
			if (RANK(cd) != found[SUIT(cd)] + 1) continue;
			if (!safe_to_home(cd)) continue;
			if (RANK(cd) < best_rank) {
				best_rank = RANK(cd); best = cd;
				best_src = LOC_TAB(c); best_js = tab_n[c]-1;
			}
		}
		if (best >= 0 && do_move(best_src, best_js, LOC_FOUND(SUIT(best)))) {
			moved = 1;
			any = 1;
		}
	}
	if (any)
		need_draw = 1;
}

/*----------------------------------------------------------------------------*/
/* dealing                                                                      */
/*----------------------------------------------------------------------------*/

static void
new_game(void)
{
	int deck[NCARD], i, c;

	for (i = 0; i < NCARD; i++)
		deck[i] = i;
	for (i = NCARD - 1; i > 0; i--) {
		int j = rand() % (i + 1), t = deck[i];
		deck[i] = deck[j];
		deck[j] = t;
	}
	for (c = 0; c < NCOLS; c++)
		tab_n[c] = 0;
	for (i = 0; i < NCELLS; i++)
		freecell[i] = -1;
	for (i = 0; i < NFOUND; i++)
		found[i] = 0;
	for (i = 0; i < NCARD; i++) {
		c = i % NCOLS;			/* round robin: cols 0-3 get 7, 4-7 get 6*/
		if (tab_n[c] < MAXH)
			tab[c][tab_n[c]++] = deck[i];
	}
	hist_n = 0;
	sel_active = 0;
	won = 0;
	need_draw = 1;
}

/*----------------------------------------------------------------------------*/
/* input                                                                        */
/*----------------------------------------------------------------------------*/

static int
in_col(int x, int *c)
{
	int i;
	for (i = 0; i < NCOLS; i++)
		if (x >= col_x[i] && x < col_x[i] + card_w) { *c = i; return 1; }
	return 0;
}

/* map a click to a location; returns 1 and fills loc and js, 0 if it hit nothing*/
static int
hit_test(int x, int y, int *loc, int *js)
{
	int c, j;

	if (y >= top_y && y < top_y + card_h) {
		if (x >= col_x[0] && x < col_x[NCELLS-1] + card_w) {
			if (!in_col(x, &c)) return 0;
			*loc = LOC_FREE(c); *js = 0; return 1;
		}
		if (x >= col_x[NCOLS/2] && x < col_x[NCOLS-1] + card_w) {
			if (!in_col(x, &c)) return 0;
			*loc = LOC_FOUND(c - NCOLS/2); *js = 0; return 1;
		}
		return 0;
	}
	if (y >= tableau_y && in_col(x, &c)) {
		if (tab_n[c] == 0) { *loc = LOC_TAB(c); *js = 0; return 1; }
		j = (y - tableau_y) / fan;
		if (j > tab_n[c] - 1) {
			if (y <= tableau_y + (tab_n[c]-1)*fan + card_h)
				j = tab_n[c] - 1;
			else
				return 0;
		}
		*loc = LOC_TAB(c); *js = j; return 1;
	}
	return 0;
}

static void
try_select(int loc, int js)
{
	if (loc < NCOLS) {				/* tableau*/
		int c = loc;
		if (tab_n[c] == 0) return;
		if (js > tab_n[c] - 1) js = tab_n[c] - 1;
		if (!valid_run(c, js)) return;		/* cannot lift a broken run*/
		sel_active = 1; sel_loc = loc; sel_js = js;
		sel_n = tab_n[c] - js;
	} else if (loc < NCOLS + NCELLS) {		/* free cell*/
		if (freecell[loc - NCOLS] < 0) return;
		sel_active = 1; sel_loc = loc; sel_js = 0; sel_n = 1;
	} else {					/* foundation*/
		int s = loc - NCOLS - NCELLS;
		if (found[s] <= 0) return;
		sel_active = 1; sel_loc = loc; sel_js = 0; sel_n = 1;
	}
}

static void
on_click(int x, int y)
{
	int loc, js;

	if (won) { sel_active = 0; need_draw = 1; return; }
	if (!hit_test(x, y, &loc, &js)) {
		sel_active = 0;
		need_draw = 1;
		return;
	}
	if (!sel_active) {
		try_select(loc, js);
	} else if (loc == sel_loc && (loc >= NCOLS || js == sel_js)) {
		sel_active = 0;				/* clicked the same: deselect*/
	} else if (!do_move(sel_loc, sel_js, loc)) {
		sel_active = 0;				/* illegal move: reselect target*/
		try_select(loc, js);
	}
	need_draw = 1;
}

static void
handle_key(XKeyEvent *ke)
{
	KeySym	ks;
	char	buf[8];

	XLookupString(ke, buf, (int)sizeof(buf), &ks, NULL);

	if (ks == XK_q || ks == XK_Escape) { running = 0; return; }
	if (ks == XK_n || ks == XK_r) { new_game(); return; }
	if (ks == XK_u) { undo(); return; }
	if (ks == XK_a) { auto_collect(); return; }
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
		if (ev->xbutton.button == Button1)
			on_click(ev->xbutton.x, ev->xbutton.y);
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

/*----------------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
	XEvent	ev;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xfreecell: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg    = alloc_rgb(0x1f, 0x6b, 0x35);	/* felt green*/
	c_card  = alloc_rgb(0xff, 0xff, 0xff);
	c_line  = alloc_rgb(0x30, 0x30, 0x38);
	c_sel   = alloc_rgb(0xff, 0xd8, 0x30);
	c_red   = alloc_rgb(0xc8, 0x14, 0x14);
	c_black = alloc_rgb(0x10, 0x10, 0x14);
	c_slot  = alloc_rgb(0x3a, 0x9a, 0x55);
	c_faint = alloc_rgb(0x2c, 0x82, 0x45);
	c_text  = alloc_rgb(0xe8, 0xf0, 0xe8);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xfreecell: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - DEF_W)/2,
			(DisplayHeight(dpy, scr) - DEF_H)/2,
			DEF_W, DEF_H, 0, c_line, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	srand((unsigned int)time(NULL));	/* shuffle varies per launch*/
	new_game();

	while (running) {
		XNextEvent(dpy, &ev);
		handle_event(&ev);
		if (need_draw)
			redraw();
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
