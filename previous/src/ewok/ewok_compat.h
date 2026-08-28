/*
 *  ewok_compat.h - declarations for libc shims missing on EwokOS
 *
 *  Force-included into all Previous sources (see Makefile).
 */

#ifndef PREVIOUS_EWOK_COMPAT_H
#define PREVIOUS_EWOK_COMPAT_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>

/* EwokOS sys/errno.h and sys/stat.h have no extern "C" guards,
 * so C++ TUs (i860.cpp) would mangle __errno()/stat()/mkdir() -- wrap them */
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/errno.h>
#include <sys/stat.h>
#ifdef __cplusplus
}
#endif

/* EwokOS fcntl.h has no O_ACCMODE */
#ifndef O_ACCMODE
#define O_ACCMODE 3
#endif

/* EwokOS has no alloca() macro */
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

/* EwokOS stdio.h does not define FILENAME_MAX */
#ifndef FILENAME_MAX
#define FILENAME_MAX 4096
#endif

/* EwokOS signal.h has no SIGPIPE/SIGFPE */
#include <signal.h>
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGFPE
#define SIGFPE 8
#endif

/* EwokOS sys/socket.h already provides struct ip; tell slirp/ip.h
 * to reuse it instead of redefining it */
#include <sys/socket.h>
#define EWOK_STRUCT_IP_PROVIDED 1

/* socket options / addrinfo flags not defined by the EwokOS headers */
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef SO_OOBINLINE
#define SO_OOBINLINE 10
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x04
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#ifndef EDESTADDRREQ
#define EDESTADDRREQ 89
#endif
#ifndef MSG_OOB
#define MSG_OOB 0x01
#endif
#ifndef MSG_PEEK
#define MSG_PEEK 0x02
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
#include <sys/ioctl.h>
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

/* BSD-ish typedefs missing from the EwokOS libc */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef char *caddr_t;

#ifdef __cplusplus
extern "C" {
#endif

/* provided by src/ewok/ewok_compat.c */
int fputc(int c, FILE *stream);

/* src/ewok/ewok_assets.c: copy bundled disks to the user dir and
 * auto-mount them on the free SCSI targets */
void Ewok_AutoMountDisks(void);

/* src/ewok/ewok_assets.c: deferred preparation of the pending user
 * copies (copy/unzip with splash); call once the window is visible
 * and before the SCSI layer opens the drives */
void Ewok_PrepareUserDisks(void);

/* src/ewok/ewok_assets.c: stage the bundled ROM/EEPROM files into
 * the writable user dir (<home>/.previous/roms) and point the config
 * at the user copies, so the "missing ROM" dialog does not appear at
 * startup and ROM writes never touch the /apps tree */
void Ewok_FixAssetPaths(void);

/* src/ewok/ewok_compat.c: turn on the libc heap lock before SDL threads
 * are spawned (call once, single-threaded, early in main()) */
void Ewok_EnableHeapLock(void);

#ifdef __cplusplus
}
#endif

#endif /* PREVIOUS_EWOK_COMPAT_H */
