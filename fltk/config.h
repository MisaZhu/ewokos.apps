/*
 * Hand-written FLTK configuration for EwokOS + NX11 (Nano-X Xlib layer).
 *
 * FLTK normally generates this from configh.in via ./configure.  EwokOS is
 * cross-built with no configure run, so the values below are chosen to match
 * the aarch64/virt SDK libc (system/basic/libc) and the NX11 Xlib subset that
 * projects/nx11 installs into $(SDK_DIR)/include/nx11.
 *
 * Two archives are built: the core libfltk (every widget + XPM/XBM/BMP
 * images) and libfltk_images (GIF/PNG/JPEG/PNM loaders).  The external codecs
 * are enabled below and come from the SDK's libpng/libjpeg/libz; there is
 * still no OpenGL, no Xft and no X double-buffer extension.
 *
 * NOTE: this file must be found BEFORE the SDK's own <config.h>, so the FLTK
 * Makefile puts -I<fltk-root> ahead of -I$(SDK_DIR)/include.
 */

#ifndef FLTK_EWOKOS_CONFIG_H
#define FLTK_EWOKOS_CONFIG_H

/* Where to find files (unused on EwokOS) */
#define FLTK_DATADIR	""
#define FLTK_DOCDIR	""

/* Thickness of FL_UP_BOX / FL_DOWN_BOX (2 = modern look) */
#define BORDER_WIDTH 2

/* --- graphics backends we do NOT have ---------------------------------- */
#define HAVE_GL 0
/* #undef HAVE_GL_GLU_H */
/* #undef HAVE_GLXGETPROCADDRESSARB */

/* TrueColor-only would let us drop USE_COLORMAP, but keep FLTK's default so
 * fl_draw_image's colormap path stays compiled in (harmless on TrueColor). */
#define USE_COLORMAP 1

/* NX11 exposes no Xinerama / Xft / Xdbe / overlay extensions */
#define HAVE_XINERAMA 0
#define USE_XFT 0
#define HAVE_XDBE 0
#define USE_XDBE HAVE_XDBE
#define USE_QUARTZ 0
/* #undef __APPLE_QUARTZ__ */
/* #undef __APPLE_QD__ */
#define HAVE_OVERLAY 0
#define HAVE_GL_OVERLAY HAVE_OVERLAY

/* aarch64 is little-endian */
#define WORDS_BIGENDIAN 0

/* Types used by fl_draw_image; aarch64 has all three widths */
#define U16 unsigned short
#define U32 unsigned int
#define U64 unsigned long long

/* --- directory scanning ------------------------------------------------ */
/* EwokOS has <dirent.h> but no scandir()/alphasort(); FLTK's bundled
 * scandir.c + numericsort.c provide them when HAVE_SCANDIR is 0. */
#define HAVE_DIRENT_H 1
/* #undef HAVE_SYS_NDIR_H */
/* #undef HAVE_SYS_DIR_H */
/* #undef HAVE_NDIR_H */
/* #undef HAVE_SCANDIR */
/* #undef HAVE_SCANDIR_POSIX */

/* --- printf-style functions (present in EwokOS <stdio.h>) -------------- */
#define HAVE_VSNPRINTF 1
#define HAVE_SNPRINTF 1

/* --- string functions (present in EwokOS <string.h>/<strings.h>) ------- */
#define HAVE_STRINGS_H 1
#define HAVE_STRCASECMP 1
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1

/* --- locale: EwokOS has no <locale.h> ---------------------------------- */
/* #undef HAVE_LOCALE_H */
/* #undef HAVE_LOCALECONV */

/* --- select(): EwokOS provides <sys/select.h> -------------------------- */
#define HAVE_SYS_SELECT_H 1
/* #undef HAVE_SYS_STDTYPES_H */
/* fl_wait()/fl_ready() are rewritten for NONETWORK nano-X and never call
 * select()/poll() themselves, so USE_POLL stays 0. */
#define USE_POLL 0

/* --- external image libraries (used by libfltk_images) ----------------- */
/* The SDK ships libpng 1.6.12 + libjpeg + libz with headers directly in
 * include/, so src/Fl_{PNG,JPEG}_Image.cxx compile their real decoders in and
 * apps link -lfltk_images -lpng -ljpeg -lz.  png_get_valid() and
 * png_set_tRNS_to_alpha() both exist in 1.6.12, so the tRNS->alpha
 * transparency path is enabled.  HAVE_LIBPNG_PNG_H stays undefined because
 * png.h lives at include/png.h, not include/libpng/png.h.  GIF/PNM/BMP need no
 * external lib and are always built into libfltk_images. */
#define HAVE_LIBPNG 1
#define HAVE_LIBZ 1
#define HAVE_LIBJPEG 1
#define HAVE_PNG_H 1
/* #undef HAVE_LIBPNG_PNG_H */
#define HAVE_PNG_GET_VALID 1
#define HAVE_PNG_SET_TRNS_TO_ALPHA 1

/* --- threading: keep Fl::lock() inert; FLTK apps here are single-threaded */
/* #undef HAVE_PTHREAD */
/* #undef HAVE_PTHREAD_H */

/* --- audio: none ------------------------------------------------------- */
/* #undef HAVE_ALSA_ASOUNDLIB_H */

/* --- long long / strtoll (aarch64 has both) ---------------------------- */
#define HAVE_LONG_LONG 1
#ifdef HAVE_LONG_LONG
#  define FLTK_LLFMT	"%lld"
#  define FLTK_LLCAST	(long long)
#else
#  define FLTK_LLFMT	"%ld"
#  define FLTK_LLCAST	(long)
#endif

#define HAVE_STRTOLL 1
#ifndef HAVE_STRTOLL
#  define strtoll(nptr,endptr,base) strtol((nptr), (endptr), (base))
#endif

/* --- dlopen/dlsym: not available --------------------------------------- */
/* #undef HAVE_DLFCN_H */
/* #undef HAVE_DLSYM */

#endif /* FLTK_EWOKOS_CONFIG_H */
