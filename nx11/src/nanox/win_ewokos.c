/*
 * win_ewokos.c -- bridge between the Nano-X server and the EwokOS window system.
 *
 * The requirement this file exists for is that an X11/Nano-X top-level window
 * IS an EwokOS xwin window: it is moved, resized, raised, focused and closed by
 * the EwokOS window manager like any other window on the desktop.
 *
 * Microwindows however draws every window through one shared root PSD in one
 * framebuffer, in absolute root coordinates.  The two models are reconciled with
 * a screen-sized "virtual screen" (graph_new_shm, so g2d can be used on it):
 *
 *   - Nano-X keeps drawing in absolute desktop coordinates into that buffer;
 *   - every top-level window gets an xwin created at the window's *frame*
 *     rectangle (x-bordersize, y-bordersize, width+2*bs, height+2*bs), so the
 *     xwin workspace covers exactly the pixels Nano-X owns for that window;
 *   - Update() regions are aggregated by scr_ewokos.c and mirrored here into
 *     each intersecting workspace with graph_blt() and presented with
 *     xwin_repaint().
 *
 * Because workspace origin == frame origin == root origin, nothing is ever
 * translated: drawing needs no offset and the absolute mouse coordinates EwokOS
 * reports can be fed straight to GsHandleMouseStatus().
 *
 * Geometry, stacking and focus are kept in sync in both directions.  Callbacks
 * from libx only record a request (they can be invoked from inside
 * xwin_repaint, which holds the window's painting lock, so calling back into
 * Nano-X from there would deadlock); nx11x_apply_pending() carries them out
 * from the event pump instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <graph/graph.h>
#include <x/x.h>
#include <x/xwin.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/klog.h>
#include <ewoksys/proc.h>
#include <ewoksys/proto.h>
#include <ewoksys/vdevice.h>
#include <ewoksys/vfs.h>
#include <mouse/mouse.h>

#include "device.h"
#include "nano-X.h"
#include "serv.h"
#include "ewokos.h"

/* deferred work recorded by the libx callbacks*/
#define PEND_GEOM		1	/* server changed the workspace: follow it*/
#define PEND_FOCUS		2	/* server gave us focus: raise + focus in Nano-X*/
#define PEND_CLOSEREQ	3	/* first close request: ask the client politely*/
#define PEND_DESTROY	4	/* second close request: tear the window down*/

typedef struct nx11xwin {
	GR_WINDOW	*wp;		/* Nano-X top-level window*/
	xwin_t		*xwin;		/* its EwokOS window*/
	int			dirty;		/* a partial mirror rectangle is pending*/
	MWCOORD		dx0, dy0, dx1, dy1;	/* that rectangle, in desktop coords*/
	int			syncing;	/* ignore callbacks echoed by our own libx calls*/
	int			closereq;	/* CLOSE_REQ already delivered once*/
	int			shown;		/* the xwin has been made visible*/
	int			pending;	/* PEND_*, 0 when idle*/
	struct nx11xwin *next;
} NX11XWIN;

static x_t			_xctx;
static graph_t *	_vfb;			/* the virtual screen*/
static NX11XWIN *	_winlist;
static int			_xserv_pid = -1;
static int32_t		_disp_index;
static int			_ready;
static int			_quit;
static int			_named;
static int			_resize_present;	/* on_resize armed a frame to publish before sleep*/

/*----------------------------------------------------------------------------*/
/* virtual screen                                                             */
/*----------------------------------------------------------------------------*/

int
nx11x_screen_init(int *pxres, int *pyres)
{
	xscreen_info_t scr;

	if (_ready) {
		*pxres = _vfb->w;
		*pyres = _vfb->h;
		return 1;
	}

	x_init(&_xctx, NULL);

	_xserv_pid = dev_get_pid("/dev/x");
	if (_xserv_pid < 0) {
		klog("nx11: no xserver (/dev/x)\n");
		return 0;
	}

	_disp_index = (int32_t)x_get_display_id(-1);

	memset(&scr, 0, sizeof(scr));
	if (x_screen_info(&scr, _disp_index) != 0 || scr.size.w <= 0 || scr.size.h <= 0) {
		klog("nx11: cannot query screen info\n");
		return 0;
	}

	/* shm-backed so graph_blt can hand the mirror copy to g2d*/
	_vfb = graph_new_shm(scr.size.w, scr.size.h);
	if (_vfb == NULL || _vfb->buffer == NULL) {
		klog("nx11: cannot allocate %dx%d virtual screen\n", scr.size.w, scr.size.h);
		return 0;
	}
	graph_clear(_vfb, argb(0, 0, 0, 0));

	*pxres = scr.size.w;
	*pyres = scr.size.h;
	_ready = 1;
	return 1;
}

void
nx11x_screen_term(void)
{
	NX11XWIN *nw;

	while ((nw = _winlist) != NULL) {
		_winlist = nw->next;
		if (nw->xwin != NULL) {
			nw->xwin->data = NULL;
			xwin_close(nw->xwin);
			xwin_destroy(nw->xwin);
		}
		free(nw);
	}
	if (_vfb != NULL) {
		graph_free(_vfb);
		_vfb = NULL;
	}
	/* drop this process's event pool on the server, the way x_run() does*/
	if (_xserv_pid >= 0)
		dev_cntl_by_pid(_xserv_pid, X_DCNTL_QUIT, NULL, NULL);
	_ready = 0;
}

void *
nx11x_screen_graph(void)
{
	return (void *)_vfb;
}

/*----------------------------------------------------------------------------*/
/* window list                                                                */
/*----------------------------------------------------------------------------*/

static NX11XWIN *
nx11x_find(GR_WINDOW *wp)
{
	NX11XWIN *nw;

	for (nw = _winlist; nw != NULL; nw = nw->next)
		if (nw->wp == wp)
			return nw;
	return NULL;
}

/* the desktop rectangle Nano-X owns for a top-level window: its client area
   plus the border the server draws around it*/
static void
nx11x_frame_rect(GR_WINDOW *wp, int *x, int *y, int *w, int *h)
{
	int bs = wp->bordersize;

	*x = wp->x - bs;
	*y = wp->y - bs;
	*w = wp->width + bs*2;
	*h = wp->height + bs*2;
	if (*w < 1) *w = 1;
	if (*h < 1) *h = 1;
}

/* GR_WM_PROPS -> XWIN_STYLE: Nano-X draws the frame itself only when the client
   asked for no decorations, in which case EwokOS must not draw one either*/
static int
nx11x_style(GR_WINDOW *wp)
{
	int style = XWIN_STYLE_NORMAL;

	if (wp->props & GR_WM_PROPS_NODECORATE)
		style |= XWIN_STYLE_NO_FRAME | XWIN_STYLE_NO_TITLE;
	if (wp->props & (GR_WM_PROPS_NORESIZE|GR_WM_PROPS_NOAUTORESIZE))
		style |= XWIN_STYLE_NO_RESIZE;
	if (wp->props & GR_WM_PROPS_NOFOCUS)
		style |= XWIN_STYLE_NO_FOCUS;
	return style;
}

/*----------------------------------------------------------------------------*/
/* geometry, both directions                                                  */
/*----------------------------------------------------------------------------*/

/* Nano-X -> EwokOS: the application moved or resized its window*/
static void
nx11x_push_geom(NX11XWIN *nw)
{
	int x, y, w, h;

	if (nw->xwin == NULL || nw->xwin->xinfo == NULL || nw->syncing)
		return;

	nx11x_frame_rect(nw->wp, &x, &y, &w, &h);
	if (x == nw->xwin->xinfo->wsr.x && y == nw->xwin->xinfo->wsr.y &&
			w == nw->xwin->xinfo->wsr.w && h == nw->xwin->xinfo->wsr.h)
		return;

	/* xwin_resize_to/xwin_move_to call on_resize/on_move synchronously and
	   xwin_move_to does so without a NULL check, so both the callbacks and
	   the echo they produce have to be suppressed here*/
	nw->syncing = 1;
	if (w != nw->xwin->xinfo->wsr.w || h != nw->xwin->xinfo->wsr.h)
		xwin_resize_to(nw->xwin, w, h);
	if (x != nw->xwin->xinfo->wsr.x || y != nw->xwin->xinfo->wsr.y)
		xwin_move_to(nw->xwin, x, y);
	nw->syncing = 0;

	/* the workspace was rebuilt: mirror the whole window next time*/
	nw->dirty = 0;
}

/* EwokOS -> Nano-X: the window manager moved or resized the workspace*/
static void
nx11x_pull_geom(NX11XWIN *nw)
{
	graph_t *save = NULL;
	uint32_t *buf = NULL;
	int bs, x, y, w, h;
	int ow, oh, sx, sy, cw, ch;

	if (nw->xwin == NULL || nw->xwin->xinfo == NULL)
		return;

	bs = nw->wp->bordersize;
	x = nw->xwin->xinfo->wsr.x + bs;
	y = nw->xwin->xinfo->wsr.y + bs;
	w = nw->xwin->xinfo->wsr.w - bs*2;
	h = nw->xwin->xinfo->wsr.h - bs*2;
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	/* the server repaints the background over the window when it resizes,
	   so snapshot the old pixels first and put the overlap back at the new
	   origin: an app without a backing store (a terminal) keeps its text*/
	ow = nw->wp->width;
	oh = nw->wp->height;
	if ((ow != w || oh != h) && ow > 0 && oh > 0 && _vfb != NULL) {
		sx = nw->xwin->xinfo->wsr.x;
		sy = nw->xwin->xinfo->wsr.y;
		buf = (uint32_t *)malloc((size_t)ow * (size_t)oh * 4);
		if (buf != NULL) {
			save = graph_new(buf, ow, oh);
			graph_blt(_vfb, sx, sy, ow, oh, save, 0, 0, ow, oh);
		}
	}

	nw->syncing = 1;
	if (nw->wp->width != w || nw->wp->height != h)
		GrResizeWindow(nw->wp->id, w, h);
	if (nw->wp->x != x || nw->wp->y != y)
		GrMoveWindow(nw->wp->id, x - nw->wp->parent->x, y - nw->wp->parent->y);
	nw->syncing = 0;

	if (save != NULL) {
		cw = ow < w ? ow : w;
		ch = oh < h ? oh : h;
		graph_blt(save, 0, 0, cw, ch, _vfb,
				nw->xwin->xinfo->wsr.x, nw->xwin->xinfo->wsr.y, cw, ch);
		graph_free(save);
		free(buf);
	}

	/* Nano-X just redrew the window at its new place, so the pending
	   rectangle (if any) is stale - mirror everything, and present right
	   away: the rebuilt workspace would otherwise keep showing the frame
	   mirrored before the resize until something else damages it*/
	nw->dirty = 0;
	xwin_repaint(nw->xwin);
}

/*----------------------------------------------------------------------------*/
/* libx callbacks                                                             */
/*----------------------------------------------------------------------------*/

/*
 * Mirror the window's part of the virtual screen into its workspace.
 * Called by xwin_repaint() with the painting lock held, so this must only ever
 * touch pixels and never call back into Nano-X or libx.
 */
static void
nx11x_on_repaint(xwin_t *xwin, graph_t *g)
{
	NX11XWIN *nw = (NX11XWIN *)xwin->data;
	MWCOORD x0, y0, x1, y1, w, h;

	if (nw == NULL || _vfb == NULL || g == NULL || g->buffer == NULL ||
			xwin->xinfo == NULL)
		return;

	if (nw->dirty) {
		x0 = nw->dx0;
		y0 = nw->dy0;
		x1 = nw->dx1;
		y1 = nw->dy1;
		nw->dirty = 0;
	} else {
		/* no pending rectangle - expose, rebuild or first present: the
		   whole workspace has to be refreshed*/
		x0 = xwin->xinfo->wsr.x;
		y0 = xwin->xinfo->wsr.y;
		x1 = x0 + g->w - 1;
		y1 = y0 + g->h - 1;
	}

	/* clip to the workspace, which is exactly what g covers*/
	if (x0 < xwin->xinfo->wsr.x) x0 = xwin->xinfo->wsr.x;
	if (y0 < xwin->xinfo->wsr.y) y0 = xwin->xinfo->wsr.y;
	if (x1 > xwin->xinfo->wsr.x + g->w - 1) x1 = xwin->xinfo->wsr.x + g->w - 1;
	if (y1 > xwin->xinfo->wsr.y + g->h - 1) y1 = xwin->xinfo->wsr.y + g->h - 1;
	w = x1 - x0 + 1;
	h = y1 - y0 + 1;
	if (w <= 0 || h <= 0)
		return;

	/* graph_blt on two shm graphs goes through g2d when it is available*/
	graph_blt(_vfb, x0, y0, w, h, g, x0 - xwin->xinfo->wsr.x, y0 - xwin->xinfo->wsr.y, w, h);
}

static void
nx11x_on_resize(xwin_t *xwin)
{
	NX11XWIN *nw = (NX11XWIN *)xwin->data;

	/* can be called from inside xwin_repaint (painting lock held), so only
	   record the request*/
	if (nw != NULL && !nw->syncing) {
		nw->pending = PEND_GEOM;
		/* The resized window is redrawn once for its new size and then the app
		   goes idle, so arm the one-shot publish nx11x_wait() runs before it
		   sleeps.  It cannot be done right here: the client redraw that makes
		   the correct frame happens only after this callback returns (see the
		   nx11x_flush_presents() comment).*/
		_resize_present = 1;
	}
}

static void
nx11x_on_move(xwin_t *xwin)
{
	nx11x_on_resize(xwin);
}

/*
 * EwokOS raised and focused the window, so Nano-X has to restack too: its
 * clipping is derived from the sibling order and would otherwise keep clipping
 * away pixels of a window the compositor already put on top.
 */
static void
nx11x_on_focus(xwin_t *xwin)
{
	NX11XWIN *nw = (NX11XWIN *)xwin->data;

	if (nw != NULL && !nw->syncing && nw->pending == 0)
		nw->pending = PEND_FOCUS;
}

static void
nx11x_on_unfocus(xwin_t *xwin)
{
	(void)xwin;	/* Nano-X focus follows the click, nothing to undo here*/
}

/*
 * The close button was pressed.  libx would tear the window down straight away
 * from xwin_event_handle(); Nano-X owns the window lifetime instead, so the
 * request is queued: the first one becomes GR_EVENT_TYPE_CLOSE_REQ (which nx11
 * turns into WM_DELETE_WINDOW), a second one destroys the window.
 */
static bool
nx11x_on_close(xwin_t *xwin)
{
	NX11XWIN *nw = (NX11XWIN *)xwin->data;

	if (nw != NULL) {
		if (nw->closereq)
			nw->pending = PEND_DESTROY;
		else {
			nw->closereq = 1;
			nw->pending = PEND_CLOSEREQ;
		}
		/* the top-level window is going away: flag the x context so the pump
		   raises the quit that shuts the linked server down, exactly as the
		   documented "main window closed" catch-all in ewokos.h intends */
		_xctx.terminated = true;
	}
	return true;
}

/*----------------------------------------------------------------------------*/
/* server hooks (see serv*.c, all guarded by #if EWOKOS)                      */
/*----------------------------------------------------------------------------*/

/*
 * Make the EwokOS window visible.  Deliberately not called from
 * nx11x_win_realize(): a window that goes visible while its workspace is still
 * empty races the compositor's first frame - the frame carrying the client's
 * first drawing can be lost in that handshake, and the desktop then keeps
 * showing the cleared background until something happens to redraw.  Showing
 * the window together with its first mirrored content (nx11x_update) removes
 * the race entirely.
 */
static void
nx11x_show(NX11XWIN *nw)
{
	nw->shown = 1;
	nw->syncing = 1;
	xwin_set_visible(nw->xwin, true);
	nw->syncing = 0;
}

void
nx11x_win_realize(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;
	const char *title;
	int x, y, w, h;

	if (!_ready || wp == NULL || wp->parent != rootwp)
		return;					/* only top-level windows are xwin windows*/

	nw = nx11x_find(wp);
	if (nw == NULL) {
		nw = (NX11XWIN *)calloc(1, sizeof(NX11XWIN));
		if (nw == NULL)
			return;
		nw->wp = wp;
		nw->next = _winlist;
		_winlist = nw;
	}

	if (nw->xwin != NULL) {
		/* re-realized after a temporary unmap (GrMoveWindow) or an unmap*/
		nw->dirty = 0;
		nw->shown = 1;
		nw->syncing = 1;
		xwin_set_visible(nw->xwin, true);
		nw->syncing = 0;
		return;
	}

	nx11x_frame_rect(wp, &x, &y, &w, &h);
	title = (wp->title != NULL && wp->title[0] != 0) ? wp->title : "NX11";

	nw->syncing = 1;
	nw->xwin = xwin_open(&_xctx, _disp_index, x, y, w, h, title, nx11x_style(wp));
	nw->syncing = 0;
	if (nw->xwin == NULL) {
		klog("nx11: xwin_open failed for window %d\n", wp->id);
		return;
	}

	nw->xwin->data = nw;
	nw->xwin->on_repaint = nx11x_on_repaint;
	nw->xwin->on_resize = nx11x_on_resize;
	nw->xwin->on_move = nx11x_on_move;
	nw->xwin->on_focus = nx11x_on_focus;
	nw->xwin->on_unfocus = nx11x_on_unfocus;
	nw->xwin->on_close = nx11x_on_close;

	if (!_named) {
		_named = 1;
		x_set_app_name(&_xctx, getenv("X_APP_NAME"));
	}

	/* the server may have clamped the workspace to the desktop: adopt it*/
	if (nw->xwin->xinfo->wsr.w != w || nw->xwin->xinfo->wsr.h != h ||
			nw->xwin->xinfo->wsr.x != x || nw->xwin->xinfo->wsr.y != y)
		nw->pending = PEND_GEOM;

	/* stay hidden until the first mirrored content is ready, see nx11x_show*/
	nw->shown = 0;
}

void
nx11x_win_unrealize(void *p, int temp_unmap)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;

	if (!_ready || temp_unmap)
		return;					/* temporary unmap for a move: keep the xwin*/

	nw = nx11x_find(wp);
	if (nw == NULL || nw->xwin == NULL || nw->syncing)
		return;

	nw->dirty = 0;
	nw->shown = 0;
	nw->syncing = 1;
	xwin_set_visible(nw->xwin, false);
	nw->syncing = 0;
}

void
nx11x_win_destroy(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw, **pp;

	for (pp = &_winlist; (nw = *pp) != NULL; pp = &nw->next) {
		if (nw->wp != wp)
			continue;
		*pp = nw->next;
		if (nw->xwin != NULL) {
			/* detach first: xwin_close() runs on_close, and the entry
			   is about to go away*/
			nw->xwin->data = NULL;
			xwin_close(nw->xwin);		/* no-op once fd <= 0*/
			xwin_destroy(nw->xwin);
		}
		free(nw);
		return;
	}
}

void
nx11x_win_moved(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;

	if (!_ready || wp == NULL || wp->parent != rootwp)
		return;
	nw = nx11x_find(wp);
	if (nw != NULL)
		nx11x_push_geom(nw);
}

void
nx11x_win_resized(void *p)
{
	nx11x_win_moved(p);
}

void
nx11x_win_raised(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;

	if (!_ready || wp == NULL || wp->parent != rootwp)
		return;
	nw = nx11x_find(wp);
	if (nw == NULL || nw->xwin == NULL || nw->syncing)
		return;

	nw->syncing = 1;
	xwin_top(nw->xwin);
	nw->syncing = 0;
}

void
nx11x_win_focused(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;

	if (!_ready || wp == NULL || wp->parent != rootwp)
		return;
	nw = nx11x_find(wp);
	if (nw == NULL || nw->xwin == NULL || nw->syncing || nw->xwin->xinfo == NULL)
		return;

	/* only ask when the server disagrees, or the focus event it sends back
	   would start a loop*/
	if (!nw->xwin->xinfo->focused)
		vfs_fcntl(nw->xwin->fd, XWIN_CNTL_TRY_FOCUS, NULL, NULL);
}

/* GrSetWMProperties: the title and possibly the border size changed*/
void
nx11x_win_props(void *p)
{
	GR_WINDOW *wp = (GR_WINDOW *)p;
	NX11XWIN *nw;

	if (!_ready || wp == NULL || wp->parent != rootwp)
		return;
	nw = nx11x_find(wp);
	if (nw == NULL || nw->xwin == NULL || nw->xwin->xinfo == NULL)
		return;

	/* libx has no set-title control, the shm field is the interface*/
	memset(nw->xwin->xinfo->title, 0, XWIN_TITLE_MAX);
	if (wp->title != NULL)
		strncpy(nw->xwin->xinfo->title, wp->title, XWIN_TITLE_MAX-1);

	nw->xwin->xinfo->style = nx11x_style(wp);
	nx11x_push_geom(nw);			/* bordersize may have changed*/
	nw->dirty = 0;
	if (nw->xwin->xinfo->visible)
		xwin_repaint(nw->xwin);
}

/*----------------------------------------------------------------------------*/
/* mirroring                                                                  */
/*----------------------------------------------------------------------------*/

/* grow the window's pending mirror rectangle, clipped to its frame*/
static void
nx11x_mark(NX11XWIN *nw, MWCOORD x, MWCOORD y, MWCOORD x2, MWCOORD y2)
{
	MWCOORD fx, fy, fx2, fy2;
	int fx0, fy0, fw, fh;

	nx11x_frame_rect(nw->wp, &fx0, &fy0, &fw, &fh);
	fx = fx0;
	fy = fy0;
	fx2 = fx + fw - 1;
	fy2 = fy + fh - 1;

	if (x < fx) x = fx;
	if (y < fy) y = fy;
	if (x2 > fx2) x2 = fx2;
	if (y2 > fy2) y2 = fy2;
	if (x > x2 || y > y2)
		return;						/* does not touch this window*/

	if (!nw->dirty) {
		nw->dirty = 1;
		nw->dx0 = x;
		nw->dy0 = y;
		nw->dx1 = x2;
		nw->dy1 = y2;
		return;
	}
	if (x < nw->dx0) nw->dx0 = x;
	if (y < nw->dy0) nw->dy0 = y;
	if (x2 > nw->dx1) nw->dx1 = x2;
	if (y2 > nw->dy1) nw->dy1 = y2;
}

/*
 * Called by scr_ewokos.c with the aggregate update region since the last flush.
 * Marks every window the region touches, then presents each of them once.
 */
void
nx11x_update(MWCOORD x, MWCOORD y, MWCOORD width, MWCOORD height)
{
	NX11XWIN *nw;
	MWCOORD x2, y2;

	if (!_ready || _vfb == NULL || width <= 0 || height <= 0)
		return;

	/* clip to the virtual screen: windows may hang off the desktop*/
	if (x < 0) { width += x; x = 0; }
	if (y < 0) { height += y; y = 0; }
	if (x >= _vfb->w || y >= _vfb->h)
		return;
	if (x + width > _vfb->w) width = _vfb->w - x;
	if (y + height > _vfb->h) height = _vfb->h - y;
	if (width <= 0 || height <= 0)
		return;

	x2 = x + width - 1;
	y2 = y + height - 1;

	for (nw = _winlist; nw != NULL; nw = nw->next) {
		if (nw->xwin == NULL || nw->wp == NULL || !nw->wp->realized)
			continue;
		nx11x_mark(nw, x, y, x2, y2);
	}

	/* second pass so a window is presented once per flush; a window becomes
	   visible together with its first mirrored content, never empty*/
	for (nw = _winlist; nw != NULL; nw = nw->next) {
		if (!nw->dirty || nw->xwin == NULL)
			continue;
		if (!nw->shown)
			nx11x_show(nw);
		xwin_repaint(nw->xwin);
	}
}

/*----------------------------------------------------------------------------*/
/* event pump                                                                 */
/*----------------------------------------------------------------------------*/

/*
 * libx keeps x_get_event() private, so the pull is repeated here: one non
 * blocking X_DCNTL_GET_EVT round trip to the xserver.  Blocking is done
 * separately in nx11x_wait() on the per-process event node.
 */
static int
nx11x_get_event(xevent_t *ev)
{
	proto_t out;
	int res;

	if (_xserv_pid < 0)
		return -1;

	PF->init(&out);
	if (dev_cntl_by_pid(_xserv_pid, X_DCNTL_GET_EVT, NULL, &out) != 0) {
		PF->clear(&out);
		return -1;
	}
	res = proto_read_int(&out);
	if (res == 0)
		proto_read_to(&out, ev, sizeof(xevent_t));
	PF->clear(&out);
	return res;
}

static void
nx11x_dispatch(xevent_t *ev)
{
	xwin_t *xwin = xwin_find_by_handle(ev->win);
	NX11XWIN *nw;

	if (xwin == NULL)
		return;
	nw = (NX11XWIN *)xwin->data;
	if (nw == NULL)
		return;

	switch (ev->type) {
	case XEVT_WIN:
		if (ev->value.window.event == XEVT_WIN_CLOSE) {
			/* handled through on_close -> pending, never through
			   xwin_event_handle() which would close the xwin
			   underneath the Nano-X window*/
			if (nw->closereq)
				nw->pending = PEND_DESTROY;
			else {
				nw->closereq = 1;
				nw->pending = PEND_CLOSEREQ;
			}
			/* the main window is closing: mark the x context terminated so
			   nx11x_pump() raises the quit that GsCheckKeyboardEvent() turns
			   into KBD_QUIT -> GsTerminate().  This shuts the process down from
			   inside the server's own select loop, so it works even when the
			   client never acts on the CLOSE_REQ (which is what left closed
			   windows as live, spinning processes).*/
			_xctx.terminated = true;
		} else
			xwin_event_handle(xwin, ev);
		break;

	case XEVT_MOUSE:
		/* absolute desktop coordinates == Nano-X root coordinates*/
		nx11x_mouse_push(ev->value.mouse.x, ev->value.mouse.y,
				ev->value.mouse.button, ev->state);
		break;

	case XEVT_IM:
		nx11x_key_push(ev->value.im.key_code, ev->value.im.value,
				(int)ev->value.im.shift, (int)ev->value.im.ctrl,
				ev->state == XIM_STATE_PRESS);
		break;

	default:
		break;
	}
}

static NX11XWIN *
nx11x_take_pending(int *what)
{
	NX11XWIN *nw;

	for (nw = _winlist; nw != NULL; nw = nw->next) {
		if (nw->pending == 0)
			continue;
		*what = nw->pending;
		nw->pending = 0;
		return nw;
	}
	return NULL;
}

/*
 * Carry out what the libx callbacks recorded.  Runs from the pump, i.e. outside
 * any xwin_repaint(), so it is safe to call back into Nano-X here - which the
 * callbacks themselves are not, since libx invokes on_resize from inside
 * xwin_fetch_graph() while the painting lock is held.
 */
static void
nx11x_apply_pending(void)
{
	int guard = 128;			/* bounded: a request may re-arm another one*/
	int what;
	NX11XWIN *nw;

	while (guard-- > 0) {
		nw = nx11x_take_pending(&what);
		if (nw == NULL)
			return;

		switch (what) {
		case PEND_GEOM:
			nx11x_pull_geom(nw);
			break;

		case PEND_FOCUS:
			nw->syncing = 1;
			GrRaiseWindow(nw->wp->id);
			GrSetFocus(nw->wp->id);
			nw->syncing = 0;
			break;

		case PEND_CLOSEREQ:
			GsDeliverGeneralEvent(nw->wp, GR_EVENT_TYPE_CLOSE_REQ, NULL);
			break;

		case PEND_DESTROY:
			/* frees the window and, through nx11x_win_destroy, the
			   entry itself - the walk restarts from the head*/
			GrDestroyWindow(nw->wp->id);
			break;
		}
	}
}

int
nx11x_pump(void)
{
	xevent_t ev;
	NX11XWIN *nw;
	int n = 0;

	if (!_ready)
		return 0;

	/* publish fps_async presents the server was still compositing when they
	   were handed in - a cheap single load when nothing is pending, and the
	   only thing that ever puts such a frame on screen (x_run() does it)*/
	xwin_retry_pending_presents();

	while (nx11x_get_event(&ev) == 0) {
		n++;
		nx11x_dispatch(&ev);
	}

	/* the main window went away: shut the server down like the quit key does*/
	if (_xctx.terminated && !_quit) {
		_quit = 1;
		n++;
	}

	nx11x_apply_pending();

	/* geometry changes and dispatched repaints may have left a frame
	   unpublished while the server was still compositing: retry now that
	   the pending requests of this tick are all done*/
	xwin_retry_pending_presents();

	/* a window that mapped but never drew still has to appear: once the client
	   goes idle, show it with whatever its background is*/
	for (nw = _winlist; nw != NULL; nw = nw->next) {
		if (nw->xwin != NULL && !nw->shown &&
				nw->wp != NULL && nw->wp->realized)
			nx11x_show(nw);
	}
	return n;
}

int
nx11x_quit_pending(void)
{
	if (_quit) {
		_quit = 0;
		return 1;
	}
	return 0;
}

/*
 * Non-consuming peek at the pending quit.  ewokos_kbd_poll() uses it to report
 * the shutdown as readable input: GsCheckKeyboardEvent() returns immediately at
 * its "Poll() == 0" gate, so without a key actually queued it would never call
 * ewokos_kbd_read() and the KBD_QUIT that terminates the server would be lost.
 */
int
nx11x_quit_peek(void)
{
	return _quit;
}

/*
 * fps_async skips a present while the server still owns the handoff buffer
 * (update_requested != 0): the finished frame waits in ws_g and
 * xwin_retry_pending_presents() re-flips it on a later pump.  A resize makes
 * this visible.  nx11x_on_resize() defers the geometry, libx presents a stale
 * frame right after it (update_requested=1), the client then redraws once for
 * the new size and that frame's flip is skipped - and because the app goes idle
 * immediately after, no later pump runs, so the new picture sits unpublished
 * until the next input event and the window keeps showing the pre-resize
 * frame.  xserverd will not nudge it either: the stale frame already made the
 * window "ready", so its not-ready XEVT_WIN_REPAINT recovery never fires, and
 * x_update_commit() does not wake an fps_async client.  The redraw happens
 * after on_resize returns, so on_resize only arms _resize_present and
 * nx11x_wait() runs this once before it sleeps.  Bounded so a wedged server
 * cannot hang the client.
 */
#define NX11X_FLUSH_PRESENT_MS	64	/* ~4 frames at the server's 60 fps */

static int
nx11x_present_pending(void)
{
	NX11XWIN *nw;

	for (nw = _winlist; nw != NULL; nw = nw->next)
		if (nw->xwin != NULL && nw->xwin->present_pending)
			return 1;
	return 0;
}

static void
nx11x_flush_presents(void)
{
	int waited = 0;

	while (waited < NX11X_FLUSH_PRESENT_MS && nx11x_present_pending()) {
		xwin_retry_pending_presents();
		if (!nx11x_present_pending())
			break;
		proc_usleep(2000);
		waited += 2;
	}
}

/*
 * Wait for the xserver to deliver something.  ms < 0 blocks until an event
 * arrives; the vfs offers no timed block on a node, so a finite wait polls in
 * small slices against a real-clock deadline (kernel_tic_ms keeps the duration
 * correct even when usleep degrades to a bare yield).
 */
static void
nx11x_wait(int ms)
{
	proto_t out;

	if (_xserv_pid < 0)
		return;

	if (_xctx.evt_node == 0) {
		PF->init(&out);
		if (dev_cntl_by_pid(_xserv_pid, X_DCNTL_GET_EVT_NODE, NULL, &out) == 0)
			_xctx.evt_node = (uint32_t)proto_read_int(&out);
		PF->clear(&out);
	}
	if (_xctx.evt_node == 0) {
		proc_usleep(ms < 0 ? 10000 : 1000);
		return;
	}

	if (ms < 0) {
		vfs_clear_poll_events(_xctx.evt_node, VFS_EVT_RD);
		/* an event may have landed between the last poll and the clear*/
		if (nx11x_pump() > 0 || _xctx.terminated)
			return;
		/* on_resize armed this: publish the frame fps_async skipped for the
		   resize redraw before sleeping, so an idle app does not leave the
		   window showing the pre-resize picture until the next input event*/
		if (_resize_present) {
			nx11x_flush_presents();
			_resize_present = 0;
		}
		vfs_block(_xctx.evt_node, VFS_EVT_RD);
		return;
	}

	vfs_clear_poll_events(_xctx.evt_node, VFS_EVT_RD);
	/* an event may have landed between the last poll and the clear*/
	if (nx11x_pump() > 0 || _xctx.terminated)
		return;
	/* sleep on the event node itself: xserverd wakes us the moment a new
	   event lands, the timeout only caps the wait - no polling in between*/
	proc_block_timeout(_xctx.evt_node, (uint32_t)ms * 1000);
}

/*
 * Replaces GsSelect's select() loop (see srvmain.c).  Flushes pending drawing,
 * turns queued input into Nano-X events, then waits exactly as long as the
 * caller asked for - blocking on the xserver event node instead of polling,
 * which is what PSF_CANTBLOCK drivers cannot do.
 */
void
nx11x_select(int timeout)
{
#if MW_FEATURE_TIMERS
	struct timeval tout;
#endif
	int wait_ms;
	int events;

	if (!_ready) {
		proc_usleep(10000);
		return;
	}

	/* flush the aggregate update region and pull in fresh events*/
	events = scrdev.PreSelect ? scrdev.PreSelect(&scrdev) : nx11x_pump();

	while (GsCheckMouseEvent())
		continue;
	while (GsCheckKeyboardEvent())
		continue;

	/* flushing server-side damage is not a client event: returning early
	   here made timed callers (GrGetNextEventTimeout) spin tight with an
	   empty queue - only bail out when a real event is waiting already*/
	if (curclient->eventhead != NULL)
		return;
	if (timeout == GR_TIMEOUT_POLL)
		return;

#if MW_FEATURE_TIMERS
	tout.tv_sec = 0;
	tout.tv_usec = 0;
	/* FALSE means "no app timers and no caller timeout": block for good*/
	wait_ms = GdGetNextTimeout(&tout, timeout) ?
			tout.tv_sec*1000 + (tout.tv_usec + 999)/1000 : -1;
#else
	wait_ms = timeout ? timeout : -1;
#endif
	if (wait_ms == 0)
		wait_ms = 1;

	nx11x_wait(wait_ms);

	events = nx11x_pump();
	while (GsCheckMouseEvent())
		continue;
	while (GsCheckKeyboardEvent())
		continue;

#if MW_FEATURE_TIMERS
	if (GdTimeout()) {
		GR_EVENT_GENERAL *gp = (GR_EVENT_GENERAL *)GsAllocEvent(curclient);
		if (gp != NULL)
			gp->type = GR_EVENT_TYPE_TIMEOUT;
	}
#endif
	/* the caller's own timeout has to surface as an event too: with no
	   application timers running GdTimeout() never fires, and without a
	   queued event GrGetNextEventTimeout()'s wait loop would never end*/
	if (timeout != GR_TIMEOUT_BLOCK && timeout != GR_TIMEOUT_POLL &&
			curclient->eventhead == NULL) {
		GR_EVENT_GENERAL *gp = (GR_EVENT_GENERAL *)GsAllocEvent(curclient);
		if (gp != NULL)
			gp->type = GR_EVENT_TYPE_TIMEOUT;
	}
	(void)events;
}
