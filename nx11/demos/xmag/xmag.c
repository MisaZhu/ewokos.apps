/*
 * xmag -- a pure-Xlib clone of the classic X11R6 "xmag" for NX11 on EwokOS.
 *
 * A screen magnifier: drag a rubber-band rectangle over the source pane and the
 * selected pixels are read back with XGetImage() and drawn enlarged in the view
 * pane, exactly the mechanism the real xmag uses.  This is ordinary X11 code --
 * libNX11 turns every call into a Nano-X server call and the top-level window
 * becomes a real EwokOS xwin window (NANOWM=0), so the desktop window manager
 * owns the frame and the move/resize/close gestures.
 *
 * NX11 is a single-process (NONETWORK) server, so each app owns a private root
 * and there is no shared desktop to grab (the same architectural fact that rules
 * out a stock X11 window manager).  xmag therefore magnifies a rendered source
 * surface held in an offscreen XPixmap -- colour bars, a grey ramp, a
 * checkerboard and a resolution target -- which gives plenty of fine detail to
 * inspect.  XGetImage() reads the real pixels back out of that pixmap, so the
 * grab-and-magnify path is genuine.
 *
 * Controls:  Button1 drag in the left pane selects a region; +/- set the zoom,
 *            f fits, r resets the selection, Q/Esc quits.  Resizable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Magnifier (NX11)"
#define FONT_NAME	"fixed"
#define FULL_CIRCLE	23040
#define POLL_US		40000		/* 40 ms */
#define TB		26		/* toolbar height */
#define SB		20		/* status height */
#define MARGIN		8
#define GAP		10
#define DEF_SEL		40

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;
static Pixmap		source_pm = None;
static int		pm_depth;

static unsigned long	c_bg, c_border, c_text, c_viewbg, c_sel, c_grid;
static unsigned long	bars[8];
static unsigned long	gray[16];

static int		win_w = 560, win_h = 380;
static int		src_x, src_y, src_w, src_h;	/* source pane */
static int		vw_x, vw_y, vw_w, vw_h;		/* view pane   */
static int		sel_x, sel_y, sel_w, sel_h;	/* selection in source coords */
static int		zoom = 0;				/* 0 = fit */
static int		dragging = 0, ax, ay;

static int		running = 1, redraw_src = 1, redraw_view = 1;

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
text_at(int cx, int baseline, const char *s, unsigned long color)
{
	int	len = (int)strlen(s);
	int	tw = XTextWidth(xfont, s, len);

	XSetForeground(dpy, gc, color);
	XDrawString(dpy, win, gc, cx - tw / 2, baseline, s, len);
}

static void
text_left(int x, int baseline, const char *s, unsigned long color)
{
	XSetForeground(dpy, gc, color);
	XDrawString(dpy, win, gc, x, baseline, s, (int)strlen(s));
}

/*----------------------------------------------------------------------------*/
/* the magnifiable source surface                                               */
/*----------------------------------------------------------------------------*/

static void
draw_pattern(void)
{
	int	bh, gy, gh, cy, ch, ty, th, tcx, tcy, rmax, i, xx, yy, cs;
	int	bw, gw;

	if (source_pm == None)
		return;

	/* background */
	XSetForeground(dpy, gc, gray[8]);
	XFillRectangle(dpy, source_pm, gc, 0, 0, src_w, src_h);

	/* colour bars across the top 30% */
	bh = src_h * 30 / 100;
	bw = src_w / 8;
	for (i = 0; i < 8; i++) {
		int	w = (i == 7) ? (src_w - 7 * bw) : bw;
		XSetForeground(dpy, gc, bars[i]);
		XFillRectangle(dpy, source_pm, gc, i * bw, 0, w, bh);
	}

	/* grey ramp, 30%..42% */
	gy = bh;
	gh = src_h * 12 / 100;
	gw = src_w / 16;
	for (i = 0; i < 16; i++) {
		int	w = (i == 15) ? (src_w - 15 * gw) : gw;
		XSetForeground(dpy, gc, gray[i]);
		XFillRectangle(dpy, source_pm, gc, i * gw, gy, w, gh);
	}

	/* checkerboard, 42%..64% */
	cy = gy + gh;
	ch = src_h * 22 / 100;
	cs = 8;
	for (yy = 0; yy < ch; yy += cs)
		for (xx = 0; xx < src_w; xx += cs) {
			int	h = (yy + cs > ch) ? (ch - yy) : cs;
			int	w = (xx + cs > src_w) ? (src_w - xx) : cs;
			XSetForeground(dpy, gc, (((xx / cs) + (yy / cs)) & 1) ? gray[15] : gray[0]);
			XFillRectangle(dpy, source_pm, gc, xx, cy + yy, w, h);
		}

	/* resolution target: concentric circles + spokes in the remainder */
	ty = cy + ch;
	th = src_h - ty;
	if (th > 8) {
		tcx = src_w / 2;
		tcy = ty + th / 2;
		rmax = (src_w < th ? src_w : th) / 2 - 2;
		XSetForeground(dpy, gc, gray[13]);
		XFillRectangle(dpy, source_pm, gc, 0, ty, src_w, th);
		XSetForeground(dpy, gc, gray[2]);
		for (i = rmax; i > 0; i -= 6)
			XDrawArc(dpy, source_pm, gc, tcx - i, tcy - i, 2 * i, 2 * i,
				 0, FULL_CIRCLE);
		for (i = 0; i < 360; i += 15) {
			double	a = i * 3.14159265358979 / 180.0;
			XDrawLine(dpy, source_pm, gc, tcx, tcy,
				  tcx + (int)(rmax * sin(a)), tcy - (int)(rmax * cos(a)));
		}
	}

	/* a label so magnified text is legible */
	XSetForeground(dpy, gc, gray[0]);
	XDrawString(dpy, source_pm, gc, 4, gy - 4, "NX11 xmag source", 16);
}

static void
make_source(void)
{
	if (source_pm != None) {
		XFreePixmap(dpy, source_pm);
		source_pm = None;
	}
	if (src_w < 4 || src_h < 4)
		return;
	source_pm = XCreatePixmap(dpy, win, (unsigned)src_w, (unsigned)src_h,
				  (unsigned)pm_depth);
	draw_pattern();
}

/*----------------------------------------------------------------------------*/
/* layout                                                                       */
/*----------------------------------------------------------------------------*/

static int
fit_scale(void)
{
	int	s;

	if (sel_w <= 0 || sel_h <= 0)
		return 1;
	s = vw_w / sel_w;
	if (vw_h / sel_h < s)
		s = vw_h / sel_h;
	if (s < 1) s = 1;
	if (s > 64) s = 64;
	return s;
}

static void
clamp_sel(void)
{
	if (src_w < 2 || src_h < 2)
		return;
	if (sel_w > src_w) sel_w = src_w;
	if (sel_h > src_h) sel_h = src_h;
	if (sel_x < 0) sel_x = 0;
	if (sel_y < 0) sel_y = 0;
	if (sel_x + sel_w > src_w) sel_x = src_w - sel_w;
	if (sel_y + sel_h > src_h) sel_y = src_h - sel_h;
	if (sel_w < 1) sel_w = 1;
	if (sel_h < 1) sel_h = 1;
}

static void
layout(void)
{
	int	pane_w, pane_h, pane_y;

	pane_y = TB + 4;
	pane_h = win_h - TB - SB - 8;
	pane_w = (win_w - 2 * MARGIN - GAP) / 2;
	if (pane_w < 16) pane_w = 16;
	if (pane_h < 16) pane_h = 16;

	src_x = MARGIN;
	src_y = pane_y;
	src_w = pane_w;
	src_h = pane_h;

	vw_x = MARGIN + pane_w + GAP;
	vw_y = pane_y;
	vw_w = win_w - vw_x - MARGIN;
	vw_h = pane_h;
	if (vw_w < 16) vw_w = 16;

	make_source();
	clamp_sel();
}

/*----------------------------------------------------------------------------*/
/* drawing                                                                      */
/*----------------------------------------------------------------------------*/

static void
draw_toolbar(void)
{
	int	base = TB / 2 + (xfont->ascent - xfont->descent) / 2;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, TB);
	text_left(8, base, "xmag (NX11)", c_text);
	text_left(120, base, "drag in left pane | +/- zoom | f fit | r reset | q quit",
		  c_border);
	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, TB - 1, win_w, TB - 1);
}

static void
draw_source(void)
{
	if (source_pm != None)
		XCopyArea(dpy, source_pm, win, gc, 0, 0, (unsigned)src_w,
			  (unsigned)src_h, src_x, src_y);

	/* selection rectangle */
	if (sel_w > 0 && sel_h > 0) {
		XSetForeground(dpy, gc, c_sel);
		XDrawRectangle(dpy, win, gc, src_x + sel_x, src_y + sel_y,
			       (unsigned)sel_w - 1, (unsigned)sel_h - 1);
	}

	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, src_x - 1, src_y - 1,
		       (unsigned)src_w + 1, (unsigned)src_h + 1);
}

static void
draw_view(void)
{
	XImage *	img;
	int		scale, ox, oy, i, j, bpp;
	int		y0, y1, x0, x1;

	XSetForeground(dpy, gc, c_viewbg);
	XFillRectangle(dpy, win, gc, vw_x, vw_y, vw_w, vw_h);
	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, vw_x, vw_y, (unsigned)vw_w - 1, (unsigned)vw_h - 1);

	if (source_pm == None || sel_w <= 0 || sel_h <= 0)
		return;

	img = XGetImage(dpy, source_pm, sel_x, sel_y, (unsigned)sel_w,
			(unsigned)sel_h, AllPlanes, ZPixmap);
	if (img == NULL) {
		text_at(vw_x + vw_w / 2, vw_y + vw_h / 2, "grab failed", c_text);
		return;
	}

	scale = (zoom > 0) ? zoom : fit_scale();
	ox = vw_x + (vw_w - sel_w * scale) / 2;
	oy = vw_y + (vw_h - sel_h * scale) / 2;

	bpp = img->bits_per_pixel / 8;
	if (bpp < 1) bpp = 1;
	if (bpp > (int)sizeof(unsigned long)) bpp = (int)sizeof(unsigned long);

	for (j = 0; j < sel_h; j++) {
		unsigned char *	row = (unsigned char *)img->data +
					(size_t)j * (size_t)img->bytes_per_line;
		for (i = 0; i < sel_w; i++) {
			unsigned long	pix = 0;
			int		dx = ox + i * scale;
			int		dy = oy + j * scale;

			if (dx + scale <= vw_x || dx >= vw_x + vw_w ||
			    dy + scale <= vw_y || dy >= vw_y + vw_h)
				continue;
			memcpy(&pix, row + (size_t)i * (size_t)bpp, (size_t)bpp);
			XSetForeground(dpy, gc, pix);
			XFillRectangle(dpy, win, gc, dx, dy, (unsigned)scale, (unsigned)scale);
		}
	}

	/* pixel grid once each source pixel is a visible block */
	if (scale >= 8) {
		x0 = ox < vw_x ? vw_x : ox;
		x1 = ox + sel_w * scale; if (x1 > vw_x + vw_w) x1 = vw_x + vw_w;
		y0 = oy < vw_y ? vw_y : oy;
		y1 = oy + sel_h * scale; if (y1 > vw_y + vw_h) y1 = vw_y + vw_h;
		XSetForeground(dpy, gc, c_grid);
		for (i = 0; i <= sel_w; i++) {
			int	gx = ox + i * scale;
			if (gx >= x0 && gx < x1)
				XDrawLine(dpy, win, gc, gx, y0, gx, y1 - 1);
		}
		for (j = 0; j <= sel_h; j++) {
			int	gy = oy + j * scale;
			if (gy >= y0 && gy < y1)
				XDrawLine(dpy, win, gc, x0, gy, x1 - 1, gy);
		}
	}

	XDestroyImage(img);
}

static void
draw_status(void)
{
	char	buf[96];
	int	base = win_h - SB / 2 + (xfont->ascent - xfont->descent) / 2;
	int	scale = (zoom > 0) ? zoom : fit_scale();

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, win_h - SB, win_w, SB);
	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, win_h - SB, win_w, win_h - SB);
	snprintf(buf, sizeof(buf),
		 "selection %dx%d at (%d,%d)   zoom x%d   source %dx%d",
		 sel_w, sel_h, sel_x, sel_y, scale, src_w, src_h);
	text_left(8, base, buf, c_text);
}

/*----------------------------------------------------------------------------*/
/* events                                                                       */
/*----------------------------------------------------------------------------*/

static int
to_src_x(int wx) { int v = wx - src_x; return v < 0 ? 0 : (v >= src_w ? src_w - 1 : v); }
static int
to_src_y(int wy) { int v = wy - src_y; return v < 0 ? 0 : (v >= src_h ? src_h - 1 : v); }

static int
in_source(int wx, int wy)
{
	return wx >= src_x && wx < src_x + src_w && wy >= src_y && wy < src_y + src_h;
}

static void
reset_sel(void)
{
	sel_w = DEF_SEL < src_w ? DEF_SEL : src_w;
	sel_h = DEF_SEL < src_h ? DEF_SEL : src_h;
	sel_x = (src_w - sel_w) / 2;
	sel_y = (src_h - sel_h) / 2;
	zoom = 0;
}

static void
handle_key(XKeyEvent *ke)
{
	KeySym	ks;
	char	buf[8];

	XLookupString(ke, buf, (int)sizeof(buf), &ks, NULL);

	if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
		running = 0;
	} else if (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add) {
		zoom = ((zoom > 0) ? zoom : fit_scale()) + 1;
		if (zoom > 64) zoom = 64;
		redraw_view = 1;
	} else if (ks == XK_minus || ks == XK_KP_Subtract) {
		zoom = ((zoom > 0) ? zoom : fit_scale()) - 1;
		if (zoom < 1) zoom = 1;
		redraw_view = 1;
	} else if (ks == XK_f || ks == XK_F) {
		zoom = 0;
		redraw_view = 1;
	} else if (ks == XK_r || ks == XK_R) {
		reset_sel();
		redraw_src = redraw_view = 1;
	}
}

static void
handle_event(XEvent *ev)
{
	switch (ev->type) {
	case Expose:
		if (ev->xexpose.count == 0)
			redraw_src = redraw_view = 1;
		break;

	case ConfigureNotify:
		if (ev->xconfigure.width != win_w ||
		    ev->xconfigure.height != win_h) {
			win_w = ev->xconfigure.width;
			win_h = ev->xconfigure.height;
			layout();
			redraw_src = redraw_view = 1;
		}
		break;

	case ButtonPress:
		if (ev->xbutton.button == Button1 &&
		    in_source(ev->xbutton.x, ev->xbutton.y)) {
			dragging = 1;
			ax = to_src_x(ev->xbutton.x);
			ay = to_src_y(ev->xbutton.y);
			sel_x = ax; sel_y = ay; sel_w = 1; sel_h = 1;
			redraw_src = 1;
		}
		break;

	case MotionNotify:
		if (dragging) {
			int	bx = to_src_x(ev->xmotion.x);
			int	by = to_src_y(ev->xmotion.y);

			sel_x = ax < bx ? ax : bx;
			sel_y = ay < by ? ay : by;
			sel_w = (ax < bx ? bx - ax : ax - bx) + 1;
			sel_h = (ay < by ? by - ay : ay - by) + 1;
			redraw_src = 1;
		}
		break;

	case ButtonRelease:
		if (dragging && ev->xbutton.button == Button1) {
			dragging = 0;
			clamp_sel();
			redraw_src = redraw_view = 1;
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

/*----------------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
	XEvent	ev;
	int	i;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xmag: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);
	pm_depth = DefaultDepth(dpy, scr);

	c_bg     = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_border = alloc_rgb(0x40, 0x40, 0x48);
	c_text   = alloc_rgb(0x10, 0x10, 0x18);
	c_viewbg = alloc_rgb(0x20, 0x20, 0x28);
	c_sel    = alloc_rgb(0xff, 0xff, 0x00);
	c_grid   = alloc_rgb(0x60, 0x60, 0x68);

	/* SMPTE-ish colour bars */
	bars[0] = alloc_rgb(0xff, 0xff, 0xff);
	bars[1] = alloc_rgb(0xff, 0xff, 0x00);
	bars[2] = alloc_rgb(0x00, 0xff, 0xff);
	bars[3] = alloc_rgb(0x00, 0xff, 0x00);
	bars[4] = alloc_rgb(0xff, 0x00, 0xff);
	bars[5] = alloc_rgb(0xff, 0x00, 0x00);
	bars[6] = alloc_rgb(0x00, 0x00, 0xff);
	bars[7] = alloc_rgb(0x00, 0x00, 0x00);
	for (i = 0; i < 16; i++) {
		int	v = i * 255 / 15;
		gray[i] = alloc_rgb(v, v, v);
	}

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xmag: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
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

	layout();
	reset_sel();

	XSelectInput(dpy, win,
		ExposureMask | StructureNotifyMask | KeyPressMask |
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (running) {
		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask |
				ButtonPressMask | ButtonReleaseMask | PointerMotionMask, &ev))
			handle_event(&ev);

		if (redraw_src) {
			draw_toolbar();
			draw_source();
			redraw_src = 0;
		}
		if (redraw_view) {
			draw_view();
			draw_status();
			redraw_view = 0;
		}
		proc_usleep(POLL_US);
	}

	if (source_pm != None)
		XFreePixmap(dpy, source_pm);
	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
