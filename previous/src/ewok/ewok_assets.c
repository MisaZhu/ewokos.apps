/*
 *  ewok_assets.c - EwokOS asset handling for Previous
 *
 *  Mirrors macemu's prefs_unix.cpp user-disk flow: bundled images in
 *  /apps/previous/res/disks are prepared into the user's home
 *  (<home>/.previous/disks) once the window is visible, and
 *  mounted from the user copies so guest writes persist across
 *  reboots.  A shipped raw image is copied as-is, a shipped
 *  <name>.zip is unpacked into the user directory; while that runs a
 *  "preparing disk" splash with byte progress is shown.
 *
 *  SCSI targets are filled bootable-first (macemu boot-candidate
 *  equivalent): a hard disk whose label sector starts with the
 *  "NeXT"/"dlV2"/"dlV3" magic the boot ROM checks for is bootable and
 *  gets target 0, CDs come next, non-bootable hard disks last - so
 *  with no bootable hard disk the first CD lands on target 0 and the
 *  ROM boots from it (an install CD next to an empty scratch disk).
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib.h>
#include <SDL.h>

#include <ewoksys/session.h>

#include "configuration.h"
#include "ewok_compat.h"
#include "file.h"
#include "screen.h"

#define RES_DISKS_DIR "/apps/previous/res/disks"

/*----------------------------------------------------------------------*/
/*  small fd helpers                                                     */
/*----------------------------------------------------------------------*/

static bool read_at_fd(int fd, off_t off, void *buf, size_t len)
{
	if (lseek(fd, off, SEEK_SET) != off)
		return false;
	while (len > 0) {
		ssize_t r = read(fd, buf, len);
		if (r <= 0)
			return false;
		buf = (char *)buf + r;
		len -= (size_t)r;
	}
	return true;
}

static size_t write_all_fd(int fd, const void *buf, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t w = write(fd, (const char *)buf + done, len - done);
		if (w <= 0)
			break;
		done += (size_t)w;
	}
	return done;
}

static bool has_suffix_ci(const char *s, const char *suffix)
{
	size_t ls = strlen(s);
	size_t lf = strlen(suffix);
	if (ls < lf)
		return false;
	return strncasecmp(s + ls - lf, suffix, lf) == 0;
}

static bool get_user_disks_dir(char *path, size_t size)
{
	session_info_t sinfo;

	if (path == NULL || size == 0)
		return false;

	if (session_get_by_uid(getuid(), &sinfo) != 0 || sinfo.home[0] == 0)
		return false;

	snprintf(path, size, "%s/.previous/disks", sinfo.home);
	return true;
}

static bool ensure_dir_exists(const char *path)
{
	char tmp[512];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (access(tmp, F_OK) != 0 && mkdir(tmp, 0755) != 0) {
			*p = '/';
			return false;
		}
		*p = '/';
	}
	if (access(tmp, F_OK) != 0 && mkdir(tmp, 0755) != 0)
		return false;
	return true;
}

static bool is_regular_file(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	return S_ISREG(st.st_mode);
}

/*----------------------------------------------------------------------*/
/*  progress callback -> splash (throttled, like macemu)                 */
/*----------------------------------------------------------------------*/

/* Aggregate progress across all pending disks.  Must be 64-bit even
 * on 32-bit EwokOS (off_t is long there): two shipped images already
 * exceed 2GiB combined, and the splash takes ints, so it is fed KiB. */
static uint64_t copy_base_bytes = 0;
static uint64_t copy_total_bytes = 0;
static uint32_t copy_last_splash_ms = 0;

typedef void (*copy_progress_fn)(off_t done, off_t file_size, void *arg);

static void copy_progress_cb(off_t done, off_t file_size, void *arg)
{
	(void)arg;
	/* a repaint is a synchronous IPC to the x server: throttle it */
	uint32_t now = SDL_GetTicks();
	if (done != file_size && now - copy_last_splash_ms < 80)
		return;
	copy_last_splash_ms = now;
	uint64_t all = copy_base_bytes + (uint64_t)done;
	if (all > copy_total_bytes)
		all = copy_total_bytes;
	Screen_DiskCopySplash((int)(all >> 10),
		(int)((copy_total_bytes + 1023) >> 10));
	/* yield so the repaint thread gets a slice to present the splash */
	SDL_Delay(2);
}

/*----------------------------------------------------------------------*/
/*  plain copy (macemu copy_file_if_needed)                              */
/*----------------------------------------------------------------------*/

static bool copy_file_if_needed(const char *src_path, const char *dst_path,
	copy_progress_fn progress, void *arg)
{
	/*
	 * 64KB matches the VFS shared-memory transfer size (SHM_MAX), so
	 * each read/write is exactly one IPC round-trip instead of two.
	 * Heap-allocated: 64KB is too large for the default thread stack.
	 */
	const size_t buf_size = 1024 * 64;
	char *buffer = (char *)malloc(buf_size);
	ssize_t nread = 0;
	struct stat st;
	off_t copied = 0;
	off_t file_size = 0;

	if (buffer == NULL)
		return false;

	int src_fd = open(src_path, O_RDONLY);
	if (src_fd < 0) {
		free(buffer);
		return false;
	}
	if (fstat(src_fd, &st) == 0)
		file_size = st.st_size;

	/* A complete copy already exists: nothing to do.  A size
	 * mismatch is a partial leftover (interrupted run): replace it */
	struct stat dst_st;
	if (stat(dst_path, &dst_st) == 0) {
		if (dst_st.st_size == file_size) {
			free(buffer);
			close(src_fd);
			return true;
		}
		printf("WARNING: %s is incomplete (%lld of %lld bytes), "
			"copying again\n", dst_path, (long long)dst_st.st_size,
			(long long)file_size);
		unlink(dst_path);
	}

	int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dst_fd < 0) {
		free(buffer);
		close(src_fd);
		return false;
	}

	while ((nread = read(src_fd, buffer, buf_size)) > 0) {
		if (write_all_fd(dst_fd, buffer, (size_t)nread) != (size_t)nread) {
			free(buffer);
			close(dst_fd);
			close(src_fd);
			unlink(dst_path);
			return false;
		}
		copied += nread;
		if (progress != NULL)
			progress(copied, file_size, arg);
	}
	free(buffer);

	if (nread < 0) {
		close(dst_fd);
		close(src_fd);
		unlink(dst_path);
		return false;
	}

	if (close(dst_fd) != 0) {
		close(src_fd);
		unlink(dst_path);
		return false;
	}
	if (close(src_fd) != 0) {
		unlink(dst_path);
		return false;
	}
	return true;
}

/*----------------------------------------------------------------------*/
/*  Minimal ZIP reader for shipped <name>.zip images (macemu): entries   */
/*  are located through the central directory and unpacked with zlib     */
/*  raw deflate.  Enough for the archives shipped in res/disks (a        */
/*  single entry, possibly next to junk like macOS "__MACOSX/" data)     */
/*----------------------------------------------------------------------*/

struct zip_entry_info {
	uint16_t method;
	uint32_t crc;
	uint32_t csize;
	uint32_t usize;
	off_t data_off;		/* absolute offset of the compressed data */
};

static uint16_t rd_le16(const unsigned char *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The end-of-central-directory record (sig 0x06054b50) sits in the
 * last 22..22+65535 bytes of the file (the variable-length comment) */
static bool zip_find_eocd(int fd, off_t file_size, uint32_t *cd_size,
	off_t *cd_off)
{
	off_t tail = file_size;
	if (tail > 22 + 65535)
		tail = 22 + 65535;
	unsigned char *buf = (unsigned char *)malloc((size_t)tail);
	if (buf == NULL)
		return false;
	bool ok = false;
	if (read_at_fd(fd, file_size - tail, buf, (size_t)tail)) {
		for (off_t i = tail - 22; i >= 0; i--) {
			if (rd_le32(buf + i) == 0x06054b50) {
				*cd_size = rd_le32(buf + i + 12);
				*cd_off = (off_t)rd_le32(buf + i + 16);
				ok = true;
				break;
			}
		}
	}
	free(buf);
	return ok;
}

/*
 *  Find an entry in the central directory: an exact name match wins,
 *  otherwise the first regular entry that is not a directory or macOS
 *  metadata junk.  Fills in the absolute data offset (resolved
 *  through the local header, whose name/extra lengths may differ
 *  from the central directory's)
 */
static bool zip_find_entry(const char *zip_path, const char *want_name,
	struct zip_entry_info *ent)
{
	struct zip_entry_info match, fallback;
	uint32_t match_lh = 0, fallback_lh = 0;
	bool have_match = false, have_fallback = false;
	bool ok = false;
	unsigned char *cd = NULL;

	int fd = open(zip_path, O_RDONLY);
	if (fd < 0)
		return false;

	off_t file_size = lseek(fd, 0, SEEK_END);
	uint32_t cd_size;
	off_t cd_off;
	if (file_size < 22 ||
	    !zip_find_eocd(fd, file_size, &cd_size, &cd_off)) {
		close(fd);
		return false;
	}

	cd = (unsigned char *)malloc(cd_size);
	if (cd == NULL || !read_at_fd(fd, cd_off, cd, cd_size)) {
		free(cd);
		close(fd);
		return false;
	}

	char name[256];
	const unsigned char *p = cd, *end = cd + cd_size;
	while (p + 46 <= end && rd_le32(p) == 0x02014b50) {
		uint16_t name_len = rd_le16(p + 28);
		uint16_t extra_len = rd_le16(p + 30);
		uint16_t comment_len = rd_le16(p + 32);
		if (name_len > 0 && name_len < (int)sizeof(name)) {
			memcpy(name, p + 46, name_len);
			name[name_len] = '\0';
			if (name[name_len - 1] != '/' &&
			    strncmp(name, "__MACOSX/", 9) != 0) {
				struct zip_entry_info zi;
				zi.method = rd_le16(p + 10);
				zi.crc = rd_le32(p + 16);
				zi.csize = rd_le32(p + 20);
				zi.usize = rd_le32(p + 24);
				uint32_t lh = rd_le32(p + 42);
				if (want_name != NULL &&
				    strcmp(name, want_name) == 0) {
					match = zi;
					match_lh = lh;
					have_match = true;
				} else if (!have_fallback) {
					fallback = zi;
					fallback_lh = lh;
					have_fallback = true;
				}
			}
		}
		p += 46 + name_len + extra_len + comment_len;
	}
	free(cd);

	const struct zip_entry_info *pick = NULL;
	uint32_t lh_off = 0;
	if (have_match) {
		pick = &match;
		lh_off = match_lh;
	} else if (have_fallback) {
		pick = &fallback;
		lh_off = fallback_lh;
	}
	if (pick != NULL) {
		unsigned char lh[30];
		if (read_at_fd(fd, (off_t)lh_off, lh, sizeof(lh)) &&
		    rd_le32(lh) == 0x04034b50) {
			*ent = *pick;
			ent->data_off = (off_t)lh_off + 30 +
				rd_le16(lh + 26) + rd_le16(lh + 28);
			ok = true;
		}
	}
	close(fd);
	return ok;
}

/*
 *  Unpack one entry of a zip archive into dst_path (created or
 *  truncated), verifying size and CRC32.  The progress callback sees
 *  unpacked (not compressed) bytes.  On any failure dst_path is
 *  removed again
 */
static bool extract_zip_file(const char *zip_path, const char *want_name,
	const char *dst_path, copy_progress_fn progress, void *arg)
{
	struct zip_entry_info ent;
	z_stream strm;
	bool strm_inited = false;
	bool ok = false;

	if (!zip_find_entry(zip_path, want_name, &ent)) {
		printf("WARNING: no usable entry in %s\n", zip_path);
		return false;
	}
	if (ent.method != 0 && ent.method != 8) {
		printf("WARNING: unsupported zip method %d in %s\n",
			ent.method, zip_path);
		return false;
	}

	/*
	 * 64KB chunks like copy_file_if_needed (one VFS SHM_MAX
	 * round-trip each); heap-allocated for EwokOS thread stacks.
	 * Separate input and output buffers: inflate cannot work
	 * in-place
	 */
	const size_t buf_size = 1024 * 64;
	char *in_buf = (char *)malloc(buf_size);
	char *out_buf = (char *)malloc(buf_size);
	if (in_buf == NULL || out_buf == NULL) {
		free(in_buf);
		free(out_buf);
		return false;
	}

	int src_fd = open(zip_path, O_RDONLY);
	if (src_fd < 0) {
		free(in_buf);
		free(out_buf);
		return false;
	}
	int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dst_fd < 0) {
		free(in_buf);
		free(out_buf);
		close(src_fd);
		return false;
	}

	uint32_t crc = crc32(0L, Z_NULL, 0);
	off_t written = 0;
	ok = lseek(src_fd, ent.data_off, SEEK_SET) == ent.data_off;

	if (ent.method == 0) {
		/* stored entry: plain copy */
		off_t left = ent.csize;
		while (ok && left > 0) {
			size_t want = left < (off_t)buf_size ?
				(size_t)left : buf_size;
			ssize_t r = read(src_fd, in_buf, want);
			if (r <= 0) {
				ok = false;
				break;
			}
			if (write_all_fd(dst_fd, in_buf, (size_t)r) !=
			    (size_t)r) {
				ok = false;
				break;
			}
			crc = crc32(crc, (const Bytef *)in_buf, (uInt)r);
			written += r;
			left -= r;
			if (progress != NULL)
				progress(written, (off_t)ent.usize, arg);
		}
	} else {
		memset(&strm, 0, sizeof(strm));
		/* negative window bits: raw deflate inside zip */
		if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
			free(in_buf);
			free(out_buf);
			close(dst_fd);
			close(src_fd);
			unlink(dst_path);
			return false;
		}
		strm_inited = true;
		off_t left = ent.csize;
		while (ok && (strm.avail_in > 0 || left > 0)) {
			if (strm.avail_in == 0) {
				size_t want = left < (off_t)buf_size ?
					(size_t)left : buf_size;
				ssize_t r = read(src_fd, in_buf, want);
				if (r <= 0) {
					ok = false;
					break;
				}
				left -= r;
				strm.next_in = (Bytef *)in_buf;
				strm.avail_in = (uInt)r;
			}
			strm.next_out = (Bytef *)out_buf;
			strm.avail_out = (uInt)buf_size;
			int ret = inflate(&strm, Z_NO_FLUSH);
			if (ret != Z_OK && ret != Z_STREAM_END &&
			    ret != Z_BUF_ERROR) {
				printf("WARNING: inflate error %d in %s\n",
					ret, zip_path);
				ok = false;
				break;
			}
			size_t produced = buf_size - strm.avail_out;
			if (produced > 0) {
				if (write_all_fd(dst_fd, out_buf,
				    produced) != produced) {
					ok = false;
					break;
				}
				crc = crc32(crc, (const Bytef *)out_buf,
					(uInt)produced);
				written += (off_t)produced;
				if (progress != NULL)
					progress(written, (off_t)ent.usize,
						arg);
			}
			if (ret == Z_STREAM_END)
				break;
		}
	}
	if (strm_inited)
		inflateEnd(&strm);

	if (ok && (written != (off_t)ent.usize || crc != ent.crc)) {
		printf("WARNING: %s unpacks to %lld bytes (crc %08x), "
			"expected %lld (crc %08x)\n", zip_path,
			(long long)written, crc, (long long)ent.usize,
			ent.crc);
		ok = false;
	}
	if (close(dst_fd) != 0)
		ok = false;
	close(src_fd);
	free(in_buf);
	free(out_buf);
	if (!ok)
		unlink(dst_path);
	return ok;
}

/*----------------------------------------------------------------------*/
/*  scan: collect the disk images, record the missing user copies as     */
/*  pending (targets are assigned later, once the user copies exist)     */
/*----------------------------------------------------------------------*/

/* optical-disk-ish images mount read-only as CD devices */
static SCSI_DEVTYPE devtype_for(const char *name)
{
	if (has_suffix_ci(name, ".iso") || has_suffix_ci(name, ".od"))
		return DEVTYPE_CD;
	return DEVTYPE_HARDDISK;
}

#define MAX_PENDING_DISKS 16

/* shipped images whose user copy does not exist yet: recorded by
 * Ewok_AutoMountDisks() and created by Ewok_PrepareUserDisks() once
 * the window is visible */
static char pending_disk_names[MAX_PENDING_DISKS][256];
static int pending_disk_count = 0;
static char pending_src_dir[256];
static char pending_dst_dir[256];

struct mount_entry {
	char base[256];		/* user copy basename (unpacked name) */
	SCSI_DEVTYPE dt;
};

/* collected by Ewok_AutoMountDisks(), mounted by
 * Ewok_AssignDiskTargets() after the pending user copies exist */
static struct mount_entry mount_entries[MAX_PENDING_DISKS * 2 + 8];
static int mount_entry_count = 0;

static void assign_target(const struct mount_entry *e)
{
	int t = 0;

	/* a free slot is DEVTYPE_NONE: szImageName is useless as an
	 * emptiness marker, the defaults fill it with the working dir
	 * and the config reader ignores empty values, so it is never
	 * '\0' and every image would be dropped silently */
	while (t < ESP_MAX_DEVS &&
	       ConfigureParams.SCSI.target[t].nDeviceType != DEVTYPE_NONE)
		t++;
	if (t >= ESP_MAX_DEVS) {
		fprintf(stderr, "EWOK-ASSETS: no free SCSI target for %s\n",
			e->base);
		return;
	}

	snprintf(ConfigureParams.SCSI.target[t].szImageName, FILENAME_MAX,
		 "%s/%s", pending_dst_dir, e->base);
	ConfigureParams.SCSI.target[t].nDeviceType = e->dt;
	ConfigureParams.SCSI.target[t].bDiskInserted = true;
	ConfigureParams.SCSI.target[t].bWriteProtected = false;
	printf("EWOK-ASSETS: %s -> scsi target %d (%s)\n", e->base, t,
		e->dt == DEVTYPE_CD ? "cd" : "hd");
}

void Ewok_AutoMountDisks(void)
{
	char user_disks_dir[512];
	DIR *d;
	struct dirent *de;

	pending_disk_count = 0;
	mount_entry_count = 0;
	if (!get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir)))
		return;
	snprintf(pending_src_dir, sizeof(pending_src_dir), "%s", RES_DISKS_DIR);
	snprintf(pending_dst_dir, sizeof(pending_dst_dir), "%s", user_disks_dir);
	ensure_dir_exists(user_disks_dir);

	/* bundled images: the user copies (existing or pending) are what
	 * gets mounted; the missing ones are prepared after the window
	 * shows */
	d = opendir(RES_DISKS_DIR);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			char src_path[512], user_path[512];
			char base[256];

			if (de->d_name[0] == '.')
				continue;
			snprintf(src_path, sizeof(src_path), "%s/%s",
				 RES_DISKS_DIR, de->d_name);
			if (!is_regular_file(src_path))
				continue;
			snprintf(base, sizeof(base), "%s", de->d_name);
			if (has_suffix_ci(base, ".zip"))
				/* shipped packed: user copy is unpacked */
				base[strlen(base) - 4] = '\0';
			snprintf(user_path, sizeof(user_path), "%s/%s",
				 user_disks_dir, base);
			if (!is_regular_file(user_path)) {
				/* no user copy: without one the image can
				 * never be mounted, skip it entirely when
				 * the pending list is full */
				if (pending_disk_count >= MAX_PENDING_DISKS)
					continue;
				snprintf(pending_disk_names[pending_disk_count],
					sizeof(pending_disk_names[0]), "%s", base);
				pending_disk_count++;
			}
			if (mount_entry_count <
			    (int)(sizeof(mount_entries) /
				  sizeof(mount_entries[0]))) {
				struct mount_entry *e =
					&mount_entries[mount_entry_count++];
				snprintf(e->base, sizeof(e->base), "%s", base);
				e->dt = devtype_for(base);
			}
		}
		closedir(d);
	}

	/* user-added images not shipped in res/disks */
	d = opendir(user_disks_dir);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			char path[512];
			bool known = false;

			if (de->d_name[0] == '.')
				continue;
			for (int i = 0; i < mount_entry_count; i++)
				if (strcmp(mount_entries[i].base,
					   de->d_name) == 0) {
					known = true;
					break;
				}
			if (known)
				continue;
			snprintf(path, sizeof(path), "%s/%s",
				 user_disks_dir, de->d_name);
			if (!is_regular_file(path))
				continue;
			if (mount_entry_count <
			    (int)(sizeof(mount_entries) /
				  sizeof(mount_entries[0]))) {
				struct mount_entry *e =
					&mount_entries[mount_entry_count++];
				snprintf(e->base, sizeof(e->base), "%s",
					de->d_name);
				e->dt = devtype_for(de->d_name);
			}
		}
		closedir(d);
	}
}

/*----------------------------------------------------------------------*/
/*  target assignment: runs in Main_Init() after the pending user        */
/*  copies are prepared, so the bootability sniff reads the real         */
/*  images (a still-packed res/disks image could not be sniffed)         */
/*----------------------------------------------------------------------*/

/* Bootable-NeXT-disk sniff (macemu is_bootable_hfs_image equivalent):
 * the boot ROM accepts a disk whose label sector starts with "NeXT",
 * "dlV2" or "dlV3" (it cmp.l's the first longword against these) */
static bool is_bootable_next_disk(const char *path)
{
	unsigned char magic[4];
	int fd = open(path, O_RDONLY);
	bool ok;

	if (fd < 0)
		return false;
	ok = read_at_fd(fd, 0, magic, sizeof(magic)) &&
		(memcmp(magic, "dlV3", 4) == 0 ||
		 memcmp(magic, "dlV2", 4) == 0 ||
		 memcmp(magic, "NeXT", 4) == 0);
	close(fd);
	return ok;
}

void Ewok_AssignDiskTargets(void)
{
	/* Three passes: bootable hard disks first so one of them lands on
	 * target 0 (the default "sd" boot reads the label from target 0),
	 * then optical media, then non-bootable hard disks.  With no
	 * bootable hard disk the first CD gets target 0 and the ROM boots
	 * it instead (NeXT CDs carry a disk label and boot via the sd
	 * driver), e.g. an install CD next to an empty scratch disk */
	for (int pass = 0; pass < 3; pass++) {
		for (int i = 0; i < mount_entry_count; i++) {
			struct mount_entry *e = &mount_entries[i];
			char path[512];
			bool bootable;
			int group;

			snprintf(path, sizeof(path), "%s/%s",
				pending_dst_dir, e->base);
			if (!is_regular_file(path))
				continue;	/* extraction failed: leave it out */
			bootable = e->dt == DEVTYPE_HARDDISK &&
				is_bootable_next_disk(path);
			group = bootable ? 0 : (e->dt == DEVTYPE_CD ? 1 : 2);
			if (group != pass)
				continue;
			if (bootable)
				printf("EWOK-ASSETS: %s has a NeXT disk "
					"label, it is bootable\n", e->base);
			assign_target(e);
		}
	}

	/* the boot device is whatever sits on target 0 */
	if (ConfigureParams.SCSI.target[0].nDeviceType == DEVTYPE_CD &&
	    ConfigureParams.SCSI.target[0].bDiskInserted)
		printf("EWOK-ASSETS: no bootable hard disk, booting from "
			"CD-ROM %s\n",
			ConfigureParams.SCSI.target[0].szImageName);
}

/*----------------------------------------------------------------------*/
/*  deferred preparation of the pending user copies (macemu              */
/*  AssetsPrepareUserDisks): runs once the window is visible, before     */
/*  the SCSI layer opens the drives                                      */
/*----------------------------------------------------------------------*/

/* Expected size of a user copy: a shipped raw image is copied as-is,
 * a shipped <name>.zip grows to its uncompressed size */
static off_t pending_user_copy_size(const char *disk_name)
{
	char src_path[512];
	struct stat st;

	snprintf(src_path, sizeof(src_path), "%s/%s",
		pending_src_dir, disk_name);
	if (is_regular_file(src_path) && stat(src_path, &st) == 0)
		return st.st_size;
	char zip_path[512];
	struct zip_entry_info ent;
	snprintf(zip_path, sizeof(zip_path), "%s.zip", src_path);
	if (zip_find_entry(zip_path, disk_name, &ent))
		return (off_t)ent.usize;
	return 0;
}

void Ewok_PrepareUserDisks(void)
{
	if (pending_disk_count == 0)
		return;

	if (!ensure_dir_exists(pending_dst_dir)) {
		fprintf(stderr, "EWOK-ASSETS: cannot create %s, %d disk "
			"image(s) left out\n", pending_dst_dir,
			pending_disk_count);
		pending_disk_count = 0;
		return;
	}

	static off_t pending_sizes[MAX_PENDING_DISKS];
	copy_total_bytes = 0;
	for (int i = 0; i < pending_disk_count; i++) {
		pending_sizes[i] = pending_user_copy_size(pending_disk_names[i]);
		copy_total_bytes += (uint64_t)pending_sizes[i];
	}

	copy_base_bytes = 0;
	copy_last_splash_ms = 0;
	Screen_DiskCopySplash(0, (int)((copy_total_bytes + 1023) >> 10));
	printf("EWOK-ASSETS: preparing %d disk image(s) in %s\n",
		pending_disk_count, pending_dst_dir);

	for (int i = 0; i < pending_disk_count; i++) {
		char src_path[512];
		char dst_path[512];
		snprintf(src_path, sizeof(src_path), "%s/%s",
			pending_src_dir, pending_disk_names[i]);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			pending_dst_dir, pending_disk_names[i]);
		bool ok;
		if (is_regular_file(src_path)) {
			ok = copy_file_if_needed(src_path, dst_path,
				copy_progress_cb, NULL);
		} else {
			/* shipped images are packed as <name>.zip */
			char zip_path[512];
			snprintf(zip_path, sizeof(zip_path), "%s.zip", src_path);
			ok = extract_zip_file(zip_path, pending_disk_names[i],
				dst_path, copy_progress_cb, NULL);
		}
		if (!ok)
			fprintf(stderr, "EWOK-ASSETS: preparing %s failed, "
				"leaving it out\n", pending_disk_names[i]);
		copy_base_bytes += (uint64_t)pending_sizes[i];
	}

	/* hide the splash again */
	Screen_DiskCopySplash(0, -1);
	pending_disk_count = 0;
}

/*----------------------------------------------------------------------*/
/*  machine selection                                                    */
/*----------------------------------------------------------------------*/

/* The 2.5-era ROM (Rev_2.5_v66.BIN, loaded for non-Turbo 040 machines)
 * accepts only INQUIRY device type 0 in its sd boot probe, so it can
 * never boot a CD-ROM and dies with the "SCSI error" alert; the 3.3-era
 * Turbo ROM (Rev_3.3_v74.BIN) accepts types 0/4/5/7/8 and boots the
 * NeXTSTEP install CD.  EwokOS has no system preferences dialog, so the
 * emulated machine is fixed to a Turbo model here.  That selects the
 * v74 ROM and Configuration_SetSystemDefaults() supplies the matching
 * Turbo settings (33MHz, MCCS1850 RTC, NCR53C90A SCSI, 4x32MB SIMMs -
 * NEXTRam is a static 128MB array either way). */
void Ewok_ConfigureMachine(void)
{
	if (ConfigureParams.System.nMachineType == NEXT_CUBE030)
		ConfigureParams.System.nMachineType = NEXT_STATION;
	ConfigureParams.System.bTurbo = true;
	Configuration_SetSystemDefaults();
	printf("EWOK: emulating a %s Turbo (Rev_3.3_v74 ROM, can boot "
		"CD-ROM)\n",
		ConfigureParams.System.nMachineType == NEXT_STATION ?
			"NeXTstation" : "NeXTcube");
}

/*----------------------------------------------------------------------*/
/*  ROM staging in the user dir                                          */
/*----------------------------------------------------------------------*/

/*
 * The ROM/EEPROM files ship inside the app bundle (CONFDIR), but the
 * /apps tree must never be written to, and Previous may want to
 * patch/overwrite the ROM at runtime (also File_Exists() demands the
 * write bit).  Stage each bundled ROM into <home>/.previous/roms and
 * point the config at the user copy; ROMs are always loaded from and
 * written to the user copy.
 *
 * Runs early in main() before the SDL window exists, so no splash
 * callbacks are used here; the images are small (<=128KB).
 */
static bool get_user_roms_dir(char *path, size_t size)
{
	session_info_t sinfo;

	if (path == NULL || size == 0)
		return false;

	if (session_get_by_uid(getuid(), &sinfo) != 0 || sinfo.home[0] == 0)
		return false;

	snprintf(path, size, "%s/.previous/roms", sinfo.home);
	return true;
}

static bool stage_rom(const char *roms_dir, const char *name, char *cfg_path)
{
	char bundled[FILENAME_MAX];
	char user_rom[FILENAME_MAX];

	/* an existing user-chosen ROM outside the app bundle is kept;
	 * anything pointing into /apps is migrated to the user copy */
	if (cfg_path[0] != '\0' && File_Exists(cfg_path) &&
	    strncmp(cfg_path, "/apps/", 6) != 0)
		return true;

	snprintf(bundled, sizeof(bundled), CONFDIR "%s", name);
	if (!File_Exists(bundled)) {
		/* no bundled ROM: fall back to the configured path */
		return cfg_path[0] != '\0' && File_Exists(cfg_path);
	}

	snprintf(user_rom, sizeof(user_rom), "%s/%s", roms_dir, name);
	/* always go through the size-checked copy: a truncated user ROM
	 * (interrupted first run) would hang the guest at reset with a
	 * pure-white screen, and mere existence does not catch that */
	if (!copy_file_if_needed(bundled, user_rom, NULL, NULL))
		return false;

	snprintf(cfg_path, FILENAME_MAX, "%s", user_rom);
	return true;
}

void Ewok_FixAssetPaths(void)
{
	char roms_dir[FILENAME_MAX];

	if (!get_user_roms_dir(roms_dir, sizeof(roms_dir)) ||
	    !ensure_dir_exists(roms_dir))
		return;

	stage_rom(roms_dir, "Rev_1.0_v41.BIN",
		ConfigureParams.Rom.szRom030FileName);
	stage_rom(roms_dir, "Rev_2.5_v66.BIN",
		ConfigureParams.Rom.szRom040FileName);
	stage_rom(roms_dir, "Rev_3.3_v74.BIN",
		ConfigureParams.Rom.szRomTurboFileName);
	stage_rom(roms_dir, "dimension_eeprom.bin",
		ConfigureParams.Dimension.szRomFileName);
}
