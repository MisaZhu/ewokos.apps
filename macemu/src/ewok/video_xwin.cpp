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
#include <ewoksys/ipc.h>
#include <ewoksys/vdevice.h>
#include <font/font.h>

extern "C" {
#include <graph/rgb15.h>
#include <graph/rgb24.h>
void rgb15be_2_argb(uint32_t *out, const uint8_t *in, int bpr, int w, int h);
void rgb24be_2_argb(uint32_t *out, const uint8_t *in, int bpr, int w, int h);
}

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

// adb.cpp: keep the button-down throttle window open while the guest is
// actively redrawing (dirty-row scan hits), so redraw bursts never get
// napped mid-flight
extern void adb_guest_drew(void);

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

// The 68k thread writes the frame buffer through the UAE memory bank
// WITHOUT holding frame_lock: the bank's lput/wput just does
// *(type *)(FrameBaseDiff + mac_addr) = val, where FrameBaseDiff was
// computed from MacFrameBaseHost (= the_buffer) at mode-switch time.
// If switch_to_current_mode() freed the old the_buffer under the lock
// while the 68k thread was mid-access (preempted between reading
// FrameBaseDiff and storing through it), the store would land in
// freed (possibly unmapped) memory — a data abort at a wild address.
// Defer the free by one mode-switch cycle, exactly like the existing
// stale_frame_graph pattern: the old buffer stays valid until the
// next mode switch (or VideoExit), by which time the 68k thread has
// picked up the new FrameBaseDiff and moved on.
static uint8 *stale_buffer = NULL;
static uint8 *stale_shadow = NULL;

// Converted ARGB8888 frame, blitted to the window on repaint.
// frame_graph is never freed at mode-switch time: the x thread's
// repaint blits from it WITHOUT holding frame_lock (a stale buffer
// only costs one torn frame after a switch), so the old buffer is
// retired one mode-switch cycle later via stale_frame_graph.
static graph_t *frame_graph = NULL;
static graph_t *stale_frame_graph = NULL;
static uint8 frame_pal[256 * 3];      // Current palette (RGB)
static uint32_t frame_pal_argb[256];  // Pre-built ARGB lookup (avoids per-pixel shifts)

static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool frame_pending = false;
static volatile bool emerg_quit = false;
static pthread_t x_thread_id = 0;            // thread running the x event loop (main thread)

// Window letterboxing (set by on_repaint, used for mouse mapping)
static float view_scale = 1.0f;
static int view_offset_x = 0;
static int view_offset_y = 0;
static x_t *x_context = NULL;
static xwin_t *xwin = NULL;
static pthread_t emul_thread_id = 0;

// Dedicated present thread: xwin_repaint() ends in a synchronous UPDATE
// fcntl IPC to xserverd.  Running that round-trip on the x event thread
// stalls x_run() for its whole duration, so a continuous input stream
// (touch drag) queues up behind it and cursor latency grows without
// bound.  A separate thread absorbs the IPC stall and keeps the event
// thread free to feed ADB.
static pthread_t present_thread_id = 0;
static volatile bool present_thread_active = false;

// Mode-switch resize request (emulation thread -> x thread).  All xwin
// API calls must happen on the x thread: the xwin functions do IPC to
// the x server, and issuing them from the emulation thread while the
// x thread is inside its own xwin IPC deadlocks the boot (black screen).
static volatile bool resize_pending = false;
static int resize_w = 0, resize_h = 0;
static bool presented_once = false;
static bool vbl_once = false;
static uint64_t last_input_ms = 0;	// x thread only: last input event time
static uint64_t last_present_ms = 0;	// present thread only: last guest-frame present
#define PRESENT_INTERVAL_MS 5	// Floor only: guest frames arrive at VBL rate anyway
static uint64_t last_scan_ms = 0;		// x thread only: last dirty-row scan
#define SCAN_INTERVAL_MS 4	// Faster dirty-row pickup during input (cursor tracking)

// Touch panels stream MOVE events at panel rate, far faster than the
// guest consumes the absolute position (its cursor machinery runs at the
// 60Hz VBL), and each injection costs an ADB dispatch plus a guest
// cursor redraw.  Coalesce minivmac-style (its queue merges consecutive
// position events): keep only the latest position and inject it at most
// once per guest frame.  pending_*/move_pending are set by the x thread
// and consumed by its paced flush or by the 68k thread's VBL-synchronous
// push (VideoInterrupt); injected_*/last_move_flush_ms are a dedup hint
// only, so a benign race on them costs at most one extra flush.
static bool move_pending = false;
static int pending_mac_x = 0, pending_mac_y = 0;
static int injected_mac_x = -1, injected_mac_y = -1;
static uint64_t last_move_flush_ms = 0;
#define MOVE_FLUSH_INTERVAL_MS 16	// Fallback cadence, see below
/*
 * Position flush paths.  The guest cursor machinery (CrsrNew -> cursor
 * task) absorbs exactly ONE position per VBL, so the freshness that
 * matters is how current the position is WHEN THE VBL RUNS, not how
 * often it is injected:
 *
 *  1. Primary: VideoInterrupt() pushes the latest coalesced position
 *     via ADBMouseVBLFlush() just before the guest's VBL tasks run, so
 *     the cursor task absorbs it in that very VBL (see there).  One
 *     push per VBL, zero added interrupts, no flooding.
 *  2. Fallback (this paced flush): pre-boot there is no guest cursor
 *     machinery to absorb a VBL push, and button edges always flush
 *     immediately so their position snapshot is exact.
 *
 * The old 5ms cadence flooded the emulation thread with 200Hz ADB
 * interrupts: InterruptFlags was permanently pending, idle_wait()'s
 * "InterruptFlags != 0" check never let it nap, and a stationary
 * long-press or drag pinned a whole core spinning the guest idle loop.
 */

static void flush_pending_move(void)
{
	if (!move_pending)
		return;
	move_pending = false;
	if (pending_mac_x == injected_mac_x && pending_mac_y == injected_mac_y)
		return;
	injected_mac_x = pending_mac_x;
	injected_mac_y = pending_mac_y;
	last_move_flush_ms = kernel_tic_ms(0);
	ADBMouseMoved(pending_mac_x, pending_mac_y);
}

// Dirty-row tracking: the guest draws straight into the_buffer with no
// host-side hook, so every VBL compares it against a shadow copy and only
// reconverts the rows that actually changed.  A static frame then costs one
// memcmp pass instead of a full-screen palette expansion, and the repaint
// plus UPDATE IPC below is skipped entirely.
static uint8 *the_shadow = NULL;
#define DIRTY_NONE_TOP   0x7fffffff
#define DIRTY_NONE_BOT   (-1)
static volatile int dirty_top = DIRTY_NONE_TOP;	// rows pending present (frame_lock)
static volatile int dirty_bottom = DIRTY_NONE_BOT;
static volatile bool force_full = true;			// palette/mode change: reconvert all rows
static int present_top = DIRTY_NONE_TOP;		// snapshot of dirty_top, present thread only
static int present_bottom = DIRTY_NONE_BOT;
static bool incremental_repaint = false;		// present thread only: present just the dirty rows
static volatile bool force_full_repaint = false;	// window shm rebuilt: next paint must cover the whole window

// Upscale cache (scale != 1), refilled by graph_scale_tof_fast() —
// the system graph library's fast scaler, the same one minivmac
// uses.  Reallocated only when the target geometry changes.
// x thread only.
static graph_t *scaled = NULL;
static float last_scale = 0.0f;
static int last_gw = 0, last_gh = 0, last_ox = 0, last_oy = 0;

/*
 *  Pre-boot "copying disk" splash: while AssetsPrepareUserDisks()
 *  (prefs_unix.cpp) copies the shipped disk images into the user
 *  directory the window is already up but the guest is not running yet,
 *  so show a disk-copy icon with a progress bar instead of the (still
 *  black) guest frame.
 */

static bool copy_splash = false;
static int copy_splash_done = 0;
static int copy_splash_total = 0;

#define SPLASH_BG      0xff151515
#define SPLASH_FG      0xffb8b8b8
#define SPLASH_SHUTTER 0xff5a5a5a
#define SPLASH_LABEL   0xfff2f2f2
#define SPLASH_TRACK   0xff3c3c3c

static void draw_floppy(graph_t *g, int x, int y, int s)
{
	// Body
	graph_fill_round(g, x, y, s, s, s/10, SPLASH_FG);
	// Metal shutter with its slider slot
	int sw = s*3/7, sh = s*2/7;
	int sx = x + (s - sw)/2 + s/12;
	graph_fill_rect(g, sx, y, sw, sh, SPLASH_SHUTTER);
	graph_fill_rect(g, sx + sw/6, y + sh/8, sw/5, sh*3/4, SPLASH_BG);
	// Label with two ruled lines
	int lw = s*5/7, lh = s*2/5;
	int lx = x + (s - lw)/2, ly = y + s*11/20;
	int lh1 = (lh/12 > 1) ? lh/12 : 1;
	graph_fill_rect(g, lx, ly, lw, lh, SPLASH_LABEL);
	graph_fill_rect(g, lx + lw/8, ly + lh/3, lw*3/4, lh1, SPLASH_SHUTTER);
	graph_fill_rect(g, lx + lw/8, ly + lh*2/3, lw*3/4, lh1, SPLASH_SHUTTER);
}

static void draw_zip_glyph(graph_t *g, int x, int y, int u,
	const uint8_t rows[5], uint32_t color)
{
	for (int r = 0; r < 5; r++) {
		for (int c = 0; c < 3; c++) {
			if (rows[r] & (4 >> c))
				graph_fill_rect(g, x + c*u, y + r*u, u, u, color);
		}
	}
}

static void draw_zip(graph_t *g, int x, int y, int s)
{
	// Page body with a folded top-right corner (1px rows: the graph lib
	// has no polygon fill). The dark flap grows while the bg cut shrinks
	// toward the corner, forming the dog-ear diagonal.
	graph_fill_round(g, x, y, s, s, s/10, SPLASH_FG);
	int f = s/3;
	for (int j = 0; j < f; j++) {
		graph_fill_rect(g, x + s - f, y + j, j, 1, SPLASH_SHUTTER);
		graph_fill_rect(g, x + s - f + j, y + j, f - j, 1, SPLASH_BG);
	}

	// "ZIP" label, 3x5 block glyphs
	static const uint8_t GL_Z[5] = {7, 1, 2, 4, 7};
	static const uint8_t GL_I[5] = {7, 2, 2, 2, 7};
	static const uint8_t GL_P[5] = {6, 5, 5, 6, 4};
	int u = (s/24 > 1) ? s/24 : 1;
	int tx = x + s/8, ty = y + s/8;
	draw_zip_glyph(g, tx, ty, u, GL_Z, SPLASH_SHUTTER);
	draw_zip_glyph(g, tx + 4*u, ty, u, GL_I, SPLASH_SHUTTER);
	draw_zip_glyph(g, tx + 8*u, ty, u, GL_P, SPLASH_SHUTTER);

	// Zipper: slider with punched hole, then tooth bars down the middle
	int cx = x + s/2;
	int sw = s/5, sh = s/5;
	int sy = y + s*2/5;
	graph_fill_round(g, cx - sw/2, sy, sw, sh, sw/3, SPLASH_SHUTTER);
	graph_fill_circle(g, cx, sy + sw/2, sw/4, SPLASH_FG);
	int bw = s/4, bh = s/16;
	if (bh < 2)
		bh = 2;
	int by = sy + sh + s/20;
	for (int i = 0; i < 3; i++) {
		graph_fill_round(g, cx - bw/2, by, bw, bh, bh/2, SPLASH_SHUTTER);
		by += bh + s/20;
	}
}

static void draw_copy_arrow(graph_t *g, int x, int cy, int s)
{
	int shaft_h = (s/8 > 2) ? s/8 : 2;
	int shaft_len = s/4;
	int head_len = s/5;
	int head_h = s/3;

	graph_fill_rect(g, x, cy - shaft_h/2, shaft_len, shaft_h, SPLASH_FG);
	// Triangle head built from 1px columns (no polygon fill in the graph lib)
	int ax = x + shaft_len;
	for (int i = 0; i < head_len; i++) {
		int hh = head_h * (head_len - i) / head_len;
		graph_fill_rect(g, ax + i, cy - hh/2, 1, hh, SPLASH_FG);
	}
}

static void draw_cd(graph_t *g, int x, int y, int s, uint32_t hole_color)
{
	int cx = x + s/2, cy = y + s/2;
	int r = s/2 - 1;
	if (r < 4)
		r = 4;
	// Disc with its inner ring and hub hole (transparent to the bg)
	graph_fill_circle(g, cx, cy, r, SPLASH_FG);
	graph_fill_circle(g, cx, cy, r/3, SPLASH_SHUTTER);
	graph_fill_circle(g, cx, cy, r/6, hole_color);
}

static void draw_hdd(graph_t *g, int x, int y, int s)
{
	// Wide slab centered in the icon box (the copy destination)
	int bw = s, bh = s/2;
	int by = y + (s - bh)/2;
	graph_fill_round(g, x, by, bw, bh, bh/6, SPLASH_FG);
	// Dark vent stripe along the lower half
	int sh = (bh/6 > 1) ? bh/6 : 1;
	graph_fill_rect(g, x + s/10, by + bh*2/3, bw - s/5, sh, SPLASH_SHUTTER);
	// Activity LED on the right
	int led = (bh/5 > 2) ? bh/5 : 2;
	graph_fill_rect(g, x + bw - s/10 - led, by + bh/6, led, led, SPLASH_LABEL);
}

static void draw_copy_splash(graph_t *g)
{
	int gw = g->w, gh = g->h;
	graph_fill_rect(g, 0, 0, gw, gh, SPLASH_BG);

	// Icon size follows the window, like the letterboxed guest frame does
	int s = ((gw < gh) ? gw : gh) / 6;
	if (s < 40) s = 40;
	if (s > 96) s = 96;

	int gap = s/4;
	int arrow_len = s/4 + s/5;	// shaft + head
	int total_w = s + gap + arrow_len + gap + s;
	int bar_h = (s/10 > 4) ? s/10 : 4;
	int block_h = s + s/5 + bar_h;

	int x0 = (gw - total_w)/2;
	int y0 = (gh - block_h)/2;
	int cy = y0 + s/2;

	// Shipped archive (.dsk.zip) unpacked onto the user's writable disk (hdd)
	draw_zip(g, x0, y0, s);
	draw_copy_arrow(g, x0 + s + gap, cy, s);
	draw_hdd(g, x0 + s + gap + arrow_len + gap, y0, s);

	// Byte progress across all pending disk images
	int by = y0 + s + s/5;
	graph_fill_round(g, x0, by, total_w, bar_h, bar_h/2, SPLASH_TRACK);
	if (copy_splash_total > 0) {
		int fw = (int)((long long)total_w * copy_splash_done / copy_splash_total);
		if (fw > bar_h)
			graph_fill_round(g, x0, by, fw, bar_h, bar_h/2, SPLASH_FG);
	}
}

/*
 *  Pre-boot startup-disk chooser: AssetsBootChoose() (prefs_unix.cpp)
 *  found two or more bootable disk images.  Arrow keys or taps move
 *  the selection, Enter / Start / a second tap confirms, Esc cancels.
 *  Drawn from on_xwin_repaint while boot_chooser is set, with the
 *  input loop polling the x server directly (VideoBootChooser below)
 *  because x_run() is not running yet at that point.
 */

static bool boot_chooser = false;
static int chooser_count = 0;
static int chooser_sel = 0;

#define CHOOSER_MAX   8
#define CHOOSER_PANEL 0xff242424
#define CHOOSER_SEL   0xff666868
#define CHOOSER_TEXT  0xffe8e8e8
#define CHOOSER_DIM   0xff8a8a8a
#define CHOOSER_DARK  0xff151515

// Row icon types: floppy = blank/data .dsk, hdd = bootable .dsk
// (a system disk), cd = raw installer image
#define ICON_FLOPPY 0
#define ICON_HDD    1
#define ICON_CD     2

static char chooser_labels[CHOOSER_MAX][64];
static int chooser_icon[CHOOSER_MAX];
static font_t *chooser_font = NULL;

typedef struct { int x, y, w, h; } chooser_rect_t;
static chooser_rect_t chooser_row_rect[CHOOSER_MAX];
static chooser_rect_t chooser_start_rect;

static bool chooser_rect_hit(const chooser_rect_t *r, int x, int y)
{
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void draw_boot_chooser(graph_t *g)
{
	int gw = g->w, gh = g->h;
	graph_fill_rect(g, 0, 0, gw, gh, SPLASH_BG);

	// Sizes follow the window, like the letterboxed guest frame does
	int fs = gh / 26;
	if (fs < 14) fs = 14;
	if (fs > 26) fs = 26;
	int row_h = fs * 5 / 2;
	int icon_s = fs * 3 / 2;
	int pad = fs;
	int panel_w = gw * 2 / 3;
	if (panel_w > 640) panel_w = 640;
	if (panel_w < 320) panel_w = (gw - 16 < 320) ? gw - 16 : 320;
	int btn_h = row_h - fs / 3;
	int panel_h = pad + fs * 2 + chooser_count * row_h +
		fs / 2 + btn_h + fs / 2 + fs + pad;
	int px = (gw - panel_w) / 2;
	int py = (gh - panel_h) / 2;
	if (py < 8) py = 8;
	graph_fill_round(g, px, py, panel_w, panel_h, fs / 2, CHOOSER_PANEL);

	if (chooser_font != NULL)
		graph_draw_text_font_align(g, px, py + pad, panel_w, fs * 2,
			"Startup Disk", chooser_font, fs + fs / 6, SPLASH_LABEL,
			FONT_ALIGN_CENTER);

	// One row per bootable volume; the selection is highlighted
	int ry = py + pad + fs * 2;
	for (int i = 0; i < chooser_count; i++) {
		chooser_rect_t *r = &chooser_row_rect[i];
		r->x = px + fs / 2;
		r->y = ry + i * row_h;
		r->w = panel_w - fs;
		r->h = row_h - fs / 3;
		bool sel = (i == chooser_sel);
		if (sel)
			graph_fill_round(g, r->x, r->y, r->w, r->h, fs / 3, CHOOSER_SEL);
		int ix = r->x + fs / 2, iy = r->y + (r->h - icon_s) / 2;
		switch (chooser_icon[i]) {
		case ICON_CD:
			draw_cd(g, ix, iy, icon_s, sel ? CHOOSER_SEL : CHOOSER_PANEL);
			break;
		case ICON_HDD:
			draw_hdd(g, ix, iy, icon_s);
			break;
		default:
			draw_floppy(g, ix, iy, icon_s);
			break;
		}
		if (chooser_font != NULL)
			graph_draw_text_font(g, r->x + fs / 2 + icon_s + fs / 2,
				r->y + (r->h - fs) / 2, chooser_labels[i], chooser_font,
				fs, sel ? CHOOSER_DARK : CHOOSER_TEXT);
	}

	// Start button confirms the highlighted volume
	chooser_rect_t *sr = &chooser_start_rect;
	sr->w = fs * 6;
	sr->h = btn_h;
	sr->x = px + (panel_w - sr->w) / 2;
	sr->y = ry + chooser_count * row_h + fs / 2;
	graph_fill_round(g, sr->x, sr->y, sr->w, sr->h, fs / 3, SPLASH_FG);
	if (chooser_font != NULL)
		graph_draw_text_font_align(g, sr->x, sr->y, sr->w, sr->h,
			"Start", chooser_font, fs, CHOOSER_DARK, FONT_ALIGN_CENTER);

	if (chooser_font != NULL)
		graph_draw_text_font_align(g, px, sr->y + sr->h, panel_w, fs * 3 / 2,
			"arrow keys + Enter, or tap", chooser_font, fs * 3 / 4,
			CHOOSER_DIM, FONT_ALIGN_CENTER);
}

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

static void convert_rows(const uint8 *src_fb, uint32 y0, uint32 y1)
{
	if (frame_graph == NULL || src_fb == NULL)
		return;

	const video_mode &mode = VideoMonitors[0]->get_current_mode();
	uint32 w = mode.x, h = mode.y, bpr = mode.bytes_per_row;
	if (y0 >= h)
		return;
	uint32 y_end = (y1 < h - 1) ? y1 : h - 1;
	uint32_t *dst = frame_graph->buffer + y0 * w;

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
		if (ppb == 1) {
			// 8-bit: direct ARGB table lookup, no per-pixel shifts
			for (uint32 y = y0; y <= y_end; y++) {
				const uint8 *src = src_fb + y * bpr;
				for (uint32 x = 0; x < w; x++)
					dst[x] = frame_pal_argb[src[x]];
				dst += w;
			}
		} else {
			for (uint32 y = y0; y <= y_end; y++) {
				const uint8 *src = src_fb + y * bpr;
				for (uint32 x = 0; x < w; x++) {
					int bitpos = 8 - ((x % ppb) + 1) * bpp;
					int idx = (src[x / ppb] >> bitpos) & mask;
					dst[x] = frame_pal_argb[idx];
				}
				dst += w;
			}
		}
		break;
	}
	case VDEPTH_16BIT:
		if (MacFrameLayout == FLAYOUT_HOST_555 && bpr == w * 2) {
			// FLAYOUT_HOST_555 (see switch_to_current_mode): guest stores
			// already landed as host-order XRGB1555 words, so expand with
			// plain uint16 loads — no per-byte big-endian reassembly.
			// (graph's argb_2_rgb15() is the opposite direction, ARGB->555,
			// and rotates 180 degrees for panel scan; unusable here.)
			// Use the graph library's rgb15->argb converter (NEON-backed
			// under ARCH_BOOST) on just the dirty row band.
			rgb15_2_argb(dst,
				(uint16_t *)(src_fb + y0 * bpr),
				w, y_end - y0 + 1);
		} else {
			// FLAYOUT_DIRECT (24-bit addressing): big-endian RGB555.
			// rgb15be_2_argb reads raw big-endian bytes and expands
			// each 5-bit channel to 8 bits with NEON acceleration
			// (vrev32 inside the NEON pipeline, no separate bswap).
			rgb15be_2_argb(dst, src_fb + y0 * bpr,
				bpr, w, y_end - y0 + 1);
		}
		break;
	case VDEPTH_32BIT:
		if (MacFrameLayout == FLAYOUT_HOST_888 && bpr == w * 4) {
			// FLAYOUT_HOST_888 (see switch_to_current_mode): the guest's
			// per-pixel long stores already landed in host xRGB8888 order
			// via frame_host_888_lput, so the dirty band is a plain row
			// copy — hand it to the graph library's rgb24->argb converter
			// (NEON-backed under ARCH_BOOST) on just the dirty row band.
			// It sets alpha to 0xFF, which is stricter than a raw graph_blt
			// (which leaves the guest's zero alpha byte untouched — still
			// harmless for xserverd's opaque composite, but now the alpha
			// is canonical).
			rgb24_2_argb(dst, (uint32_t *)(src_fb + y0 * bpr),
				w, y_end - y0 + 1);
		} else {
			// FLAYOUT_DIRECT (24-bit addressing): bytes [00][RR][GG][BB]
			// in memory.  rgb24be_2_argb reads raw big-endian bytes and
			// produces ARGB with NEON acceleration (vrev32 inside the
			// NEON pipeline, no separate bswap).
			rgb24be_2_argb(dst, src_fb + y0 * bpr,
				bpr, w, y_end - y0 + 1);
		}
		break;
	}
}

/*
 *  Scan the guest frame for changed rows, convert them and merge them
 *  into the pending present range.  Called from both the 68k thread
 *  (VBL) and the x thread (input-paced polling), so the whole scan
 *  runs under frame_lock: a concurrent switch_to_current_mode() frees
 *  and reallocates the_buffer under the same lock, and reading a stale
 *  pointer mid-scan must be impossible.
 *
 *  The guest draws into the_buffer without ever taking the lock, so
 *  the compare/convert may race with a row being drawn.  To stay
 *  coherent the changed rows are latched into the_shadow FIRST and
 *  the conversion reads the_shadow, never the_buffer: frame_graph
 *  then always matches the shadow exactly, and any guest write that
 *  lands after the latch makes the row differ from the shadow again,
 *  so the next scan reconverts it — self-healing by construction.
 *  (Converting from the_buffer and copying the shadow afterwards was
 *  not coherent: a guest write between the two was latched into the
 *  shadow but never converted, leaving a permanently stale row —
 *  the visible "redraw ghost" — until the row changed again.)
 *  Returns true when at least one changed row was found (and latched).
 */
static bool scan_convert_rows(void)
{
	pthread_mutex_lock(&frame_lock);
	if (MacFrameBaseHost == NULL || the_buffer == NULL || frame_graph == NULL) {
		pthread_mutex_unlock(&frame_lock);
		return false;
	}

	const video_mode &mode = VideoMonitors[0]->get_current_mode();
	uint32 h = mode.y, bpr = mode.bytes_per_row;
	uint32 top = 0, bottom = h - 1;
	// No shadow (alloc failed): degrade to a full-frame convert, exactly
	// the old behaviour
	if (!force_full && the_shadow != NULL) {
		top = h;
		bottom = 0;
		for (uint32 y = 0; y < h; y++) {
			if (memcmp(the_buffer + y * bpr, the_shadow + y * bpr, bpr) != 0) {
				if (y < top) top = y;
				bottom = y;
			}
		}
	}
	force_full = false;

	bool changed = (bottom >= top);
	if (changed) {
		// Latch the changed rows into the shadow first, then convert
		// from the shadow (see the comment above): frame_graph and
		// the_shadow are one coherent snapshot, and a guest write
		// racing the latch simply re-dirties the row on the next scan.
		const uint8 *src_fb = the_buffer;
		if (the_shadow != NULL) {
			for (uint32 y = top; y <= bottom; y++)
				memcpy(the_shadow + y * bpr, the_buffer + y * bpr, bpr);
			src_fb = the_shadow;
		}
		convert_rows(src_fb, top, bottom);
		if (frame_pending) {	// previous range not presented yet: merge
			if ((int)top < dirty_top) dirty_top = (int)top;
			if ((int)bottom > dirty_bottom) dirty_bottom = (int)bottom;
		} else {
			dirty_top = (int)top;
			dirty_bottom = (int)bottom;
			frame_pending = true;
		}
	}
	pthread_mutex_unlock(&frame_lock);
	return changed;
}

/*
 *  xwin callbacks
 */

static void xwin_service(void);

static void on_xwin_resize(xwin_t *win)
{
	(void)win;
	// The window's shm buffer may have been rebuilt (fresh, empty): the
	// next repaint must cover the whole window, not just the dirty band.
	// Letterbox geometry itself is recomputed inside on_xwin_repaint().
	force_full_repaint = true;
}

static void on_xwin_event(xwin_t *win, xevent_t *ev)
{
	extern int xwin_key2adb(int key);

	switch (ev->type) {
	case XEVT_IM: {
		last_input_ms = kernel_tic_ms(0);
		int code = xwin_key2adb(ev->value.im.key_code);
		if (code >= 0) {
			if (ev->state == XIM_STATE_PRESS)
				ADBKeyDown(code);
			else
				ADBKeyUp(code);
		}
		// Service scan+present from the event path too: x_run only
		// calls xwin_loop when the event queue is EMPTY, so a
		// continuous input stream would starve the screen updates
		xwin_service();
		break;
	}
	case XEVT_MOUSE: {
		last_input_ms = kernel_tic_ms(0);
		// Absolute position sync, minivmac-style: set the Mac cursor to
		// wherever the host cursor points inside our window.  Relative
		// deltas would slowly drift away from the host cursor (events are
		// not delivered while the host cursor is outside the window, and
		// the ADB packet encodes deltas in 7 bits), while the guest cursor
		// is the only one visible here (the host cursor is hidden), so it
		// must always match the real mouse position.  ADBMouseMoved()
		// takes absolute Mac coordinates in this mode (see VideoInit).
		gpos_t pos = xwin_get_inside_pos(win, ev->value.mouse.x, ev->value.mouse.y);
		int mac_x = (int)((pos.x - view_offset_x) / view_scale);
		int mac_y = (int)((pos.y - view_offset_y) / view_scale);
		// minivmac semantics: only notify positions that land inside the
		// Mac screen.  Clamping letterbox/margin touches to the screen
		// edge made the guest cursor crawl along the border while the
		// finger was really over dead space, wrecking drags.
		if (mac_x >= 0 && mac_x < (int)MacScreenWidth &&
			mac_y >= 0 && mac_y < (int)MacScreenHeight) {
			pending_mac_x = mac_x;
			pending_mac_y = mac_y;
			move_pending = true;
			// A queued button edge snapshots the ADB position slot, so
			// the freshest spot must reach it before DOWN/UP; plain
			// moves go through the coalescing flush in xwin_loop.
			if (ev->state == MOUSE_STATE_DOWN || ev->state == MOUSE_STATE_UP)
				flush_pending_move();
		}
		if (ev->state == MOUSE_STATE_DOWN) {
			ADBMouseDown(0);
		} else if (ev->state == MOUSE_STATE_UP) {
			ADBMouseUp(0);
		}
		// Service scan+present from the event path too: x_run only
		// calls xwin_loop when the event queue is EMPTY, so a
		// continuous input stream (touch drag) would starve the
		// screen updates for the whole duration of the stream
		xwin_service();
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
	(void)win;
	if (g == NULL)
		return;

	// Startup-disk chooser replaces the guest frame while active
	if (boot_chooser) {
		draw_boot_chooser(g);
		return;
	}

	// Pre-boot disk-copy splash replaces the (still empty) guest frame
	if (copy_splash) {
		draw_copy_splash(g);
		return;
	}

	// Incremental (dirty-band) painting is only ever valid on the present
	// thread.  Expose / resize / maximize repaints arrive on the x event
	// thread and MUST redraw the whole frame, and the incremental flag the
	// present thread raises just before xwin_repaint() must not leak into
	// an expose that wins the painting_lock first (it would redraw only the
	// dirty band and leave the rest of the window stale/incomplete).  Gate
	// on actually being the present thread.  force_full_repaint covers a
	// rebuilt (empty) window shm buffer.
	bool from_present_thread = (present_thread_id != 0) &&
		pthread_equal(pthread_self(), present_thread_id) != 0;
	bool incremental = from_present_thread && incremental_repaint && !force_full_repaint;
	if (from_present_thread)
		incremental_repaint = false;
	force_full_repaint = false;

	// NO frame_lock here: the scale+blit below is the longest frame-path
	// critical section, and this thread can be preempted mid-blit
	// (per-core pinned scheduling); holding frame_lock would stall the
	// 68k thread's per-VBL scan_convert_rows() behind it and freeze the
	// guest's VBL pipeline.  Everything touched here is either
	// present-thread local (present_*, scaled) or safe by construction:
	// view_scale/view_offset are also read by the x thread's mouse
	// mapping but only change on a window resize, so a concurrent read
	// is benign.  frame_graph is swapped atomically at mode switch and
	// its old
	// buffer is freed one cycle later (see switch_to_current_mode), so
	// a concurrently swapped pointer can only cost one torn frame.
	graph_t *fg = frame_graph;
	if (fg != NULL && fg->buffer != NULL) {
		int fw = fg->w, fh = fg->h;
		float scale_x = (float)g->w / fw;
		float scale_y = (float)g->h / fh;
		float scale = (scale_x < scale_y) ? scale_x : scale_y;
		if (scale < 0.5f) scale = 0.5f;

		int scaled_w = (int)(fw * scale);
		int scaled_h = (int)(fh * scale);
		int offset_x = (g->w - scaled_w) / 2;
		int offset_y = (g->h - scaled_h) / 2;

		view_scale = scale;
		view_offset_x = offset_x;
		view_offset_y = offset_y;

		bool layout_changed =
			last_gw != g->w || last_gh != g->h || last_scale != scale ||
			last_ox != offset_x || last_oy != offset_y;
		if (layout_changed) {
			// Window geometry changed: clear the letterbox once and
			// redraw everything below.
			graph_fill_rect(g, 0, 0, g->w, g->h, 0xff000000);
			last_gw = g->w; last_gh = g->h;
			last_scale = scale; last_ox = offset_x; last_oy = offset_y;
		}

		int top, bottom;
		if (incremental && !layout_changed) {
			top = present_top;
			bottom = present_bottom;
		} else {
			top = 0;
			bottom = fh - 1;
		}
		present_top = DIRTY_NONE_TOP;
		present_bottom = DIRTY_NONE_BOT;

		if (top >= 0 && top < fh && bottom >= top) {
			if (bottom > fh - 1) bottom = fh - 1;

			if (scale != 1.0f) {
				// Scaled present: scale the dirty row band only (with a
				// 1-row bilinear margin) and blit just the scaled band to
				// the window.  For large dirty regions or full repaints,
				// fall back to the whole-frame scale.
				if (scaled == NULL || scaled->w != scaled_w || scaled->h != scaled_h) {
					graph_t *tmp = graph_new(NULL, scaled_w, scaled_h);
					if (scaled != NULL)
						graph_free(scaled);
					scaled = tmp;
				}
				if (scaled != NULL) {
					int band_h = bottom - top + 1;
					bool partial = incremental && !layout_changed &&
						band_h < fh / 4;

					if (partial) {
						// Expand by 1 row for bilinear interpolation margin
						int src_y0 = (top > 0) ? top - 1 : 0;
						int src_y1 = (bottom < fh - 1) ? bottom + 1 : fh - 1;
						int src_rows = src_y1 - src_y0 + 1;
						// Map to scaled coordinates using the same floor
						// mapping as the full-frame scale
						int dst_y0 = (int)(src_y0 * scale);
						int dst_y1 = (int)((src_y1 + 1) * scale) - 1;
						if (dst_y1 >= scaled_h) dst_y1 = scaled_h - 1;
						int dst_rows = dst_y1 - dst_y0 + 1;
						if (dst_rows > 0 && src_rows > 0) {
							graph_t src_band, dst_band;
							memset(&src_band, 0, sizeof(src_band));
							memset(&dst_band, 0, sizeof(dst_band));
							src_band.buffer = fg->buffer + src_y0 * fw;
							src_band.w = fw;
							src_band.h = src_rows;
							dst_band.buffer = scaled->buffer + dst_y0 * scaled_w;
							dst_band.w = scaled_w;
							dst_band.h = dst_rows;
							graph_scale_tof_fast(&src_band, &dst_band, scale);
							graph_blt(scaled, 0, dst_y0, scaled_w, dst_rows,
								g, offset_x, offset_y + dst_y0,
								scaled_w, dst_rows);
						}
					} else {
						graph_scale_tof_fast(fg, scaled, scale);
						// Blit only the scaled dirty band (saves full-frame copy)
						int dst_y0 = (int)(top * scale);
						int dst_y1 = (int)((bottom + 1) * scale) - 1;
						if (dst_y1 >= scaled_h) dst_y1 = scaled_h - 1;
						if (dst_y0 < 0) dst_y0 = 0;
						int dst_band = dst_y1 - dst_y0 + 1;
						if (dst_band > 0)
							graph_blt(scaled, 0, dst_y0, scaled_w, dst_band,
								g, offset_x, offset_y + dst_y0,
								scaled_w, dst_band);
					}
				} else {
					// Cache allocation failed: let graph_blt() scale directly
					graph_blt(fg, 0, 0, fw, fh,
						g, offset_x, offset_y, scaled_w, scaled_h);
				}
			} else {
				graph_blt(fg, 0, top, fw, bottom - top + 1,
					g, offset_x, offset_y + top, fw, bottom - top + 1);
			}
		}
	}
}

/*
 *  Paced frame service: apply a pending mode-switch resize, flush the
 *  coalesced mouse position, scan for freshly drawn guest rows and
 *  present.  Runs from xwin_loop at the idle cadence AND from
 *  on_xwin_event while input flows: x_run only calls the on_loop
 *  callback when the x event queue is EMPTY, so under a continuous
 *  input stream (touch drag) xwin_loop never runs and without this the
 *  screen would not update for the whole duration of the stream.
 *  Every step here is internally paced (MOVE_FLUSH/SCAN/PRESENT
 *  intervals), so the extra calls cost almost nothing.
 */
static void xwin_service(void)
{
	uint64_t tik = kernel_tic_ms(0);

	// Apply a pending mode-switch resize (deferred here so it runs on
	// the x thread, see switch_to_current_mode)
	if (resize_pending && xwin != NULL) {
		resize_pending = false;
		xwin_resize_to(xwin, resize_w, resize_h);
		xwin_hide_cursor(xwin, true);
	}

	// Deliver the latest coalesced mouse position at guest-VBL cadence
	if (move_pending &&
		kernel_tic_ms(0) - last_move_flush_ms >= MOVE_FLUSH_INTERVAL_MS)
		flush_pending_move();

	// Pick up freshly drawn guest rows (a moving cursor above all)
	// without waiting for the next 60Hz VBL.  Only while input is
	// flowing: at idle the per-VBL scan from VideoInterrupt() already
	// covers all guest drawing one frame later, and every scan is a
	// frame_lock section plus a full-frame compare — running it at
	// the idle 60Hz cadence was the dominant idle CPU cost here.
	if (last_input_ms != 0 && tik - last_input_ms < 100 &&
		tik - last_scan_ms >= SCAN_INTERVAL_MS) {
		last_scan_ms = tik;
		if (scan_convert_rows())
			adb_guest_drew();	// guest redrawing: extend the throttle window
	}

	// Presenting runs on present_thread_func: the synchronous UPDATE IPC
	// in xwin_repaint() must not stall this (x event) thread, or a
	// continuous input stream queues up behind it and the cursor lags.
}

static void xwin_loop(void *p)
{
	(void)p;

	if (emerg_quit) {
		x_terminate(x_context);
		return;
	}

	// The 60Hz guest heartbeat runs on its own tick thread
	// (main_unix.cpp): it must not depend on this callback, which
	// x_run skips whenever the x event queue is non-empty

	uint64_t tik = kernel_tic_ms(0);
	xwin_service();

	uint64_t now = kernel_tic_ms(0);
	uint32_t gap = (uint32_t)(now - tik);
	// 5ms cadence while input is flowing (dragging): the mouse -> ADB ->
	// guest draw -> convert -> present chain then settles within ~1 frame
	// instead of stacking up to five.  Idle: back to the 60Hz VBL pace.
	uint32_t budget =
		(last_input_ms != 0 && now - last_input_ms < 100) ? 5 : 1000 / 60;
	if (gap < budget)
		proc_usleep((budget - gap) * 1000);
}

static void *present_thread_func(void *arg)
{
	(void)arg;
	while (present_thread_active) {
		uint64_t tik = kernel_tic_ms(0);

		bool do_repaint = false;
		if (tik - last_present_ms >= PRESENT_INTERVAL_MS) {
			pthread_mutex_lock(&frame_lock);
			if (frame_pending) {
				frame_pending = false;
				present_top = dirty_top;
				present_bottom = dirty_bottom;
				dirty_top = DIRTY_NONE_TOP;
				dirty_bottom = DIRTY_NONE_BOT;
				do_repaint = true;
			}
			pthread_mutex_unlock(&frame_lock);
		}

		incremental_repaint = false;
		if (do_repaint && xwin != NULL) {
			if (!presented_once) {
				presented_once = true;
				printf("xwin: presenting guest frames\n");
			}
			incremental_repaint = true;
			xwin_repaint(xwin);
			last_present_ms = kernel_tic_ms(0);
		}

		proc_usleep(2000);
	}
	return NULL;
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
 *  QuitEmulator() invoked off the x thread (guest Shut Down via
 *  M68K_EMUL_OP_SHUTDOWN, fatal emulation errors): the x thread owns
 *  every xwin object and may be blocked inside an xwin IPC right now
 *  (e.g. xwin_repaint()'s UPDATE fcntl), so tearing the window down
 *  here races it — xwin_close() freed xinfo under an in-flight repaint
 *  and the main thread died with a data abort at NULL+0x1c.  Wake the
 *  x loop and end this thread in place instead: the caller sits deep
 *  inside the 68k interpreter, whose inner loop (m68k_do_execute)
 *  never checks quit_program — with guest interrupts masked during
 *  shutdown a cooperative stop never happens and the session just
 *  black-screens.  pthread_exit() unwinds nothing, which is fine: no
 *  quit call site holds a lock, and the x thread's pthread_join()
 *  plus re-entered QuitEmulator() do the real teardown.
 *  Returns true when the quit was deferred (never returns in
 *  practice: the thread exits first).
 */
bool VideoDeferQuitToXThread(void)
{
	// No x loop running (early init failures quit in place), or already
	// on the x thread: tear down directly
	if (x_thread_id == 0 || pthread_equal(pthread_self(), x_thread_id))
		return false;
	x_terminate(x_context);	// wake the x loop now
	pthread_exit(NULL);		// VideoRunLoop's pthread_join then succeeds
	return true;			// not reached
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

	x_thread_id = pthread_self();

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

	// Free the buffer from TWO cycles ago (the one-cycle-stale buffer).
	// The current buffer becomes stale: the 68k thread may still hold
	// a cached FrameBaseDiff derived from it and write through it for
	// a few more instructions.  Freeing it here would unmap the pages
	// and turn those writes into data aborts (the exact crash this
	// fixes: data abort at 0x3FF4FE8000, a former mmap'd buffer addr).
	free(stale_buffer);
	free(stale_shadow);
	stale_buffer = the_buffer;
	stale_shadow = the_shadow;
	the_shadow = NULL;
	the_buffer_size = (height + 2) * bpr;
	// memory_init maps (MacFrameSize >> 16) + 1 64K banks, so back the
	// whole mapped range; edge writes must not run past the allocation
	the_buffer = (uint8 *)calloc(1, ((the_buffer_size >> 16) + 1) << 16);
	if (the_buffer == NULL) {
		pthread_mutex_unlock(&frame_lock);
		ErrorAlert(STR_NO_MEM_ERR);
		QuitEmulator();
	}
	// Shadow copy for the dirty-row compare; NULL just falls back to a
	// full-frame convert every VBL
	the_shadow = (uint8 *)calloc(1, ((the_buffer_size >> 16) + 1) << 16);

	// Swap in the new frame graph under frame_lock (the scan path uses
	// it there), but defer the free: a lock-free repaint blit on the x
	// thread may still be reading the old buffer.
	if (stale_frame_graph != NULL)
		graph_free(stale_frame_graph);
	stale_frame_graph = frame_graph;
	frame_graph = graph_new(NULL, width, height);

	// UAE memory banking variables.  In 32-bit depth use the host-order
	// frame bank: frame_host_888_lput stores the guest's per-pixel longs
	// in host xRGB8888 order, so convert_rows() degrades to a plain
	// graph_blt row copy instead of a per-byte endian gather.  16-bit
	// depth likewise uses frame_host_555 (word stores land host-order)
	// so convert_rows() expands with uint16 loads.  Not available with
	// 24-bit addressing (memory_init always maps the big-endian frame24
	// bank there); other depths stay big-endian direct.
	// InitFrameBufferMapping() below remaps the banks.
	MacFrameBaseHost = the_buffer;
	MacFrameSize = the_buffer_size;
	if (mode.depth == VDEPTH_32BIT && !TwentyFourBitAddressing)
		MacFrameLayout = FLAYOUT_HOST_888;
	else if (mode.depth == VDEPTH_16BIT && !TwentyFourBitAddressing)
		MacFrameLayout = FLAYOUT_HOST_555;
	else
		MacFrameLayout = FLAYOUT_DIRECT;
	MacScreenWidth = width;
	MacScreenHeight = height;

	// New mode: everything must be reconverted and represented
	force_full = true;
	frame_pending = false;
	dirty_top = DIRTY_NONE_TOP;
	dirty_bottom = DIRTY_NONE_BOT;
	present_top = DIRTY_NONE_TOP;
	present_bottom = DIRTY_NONE_BOT;
	incremental_repaint = false;

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
		frame_pal_argb[i] = 0xff000000u |
			((uint32_t)pal[c * 3 + 0] << 16) |
			((uint32_t)pal[c * 3 + 1] << 8) |
			(uint32_t)pal[c * 3 + 2];
	}
	// Rows already converted used the old palette: reconvert them all
	force_full = true;
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
	// Physical USB keyboards deliver backspace as '\b' (CONSOLE_LEFT)
	// through hid_keybd's downMap; only the on-screen vkey sends
	// KEY_BACKSPACE.  Accept both like the rest of the system does.
	case KEY_BACKSPACE: case CONSOLE_LEFT: return 0x33;
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
	// Only the color depth comes from preferences ("win/W/H[/D]"); the
	// desktop size tracks the fullscreen window's workspace rect (read
	// below, before the window turns visible).
	bool hidpi = PrefsFindBool("hidpi");
	bool fullscreen = PrefsFindBool("fullscreen");
	const char *mode_str = PrefsFindString("screen");
	if (classic) {
		display_width = 512;
		display_height = 342;
		display_depth = 1;
	}
	if (mode_str) {
		int w, h, d;
		if (sscanf(mode_str, "win/%d/%d/%d", &w, &h, &d) == 3)
			display_depth = d;
		display_width = w;
		display_height = h;
	}
	if (classic)
		display_depth = 1;

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
	//xwin_hide_cursor(xwin, true);
	// This backend hides the host cursor and lets the guest draw its own,
	// so the guest cursor must track the real mouse position exactly:
	// keep the ADB mouse in absolute mode and feed ADBMouseMoved() the
	// Mac-screen coordinates of the host cursor (see on_xwin_event).
	ADBSetRelMouseMode(false);

	if (fullscreen)
		xwin_fullscreen(xwin);
	// xwin_fullscreen() waited on the x server, which recomputed the
	// workspace rect for the MAX state into the shared xinfo: that is
	// the display size the guest desktop runs at.  Classic keeps its
	// fixed 512x342.
	if (!classic && xwin->xinfo != NULL &&
			xwin->xinfo->wsr.w > 0 && xwin->xinfo->wsr.h > 0) {
		display_width = xwin->xinfo->wsr.w;
		display_height = xwin->xinfo->wsr.h;
	}

	if(hidpi && display_width >= 1280) {
		int dw = display_width * 2 / 3;
		int dh = display_height * 2 / 3;
		if(dw >= 480 &&	dh >= 320) {
			display_width = dw;
			display_height = dh;
		}
	}

	if (display_width < 480) display_width = 480;
	if (display_width & 7) display_width &= ~7;
	if (display_height < 320) display_height = 320;
	if (display_height & 7) display_height &= ~7;

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

	// Allocate the initial frame buffer and publish its base now: the
	// slot ROM (InstallSlotROM(), called later from PatchROM()) records
	// this base as minorBase, and memory_init() maps the frame24 banks
	// and computes FrameBaseDiff from MacFrameBaseHost/MacFrameSize.
	// Deferring this to the guest's first cscSetMode (as before) leaves
	// a window in which the slot ROM advertises a frame buffer at
	// address 0 and the guest's boot gray-screen fill wipes low memory
	// (vector table, globals, system heap start).  The SDL2 backends
	// publish the base from their monitor init the same way.
	monitor->switch_to_current_mode();

	// Default palette: gray ramp
	for (int i = 0; i < 256; i++) {
		frame_pal[i * 3 + 0] = frame_pal[i * 3 + 1] = frame_pal[i * 3 + 2] = i;
		frame_pal_argb[i] = 0xff000000u | ((uint32_t)i << 16) | ((uint32_t)i << 8) | (uint32_t)i;
	}

	xwin_set_visible(xwin, true);

	// Start the present thread: the blocking repaint IPC runs there,
	// off the x event loop (see present_thread_func)
	present_thread_active = true;
	pthread_create(&present_thread_id, NULL, present_thread_func, NULL);

	printf("Using EwokOS xwin video output (%dx%d)\n", display_width, display_height);
	return true;
}

void VideoExit(void)
{
	// Stop the present thread before tearing the window down, so it
	// can't call xwin_repaint() on a closed xwin
	present_thread_active = false;
	if (present_thread_id != 0) {
		pthread_join(present_thread_id, NULL);
		present_thread_id = 0;
	}

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
	if (stale_frame_graph != NULL) {
		graph_free(stale_frame_graph);
		stale_frame_graph = NULL;
	}
	free(stale_buffer);
	stale_buffer = NULL;
	free(stale_shadow);
	stale_shadow = NULL;
	free(the_buffer);
	the_buffer = NULL;
	free(the_shadow);
	the_shadow = NULL;
	if (scaled != NULL) {
		graph_free(scaled);
		scaled = NULL;
	}
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

	// Guest-VBL heartbeat for the ADB button-edge settle (adb.cpp):
	// advances the settle gate and keeps queued edges dispatching at
	// the guest's 60Hz cadence even when no further input arrives
	ADBVBLTick();

	// VBL-synchronous position push: write the latest host pointer into
	// the guest's cursor low memory NOW, before the guest's VBL tasks
	// run (the interrupt dispatch calls them right after VideoInterrupt),
	// so the position is as fresh as possible when the guest couples the
	// cursor.  Deliberately does NOT touch move_pending/injected_*/
	// last_move_flush_ms: the paced flush_pending_move() in xwin_service
	// must keep firing at its own 16ms cadence — it is what keeps the
	// INTFLAG_ADB dispatch rhythm alive during motion, and killing it
	// (dedup against a VBL-updated injected_*) measurably slowed the
	// guest's cursor coupling.  adb_push_position() is idempotent, so
	// the duplicate write from that path is a no-op.
	if (move_pending)
		ADBMouseVBLFlush(pending_mac_x, pending_mac_y);

	// Adaptive frame scan: while the guest keeps drawing, scan on every
	// VBL; after 12 change-free scans (~200ms) back off to a 5Hz scan —
	// the full-frame compare is the dominant idle cost on this thread.
	// Input-driven redraws are additionally covered by the x thread's
	// input-paced scan, so typing/mouse tracking stay at full rate.
	static uint32_t quiet_vbls = 0;
	if ((quiet_vbls < 12 || (quiet_vbls % 12) == 0) && scan_convert_rows()) {
		quiet_vbls = 0;
		adb_guest_drew();	// guest redrawing: extend the throttle window
	} else
		quiet_vbls++;
}

// Refreshed (non-VOSF) mode update: same as VBL for us
void VideoRefresh(void)
{
	VideoInterrupt();
}

void VideoQuitFullScreen(void)
{
}


/*
 *  Copy-progress splash for AssetsPrepareUserDisks() (prefs_unix.cpp):
 *  done/total bytes across all pending disk images, total <= 0 hides
 *  the splash again.  Runs on the main thread before x_run() starts,
 *  so xwin_repaint() pushes every update synchronously.
 */

void VideoDiskCopySplash(int done, int total)
{
	if (xwin == NULL)
		return;
	if (total <= 0) {
		copy_splash = false;
		copy_splash_done = 0;
		copy_splash_total = 0;
	} else {
		copy_splash = true;
		copy_splash_done = done;
		copy_splash_total = total;
	}
	xwin_repaint(xwin);
}


/*
 *  Startup-disk chooser for AssetsBootChoose() (prefs_unix.cpp): two
 *  or more bootable disk images were found and the user picks one.
 *  Runs on the main thread before x_run() starts (same phase as the
 *  copy splash above), so events are polled straight from the x
 *  server the same way x_run() does and xwin_repaint() pushes every
 *  redraw synchronously.  Arrow keys or taps move the selection,
 *  Enter / Start / a second tap on the same row confirms, Esc or a
 *  window close cancels (returns -1, the caller keeps its default).
 *  Returns the index of the chosen volume.
 */

int VideoBootChooser(const char *const *labels, const int *icons, int count)
{
	if (xwin == NULL || labels == NULL || count < 2)
		return -1;
	if (count > CHOOSER_MAX)
		count = CHOOSER_MAX;
	if (chooser_font == NULL)
		chooser_font = font_new(DEFAULT_SYSTEM_FONT, true);

	int xserv_pid = dev_get_pid("/dev/x");

	// Drain events that queued up before the chooser appeared (e.g.
	// touches during the copy splash), so a stale press cannot
	// confirm by accident
	if (xserv_pid >= 0) {
		for (;;) {
			proto_t out;
			PF->init(&out);
			int have = -1;
			if (dev_cntl_by_pid(xserv_pid, X_DCNTL_GET_EVT, NULL, &out) == 0)
				have = (int)proto_read_int(&out);
			PF->clear(&out);
			if (have != 0)
				break;
		}
	}

	chooser_count = count;
	chooser_sel = 0;
	for (int i = 0; i < count; i++) {
		snprintf(chooser_labels[i], sizeof(chooser_labels[i]), "%s",
			labels[i] != NULL ? labels[i] : "");
		int ic = (icons != NULL) ? icons[i] : ICON_FLOPPY;
		chooser_icon[i] = (ic >= ICON_FLOPPY && ic <= ICON_CD) ?
			ic : ICON_FLOPPY;
	}
	boot_chooser = true;
	xwin_repaint(xwin);
	printf("xwin: boot chooser, %d bootable volumes\n", count);

	int result = -1;
	bool done = false;
	while (!done && !emerg_quit) {
		xevent_t ev;
		bool got = false;
		if (xserv_pid >= 0) {
			proto_t out;
			PF->init(&out);
			if (dev_cntl_by_pid(xserv_pid, X_DCNTL_GET_EVT, NULL, &out) == 0 &&
			    proto_read_int(&out) == 0) {
				proto_read_to(&out, &ev, sizeof(ev));
				got = true;
			}
			PF->clear(&out);
		}
		if (!got) {
			proc_usleep(16 * 1000);
			continue;
		}

		switch (ev.type) {
		case XEVT_IM:
			if (ev.state != XIM_STATE_PRESS)
				break;
			switch (ev.value.im.key_code) {
			case KEY_UP: case KEY_LEFT:
				chooser_sel = (chooser_sel + chooser_count - 1) %
					chooser_count;
				xwin_repaint(xwin);
				break;
			case KEY_DOWN: case KEY_RIGHT:
				chooser_sel = (chooser_sel + 1) % chooser_count;
				xwin_repaint(xwin);
				break;
			case KEY_ENTER: case '\n':
				result = chooser_sel;
				done = true;
				break;
			case KEY_ESC:
				done = true;	// cancel: result stays -1
				break;
			}
			break;
		case XEVT_MOUSE:
			if (ev.state != MOUSE_STATE_DOWN)
				break;
			{
				gpos_t pos = xwin_get_inside_pos(xwin,
					ev.value.mouse.x, ev.value.mouse.y);
				int hit = -1;
				for (int i = 0; i < chooser_count; i++) {
					if (chooser_rect_hit(&chooser_row_rect[i],
							pos.x, pos.y)) {
						hit = i;
						break;
					}
				}
				if (hit >= 0) {
					if (hit == chooser_sel) {
						// Second tap on the same row confirms
						result = hit;
						done = true;
					} else {
						chooser_sel = hit;
						xwin_repaint(xwin);
					}
				} else if (chooser_rect_hit(&chooser_start_rect,
						pos.x, pos.y)) {
					result = chooser_sel;
					done = true;
				}
			}
			break;
		case XEVT_WIN:
			if (ev.value.window.event == XEVT_WIN_CLOSE) {
				// Same as on_xwin_event: the normal quit path takes
				// over once the guest runs
				emerg_quit = true;
				ADBKeyDown(0x7f);	// Power key
				ADBKeyUp(0x7f);
			} else {
				// resize/move/focus: keep the window state coherent
				// (xwin_event_handle repaints resizes itself, which
				// redraws the chooser in the new geometry)
				xwin_event_handle(xwin, &ev);
				if (xwin->xinfo == NULL)
					done = true;	// window destroyed under us
			}
			break;
		}
	}

	boot_chooser = false;
	if (xwin->xinfo != NULL)
		xwin_repaint(xwin);
	printf("xwin: boot chooser done, selection %d\n", result);
	return result;
}
