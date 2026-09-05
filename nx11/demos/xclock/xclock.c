/*
 * xclock -- a pure-Xlib clone of the classic X11R6 "xclock" for NX11 on EwokOS.
 *
 * An analogue clock (hour/minute/second hands, tick ring, numerals) with a
 * digital read-out strip, and a large digital-only mode.  This is ordinary X11
 * code: libNX11 turns every call into a Nano-X server call and the top-level
 * window becomes a real EwokOS xwin window (NANOWM=0), so the desktop window
 * manager owns the frame and the move/resize/close gestures.
 *
 * Time comes from the portable C library: time()/localtime().  EwokOS honours
 * the TZ environment variable for the UTC offset (see libgloss/compat.c), so
 * setting TZ shifts the hands just like a real xclock; with TZ unset it shows
 * UTC.  The hands are filled polygons and the second hand a thin line, so no
 * XSetLineAttributes is needed.
 *
 * The loop is a non-blocking poll: XCheckWindowEvent() -> GsSelect(POLL) also
 * flushes and presents the previous frame; time() detects the second boundary
 * and marks a redraw.  proc_usleep() between polls yields the CPU.
 *
 * Controls:  d = digital-only, a = analogue, Q/Esc quits, the window manager
 *            close box quits, and the window is freely resizable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Clock (NX11)"
#define FONT_NAME	"fixed"		/* mapped to the builtin SystemFixed font */
#define FULL_CIRCLE	23040		/* 360 * 64 */
#define POLL_US		100000		/* 100 ms */
#define STRIP		24		/* digital read-out strip under the face */
#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

static const char *	wday_name[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *	mon_name[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_face, c_rim, c_tick, c_text;
static unsigned long	c_hour, c_min, c_sec, c_center;

static int		win_w = 220, win_h = 250;
static int		digital = 0;
static int		cx, cy, rad;
static int		running = 1, need_draw = 1;
static time_t		last_t = 0;

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
layout(void)
{
	int	avail_h = digital ? win_h : win_h - STRIP;
	int	r;

	cx = win_w / 2;
	cy = avail_h / 2;
	r = (win_w < avail_h ? win_w : avail_h) / 2 - 8;
	if (r < 10)
		r = 10;
	rad = r;
}

static void
text_center(int baseline, const char *s, unsigned long color)
{
	int	len = (int)strlen(s);
	int	tw = XTextWidth(xfont, s, len);

	XSetForeground(dpy, gc, color);
	XDrawString(dpy, win, gc, (win_w - tw) / 2, baseline, s, len);
}

/* A tapered hand as a convex polygon; angle is degrees clockwise from 12. */
static void
draw_hand(double angle_deg, int len, int hw, unsigned long color)
{
	double	a  = angle_deg * M_PI / 180.0;
	double	sx = sin(a), co = cos(a);
	double	tipx = cx + len * sx, tipy = cy - len * co;
	double	px = co, py = sx;			/* perpendicular to the hand */
	double	tail = len * 0.14;
	XPoint	pts[4];

	pts[0].x = (short)(cx - tail * sx + hw * px);
	pts[0].y = (short)(cy + tail * co + hw * py);
	pts[1].x = (short)(tipx + hw * 0.35 * px);
	pts[1].y = (short)(tipy + hw * 0.35 * py);
	pts[2].x = (short)(tipx - hw * 0.35 * px);
	pts[2].y = (short)(tipy - hw * 0.35 * py);
	pts[3].x = (short)(cx - tail * sx - hw * px);
	pts[3].y = (short)(cy + tail * co - hw * py);

	XSetForeground(dpy, gc, color);
	XFillPolygon(dpy, win, gc, pts, 4, Convex, CoordModeOrigin);
}

static void
draw_face(void)
{
	int	i;
	double	a;

	/* dial */
	XSetForeground(dpy, gc, c_face);
	XFillArc(dpy, win, gc, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, FULL_CIRCLE);
	XSetForeground(dpy, gc, c_rim);
	XDrawArc(dpy, win, gc, cx - rad, cy - rad, 2 * rad, 2 * rad, 0, FULL_CIRCLE);

	/* minute / hour tick ring */
	XSetForeground(dpy, gc, c_tick);
	for (i = 0; i < 60; i++) {
		int	hour = (i % 5 == 0);
		double	r1 = rad * (hour ? 0.80 : 0.90);
		double	r2 = rad * 0.96;

		a = (i * 6.0) * M_PI / 180.0;
		XDrawLine(dpy, win, gc,
			  cx + (int)(r1 * sin(a)), cy - (int)(r1 * cos(a)),
			  cx + (int)(r2 * sin(a)), cy - (int)(r2 * cos(a)));
	}

	/* hour numerals */
	for (i = 1; i <= 12; i++) {
		char	buf[4];
		int	len, tw, x, y;
		double	rr = rad * 0.66;

		snprintf(buf, sizeof(buf), "%d", i);
		len = (int)strlen(buf);
		tw = XTextWidth(xfont, buf, len);
		a = (i * 30.0) * M_PI / 180.0;
		x = cx + (int)(rr * sin(a)) - tw / 2;
		y = cy - (int)(rr * cos(a)) + (xfont->ascent - xfont->descent) / 2;
		XSetForeground(dpy, gc, c_text);
		XDrawString(dpy, win, gc, x, y, buf, len);
	}
}

static void
draw_hands(int h, int m, int s)
{
	double	hh = (double)(h % 12) + m / 60.0 + s / 3600.0;
	double	ha = hh * 30.0;				/* 360 / 12 */
	double	ma = ((double)m + s / 60.0) * 6.0;		/* 360 / 60 */
	double	sa = (double)s * 6.0;
	int	hw_h = (int)(rad * 0.055) + 1;
	int	hw_m = (int)(rad * 0.035) + 1;

	draw_hand(ha, (int)(rad * 0.50), hw_h, c_hour);
	draw_hand(ma, (int)(rad * 0.72), hw_m, c_min);

	/* thin second hand with a small counterweight */
	{
		double	aa = sa * M_PI / 180.0;
		int	tipx = cx + (int)(rad * 0.82 * sin(aa));
		int	tipy = cy - (int)(rad * 0.82 * cos(aa));
		int	tailx = cx - (int)(rad * 0.18 * sin(aa));
		int	taily = cy + (int)(rad * 0.18 * cos(aa));

		XSetForeground(dpy, gc, c_sec);
		XDrawLine(dpy, win, gc, tailx, taily, tipx, tipy);
	}

	/* centre cap */
	{
		int	cr = rad / 18;
		if (cr < 2) cr = 2;
		XSetForeground(dpy, gc, c_center);
		XFillArc(dpy, win, gc, cx - cr, cy - cr, 2 * cr, 2 * cr, 0, FULL_CIRCLE);
	}
}

static void
redraw(void)
{
	time_t		now = time(NULL);
	struct tm *	lt = localtime(&now);
	char		buf[48];
	int		th = xfont->ascent + xfont->descent;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

	if (digital) {
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
			 lt->tm_hour, lt->tm_min, lt->tm_sec);
		text_center(win_h / 2 + (xfont->ascent - xfont->descent) / 2,
			    buf, c_text);
		snprintf(buf, sizeof(buf), "%s %02d %s %d",
			 wday_name[lt->tm_wday % 7], lt->tm_mday,
			 mon_name[lt->tm_mon % 12], lt->tm_year + 1900);
		text_center(win_h / 2 + th + 8, buf, c_tick);
		return;
	}

	draw_face();
	draw_hands(lt->tm_hour, lt->tm_min, lt->tm_sec);

	/* digital read-out strip beneath the dial */
	XSetForeground(dpy, gc, c_tick);
	XDrawLine(dpy, win, gc, 0, win_h - STRIP, win_w, win_h - STRIP);
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
		 lt->tm_hour, lt->tm_min, lt->tm_sec);
	text_center(win_h - STRIP / 2 + (xfont->ascent - xfont->descent) / 2,
		    buf, c_text);
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

	case KeyPress: {
		KeySym	ks;
		char	buf[8];

		XLookupString(&ev->xkey, buf, (int)sizeof(buf), &ks, NULL);
		if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
			running = 0;
		} else if (ks == XK_d || ks == XK_D) {
			digital = 1;
			layout();
			need_draw = 1;
		} else if (ks == XK_a || ks == XK_A) {
			digital = 0;
			layout();
			need_draw = 1;
		}
		break;
	}

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
		fprintf(stderr, "xclock: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg     = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_face   = alloc_rgb(0xff, 0xff, 0xff);
	c_rim    = alloc_rgb(0x20, 0x20, 0x28);
	c_tick   = alloc_rgb(0x40, 0x40, 0x48);
	c_text   = alloc_rgb(0x10, 0x10, 0x18);
	c_hour   = alloc_rgb(0x10, 0x10, 0x18);
	c_min    = alloc_rgb(0x10, 0x10, 0x18);
	c_sec    = alloc_rgb(0xc8, 0x14, 0x14);
	c_center = alloc_rgb(0x10, 0x10, 0x18);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xclock: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	layout();

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - win_w) / 2,
			(DisplayHeight(dpy, scr) - win_h) / 2,
			(unsigned)win_w, (unsigned)win_h, 0, c_rim, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (running) {
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask, &ev))
			handle_event(&ev);

		{
			time_t	now = time(NULL);
			if (now != last_t) {
				last_t = now;
				need_draw = 1;
			}
		}
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
