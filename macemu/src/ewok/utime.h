/*
 *  utime.h - utime for EwokOS
 *
 *  libewoksys now provides struct utimbuf and utime() in sys/time.h,
 *  so this shim just forwards to the system header.
 */

#ifndef UTIME_H
#define UTIME_H

#include <sys/time.h>

#endif /* UTIME_H */
