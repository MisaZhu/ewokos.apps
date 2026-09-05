/*
 * EwokOS screen driver.
 *
 * Microwindows owns one framebuffer for the whole desktop, but an EwokOS
 * application owns one xwin window per top-level window.  Rather than giving
 * the engine a per-window framebuffer (which would break the single shared
 * root PSD every Nano-X window points at), this driver hands it a screen-sized
 * memory framebuffer - the "virtual screen" - and lets the bridge in
 * nanox/win_ewokos.c mirror the dirty area into every xwin workspace it
 * intersects.  The mirror uses graph_blt on shm-backed graphs, so g2d does the
 * copy.
 *
 * PSF_DELAYUPDATE collects all Update() calls between two PreSelect() calls
 * into one aggregate rectangle, exactly like the X11 and SDL2 drivers, so a
 * full frame costs one blit per window instead of one per drawing operation.
 */
#include <stdlib.h>
#include <graph/graph.h>
#include "device.h"
#include "genmem.h"
#include "genfont.h"
#include "fb.h"
#include "ewokos.h"

/* aggregate update region, flushed in ewokos_preselect()*/
static MWCOORD upminX, upminY, upmaxX, upmaxY;

static PSD	ewokos_open(PSD psd);
static void	ewokos_close(PSD psd);
static void	ewokos_setpalette(PSD psd, int first, int count, MWPALENTRY *pal);
static void	ewokos_update(PSD psd, MWCOORD x, MWCOORD y, MWCOORD width, MWCOORD height);
static int	ewokos_preselect(PSD psd);

/*
 * PollEvents is deliberately NULL: nx11x_select() replaces GsSelect's
 * select() loop and calls PreSelect() itself, which both flushes the pending
 * update region and pulls in fresh xwin events.
 */
SCREENDEVICE scrdev = {
	0,0,0,0,0,0,0,NULL,0,NULL,0,0,0,0,0,0,
	gen_fonts, ewokos_open, ewokos_close, ewokos_setpalette, gen_getscreeninfo,
	gen_allocatememgc, gen_mapmemgc, gen_freememgc, gen_setportrait,
	ewokos_update, ewokos_preselect,
	0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL
};

static PSD
ewokos_open(PSD psd)
{
	graph_t *vfb;
	int xres = 0, yres = 0;

	if (!nx11x_screen_init(&xres, &yres))
		return NULL;

	/*
	 * PSF_ADDRMALLOC is not passed: the framebuffer is EwokOS shared memory
	 * owned by the bridge, and gen_freememgc()/free() must never touch it.
	 * PSF_CANTBLOCK is not passed either - nx11x_select() really blocks on
	 * the xserver's event node instead of spinning at WAITTIME intervals.
	 */
	if (!gen_initpsd(psd, MWPIXEL_FORMAT, xres, yres, PSF_SCREEN|PSF_DELAYUPDATE))
		return NULL;

	vfb = (graph_t *)nx11x_screen_graph();
	if (vfb == NULL || vfb->buffer == NULL)
		return NULL;

	/* 32bpp BGRA8888 in memory == EwokOS argb() 0xAARRGGBB words, and
	   graph_t has no pitch padding, so psd->pitch is already vfb->w*4*/
	psd->addr = (unsigned char *)vfb->buffer;

	upminX = upminY = MAX_MWCOORD;
	upmaxX = upmaxY = MIN_MWCOORD;
	return psd;
}

static void
ewokos_close(PSD psd)
{
	/* the framebuffer belongs to the bridge, only detach from it*/
	psd->addr = NULL;
	nx11x_screen_term();
}

static void
ewokos_setpalette(PSD psd, int first, int count, MWPALENTRY *pal)
{
	(void)psd; (void)first; (void)count; (void)pal;	/* truecolor, no palette*/
}

static void
ewokos_update(PSD psd, MWCOORD x, MWCOORD y, MWCOORD width, MWCOORD height)
{
	if (width <= 0 || height <= 0)
		return;

	if (psd->flags & PSF_DELAYUPDATE) {
		/* grow the aggregate update region*/
		if (x < upminX) upminX = x;
		if (y < upminY) upminY = y;
		if (x+width-1 > upmaxX) upmaxX = x+width-1;
		if (y+height-1 > upmaxY) upmaxY = y+height-1;
	} else
		nx11x_update(x, y, width, height);
}

/*
 * Called before every blocking wait and by GrFlush(): push the aggregate
 * update region out to the xwin workspaces, then report how many EwokOS
 * events are ready to be turned into Nano-X events.
 */
static int
ewokos_preselect(PSD psd)
{
	if ((psd->flags & PSF_DELAYUPDATE) && (upmaxX >= 0 || upmaxY >= 0)) {
		MWCOORD x = upminX, y = upminY;
		MWCOORD w = upmaxX - upminX + 1, h = upmaxY - upminY + 1;

		upminX = upminY = MAX_MWCOORD;
		upmaxX = upmaxY = MIN_MWCOORD;
		nx11x_update(x, y, w, h);
	}

	return nx11x_pump();
}
