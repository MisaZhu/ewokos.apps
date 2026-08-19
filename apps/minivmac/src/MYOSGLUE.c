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
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ewoksys/klog.h>
#include <ewoksys/cmain.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/session.h>
#include <ewoksys/timer.h>
#include <ewoksys/keydef.h>
#include <ewoksys/vfs.h>
#include <pthread.h>

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

/*
    Frame hand-off between the emulation thread (producer) and the
    GUI loop (consumer), same pattern as the emu app: the emulation
    thread converts changed screen regions into screen_buffer under
    frame_lock and raises frame_pending; the GUI loop repaints only
    when a new frame is pending, so slow repaints never stall
    emulation (and thus never stall sound sample production).
*/
LOCALVAR pthread_mutex_t frame_lock;
LOCALVAR volatile blnr frame_pending = falseblnr;

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

LOCALFUNC size_t write_all_fd(int fd, const void *buffer, size_t count)
{
    size_t total = 0;

    while (total < count) {
        ssize_t nwritten = write(fd, (const char *)buffer + total,
            count - total);
        if (nwritten <= 0) {
            break;
        }
        total += (size_t)nwritten;
    }

    return total;
}

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
    int fd = fileno(refnum);
    ui5r NewSony_Count = 0;

    if ((fd >= 0) && (lseek(fd, (off_t)Sony_Start, SEEK_SET) >= 0)) {
        if (IsWrite) {
            NewSony_Count = write_all_fd(fd, Buffer, (size_t)Sony_Count);
        } else {
            ssize_t nread = read(fd, Buffer, (size_t)Sony_Count);

            if (nread > 0) {
                NewSony_Count = (ui5r)nread;
            }
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
    int fd = fileno(refnum);
    off_t v;

    if (fd < 0) {
        return err;
    }

    v = lseek(fd, 0, SEEK_END);
    if (v >= 0) {
        *Sony_Count = (ui5r)v;
        err = mnvm_noErr;
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

static blnr has_dsk_suffix(const char* name) {
    size_t len;

    if (NULL == name) {
        return falseblnr;
    }

    len = strlen(name);
    if (len < 4) {
        return falseblnr;
    }

    return strcmp(name + len - 4, ".dsk") == 0;
}

static int compare_disk_names(const void* a, const void* b) {
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

static void free_disk_names(char** names, size_t count) {
    size_t i;

    if (NULL == names) {
        return;
    }

    for (i = 0; i < count; ++i) {
        free(names[i]);
    }
    free(names);
}

static char** collect_disk_names(const char* dir_path, size_t* out_count) {
    DIR* dirp;
    struct dirent* entry;
    char** names = NULL;
    size_t count = 0;
    size_t capacity = 0;

    *out_count = 0;

    dirp = opendir(dir_path);
    if (NULL == dirp) {
        return NULL;
    }

    while ((entry = readdir(dirp)) != NULL) {
        char* copy;
        size_t len;
        char** new_names;

        if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (! has_dsk_suffix(entry->d_name)) {
            continue;
        }

        if (count == capacity) {
            capacity = (capacity == 0) ? 8 : (capacity * 2);
            new_names = (char**)realloc(names, capacity * sizeof(char*));
            if (NULL == new_names) {
                free_disk_names(names, count);
                closedir(dirp);
                return NULL;
            }
            names = new_names;
        }

        len = strlen(entry->d_name) + 1;
        copy = (char*)malloc(len);
        if (NULL == copy) {
            free_disk_names(names, count);
            closedir(dirp);
            return NULL;
        }
        memcpy(copy, entry->d_name, len);
        names[count++] = copy;
    }

    closedir(dirp);

    if (count > 1) {
        qsort(names, count, sizeof(char*), compare_disk_names);
    }

    *out_count = count;
    return names;
}

static const char* get_res_name(const char* name) {
    static char res_name[256] = {0};
    snprintf(res_name, sizeof(res_name), "%s/res/%s", cmain_get_own_dir(NULL, 0), name);
    return res_name;
}

static blnr ensure_dir_exists(const char* path) {
    char tmp[512];
    char* p;

    if (NULL == path || path[0] == 0) {
        return falseblnr;
    }

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p != 0; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = 0;
        if (access(tmp, F_OK) != 0 && mkdir(tmp, 0755) != 0) {
            *p = '/';
            return falseblnr;
        }
        *p = '/';
    }

    if (access(tmp, F_OK) != 0 && mkdir(tmp, 0755) != 0) {
        return falseblnr;
    }
    return trueblnr;
}

typedef void (*copy_progress_proc)(off_t done, off_t file_size, void* arg);

static blnr copy_file_if_needed(const char* src_path, const char* dst_path,
    copy_progress_proc progress, void* arg) {
    int src_fd;
    int dst_fd;
    char buffer[1024*32];
    ssize_t nread;
    off_t file_size = 0;
    off_t done = 0;
    struct stat st;

    if (access(dst_path, F_OK) == 0) {
        return trueblnr;
    }

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        return falseblnr;
    }

    if (fstat(src_fd, &st) == 0) {
        file_size = st.st_size;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0) {
        close(src_fd);
        return falseblnr;
    }

    while ((nread = read(src_fd, buffer, sizeof(buffer))) > 0) {
        if (write_all_fd(dst_fd, buffer, (size_t)nread) != (size_t)nread) {
            close(dst_fd);
            close(src_fd);
            unlink(dst_path);
            return falseblnr;
        }
        done += nread;
        if (progress != NULL) {
            progress(done, file_size, arg);
        }
    }

    if (nread < 0) {
        close(dst_fd);
        close(src_fd);
        unlink(dst_path);
        return falseblnr;
    }

    if (close(dst_fd) != 0) {
        close(src_fd);
        unlink(dst_path);
        return falseblnr;
    }
    if (close(src_fd) != 0) {
        unlink(dst_path);
        return falseblnr;
    }
    return trueblnr;
}

static blnr get_user_disks_dir(char* path, size_t size) {
    session_info_t sinfo;

    if (NULL == path || size == 0) {
        return falseblnr;
    }

    if (session_get_by_uid(getuid(), &sinfo) != 0 || sinfo.home[0] == 0) {
        return falseblnr;
    }

    snprintf(path, size, "%s/docs/minivmac/disks", sinfo.home);
    return trueblnr;
}

static blnr prepare_user_disk(const char* src_dir, const char* dst_dir,
    const char* disk_name, char* out_path, size_t out_size,
    copy_progress_proc progress, void* arg)
{
    char src_path[512];

    if (! ensure_dir_exists(dst_dir)) {
        return falseblnr;
    }

    snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, disk_name);
    snprintf(out_path, out_size, "%s/%s", dst_dir, disk_name);

    return copy_file_if_needed(src_path, out_path, progress, arg);
}

/*
    Disk-copy progress reporting, macemu-style: while the shipped
    disk images are copied into the user directory the window is
    already up but the guest is not running yet, so a "copying disk"
    icon with a byte progress bar (drawn by on_xwin_repaint) is
    shown instead of the guest frame.  MyDiskCopySplash() runs on
    the main thread before x_run() starts, so xwin_repaint() pushes
    every update synchronously.
*/

static void MyDiskCopySplash(int done, int total);

static off_t copy_base_bytes = 0;
static off_t copy_total_bytes = 0;
static uint64_t copy_last_splash_ms = 0;

static void copy_progress_cb(off_t done, off_t file_size, void* arg) {
    uint64_t now;
    off_t all;

    (void)arg;
    /* A repaint is a synchronous IPC to the x server: throttle it */
    now = kernel_tic_ms(0);
    if (done != file_size && now - copy_last_splash_ms < 80) {
        return;
    }
    copy_last_splash_ms = now;
    all = copy_base_bytes + done;
    if (all > copy_total_bytes) {
        all = copy_total_bytes;
    }
    MyDiskCopySplash((int)all, (int)copy_total_bytes);
}

LOCALFUNC blnr LoadInitialImages(void)
{
    if (! AnyDiskInserted()) {
        char res_disks_dir[256] = {0};
        char user_disks_dir[256] = {0};
        char disk_path[512] = {0};
        char** disk_names;
        size_t disk_count;
        size_t i;
        size_t limit;

        snprintf(res_disks_dir, sizeof(res_disks_dir), "%s/res/disks",
            cmain_get_own_dir(NULL, 0));

        disk_names = collect_disk_names(res_disks_dir, &disk_count);
        if (NULL == disk_names) {
            return trueblnr;
        }

        if (! get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir))) {
            free_disk_names(disk_names, disk_count);
            return trueblnr;
        }

        limit = (disk_count < (size_t)NumDrives) ? disk_count : (size_t)NumDrives;

        /* Total bytes still missing a user copy, for the splash bar */
        copy_total_bytes = 0;
        for (i = 0; i < limit; ++i) {
            char src_path[512];
            char user_path[512];
            struct stat st;

            snprintf(src_path, sizeof(src_path), "%s/%s",
                res_disks_dir, disk_names[i]);
            snprintf(user_path, sizeof(user_path), "%s/%s",
                user_disks_dir, disk_names[i]);
            if (access(user_path, F_OK) != 0 &&
                    stat(src_path, &st) == 0) {
                copy_total_bytes += st.st_size;
            }
        }

        copy_base_bytes = 0;
        copy_last_splash_ms = 0;
        if (copy_total_bytes > 0) {
            MyDiskCopySplash(0, (int)copy_total_bytes);
        }

        for (i = 0; i < limit; ++i) {
            char src_path[512];
            char user_path[512];
            struct stat st;
            blnr needs_copy;

            snprintf(user_path, sizeof(user_path), "%s/%s",
                user_disks_dir, disk_names[i]);
            needs_copy = (access(user_path, F_OK) != 0);

            if (! prepare_user_disk(res_disks_dir, user_disks_dir,
                    disk_names[i], disk_path, sizeof(disk_path),
                    copy_progress_cb, NULL)) {
                continue;
            }
            if (needs_copy) {
                snprintf(src_path, sizeof(src_path), "%s/%s",
                    res_disks_dir, disk_names[i]);
                if (stat(src_path, &st) == 0) {
                    copy_base_bytes += st.st_size;
                }
            }
            if (! Sony_Insert2(disk_path)) {
                break;
            }
        }

        if (copy_total_bytes > 0) {
            MyDiskCopySplash(0, 0);
        }

        free_disk_names(disk_names, disk_count);
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

/*
    Keep the ring half full: sound samples are only produced in
    60Hz emulation ticks, so a deep reserve decouples playback from
    frame cadence jitter (heavy frames, scheduler delays, catch-up
    bursts). 8 of 16 buffers leaves equal room in both directions.
*/
#define DesiredMinFilledSoundBuffs 8

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

LOCALPROC MySound_Start0(void)
{
    ThePlayOffset = 0;
    TheFillOffset = 0;
    TheWriteOffset = 0;
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

/*
    Clock servo: once per second, nudge the emulated clock so the
    sound ring stays near DesiredMinFilledSoundBuffs. This keeps the
    ring away from both underrun and overflow under long term drift
    (max one tick per second, so video cadence is unaffected).
*/
LOCALPROC MySound_SecondNotify0(void)
{
    /* wrap-safe filled length (offsets are ui4b counters) */
    ui4b FilledBuffs =
        (ui4b)(TheFillOffset - ThePlayOffset) >> kLnOneBuffLen;

    if (FilledBuffs <= kSoundBuffers) {
        if (FilledBuffs > DesiredMinFilledSoundBuffs) {
            ++CurEmulatedTime;
        } else if (FilledBuffs < DesiredMinFilledSoundBuffs) {
            --CurEmulatedTime;
        }
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

    /*
     * Returns bytes actually written (>= 0) or a negative errno. The
     * driver uses partial-write semantics: an XRUN mid-write returns
     * the bytes consumed so far, so the caller must advance by the
     * returned count instead of treating a short write as an error.
     */
    if (pcm->running == 0) {
        int err = pcm_prepare(pcm);
        if (err != 0) {
            return err;
        }

        int written = write(pcm->fd, data, count);
        if (written > 0) {
            pcm->running = 1;
        }
        return written;
    }

    return write(pcm->fd, data, count);
}

static int pcm_write(struct pcm *pcm, const void* data, unsigned int count) {
    if (count == 0) return 0;

    int period_bytes = pcm->config.period_size * 4; // 16-bit stereo = 4 bytes per frame
    int bytes = (int)count;
    int written = 0;
    int offset = 0;
    int xrun_retry = 0;

    /*
     * No avail polling here: when the driver ring is full, write()
     * blocks inside vfsd (VFS_EVT_WR) and really sleeps until the
     * playback loop drains a period, so this loop only advances on
     * real progress or runs the XRUN recovery path. Blocking on the
     * device clock is what paces this thread, free of poll jitter.
     */
    while (bytes > 0) {
        int copy_bytes = bytes < period_bytes ? bytes : period_bytes;
        int ret = pcm_try_write(pcm, (const char*)data + offset, copy_bytes);
        if (ret == -EPIPE) {
            /* XRUN: re-prepare then restart from the 1st-write path */
            if (xrun_retry++ >= 5) {
                break;
            }
            pcm->prepared = 0;
            pcm->running = 0;
            if (pcm_prepare(pcm) != 0) {
                proc_usleep(100);
            }
            continue;
        }
        if (ret <= 0) {
            if (ret < 0) break;
            /* ret == 0 without error: no progress, avoid spinning */
            break;
        }
        xrun_retry = 0;
        offset += ret;
        written += ret;
        bytes -= ret;
    }

    /* bytes actually accepted by the device (may be < count) */
    return written;
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

/*
    Fractional resampler step in Q16: input rate / output rate =
    22254.54 / 44100 = 0.5046382 -> 33072. An exact-ratio resampler
    (instead of plain 2x duplication) keeps consumption locked to
    production, so the MySound_SecondNotify0 clock servo almost never
    has to skip/insert whole ticks - each skipped tick is a 370-sample
    phase jump, audible as a periodic click in sustained tones.
*/
#define kResampleStep 33072

static void *audio_thread(void *arg)
{
    struct audio_thread_data *data = (struct audio_thread_data *)arg;
    struct pcm *pcm = data->pcm;
    unsigned char *buffer = NULL;
    ui5b phase = 0; /* Q16 position within the input ring, relative to ThePlayOffset */
    int16_t last_v = 0; /* last output level, held during underrun padding */

    if (!pcm) {
        return NULL;
    }

    /* one full device period per write, stereo 16-bit */
    buffer = (unsigned char *)malloc((size_t)pcm->config.period_size * 4);
    if (!buffer) {
        return NULL;
    }

    /*
        Build the startup reserve before playing: the first writes fill
        the device queue (period_count periods) without blocking, which
        migrates ~4 ring buffers out of the ring. Waiting for target+4
        leaves the ring sitting right at DesiredMinFilledSoundBuffs
        once the device queue is primed.
    */
    while (data->enabled &&
        (ui4b)(TheFillOffset - ThePlayOffset) <
            (ui4b)((DesiredMinFilledSoundBuffs + 4) << kLnOneBuffLen)) {
        proc_usleep(100);
    }

    while (data->enabled) {
        /* wrap-safe filled sample count (offsets are ui4b counters) */
        ui4b filled = (ui4b)(TheFillOffset - ThePlayOffset);
        int period_frames = pcm->config.period_size;
        int in_avail;
        int gen_frames;
        int out_frames;
        int wr;

        if (!HaveStartedPlaying) {
            proc_usleep(6000);
            continue;
        }

        /* up to 2 ring buffers of input per period: a full 1024-frame
           period needs ~517 input samples plus 1 lookahead sample */
        in_avail = filled;
        if (in_avail > (int)(kOneBuffLen * 2)) {
            in_avail = kOneBuffLen * 2;
        }

        out_frames = 0;
        while (out_frames < period_frames) {
            int ipart = (int)(phase >> 16);
            int s0, s1, s;

            if (ipart >= in_avail - 1) {
                break;
            }

            s0 = TheSoundBuffer[(ThePlayOffset + ipart) & kAllBuffMask];
            s1 = TheSoundBuffer[(ThePlayOffset + ipart + 1) & kAllBuffMask];
            s = s0 + (((s1 - s0) * (int)(phase & 0xffff)) >> 16);
            last_v = (int16_t)((s - 128) * 256);

            ((int16_t *)buffer)[out_frames * 2] = last_v;
            ((int16_t *)buffer)[out_frames * 2 + 1] = last_v;
            out_frames++;
            phase += kResampleStep;
        }
        gen_frames = out_frames;

        /*
            Always emit a full period: when the ring runs short, hold
            the last sample level instead of skipping the write. The
            device never starves (an XRUN restarts the stream - a loud
            glitch), and held frames consume no ring samples, so the
            ring refills while padding plays.
        */
        while (out_frames < period_frames) {
            ((int16_t *)buffer)[out_frames * 2] = last_v;
            ((int16_t *)buffer)[out_frames * 2 + 1] = last_v;
            out_frames++;
        }

        /* blocks on the device clock, pacing this thread; returns the
           number of bytes actually accepted */
        wr = pcm_write(pcm, buffer, period_frames * 4);
        {
            int accepted = (wr > 0) ? (wr >> 2) : 0;

            if (accepted < gen_frames) {
                /* stalled/failed write: rewind phase so unaccepted
                   frames are regenerated instead of dropped */
                phase -= (ui5b)(gen_frames - accepted) * kResampleStep;
            }

            /* advance the ring by whole input samples consumed and
               keep only the fractional part in phase */
            ThePlayOffset += (ui4b)(phase >> 16);
            phase &= 0xffff;

            if (accepted == 0) {
                /* device error path: back off one period instead of
                   hammering the driver with re-prepare requests */
                proc_usleep(period_frames * 1000000 / SOUND_SAMPLERATE);
            }
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
            int mac_x = -1, mac_y = -1;

            if (window_width > 0 && window_height > 0) {
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

            if (ev->state == MOUSE_STATE_DOWN) {
                MyMouseButtonSet(falseblnr);
                MyMouseButtonSet(trueblnr);
            } else if (ev->state == MOUSE_STATE_UP) {
                MyMouseButtonSet(falseblnr);
            } else if (ev->state == MOUSE_STATE_MOVE) {
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

/*
    Pre-boot "copying disk" splash, same as macemu's: while
    LoadInitialImages() copies the shipped disk images into the user
    directory the window is already up but the guest is not running
    yet, so show a disk-copy icon with a progress bar instead of the
    (still blank) guest frame.
*/

static blnr copy_splash = falseblnr;
static int copy_splash_done = 0;
static int copy_splash_total = 0;

#define SPLASH_BG      0xff151515
#define SPLASH_FG      0xffb8b8b8
#define SPLASH_SHUTTER 0xff5a5a5a
#define SPLASH_LABEL   0xfff2f2f2
#define SPLASH_TRACK   0xff3c3c3c

static void draw_floppy(graph_t* g, int x, int y, int s) {
    /* Body */
    int sw, sh, sx;
    int lw, lh, lx, ly, lh1;

    graph_fill_round(g, x, y, s, s, s/10, SPLASH_FG);
    /* Metal shutter with its slider slot */
    sw = s*3/7; sh = s*2/7;
    sx = x + (s - sw)/2 + s/12;
    graph_fill_rect(g, sx, y, sw, sh, SPLASH_SHUTTER);
    graph_fill_rect(g, sx + sw/6, y + sh/8, sw/5, sh*3/4, SPLASH_BG);
    /* Label with two ruled lines */
    lw = s*5/7; lh = s*2/5;
    lx = x + (s - lw)/2; ly = y + s*11/20;
    lh1 = (lh/12 > 1) ? lh/12 : 1;
    graph_fill_rect(g, lx, ly, lw, lh, SPLASH_LABEL);
    graph_fill_rect(g, lx + lw/8, ly + lh/3, lw*3/4, lh1, SPLASH_SHUTTER);
    graph_fill_rect(g, lx + lw/8, ly + lh*2/3, lw*3/4, lh1, SPLASH_SHUTTER);
}

static void draw_copy_arrow(graph_t* g, int x, int cy, int s) {
    int shaft_h = (s/8 > 2) ? s/8 : 2;
    int shaft_len = s/4;
    int head_len = s/5;
    int head_h = s/3;
    int ax;
    int i;

    graph_fill_rect(g, x, cy - shaft_h/2, shaft_len, shaft_h, SPLASH_FG);
    /* Triangle head built from 1px columns (no polygon fill in the graph lib) */
    ax = x + shaft_len;
    for (i = 0; i < head_len; i++) {
        int hh = head_h * (head_len - i) / head_len;
        graph_fill_rect(g, ax + i, cy - hh/2, 1, hh, SPLASH_FG);
    }
}

static void draw_copy_splash(graph_t* g) {
    int gw = g->w, gh = g->h;
    int s, gap, arrow_len, total_w, bar_h, block_h;
    int x0, y0, cy, by;

    graph_fill_rect(g, 0, 0, gw, gh, SPLASH_BG);

    /* Icon size follows the window, like the letterboxed guest frame does */
    s = ((gw < gh) ? gw : gh) / 6;
    if (s < 40) s = 40;
    if (s > 96) s = 96;

    gap = s/4;
    arrow_len = s/4 + s/5;  /* shaft + head */
    total_w = s + gap + arrow_len + gap + s;
    bar_h = (s/10 > 4) ? s/10 : 4;
    block_h = s + s/5 + bar_h;

    x0 = (gw - total_w)/2;
    y0 = (gh - block_h)/2;
    cy = y0 + s/2;

    draw_floppy(g, x0, y0, s);
    draw_copy_arrow(g, x0 + s + gap, cy, s);
    draw_floppy(g, x0 + s + gap + arrow_len + gap, y0, s);

    /* Byte progress across all pending disk images */
    by = y0 + s + s/5;
    graph_fill_round(g, x0, by, total_w, bar_h, bar_h/2, SPLASH_TRACK);
    if (copy_splash_total > 0) {
        int fw = (int)((long long)total_w * copy_splash_done / copy_splash_total);
        if (fw > bar_h) {
            graph_fill_round(g, x0, by, fw, bar_h, bar_h/2, SPLASH_FG);
        }
    }
}

static void MyDiskCopySplash(int done, int total) {
    if (NULL == xwin) {
        return;
    }
    if (total <= 0) {
        copy_splash = falseblnr;
        copy_splash_done = 0;
        copy_splash_total = 0;
    } else {
        copy_splash = trueblnr;
        copy_splash_done = done;
        copy_splash_total = total;
    }
    xwin_repaint(xwin);
}

static graph_t* scaled = NULL;
static void on_xwin_repaint(xwin_t* win, graph_t* g) {
    if (g == NULL)
        return;

    /* Pre-boot disk-copy splash replaces the (still blank) guest frame */
    if (copy_splash) {
        draw_copy_splash(g);
        return;
    }

    screen_graph = g;
    window_width = g->w;
    window_height = g->h;

    graph_fill_rect(g, 0, 0, g->w, g->h, 0xff000000);

    if (screen_buffer != NULL && screen_buffer->buffer != NULL) {
        /* screen_buffer is filled by the emulation thread; hold
           frame_lock so a repaint never overlaps a conversion */
        pthread_mutex_lock(&frame_lock);

        float scale_x = (float)g->w / screen_buffer->w;
        float scale_y = (float)g->h / screen_buffer->h;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 0.5f) scale = 0.5f;

        int scaled_w = screen_buffer->w * scale;
        int scaled_h = screen_buffer->h * scale;
        int offset_x = (g->w - scaled_w) / 2;
        int offset_y = (g->h - scaled_h) / 2;

        display_scale = scale;
        display_offset_x = offset_x;
        display_offset_y = offset_y;

        if (scale != 1.0) {
            if (scaled == NULL || scaled->w != scaled_w || scaled->h != scaled_h) {
                graph_t* tmp = graph_new(NULL, scaled_w, scaled_h);
                if(scaled != NULL)
                    graph_free(scaled);
                scaled = tmp;
            }
            if (scaled != NULL) {
                graph_scale_tof_fast(screen_buffer, scaled, scale);
                graph_blt(scaled, 0, 0, scaled_w, scaled_h, g, offset_x, offset_y, scaled_w, scaled_h);
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

        pthread_mutex_unlock(&frame_lock);
    }
}

LOCALPROC CheckForSavedTasks(void);
LOCALPROC RunEmulatedTicksToTrueTime(void);
LOCALPROC DoEmulateOneTick(void);

#define mac_FPS 60
static void xwin_loop(void* p) {
    if (ForceMacOff) {
        x_terminate(x_context);
        return;
    }

    uint64_t tik = kernel_tic_ms(0);
    uint32_t tm = 1000/mac_FPS;

    CheckForSavedTasks();

    /*
        GUI work only: emulation (and thus sound generation) runs in
        its own thread; here we just push the latest published frame
        to the xserver. A slow repaint can no longer delay ticks.
    */
    blnr do_repaint = falseblnr;
    pthread_mutex_lock(&frame_lock);
    if (frame_pending) {
        frame_pending = falseblnr;
        do_repaint = trueblnr;
    }
    pthread_mutex_unlock(&frame_lock);

    if (do_repaint && xwin != NULL) {
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

    graph_fill_rect(screen_buffer, 0, 0, vMacScreenWidth, vMacScreenHeight, 0xffffffff);

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
    xwin_fullscreen(xwin);
    xwin_set_visible(xwin, true);

    /* push the initial (white) frame at once instead of leaving
       the window black until the first published guest frame */
    xwin_repaint(xwin);

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

    /* screen conversion is done by the emulation thread
       (PublishFrameChanges), not here */

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

/* --- emulation thread --- */

/*
    Like the emu app's emuLoop: DoEmulateOneTick is paced by real
    time on a dedicated thread, fully decoupled from GUI repaint.
    The emulated Mac produces sound samples every tick, so keeping
    ticks on schedule is what keeps the sound ring fed; repaint cost
    (screen scaling + xserver IPC) can no longer starve it.
*/

LOCALVAR pthread_t emu_thread_id = 0;
LOCALVAR volatile blnr emu_thread_running = falseblnr;

LOCALPROC PublishFrameChanges(void)
{
    if (ScreenChangedBottom > ScreenChangedTop) {
        /* never block the tick on GUI work: if the GUI thread is busy
           scaling/blitting under frame_lock, keep the dirty region and
           publish on a later tick; sound production must not stall */
        if (pthread_mutex_trylock(&frame_lock) == 0) {
            MyDrawChangesAndClear();
            frame_pending = trueblnr;
            pthread_mutex_unlock(&frame_lock);
        }
    }
}

/*
    Fast boot: the Mac Plus ROM runs a full power-on memory test
    (4 MB here) before it draws anything; at faithful 1x speed that
    leaves the screen black for several seconds (Basilisk II patches
    the test out, which is why macemu shows video right away).  Run
    the first ticks "all out": the CPU gets host-speed cycle bursts
    within each tick while the ticks themselves stay paced at real
    60Hz, so the pacing clock and the tick-driven sound engine never
    desync; the test finishes in a fraction of the usual time.
*/
#define kFastBootTicks 120  /* ~2s worst-case cap on slow hosts */

LOCALFUNC void *emu_thread_entry(void *arg)
{
    ui3b saved_speed;
    int fast_ticks;

    (void)arg;

    saved_speed = SpeedValue;
    fast_ticks = kFastBootTicks;
    SpeedValue = (ui3b) -1;

    while (emu_thread_running) {
        if (ForceMacOff || CurSpeedStopped) {
            /* still publish control-mode/message screen changes */
            PublishFrameChanges();
            proc_usleep(16000);
            continue;
        }

        UpdateTrueEmulatedTime();
        RunOnEndOfSixtieth();

        if (fast_ticks > 0) {
            /* all-out extra cycles for the ROM self-test; restore
               the configured speed when the budget is exhausted */
            --fast_ticks;
            DoEmulateExtraTime();
            if (0 == fast_ticks) {
                SpeedValue = saved_speed;
            }
        }

        PublishFrameChanges();

        /* sleep until the next 60.15Hz tick boundary */
        {
            uint32_t low;
            si5b wait_ms;

            kernel_tic32(NULL, NULL, &low);
            wait_ms = (si5b)(NextIntTime - (low / 1000));
            if (wait_ms > 0) {
                if (wait_ms > 20) {
                    wait_ms = 20;
                }
                proc_usleep(wait_ms * 1000);
            } else {
                proc_yield();
            }
        }
    }

    return NULL;
}

LOCALPROC StartEmuThread(void)
{
    emu_thread_running = trueblnr;
    if (pthread_create(&emu_thread_id, NULL, emu_thread_entry, NULL) != 0) {
        emu_thread_running = falseblnr;
        emu_thread_id = 0;
    }
}

LOCALPROC StopEmuThread(void)
{
    if (emu_thread_running) {
        emu_thread_running = falseblnr;
        pthread_join(emu_thread_id, NULL);
        emu_thread_id = 0;
    }
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

    pthread_mutex_init(&frame_lock, NULL);

    ZapOSGLUVars();
    if (InitOSGLU()) {
        LeaveSpeedStopped();
        StartEmuThread();
        x_run(x_context, xwin);
        StopEmuThread();
    }
    if(scaled != NULL)
        graph_free(scaled);

    UnInitOSGLU();

    return 0;
}
