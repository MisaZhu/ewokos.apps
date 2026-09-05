/*
 * xeyes -- a pure-Xlib clone of the classic X11R6 "xeyes" for NX11 on EwokOS.
 *
 * Two eyes whose pupils follow the pointer.  This is ordinary X11 code: libNX11
 * turns every call into a Nano-X server call and the top-level window becomes a
 * real EwokOS xwin window (NANOWM=0), so the desktop window manager owns the
 * frame and the move/resize/close gestures.  Nothing here knows about EwokOS
 * except proc_usleep(), used to pace the poll loop.
 *
 * The pointer is sampled with XQueryPointer() on a non-blocking poll: each
 * XCheckWindowEvent() -> GsSelect(GR_TIMEOUT_POLL) runs the screen driver
 * PreSelect, which flushes and presents the previous frame.  NX11 has no
 * portable X timer and select() on ConnectionNumber() does not work because it
 * is a single-process (NONETWORK) server whose display fd is not a real socket.
 * Because each NX11 app owns a private root, the pupils track the pointer in
 * this window's coordinate space, which reproduces xeyes for a pointer anywhere
 * in or around the window.
 *
 * Controls:  move the mouse - the eyes follow.  Q/Esc quits, the window manager
 *            close box quits, and the window is freely resizable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Eyes (NX11)"
#define FULL_CIRCLE	23040		/* 360 * 64 */
#define POLL_US		25000		/* 25 ms -> 40 Hz */
#define PAD		8

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned long	c_bg, c_white, c_black, c_edge, c_glint;

static int		win_w = 320, win_h = 200;
static int		ptr_x = 160, ptr_y = 100;
static int		last_px = -1, last_py = -1;

/* eye geometry, recomputed on resize */
static int		lex, ley, rex, rey;	/* centres */
static int		rx, ry;			/* radii   */

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
layout(void)
{
	int eye_h = win_h - 2 * PAD;
	int maxr_x = win_w / 4 - PAD;

	if (eye_h < 8)
		eye_h = 8;
	ry = eye_h / 2;
	rx = (int)(ry * 0.75);
	if (rx > maxr_x)
		rx = maxr_x;
	if (rx < 4)
		rx = 4;
	if (ry < rx)
		ry = rx;

	ley = rey = win_h / 2;
	lex = win_w / 4;
	rex = win_w - win_w / 4;
}

/* Draw one eye centred at (cx,cy); the pupil looks toward (tx,ty). */
static void
draw_eye(int cx, int cy, int tx, int ty)
{
	double	dx, dy, t;
	int	ox, oy, px, py, prx, pry, maxx, maxy;

	/* sclera */
	XSetForeground(dpy, gc, c_white);
	XFillArc(dpy, win, gc, cx - rx, cy - ry, 2 * rx, 2 * ry, 0, FULL_CIRCLE);
	XSetForeground(dpy, gc, c_edge);
	XDrawArc(dpy, win, gc, cx - rx, cy - ry, 2 * rx, 2 * ry, 0, FULL_CIRCLE);

	/* pupil offset, clamped to an ellipse that keeps it inside the sclera */
	maxx = (int)(rx * 0.42);
	maxy = (int)(ry * 0.42);
	dx = (double)(tx - cx);
	dy = (double)(ty - cy);
	if (maxx > 0 && maxy > 0) {
		t = sqrt((dx / maxx) * (dx / maxx) + (dy / maxy) * (dy / maxy));
		if (t > 1.0) {
			dx /= t;
			dy /= t;
		}
	}
	ox = (int)dx;
	oy = (int)dy;

	prx = (int)(rx * 0.34);
	pry = (int)(ry * 0.42);
	if (prx < 2) prx = 2;
	if (pry < 2) pry = 2;
	px = cx + ox;
	py = cy + oy;

	XSetForeground(dpy, gc, c_black);
	XFillArc(dpy, win, gc, px - prx, py - pry, 2 * prx, 2 * pry, 0, FULL_CIRCLE);

	/* a small glint so the eye looks alive */
	XSetForeground(dpy, gc, c_glint);
	XFillArc(dpy, win, gc, px - prx / 2, py - pry / 2 - 1,
		 prx > 2 ? prx : 2, pry > 3 ? pry / 2 : 2, 0, FULL_CIRCLE);
}

static void
redraw(void)
{
	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);
	draw_eye(lex, ley, ptr_x, ptr_y);
	draw_eye(rex, rey, ptr_x, ptr_y);
}

static void
get_pointer(void)
{
	Window		root_r, child;
	int		root_x, root_y, wx, wy;
	unsigned int	mask;

	if (XQueryPointer(dpy, win, &root_r, &child, &root_x, &root_y,
			  &wx, &wy, &mask)) {
		ptr_x = wx;
		ptr_y = wy;
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

	case MotionNotify:
		ptr_x = ev->xmotion.x;
		ptr_y = ev->xmotion.y;
		need_draw = 1;
		break;

	case KeyPress: {
		KeySym	ks;
		char	buf[8];

		XLookupString(&ev->xkey, buf, (int)sizeof(buf), &ks, NULL);
		if (ks == XK_q || ks == XK_Q || ks == XK_Escape)
			running = 0;
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
		fprintf(stderr, "xeyes: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg    = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_white = alloc_rgb(0xff, 0xff, 0xff);
	c_black = alloc_rgb(0x10, 0x10, 0x14);
	c_edge  = alloc_rgb(0x30, 0x30, 0x38);
	c_glint = alloc_rgb(0xff, 0xff, 0xff);

	layout();

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - win_w) / 2,
			(DisplayHeight(dpy, scr) - win_h) / 2,
			(unsigned)win_w, (unsigned)win_h, 0, c_edge, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | KeyPressMask | PointerMotionMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (running) {
		/* drains input and, through GsSelect(POLL), presents last frame */
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask |
				PointerMotionMask, &ev))
			handle_event(&ev);

		get_pointer();
		if (need_draw || ptr_x != last_px || ptr_y != last_py) {
			redraw();
			need_draw = 0;
			last_px = ptr_x;
			last_py = ptr_y;
		}
		proc_usleep(POLL_US);
	}

	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
