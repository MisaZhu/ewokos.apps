/*
 * x11demo -- a plain Xlib client for NX11 on EwokOS.
 *
 * Nothing in this file knows about EwokOS or Nano-X: it is ordinary X11 code.
 * libNX11 turns each call into a Nano-X server call, and because NX11 has no
 * window manager of its own (NANOWM=0), the window created below is created as
 * a real EwokOS xwin window - the desktop window manager draws the frame and
 * owns the move/resize/raise/focus/close gestures (src/nanox/win_ewokos.c).
 *
 * What this exercises:
 *   - XOpenDisplay/XCreateSimpleWindow/XMapWindow window lifetime
 *   - XStoreName -> the EwokOS window title
 *   - Expose, ConfigureNotify, ButtonPress, MotionNotify, KeyPress events
 *   - the WM_DELETE_WINDOW ClientMessage NX11 synthesises for a frame close
 *   - XFillRectangle/XDrawRectangle/XDrawLine/XDrawString/XTextWidth drawing
 *   - XAllocColor on the TrueColor visual the server publishes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define DEMO_TITLE		"X11 Demo (NX11)"

#define DEF_W			460
#define DEF_H			330

#define MARGIN			16
#define BTN_H			30
#define BTN_GAP			12

#define FONT_NAME		"fixed"		/* mapped to the builtin SystemFixed font*/

/* RGB triplets; the server is TrueColor, so they go through XAllocColor*/
#define RGB_BG			0xf2, 0xf2, 0xf2
#define RGB_TEXT		0x20, 0x20, 0x20
#define RGB_DIM			0x70, 0x70, 0x78
#define RGB_BORDER		0x60, 0x60, 0x68
#define RGB_BOX			0xff, 0xff, 0xff
#define RGB_BOX_HI		0xd8, 0xe4, 0xf6

typedef struct {
	int	x, y, w, h;
} Box;

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;

static unsigned long	c_bg, c_text, c_dim, c_border, c_box, c_boxhi;

static int		win_w = DEF_W;
static int		win_h = DEF_H;
static int		mouse_x = -1;
static int		mouse_y = -1;
static int		clicks;
static char		lastkey[40] = "";

static Box		btn_click;
static Box		btn_quit;

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
box_set(Box *b, int x, int y, int w, int h)
{
	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
}

static int
box_hit(const Box *b, int x, int y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void
box_layout(void)
{
	int	w = (win_w - MARGIN*2 - BTN_GAP) / 2;

	if (w < 40)
		w = 40;
	box_set(&btn_click, MARGIN, win_h - BTN_H - MARGIN, w, BTN_H);
	box_set(&btn_quit, MARGIN + w + BTN_GAP, win_h - BTN_H - MARGIN, w, BTN_H);
}

static void
draw_text(int x, int y, const char *s, unsigned long fg)
{
	XSetForeground(dpy, gc, fg);
	XDrawString(dpy, win, gc, x, y, s, (int)strlen(s));
}

static void
draw_button(const Box *b, const char *label, unsigned long fill)
{
	int	len = (int)strlen(label);
	int	tw = XTextWidth(xfont, label, len);
	int	th = xfont ? xfont->ascent + xfont->descent : 12;

	XSetForeground(dpy, gc, fill);
	XFillRectangle(dpy, win, gc, b->x, b->y, (unsigned)b->w, (unsigned)b->h);
	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, b->x, b->y, (unsigned)b->w, (unsigned)b->h);
	draw_text(b->x + (b->w - tw)/2, b->y + (b->h + th)/2 - 2, label, c_text);
}

/* a strip of primary colours - a quick check that the TrueColor pixel values
   the server hands out really do come back as the colours asked for*/
static void
draw_palette(int y)
{
	static const unsigned char rgb[][3] = {
		{0xe6, 0x19, 0x4b}, {0x3c, 0xb4, 0x4b}, {0x43, 0x63, 0xd8},
		{0xf5, 0x82, 0x31}, {0x91, 0x1e, 0xb4}, {0x46, 0xf0, 0xf0},
		{0xf0, 0x32, 0xe6}, {0xbc, 0xf6, 0x0c}, {0xfa, 0xbe, 0x33},
		{0x00, 0x80, 0x80},
	};
	int	n = (int)(sizeof(rgb)/sizeof(rgb[0]));
	int	w = (win_w - MARGIN*2) / n;
	int	i;

	for (i = 0; i < n; i++) {
		XSetForeground(dpy, gc, alloc_rgb(rgb[i][0], rgb[i][1], rgb[i][2]));
		XFillRectangle(dpy, win, gc, MARGIN + w*i, y, (unsigned)w, 18u);
	}
}

static void
draw_all(void)
{
	char	buf[128];
	int	th = xfont ? xfont->ascent + xfont->descent : 12;
	int	y;

	box_layout();

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, (unsigned)win_w, (unsigned)win_h);

	y = 12 + xfont->ascent;
	draw_text(MARGIN, y, DEMO_TITLE, c_text);

	y += th - xfont->ascent + 10;
	XSetForeground(dpy, gc, c_dim);
	XDrawLine(dpy, win, gc, MARGIN, y, win_w - MARGIN, y);

	y += 10 + xfont->ascent;
	snprintf(buf, sizeof(buf), "display: %dx%d  depth %d  screen %d",
		DisplayWidth(dpy, scr), DisplayHeight(dpy, scr),
		DefaultDepth(dpy, scr), scr);
	draw_text(MARGIN, y, buf, c_dim);

	y += th;
	if (mouse_x < 0)
		snprintf(buf, sizeof(buf), "mouse  : (waiting for input)");
	else
		snprintf(buf, sizeof(buf), "mouse  : x=%d y=%d", mouse_x, mouse_y);
	draw_text(MARGIN, y, buf, c_dim);

	y += th;
	snprintf(buf, sizeof(buf), "keys   : %s", lastkey[0] ? lastkey : "(waiting for input)");
	draw_text(MARGIN, y, buf, c_dim);

	y += th;
	snprintf(buf, sizeof(buf), "clicks : %d", clicks);
	draw_text(MARGIN, y, buf, c_text);

	draw_palette(y + 12);

	draw_button(&btn_click, "Click me", c_box);
	draw_button(&btn_quit, "Quit", c_boxhi);
}

/* only the mouse line needs refreshing while the pointer moves*/
static void
draw_mouse_line(void)
{
	char	buf[128];
	int	th = xfont ? xfont->ascent + xfont->descent : 12;
	int	y = 12 + th + 10 + 10 + th + th;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, MARGIN, y, (unsigned)(win_w - MARGIN*2), (unsigned)th);
	if (mouse_x < 0)
		snprintf(buf, sizeof(buf), "mouse  : (waiting for input)");
	else
		snprintf(buf, sizeof(buf), "mouse  : x=%d y=%d", mouse_x, mouse_y);
	draw_text(MARGIN, y + xfont->ascent, buf, c_dim);
}

/*----------------------------------------------------------------------------*/

/* echoes the last few keys, and returns the keysym so the caller can spot the
   quit keys*/
static KeySym
record_key(XKeyEvent *ke)
{
	const char *	name;
	char		buf[16];
	KeySym		keysym = NoSymbol;
	size_t		bl, ll;
	int		n;

	n = XLookupString(ke, buf, (int)sizeof(buf) - 1, &keysym, NULL);
	/* NX11 writes only buffer[0] and leaves termination to the caller*/
	buf[n > 0 ? n : 0] = '\0';

	/* 0 characters, or a control byte: show the keysym name instead*/
	if (buf[0] < ' ' || buf[0] >= 0x7f) {
		name = XKeysymToString(keysym);
		snprintf(buf, sizeof(buf), "<%s>", name ? name : "?");
	}

	bl = strlen(buf);
	ll = strlen(lastkey);
	if (ll > bl)
		memmove(lastkey, lastkey + bl, ll - bl + 1);
	else
		lastkey[0] = '\0';
	strncat(lastkey, buf, sizeof(lastkey) - strlen(lastkey) - 1);

	return keysym;
}

int
main(int argc, char **argv)
{
	Atom		wm_delete;
	XEvent		ev;
	int		done = 0;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "x11demo: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg     = alloc_rgb(RGB_BG);
	c_text   = alloc_rgb(RGB_TEXT);
	c_dim    = alloc_rgb(RGB_DIM);
	c_border = alloc_rgb(RGB_BORDER);
	c_box    = alloc_rgb(RGB_BOX);
	c_boxhi  = alloc_rgb(RGB_BOX_HI);

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "x11demo: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	/* border width 0: the EwokOS frame is drawn by the window manager, so the
	   window owns exactly the pixels asked for here*/
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - DEF_W)/2,
			(DisplayHeight(dpy, scr) - DEF_H)/2,
			DEF_W, DEF_H, 0, c_border, c_bg);

	XStoreName(dpy, win, DEMO_TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win,
		ExposureMask |
		StructureNotifyMask |
		ButtonPressMask |
		ButtonReleaseMask |
		PointerMotionMask |
		KeyPressMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (!done) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw_all();
			break;

		case ConfigureNotify:
			if (ev.xconfigure.width != win_w || ev.xconfigure.height != win_h) {
				win_w = ev.xconfigure.width;
				win_h = ev.xconfigure.height;
				draw_all();
			}
			break;

		case MotionNotify:
			mouse_x = ev.xmotion.x;
			mouse_y = ev.xmotion.y;
			draw_mouse_line();
			break;

		case ButtonPress:
			mouse_x = ev.xbutton.x;
			mouse_y = ev.xbutton.y;
			draw_mouse_line();
			if (box_hit(&btn_quit, mouse_x, mouse_y))
				done = 1;
			else if (box_hit(&btn_click, mouse_x, mouse_y)) {
				clicks++;
				draw_all();
			}
			break;

		case KeyPress: {
			KeySym	ks = record_key(&ev.xkey);

			if (ks == XK_Escape || ks == XK_q || ks == XK_Q)
				done = 1;
			else
				draw_all();
			break;
		}

		case ClientMessage:
			/* NX11 hands a frame close to the client as this message*/
			if ((Atom)ev.xclient.data.l[0] == wm_delete)
				done = 1;
			break;

		default:
			break;
		}
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
