/*
 * xedit -- a pure-Xlib clone of the classic X11R6 "xedit" for NX11 on EwokOS.
 *
 * A small fixed-grid text editor: a scrollable monospace text area with a block
 * cursor, Load/Save/Quit buttons, a message line, arrow/Home/End/Page
 * navigation, mouse cursor placement and Ctrl-S / Ctrl-L shortcuts.  This is
 * ordinary X11 code -- libNX11 turns every call into a Nano-X server call and
 * the top-level window becomes a real EwokOS xwin window (NANOWM=0), so the
 * desktop window manager owns the frame and the move/resize/close gestures.
 *
 * The real xedit is built on the Xaw Text widget and the Xt toolkit, which NX11
 * does not provide; this clone reproduces the look and behaviour with plain
 * Xlib drawing and hit-testing, exactly like the other demos.
 *
 * The loop is a non-blocking poll (XCheckWindowEvent() -> GsSelect(POLL) also
 * flushes and presents the previous frame); proc_usleep() paces it.
 *
 * Usage:  xedit [filename]   -- with a filename, Load/Save use it; without one
 *         the buffer is untitled and Save reports that no filename was given.
 *         Esc / Ctrl-Q quits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Editor (NX11)"
#define FONT_NAME	"fixed"
#define POLL_US		50000		/* 50 ms */
#define TB		30		/* toolbar height */
#define SB		20		/* status height */
#define MAXLINES	512
#define MAXCOLS		200

enum { BTN_LOAD, BTN_SAVE, BTN_QUIT, NBTN };

static const char *	btn_label[NBTN] = { "Load", "Save", "Quit" };

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_face, c_hi, c_lo, c_textbg, c_textfg;
static unsigned long	c_cursor, c_cursortxt, c_border, c_msg;

static int		win_w = 520, win_h = 400;
static int		tx, ty, tw, th;		/* text area */
static int		char_w, char_h, ascent;
static int		vis_cols, vis_rows;
static int		btn_x[NBTN], btn_y, btn_w, btn_h;

/* the document: a simple fixed grid of lines */
static char		lines[MAXLINES][MAXCOLS];
static int		nlines = 1;
static int		crow = 0, ccol = 0;		/* cursor */
static int		top_row = 0, left_col = 0;	/* scroll */

static char		filename[256] = "";
static char		msg[160] = "";
static int		pressed = -1, press_idx = -1;
static int		running = 1, need_draw = 1;

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
set_msg(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	need_draw = 1;
}

/*----------------------------------------------------------------------------*/
/* editing primitives                                                           */
/*----------------------------------------------------------------------------*/

static int
line_len(int r)
{
	return (int)strlen(lines[r]);
}

static void
ensure_visible(void)
{
	if (crow < top_row)
		top_row = crow;
	if (vis_rows > 0 && crow >= top_row + vis_rows)
		top_row = crow - vis_rows + 1;
	if (top_row < 0)
		top_row = 0;
	if (top_row > nlines - 1)
		top_row = nlines - 1;

	if (ccol < left_col)
		left_col = ccol;
	if (vis_cols > 0 && ccol >= left_col + vis_cols)
		left_col = ccol - vis_cols + 1;
	if (left_col < 0)
		left_col = 0;
}

static void
ed_insert(char ch)
{
	int	l = line_len(crow);

	if (l >= MAXCOLS - 1)
		return;
	if (ccol > l)
		ccol = l;
	memmove(&lines[crow][ccol + 1], &lines[crow][ccol], (size_t)(l - ccol + 1));
	lines[crow][ccol] = ch;
	ccol++;
	ensure_visible();
	need_draw = 1;
}

static void
ed_newline(void)
{
	int	i, tail;

	if (nlines >= MAXLINES)
		return;
	tail = line_len(crow) - ccol;		/* chars after the cursor */
	if (tail < 0)
		tail = 0;
	for (i = nlines; i > crow + 1; i--)
		memcpy(lines[i], lines[i - 1], MAXCOLS);
	/* memmove, not strcpy: the rows of lines[][] are contiguous, so GCC
	 * cannot rule out overlap and warns (-Wrestrict) on strcpy/strcat. */
	memmove(lines[crow + 1], &lines[crow][ccol], (size_t)tail + 1);
	lines[crow][ccol] = '\0';
	nlines++;
	crow++;
	ccol = 0;
	ensure_visible();
	need_draw = 1;
}

static void
ed_backspace(void)
{
	int	i, l = line_len(crow);

	if (ccol > 0) {
		if (ccol > l) ccol = l;
		memmove(&lines[crow][ccol - 1], &lines[crow][ccol], (size_t)(l - ccol + 1));
		ccol--;
	} else if (crow > 0) {
		int	pl = line_len(crow - 1);
		if (pl + l < MAXCOLS) {
			memmove(lines[crow - 1] + pl, lines[crow], (size_t)l + 1);
			for (i = crow; i < nlines - 1; i++)
				memcpy(lines[i], lines[i + 1], MAXCOLS);
			nlines--;
			crow--;
			ccol = pl;
		}
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_delete(void)
{
	int	i, l = line_len(crow);

	if (ccol < l) {
		memmove(&lines[crow][ccol], &lines[crow][ccol + 1], (size_t)(l - ccol));
	} else if (crow < nlines - 1) {
		int	cl = line_len(crow + 1);
		if (l + cl < MAXCOLS) {
			memmove(lines[crow] + l, lines[crow + 1], (size_t)cl + 1);
			for (i = crow + 1; i < nlines - 1; i++)
				memcpy(lines[i], lines[i + 1], MAXCOLS);
			nlines--;
		}
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_left(void)
{
	if (ccol > 0)
		ccol--;
	else if (crow > 0) {
		crow--;
		ccol = line_len(crow);
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_right(void)
{
	if (ccol < line_len(crow))
		ccol++;
	else if (crow < nlines - 1) {
		crow++;
		ccol = 0;
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_up(void)
{
	if (crow > 0) {
		crow--;
		if (ccol > line_len(crow))
			ccol = line_len(crow);
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_down(void)
{
	if (crow < nlines - 1) {
		crow++;
		if (ccol > line_len(crow))
			ccol = line_len(crow);
	}
	ensure_visible();
	need_draw = 1;
}

static void
ed_page(int dir)
{
	int	pg = vis_rows > 1 ? vis_rows - 1 : 1;

	crow += dir * pg;
	if (crow < 0) crow = 0;
	if (crow > nlines - 1) crow = nlines - 1;
	if (ccol > line_len(crow)) ccol = line_len(crow);
	ensure_visible();
	need_draw = 1;
}

/*----------------------------------------------------------------------------*/
/* file I/O                                                                     */
/*----------------------------------------------------------------------------*/

static void
clear_doc(void)
{
	nlines = 1;
	lines[0][0] = '\0';
	crow = ccol = top_row = left_col = 0;
}

static void
do_load(void)
{
	FILE *	f;
	int	r = 0;

	if (filename[0] == '\0') {
		set_msg("No filename - start xedit with an argument");
		return;
	}
	f = fopen(filename, "rb");
	if (f == NULL) {
		set_msg("Cannot open %s", filename);
		return;
	}
	clear_doc();
	while (r < MAXLINES - 1 && fgets(lines[r], MAXCOLS, f) != NULL) {
		int	l = (int)strlen(lines[r]);
		while (l > 0 && (lines[r][l - 1] == '\n' || lines[r][l - 1] == '\r'))
			lines[r][--l] = '\0';
		r++;
	}
	fclose(f);
	nlines = (r > 0) ? r : 1;
	set_msg("Loaded %d line(s) from %s", nlines, filename);
	need_draw = 1;
}

static void
do_save(void)
{
	FILE *	f;
	int	i;

	if (filename[0] == '\0') {
		set_msg("No filename - cannot save (start xedit with an argument)");
		return;
	}
	f = fopen(filename, "wb");
	if (f == NULL) {
		set_msg("Cannot write %s", filename);
		return;
	}
	for (i = 0; i < nlines; i++)
		fprintf(f, "%s\n", lines[i]);
	fclose(f);
	set_msg("Saved %d line(s) to %s", nlines, filename);
	need_draw = 1;
}

static void
sample_doc(void)
{
	static const char *	const s[] = {
		"Welcome to xedit (NX11).",
		"",
		"A small pure-Xlib text editor.  Just type to insert text.",
		"Arrow keys, Home/End and Page Up/Down move the cursor;",
		"BackSpace deletes, Enter starts a new line, and clicking",
		"with Button1 places the cursor.",
		"",
		"  Ctrl-S  save        Ctrl-L  load        Esc / Ctrl-Q  quit",
		"  Or use the Load / Save / Quit buttons above.",
		"",
		"Pass a filename as an argument to edit a real file:",
		"    xedit myfile.txt",
		""
	};
	int	i;

	nlines = (int)(sizeof(s) / sizeof(s[0]));
	if (nlines > MAXLINES) nlines = MAXLINES;
	for (i = 0; i < nlines; i++) {
		strncpy(lines[i], s[i], MAXCOLS - 1);
		lines[i][MAXCOLS - 1] = '\0';
	}
	crow = ccol = top_row = left_col = 0;
}

/*----------------------------------------------------------------------------*/
/* layout + drawing                                                             */
/*----------------------------------------------------------------------------*/

static void
layout(void)
{
	int	i;

	char_w = XTextWidth(xfont, "M", 1);
	if (char_w < 1) char_w = 6;
	ascent = xfont->ascent;
	char_h = xfont->ascent + xfont->descent;
	if (char_h < 1) char_h = 13;

	btn_w = 56;
	btn_h = 20;
	btn_y = (TB - btn_h) / 2;
	for (i = 0; i < NBTN; i++)
		btn_x[i] = 8 + i * (btn_w + 6);

	tx = 6;
	ty = TB + 4;
	tw = win_w - 12;
	th = win_h - TB - SB - 8;
	if (tw < 16) tw = 16;
	if (th < 16) th = 16;

	vis_cols = tw / char_w;
	vis_rows = th / char_h;
	if (vis_cols < 1) vis_cols = 1;
	if (vis_rows < 1) vis_rows = 1;
	ensure_visible();
}

static void
bevel(int x, int y, int w, int h, int raised, unsigned long face)
{
	XSetForeground(dpy, gc, face);
	XFillRectangle(dpy, win, gc, x, y, w, h);
	XSetForeground(dpy, gc, raised ? c_hi : c_lo);
	XDrawLine(dpy, win, gc, x, y, x + w - 1, y);
	XDrawLine(dpy, win, gc, x, y, x, y + h - 1);
	XSetForeground(dpy, gc, raised ? c_lo : c_hi);
	XDrawLine(dpy, win, gc, x + w - 1, y, x + w - 1, y + h - 1);
	XDrawLine(dpy, win, gc, x, y + h - 1, x + w - 1, y + h - 1);
}

static int
hit_button(int x, int y)
{
	int	i;

	for (i = 0; i < NBTN; i++)
		if (x >= btn_x[i] && x < btn_x[i] + btn_w &&
		    y >= btn_y && y < btn_y + btn_h)
			return i;
	return -1;
}

static void
draw_toolbar(void)
{
	int	i, base = btn_y + (btn_h - char_h) / 2 + ascent;
	int	hx;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, TB);

	for (i = 0; i < NBTN; i++) {
		int	len = (int)strlen(btn_label[i]);
		int	tw2 = XTextWidth(xfont, btn_label[i], len);

		bevel(btn_x[i], btn_y, btn_w, btn_h, pressed != i, c_face);
		XSetForeground(dpy, gc, c_textfg);
		XDrawString(dpy, win, gc, btn_x[i] + (btn_w - tw2) / 2, base,
			    btn_label[i], len);
	}

	hx = btn_x[NBTN - 1] + btn_w + 14;
	if (hx + 200 < win_w) {
		const char *hint = "Ctrl-S save  Ctrl-L load  Esc quit";
		XSetForeground(dpy, gc, c_border);
		XDrawString(dpy, win, gc, hx, base, hint, (int)strlen(hint));
	}

	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, TB - 1, win_w, TB - 1);
}

static void
draw_text(void)
{
	int	r, ybase;

	XSetForeground(dpy, gc, c_textbg);
	XFillRectangle(dpy, win, gc, tx, ty, tw, th);

	XSetForeground(dpy, gc, c_textfg);
	for (r = top_row; r < nlines && r < top_row + vis_rows; r++) {
		int	l = line_len(r);
		int	cnt;

		if (left_col >= l)
			continue;
		cnt = l - left_col;
		if (cnt > vis_cols)
			cnt = vis_cols;
		ybase = ty + (r - top_row) * char_h + ascent;
		XDrawString(dpy, win, gc, tx, ybase, &lines[r][left_col], cnt);
	}

	/* block cursor */
	if (crow >= top_row && crow < top_row + vis_rows && ccol >= left_col &&
	    ccol - left_col < vis_cols) {
		int	cxp = tx + (ccol - left_col) * char_w;
		int	cyp = ty + (crow - top_row) * char_h;

		XSetForeground(dpy, gc, c_cursor);
		XFillRectangle(dpy, win, gc, cxp, cyp, char_w, char_h);
		if (ccol < line_len(crow)) {
			XSetForeground(dpy, gc, c_cursortxt);
			XDrawString(dpy, win, gc, cxp, cyp + ascent,
				    &lines[crow][ccol], 1);
		}
	}

	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, tx, ty, (unsigned)tw - 1, (unsigned)th - 1);
}

static void
draw_status(void)
{
	int	base = win_h - SB / 2 + (ascent - xfont->descent) / 2;
	int	mlen, mx;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, win_h - SB, win_w, SB);
	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, win_h - SB, win_w, win_h - SB);

	XSetForeground(dpy, gc, c_textfg);
	{
		const char *	nm = filename[0] ? filename : "(untitled)";
		XDrawString(dpy, win, gc, 8, base, nm, (int)strlen(nm));
	}

	mlen = (int)strlen(msg);
	mx = win_w - 8 - XTextWidth(xfont, msg, mlen);
	if (mx < 160) mx = 160;
	XSetForeground(dpy, gc, c_msg);
	XDrawString(dpy, win, gc, mx, base, msg, mlen);
}

static void
redraw(void)
{
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);
	draw_toolbar();
	draw_text();
	draw_status();
}

/*----------------------------------------------------------------------------*/
/* events                                                                       */
/*----------------------------------------------------------------------------*/

static void
activate_button(int b)
{
	switch (b) {
	case BTN_LOAD:	do_load();	break;
	case BTN_SAVE:	do_save();	break;
	case BTN_QUIT:	running = 0;	break;
	default:	break;
	}
}

static void
place_cursor(int wx, int wy)
{
	int	r, c;

	if (wx < tx || wx >= tx + tw || wy < ty || wy >= ty + th)
		return;
	r = top_row + (wy - ty) / char_h;
	c = left_col + (wx - tx) / char_w;
	if (r < 0) r = 0;
	if (r > nlines - 1) r = nlines - 1;
	if (c < 0) c = 0;
	if (c > line_len(r)) c = line_len(r);
	crow = r;
	ccol = c;
	need_draw = 1;
}

static void
handle_key(XKeyEvent *ke)
{
	KeySym	ks;
	char	buf[16];
	int	n, i;

	n = XLookupString(ke, buf, (int)sizeof(buf) - 1, &ks, NULL);
	if (n < 0) n = 0;
	buf[n] = '\0';

	if (ke->state & ControlMask) {
		if (ks == XK_s || ks == XK_S) do_save();
		else if (ks == XK_l || ks == XK_L) do_load();
		else if (ks == XK_q || ks == XK_Q) running = 0;
		return;
	}

	switch (ks) {
	case XK_Escape:		running = 0;	return;
	case XK_Return:
	case XK_KP_Enter:	ed_newline();	return;
	case XK_BackSpace:	ed_backspace();	return;
	case XK_Delete:		ed_delete();	return;
	case XK_Left:		ed_left();	return;
	case XK_Right:		ed_right();	return;
	case XK_Up:		ed_up();	return;
	case XK_Down:		ed_down();	return;
	case XK_Home:		ccol = 0; ensure_visible(); need_draw = 1; return;
	case XK_End:		ccol = line_len(crow); ensure_visible(); need_draw = 1; return;
	case XK_Page_Up:	ed_page(-1);	return;
	case XK_Page_Down:	ed_page(1);	return;
	default:		break;
	}

	for (i = 0; i < n; i++) {
		unsigned char	ch = (unsigned char)buf[i];
		if (ch >= 0x20 && ch < 0x7f)
			ed_insert((char)ch);
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
		if (ev->xconfigure.width != win_w ||
		    ev->xconfigure.height != win_h) {
			win_w = ev->xconfigure.width;
			win_h = ev->xconfigure.height;
			layout();
			need_draw = 1;
		}
		break;

	case ButtonPress:
		press_idx = hit_button(ev->xbutton.x, ev->xbutton.y);
		if (press_idx >= 0) {
			pressed = press_idx;
			need_draw = 1;
		} else if (ev->xbutton.button == Button1) {
			place_cursor(ev->xbutton.x, ev->xbutton.y);
		}
		break;

	case ButtonRelease:
		if (press_idx >= 0 &&
		    hit_button(ev->xbutton.x, ev->xbutton.y) == press_idx)
			activate_button(press_idx);
		press_idx = -1;
		pressed = -1;
		need_draw = 1;
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

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xedit: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg         = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_face       = alloc_rgb(0xe8, 0xe8, 0xee);
	c_hi         = alloc_rgb(0xff, 0xff, 0xff);
	c_lo         = alloc_rgb(0x80, 0x80, 0x88);
	c_textbg     = alloc_rgb(0xff, 0xff, 0xff);
	c_textfg     = alloc_rgb(0x10, 0x10, 0x18);
	c_cursor     = alloc_rgb(0x14, 0x3c, 0xc8);
	c_cursortxt  = alloc_rgb(0xff, 0xff, 0xff);
	c_border     = alloc_rgb(0x40, 0x40, 0x48);
	c_msg        = alloc_rgb(0x1c, 0x5c, 0x28);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xedit: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	if (argc > 1) {
		strncpy(filename, argv[1], sizeof(filename) - 1);
		filename[sizeof(filename) - 1] = '\0';
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - win_w) / 2,
			(DisplayHeight(dpy, scr) - win_h) / 2,
			(unsigned)win_w, (unsigned)win_h, 0, c_border, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	if (filename[0])
		do_load();
	else
		sample_doc();

	layout();

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | KeyPressMask |
		ButtonPressMask | ButtonReleaseMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (running) {
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask |
				ButtonPressMask | ButtonReleaseMask, &ev))
			handle_event(&ev);

		if (need_draw) {
			redraw();
			need_draw = 0;
		}
		proc_usleep(POLL_US);
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
