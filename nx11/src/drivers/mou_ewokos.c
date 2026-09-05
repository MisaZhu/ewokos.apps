/*
 * EwokOS mouse driver.
 *
 * The xwin bridge (nanox/win_ewokos.c) queues the raw EwokOS mouse events it
 * receives - absolute desktop coordinates plus a MOUSE_STATE_* / MOUSE_BUTTON_*
 * pair - and this driver turns them into the MWBUTTON_* mask and MOUSE_ABSPOS
 * position GdReadMouse() hands to GsHandleMouseStatus().
 *
 * Because the virtual screen is the desktop and every xwin sits exactly on its
 * Nano-X window's frame rectangle, the coordinates need no adjustment at all.
 */
#include <stdio.h>
#include <mouse/mouse.h>
#include "device.h"
#include "ewokos.h"

#define MOUSE_QUEUE		64	/* power of two*/

static int mq_x[MOUSE_QUEUE];
static int mq_y[MOUSE_QUEUE];
static int mq_button[MOUSE_QUEUE];
static int mq_state[MOUSE_QUEUE];
static int mq_head, mq_tail, mq_count;

/* buttons currently held down, in MWBUTTON_* bits*/
static int curbuttons;

void
nx11x_mouse_push(int x, int y, int button, int state)
{
	if (mq_count >= MOUSE_QUEUE)
		return;					/* drop, the server caps its own pool at 128 anyway*/

	mq_x[mq_tail] = x;
	mq_y[mq_tail] = y;
	mq_button[mq_tail] = button;
	mq_state[mq_tail] = state;
	mq_tail = (mq_tail + 1) & (MOUSE_QUEUE - 1);
	mq_count++;
}

int
nx11x_mouse_pop(int *x, int *y, int *button, int *state)
{
	if (mq_count == 0)
		return 0;

	*x = mq_x[mq_head];
	*y = mq_y[mq_head];
	*button = mq_button[mq_head];
	*state = mq_state[mq_head];
	mq_head = (mq_head + 1) & (MOUSE_QUEUE - 1);
	mq_count--;
	return 1;
}

int
nx11x_mouse_pending(void)
{
	return mq_count;
}

static int
ewokos_mouse_open(MOUSEDEVICE *pd)
{
	(void)pd;
	curbuttons = 0;
	mq_head = mq_tail = mq_count = 0;
	return DRIVER_OKNOTFILEDESC;	/* no file descriptor, input comes from xwin events*/
}

static void
ewokos_mouse_close(void)
{
}

static int
ewokos_mouse_getbuttoninfo(void)
{
	return MWBUTTON_L|MWBUTTON_M|MWBUTTON_R|MWBUTTON_SCROLLUP|MWBUTTON_SCROLLDN;
}

static void
ewokos_mouse_getdefaultaccel(int *pscale, int *pthresh)
{
	*pscale = 3;
	*pthresh = 5;
}

static int
ewokos_mouse_poll(void)
{
	return nx11x_mouse_pending();
}

static int
ewokos_mouse_read(MWCOORD *px, MWCOORD *py, MWCOORD *pz, int *pb)
{
	int x, y, button, state;
	int scroll = 0;

	if (!nx11x_mouse_pop(&x, &y, &button, &state))
		return MOUSE_NODATA;

	switch (state) {
	case MOUSE_STATE_DOWN:
		if (button == MOUSE_BUTTON_LEFT)
			curbuttons |= MWBUTTON_L;
		else if (button == MOUSE_BUTTON_RIGHT)
			curbuttons |= MWBUTTON_R;
		else if (button == MOUSE_BUTTON_MID)
			curbuttons |= MWBUTTON_M;
		break;

	case MOUSE_STATE_UP:
		if (button == MOUSE_BUTTON_LEFT)
			curbuttons &= ~MWBUTTON_L;
		else if (button == MOUSE_BUTTON_RIGHT)
			curbuttons &= ~MWBUTTON_R;
		else if (button == MOUSE_BUTTON_MID)
			curbuttons &= ~MWBUTTON_M;
		break;

	case MOUSE_STATE_MOVE:
	case MOUSE_STATE_DRAG:
		/* EwokOS reports the wheel as a MOVE carrying a scroll button
		   code; that bit may only be set for this single sample or the
		   server would keep re-delivering the same notch.*/
		if (button == MOUSE_BUTTON_SCROLL_UP)
			scroll = MWBUTTON_SCROLLUP;
		else if (button == MOUSE_BUTTON_SCROLL_DOWN)
			scroll = MWBUTTON_SCROLLDN;
		break;

	default:
		/* CLICK and DOUBLE_CLICK are extra events EwokOS pushes after
		   UP; the press and the release were both delivered already and
		   Nano-X synthesises double clicks itself.*/
		return MOUSE_NODATA;
	}

	*px = x;
	*py = y;
	*pz = 0;
	*pb = curbuttons | scroll;
	return MOUSE_ABSPOS;
}

MOUSEDEVICE mousedev = {
	ewokos_mouse_open,
	ewokos_mouse_close,
	ewokos_mouse_getbuttoninfo,
	ewokos_mouse_getdefaultaccel,
	ewokos_mouse_read,
	ewokos_mouse_poll,
	MOUSE_NORMAL					/* flags*/
};
