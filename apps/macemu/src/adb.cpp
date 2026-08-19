/*
 *  adb.cpp - ADB emulation (mouse/keyboard)
 *
 *  Basilisk II (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SEE ALSO
 *    Inside Macintosh: Devices, chapter 5 "ADB Manager"
 *    Technote HW 01: "ADB - The Untold Story: Space Aliens Ate My Mouse"
 */

#include <stdlib.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "emul_op.h"
#include "main.h"
#include "prefs.h"
#include "video.h"
#include "adb.h"

#ifdef POWERPC_ROM
#include "thunks.h"
#endif

#define DEBUG 0
#include "debug.h"


// Global variables
static int mouse_x = 0, mouse_y = 0;							// Mouse position
static int old_mouse_x = 0, old_mouse_y = 0;
static bool mouse_button[3] = {false, false, false};			// Mouse button states
static bool old_mouse_button[3] = {false, false, false};
static bool relative_mouse = false;

static uint8 key_states[16];				// Key states (Mac keycodes)
#define MATRIX(code) (key_states[code >> 3] & (1 << (~code & 7)))

// Keyboard event buffer (Mac keycodes with up/down flag)
const int KEY_BUFFER_SIZE = 16;
static uint8 key_buffer[KEY_BUFFER_SIZE];
static unsigned int key_read_ptr = 0, key_write_ptr = 0;

static uint8 mouse_reg_3[2] = {0x63, 0x01};	// Mouse ADB register 3

static uint8 key_reg_2[2] = {0xff, 0xff};	// Keyboard ADB register 2
static uint8 key_reg_3[2] = {0x62, 0x05};	// Keyboard ADB register 3

static uint8 m_keyboard_type = 0x05;

// Pending button edges (mouse_lock protected).  One entry per state
// transition, holding the full button bitmap of that moment.  Dispatching
// ADB packets from a "current state vs last sent" compare can only see
// the final state, so a quick UP+DOWN pair (touch tip bounce) merged into
// one interrupt dispatch vanished entirely and left the guest stuck on a
// phantom press.  An explicit edge queue keeps every transition, exactly
// like minivmac's MyEvtQ.
#define BTN_Q_SIZE 32
static uint8 btn_q[BTN_Q_SIZE];
static int btn_q_head = 0, btn_q_tail = 0;		// head = next to send, tail = next free
static uint8 btn_queued_state = 0;				// button bitmap of the last queued edge

static void queue_btn_edge(uint8 st)
{
	btn_q[btn_q_tail] = st;
	btn_q_tail = (btn_q_tail + 1) % BTN_Q_SIZE;
	if (btn_q_tail == btn_q_head)				// full: drop the oldest edge
		btn_q_head = (btn_q_head + 1) % BTN_Q_SIZE;
	btn_queued_state = st;
}

// ADB mouse motion lock (for platforms that use separate input thread)
static B2_mutex *mouse_lock;


/*
 *  Initialize ADB emulation
 */

void ADBInit(void)
{
	mouse_lock = B2_create_mutex();
	m_keyboard_type = (uint8)PrefsFindInt32("keyboardtype");
	key_reg_3[1] = m_keyboard_type;
}


/*
 *  Exit ADB emulation
 */

void ADBExit(void)
{
	if (mouse_lock) {
		B2_delete_mutex(mouse_lock);
		mouse_lock = NULL;
	}
}


/*
 *  ADBOp() replacement
 */

void ADBOp(uint8 op, uint8 *data)
{
	D(bug("ADBOp op %02x, data %02x %02x %02x\n", op, data[0], data[1], data[2]));

	// ADB reset?
	if ((op & 0x0f) == 0) {
		mouse_reg_3[0] = 0x63;
		mouse_reg_3[1] = 0x01;
		key_reg_2[0] = 0xff;
		key_reg_2[1] = 0xff;
		key_reg_3[0] = 0x62;
		key_reg_3[1] = m_keyboard_type;
		return;
	}

	// Cut op into fields
	uint8 adr = op >> 4;
	uint8 cmd = (op >> 2) & 3;
	uint8 reg = op & 3;

	// Check which device was addressed and act accordingly
	if (adr == (mouse_reg_3[0] & 0x0f)) {

		// Mouse
		if (cmd == 2) {

			// Listen
			switch (reg) {
				case 3:		// Address/HandlerID
					if (data[2] == 0xfe)			// Change address
						mouse_reg_3[0] = (mouse_reg_3[0] & 0xf0) | (data[1] & 0x0f);
					else if (data[2] == 1 || data[2] == 2 || data[2] == 4)	// Change device handler ID
						mouse_reg_3[1] = data[2];
					else if (data[2] == 0x00)		// Change address and enable bit
						mouse_reg_3[0] = (mouse_reg_3[0] & 0xd0) | (data[1] & 0x2f);
					break;
			}

		} else if (cmd == 3) {

			// Talk
			switch (reg) {
				case 1:		// Extended mouse protocol
					data[0] = 8;
					data[1] = 'a';				// Identifier
					data[2] = 'p';
					data[3] = 'p';
					data[4] = 'l';
					data[5] = 300 >> 8;			// Resolution (dpi)
					data[6] = 300 & 0xff;
					data[7] = 1;				// Class (mouse)
					data[8] = 3;				// Number of buttons
					break;
				case 3:		// Address/HandlerID
					data[0] = 2;
					data[1] = mouse_reg_3[0] & 0xf0 | (rand() & 0x0f);
					data[2] = mouse_reg_3[1];
					break;
				default:
					data[0] = 0;
					break;
			}
		}
		D(bug(" mouse reg 3 %02x%02x\n", mouse_reg_3[0], mouse_reg_3[1]));

	} else if (adr == (key_reg_3[0] & 0x0f)) {

		// Keyboard
		if (cmd == 2) {

			// Listen
			switch (reg) {
				case 2:		// LEDs/Modifiers
					key_reg_2[0] = data[1];
					key_reg_2[1] = data[2];
					break;
				case 3:		// Address/HandlerID
					if (data[2] == 0xfe)			// Change address
							key_reg_3[0] = (key_reg_3[0] & 0xf0) | (data[1] & 0x0f);
					else if (data[2] == 0x00)		// Change address and enable bit
						key_reg_3[0] = (key_reg_3[0] & 0xd0) | (data[1] & 0x2f);
					break;
			}

		} else if (cmd == 3) {

			// Talk
			switch (reg) {
				case 2: {	// LEDs/Modifiers
					uint8 reg2hi = 0xff;
					uint8 reg2lo = key_reg_2[1] | 0xf8;
					if (MATRIX(0x6b))	// Scroll Lock
						reg2lo &= ~0x40;
					if (MATRIX(0x47))	// Num Lock
						reg2lo &= ~0x80;
					if (MATRIX(0x37))	// Command
						reg2hi &= ~0x01;
					if (MATRIX(0x3a))	// Option
						reg2hi &= ~0x02;
					if (MATRIX(0x38))	// Shift
						reg2hi &= ~0x04;
					if (MATRIX(0x36))	// Control
						reg2hi &= ~0x08;
					if (MATRIX(0x39))	// Caps Lock
						reg2hi &= ~0x20;
					if (MATRIX(0x75))	// Delete
						reg2hi &= ~0x40;
					data[0] = 2;
					data[1] = reg2hi;
					data[2] = reg2lo;
					break;
				}
				case 3:		// Address/HandlerID
					data[0] = 2;
					data[1] = key_reg_3[0] & 0xf0 | (rand() & 0x0f);
					data[2] = key_reg_3[1];
					break;
				default:
					data[0] = 0;
					break;
			}
		}
		D(bug(" keyboard reg 3 %02x%02x\n", key_reg_3[0], key_reg_3[1]));

	} else												// Unknown address
		if (cmd == 3)
			data[0] = 0;								// Talk: 0 bytes of data
}


/*
 *  Mouse was moved (x/y are absolute or relative, depending on ADBSetRelMouseMode())
 */

void ADBMouseMoved(int x, int y)
{
	B2_lock_mutex(mouse_lock);
	if (relative_mouse) {
		mouse_x += x; mouse_y += y;
	} else {
		mouse_x = x; mouse_y = y;
	}
	B2_unlock_mutex(mouse_lock);
	SetInterruptFlag(INTFLAG_ADB);
	// Inject right away: cursor latency stacks up along the host mouse ->
	// ADB -> guest draw -> convert -> present chain, and deferring to the
	// 60Hz tick would add up to a full frame for nothing.  Waking the 68k
	// core is cheap (an atomic flag plus at most one sem_post), so even a
	// 1000Hz gaming mouse costs a fraction of a percent of CPU here.
	TriggerInterrupt();
}


/*
 *  Mouse button pressed
 */

void ADBMouseDown(int button)
{
	uint8 q_before = 0;
	bool forced = false;
	B2_lock_mutex(mouse_lock);
	mouse_button[button] = true;
	if (!relative_mouse) {
		uint8 st = 0;
		if (mouse_button[0]) st |= 1;
		if (mouse_button[1]) st |= 2;
		if (mouse_button[2]) st |= 4;
		q_before = btn_queued_state;
		// minivmac semantics: a DOWN always delivers a complete press
		// edge.  If this button is already queued pressed (repeated DOWN,
		// or a lost UP), force a release edge first so a stuck guest
		// state recovers — MyMouseButtonSet(false) before (true).
		forced = (btn_queued_state & (1 << button)) != 0;
		if (forced)
			queue_btn_edge(st & ~(1 << button));
		queue_btn_edge(st);
	}
	B2_unlock_mutex(mouse_lock);
	printf("dbg adb: DOWN b=%d q=%d forced=%d\n", button, q_before, forced);
	SetInterruptFlag(INTFLAG_ADB);
	TriggerInterrupt();
}


/*
 *  Mouse button released
 */

void ADBMouseUp(int button)
{
	uint8 q_before = 0;
	bool queued = false;
	B2_lock_mutex(mouse_lock);
	mouse_button[button] = false;
	if (!relative_mouse) {
		uint8 st = 0;
		if (mouse_button[0]) st |= 1;
		if (mouse_button[1]) st |= 2;
		if (mouse_button[2]) st |= 4;
		q_before = btn_queued_state;
		// Edge dedup like minivmac's MyMouseButtonSet: a repeated UP
		// (button already queued released) queues nothing
		queued = (btn_queued_state & (1 << button)) != 0;
		if (queued)
			queue_btn_edge(st);
	}
	B2_unlock_mutex(mouse_lock);
	printf("dbg adb: UP b=%d q=%d queued=%d\n", button, q_before, queued);
	SetInterruptFlag(INTFLAG_ADB);
	TriggerInterrupt();
}


/*
 *  Set mouse mode (absolute or relative)
 */

void ADBSetRelMouseMode(bool relative)
{
	if (relative_mouse != relative) {
		relative_mouse = relative;
		mouse_x = mouse_y = 0;
	}
}


/*
 *  Key pressed ("code" is the Mac key code)
 */

void ADBKeyDown(int code)
{
	// Add keycode to buffer
	key_buffer[key_write_ptr] = code;
	key_write_ptr = (key_write_ptr + 1) % KEY_BUFFER_SIZE;

	// Set key in matrix
	key_states[code >> 3] |= (1 << (~code & 7));

	// Trigger interrupt
	SetInterruptFlag(INTFLAG_ADB);
	TriggerInterrupt();
}


/*
 *  Key released ("code" is the Mac key code)
 */

void ADBKeyUp(int code)
{
	// Add keycode to buffer
	key_buffer[key_write_ptr] = code | 0x80;	// Key-up flag
	key_write_ptr = (key_write_ptr + 1) % KEY_BUFFER_SIZE;

	// Clear key in matrix
	key_states[code >> 3] &= ~(1 << (~code & 7));

	// Trigger interrupt
	SetInterruptFlag(INTFLAG_ADB);
	TriggerInterrupt();
}


/*
 *  ADB interrupt function (executed as part of 60Hz interrupt)
 */

void ADBInterrupt(void)
{
	M68kRegisters r;

	// Return if ADB is not initialized
	uint32 adb_base = ReadMacInt32(0xcf8);
	if (!adb_base || adb_base == 0xffffffff) {
		// NOTE: the INTFLAG_ADB was already cleared by the dispatcher, so
		// any queued button edge stays stuck here until the next input
		// event re-arms the flag — a release edge stuck this way leaves
		// the guest with a pressed mouse button.
		printf("dbg adb: IRQ SKIP base=%08x\n", adb_base);
		return;
	}
	uint32 tmp_data = adb_base + 0x163;	// Temporary storage for faked ADB data

	// Get mouse state
	B2_lock_mutex(mouse_lock);
	int mx = mouse_x;
	int my = mouse_y;
	if (relative_mouse)
		mouse_x = mouse_y = 0;
	bool mb[3] = {mouse_button[0], mouse_button[1], mouse_button[2]};
	B2_unlock_mutex(mouse_lock);

	uint32 key_base = adb_base + 4;
	uint32 mouse_base = adb_base + 16;

	if (relative_mouse) {

		// Mouse movement (relative) and buttons
		if (mx != 0 || my != 0 || mb[0] != old_mouse_button[0] || mb[1] != old_mouse_button[1] || mb[2] != old_mouse_button[2]) {

			// Call mouse ADB handler
			if (mouse_reg_3[1] == 4) {
				// Extended mouse protocol
				WriteMacInt8(tmp_data, 3);
				WriteMacInt8(tmp_data + 1, (my & 0x7f) | (mb[0] ? 0 : 0x80));
				WriteMacInt8(tmp_data + 2, (mx & 0x7f) | (mb[1] ? 0 : 0x80));
				WriteMacInt8(tmp_data + 3, ((my >> 3) & 0x70) | ((mx >> 7) & 0x07) | (mb[2] ? 0x08 : 0x88));
			} else {
				// 100/200 dpi mode
				WriteMacInt8(tmp_data, 2);
				WriteMacInt8(tmp_data + 1, (my & 0x7f) | (mb[0] ? 0 : 0x80));
				WriteMacInt8(tmp_data + 2, (mx & 0x7f) | (mb[1] ? 0 : 0x80));
			}	
			r.a[0] = tmp_data;
			r.a[1] = ReadMacInt32(mouse_base);
			r.a[2] = ReadMacInt32(mouse_base + 4);
			r.a[3] = adb_base;
			r.d[0] = (mouse_reg_3[0] << 4) | 0x0c;	// Talk 0
			Execute68k(r.a[1], &r);

			old_mouse_button[0] = mb[0];
			old_mouse_button[1] = mb[1];
			old_mouse_button[2] = mb[2];
		}

	} else {

		// Update mouse position (absolute)
		if (mx != old_mouse_x || my != old_mouse_y) {
#ifdef POWERPC_ROM
			static const uint8 proc_template[] = {
				0x2f, 0x08,		// move.l a0,-(sp)
				0x2f, 0x00,		// move.l d0,-(sp)
				0x2f, 0x01,		// move.l d1,-(sp)
				0x70, 0x01,		// moveq #1,d0 (MoveTo)
				0xaa, 0xdb,		// CursorDeviceDispatch
				M68K_RTS >> 8, M68K_RTS & 0xff
			};
			BUILD_SHEEPSHAVER_PROCEDURE(proc);
			r.a[0] = ReadMacInt32(mouse_base + 4);
			r.d[0] = mx;
			r.d[1] = my;
			Execute68k(proc, &r);
#else
			WriteMacInt16(0x82a, mx);
			WriteMacInt16(0x828, my);
			WriteMacInt16(0x82e, mx);
			WriteMacInt16(0x82c, my);
			WriteMacInt8(0x8ce, ReadMacInt8(0x8cf));	// CrsrCouple -> CrsrNew
#endif
			old_mouse_x = mx;
			old_mouse_y = my;
		}

		// Send ONE queued button edge per interrupt dispatch.  Upstream
		// never sends more than one mouse ADB packet per ADBInterrupt and
		// the guest's ADB service task is written for that cadence: a
		// quick tap queues press+release back to back, and draining the
		// whole burst into back-to-back Execute68k calls inside one
		// dispatch window made the guest drop the release (finger up,
		// Mac still dragging).  Leftover edges stay queued; re-arm the
		// flag and self-trigger so the next 68k checkpoint dispatches the
		// next edge microseconds later — a tap's press and release are
		// two independent service calls, exactly like real hardware.
		uint8 edge = 0;
		bool have_edge = false;
		bool more_edges = false;
		B2_lock_mutex(mouse_lock);
		if (btn_q_head != btn_q_tail) {
			edge = btn_q[btn_q_head];
			btn_q_head = (btn_q_head + 1) % BTN_Q_SIZE;
			have_edge = true;
			more_edges = (btn_q_head != btn_q_tail);
		}
		B2_unlock_mutex(mouse_lock);

		if (have_edge) {
			bool b0 = (edge & 1) != 0;
			bool b1 = (edge & 2) != 0;
			bool b2 = (edge & 4) != 0;
			printf("dbg adb: IRQ edge=%d more=%d mx=%d my=%d\n", edge, more_edges, mx, my);

			// Call mouse ADB handler
			if (mouse_reg_3[1] == 4) {
				// Extended mouse protocol
				WriteMacInt8(tmp_data, 3);
				WriteMacInt8(tmp_data + 1, b0 ? 0 : 0x80);
				WriteMacInt8(tmp_data + 2, b1 ? 0 : 0x80);
				WriteMacInt8(tmp_data + 3, b2 ? 0x08 : 0x88);
			} else {
				// 100/200 dpi mode
				WriteMacInt8(tmp_data, 2);
				WriteMacInt8(tmp_data + 1, b0 ? 0 : 0x80);
				WriteMacInt8(tmp_data + 2, b1 ? 0 : 0x80);
			}
			r.a[0] = tmp_data;
			r.a[1] = ReadMacInt32(mouse_base);
			r.a[2] = ReadMacInt32(mouse_base + 4);
			r.a[3] = adb_base;
			r.d[0] = (mouse_reg_3[0] << 4) | 0x0c;	// Talk 0
			Execute68k(r.a[1], &r);

			if (more_edges) {
				// Same-thread trigger is race-free (SPCFLAGS_SET from
				// the 68k thread itself) and fires at the very next
				// instruction-boundary checkpoint.
				SetInterruptFlag(INTFLAG_ADB);
				TriggerInterrupt();
			}
		}
	}

	// Process accumulated keyboard events
	while (key_read_ptr != key_write_ptr) {

		// Read keyboard event
		uint8 mac_code = key_buffer[key_read_ptr];
		key_read_ptr = (key_read_ptr + 1) % KEY_BUFFER_SIZE;

		// Call keyboard ADB handler
		WriteMacInt8(tmp_data, 2);
		WriteMacInt8(tmp_data + 1, mac_code);
		WriteMacInt8(tmp_data + 2, mac_code == 0x7f ? 0x7f : 0xff);	// Power key is special
		r.a[0] = tmp_data;
		r.a[1] = ReadMacInt32(key_base);
		r.a[2] = ReadMacInt32(key_base + 4);
		r.a[3] = adb_base;
		r.d[0] = (key_reg_3[0] << 4) | 0x0c;	// Talk 0
		Execute68k(r.a[1], &r);
	}

	// Clear temporary data
	WriteMacInt32(tmp_data, 0);
	WriteMacInt32(tmp_data + 4, 0);
}
