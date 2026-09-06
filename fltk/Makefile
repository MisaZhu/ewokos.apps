PROJS_ROOT_DIR=..
include $(PROJS_ROOT_DIR)/make.inc

## ---------------------------------------------------------------------------
## FLTK 1.1 (Fast Light Tool Kit) core on EwokOS, rendering through NX11.
##
## FLTK talks to the display with plain Xlib, so it rides on the NX11 stack
## that projects/nx11 already installs into the SDK: every Fl_Window becomes a
## Nano-X top-level window, which the NX11 bridge maps onto a real EwokOS xwin
## window composited by xserverd.
##
## Two EwokOS-specific things make this work:
##   * config.h (hand-written, this directory) tells FLTK there is no GL, no
##     Xft, no Xdbe, no external image codecs and no locale/scandir in libc.
##     It MUST be found before the SDK's own <config.h>, hence -I. first.
##   * src/Fl_x.cxx fl_wait()/fl_ready() are patched (#if EWOKOS) to drive the
##     NONETWORK nano-X event node via nx11x_select() instead of select()ing a
##     dummy connection fd that would never become readable.
##
## Builds libfltk.a (core widgets + XBM/XPM/BMP images) into the SDK and
## installs the FL/ headers, then builds the demos.
## ---------------------------------------------------------------------------

## EWOKOS selects the patched fl_wait()/fl_ready() in Fl_x.cxx.  The getc()
## mapping is a libc gap: EwokOS offers fgetc() but no getc(), used by
## Fl_BMP_Image.cxx and Fl_Browser_load.cxx (same shim nx11 uses).
FLTK_DEFS = -DEWOKOS=1 -D'getc(f)=fgetc(f)'

## -I. first so that FLTK's config.h and FL/ win over the SDK's config.h;
## -Isrc for the internal headers (flstring.h, Fl_Font.H, ...) and the
## platform .cxx files that the public sources #include; the NX11 dir for
## <X11/Xlib.h>, <nano-X.h> and <ewokos.h>.
FLTK_INC = -I. -Isrc -I$(SDK_DIR)/include/nx11

## Third-party code: make.rule passes -Wall -Wextra -pedantic, which floods the
## log on FLTK 1.1's old C++ (long long, anonymous structs, signedness, ...).
## -w inhibits warnings for these vendored sources so real errors stay visible.
FLTK_WARN = -w

## Core translation units (verbatim from FLTK's own src/Makefile CPPFILES).
## Platform variants such as fl_set_fonts_x.cxx are #included by their public
## sibling and are therefore NOT listed here.
CPP_SRCS = \
	Fl Fl_Adjuster Fl_Bitmap Fl_Browser Fl_Browser_ Fl_Browser_load \
	Fl_Box Fl_Button Fl_Chart Fl_Check_Browser Fl_Check_Button Fl_Choice \
	Fl_Clock Fl_Color_Chooser Fl_Counter Fl_Dial Fl_Double_Window \
	Fl_File_Browser Fl_File_Chooser Fl_File_Chooser2 Fl_File_Icon \
	Fl_File_Input Fl_Group Fl_Help_View Fl_Image Fl_Input Fl_Input_ \
	Fl_Light_Button Fl_Menu Fl_Menu_ Fl_Menu_Bar Fl_Sys_Menu_Bar \
	Fl_Menu_Button Fl_Menu_Window Fl_Menu_add Fl_Menu_global Fl_Multi_Label \
	Fl_Overlay_Window Fl_Pack Fl_Pixmap Fl_Positioner Fl_Preferences \
	Fl_Progress Fl_Repeat_Button Fl_Return_Button Fl_Roller Fl_Round_Button \
	Fl_Scroll Fl_Scrollbar Fl_Shared_Image Fl_Single_Window Fl_Slider \
	Fl_Tabs Fl_Text_Buffer Fl_Text_Display Fl_Text_Editor Fl_Tile \
	Fl_Tiled_Image Fl_Tooltip Fl_Valuator Fl_Value_Input Fl_Value_Output \
	Fl_Value_Slider Fl_Widget Fl_Window Fl_Window_fullscreen \
	Fl_Window_hotspot Fl_Window_iconize Fl_Wizard Fl_XBM_Image Fl_XPM_Image \
	Fl_abort Fl_add_idle Fl_arg Fl_compose Fl_display Fl_get_key \
	Fl_get_system_colors Fl_grab Fl_lock Fl_own_colormap Fl_visual Fl_x \
	filename_absolute filename_expand filename_ext filename_isdir \
	filename_list filename_match filename_setext fl_arc fl_arci fl_ask \
	fl_boxtype fl_color fl_cursor fl_curve fl_diamond_box fl_dnd fl_draw \
	fl_draw_image fl_draw_pixmap fl_encoding_latin1 fl_encoding_mac_roman \
	fl_engraved_label fl_file_dir fl_font fl_gtk fl_labeltype fl_line_style \
	fl_open_uri fl_oval_box fl_overlay fl_overlay_visual fl_plastic \
	fl_read_image fl_rect fl_round_box fl_rounded_box fl_set_font \
	fl_set_fonts fl_scroll_area fl_shadow_box fl_shortcut fl_show_colormap \
	fl_symbols fl_vertex screen_xywh

## fl_call_main.c is Windows/HP-UX only and compiles to an empty TU here, so
## it is dropped.  vsnprintf.c self-guards on HAVE_VSNPRINTF (defined in
## config.h) and compiles to nothing, but is kept to match upstream.
C_SRCS = flstring scandir numericsort vsnprintf

CPP_OBJS = $(patsubst %,$(ARCH)/%.o,$(CPP_SRCS))
C_OBJS   = $(patsubst %,$(ARCH)/%.o,$(C_SRCS))
OBJS     = $(CPP_OBJS) $(C_OBJS)

LIB_DIR = $(SDK_DIR)/lib
INC_DIR = $(SDK_DIR)/include
LIBFLTK = $(LIB_DIR)/libfltk.a

all: lib headers demos
	@echo "fltk done."

lib: $(LIBFLTK)

$(LIBFLTK): $(OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(OBJS)

## public headers: apps include <FL/Fl.H> from -I$(SDK_DIR)/include.
## config.h is deliberately NOT installed - it is an implementation detail and
## would clash with the SDK's own <config.h>; no FL/ header needs it.
headers: lib
	@mkdir -p $(INC_DIR)/FL
	@cp -Rp FL/. $(INC_DIR)/FL/

demos: headers
	@$(MAKE) -C demos

## FLTK sources use the .cxx extension, which make.rule has no rule for.
$(ARCH)/%.o: src/%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(FLTK_INC) $(CXXFLAGS) $(FLTK_DEFS) $(FLTK_WARN) -c $< -o $@

$(ARCH)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLTK_INC) $(CFLAGS) $(FLTK_DEFS) $(FLTK_WARN) -c $< -o $@

.PHONY: all lib headers demos clean

clean:
	rm -rf $(ARCH)
	rm -f $(LIBFLTK)
	rm -rf $(INC_DIR)/FL
	@$(MAKE) -C demos clean
