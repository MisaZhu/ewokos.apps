PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

## ---------------------------------------------------------------------------
## NX11 - microwindows (Nano-X engine/server + NX11 Xlib) on EwokOS.
##
## Every top-level nano-X window *is* an EwokOS xwin window: the EwokOS xserver
## draws the frame and owns stacking/focus/cursor, while nano-X renders into a
## screen-sized virtual framebuffer and mirrors the dirty rectangles into each
## window workspace with graph_blt (g2d).  See src/nanox/win_ewokos.c and
## src/drivers/scr_ewokos.c.
##
## Builds two libraries into the SDK:
##   libnano-X.a  engine + screen/mouse/kbd drivers + builtin fonts + nano-X
##   libNX11.a    the Xlib-compatible client API
## ---------------------------------------------------------------------------

## src/include/mwconfig.h is entirely #ifndef guarded, so every Microwindows
## knob is set from here instead of by editing the upstream header.  The getc()
## mapping is a libc gap: EwokOS offers fgetc() but no getc(), and
## engine/font_fnt.c is the only user of it in the whole tree.
MW_DEFS = \
	-DEWOKOS=1 \
	-DUNIX=1 \
	-DNONETWORK=1 \
	-DNANOWM=0 \
	-DHAVE_MMAP=0 \
	-DHAVE_FILEIO=1 \
	-DHAVE_BMP_SUPPORT=1 \
	-DHAVE_XPM_SUPPORT=1 \
	-DHAVE_FNT_SUPPORT=1 \
	-DMW_FONT_DIR=\"/usr/nx11/fonts\" \
	-DFNT_FONT_DIR=\"/usr/nx11/fonts/fnt\" \
	-DX11_RGBTXT=\"/usr/nx11/rgb.txt\" \
	-D'getc(f)=fgetc(f)'

MW_INC = -I src/include -I src/drivers -I src/nanox -I src/nx11

## The upstream sources are not -Wall -Wextra clean and need the POSIX
## declarations that the SDK's -std=c99 hides; MW_CFLAGS is appended after
## CFLAGS so that -std=gnu99 and the warning switches win.
##
## -fno-strict-aliasing is required, not just cosmetic: Microwindows type-puns
## pixel buffers (engine/convblit_*.c) and XEHeadOfExtensionList() puns its
## union argument.  The arm make.rule already passes it, aarch64 does not.
## -Wno-stringop-truncation silences image_xpm.c, whose strncpy() calls are
## bounded by a switch on strlen() that gcc cannot see through.
MW_CFLAGS = -std=gnu99 -fno-strict-aliasing $(MW_DEFS) $(MW_INC) \
	-Wno-sign-compare \
	-Wno-unused-variable \
	-Wno-unused-but-set-variable \
	-Wno-missing-field-initializers \
	-Wno-unused-function \
	-Wno-comment \
	-Wno-parentheses \
	-Wno-char-subscripts \
	-Wno-missing-braces \
	-Wno-format \
	-Wno-empty-body \
	-Wno-maybe-uninitialized \
	-Wno-builtin-declaration-mismatch \
	-Wno-stringop-truncation

## engine: devclip1/devclip2 are #included by devclip.c, never compiled alone
ENGINE_SRCS = convblit_8888 convblit_frameb convblit_mask devarc devblit \
	devclip devdraw devfont devimage devimage_stretch devkbd devlist \
	devmouse devopen devpal1 devpal2 devpal4 devpal8 devpoly devrgn devrgn2 \
	devstipple devtimer font_dbcs font_fnt image_bmp image_gif image_jpeg \
	image_png image_pnm image_tiff image_xpm selfont

## drivers: only the EwokOS screen/mouse/keyboard backends plus the generic
## framebuffer helpers they are built on
DRIVER_SRCS = fb fblin1 fblin2 fblin4 fblin8 fblin16 fblin24 fblin32 \
	fbportrait_down fbportrait_left fbportrait_right genfont genmem \
	kbd_ewokos mou_ewokos osdep rasterops scr_ewokos

FONT_SRCS = rom8x8 rom8x16 X6x13 winFreeSansSerif11x13 winFreeSystem14x16

## nanox: srvclip1/srvclip2 are #included by srvclip.c, never compiled alone
NANOX_SRCS = nxdraw nxpaintnc nxtransform nxutil srvclip srvevent srvfunc \
	srvmain srvnonet srvutil win_ewokos

NX11_SRCS = AllocColor Atom Backgnd BdrWidth Bell Border ChProperty ChSaveSet \
	ChWindow ClassHint ClDisplay Clear ClearArea Colormap Colorname Context \
	Copy CrBFData CrCursor CrGC CrPFBData CrPixmap CrWindow DefCursor \
	DestWind DrArc DrLine DrLines DrPoint DrRect ErrorHandler Extension \
	FillPolygon FillRct Flush font_find FontCursor FontInfo fontlist Free \
	FreeGC GetGCVals GetGeom GetPntMap GetWMProps Grab Image ListPix Locale \
	LowerWin MapRaised MapWindow Misc MoveWin NextEvent NXext OpenDis \
	ParseColor PmapBgnd QueryBest QueryColor QueryFont QueryPointer \
	QueryTree RaiseWin Region RepWindow Request Resource Screen Selection \
	SelInput SetAttributes SetClip SetIFocus SetWMProps SetWMProto Shape \
	StName StrKeysym StrToText stub Sync Text Text16 TextExt TextToStr \
	Threads UndefCurs UnloadFont UnmapWin Visual Window WindowProperty \
	Xinerama XKB XMisc

ENGINE_OBJS = $(patsubst %,$(ARCH)/engine/%.o,$(ENGINE_SRCS))
DRIVER_OBJS = $(patsubst %,$(ARCH)/drivers/%.o,$(DRIVER_SRCS))
FONT_OBJS   = $(patsubst %,$(ARCH)/fonts/%.o,$(FONT_SRCS))
NANOX_OBJS  = $(patsubst %,$(ARCH)/nanox/%.o,$(NANOX_SRCS))
NX11_OBJS   = $(patsubst %,$(ARCH)/nx11/%.o,$(NX11_SRCS))

NANOX_ALL_OBJS = $(ENGINE_OBJS) $(DRIVER_OBJS) $(FONT_OBJS) $(NANOX_OBJS)

LIB_DIR  = $(SDK_DIR)/lib
INC_DIR  = $(SDK_DIR)/include/nx11
DATA_DIR = $(SDK_DIR)/rootfs/usr/nx11

LIBNANOX = $(LIB_DIR)/libnano-X.a
LIBNX11  = $(LIB_DIR)/libNX11.a

all: libs headers data demos
	@echo "nx11 done."

libs: $(LIBNANOX) $(LIBNX11)

## public headers go into include/nx11 so that both <nano-X.h> and
## <X11/Xlib.h> resolve with a single -I $(SDK_DIR)/include/nx11
headers: libs
	@mkdir -p $(INC_DIR)/X11
	@cp -Rp src/include/. $(INC_DIR)/
	@cp -Rp src/nx11/X11/. $(INC_DIR)/X11/

## runtime data: .fnt fonts and the X11 colour database (X11_RGBTXT)
data:
	@mkdir -p $(DATA_DIR)/fonts/fnt
	@cp -Rp src/fonts/fnt/. $(DATA_DIR)/fonts/fnt/
	@cp -p src/fonts/rgb.txt $(DATA_DIR)/rgb.txt

demos: headers
	@$(MAKE) -C demos

$(LIBNANOX): $(NANOX_ALL_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(NANOX_ALL_OBJS)

$(LIBNX11): $(NX11_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(NX11_OBJS)

## Source groups
$(ARCH)/engine/%.o: src/engine/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MW_CFLAGS) -c $< -o $@

$(ARCH)/drivers/%.o: src/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MW_CFLAGS) -c $< -o $@

$(ARCH)/fonts/%.o: src/fonts/compiled/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MW_CFLAGS) -c $< -o $@

$(ARCH)/nanox/%.o: src/nanox/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MW_CFLAGS) -c $< -o $@

$(ARCH)/nx11/%.o: src/nx11/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MW_CFLAGS) -c $< -o $@

.PHONY: all libs headers data demos clean

clean:
	rm -rf $(ARCH)
	rm -f $(LIBNANOX) $(LIBNX11)
	rm -rf $(INC_DIR)
	@$(MAKE) -C demos clean
