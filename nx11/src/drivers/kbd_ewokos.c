/*
 * EwokOS keyboard driver.
 *
 * Keys reach an EwokOS application as XEVT_IM events routed by the xserver to
 * the focused window.  The bridge queues the raw im payload (key_code, the
 * already shift/ctrl translated value, and the press/release state); this
 * driver translates it into MWKEY codes and keeps the MWKEYMOD latch.
 *
 * EwokOS delivers printable keys as ASCII in key_code and the shifted/controlled
 * character in value, so value is what Nano-X wants for those.  Everything else
 * (arrows, function keys, modifiers, home/end/page) is a private key code, part
 * of it defined in ewoksys/keydef.h and part of it only by convention.
 */
#include <stdio.h>
#include <ewoksys/keydef.h>
#include "device.h"
#include "ewokos.h"

/* EwokOS key codes ewoksys/keydef.h does not define (same values the rest of
   the tree - minivmac, macemu - uses)*/
#define EWOK_KEY_CAPSLOCK	0xA1
#define EWOK_KEY_ALT		0xA5
#define EWOK_KEY_RCTRL		0xA6
#define EWOK_KEY_RALT		0xA7
#define EWOK_KEY_INSERT		0xF3
#define EWOK_KEY_PAGEUP		0xF4
#define EWOK_KEY_PAGEDOWN	0xF5
#define EWOK_KEY_F1			0xF6
#define EWOK_KEY_F2			0xF7
#define EWOK_KEY_F3			0xF8
#define EWOK_KEY_F4			0xF9
#define EWOK_KEY_F5			0xFA
#define EWOK_KEY_F6			0xFB
#define EWOK_KEY_F7			0xFC
#define EWOK_KEY_F8			0xFD
#define EWOK_KEY_F9			0xFE
#define EWOK_KEY_F10		0xFF
#define EWOK_KEY_F11		0x100
#define EWOK_KEY_F12		0x101

#define KEY_QUEUE		64	/* power of two*/

static int kq_code[KEY_QUEUE];
static int kq_value[KEY_QUEUE];
static int kq_shift[KEY_QUEUE];
static int kq_ctrl[KEY_QUEUE];
static int kq_press[KEY_QUEUE];
static int kq_head, kq_tail, kq_count;

/* modifier latch reported to the server*/
static MWKEYMOD curmodifiers;

void
nx11x_key_push(int key_code, int value, int shift, int ctrl, int press)
{
	if (kq_count >= KEY_QUEUE)
		return;

	kq_code[kq_tail] = key_code;
	kq_value[kq_tail] = value;
	kq_shift[kq_tail] = shift;
	kq_ctrl[kq_tail] = ctrl;
	kq_press[kq_tail] = press;
	kq_tail = (kq_tail + 1) & (KEY_QUEUE - 1);
	kq_count++;
}

int
nx11x_key_pop(int *key_code, int *value, int *shift, int *ctrl, int *press)
{
	if (kq_count == 0)
		return 0;

	*key_code = kq_code[kq_head];
	*value = kq_value[kq_head];
	*shift = kq_shift[kq_head];
	*ctrl = kq_ctrl[kq_head];
	*press = kq_press[kq_head];
	kq_head = (kq_head + 1) & (KEY_QUEUE - 1);
	kq_count--;
	return 1;
}

int
nx11x_key_pending(void)
{
	return kq_count;
}

/*
 * Non-printable EwokOS key code to MWKEY.  Returns 0 when the code is not a
 * known special key, in which case the caller falls back to the translated
 * character in value.
 */
static MWKEY
ewokos_special_key(int key_code)
{
	switch (key_code) {
	case KEY_UP:			return MWKEY_UP;
	case KEY_DOWN:			return MWKEY_DOWN;
	case KEY_LEFT:			return MWKEY_LEFT;
	case KEY_RIGHT:			return MWKEY_RIGHT;
	case KEY_HOME:			return MWKEY_HOME;
	case KEY_END:			return MWKEY_END;
	case KEY_TAB:			return MWKEY_TAB;
	case KEY_ENTER:			return MWKEY_ENTER;
	case KEY_ESC:			return MWKEY_ESCAPE;
	case KEY_BACKSPACE:
	/* hid_keybd's down map emits '\b' for backspace, only the on-screen
	   vkey sends KEY_BACKSPACE - accept both like the rest of the tree*/
	case CONSOLE_LEFT:		return MWKEY_BACKSPACE;
	case EWOK_KEY_INSERT:	return MWKEY_INSERT;
	case EWOK_KEY_PAGEUP:	return MWKEY_PAGEUP;
	case EWOK_KEY_PAGEDOWN:	return MWKEY_PAGEDOWN;
	case KEY_LSHIFT:		return MWKEY_LSHIFT;
	case KEY_RSHIFT:		return MWKEY_RSHIFT;
	case KEY_CTRL:			return MWKEY_LCTRL;
	case EWOK_KEY_RCTRL:	return MWKEY_RCTRL;
	case EWOK_KEY_ALT:		return MWKEY_LALT;
	case EWOK_KEY_RALT:		return MWKEY_RALT;
	case EWOK_KEY_CAPSLOCK:	return MWKEY_CAPSLOCK;
	case KEY_POWER:			return MWKEY_POWER;
	case EWOK_KEY_F1:		return MWKEY_F1;
	case EWOK_KEY_F2:		return MWKEY_F2;
	case EWOK_KEY_F3:		return MWKEY_F3;
	case EWOK_KEY_F4:		return MWKEY_F4;
	case EWOK_KEY_F5:		return MWKEY_F5;
	case EWOK_KEY_F6:		return MWKEY_F6;
	case EWOK_KEY_F7:		return MWKEY_F7;
	case EWOK_KEY_F8:		return MWKEY_F8;
	case EWOK_KEY_F9:		return MWKEY_F9;
	case EWOK_KEY_F10:		return MWKEY_F10;
	case EWOK_KEY_F11:		return MWKEY_F11;
	case EWOK_KEY_F12:		return MWKEY_F12;
	/* gamepads report their buttons as key codes; map the two action
	   buttons onto the keys an application is most likely to bind*/
	case JOYSTICK_A:		return MWKEY_ENTER;
	case JOYSTICK_B:		return MWKEY_ESCAPE;
	case JOYSTICK_START:	return MWKEY_MENU;
	}
	return 0;
}

/* keep the modifier latch in step with the key that just went up or down*/
static void
ewokos_update_modifiers(int key_code, int shift, int ctrl, int press)
{
	if (key_code == KEY_LSHIFT || shift == KEY_LSHIFT) {
		if (press) curmodifiers |= MWKMOD_LSHIFT;
		else curmodifiers &= ~MWKMOD_LSHIFT;
	}
	if (key_code == KEY_RSHIFT || shift == KEY_RSHIFT) {
		if (press) curmodifiers |= MWKMOD_RSHIFT;
		else curmodifiers &= ~MWKMOD_RSHIFT;
	}
	if (key_code == KEY_CTRL) {
		if (press) curmodifiers |= MWKMOD_LCTRL;
		else curmodifiers &= ~MWKMOD_LCTRL;
	}
	if (key_code == EWOK_KEY_RCTRL) {
		if (press) curmodifiers |= MWKMOD_RCTRL;
		else curmodifiers &= ~MWKMOD_RCTRL;
	}
	if (key_code == EWOK_KEY_ALT) {
		if (press) curmodifiers |= MWKMOD_LALT;
		else curmodifiers &= ~MWKMOD_LALT;
	}
	if (key_code == EWOK_KEY_RALT) {
		if (press) curmodifiers |= MWKMOD_RALT;
		else curmodifiers &= ~MWKMOD_RALT;
	}
	if (key_code == EWOK_KEY_CAPSLOCK && press)
		curmodifiers ^= MWKMOD_CAPS;
	if (ctrl)
		curmodifiers |= MWKMOD_LCTRL;
}

static int
ewokos_kbd_open(KBDDEVICE *pkd)
{
	(void)pkd;
	curmodifiers = MWKMOD_NONE;
	kq_head = kq_tail = kq_count = 0;
	return DRIVER_OKNOTFILEDESC;	/* no file descriptor, input comes from xwin events*/
}

static void
ewokos_kbd_close(void)
{
}

static void
ewokos_kbd_getmodifierinfo(MWKEYMOD *modifiers, MWKEYMOD *pcurmodifiers)
{
	if (modifiers)
		*modifiers = MWKMOD_SHIFT|MWKMOD_CTRL|MWKMOD_ALT|MWKMOD_CAPS;
	if (pcurmodifiers)
		*pcurmodifiers = curmodifiers;
}

static int
ewokos_kbd_poll(void)
{
	/* a pending server quit (main window closed) must look like readable input:
	   GsCheckKeyboardEvent() bails out at its "Poll() == 0" gate, so with no key
	   actually queued it would never call ewokos_kbd_read() and the KBD_QUIT that
	   shuts the server down would be lost - the closed window would linger */
	if (nx11x_quit_peek())
		return kq_count + 1;
	return kq_count;
}

static int
ewokos_kbd_read(MWKEY *buf, MWKEYMOD *modifiers, MWSCANCODE *scancode)
{
	int key_code, value, shift, ctrl, press;
	MWKEY mwkey;

	/* the x context went away (main window closed): shut the server down
	   the same way the quit key does*/
	if (nx11x_quit_pending())
		return KBD_QUIT;

	if (!nx11x_key_pop(&key_code, &value, &shift, &ctrl, &press))
		return KBD_NODATA;

	ewokos_update_modifiers(key_code, shift, ctrl, press);

	mwkey = ewokos_special_key(key_code);
	if (mwkey == 0) {
		/* printable: the input method already applied shift and ctrl*/
		if ((value >= 32 && value < 127) || value == 8 || value == 9 ||
				value == 13 || value == 27 || value == 127)
			mwkey = (MWKEY)value;
		else if (key_code >= 32 && key_code < 127)
			mwkey = (MWKEY)key_code;
		else
			return KBD_NODATA;	/* unmappable, drop it*/
	}

	if (buf)
		*buf = mwkey;
	if (modifiers)
		*modifiers = curmodifiers;
	if (scancode)
		*scancode = (MWSCANCODE)key_code;

	return press ? KBD_KEYPRESS : KBD_KEYRELEASE;
}

KBDDEVICE kbddev = {
	ewokos_kbd_open,
	ewokos_kbd_close,
	ewokos_kbd_getmodifierinfo,
	ewokos_kbd_read,
	ewokos_kbd_poll
};
