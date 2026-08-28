/*
 *  config.h - Build configuration for Previous on EwokOS
 *
 *  Replaces the cmake-generated config.h (see cmake/config-cmake.h for
 *  the upstream template).
 */

#ifndef PREVIOUS_EWOK_CONFIG_H
#define PREVIOUS_EWOK_CONFIG_H

/* zlib is available in the EwokOS SDK */
#define HAVE_LIBZ 1
#define HAVE_ZLIB_H 1

/* Headers present in the EwokOS libc */
#define HAVE_STRINGS_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_SDL2_SDL_CONFIG_H 1

/* Functions present in the EwokOS libc */
#define HAVE_SETENV 1
#define HAVE_SELECT 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_NANOSLEEP 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1

/* Not available in the EwokOS libc: the bundled fallbacks in
 * scandir.c are compiled instead */
/* #undef HAVE_SCANDIR */
/* #undef HAVE_ALPHASORT */

/* Not available / not used on EwokOS */
/* #undef HAVE_LIBPNG */
/* #undef HAVE_LIBREADLINE */
/* #undef HAVE_PORTAUDIO */
/* #undef HAVE_X11 */
/* #undef HAVE_GLOB_H */
/* #undef HAVE_UNIX_DOMAIN_SOCKETS */
/* #undef HAVE_CFMAKERAW */

/* Relative path from bindir to datadir:
 * the app lives in /apps/previous, assets in /apps/previous/res */
#define BIN2DATADIR "res"

/* Enable DSP 56k emulation */
#define ENABLE_DSP_EMU 1

/* Enable trace logs */
#define ENABLE_TRACING 1

#endif /* PREVIOUS_EWOK_CONFIG_H */
