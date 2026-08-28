/*
 *  ewok_assets.c - EwokOS asset handling for Previous
 *
 *  Mirrors macemu's prefs_unix.cpp user-disk flow: bundled images in
 *  /apps/previous/res/disks are prepared into the user's home
 *  (<home>/docs/previous/disks) once the window is visible, and
 *  mounted from the user copies so guest writes persist across
 *  reboots.  A shipped raw image is copied as-is, a shipped
 *  <name>.zip is unpacked into the user directory; while that runs a
 *  "preparing disk" splash with byte progress is shown.
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

	snprintf(path, size, "%s/docs/previous/disks", sinfo.home);
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

static off_t copy_base_bytes = 0;
static off_t copy_total_bytes = 0;
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
	off_t all = copy_base_bytes + done;
	if (all > copy_total_bytes)
		all = copy_total_bytes;
	Screen_DiskCopySplash((int)all, (int)copy_total_bytes);
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
/*  auto mount: record pending user copies, mount what already exists    */
/*----------------------------------------------------------------------*/

/* optical-disk-ish images mount read-only as CD devices */
static SCSI_DEVTYPE devtype_for(const char *name)
{
	if (has_suffix_ci(name, ".iso") || has_suffix_ci(name, ".od"))
		return DEVTYPE_CD;
	return DEVTYPE_HARDDISK;
}

#define MAX_PENDING_DISKS 16

struct pending_disk {
	char name[256];		/* user copy basename (unpacked name) */
	bool packed;		/* shipped as res/disks/<name>.zip */
	int target;		/* reserved SCSI target */
};

static struct pending_disk pending_disks[MAX_PENDING_DISKS];
static int pending_disk_count = 0;
static char pending_src_dir[256];
static char pending_dst_dir[256];

struct mount_entry {
	char base[256];
	SCSI_DEVTYPE dt;
	int pending_idx;	/* -1: user copy exists, mount now */
};

static void assign_target(struct mount_entry *e)
{
	int t = 0;

	while (t < ESP_MAX_DEVS &&
	       ConfigureParams.SCSI.target[t].szImageName[0] != '\0')
		t++;
	if (t >= ESP_MAX_DEVS)
		return;

	snprintf(ConfigureParams.SCSI.target[t].szImageName, FILENAME_MAX,
		 "%s/%s", pending_dst_dir, e->base);
	ConfigureParams.SCSI.target[t].nDeviceType = e->dt;
	ConfigureParams.SCSI.target[t].bDiskInserted = true;
	ConfigureParams.SCSI.target[t].bWriteProtected = false;
	if (e->pending_idx >= 0)
		pending_disks[e->pending_idx].target = t;
}

void Ewok_AutoMountDisks(void)
{
	char user_disks_dir[512];
	static struct mount_entry entries[MAX_PENDING_DISKS * 2 + 8];
	int entry_count = 0;
	DIR *d;
	struct dirent *de;

	pending_disk_count = 0;
	if (!get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir)))
		return;
	snprintf(pending_src_dir, sizeof(pending_src_dir), "%s", RES_DISKS_DIR);
	snprintf(pending_dst_dir, sizeof(pending_dst_dir), "%s", user_disks_dir);
	ensure_dir_exists(user_disks_dir);

	/* bundled images: mount existing user copies now, record the
	 * missing ones as pending (prepared after the window shows) */
	d = opendir(RES_DISKS_DIR);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			char src_path[512], user_path[512];
			char base[256];
			bool packed = false;

			if (de->d_name[0] == '.')
				continue;
			snprintf(src_path, sizeof(src_path), "%s/%s",
				 RES_DISKS_DIR, de->d_name);
			if (!is_regular_file(src_path))
				continue;
			snprintf(base, sizeof(base), "%s", de->d_name);
			if (has_suffix_ci(base, ".zip")) {
				/* shipped packed: user copy is unpacked */
				base[strlen(base) - 4] = '\0';
				packed = true;
			}
			snprintf(user_path, sizeof(user_path), "%s/%s",
				 user_disks_dir, base);
			if (is_regular_file(user_path)) {
				if (entry_count <
				    (int)(sizeof(entries) / sizeof(entries[0]))) {
					struct mount_entry *e = &entries[entry_count++];
					snprintf(e->base, sizeof(e->base), "%s", base);
					e->dt = devtype_for(base);
					e->pending_idx = -1;
				}
			} else if (pending_disk_count < MAX_PENDING_DISKS &&
				   entry_count <
				   (int)(sizeof(entries) / sizeof(entries[0]))) {
				struct pending_disk *p =
					&pending_disks[pending_disk_count];
				snprintf(p->name, sizeof(p->name), "%s", base);
				p->packed = packed;
				p->target = -1;
				struct mount_entry *e = &entries[entry_count++];
				snprintf(e->base, sizeof(e->base), "%s", base);
				e->dt = devtype_for(base);
				e->pending_idx = pending_disk_count;
				pending_disk_count++;
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
			for (int i = 0; i < entry_count; i++)
				if (strcmp(entries[i].base, de->d_name) == 0) {
					known = true;
					break;
				}
			if (known)
				continue;
			snprintf(path, sizeof(path), "%s/%s",
				 user_disks_dir, de->d_name);
			if (!is_regular_file(path))
				continue;
			if (entry_count < (int)(sizeof(entries) / sizeof(entries[0]))) {
				struct mount_entry *e = &entries[entry_count++];
				snprintf(e->base, sizeof(e->base), "%s", de->d_name);
				e->dt = devtype_for(de->d_name);
				e->pending_idx = -1;
			}
		}
		closedir(d);
	}

	/* hard disks first so the bootable disk lands on target 0 (the
	 * ROM boots from SCSI id 0 by default), optical media after */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < entry_count; i++) {
			if ((pass == 0) !=
			    (entries[i].dt == DEVTYPE_HARDDISK))
				continue;
			assign_target(&entries[i]);
		}
	}
	fprintf(stderr, "EWOK-ASSETS: %d mount entr%s, %d pending\n",
		entry_count, entry_count == 1 ? "y" : "ies",
		pending_disk_count);
}

/*----------------------------------------------------------------------*/
/*  deferred preparation of the pending user copies (macemu              */
/*  AssetsPrepareUserDisks): runs once the window is visible, before     */
/*  the SCSI layer opens the drives                                      */
/*----------------------------------------------------------------------*/

/* Expected size of a user copy: a shipped raw image is copied as-is,
 * a shipped <name>.zip grows to its uncompressed size */
static off_t pending_user_copy_size(const struct pending_disk *p)
{
	char src_path[512];
	struct stat st;

	snprintf(src_path, sizeof(src_path), "%s/%s",
		pending_src_dir, p->name);
	if (is_regular_file(src_path) && stat(src_path, &st) == 0)
		return st.st_size;
	char zip_path[512];
	struct zip_entry_info ent;
	snprintf(zip_path, sizeof(zip_path), "%s.zip", src_path);
	if (zip_find_entry(zip_path, p->name, &ent))
		return (off_t)ent.usize;
	return 0;
}

/* A pending copy failed: the user copy will never appear, so drop the
 * reserved SCSI target instead of mounting a missing file */
static void fallback_pending(const struct pending_disk *p)
{
	if (p->target < 0 || p->target >= ESP_MAX_DEVS)
		return;
	ConfigureParams.SCSI.target[p->target].szImageName[0] = '\0';
	ConfigureParams.SCSI.target[p->target].nDeviceType = DEVTYPE_NONE;
	ConfigureParams.SCSI.target[p->target].bDiskInserted = false;
}

void Ewok_PrepareUserDisks(void)
{
	if (pending_disk_count == 0)
		return;

	if (!ensure_dir_exists(pending_dst_dir)) {
		for (int i = 0; i < pending_disk_count; i++)
			fallback_pending(&pending_disks[i]);
		pending_disk_count = 0;
		return;
	}

	static off_t pending_sizes[MAX_PENDING_DISKS];
	copy_total_bytes = 0;
	for (int i = 0; i < pending_disk_count; i++) {
		pending_sizes[i] = pending_user_copy_size(&pending_disks[i]);
		copy_total_bytes += pending_sizes[i];
	}

	copy_base_bytes = 0;
	copy_last_splash_ms = 0;
	fprintf(stderr, "EWOK-ASSETS: preparing %d disk image(s), %lld bytes, into %s\n",
		pending_disk_count, (long long)copy_total_bytes, pending_dst_dir);
	Screen_DiskCopySplash(0, (int)copy_total_bytes);

	for (int i = 0; i < pending_disk_count; i++) {
		char src_path[512];
		char dst_path[512];
		snprintf(src_path, sizeof(src_path), "%s/%s",
			pending_src_dir, pending_disks[i].name);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			pending_dst_dir, pending_disks[i].name);
		bool ok;
		if (is_regular_file(src_path)) {
			ok = copy_file_if_needed(src_path, dst_path,
				copy_progress_cb, NULL);
		} else {
			/* shipped images are packed as <name>.zip */
			char zip_path[512];
			snprintf(zip_path, sizeof(zip_path), "%s.zip", src_path);
			ok = extract_zip_file(zip_path, pending_disks[i].name,
				dst_path, copy_progress_cb, NULL);
		}
		if (ok) {
			struct stat st;
			if (stat(dst_path, &st) == 0)
				fprintf(stderr, "EWOK-ASSETS: prepared %s (%lld bytes)\n",
					dst_path, (long long)st.st_size);
		} else {
			fprintf(stderr, "EWOK-ASSETS: preparing %s FAILED\n", dst_path);
			fallback_pending(&pending_disks[i]);
		}
		copy_base_bytes += pending_sizes[i];
	}

	/* hide the splash again */
	Screen_DiskCopySplash(0, -1);
	pending_disk_count = 0;
}
