/*
 *  ewok_compat.c - libc shims for functions missing on EwokOS
 *
 *  tzset()/fsync()/ftruncate()/system() are provided by the EwokOS libc
 *  itself, only genuinely missing functions live here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* fputc() is not in the EwokOS libc, map it onto putc() */
int fputc(int c, FILE *stream)
{
	return putc(c, stream);
}
