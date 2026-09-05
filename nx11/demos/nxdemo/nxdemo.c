/*
 * nxdemo -- a Nano-X client for NX11 on EwokOS.
 *
 * Nano-X is built as a library (NONETWORK), so GrOpen() starts the server
 * inside this process: the top-level window created below *is* an EwokOS xwin
 * window (see src/nanox/win_ewokos.c), which means the EwokOS window manager
 * draws the frame and moves, resizes, raises, focuses and closes it exactly
 * like any other desktop window.
 *
 * What this exercises:
 *   - GrNewWindow()/GrSetWMProperties()/GrMapWindow() window lifetime
 *   - EXPOSURE and UPDATE(SIZE) driven redraw, so resizing works
 *   - mouse (motion + button) and keyboard events, CLOSE_REQ from the frame
 *   - GrFillRect()/GrRect()/GrLine()/GrText() drawing, all of which end up
 *     graph_blt'ed into the xwin workspace
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nano-X.h>

#define DEMO_TITLE		"Nano-X Demo (NX11)"

#define DEF_W			440
#define DEF_H			320

#define C_BG			GR_RGB(0xf2, 0xf2, 0xf2)
#define C_BORDER		GR_RGB(0x60, 0x60, 0x68)
#define C_TEXT			GR_RGB(0x20, 0x20, 0x20)
#define C_DIM			GR_RGB(0x70, 0x70, 0x78)
#define C_BOX			GR_RGB(0xff, 0xff, 0xff)
#define C_BOX_HI		GR_RGB(0xd8, 0xe4, 0xf6)

/* the two push buttons, laid out from the current window size*/
#define BTN_H			30
#define BTN_GAP			12
#define BTN_MARGIN		16

typedef struct {
	GR_COORD	x, y, w, h;
} BOX;

static GR_WINDOW_ID	wid;
static GR_GC_ID		gc;
static GR_FONT_ID	font;

static GR_SCREEN_INFO	si;
static GR_SIZE		win_w = DEF_W;
static GR_SIZE		win_h = DEF_H;
static GR_COORD		mouse_x = -1;
static GR_COORD		mouse_y = -1;
static GR_BUTTON	buttons;
static int		clicks;
static char		lastkey[32] = "";

static BOX		btn_click;
static BOX		btn_quit;

/*----------------------------------------------------------------------------*/

static void
box_set(BOX *b, GR_COORD x, GR_COORD y, GR_COORD w, GR_COORD h)
{
	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
}

static int
box_hit(const BOX *b, GR_COORD x, GR_COORD y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void
box_layout(void)
{
	GR_COORD w = (win_w - BTN_MARGIN*2 - BTN_GAP) / 2;

	if (w < 40)
		w = 40;
	box_set(&btn_click, BTN_MARGIN, win_h - BTN_H - BTN_MARGIN, w, BTN_H);
	box_set(&btn_quit, BTN_MARGIN + w + BTN_GAP, win_h - BTN_H - BTN_MARGIN, w, BTN_H);
}

static void
text(GR_COORD x, GR_COORD y, const char *s, GR_COLOR fg)
{
	GrSetGCForeground(gc, fg);
	GrText(wid, gc, x, y, (void *)s, (GR_COUNT)strlen(s), GR_TFASCII|GR_TFTOP);
}

static void
draw_button(const BOX *b, const char *label, GR_COLOR fill)
{
	GR_SIZE tw = 0, th = 0, base = 0;

	GrSetGCForeground(gc, fill);
	GrFillRect(wid, gc, b->x, b->y, b->w, b->h);
	GrSetGCForeground(gc, C_BORDER);
	GrRect(wid, gc, b->x, b->y, b->w, b->h);

	GrGetGCTextSize(gc, (void *)label, (int)strlen(label), GR_TFASCII, &tw, &th, &base);
	text(b->x + (b->w - tw)/2, b->y + (b->h - th)/2, label, C_TEXT);
}

/* a strip of the primary and secondary colours, so it is obvious at a glance
   that 32bpp ARGB conversion is working*/
static void
draw_palette(GR_COORD y)
{
	static const unsigned char rgb[][3] = {
		{0xe6, 0x19, 0x4b}, {0x3c, 0xb4, 0x4b}, {0x43, 0x63, 0xd8},
		{0xf5, 0x82, 0x31}, {0x91, 0x1e, 0xb4}, {0x46, 0xf0, 0xf0},
		{0xf0, 0x32, 0xe6}, {0xbc, 0xf6, 0x0c}, {0xfa, 0xbe, 0x33},
		{0x00, 0x80, 0x80},
	};
	GR_COORD n = (GR_COORD)(sizeof(rgb)/sizeof(rgb[0]));
	GR_COORD w = (win_w - BTN_MARGIN*2) / n;
	GR_COORD i;

	for (i = 0; i < n; i++)
		GrSetGCForeground(gc, GR_RGB(rgb[i][0], rgb[i][1], rgb[i][2]));
	for (i = 0; i < n; i++) {
		GrSetGCForeground(gc, GR_RGB(rgb[i][0], rgb[i][1], rgb[i][2]));
		GrFillRect(wid, gc, BTN_MARGIN + w*i, y, w, 18);
	}
}

static void
draw_all(void)
{
	char	buf[128];
	GR_COORD	y;

	box_layout();

	GrSetGCForeground(gc, C_BG);
	GrFillRect(wid, gc, 0, 0, win_w, win_h);

	y = 12;
	text(BTN_MARGIN, y, DEMO_TITLE, C_TEXT);
	y += 22;

	GrSetGCForeground(gc, C_DIM);
	GrLine(wid, gc, BTN_MARGIN, y, win_w - BTN_MARGIN, y);
	y += 10;

	snprintf(buf, sizeof(buf), "screen: %dx%d  %dbpp  %d builtin fonts",
		(int)si.cols, (int)si.rows, si.bpp, si.fonts);
	text(BTN_MARGIN, y, buf, C_DIM);
	y += 18;

	if (mouse_x < 0)
		snprintf(buf, sizeof(buf), "mouse : (waiting for input)");
	else
		snprintf(buf, sizeof(buf), "mouse : x=%d y=%d buttons=0x%02x",
			(int)mouse_x, (int)mouse_y, (unsigned)buttons);
	text(BTN_MARGIN, y, buf, C_DIM);
	y += 18;

	snprintf(buf, sizeof(buf), "keys  : %s", lastkey[0] ? lastkey : "(waiting for input)");
	text(BTN_MARGIN, y, buf, C_DIM);
	y += 18;

	snprintf(buf, sizeof(buf), "clicks: %d", clicks);
	text(BTN_MARGIN, y, buf, C_TEXT);
	y += 26;

	draw_palette(y);

	draw_button(&btn_click, "Click me", C_BOX);
	draw_button(&btn_quit, "Quit", C_BOX_HI);
}

/* only the mouse line needs refreshing while the pointer moves, and repainting
   the whole window on every motion event would swamp the server*/
static void
draw_mouse_line(void)
{
	char	buf[128];
	GR_COORD	y = 12 + 22 + 10 + 18;

	GrSetGCForeground(gc, C_BG);
	GrFillRect(wid, gc, BTN_MARGIN, y, win_w - BTN_MARGIN*2, 18);
	if (mouse_x < 0)
		snprintf(buf, sizeof(buf), "mouse : (waiting for input)");
	else
		snprintf(buf, sizeof(buf), "mouse : x=%d y=%d buttons=0x%02x",
			(int)mouse_x, (int)mouse_y, (unsigned)buttons);
	text(BTN_MARGIN, y, buf, C_DIM);
}

/*----------------------------------------------------------------------------*/

static void
record_key(GR_KEY ch)
{
	size_t	len;

	if (ch >= ' ' && ch < 0x7f) {
		len = strlen(lastkey);
		if (len >= sizeof(lastkey) - 2) {
			memmove(lastkey, lastkey + 1, len);
			len--;
		}
		lastkey[len] = (char)ch;
		lastkey[len + 1] = '\0';
	} else
		snprintf(lastkey, sizeof(lastkey), "<key 0x%04x>", (unsigned)ch);
}

int
main(int argc, char **argv)
{
	GR_WM_PROPERTIES	wmprops;
	GR_EVENT		event;
	int			done = 0;

	(void)argc;
	(void)argv;

	if (GrOpen() < 0) {
		fprintf(stderr, "nxdemo: cannot open Nano-X (is /dev/x running?)\n");
		return 1;
	}

	GrGetScreenInfo(&si);

	/* centre the window, but never larger than the desktop*/
	if (si.cols > 0 && (GR_SIZE)si.cols < DEF_W + 40)
		win_w = (GR_SIZE)si.cols - 40;
	if (si.rows > 0 && (GR_SIZE)si.rows < DEF_H + 40)
		win_h = (GR_SIZE)si.rows - 40;

	gc = GrNewGC();
	/* a fresh GC paints the background of every glyph cell (usebackground
	   defaults to GR_TRUE); XDrawString turns this off, so do the same here or
	   the text comes out on black bands*/
	GrSetGCUseBackground(gc, GR_FALSE);
	font = GrCreateFontEx(GR_FONT_SYSTEM_VAR, 16, 16, NULL);
	if (font)
		GrSetGCFont(gc, font);

	/* bordersize 0: the EwokOS frame is drawn by the xserver, so Nano-X owns
	   exactly the pixels of the window it created*/
	wid = GrNewWindow(GR_ROOT_WINDOW_ID,
			(si.cols > win_w)? (si.cols - win_w)/2: 20,
			(si.rows > win_h)? (si.rows - win_h)/2: 20,
			win_w, win_h, 0, C_BG, C_BORDER);
	if (wid == 0) {
		fprintf(stderr, "nxdemo: cannot create window\n");
		GrClose();
		return 1;
	}

	memset(&wmprops, 0, sizeof(wmprops));
	wmprops.flags = GR_WM_FLAGS_TITLE | GR_WM_FLAGS_BACKGROUND;
	wmprops.title = (char *)DEMO_TITLE;
	wmprops.background = C_BG;
	GrSetWMProperties(wid, &wmprops);

	GrSelectEvents(wid,
		GR_EVENT_MASK_EXPOSURE |
		GR_EVENT_MASK_UPDATE |
		GR_EVENT_MASK_CLOSE_REQ |
		GR_EVENT_MASK_BUTTON_DOWN |
		GR_EVENT_MASK_BUTTON_UP |
		GR_EVENT_MASK_MOUSE_POSITION |
		GR_EVENT_MASK_KEY_DOWN);

	GrMapWindow(wid);

	while (!done) {
		GrGetNextEvent(&event);
		switch (event.type) {
		case GR_EVENT_TYPE_EXPOSURE:
			draw_all();
			break;

		case GR_EVENT_TYPE_UPDATE:
			if (event.update.utype == GR_UPDATE_SIZE) {
				win_w = event.update.width;
				win_h = event.update.height;
				draw_all();
			} else if (event.update.utype == GR_UPDATE_DESTROY)
				done = 1;
			break;

		case GR_EVENT_TYPE_MOUSE_POSITION:
			mouse_x = event.mouse.x;
			mouse_y = event.mouse.y;
			buttons = event.mouse.buttons;
			draw_mouse_line();
			break;

		case GR_EVENT_TYPE_BUTTON_DOWN:
			buttons = event.button.buttons;
			draw_mouse_line();
			if (box_hit(&btn_quit, event.button.x, event.button.y))
				done = 1;
			else if (box_hit(&btn_click, event.button.x, event.button.y)) {
				clicks++;
				draw_all();
			}
			break;

		case GR_EVENT_TYPE_BUTTON_UP:
			buttons = event.button.buttons;
			draw_mouse_line();
			break;

		case GR_EVENT_TYPE_KEY_DOWN:
			record_key(event.keystroke.ch);
			if (event.keystroke.ch == MWKEY_ESCAPE ||
					event.keystroke.ch == 'q' ||
					event.keystroke.ch == 'Q')
				done = 1;
			else
				draw_all();
			break;

		case GR_EVENT_TYPE_CLOSE_REQ:
			done = 1;
			break;

		case GR_EVENT_TYPE_ERROR:
			fprintf(stderr, "nxdemo: server error %d in %s\n",
				(int)event.error.code, event.error.name);
			done = 1;
			break;

		default:
			break;
		}
	}

	GrClose();
	return 0;
}
