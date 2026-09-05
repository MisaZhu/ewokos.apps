/*
 * xcalc -- a pure-Xlib clone of the classic X11R6 "xcalc" for NX11 on EwokOS.
 *
 * A working desk calculator: an LCD read-out plus a 4x5 grid of bevelled
 * buttons (AC C % / | 7 8 9 * | 4 5 6 - | 1 2 3 + | +/- 0 . =).  This is
 * ordinary X11 code -- libNX11 turns every call into a Nano-X server call and
 * the top-level window becomes a real EwokOS xwin window (NANOWM=0), so the
 * desktop window manager owns the frame and the move/resize/close gestures.
 *
 * The real xcalc is built on the Xaw/Xt widget toolkit, which NX11 does not
 * provide; this clone reproduces the look and behaviour with plain Xlib drawing
 * and hit-testing, exactly like the xmine/xfreecell demos.
 *
 * The loop is a non-blocking poll (XCheckWindowEvent() -> GsSelect(POLL) also
 * flushes and presents the previous frame); proc_usleep() paces it.  Division by
 * zero, sqrt of a negative and 1/0 show "Error"; AC clears it.
 *
 * Controls:  click the buttons, or use the keyboard -- 0-9 . + - * / = Return,
 *            Escape = AC, BackSpace = C, % s (sqrt) n (+/-), Q quits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Calculator (NX11)"
#define FONT_NAME	"fixed"
#define POLL_US		50000		/* 50 ms */
#define DISP_H		54		/* read-out strip height */
#define COLS		4
#define ROWS		5
#define NBTN		(COLS * ROWS)
#define MAXENTRY	14

/* button actions */
#define ACT_DIGIT	0		/* arg = digit */
#define ACT_DOT		1
#define ACT_OP		2		/* arg = 1 +, 2 -, 3 *, 4 / */
#define ACT_EQ		3
#define ACT_AC		4
#define ACT_C		5
#define ACT_NEG		6
#define ACT_PCT		7

typedef struct {
	const char *	label;
	int		act;
	int		arg;
} Btn;

static const Btn buttons[NBTN] = {
	{ "AC",  ACT_AC,  0 }, { "C",   ACT_C,   0 }, { "%",   ACT_PCT, 0 }, { "/", ACT_OP, 4 },
	{ "7",   ACT_DIGIT,7 }, { "8",   ACT_DIGIT,8 }, { "9",   ACT_DIGIT,9 }, { "*", ACT_OP, 3 },
	{ "4",   ACT_DIGIT,4 }, { "5",   ACT_DIGIT,5 }, { "6",   ACT_DIGIT,6 }, { "-", ACT_OP, 2 },
	{ "1",   ACT_DIGIT,1 }, { "2",   ACT_DIGIT,2 }, { "3",   ACT_DIGIT,3 }, { "+", ACT_OP, 1 },
	{ "+/-", ACT_NEG, 0 }, { "0",   ACT_DIGIT,0 }, { ".",   ACT_DOT, 0 }, { "=", ACT_EQ, 0 },
};

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_face, c_hi, c_lo;
static unsigned long	c_lcd, c_lcdtext, c_num, c_op, c_fn;

static int		win_w = 240, win_h = 320;
static int		running = 1, need_draw = 1;
static int		pressed = -1, press_idx = -1;

/* calculator state */
static char		entry[MAXENTRY + 2] = "0";
static int		entering = 0;
static double		acc = 0.0, result = 0.0;
static int		op = 0;			/* pending operator */
static int		error = 0;
static char		disp[32] = "0";

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
/* calculator logic                                                             */
/*----------------------------------------------------------------------------*/

static void
fmt_val(double v, char *buf, int n)
{
	double	av = v < 0 ? -v : v;
	double	ip;

	if (error) {
		snprintf(buf, (size_t)n, "Error");
		return;
	}
	if (modf(av, &ip) == 0.0 && av < 1e15)
		snprintf(buf, (size_t)n, "%ld", (long)v);
	else
		snprintf(buf, (size_t)n, "%.10g", v);
}

static void
update_display(void)
{
	if (error)
		snprintf(disp, sizeof(disp), "Error");
	else if (entering)
		snprintf(disp, sizeof(disp), "%s", entry[0] ? entry : "0");
	else
		fmt_val(result, disp, (int)sizeof(disp));
	need_draw = 1;
}

static double
cur_value(void)
{
	return entering ? atof(entry) : result;
}

static double
apply(double a, double b, int o, int *err)
{
	switch (o) {
	case 1: return a + b;
	case 2: return a - b;
	case 3: return a * b;
	case 4:
		if (b == 0.0) { *err = 1; return 0.0; }
		return a / b;
	}
	return b;
}

static void
do_digit(int d)
{
	int	len;

	if (error) return;
	if (!entering) {
		snprintf(entry, sizeof(entry), "0");
		entering = 1;
	}
	len = (int)strlen(entry);
	if (entry[0] == '0' && entry[1] == '\0') {
		entry[0] = (char)('0' + d);
		entry[1] = '\0';
	} else if (len < MAXENTRY) {
		entry[len] = (char)('0' + d);
		entry[len + 1] = '\0';
	}
	update_display();
}

static void
do_dot(void)
{
	int	len;

	if (error) return;
	if (!entering) {
		snprintf(entry, sizeof(entry), "0");
		entering = 1;
	}
	if (strchr(entry, '.') == NULL && (len = (int)strlen(entry)) < MAXENTRY) {
		entry[len] = '.';
		entry[len + 1] = '\0';
	}
	update_display();
}

static void
set_op(int o)
{
	int	e = 0;

	if (error) return;
	if (op != 0 && entering) {
		result = apply(acc, atof(entry), op, &e);
		if (e) { error = 1; update_display(); return; }
		acc = result;
	} else {
		acc = entering ? atof(entry) : result;
	}
	result = acc;
	op = o;
	entering = 0;
	update_display();
}

static void
do_equals(void)
{
	int	e = 0;

	if (error) return;
	if (op != 0) {
		double	b = entering ? atof(entry) : result;
		result = apply(acc, b, op, &e);
		if (e) error = 1;
		op = 0;
	} else {
		result = entering ? atof(entry) : result;
	}
	entering = 0;
	update_display();
}

static void
do_ac(void)
{
	snprintf(entry, sizeof(entry), "0");
	entering = 0;
	acc = result = 0.0;
	op = 0;
	error = 0;
	update_display();
}

static void
do_c(void)
{
	error = 0;
	snprintf(entry, sizeof(entry), "0");
	entering = 1;
	update_display();
}

static void
do_neg(void)
{
	int	len;

	if (error) return;
	if (entering) {
		len = (int)strlen(entry);
		if (entry[0] == '-') {
			memmove(entry, entry + 1, (size_t)len);	/* drop sign */
		} else if (len < MAXENTRY) {
			memmove(entry + 1, entry, (size_t)len + 1);
			entry[0] = '-';
		}
	} else {
		result = -result;
	}
	update_display();
}

static void
do_pct(void)
{
	if (error) return;
	result = cur_value() / 100.0;
	entering = 0;
	update_display();
}

static void
activate(const Btn *b)
{
	switch (b->act) {
	case ACT_DIGIT:	do_digit(b->arg);	break;
	case ACT_DOT:	do_dot();		break;
	case ACT_OP:	set_op(b->arg);		break;
	case ACT_EQ:	do_equals();		break;
	case ACT_AC:	do_ac();		break;
	case ACT_C:	do_c();			break;
	case ACT_NEG:	do_neg();		break;
	case ACT_PCT:	do_pct();		break;
	default:	break;
	}
}

/*----------------------------------------------------------------------------*/
/* drawing                                                                      */
/*----------------------------------------------------------------------------*/

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

static unsigned long
label_color(const Btn *b)
{
	if (b->act == ACT_OP || b->act == ACT_EQ)
		return c_op;
	if (b->act == ACT_AC || b->act == ACT_C || b->act == ACT_PCT || b->act == ACT_NEG)
		return c_fn;
	return c_num;
}

static void
btn_rect(int idx, int *x, int *y, int *w, int *h)
{
	int	r = idx / COLS, c = idx % COLS;
	int	bw = win_w / COLS;
	int	bh = (win_h - DISP_H) / ROWS;

	*x = c * bw + 3;
	*y = DISP_H + r * bh + 3;
	*w = bw - 6;
	*h = bh - 6;
	if (*w < 4) *w = 4;
	if (*h < 4) *h = 4;
}

static int
hit_test(int x, int y)
{
	int	bw = win_w / COLS;
	int	bh = (win_h - DISP_H) / ROWS;
	int	c, r;

	if (y < DISP_H || bw <= 0 || bh <= 0)
		return -1;
	c = x / bw;
	r = (y - DISP_H) / bh;
	if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
		return -1;
	return r * COLS + c;
}

static void
draw_display(void)
{
	int	x = 6, y = 6, w = win_w - 12, h = DISP_H - 12;
	int	len, tw, th, bx;

	bevel(x, y, w, h, 0, c_lcd);

	/* pending-operator / error indicator on the left */
	{
		const char *ind = error ? "E" : (op == 1 ? "+" : op == 2 ? "-" :
				 op == 3 ? "*" : op == 4 ? "/" : entering ? "" : "=");
		if (ind[0]) {
			XSetForeground(dpy, gc, c_lcdtext);
			XDrawString(dpy, win, gc, x + 8, y + h / 2 +
				    (xfont->ascent - xfont->descent) / 2, ind,
				    (int)strlen(ind));
		}
	}

	/* main read-out, right aligned */
	len = (int)strlen(disp);
	tw = XTextWidth(xfont, disp, len);
	th = xfont->ascent + xfont->descent;
	bx = x + w - 8 - tw;
	if (bx < x + 24) bx = x + 24;
	XSetForeground(dpy, gc, c_lcdtext);
	XDrawString(dpy, win, gc, bx, y + (h - th) / 2 + xfont->ascent, disp, len);
}

static void
redraw(void)
{
	int	i, x, y, w, h;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

	draw_display();

	for (i = 0; i < NBTN; i++) {
		const char *	lbl = buttons[i].label;
		int		len = (int)strlen(lbl);
		int		tw, th, raised = (pressed != i);

		btn_rect(i, &x, &y, &w, &h);
		bevel(x, y, w, h, raised, c_face);
		tw = XTextWidth(xfont, lbl, len);
		th = xfont->ascent + xfont->descent;
		XSetForeground(dpy, gc, label_color(&buttons[i]));
		XDrawString(dpy, win, gc, x + (w - tw) / 2,
			    y + (h - th) / 2 + xfont->ascent, lbl, len);
	}
}

/*----------------------------------------------------------------------------*/
/* events                                                                       */
/*----------------------------------------------------------------------------*/

static void
handle_key(XKeyEvent *ke)
{
	KeySym	ks;
	char	buf[8];
	int	n;

	n = XLookupString(ke, buf, (int)sizeof(buf), &ks, NULL);
	(void)n;

	if (ks == XK_q || ks == XK_Q) { running = 0; return; }
	if (ks == XK_Escape) { do_ac(); return; }
	if (ks == XK_BackSpace) { do_c(); return; }
	if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_equal) { do_equals(); return; }
	if (ks == XK_period || ks == XK_comma || ks == XK_KP_Decimal) { do_dot(); return; }
	if (ks == XK_plus || ks == XK_KP_Add) { set_op(1); return; }
	if (ks == XK_minus || ks == XK_KP_Subtract) { set_op(2); return; }
	if (ks == XK_asterisk || ks == XK_x || ks == XK_X || ks == XK_KP_Multiply) { set_op(3); return; }
	if (ks == XK_slash || ks == XK_KP_Divide) { set_op(4); return; }
	if (ks == XK_percent) { do_pct(); return; }
	if (ks == XK_s) {						/* sqrt */
		double	v;
		if (error) return;
		v = cur_value();
		if (v < 0.0) error = 1;
		else { result = sqrt(v); entering = 0; }
		update_display();
		return;
	}
	if (ks == XK_n) { do_neg(); return; }
	if (ks >= XK_0 && ks <= XK_9) { do_digit((int)(ks - XK_0)); return; }
	if (ks >= XK_KP_0 && ks <= XK_KP_9) { do_digit((int)(ks - XK_KP_0)); return; }
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
			need_draw = 1;
		}
		break;

	case ButtonPress:
		press_idx = hit_test(ev->xbutton.x, ev->xbutton.y);
		pressed = press_idx;
		if (pressed >= 0)
			need_draw = 1;
		break;

	case ButtonRelease:
		if (press_idx >= 0 &&
		    hit_test(ev->xbutton.x, ev->xbutton.y) == press_idx)
			activate(&buttons[press_idx]);
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

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xcalc: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg      = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_face    = alloc_rgb(0xe8, 0xe8, 0xee);
	c_hi      = alloc_rgb(0xff, 0xff, 0xff);
	c_lo      = alloc_rgb(0x80, 0x80, 0x88);
	c_lcd     = alloc_rgb(0xb8, 0xc4, 0x98);
	c_lcdtext = alloc_rgb(0x14, 0x1a, 0x10);
	c_num     = alloc_rgb(0x10, 0x10, 0x18);
	c_op      = alloc_rgb(0x14, 0x3c, 0xc8);
	c_fn      = alloc_rgb(0xc8, 0x14, 0x14);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xcalc: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - win_w) / 2,
			(DisplayHeight(dpy, scr) - win_h) / 2,
			(unsigned)win_w, (unsigned)win_h, 0, c_lo, c_bg);
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

	update_display();

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
