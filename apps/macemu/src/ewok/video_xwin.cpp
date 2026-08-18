/*
 *  video_xwin.cpp - Video driver for the EwokOS xwin system (no SDL)
 *
 *  The Mac frame buffer (the_buffer) is what the 68k guest draws into
 *  (big-endian, MacOS layout).  On every VBL (VideoInterrupt) it is
 *  converted into an ARGB8888 graph_t which the xwin repaint callback
 *  then blits into the window, letterboxed like minivmac does.
 */

#include "sysdeps.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <x/xwin.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/proc.h>

#include "cpu_emulation.h"
#include "video.h"
#include "adb.h"
#include "prefs.h"
#include "user_strings.h"
#include "main.h"

#define DEBUG 0
#include "debug.h"

// From newcpu.cpp: set to stop the 68k interpreter loop
extern bool quit_program;

// Extra key codes not in ewoksys/keydef.h (same values minivmac uses)
#ifndef KEY_INSERT
#define KEY_INSERT      0xF3
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP      0xF4
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN    0xF5
#endif
#ifndef KEY_F1
#define KEY_F1          0xF6
#endif
#ifndef KEY_F2
#define KEY_F2          0xF7
#endif
#ifndef KEY_F3
#define KEY_F3          0xF8
#endif
#ifndef KEY_F4
#define KEY_F4          0xF9
#endif
#ifndef KEY_F5
#define KEY_F5          0xFA
#endif
#ifndef KEY_F6
#define KEY_F6          0xFB
#endif
#ifndef KEY_F7
#define KEY_F7          0xFC
#endif
#ifndef KEY_F8
#define KEY_F8          0xFD
#endif
#ifndef KEY_F9
#define KEY_F9          0xFE
#endif
#ifndef KEY_F10
#define KEY_F10         0xFF
#endif
#ifndef KEY_F11
#define KEY_F11         0x100
#endif
#ifndef KEY_F12
#define KEY_F12         0x101
#endif
#ifndef KEY_CAPSLOCK
#define KEY_CAPSLOCK    0xA1
#endif
#ifndef KEY_SHIFT
#define KEY_SHIFT       KEY_LSHIFT
#endif
#ifndef KEY_ALT
#define KEY_ALT         0xA5
#endif
#ifndef KEY_RCTRL
#define KEY_RCTRL       0xA6
#endif
#ifndef KEY_RALT
#define KEY_RALT        0xA7
#endif

/*
 *  Globals shared with the rest of the emulator
 *  (VideoMonitors itself is defined in src/video.cpp)
 */

uint32 MacScreenWidth;
uint32 MacScreenHeight;

/*
 *  State
 */

// Parsed "screen" preference
static int display_width = 640;
static int display_height = 480;
static int display_depth = 8;

// Mac frame buffer (guest draws here, big-endian MacOS layout)
static uint8 *the_buffer = NULL;
static uint32 the_buffer_size = 0;

// Converted ARGB8888 frame, blitted to the window on repaint
static graph_t *frame_graph = NULL;
static uint8 frame_pal[256 * 3];      // Current palette (RGB)

static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool frame_pending = false;
static volatile bool emerg_quit = false;

// Window letterboxing (set by on_repaint, used for mouse mapping)
static float view_scale = 1.0f;
static int view_offset_x = 0;
static int view_offset_y = 0;
static int last_mac_x = -1, last_mac_y = -1;

static x_t *x_context = NULL;
static xwin_t *xwin = NULL;
static pthread_t emul_thread_id = 0;

// Mode-switch resize request (emulation thread -> x thread).  All xwin
// API calls must happen on the x thread: the xwin functions do IPC to
// the x server, and issuing them from the emulation thread while the
// x thread is inside its own xwin IPC deadlocks the boot (black screen).
static volatile bool resize_pending = false;
static int resize_w = 0, resize_h = 0;
static bool presented_once = false;
static bool vbl_once = false;

/*
 *  monitor_desc subclass for the xwin display
 */

class XWIN_monitor_desc : public monitor_desc {
public:
	XWIN_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
	 : monitor_desc(available_modes, default_depth, default_id) {}
	~XWIN_monitor_desc() {}

	virtual void switch_to_current_mode(void);
	virtual void set_palette(uint8 *pal, int num);
};

/*
 *  Frame conversion: Mac layout (big-endian) -> host ARGB8888
 */

static void convert_frame(void)
{
	if (frame_graph == NULL || the_buffer == NULL)
		return;

	const video_mode &mode = VideoMonitors[0]->get_current_mode();
	uint32 w = mode.x, h = mode.y, bpr = mode.bytes_per_row;
	uint32 *dst = frame_graph->buffer;

	switch (mode.depth) {
	case VDEPTH_1BIT:
	case VDEPTH_2BIT:
	case VDEPTH_4BIT:
	case VDEPTH_8BIT: {
		int ppb, bpp, mask;	// pixels per byte, bits per pixel
		switch (mode.depth) {
		case VDEPTH_1BIT: ppb = 8; bpp = 1; mask = 1; break;
		case VDEPTH_2BIT: ppb = 4; bpp = 2; mask = 3; break;
		case VDEPTH_4BIT: ppb = 2; bpp = 4; mask = 15; break;
		default:          ppb = 1; bpp = 8; mask = 255; break;
		}
		for (uint32 y = 0; y < h; y++) {
			const uint8 *src = the_buffer + y * bpr;
			for (uint32 x = 0; x < w; x++) {
				int idx;
				if (ppb == 1) {
					idx = src[x];
				} else {
					int bitpos = 8 - ((x % ppb) + 1) * bpp;
					idx = (src[x / ppb] >> bitpos) & mask;
				}
				uint8 r = frame_pal[idx * 3 + 0];
				uint8 g = frame_pal[idx * 3 + 1];
				uint8 b = frame_pal[idx * 3 + 2];
				dst[x] = 0xff000000 | (r << 16) | (g << 8) | b;
			}
			dst += w;
		}
		break;
	}
	case VDEPTH_16BIT:
		// Mac 16-bit = big-endian RGB555
		for (uint32 y = 0; y < h; y++) {
			const uint8 *src = the_buffer + y * bpr;
			for (uint32 x = 0; x < w; x++) {
				uint32 v = (src[x * 2] << 8) | src[x * 2 + 1];
				uint32 r = (v >> 10) & 0x1f;
				uint32 g = (v >> 5) & 0x1f;
				uint32 b = v & 0x1f;
				r = (r << 3) | (r >> 2);
				g = (g << 3) | (g >> 2);
				b = (b << 3) | (b >> 2);
				dst[x] = 0xff000000 | (r << 16) | (g << 8) | b;
			}
			dst += w;
		}
		break;
	case VDEPTH_32BIT:
		// Mac 32-bit = big-endian xRGB: bytes [00][RR][GG][BB]
		for (uint32 y = 0; y < h; y++) {
			const uint8 *src = the_buffer + y * bpr;
			for (uint32 x = 0; x < w; x++) {
				dst[x] = 0xff000000 | (src[x * 4 + 1] << 16) | (src[x * 4 + 2] << 8) | src[x * 4 + 3];
			}
			dst += w;
		}
		break;
	}
}

/*
 *  xwin callbacks
 */

static void on_xwin_resize(xwin_t *win)
{
	(void)win;
}

static void on_xwin_event(xwin_t *win, xevent_t *ev)
{
	extern int xwin_key2adb(int key);

	switch (ev->type) {
	case XEVT_IM: {
		int code = xwin_key2adb(ev->value.im.key_code);
		if (code >= 0) {
			if (ev->state == XIM_STATE_PRESS)
				ADBKeyDown(code);
			else
				ADBKeyUp(code);
		}
		break;
	}
	case XEVT_MOUSE: {
		gpos_t pos = xwin_get_inside_pos(win, ev->value.mouse.x, ev->value.mouse.y);
		if (ev->state == MOUSE_STATE_DOWN) {
			ADBMouseDown(0);
		} else if (ev->state == MOUSE_STATE_UP) {
			ADBMouseUp(0);
		} else if (ev->state == MOUSE_STATE_MOVE) {
			int mac_x = (int)((pos.x - view_offset_x) / view_scale);
			int mac_y = (int)((pos.y - view_offset_y) / view_scale);
			if (last_mac_x >= 0) {
				int dx = mac_x - last_mac_x;
				int dy = mac_y - last_mac_y;
				if (dx || dy)
					ADBMouseMoved(dx, dy);
			}
			last_mac_x = mac_x;
			last_mac_y = mac_y;
		}
		break;
	}
	case XEVT_WIN:
		if (ev->value.window.event == XEVT_WIN_CLOSE) {
			emerg_quit = true;
			ADBKeyDown(0x7f);	// Power key
			ADBKeyUp(0x7f);
		}
		break;
	}
}

static void on_xwin_repaint(xwin_t *win, graph_t *g)
{
	if (g == NULL)
		return;

	graph_fill_rect(g, 0, 0, g->w, g->h, 0xff000000);

	pthread_mutex_lock(&frame_lock);
	if (frame_graph != NULL && frame_graph->buffer != NULL) {
		float scale_x = (float)g->w / frame_graph->w;
		float scale_y = (float)g->h / frame_graph->h;
		float scale = (scale_x < scale_y) ? scale_x : scale_y;
		if (scale < 1.0f) scale = 1.0f;

		int scaled_w = (int)(frame_graph->w * scale);
		int scaled_h = (int)(frame_graph->h * scale);
		int offset_x = (g->w - scaled_w) / 2;
		int offset_y = (g->h - scaled_h) / 2;

		view_scale = scale;
		view_offset_x = offset_x;
		view_offset_y = offset_y;

		if (scale > 1.0f) {
			graph_t *scaled = graph_scalef_fast(frame_graph, scale);
			if (scaled != NULL) {
				graph_blt(scaled, 0, 0, scaled_w, scaled_h, g, offset_x, offset_y, scaled_w, scaled_h);
				graph_free(scaled);
			}
		} else {
			graph_blt(frame_graph, 0, 0, frame_graph->w, frame_graph->h,
				g, offset_x, offset_y, frame_graph->w, frame_graph->h);
		}
	}
	pthread_mutex_unlock(&frame_lock);
}

static void xwin_loop(void *p)
{
	(void)p;

	if (emerg_quit) {
		x_terminate(x_context);
		return;
	}

	uint64_t tik = kernel_tic_ms(0);

	// Apply a pending mode-switch resize (deferred here so it runs on
	// the x thread, see switch_to_current_mode)
	if (resize_pending && xwin != NULL) {
		resize_pending = false;
		xwin_resize_to(xwin, resize_w, resize_h);
		xwin_hide_cursor(xwin, true);
	}

	bool do_repaint = false;
	pthread_mutex_lock(&frame_lock);
	if (frame_pending) {
		frame_pending = false;
		do_repaint = true;
	}
	pthread_mutex_unlock(&frame_lock);

	if (do_repaint && xwin != NULL) {
		if (!presented_once) {
			presented_once = true;
			printf("xwin: presenting guest frames\n");
		}
		xwin_repaint(xwin);
	}

	uint32_t gap = (uint32_t)(kernel_tic_ms(0) - tik);
	if (gap < 1000 / 60)
		proc_usleep((1000 / 60 - gap) * 1000);
}

static void *emul_thread_func(void *arg)
{
	(void)arg;
	Start680x0();		// returns when quit_program is set
	// Emulation done: ask the main thread's x loop to exit
	if (x_context != NULL)
		x_terminate(x_context);
	return NULL;
}

/*
 *  Run the emulation, minivmac-style: the 68k core runs in its own
 *  pthread while the x event loop (x_run) runs on the calling (main)
 *  thread.  All xwin API calls therefore happen on the main thread.
 *  Returns when emulation has quit.
 */

void VideoRunLoop(void)
{
	if (x_context == NULL || xwin == NULL)
		return;

	if (pthread_create(&emul_thread_id, NULL, emul_thread_func, NULL) != 0)
		return;

	x_run(x_context, xwin);

	// x loop done (window closed or emulation quit): make sure the 68k
	// core stops even if it never sees another VBL
	quit_program = true;
	pthread_join(emul_thread_id, NULL);
	emul_thread_id = 0;
}

/*
 *  Mode switching
 */

void XWIN_monitor_desc::switch_to_current_mode(void)
{
	const video_mode &mode = get_current_mode();
	uint32 width = mode.x, height = mode.y, bpr = mode.bytes_per_row;

	D(bug("switch_to_current_mode: %dx%d depth %d\n", width, height, (int)mode.depth));

	pthread_mutex_lock(&frame_lock);

	free(the_buffer);
	the_buffer_size = (height + 2) * bpr;
	// memory_init maps (MacFrameSize >> 16) + 1 64K banks, so back the
	// whole mapped range; edge writes must not run past the allocation
	the_buffer = (uint8 *)calloc(1, ((the_buffer_size >> 16) + 1) << 16);
	if (the_buffer == NULL) {
		pthread_mutex_unlock(&frame_lock);
		ErrorAlert(STR_NO_MEM_ERR);
		QuitEmulator();
	}

	if (frame_graph != NULL)
		graph_free(frame_graph);
	frame_graph = graph_new(NULL, width, height);

	// UAE memory banking variables
	MacFrameBaseHost = the_buffer;
	MacFrameSize = the_buffer_size;
	MacFrameLayout = FLAYOUT_DIRECT;
	MacScreenWidth = width;
	MacScreenHeight = height;

	if (TwentyFourBitAddressing)
		set_mac_frame_base(MacFrameBaseMac24Bit);
	else
		set_mac_frame_base(MacFrameBaseMac);

	pthread_mutex_unlock(&frame_lock);

	InitFrameBufferMapping();

	// Resize the window to the new mode.  Deferred to the x loop (main
	// thread): xwin_resize_to() does blocking IPC to the x server and
	// must not run on the emulation thread.
	if (xwin != NULL) {
		resize_w = width;
		resize_h = height;
		resize_pending = true;
	}
	last_mac_x = last_mac_y = -1;

	printf("xwin: guest video mode %dx%d depth %d\n",
	       (int)width, (int)height, (int)mode.depth);
}

void XWIN_monitor_desc::set_palette(uint8 *pal, int num)
{
	pthread_mutex_lock(&frame_lock);
	for (int i = 0; i < 256; i++) {
		int c = (i < num) ? i : num - 1;
		frame_pal[i * 3 + 0] = pal[c * 3 + 0];
		frame_pal[i * 3 + 1] = pal[c * 3 + 1];
		frame_pal[i * 3 + 2] = pal[c * 3 + 2];
	}
	pthread_mutex_unlock(&frame_lock);
}

/*
 *  Keyboard: EwokOS key code -> ADB key code
 */

int xwin_key2adb(int key)
{
	// Letters and digits arrive as ASCII
	if (key >= 'A' && key <= 'Z')
		key = key - 'A' + 'a';

	switch (key) {
	case 'a': return 0x00; case 's': return 0x01; case 'd': return 0x02;
	case 'f': return 0x03; case 'h': return 0x04; case 'g': return 0x05;
	case 'z': return 0x06; case 'x': return 0x07; case 'c': return 0x08;
	case 'v': return 0x09; case 'b': return 0x0b; case 'q': return 0x0c;
	case 'w': return 0x0d; case 'e': return 0x0e; case 'r': return 0x0f;
	case 'y': return 0x10; case 't': return 0x11; case '1': return 0x12;
	case '2': return 0x13; case '3': return 0x14; case '4': return 0x15;
	case '6': return 0x16; case '5': return 0x17; case '=': return 0x18;
	case '9': return 0x19; case '7': return 0x1a; case '-': return 0x1b;
	case '8': return 0x1c; case '0': return 0x1d; case ']': return 0x1e;
	case 'o': return 0x1f; case 'u': return 0x20; case '[': return 0x21;
	case 'i': return 0x22; case 'p': return 0x23; case 'l': return 0x25;
	case 'j': return 0x26; case '\'': return 0x27; case 'k': return 0x28;
	case ';': return 0x29; case '\\': return 0x2a; case ',': return 0x2b;
	case '/': return 0x2c; case 'n': return 0x2d; case 'm': return 0x2e;
	case '.': return 0x2f;
	case KEY_TAB: return 0x30;
	case KEY_SPACE: return 0x31;
	case '`': return 0x32;
	case KEY_BACKSPACE: return 0x33;
	case KEY_ENTER: return 0x24;
	case KEY_ESC: return 0x35;
	case KEY_SHIFT: case KEY_RSHIFT: return 0x38;
	case KEY_CAPSLOCK: return 0x39;
	case KEY_ALT: case KEY_RALT: return 0x3a;
	case KEY_CTRL: case KEY_RCTRL: return 0x3b;
	case KEY_LEFT: return 0x7b; case KEY_RIGHT: return 0x7c;
	case KEY_DOWN: return 0x7d; case KEY_UP: return 0x7e;
	case KEY_PAGEUP: return 0x74; case KEY_PAGEDOWN: return 0x79;
	case KEY_HOME: return 0x73; case KEY_END: return 0x77;
	case KEY_INSERT: return 0x72;
	case KEY_F1: return 0x7a; case KEY_F2: return 0x78; case KEY_F3: return 0x63;
	case KEY_F4: return 0x76; case KEY_F5: return 0x60; case KEY_F6: return 0x61;
	case KEY_F7: return 0x62; case KEY_F8: return 0x64; case KEY_F9: return 0x65;
	case KEY_F10: return 0x6d; case KEY_F11: return 0x67; case KEY_F12: return 0x6f;
	}
	return -1;
}

/*
 *  Initialization
 */

// Add mode to list of supported modes
static void add_mode(vector<video_mode> &modes, int width, int height, int resolution_id, video_depth depth)
{
	video_mode mode;
	mode.x = width;
	mode.y = height;
	mode.resolution_id = resolution_id;
	mode.depth = depth;
	mode.bytes_per_row = TrivialBytesPerRow(width, depth);
	if (depth == VDEPTH_1BIT)
		mode.bytes_per_row = (mode.bytes_per_row + 3) & ~3;
	mode.user_data = 0;
	modes.push_back(mode);
}

bool VideoInit(bool classic)
{
	// Get screen mode from preferences ("win/W/H[/D]")
	const char *mode_str = PrefsFindString("screen");
	if (classic) {
		display_width = 512;
		display_height = 342;
		display_depth = 1;
	}
	if (mode_str) {
		int w, h, d;
		if (sscanf(mode_str, "win/%d/%d/%d", &w, &h, &d) == 3) {
			display_width = w; display_height = h; display_depth = d;
		} else if (sscanf(mode_str, "win/%d/%d", &w, &h) == 2) {
			display_width = w; display_height = h;
		}
	}
	if (classic)
		display_depth = 1;
	if (display_width < 320) display_width = 320;
	if (display_width & 7) display_width &= ~7;

	video_depth default_depth;
	switch (display_depth) {
	case 1: default_depth = VDEPTH_1BIT; break;
	case 15: case 16: default_depth = VDEPTH_16BIT; break;
	case 24: case 32: default_depth = VDEPTH_32BIT; break;
	default: default_depth = VDEPTH_8BIT; break;
	}

	// One resolution, all depths (lowest depth must come first)
	vector<video_mode> modes;
	add_mode(modes, display_width, display_height, 0x80, VDEPTH_1BIT);
	add_mode(modes, display_width, display_height, 0x80, VDEPTH_8BIT);
	add_mode(modes, display_width, display_height, 0x80, VDEPTH_16BIT);
	add_mode(modes, display_width, display_height, 0x80, VDEPTH_32BIT);

	XWIN_monitor_desc *monitor = new XWIN_monitor_desc(modes, default_depth, 0x80);
	VideoMonitors.push_back(monitor);

	// Default palette: gray ramp
	for (int i = 0; i < 256; i++)
		frame_pal[i * 3 + 0] = frame_pal[i * 3 + 1] = frame_pal[i * 3 + 2] = i;

	// Open the xwin context and window
	x_context = (x_t *)malloc(sizeof(x_t));
	if (x_context == NULL)
		return false;
	memset(x_context, 0, sizeof(x_t));
	x_init(x_context, NULL);
	x_context->on_loop = xwin_loop;

	xwin = xwin_open(x_context, -1, 32, 32, display_width, display_height, "Basilisk II", 0);
	if (xwin == NULL) {
		printf("ERROR: cannot open xwin window\n");
		return false;
	}
	xwin->on_resize = on_xwin_resize;
	xwin->on_event = on_xwin_event;
	xwin->on_repaint = on_xwin_repaint;
	xwin_hide_cursor(xwin, true);
	xwin_set_visible(xwin, true);

	printf("Using EwokOS xwin video output (%dx%d)\n", display_width, display_height);
	return true;
}

void VideoExit(void)
{
	// x_run() has already returned on the main thread by the time we
	// get here (see VideoRunLoop); just tear the window down
	if (x_context != NULL)
		x_terminate(x_context);
	if (xwin != NULL) {
		xwin_close(xwin);
		xwin = NULL;
	}
	pthread_mutex_lock(&frame_lock);
	if (frame_graph != NULL) {
		graph_free(frame_graph);
		frame_graph = NULL;
	}
	free(the_buffer);
	the_buffer = NULL;
	pthread_mutex_unlock(&frame_lock);
}

/*
 *  VBL: convert the guest frame and ask the window to repaint
 *  (called from the 68k emulation thread)
 */

void VideoInterrupt(void)
{
	if (emerg_quit) {
		// Window close requested: stop the 68k core cleanly; the main
		// thread's x loop then exits and does the full teardown.
		// Keep asserting: nested Execute68k() calls clear quit_program
		quit_program = true;
	}

	if (!vbl_once) {
		vbl_once = true;
		printf("xwin: MacOS started, VBL running\n");
	}

	pthread_mutex_lock(&frame_lock);
	if (MacFrameBaseHost != NULL) {	// frame buffer allocated yet?
		convert_frame();
		frame_pending = true;
	}
	pthread_mutex_unlock(&frame_lock);
}

// Refreshed (non-VOSF) mode update: same as VBL for us
void VideoRefresh(void)
{
	VideoInterrupt();
}

void VideoQuitFullScreen(void)
{
}
