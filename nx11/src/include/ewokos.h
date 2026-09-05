#ifndef _EWOKOS_H
#define _EWOKOS_H
/*
 * EwokOS backend interface, shared by the Microwindows screen/mouse/keyboard
 * drivers in src/drivers and the xwin window bridge in src/nanox.
 *
 * Microwindows draws the whole desktop into a single screen-sized framebuffer
 * while an EwokOS application owns one xwin window per top-level window.  The
 * bridge therefore keeps a "virtual screen" the size of the desktop, mirrors
 * every dirty rectangle into the workspace of each xwin it lands in (through
 * graph_blt, so g2d performs the copy) and feeds xwin mouse/IM events back as
 * the raw EwokOS values the drivers below translate.
 *
 * Each xwin is created at the frame rectangle of its Nano-X window, so virtual
 * screen coordinates are EwokOS desktop coordinates and no translation happens
 * anywhere - not for drawing and not for input.
 *
 * GR_WINDOW is passed as void * so the drivers can include this header without
 * pulling in the server-private serv.h; graph_t is left out for the same reason.
 */
#include "mwtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* scr_ewokos.c <-> bridge: the virtual screen */
int		nx11x_screen_init(int *pxres, int *pyres);
void	nx11x_screen_term(void);
void *	nx11x_screen_graph(void);
void	nx11x_update(MWCOORD x, MWCOORD y, MWCOORD width, MWCOORD height);

/* serv*.c <-> bridge: window lifetime, geometry and stacking */
void	nx11x_win_realize(void *wp);
void	nx11x_win_unrealize(void *wp, int temp_unmap);
void	nx11x_win_destroy(void *wp);
void	nx11x_win_moved(void *wp);
void	nx11x_win_resized(void *wp);
void	nx11x_win_raised(void *wp);
void	nx11x_win_focused(void *wp);
void	nx11x_win_props(void *wp);

/* srvmain.c <-> bridge: replaces GsSelect's select() loop */
int		nx11x_pump(void);
void	nx11x_select(int timeout);

/* bridge <-> mou_ewokos.c: raw EwokOS mouse event queue */
void	nx11x_mouse_push(int x, int y, int button, int state);
int		nx11x_mouse_pop(int *x, int *y, int *button, int *state);
int		nx11x_mouse_pending(void);

/* bridge <-> kbd_ewokos.c: raw EwokOS IM event queue */
void	nx11x_key_push(int key_code, int value, int shift, int ctrl, int press);
int		nx11x_key_pop(int *key_code, int *value, int *shift, int *ctrl, int *press);
int		nx11x_key_pending(void);

/* set when the x context was terminated (main window closed): kbd_ewokos.c
   turns it into KBD_QUIT, which makes the server shut down cleanly */
int		nx11x_quit_pending(void);
/* non-consuming peek at the same quit flag, for the keyboard Poll() gate */
int		nx11x_quit_peek(void);

#ifdef __cplusplus
}
#endif
#endif /* _EWOKOS_H */
