/*
 *  config.h - Build configuration for EwokOS
 *
 *  Basilisk II (C) Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef B2_CONFIG_H
#define B2_CONFIG_H

/* EwokOS has no mmap/mprotect: use the banked memory model and the
 * malloc-based vm_alloc replacement in src/ewok. */
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_TIME_H 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_NANOSLEEP 1
#define USE_NANOSLEEP 1
#define STDC_HEADERS 1

#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_VOID_P 8

/* pthreads are available on EwokOS */
#define HAVE_PTHREADS 1

/* Video/audio are provided by the EwokOS xwin backend (src/ewok),
 * no SDL is linked.  USE_SDL_VIDEO/USE_SDL_AUDIO are kept defined so
 * that main_unix.cpp skips the X11 path and SDL initialization. */
#define USE_SDL_VIDEO 1
#define USE_SDL_AUDIO 1
#define ENABLE_SDL2 1
#define USE_EWOK_XWIN 1

/* No JIT compiler (x86-only backend upstream): run the 68k interpreter */
#define USE_JIT 0

/* FPU core */
#define FPU_UAE 1
#define IEEE_DOUBLE_BIG_ENDIAN 0

/* Math functions available */
#define HAVE_ATANH 1
#define HAVE_ISNAN 1
#define HAVE_ISINF 1
#define HAVE_FINITE 1
#define HAVE_ISNORMAL 1
#define HAVE_SIGNBIT 1

/* Misc */
#define HAVE_STRDUP 1

#endif /* B2_CONFIG_H */
