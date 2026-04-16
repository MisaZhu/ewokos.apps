/*
	MYOSGLUE.c

	Copyright (C) 2012 Paul C. Pratt

	You can redistribute this file and/or modify it under the terms
	of version 2 of the GNU General Public License as published by
	the Free Software Foundation.  You should have received a copy
	of the license along with this file; see the file COPYING.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	license for more details.
*/

/*
	MY Operating System GLUE. (for XWin Library)

	All operating system dependent code for the
	XWin Library should go here.
*/

#include "CNFGRAPI.h"
#include "SYSDEPNS.h"
#include "ENDIANAC.h"

#include "MYOSGLUE.h"

#include "STRCONST.h"
#include <string.h>
#include <ewoksys/klog.h>
#include <ewoksys/cmain.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/keydef.h>
#include <ewoksys/vfs.h>

#ifndef KEY_INSERT
#define KEY_INSERT		0xF3
#endif
#ifndef KEY_PAGEUP
#define KEY_PAGEUP		0xF4
#endif
#ifndef KEY_PAGEDOWN
#define KEY_PAGEDOWN	0xF5
#endif
#ifndef KEY_F1
#define KEY_F1			0xF6
#endif
#ifndef KEY_F2
#define KEY_F2			0xF7
#endif
#ifndef KEY_F3
#define KEY_F3			0xF8
#endif
#ifndef KEY_F4
#define KEY_F4			0xF9
#endif
#ifndef KEY_F5
#define KEY_F5			0xFA
#endif
#ifndef KEY_F6
#define KEY_F6			0xFB
#endif
#ifndef KEY_F7
#define KEY_F7			0xFC
#endif
#ifndef KEY_F8
#define KEY_F8			0xFD
#endif
#ifndef KEY_F9
#define KEY_F9			0xFE
#endif
#ifndef KEY_F10
#define KEY_F10			0xFF
#endif
#ifndef KEY_F11
#define KEY_F11			0x100
#endif
#ifndef KEY_F12
#define KEY_F12			0x101
#endif
#ifndef KEY_CAPSLOCK
#define KEY_CAPSLOCK		0xA1
#endif
#ifndef KEY_SCROLLLOCK
#define KEY_SCROLLLOCK	0xA4
#endif
#ifndef KEY_SHIFT
#define KEY_SHIFT		KEY_LSHIFT
#endif
#ifndef KEY_ALT
#define KEY_ALT			0xA5
#endif
#ifndef KEY_FLAG_RSHIFT
#define KEY_FLAG_RSHIFT	KEY_RSHIFT
#endif
#ifndef KEY_FLAG_RCTRL
#define KEY_FLAG_RCTRL	0xA6
#endif
#ifndef KEY_FLAG_RALT
#define KEY_FLAG_RALT	0xA7
#endif

xwin_t *xwin = NULL;
x_t *x_context = NULL;

graph_t *screen_graph = NULL;
int window_width = 0;
int window_height = 0;
float display_scale = 1.0;
int display_offset_x = 0;
int display_offset_y = 0;

/* --- some simple utilities --- */

GLOBALPROC MyMoveBytes(anyp srcPtr, anyp destPtr, si5b byteCount)
{
	(void) memcpy((char *)destPtr, (char *)srcPtr, byteCount);
}

/* --- control mode and internationalization --- */

#define NeedCell2PlainAsciiMap 1

#include "INTLCHAR.h"

/* --- sending debugging info to file --- */

#if dbglog_HAVE

#define dbglog_ToStdErr 0

#if ! dbglog_ToStdErr
LOCALVAR FILE *dbglog_File = NULL;
#endif

LOCALFUNC blnr dbglog_open0(void)
{
#if dbglog_ToStdErr
	return trueblnr;
#else
	dbglog_File = fopen("dbglog.txt", "w");
	return (NULL != dbglog_File);
#endif
}

LOCALPROC dbglog_write0(char *s, uimr L)
{
#if dbglog_ToStdErr
	(void) fwrite(s, 1, L, stderr);
#else
	if (dbglog_File != NULL) {
		(void) fwrite(s, 1, L, dbglog_File);
	}
#endif
}

LOCALPROC dbglog_close0(void)
{
#if ! dbglog_ToStdErr
	if (dbglog_File != NULL) {
		fclose(dbglog_File);
		dbglog_File = NULL;
	}
#endif
}

#endif

/* --- information about the environment --- */

#define WantColorTransValid 0

#include "COMOSGLU.h"

#include "CONTROLM.h"

/* --- parameter buffers --- */

#if IncludePbufs
LOCALVAR void *PbufDat[NumPbufs];
#endif

#if IncludePbufs
LOCALFUNC tMacErr PbufNewFromPtr(void *p, ui5b count, tPbuf *r)
{
	tDrive i;
	tMacErr err;

	if (! FirstFreePbuf(&i)) {
		free(p);
		err = mnvm_miscErr;
	} else {
		*r = i;
		PbufDat[i] = p;
		PbufNewNotify(i, count);
		err = mnvm_noErr;
	}

	return err;
}
#endif

#if IncludePbufs
GLOBALFUNC tMacErr PbufNew(ui5b count, tPbuf *r)
{
	tMacErr err = mnvm_miscErr;

	void *p = calloc(1, count);
	if (NULL != p) {
		err = PbufNewFromPtr(p, count, r);
	}

	return err;
}
#endif

#if IncludePbufs
GLOBALPROC PbufDispose(tPbuf i)
{
	free(PbufDat[i]);
	PbufDisposeNotify(i);
}
#endif

#if IncludePbufs
LOCALPROC UnInitPbufs(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		if (PbufIsAllocated(i)) {
			PbufDispose(i);
		}
	}
}
#endif

#if IncludePbufs
GLOBALPROC PbufTransfer(ui3p Buffer,
	tPbuf i, ui5r offset, ui5r count, blnr IsWrite)
{
	void *p = ((ui3p)PbufDat[i]) + offset;
	if (IsWrite) {
		(void) memcpy(p, Buffer, count);
	} else {
		(void) memcpy(Buffer, p, count);
	}
}
#endif

/* --- text translation --- */

LOCALPROC NativeStrFromCStr(char *r, char *s)
{
	ui3b ps[ClStrMaxLength];
	int i;
	int L;

	ClStrFromSubstCStr(&L, ps, s);

	for (i = 0; i < L; ++i) {
		r[i] = Cell2PlainAsciiMap[ps[i]];
	}

	r[L] = 0;
}

/* --- drives --- */

#define NotAfileRef NULL

LOCALVAR FILE *Drives[NumDrives]; /* open disk image files */

LOCALPROC InitDrives(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		Drives[i] = NotAfileRef;
	}
}

GLOBALFUNC tMacErr vSonyTransfer(blnr IsWrite, ui3p Buffer,
	tDrive Drive_No, ui5r Sony_Start, ui5r Sony_Count,
	ui5r *Sony_ActCount)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	ui5r NewSony_Count = 0;

	if (0 == fseek(refnum, Sony_Start, SEEK_SET)) {
		if (IsWrite) {
			NewSony_Count = fwrite(Buffer, 1, Sony_Count, refnum);
		} else {
			NewSony_Count = fread(Buffer, 1, Sony_Count, refnum);
		}

		if (NewSony_Count == Sony_Count) {
			err = mnvm_noErr;
		}
	}

	if (nullpr != Sony_ActCount) {
		*Sony_ActCount = NewSony_Count;
	}

	return err;
}

GLOBALFUNC tMacErr vSonyGetSize(tDrive Drive_No, ui5r *Sony_Count)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	long v;

	if (0 == fseek(refnum, 0, SEEK_END)) {
		v = ftell(refnum);
		if (v >= 0) {
			*Sony_Count = v;
			err = mnvm_noErr;
		}
	}

	return err;
}

LOCALFUNC tMacErr vSonyEject0(tDrive Drive_No, blnr deleteit)
{
	FILE *refnum = Drives[Drive_No];

	DiskEjectedNotify(Drive_No);

	fclose(refnum);
	Drives[Drive_No] = NotAfileRef;

	return mnvm_noErr;
}

GLOBALFUNC tMacErr vSonyEject(tDrive Drive_No)
{
	return vSonyEject0(Drive_No, falseblnr);
}

LOCALPROC UnInitDrives(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		if (vSonyIsInserted(i)) {
			(void) vSonyEject(i);
		}
	}
}

LOCALFUNC blnr Sony_Insert0(FILE *refnum, blnr locked,
	char *drivepath)
{
	tDrive Drive_No;
	blnr IsOk = falseblnr;

	if (! FirstFreeDisk(&Drive_No)) {
		MacMsg(kStrTooManyImagesTitle, kStrTooManyImagesMessage,
			falseblnr);
	} else {
		Drives[Drive_No] = refnum;
		DiskInsertNotify(Drive_No, locked);
		IsOk = trueblnr;
	}

	if (! IsOk) {
		fclose(refnum);
	}

	return IsOk;
}

LOCALFUNC blnr Sony_Insert1(char *drivepath, blnr silentfail)
{
	blnr locked = falseblnr;
	FILE *refnum = fopen(drivepath, "rb+");
	if (NULL == refnum) {
		locked = trueblnr;
		refnum = fopen(drivepath, "rb");
	}
	if (NULL == refnum) {
		if (! silentfail) {
			MacMsg(kStrOpenFailTitle, kStrOpenFailMessage, falseblnr);
		}
	} else {
		return Sony_Insert0(refnum, locked, drivepath);
	}
	return falseblnr;
}

LOCALFUNC blnr Sony_Insert2(char *s)
{
	return Sony_Insert1(s, trueblnr);
}

static const char* get_res_name(const char* name) {
	static char res_name[256] = {0};
	snprintf(res_name, sizeof(res_name), "%s/res/%s", cmain_get_own_dir(NULL, 0), name);
	return res_name;
}

LOCALFUNC blnr LoadInitialImages(void)
{
	if (! AnyDiskInserted()) {
		int n = NumDrives > 9 ? 9 : NumDrives;
		int i;

		for (i = 1; i <= n; ++i) {
			char s[32] = {};
			snprintf(s, 31, "roms/disk%d.dsk", i);
			const char* disk = get_res_name(s);
			if (! Sony_Insert2(disk)) {
				return trueblnr;
			}
		}
	}

	return trueblnr;
}

/* --- ROM --- */

LOCALVAR char *rom_path = NULL;

LOCALFUNC tMacErr LoadMacRomFrom(char *path)
{
	tMacErr err;
	FILE *ROM_File;
	int File_Size;

	ROM_File = fopen(path, "rb");
	if (NULL == ROM_File) {
		err = mnvm_fnfErr;
	} else {
		File_Size = fread(ROM, 1, kROM_Size, ROM_File);
		if (File_Size != kROM_Size) {
			if (feof(ROM_File)) {
				err = mnvm_eofErr;
			} else {
				err = mnvm_miscErr;
			}
		} else {
			err = mnvm_noErr;
		}
		fclose(ROM_File);
	}

	return err;
}

LOCALFUNC blnr LoadMacRom(void)
{
	tMacErr err;
	rom_path = get_res_name("roms/vMac.ROM");

	if ((NULL == rom_path)
		|| (mnvm_fnfErr == (err = LoadMacRomFrom(rom_path))))
	if (mnvm_fnfErr == (err = LoadMacRomFrom(RomFileName)))
	{
	}

	if (mnvm_noErr != err) {
		if (mnvm_fnfErr == err) {
			MacMsg(kStrNoROMTitle, kStrNoROMMessage, trueblnr);
		} else if (mnvm_eofErr == err) {
			MacMsg(kStrShortROMTitle, kStrShortROMMessage,
				trueblnr);
		} else {
			MacMsg(kStrNoReadROMTitle, kStrNoReadROMMessage,
				trueblnr);
		}
		SpeedStopped = trueblnr;
	}

	return trueblnr;
}

/* --- video out --- */

#if VarFullScreen
LOCALVAR blnr UseFullScreen = (WantInitFullScreen != 0);
#endif

#if EnableMagnify
LOCALVAR blnr UseMagnify = (WantInitMagnify != 0);
#endif

LOCALVAR blnr gBackgroundFlag = falseblnr;
LOCALVAR blnr gTrueBackgroundFlag = falseblnr;
LOCALVAR blnr CurSpeedStopped = trueblnr;

#if EnableMagnify
#define MaxScale MyWindowScale
#else
#define MaxScale 1
#endif


LOCALVAR graph_t *screen_buffer = NULL;

LOCALVAR ui3p ScalingBuff = nullpr;

LOCALVAR ui3p CLUT_final;

#define CLUT_finalsz (256 * 8 * 4 * MaxScale)

#define ScrnMapr_DoMap UpdateBWDepth3Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 3
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateBWDepth4Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 4
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateBWDepth5Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 5
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateBWDepth3ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 3
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateBWDepth4ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 4
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateBWDepth5ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth 0
#define ScrnMapr_DstDepth 5
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"


#if (0 != vMacScreenDepth) && (vMacScreenDepth < 4)

#define ScrnMapr_DoMap UpdateColorDepth3Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 3
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateColorDepth4Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 4
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateColorDepth5Copy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 5
#define ScrnMapr_Map CLUT_final

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateColorDepth3ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 3
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateColorDepth4ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 4
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"

#define ScrnMapr_DoMap UpdateColorDepth5ScaledCopy
#define ScrnMapr_Src GetCurDrawBuff()
#define ScrnMapr_Dst ScalingBuff
#define ScrnMapr_SrcDepth vMacScreenDepth
#define ScrnMapr_DstDepth 5
#define ScrnMapr_Map CLUT_final
#define ScrnMapr_Scale MyWindowScale

#include "SCRNMAPR.h"

#endif


LOCALPROC HaveChangedScreenBuff(ui4r top, ui4r left,
	ui4r bottom, ui4r right)
{
	int i;
	ui3b *p;
	uint32_t pixel;
#if (0 != vMacScreenDepth) && (vMacScreenDepth < 4)
	uint32_t CLUT_pixel[CLUT_size];
#endif
	uint32_t BWLUT_pixel[2];

	if (screen_buffer == NULL)
		return;

#if 0 != vMacScreenDepth
	if (UseColorMode) {
#if vMacScreenDepth < 4
		for (i = 0; i < CLUT_size; ++i) {
			CLUT_pixel[i] = 0xff000000 |
				((CLUT_reds[i] >> 8) << 16) |
				((CLUT_greens[i] >> 8) << 8) |
				(CLUT_blues[i] >> 8);
		}
#endif
	} else
#endif
	{
		BWLUT_pixel[1] = 0xff000000; /* black */
		BWLUT_pixel[0] = 0xffffffff; /* white */
	}

	ui3b *dst_data = (ui3b *)screen_buffer->buffer;
	int dst_pitch = screen_buffer->w * 4;

	{
		int k;
		uint32_t v;
#if EnableMagnify
		int a;
#endif
		int PixPerByte =
#if (0 != vMacScreenDepth) && (vMacScreenDepth < 4)
			UseColorMode ? (1 << (3 - vMacScreenDepth)) :
#endif
			8;
		uint8_t *p4 = (uint8_t *)CLUT_final;

		for (i = 0; i < 256; ++i) {
			for (k = PixPerByte; --k >= 0; ) {

#if (0 != vMacScreenDepth) && (vMacScreenDepth < 4)
				if (UseColorMode) {
					v = CLUT_pixel[
#if 3 == vMacScreenDepth
						i
#else
						(i >> (k << vMacScreenDepth))
							& (CLUT_size - 1)
#endif
						];
				} else
#endif
				{
					v = BWLUT_pixel[(i >> k) & 1];
				}

#if EnableMagnify
				for (a = UseMagnify ? MyWindowScale : 1; --a >= 0; )
#endif
				{
					*(uint32_t *)p4 = v;
					p4 += 4;
				}
			}
		}

		ScalingBuff = (ui3p)dst_data;

#if (0 != vMacScreenDepth) && (vMacScreenDepth < 4)
		if (UseColorMode) {
#if EnableMagnify
			if (UseMagnify) {
				UpdateColorDepth3ScaledCopy(top, left, bottom, right);
			} else
#endif
			{
				UpdateColorDepth3Copy(top, left, bottom, right);
			}
		} else
#endif
		{
#if EnableMagnify
			if (UseMagnify) {
				UpdateBWDepth3ScaledCopy(top, left, bottom, right);
			} else
#endif
			{
				ui3p src_data = GetCurDrawBuff();
				if (dst_data != NULL && src_data != NULL) {
					int y, x;

					for (y = top; y < bottom && y < vMacScreenHeight; y++) {
						ui3b *src_row = src_data + (y * (vMacScreenWidth / 8));
						uint32_t *dst_row = (uint32_t *)(dst_data + (y * dst_pitch));

						for (x = left; x < right && x < vMacScreenWidth; x++) {
							int byte_index = x / 8;
							int bit_index = 7 - (x % 8);
							int bit = (src_row[byte_index] >> bit_index) & 1;

							dst_row[x] = bit ? 0xff000000 : 0xffffffff;
						}
					}
				}
			}
		}
	}
}

LOCALPROC MyDrawChangesAndClear(void)
{
	if (ScreenChangedBottom > ScreenChangedTop) {
		HaveChangedScreenBuff(ScreenChangedTop, ScreenChangedLeft,
			ScreenChangedBottom, ScreenChangedRight);
		ScreenClearChanges();
	}
}

/* --- mouse --- */

/* cursor hiding */

LOCALVAR blnr HaveCursorHidden = falseblnr;
LOCALVAR blnr WantCursorHidden = falseblnr;

LOCALPROC ForceShowCursor(void)
{
	if (HaveCursorHidden) {
		HaveCursorHidden = falseblnr;
		x_show_cursor(true);
	}
}

/* cursor moving */

LOCALFUNC blnr MyMoveMouse(si4b h, si4b v)
{
	return trueblnr;
}

/* cursor state */

LOCALPROC MousePositionNotify(int NewMousePosh, int NewMousePosv)
{
	blnr ShouldHaveCursorHidden = trueblnr;

#if EnableMagnify
	if (UseMagnify) {
		NewMousePosh /= MyWindowScale;
		NewMousePosv /= MyWindowScale;
	}
#endif

	if (NewMousePosh < 0) {
		NewMousePosh = 0;
		ShouldHaveCursorHidden = falseblnr;
	} else if (NewMousePosh >= vMacScreenWidth) {
		NewMousePosh = vMacScreenWidth - 1;
		ShouldHaveCursorHidden = falseblnr;
	}
	if (NewMousePosv < 0) {
		NewMousePosv = 0;
		ShouldHaveCursorHidden = falseblnr;
	} else if (NewMousePosv >= vMacScreenHeight) {
		NewMousePosv = vMacScreenHeight - 1;
		ShouldHaveCursorHidden = falseblnr;
	}

#if VarFullScreen
	if (UseFullScreen)
#endif
#if MayFullScreen
	{
		ShouldHaveCursorHidden = trueblnr;
	}
#endif

	MyMousePositionSet(NewMousePosh, NewMousePosv);

#if EnableMouseMotion && MayFullScreen
	SavedMouseH = NewMousePosh;
	SavedMouseV = NewMousePosv;
#endif

	WantCursorHidden = ShouldHaveCursorHidden;
}

LOCALPROC MousePositionNotifyRelative(int deltah, int deltav)
{
	blnr ShouldHaveCursorHidden = trueblnr;

#if EnableMagnify
	if (UseMagnify) {
		deltah /= MyWindowScale;
		deltav /= MyWindowScale;
	}
#endif
	MyMousePositionSetDelta(deltah,
		deltav);

	WantCursorHidden = ShouldHaveCursorHidden;
}

/* --- keyboard input --- */

LOCALFUNC int XWinKey2MacKeyCode(int key)
{
	int v = -1;

	switch (key) {
		case 8: v = MKC_BackSpace; break;
		case 9: v = MKC_Tab; break;
		case 13: v = MKC_Return; break;
		case 27: v = MKC_Escape; break;
		case 32: v = MKC_Space; break;
		case 39: v = MKC_SingleQuote; break;
		case 44: v = MKC_Comma; break;
		case 45: v = MKC_Minus; break;
		case 46: v = MKC_Period; break;
		case 47: v = MKC_Slash; break;
		case 48: v = MKC_0; break;
		case 49: v = MKC_1; break;
		case 50: v = MKC_2; break;
		case 51: v = MKC_3; break;
		case 52: v = MKC_4; break;
		case 53: v = MKC_5; break;
		case 54: v = MKC_6; break;
		case 55: v = MKC_7; break;
		case 56: v = MKC_8; break;
		case 57: v = MKC_9; break;
		case 59: v = MKC_SemiColon; break;
		case 61: v = MKC_Equal; break;
		case 91: v = MKC_LeftBracket; break;
		case 92: v = MKC_BackSlash; break;
		case 93: v = MKC_RightBracket; break;
		case 96: v = MKC_Grave; break;

		case 'a': case 'A': v = MKC_A; break;
		case 'b': case 'B': v = MKC_B; break;
		case 'c': case 'C': v = MKC_C; break;
		case 'd': case 'D': v = MKC_D; break;
		case 'e': case 'E': v = MKC_E; break;
		case 'f': case 'F': v = MKC_F; break;
		case 'g': case 'G': v = MKC_G; break;
		case 'h': case 'H': v = MKC_H; break;
		case 'i': case 'I': v = MKC_I; break;
		case 'j': case 'J': v = MKC_J; break;
		case 'k': case 'K': v = MKC_K; break;
		case 'l': case 'L': v = MKC_L; break;
		case 'm': case 'M': v = MKC_M; break;
		case 'n': case 'N': v = MKC_N; break;
		case 'o': case 'O': v = MKC_O; break;
		case 'p': case 'P': v = MKC_P; break;
		case 'q': case 'Q': v = MKC_Q; break;
		case 'r': case 'R': v = MKC_R; break;
		case 's': case 'S': v = MKC_S; break;
		case 't': case 'T': v = MKC_T; break;
		case 'u': case 'U': v = MKC_U; break;
		case 'v': case 'V': v = MKC_V; break;
		case 'w': case 'W': v = MKC_W; break;
		case 'x': case 'X': v = MKC_X; break;
		case 'y': case 'Y': v = MKC_Y; break;
		case 'z': case 'Z': v = MKC_Z; break;

		case KEY_UP: v = MKC_Up; break;
		case KEY_DOWN: v = MKC_Down; break;
		case KEY_RIGHT: v = MKC_Right; break;
		case KEY_LEFT: v = MKC_Left; break;
		case KEY_INSERT: v = MKC_Help; break;
		case KEY_HOME: v = MKC_Home; break;
		case KEY_END: v = MKC_End; break;
		case KEY_PAGEUP: v = MKC_PageUp; break;
		case KEY_PAGEDOWN: v = MKC_PageDown; break;

		case KEY_F1: v = MKC_F1; break;
		case KEY_F2: v = MKC_F2; break;
		case KEY_F3: v = MKC_F3; break;
		case KEY_F4: v = MKC_F4; break;
		case KEY_F5: v = MKC_F5; break;
		case KEY_F6: v = MKC_F6; break;
		case KEY_F7: v = MKC_F7; break;
		case KEY_F8: v = MKC_F8; break;
		case KEY_F9: v = MKC_F9; break;
		case KEY_F10: v = MKC_F10; break;
		case KEY_F11: v = MKC_F11; break;
		case KEY_F12: v = MKC_F11; break;

		case KEY_CAPSLOCK: v = MKC_CapsLock; break;
		case KEY_SCROLLLOCK: v = MKC_ScrollLock; break;
		case KEY_SHIFT: v = MKC_Shift; break;
		case KEY_CTRL: v = MKC_Control; break;
		case KEY_ALT: v = MKC_Option; break;
		case KEY_FLAG_RSHIFT: v = MKC_Shift; break;
		case KEY_FLAG_RCTRL: v = MKC_Control; break;
		case KEY_FLAG_RALT: v = MKC_Option; break;

		default:
			break;
	}

	return v;
}

LOCALPROC DoKeyCode(int key, blnr down)
{
	int v = XWinKey2MacKeyCode(key);
	if (v >= 0) {
		Keyboard_UpdateKeyMap2(v, down);
	}
}

LOCALPROC DisableKeyRepeat(void)
{
}

LOCALPROC RestoreKeyRepeat(void)
{
}

LOCALPROC ReconnectKeyCodes3(void)
{
}

LOCALPROC DisconnectKeyCodes3(void)
{
	DisconnectKeyCodes2();
	MyMouseButtonSet(falseblnr);
}

/* --- time, date, location --- */

LOCALVAR ui5b TrueEmulatedTime = 0;
LOCALVAR ui5b CurEmulatedTime = 0;

#define MyInvTimeDivPow 16
#define MyInvTimeDiv (1 << MyInvTimeDivPow)
#define MyInvTimeDivMask (MyInvTimeDiv - 1)
#define MyInvTimeStep 1089590 /* 1000 / 60.14742 * MyInvTimeDiv */

LOCALVAR uint32_t LastTime;

LOCALVAR uint32_t NextIntTime;
LOCALVAR ui5b NextFracTime;

LOCALPROC IncrNextTime(void)
{
	NextFracTime += MyInvTimeStep;
	NextIntTime += (NextFracTime >> MyInvTimeDivPow);
	NextFracTime &= MyInvTimeDivMask;
}

LOCALPROC InitNextTime(void)
{
	NextIntTime = LastTime;
	NextFracTime = 0;
	IncrNextTime();
}

LOCALVAR ui5b NewMacDateInSeconds;

LOCALFUNC blnr UpdateTrueEmulatedTime(void)
{
	uint32_t LatestTime;
	si5b TimeDiff;

	uint32_t low;
	kernel_tic32(NULL, NULL, &low);
	LatestTime = low / 1000;

	if (LatestTime != LastTime) {

		NewMacDateInSeconds = LatestTime / 1000;

		LastTime = LatestTime;
		TimeDiff = (LatestTime - NextIntTime);
		if (TimeDiff >= 0) {
			if (TimeDiff > 64) {
				++TrueEmulatedTime;
				InitNextTime();
			} else {
				do {
					++TrueEmulatedTime;
					IncrNextTime();
					TimeDiff = (LatestTime - NextIntTime);
				} while (TimeDiff >= 0);
			}
			return trueblnr;
		} else {
			if (TimeDiff < -20) {
				InitNextTime();
			}
		}
	}
	return falseblnr;
}


LOCALFUNC blnr CheckDateTime(void)
{
	if (CurMacDateInSeconds != NewMacDateInSeconds) {
		CurMacDateInSeconds = NewMacDateInSeconds;
		return trueblnr;
	} else {
		return falseblnr;
	}
}

LOCALPROC StartUpTimeAdjust(void)
{
	uint32_t low;
	kernel_tic32(NULL, NULL, &low);
	LastTime = low / 1000;
	InitNextTime();
}

LOCALFUNC blnr InitLocationDat(void)
{
	uint32_t low;
	kernel_tic32(NULL, NULL, &low);
	LastTime = low / 1000;
	InitNextTime();
	NewMacDateInSeconds = LastTime / 1000;
	CurMacDateInSeconds = NewMacDateInSeconds;

	return trueblnr;
}

/* --- sound --- */

#if MySoundEnabled

#define kLn2SoundBuffers 4
#define kSoundBuffers (1 << kLn2SoundBuffers)
#define kSoundBuffMask (kSoundBuffers - 1)

#define DesiredMinFilledSoundBuffs 3

#define kLnOneBuffLen 9
#define kLnAllBuffLen (kLn2SoundBuffers + kLnOneBuffLen)
#define kOneBuffLen (1UL << kLnOneBuffLen)
#define kAllBuffLen (1UL << kLnAllBuffLen)
#define kLnOneBuffSz (kLnOneBuffLen + kLn2SoundSampSz - 3)
#define kLnAllBuffSz (kLnAllBuffLen + kLn2SoundSampSz - 3)
#define kOneBuffSz (1UL << kLnOneBuffSz)
#define kAllBuffSz (1UL << kLnAllBuffSz)
#define kOneBuffMask (kOneBuffLen - 1)
#define kAllBuffMask (kAllBuffLen - 1)
#define dbhBufferSize (kAllBuffSz + kOneBuffSz)

LOCALVAR tpSoundSamp TheSoundBuffer = nullpr;
LOCALVAR ui4b ThePlayOffset;
LOCALVAR ui4b TheFillOffset;
LOCALVAR ui4b TheWriteOffset;
LOCALVAR ui4b MinFilledSoundBuffs;

LOCALPROC MySound_Start0(void)
{
	ThePlayOffset = 0;
	TheFillOffset = 0;
	TheWriteOffset = 0;
	MinFilledSoundBuffs = kSoundBuffers + 1;
}

GLOBALFUNC tpSoundSamp MySound_BeginWrite(ui4r n, ui4r *actL)
{
	ui4b ToFillLen = kAllBuffLen - (TheWriteOffset - ThePlayOffset);
	ui4b WriteBuffContig =
		kOneBuffLen - (TheWriteOffset & kOneBuffMask);

	if (WriteBuffContig < n) {
		n = WriteBuffContig;
	}
	if (ToFillLen < n) {
		TheWriteOffset -= kOneBuffLen;
	}

	*actL = n;
	return TheSoundBuffer + (TheWriteOffset & kAllBuffMask);
}

LOCALFUNC blnr MySound_EndWrite0(ui4r actL)
{
	blnr v;

	TheWriteOffset += actL;

	if (0 != (TheWriteOffset & kOneBuffMask)) {
		v = falseblnr;
	} else {
		TheFillOffset = TheWriteOffset;
		v = trueblnr;
	}

	return v;
}

LOCALPROC MySound_SecondNotify0(void)
{
	if (MinFilledSoundBuffs <= kSoundBuffers) {
		if (MinFilledSoundBuffs > DesiredMinFilledSoundBuffs) {
			++CurEmulatedTime;
		} else if (MinFilledSoundBuffs < DesiredMinFilledSoundBuffs) {
			--CurEmulatedTime;
		}
		MinFilledSoundBuffs = kSoundBuffers + 1;
	}
}

#define SOUND_SAMPLERATE 44100

blnr HaveSoundOut = falseblnr;
LOCALVAR blnr HaveStartedPlaying = falseblnr;

/* PCM device support */
#include <ewoksys/proto.h>
#include <ewoksys/vdevice.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#define CTRL_PCM_DEV_HW				(0xF0)
#define CTRL_PCM_DEV_HW_FREE		(0xF1)
#define CTRL_PCM_DEV_PRPARE			(0xF2)
#define CTRL_PCM_BUF_AVAIL			(0xF3)

#define	EPIPE					(32)

struct pcm_config {
    int bit_depth;
    int rate;
    int channels;
    int period_size;
    int period_count;
    int start_threshold;
    int stop_threshold;
};

struct pcm {
    int fd;
    int prepared;
    int running;
    char name[32];
    int framesize;
    struct pcm_config config;
};

static int pcm_buf_avail(struct pcm *pcm)
{
    proto_t in, out;
    PF->init(&in);
    PF->init(&out);
    int ret = 0;

    ret = dev_cntl(pcm->name, CTRL_PCM_BUF_AVAIL, &in, &out);
    if(ret == 0) {
        ret = proto_read_int(&out);
    } 

    PF->clear(&in);
    PF->clear(&out);
    return ret;
}

static int support_rate(unsigned int rate) {
    switch (rate) {
    case 8000:
    case 16000:
    case 32000:
    case 44100:
    case 48000:
    case 96000:
        return 1;
    }
    return 0;
}

static int support_channels(unsigned int channels) {
    if (channels != 2) {
        return 0;
    }
    return 1;
}

static int support_bit_depth(unsigned int bit_depth) {
    switch (bit_depth) {
    case 16:
    case 24:
    case 32:
        return 1;
    default:
        return 0;
    }
}

static int is_valid_config(struct pcm_config *config)
{
    if (!support_bit_depth(config->bit_depth) ||
        !support_channels(config->channels) ||
        !support_rate(config->rate)) {
        return 0;
    }

    if (config->period_size == 0 || config->period_count == 0) {
        return 0;
    }

    if (config->start_threshold == 0) {
        config->start_threshold = config->period_size;
    }

    if (config->stop_threshold == 0) {
        config->stop_threshold = config->period_size * config->period_count;
    }
    return 1;
}

static int pcm_prepare(struct pcm *pcm)
{
    if (pcm->prepared) {
        return 0;
    }

    proto_t in, out;
    PF->init(&in);
    PF->init(&out);
    int ret = dev_cntl(pcm->name, CTRL_PCM_DEV_PRPARE, &in, &out);
    if(ret == 0) {
        ret = proto_read_int(&out);
    }
    PF->clear(&in);
    PF->clear(&out);

    if (ret == 0) {
        pcm->prepared = 1;
    }
    return ret;
}

static int pcm_try_write(struct pcm *pcm, const void* data, unsigned int count)
{
    if (count == 0) return 0;

    if (pcm->running == 0) {
        int err = pcm_prepare(pcm);
        if (err != 0) {
            return err;
        }

        int written = write(pcm->fd, data, count);
        if (written != (int)count) {
            return -1;
        }
        pcm->running = 1;
        return 0;
    }

    int ret = write(pcm->fd, data, count);
    return (ret == (int)count ? 0 : -1);
}

static int wait_avail(struct pcm *pcm, int *avail, int time_out_ms)
{
    enum { SLEEP_TIME_MS = 5 };
    *avail = 0;
    int ret = 0;
    int period_bytes = pcm->config.period_size * 4;
    int max_try_count = time_out_ms / SLEEP_TIME_MS;
    int try_count = 0;

    for(;;) {
        ret = pcm_buf_avail(pcm);
        if (ret < 0) {
            *avail = period_bytes;
            return 0;
        }

        if (ret >= period_bytes) {
            *avail = ret;
            break;
        }

        if(try_count++ >= max_try_count) {
            *avail = ret;
            break;
        }

        proc_usleep(SLEEP_TIME_MS * 1000);
    }

    return ret;
}

static int pcm_write(struct pcm *pcm, const void* data, unsigned int count) {
    if (count == 0) return 0;

    int period_bytes = pcm->config.period_size * 4; // 16-bit stereo = 4 bytes per frame
    int avail = 0;
    int bytes = (int)count;
    int written = 0;
    int offset = 0;
    int copy_bytes = 0;
    int ret = 0;

    copy_bytes = bytes < period_bytes ? bytes : period_bytes;
    while (bytes > 0) {
        ret = wait_avail(pcm, &avail, 2000); // Wait up to 2 seconds
        if (ret < 0 || (avail == 0 && bytes > 0)) {
            break;
        }
        copy_bytes = bytes < avail ? bytes : avail;

        ret = pcm_try_write(pcm, (const char*)data + offset, copy_bytes);
        if (ret == 0) {
            offset += copy_bytes;
            written += copy_bytes;
            bytes -= copy_bytes;
            copy_bytes = bytes < period_bytes ? bytes : period_bytes;
        }
    }

    return (written == (int)count ? 0 : -1);
}

static struct pcm* pcm_open(const char *name, struct pcm_config *config)
{
    struct pcm* pcm;

    if (!is_valid_config(config)) {
        return NULL;
    }

    pcm = calloc(1, sizeof(struct pcm));
    if (pcm == NULL) {
        return NULL;
    }

    strncpy(pcm->name, name, 31);
    memcpy(&pcm->config, config, sizeof(struct pcm_config));
    pcm->framesize = config->channels * config->bit_depth / 8;

    pcm->fd = open(name, O_RDWR);
    if (pcm->fd < 0) {
        free(pcm);
        return NULL;
    }

    proto_t in, out;
    PF->init(&in)->add(&in, config, sizeof(struct pcm_config));
    PF->init(&out);
    int temp = dev_cntl(pcm->name, CTRL_PCM_DEV_HW, &in, &out);
    if(temp == 0) {
        temp = proto_read_int(&out);
    }
    PF->clear(&in);
    PF->clear(&out);

    if (temp != 0) {
        close(pcm->fd);
        free(pcm);
        return NULL;
    }

    return pcm;
}

static int pcm_close(struct pcm *pcm)
{
    if (pcm == NULL) {
        return 0;
    }

    close(pcm->fd);
    free(pcm);
    return 0;
}

/* Audio thread data */
static struct audio_thread_data {
    struct pcm *pcm;
    int enabled;
} audio_thread_data;

static pthread_t audio_thread_id = 0;

/* Audio thread */
static void *audio_thread(void *arg)
{
    struct audio_thread_data *data = (struct audio_thread_data *)arg;
    struct pcm *pcm = data->pcm;
    int buffer_size = kOneBuffLen * 4; /* 2 channels * 2 bytes per sample */
    unsigned char *buffer = NULL;

    if (!pcm) {
        return NULL;
    }

    /* Allocate buffer */
    buffer = (unsigned char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }

    /* Initial delay */
    proc_usleep(200 * 1000);

    while (data->enabled) {
        /* Check if there's data to play */
        if (TheFillOffset > ThePlayOffset) {
            int play_len = TheFillOffset - ThePlayOffset;
            if (play_len > kOneBuffLen) {
                play_len = kOneBuffLen;
            }

            /* Convert 8-bit mono to 16-bit stereo */
            for (int i = 0; i < play_len; i++) {
                int sample = TheSoundBuffer[(ThePlayOffset + i) & kAllBuffMask];
                /* Convert 8-bit to 16-bit */
                sample = (sample - 128) * 256;
                /* Write to both channels */
                ((int16_t *)buffer)[i * 2] = sample;
                ((int16_t *)buffer)[i * 2 + 1] = sample;
            }

            /* Write to PCM device using pcm_write (like emu does) */
            int res = pcm_write(pcm, buffer, play_len * 4);

            /* Update play offset - always advance to keep buffer flowing */
            ThePlayOffset += play_len;
        } else {
            /* No data to play, sleep briefly */
            proc_usleep(10000);
        }
    }

    free(buffer);
    return NULL;
}

static struct pcm *sound_pcm = NULL;

LOCALPROC MySound_Start(void)
{
	if (HaveSoundOut) {
		MySound_Start0();
		HaveStartedPlaying = trueblnr;
	}
}

LOCALPROC MySound_Stop(void)
{
	if (HaveSoundOut) {
		HaveStartedPlaying = falseblnr;
	}
}

LOCALFUNC blnr MySound_Init(void)
{
	/* Allocate sound buffer */
	TheSoundBuffer = (tpSoundSamp)malloc(kAllBuffSz);
	if (!TheSoundBuffer) {
		// If buffer allocation fails, continue without sound
		HaveSoundOut = falseblnr;
		klog("MySound_Init: Failed to allocate sound buffer\n");
		return falseblnr;
	}

	/* Initialize PCM device */
	struct pcm_config config;
	memset(&config, 0, sizeof(config));
	config.bit_depth = 16;
	config.rate = SOUND_SAMPLERATE;
	config.channels = 2;
	config.period_size = 1024;
	config.period_count = 4;
	config.start_threshold = 1024 * 2;
	config.stop_threshold = 0;

	// Try different sound device paths
	const char *sound_devices[] = {
		"/dev/sound0",
		"/dev/sound",
		"/dev/pcm0",
		"/dev/pcm",
		NULL
	};

	int i = 0;
	while (sound_devices[i] != NULL) {
		sound_pcm = pcm_open(sound_devices[i], &config);
		if (sound_pcm) {
			slog("MySound_Init: Successfully opened PCM device %s\n", sound_devices[i]);
			break;
		}
		slog("MySound_Init: Failed to open PCM device %s\n", sound_devices[i]);
		i++;
	}

	if (!sound_pcm) {
		// If all PCM device opens fail, continue without sound
		slog("MySound_Init: All PCM devices failed to open\n");
		free(TheSoundBuffer);
		TheSoundBuffer = nullpr;
		HaveSoundOut = falseblnr;
		//return falseblnr; //sound is optional
		return trueblnr;
	}

	/* Start audio thread */
	audio_thread_data.pcm = sound_pcm;
	audio_thread_data.enabled = 1;

	if (pthread_create(&audio_thread_id, NULL, audio_thread, &audio_thread_data) != 0) {
		// If thread creation fails, continue without sound
		pcm_close(sound_pcm);
		sound_pcm = NULL;
		free(TheSoundBuffer);
		TheSoundBuffer = nullpr;
		HaveSoundOut = falseblnr;
		slog("MySound_Init: Failed to create audio thread\n");
		return falseblnr;
	}

	HaveSoundOut = trueblnr;
	slog("MySound_Init: Success\n");
	return trueblnr;
}

LOCALPROC MySound_UnInit(void)
{
	if (HaveSoundOut) {
		/* Stop audio thread */
		audio_thread_data.enabled = 0;
		if (audio_thread_id != 0) {
			pthread_join(audio_thread_id, NULL);
			audio_thread_id = 0;
		}

		/* Close PCM device */
		if (sound_pcm) {
			pcm_close(sound_pcm);
			sound_pcm = NULL;
		}

		/* Free sound buffer */
		if (TheSoundBuffer) {
			free(TheSoundBuffer);
			TheSoundBuffer = nullpr;
		}

		HaveSoundOut = falseblnr;
	}
}

GLOBALPROC MySound_EndWrite(ui4r actL)
{
	if (MySound_EndWrite0(actL)) {
		/* Buffer is full, let the audio thread handle it */
	}
}

LOCALPROC MySound_SecondNotify(void)
{
	if (HaveSoundOut) {
		MySound_SecondNotify0();
	}
}

#endif

/* --- basic dialogs --- */

LOCALPROC CheckSavedMacMsg(void)
{
	if (nullpr != SavedBriefMsg) {
		char briefMsg0[ClStrMaxLength + 1];
		char longMsg0[ClStrMaxLength + 1];

		NativeStrFromCStr(briefMsg0, SavedBriefMsg);
		NativeStrFromCStr(longMsg0, SavedLongMsg);
		SavedBriefMsg = nullpr;
	}
}

/* --- clipboard --- */

#define UseMotionEvents 1

#if UseMotionEvents
LOCALVAR blnr CaughtMouse = falseblnr;
#endif

/* --- XWin event handling --- */

static void on_xwin_resize(xwin_t* win) {
	if(win == NULL || win->xinfo == NULL)
		return;
	window_width = win->xinfo->wsr.w;
	window_height = win->xinfo->wsr.h;
}

static void on_xwin_event(xwin_t* win, xevent_t* ev) {
	switch (ev->type) {
		case XEVT_IM:
		{
			int key = ev->value.im.key_code;
			blnr down = (ev->state == XIM_STATE_PRESS);
			DoKeyCode(key, down);
			break;
		}
		case XEVT_MOUSE:
		{
			gpos_t pos = xwin_get_inside_pos(win, ev->value.mouse.x, ev->value.mouse.y);
			int x = pos.x;
			int y = pos.y;
			
			int button = ev->value.mouse.button;

			//if (button == MOUSE_BUTTON_LEFT) {
			if (ev->state == MOUSE_STATE_DOWN) {
				MyMouseButtonSet(trueblnr);
			} else if (ev->state == MOUSE_STATE_UP) {
				MyMouseButtonSet(falseblnr);
			}
			//}

			if (window_width > 0 && window_height > 0) {
				int mac_x, mac_y;

				if (display_scale > 1.0) {
					int scaled_x = x - display_offset_x;
					int scaled_y = y - display_offset_y;
					mac_x = scaled_x / display_scale;
					mac_y = scaled_y / display_scale;
				} else {
					mac_x = (x - display_offset_x) * vMacScreenWidth / (vMacScreenWidth * display_scale);
					mac_y = (y - display_offset_y) * vMacScreenHeight / (vMacScreenHeight * display_scale);
					if (display_offset_x == 0 && display_offset_y == 0) {
						mac_x = x * vMacScreenWidth / window_width;
						mac_y = y * vMacScreenHeight / window_height;
					}
				}

				if (mac_x >= 0 && mac_x < vMacScreenWidth &&
					mac_y >= 0 && mac_y < vMacScreenHeight) {
					MousePositionNotify(mac_x, mac_y);
				}
			}
			break;
		}
		case XEVT_WIN:
		{
			if (ev->value.window.event == XEVT_WIN_CLOSE) {
				RequestMacOff = trueblnr;
			}
			break;
		}
	}
}

static void on_xwin_repaint(xwin_t* win, graph_t* g) {
	if (g == NULL)
		return;

	screen_graph = g;
	window_width = g->w;
	window_height = g->h;

	graph_fill(g, 0, 0, g->w, g->h, 0xff000000);

	if (screen_buffer != NULL && screen_buffer->buffer != NULL) {
		ScreenChangedTop = 0;
		ScreenChangedLeft = 0;
		ScreenChangedBottom = vMacScreenHeight;
		ScreenChangedRight = vMacScreenWidth;

		if (ScreenChangedBottom > ScreenChangedTop) {
			HaveChangedScreenBuff(ScreenChangedTop, ScreenChangedLeft,
				ScreenChangedBottom, ScreenChangedRight);
			ScreenClearChanges();
		}

		float scale_x = (float)g->w / screen_buffer->w;
		float scale_y = (float)g->h / screen_buffer->h;
		float scale = (scale_x < scale_y) ? scale_x : scale_y;
		if (scale < 1.0f) scale = 1.0f;

		int scaled_w = screen_buffer->w * scale;
		int scaled_h = screen_buffer->h * scale;
		int offset_x = (g->w - scaled_w) / 2;
		int offset_y = (g->h - scaled_h) / 2;

		display_scale = scale;
		display_offset_x = offset_x;
		display_offset_y = offset_y;

		if (scale > 1) {
			graph_t* scaled = graph_scalef_fast(screen_buffer, scale);
			if (scaled != NULL) {
				graph_blt(scaled, 0, 0, scaled_w, scaled_h, g, offset_x, offset_y, scaled_w, scaled_h);
				graph_free(scaled);
			}
		} else {
			if (offset_x > 0 || offset_y > 0) {
				graph_blt(screen_buffer, 0, 0, screen_buffer->w, screen_buffer->h,
					g, offset_x, offset_y, screen_buffer->w, screen_buffer->h);
			} else {
				graph_blt(screen_buffer, 0, 0, screen_buffer->w, screen_buffer->h,
					g, 0, 0, screen_buffer->w, screen_buffer->h);
			}
		}
	}
}

LOCALPROC CheckForSavedTasks(void);
LOCALPROC RunEmulatedTicksToTrueTime(void);
LOCALPROC DoEmulateOneTick(void);

#define mac_FPS 160
static void xwin_loop(void* p) {
	if (ForceMacOff) {
		x_terminate(x_context);
		return;
	}

	uint64_t tik = kernel_tic_ms(0);
	uint32_t tm = 1000/mac_FPS;

	CheckForSavedTasks();

	if (!CurSpeedStopped) {
		RunEmulatedTicksToTrueTime();

		DoEmulateOneTick();
		++CurEmulatedTime;
	}

	if (xwin != NULL) {
		xwin_repaint(xwin);
	}

	uint32_t gap = (uint32_t)(kernel_tic_ms(0) - tik);
	if(gap < tm) {
		gap = tm - gap;
		proc_usleep(gap*1000);
	}
}

/* --- main window creation and disposal --- */

LOCALVAR int my_argc;
LOCALVAR char **my_argv;

LOCALFUNC blnr Screen_Init(void)
{
	blnr v = falseblnr;

	InitKeyCodes();

	x_context = (x_t *)malloc(sizeof(x_t));
	if (x_context != NULL) {
		memset(x_context, 0, sizeof(x_t));
		x_init(x_context, NULL);
		x_context->on_loop = xwin_loop;
		v = trueblnr;
	}

	return v;
}

#if MayFullScreen
LOCALVAR blnr GrabMachine = falseblnr;
#endif

#if MayFullScreen
LOCALPROC GrabTheMachine(void)
{
}
#endif

#if MayFullScreen
LOCALPROC UngrabMachine(void)
{
}
#endif

#if EnableMouseMotion && MayFullScreen
LOCALPROC MyMouseConstrain(void)
{
}
#endif

LOCALFUNC blnr CreateMainWindow(void)
{
	int NewWindowHeight = vMacScreenHeight;
	int NewWindowWidth = vMacScreenWidth;
	blnr v = falseblnr;

#if EnableMagnify && 1
	if (UseMagnify) {
		NewWindowHeight *= MyWindowScale;
		NewWindowWidth *= MyWindowScale;
	}
#endif

	ViewHStart = 0;
	ViewVStart = 0;
	ViewHSize = vMacScreenWidth;
	ViewVSize = vMacScreenHeight;

	screen_buffer = graph_new(NULL, vMacScreenWidth, vMacScreenHeight);
	if (screen_buffer == NULL) {
		return falseblnr;
	}

	graph_fill(screen_buffer, 0, 0, vMacScreenWidth, vMacScreenHeight, 0xffffffff);

	xwin = xwin_open(x_context, -1, 32, 32, NewWindowWidth, NewWindowHeight,
		"Mini vMac", XWIN_STYLE_NORMAL);

	if (xwin == NULL) {
		graph_free(screen_buffer);
		screen_buffer = NULL;
		return falseblnr;
	}

	xwin->on_resize = on_xwin_resize;
	xwin->on_event = on_xwin_event;
	xwin->on_repaint = on_xwin_repaint;
	xwin_hide_cursor(xwin, true);
	xwin_set_visible(xwin, true);

	window_width = NewWindowWidth;
	window_height = NewWindowHeight;

	ScreenChangedAll();

	v = trueblnr;
	return v;
}

LOCALFUNC blnr ReCreateMainWindow(void)
{
	ForceShowCursor();

#if MayFullScreen
	if (GrabMachine) {
		GrabMachine = falseblnr;
		UngrabMachine();
	}
#endif

#if EnableMagnify
	UseMagnify = WantMagnify;
#endif
#if VarFullScreen
	UseFullScreen = WantFullScreen;
#endif

	if (xwin != NULL) {
		xwin_destroy(xwin);
		xwin = NULL;
	}

	if (screen_buffer != NULL) {
		graph_free(screen_buffer);
		screen_buffer = NULL;
	}

	(void) CreateMainWindow();

	if (HaveCursorHidden) {
		(void) MyMoveMouse(CurMouseH, CurMouseV);
	}

	return trueblnr;
}

LOCALPROC ZapWinStateVars(void)
{
}

#if VarFullScreen
LOCALPROC ToggleWantFullScreen(void)
{
	WantFullScreen = ! WantFullScreen;
}
#endif

/* --- SavedTasks --- */

LOCALPROC LeaveBackground(void)
{
	ReconnectKeyCodes3();
	DisableKeyRepeat();
}

LOCALPROC EnterBackground(void)
{
	RestoreKeyRepeat();
	DisconnectKeyCodes3();

	ForceShowCursor();
}

LOCALPROC LeaveSpeedStopped(void)
{
#if MySoundEnabled
	MySound_Start();
#endif

	StartUpTimeAdjust();
}

LOCALPROC EnterSpeedStopped(void)
{
#if MySoundEnabled
	MySound_Stop();
#endif
}

LOCALPROC CheckForSavedTasks(void)
{
	if (MyEvtQNeedRecover) {
		MyEvtQNeedRecover = falseblnr;

		MyEvtQTryRecoverFromFull();
	}

#if EnableMouseMotion && MayFullScreen
	if (HaveMouseMotion) {
		MyMouseConstrain();
	}
#endif

	if (RequestMacOff) {
		RequestMacOff = falseblnr;
		if (AnyDiskInserted()) {
			MacMsgOverride(kStrQuitWarningTitle,
				kStrQuitWarningMessage);
		} else {
			ForceMacOff = trueblnr;
		}
	}

	if (ForceMacOff) {
		return;
	}

	if (gTrueBackgroundFlag != gBackgroundFlag) {
		gBackgroundFlag = gTrueBackgroundFlag;
		if (gTrueBackgroundFlag) {
			EnterBackground();
		} else {
			LeaveBackground();
		}
	}

	if (CurSpeedStopped != (SpeedStopped ||
		(gBackgroundFlag && ! RunInBackground
#if EnableAutoSlow && 0
			&& (QuietSubTicks >= 4092)
#endif
		)))
	{
		CurSpeedStopped = ! CurSpeedStopped;
		if (CurSpeedStopped) {
			EnterSpeedStopped();
		} else {
			LeaveSpeedStopped();
		}
	}

	if ((nullpr != SavedBriefMsg) & ! MacMsgDisplayed) {
		MacMsgDisplayOn();
	}

#if EnableMagnify || VarFullScreen
	if (0
#if EnableMagnify
		|| (UseMagnify != WantMagnify)
#endif
#if VarFullScreen
		|| (UseFullScreen != WantFullScreen)
#endif
		)
	{
		(void) ReCreateMainWindow();
	}
#endif

#if MayFullScreen
	if (GrabMachine != (
#if VarFullScreen
		UseFullScreen &&
#endif
		! (gTrueBackgroundFlag || CurSpeedStopped)))
	{
		GrabMachine = ! GrabMachine;
		if (GrabMachine) {
			GrabTheMachine();
		} else {
			UngrabMachine();
		}
	}
#endif

	if (NeedWholeScreenDraw) {
		NeedWholeScreenDraw = falseblnr;
		ScreenChangedAll();
	}

	MyDrawChangesAndClear();

	if (HaveCursorHidden != (WantCursorHidden
		&& ! (gTrueBackgroundFlag || CurSpeedStopped)))
	{
		HaveCursorHidden = ! HaveCursorHidden;
	}
}

/* --- main program flow --- */

LOCALVAR ui5b OnTrueTime = 0;

GLOBALFUNC blnr ExtraTimeNotOver(void)
{
	UpdateTrueEmulatedTime();
	return TrueEmulatedTime == OnTrueTime;
}

/* --- platform independent code can be thought of as going here --- */

#include "PROGMAIN.h"

LOCALPROC RunEmulatedTicksToTrueTime(void)
{
	si3b n = OnTrueTime - CurEmulatedTime;

	if (n > 0) {
		if (CheckDateTime()) {
#if MySoundEnabled
			MySound_SecondNotify();
#endif
		}

		DoEmulateOneTick();
		++CurEmulatedTime;

		MyDrawChangesAndClear();

		if (n > 8) {
			n = 8;
			CurEmulatedTime = OnTrueTime - n;
		}

		if (ExtraTimeNotOver() && (--n > 0)) {
			EmVideoDisable = trueblnr;

			do {
				DoEmulateOneTick();
				++CurEmulatedTime;
			} while (ExtraTimeNotOver()
				&& (--n > 0));

			EmVideoDisable = falseblnr;
		}

		EmLagTime = n;
	}
}

LOCALPROC RunOnEndOfSixtieth(void)
{
	OnTrueTime = TrueEmulatedTime;
	RunEmulatedTicksToTrueTime();
}

LOCALPROC ZapOSGLUVars(void)
{
	InitDrives();
	ZapWinStateVars();
}

LOCALPROC ReserveAllocAll(void)
{
#if dbglog_HAVE
	dbglog_ReserveAlloc();
#endif
	ReserveAllocOneBlock(&ROM, kROM_Size, 5, falseblnr);

	ReserveAllocOneBlock(&screencomparebuff,
		vMacScreenNumBytes, 5, trueblnr);
#if UseControlKeys
	ReserveAllocOneBlock(&CntrlDisplayBuff,
		vMacScreenNumBytes, 5, falseblnr);
#endif

	ReserveAllocOneBlock(&CLUT_final, CLUT_finalsz, 5, falseblnr);
#if MySoundEnabled
	ReserveAllocOneBlock((ui3p *)&TheSoundBuffer,
		dbhBufferSize, 5, falseblnr);
#endif

	EmulationReserveAlloc();
}

LOCALFUNC blnr AllocMyMemory(void)
{
	uimr n;
	blnr IsOk = falseblnr;

	ReserveAllocOffset = 0;
	ReserveAllocBigBlock = nullpr;
	ReserveAllocAll();
	n = ReserveAllocOffset;
	ReserveAllocBigBlock = (ui3p)calloc(1, n);
	if (NULL == ReserveAllocBigBlock) {
		MacMsg(kStrOutOfMemTitle, kStrOutOfMemMessage, trueblnr);
	} else {
		ReserveAllocOffset = 0;
		ReserveAllocAll();
		if (n != ReserveAllocOffset) {
		} else {
			IsOk = trueblnr;
		}
	}

	return IsOk;
}

LOCALPROC UnallocMyMemory(void)
{
	if (nullpr != ReserveAllocBigBlock) {
		free((char *)ReserveAllocBigBlock);
	}
}

LOCALFUNC blnr InitOSGLU(void)
{
	if (AllocMyMemory())

#if dbglog_HAVE
	if (dbglog_open())
#endif

#if MySoundEnabled
	if (MySound_Init())
#endif

	if (Screen_Init())
	if (CreateMainWindow())
	if (LoadMacRom())
	if (LoadInitialImages())
	if (InitLocationDat())
	if (InitEmulation())
	{
		return trueblnr;
	}
	return falseblnr;
}

LOCALPROC UnInitOSGLU(void)
{
	if (MacMsgDisplayed) {
		MacMsgDisplayOff();
	}

	RestoreKeyRepeat();
#if MayFullScreen
	UngrabMachine();
#endif
#if MySoundEnabled
	MySound_Stop();
#endif
#if MySoundEnabled
	MySound_UnInit();
#endif
#if IncludePbufs
	UnInitPbufs();
#endif
	UnInitDrives();

	ForceShowCursor();

#if dbglog_HAVE
	dbglog_close();
#endif

	UnallocMyMemory();

	CheckSavedMacMsg();

	if (xwin != NULL) {
		xwin_destroy(xwin);
		xwin = NULL;
	}

	if (screen_buffer != NULL) {
		graph_free(screen_buffer);
		screen_buffer = NULL;
	}

	if (x_context != NULL) {
		free(x_context);
		x_context = NULL;
	}
}

int main(int argc, char **argv)
{
	my_argc = argc;
	my_argv = argv;

	ZapOSGLUVars();
	if (InitOSGLU()) {
		LeaveSpeedStopped();
		x_run(x_context, xwin);
	}
	UnInitOSGLU();

	return 0;
}
