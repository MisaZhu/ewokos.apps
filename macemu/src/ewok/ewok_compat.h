/*
 *  ewok_compat.h - declarations for libc shims missing on EwokOS
 *
 *  Force-included into all Basilisk II sources (see Makefile).
 */

#ifndef EWOK_COMPAT_H
#define EWOK_COMPAT_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

/* EwokOS sys/errno.h and sys/stat.h have no extern "C" guards,
 * so C++ TUs would mangle __errno()/stat()/fstat()/mkdir() -- wrap them */
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

#ifdef __cplusplus
extern "C" {
#endif

void tzset(void);
int fsync(int fd);
int ftruncate(int fd, off_t length);
int system(const char *command);

/* creat() is a classic Unix idiom not present in the EwokOS libc */
static inline int creat(const char *path, mode_t mode)
{
	return open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

#ifdef __cplusplus
}
#endif

#endif /* EWOK_COMPAT_H */
