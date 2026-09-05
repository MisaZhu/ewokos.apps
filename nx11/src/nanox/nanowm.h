#ifndef _NANOWM_H
#define _NANOWM_H
/*
 * Minimal replacement for the upstream nanowm.h.
 *
 * NX11 has no built-in window manager (NANOWM=0): every top-level nano-X
 * window *is* an EwokOS xwin window, and the EwokOS xserver draws the
 * frame/caption and owns the desktop stacking order (see nanox/win_ewokos.c).
 *
 * What is still needed from the original header is the GrGetSysColor()
 * colour-scheme table, which nanox/nxdraw.c defines and nanox/srvfunc.c
 * reads, plus the window-decoration metrics nanox/nxpaintnc.c draws with.
 * Everything else upstream declared here was the CLIENT/WINDOW/PROP
 * window-manager state, only referenced inside "#if NANOWM" blocks.
 */

#include "mwconfig.h"
#include "nano-X.h"

/* one entry per GR_COLOR_xxx index in nano-X.h*/
#define MAXSYSCOLORS		(GR_COLOR_WINDOWFRAMELT + 1)

/*
 * nxdraw.c selects exactly one scheme: SCHEME_NUKLEAR/SCHEME_NUK16 are
 * tested with #if, SCHEME_TAN/SCHEME_WINSTD/SCHEME_OLD with #ifdef, so the
 * latter three must stay undefined.
 */
#if NUKLEARUI
#define SCHEME_NUKLEAR	1	/* Nuklear dark scheme*/
#else
#define SCHEME_NUKLEAR	0
#endif
#define SCHEME_NUK16	0	/* Nuklear scheme for 16bpp*/

extern const GR_COLOR nxSysColors[MAXSYSCOLORS];

/* window decoration metrics used by nxPaintNCArea(): caption height is
   2x padding (4) + font ascent/descent (CYTEXTBASE)*/
#define CYTEXTBASE	11
#define CYCAPTION	19
#define CXCLOSEBOX	12
#define CYCLOSEBOX	12

#endif /* _NANOWM_H*/
