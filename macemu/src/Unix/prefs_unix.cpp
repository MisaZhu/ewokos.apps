/*
 *  prefs_unix.cpp - Preferences handling, Unix specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ewoksys/cmain.h>
#include <ewoksys/session.h>
#include <ewoksys/kernel_tic.h>

#include <string>
using std::string;

#include "prefs.h"
#include "disk.h"


// Platform-specific preferences items
prefs_desc platform_prefs_items[] = {
	{"keycodes", TYPE_BOOLEAN, false,      "use keycodes rather than keysyms to decode keyboard"},
	{"keycodefile", TYPE_STRING, false,    "path of keycode translation file"},
	{"fbdevicefile", TYPE_STRING, false,   "path of frame buffer device specification file"},
	{"mousewheelmode", TYPE_INT32, false,  "mouse wheel support mode (0=page up/down, 1=cursor up/down)"},
	{"mousewheellines", TYPE_INT32, false, "number of lines to scroll in mouse wheel mode 1"},
	{"dsp", TYPE_STRING, false,            "audio output (dsp) device name"},
	{"mixer", TYPE_STRING, false,          "audio mixer device name"},
#ifdef HAVE_SIGSEGV_SKIP_INSTRUCTION
	{"ignoresegv", TYPE_BOOLEAN, false,    "ignore illegal memory accesses"},
#endif
	{"idlewait", TYPE_BOOLEAN, false,      "sleep when idle"},
	{NULL, TYPE_END, false, NULL} // End of list
};


// Prefs file name and path
const char PREFS_FILE_NAME[] = ".basilisk_ii_prefs";
string UserPrefsPath;
static string prefs_path;


/*
 *  Auto-detect assets shipped next to the executable (EwokOS):
 *  if the configured "rom" does not exist, use the first regular file in
 *  <app dir>/res/roms.  Disk volumes come from two places: .dsk images
 *  are mounted ONLY from <home>/docs/macemu/disks (writable, guest
 *  writes persist; the .dsk files shipped in res/disks are copied
 *  there on first run, and the user can add their own); .iso images
 *  are not copied, they are loaded from <app dir>/res/disks, real
 *  ISO 9660 ones read-only as "cdrom" drives, misnamed raw disk
 *  images (no CD001 PVD) read-only as disks.  Boot order: every image
 *  with valid boot blocks and a blessed System Folder is a candidate;
 *  with a single candidate it boots automatically, with two or more
 *  AssetsBootChoose() asks the user which one to start from once the
 *  window is up.  The chosen volume is moved to the front of the
 *  "disk" list.
 *  Missing .dsk copies are not created here (LoadPrefs runs before
 *  the window exists); they are recorded and copied by
 *  AssetsPrepareUserDisks() once the window is visible, with a copy
 *  icon shown in the window while it runs.
 */

static bool is_regular_file(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool has_suffix_ci(const char *name, const char *suffix)
{
	size_t name_len = strlen(name);
	size_t suf_len = strlen(suffix);

	if (name_len < suf_len)
		return false;
	name += name_len - suf_len;
	for (size_t i = 0; i < suf_len; i++) {
		char a = name[i], b = suffix[i];
		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z')
			b += 'a' - 'A';
		if (a != b)
			return false;
	}
	return true;
}

/*
 *  Basilisk II only supports SE/Classic ROMs (0x0276, 256/512KB) and
 *  32-bit clean Mac II ROMs (0x067c, 512KB/1MB); the ROM version lives
 *  big-endian at offset 8 of the image.
 */

static bool is_supported_rom(const char *path)
{
	unsigned char buf[16];
	bool ok = false;

	int fd = open(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t got = 0;
		while (got < (ssize_t)sizeof(buf)) {
			ssize_t r = read(fd, buf + got, sizeof(buf) - got);
			if (r <= 0)
				break;
			got += r;
		}
		if (got == (ssize_t)sizeof(buf)) {
			unsigned version = (buf[8] << 8) | buf[9];
			ok = (version == 0x0276 || version == 0x067c);
		}
		close(fd);
	}
	return ok;
}

/*
 *  ISO 9660 images carry a primary volume descriptor (type 1, "CD001")
 *  at logical block 16.  Files named .iso without one are raw disk
 *  images (e.g. a bootable HFS installer volume) and must be mounted
 *  as disks, not CD-ROM drives.
 */

static bool is_iso9660_image(const char *path)
{
	unsigned char buf[5];
	bool ok = false;

	int fd = open(path, O_RDONLY);
	if (fd >= 0) {
		if (lseek(fd, 16 * 2048 + 1, SEEK_SET) == 16 * 2048 + 1) {
			ssize_t got = 0;
			while (got < (ssize_t)sizeof(buf)) {
				ssize_t r = read(fd, buf + got, sizeof(buf) - got);
				if (r <= 0)
					break;
				got += r;
			}
			if (got == (ssize_t)sizeof(buf))
				ok = memcmp(buf, "CD001", 5) == 0;
		}
		close(fd);
	}
	return ok;
}

/*
 *  A raw HFS image can boot when it carries valid boot blocks
 *  (bbID 'lk'), an HFS master directory block ('BD') and a blessed
 *  System Folder (the boot directory ID in drFndrInfo is non-zero).
 *  Used to pick the boot volume: a bootable .dsk wins, otherwise a
 *  bootable raw installer image is used.
 */

static bool read_at_fd(int fd, off_t off, void *buf, size_t len)
{
	if (lseek(fd, off, SEEK_SET) != off)
		return false;
	size_t got = 0;
	while (got < len) {
		ssize_t r = read(fd, (char *)buf + got, len - got);
		if (r <= 0)
			return false;
		got += (size_t)r;
	}
	return true;
}

static bool is_bootable_hfs_image(const char *path, char *vol_name,
	size_t vol_name_size)
{
	unsigned char bb_id[2], mdb_sig[2], blessed[4];
	bool ok = false;

	if (vol_name != NULL && vol_name_size != 0)
		vol_name[0] = '\0';

	int fd = open(path, O_RDONLY);
	if (fd >= 0) {
		ok = read_at_fd(fd, 0, bb_id, 2) &&
			bb_id[0] == 0x4c && bb_id[1] == 0x4b &&
			read_at_fd(fd, 1024, mdb_sig, 2) &&
			mdb_sig[0] == 'B' && mdb_sig[1] == 'D' &&
			read_at_fd(fd, 1024 + 92, blessed, 4) &&
			(blessed[0] | blessed[1] | blessed[2] | blessed[3]) != 0;
		// Volume name (drVN, Pascal string at MDB+36) for the boot
		// chooser; printable ASCII only
		if (ok && vol_name != NULL && vol_name_size != 0) {
			unsigned char vn[28];
			if (read_at_fd(fd, 1024 + 36, vn, sizeof(vn))) {
				size_t n = vn[0];
				if (n > sizeof(vn) - 1)
					n = sizeof(vn) - 1;
				size_t o = 0;
				for (size_t j = 1; j <= n && o + 1 < vol_name_size; j++) {
					char c = (char)vn[j];
					if (c >= 0x20 && c < 0x7f)
						vol_name[o++] = c;
				}
				vol_name[o] = '\0';
			}
		}
		close(fd);
	}
	return ok;
}

/*
 *  Boot volume candidates: more than one bootable image defers the
 *  choice to AssetsBootChoose() which asks the user (chooser UI in
 *  video_xwin.cpp) once the window is up.
 */
#define MAX_BOOT_CHOICES 8
static char boot_choice_entries[MAX_BOOT_CHOICES][514];
static char boot_choice_names[MAX_BOOT_CHOICES][64];
// Must match ICON_* in video_xwin.cpp: 0 floppy, 1 hdd, 2 cd
static int boot_choice_icon[MAX_BOOT_CHOICES];
static int boot_choice_count = 0;
static bool boot_choice_pending = false;

static size_t write_all_fd(int fd, const void *buffer, size_t count)
{
	size_t total = 0;

	while (total < count) {
		ssize_t nwritten = write(fd, (const char *)buffer + total,
			count - total);
		if (nwritten <= 0)
			break;
		total += (size_t)nwritten;
	}

	return total;
}

static bool ensure_dir_exists(const char *path)
{
	char tmp[512];
	char *p;

	if (path == NULL || path[0] == '\0')
		return false;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p != '\0'; ++p) {
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

typedef void (*copy_progress_fn)(off_t done, off_t file_size, void *arg);

static bool copy_file_if_needed(const char *src_path, const char *dst_path,
	copy_progress_fn progress, void *arg)
{
	/*
	 * 64KB matches the VFS shared-memory transfer size (SHM_MAX), so each
	 * read/write is exactly one IPC round-trip instead of two.
	 * Heap-allocated: 64KB is too large for the default thread stack.
	 */
	const size_t buf_size = 1024*64;
	char *buffer = (char *)malloc(buf_size);
	ssize_t nread;
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

	// A complete copy already exists: nothing to do.  A size
	// mismatch is a partial leftover (interrupted run): replace it
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

// Shipped disks whose user copy does not exist yet: recorded by
// autodetect_assets() and created by AssetsPrepareUserDisks() once the
// window is visible
#define MAX_PENDING_DISKS 16
static char pending_disk_names[MAX_PENDING_DISKS][256];
static int pending_disk_count = 0;
static char pending_src_dir[256];
static char pending_dst_dir[256];

// Move a "disk" prefs entry to the front of the list (it becomes
// drive 1, the first bootable volume in the drive queue)
static void move_disk_entry_first(const char *entry)
{
	char entries[MAX_PENDING_DISKS][514];
	int count = 0;

	for (int i = 0; ; i++) {
		const char *p = PrefsFindString("disk", i);
		if (p == NULL)
			break;
		if (count < MAX_PENDING_DISKS)
			snprintf(entries[count++], sizeof(entries[0]), "%s", p);
	}
	if (count == 0 || strcmp(entries[0], entry) == 0)
		return;
	bool found = false;
	for (int i = 0; i < count; i++)
		if (strcmp(entries[i], entry) == 0)
			found = true;
	if (!found)
		return;
	for (int i = count; i-- > 0; )
		PrefsRemoveItem("disk", i);
	PrefsAddString("disk", entry);
	for (int i = 0; i < count; i++)
		if (strcmp(entries[i], entry) != 0)
			PrefsAddString("disk", entries[i]);
}

static bool get_user_disks_dir(char *path, size_t size)
{
	session_info_t sinfo;

	if (path == NULL || size == 0)
		return false;

	if (session_get_by_uid(getuid(), &sinfo) != 0 || sinfo.home[0] == 0)
		return false;

	snprintf(path, size, "%s/docs/macemu/disks", sinfo.home);
	return true;
}

static void autodetect_assets(void)
{
	char res_dir[256];

	// Assets live in "res/" below the app's own directory (like minivmac)
	snprintf(res_dir, sizeof(res_dir), "%s/res", cmain_get_own_dir(NULL, 0));

	// ROM image: prefer a ROM type Basilisk II supports over anything else
	const char *rom = PrefsFindString("rom");
	if (rom == NULL || !is_regular_file(rom) || !is_supported_rom(rom)) {
		char roms_dir[256];
		char chosen[256];
		char fallback[256];
		snprintf(roms_dir, sizeof(roms_dir), "%s/roms", res_dir);
		chosen[0] = '\0';
		fallback[0] = '\0';
		DIR *d = opendir(roms_dir);
		if (d != NULL) {
			struct dirent *de;
			while ((de = readdir(d)) != NULL) {
				if (de->d_name[0] == '.')
					continue;
				char path[256];
				snprintf(path, sizeof(path), "%s/%s", roms_dir, de->d_name);
				if (!is_regular_file(path))
					continue;
				if (is_supported_rom(path)) {
					strcpy(chosen, path);
					break;
				}
				if (fallback[0] == '\0')
					strcpy(fallback, path);
			}
			closedir(d);
		}
		const char *pick = chosen[0] ? chosen : fallback;
		if (pick[0]) {
			if (rom != NULL)
				PrefsReplaceString("rom", pick);
			else
				PrefsAddString("rom", pick);
		}
	}

	// Disk volumes:
	// - .dsk images are mounted ONLY from <home>/docs/macemu/disks so
	//   guest writes persist.  The .dsk files shipped in res/disks are
	//   copied there; missing copies are deferred to
	//   AssetsPrepareUserDisks() which runs once the window is
	//   visible (the prefs already point at the future user copy and
	//   AssetsPrepareUserDisks() finishes before DiskInit() opens the
	//   drives).  The user can also add their own .dsk files, picked
	//   up by the user-dir scan below.
	// - .iso images are never copied: real ISO 9660 ones are mounted
	//   read-only from res/disks as CD-ROM drives, misnamed raw disk
	//   images (no CD001 PVD) as read-only disks.  After the scan the
	//   boot volume is chosen among all bootable images (see the boot
	//   volume selection below) and moved to the front of the "disk"
	//   list so it becomes drive 1 and the first bootable volume in
	//   the drive queue.
	char disks_dir[256];
	char user_disks_dir[256];
	snprintf(disks_dir, sizeof(disks_dir), "%s/disks", res_dir);
	if (get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir)))
		ensure_dir_exists(user_disks_dir);
	else
		user_disks_dir[0] = '\0';
	DIR *d = opendir(disks_dir);
	if (d != NULL) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.')
				continue;
			// Only .dsk and .iso are served from res/disks
			bool dsk_suffix = has_suffix_ci(de->d_name, ".dsk");
			bool iso_suffix = has_suffix_ci(de->d_name, ".iso");
			if (!dsk_suffix && !iso_suffix)
				continue;
			char src_path[512];
			snprintf(src_path, sizeof(src_path), "%s/%s", disks_dir, de->d_name);
			if (!is_regular_file(src_path))
				continue;

			// Real ISO 9660 images stay in the app's res/disks and are
			// mounted read-only as CD-ROM drives (never copied to the
			// user dir); misnamed raw disk images fall through to the
			// disk handling below
			if (iso_suffix && is_iso9660_image(src_path)) {
				// Migrate stale "disk" entries (shipped path or an old
				// user-dir copy)
				char old_user_path[512];
				if (user_disks_dir[0] != '\0')
					snprintf(old_user_path, sizeof(old_user_path),
						"%s/%s", user_disks_dir, de->d_name);
				else
					old_user_path[0] = '\0';
				if (old_user_path[0] != '\0' &&
				    is_regular_file(old_user_path))
					unlink(old_user_path);
				for (int i = 0; ; i++) {
					const char *p = PrefsFindString("disk", i);
					if (p == NULL)
						break;
					if (strcmp(p, src_path) == 0 ||
					    (old_user_path[0] != '\0' &&
					     strcmp(p, old_user_path) == 0)) {
						PrefsRemoveItem("disk", i);
						i--;
					}
				}
				bool listed = false;
				for (int i = 0; ; i++) {
					const char *p = PrefsFindString("cdrom", i);
					if (p == NULL)
						break;
					if (strcmp(p, src_path) == 0) {
						listed = true;
						break;
					}
				}
				if (!listed)
					PrefsAddString("cdrom", src_path);
				printf("autodetect: CD-ROM image %s\n", src_path);
				continue;
			}

			// Raw disk image in .iso clothing: mount it read-only from
			// res/disks ("*" prefix, like a real CD) so guest writes
			// cannot corrupt the shipped file
			char ro_path[514];
			if (iso_suffix) {
				printf("autodetect: %s is not ISO 9660, using it as a "
					"read-only disk image\n", de->d_name);
				snprintf(ro_path, sizeof(ro_path), "*%s", src_path);
				// Drop a stale user-dir copy (old versions copied
				// .iso files) and migrate its prefs entry
				if (user_disks_dir[0] != '\0') {
					char stale_path[512];
					snprintf(stale_path, sizeof(stale_path), "%s/%s",
						user_disks_dir, de->d_name);
					if (is_regular_file(stale_path))
						unlink(stale_path);
					for (int i = 0; ; i++) {
						const char *p = PrefsFindString("disk", i);
						if (p == NULL)
							break;
						if (strcmp(p, stale_path) == 0) {
							PrefsReplaceString("disk", ro_path, i);
							break;
						}
					}
				}
			}

			// .dsk images mount ONLY from the user dir; .iso raw
			// images read-only from res/disks
			char user_path[512];
			const char *mount_path;
			if (iso_suffix) {
				mount_path = ro_path;
			} else if (user_disks_dir[0] == '\0') {
				printf("autodetect: no user dir, skipping %s\n",
					de->d_name);
				continue;
			} else {
				snprintf(user_path, sizeof(user_path), "%s/%s",
					user_disks_dir, de->d_name);
				if (!is_regular_file(user_path) &&
				    pending_disk_count < MAX_PENDING_DISKS) {
					// No user copy yet: create it once the window is up
					snprintf(pending_disk_names[pending_disk_count],
						sizeof(pending_disk_names[0]), "%s", de->d_name);
					snprintf(pending_src_dir, sizeof(pending_src_dir),
						"%s", disks_dir);
					snprintf(pending_dst_dir, sizeof(pending_dst_dir),
						"%s", user_disks_dir);
					pending_disk_count++;
				}
				mount_path = user_path;
			}

			bool listed = false;
			for (int i = 0; ; i++) {
				const char *p = PrefsFindString("disk", i);
				if (p == NULL)
					break;
				if (strcmp(p, mount_path) == 0) {
					listed = true;
					break;
				}
				if (mount_path != src_path && strcmp(p, src_path) == 0) {
					// Migrate a stale res/disks entry to the user copy
					PrefsReplaceString("disk", mount_path, i);
					listed = true;
					break;
				}
			}
			if (!listed)
				PrefsAddString("disk", mount_path);
		}
		closedir(d);
	}

	// The user dir is user-managed: pick up .dsk files the user added
	// there on their own (no shipped counterpart); never delete user
	// files
	if (user_disks_dir[0] != '\0') {
		DIR *ud = opendir(user_disks_dir);
		if (ud != NULL) {
			struct dirent *de;
			while ((de = readdir(ud)) != NULL) {
				if (de->d_name[0] == '.' ||
				    !has_suffix_ci(de->d_name, ".dsk"))
					continue;
				char user_path[512];
				snprintf(user_path, sizeof(user_path), "%s/%s",
					user_disks_dir, de->d_name);
				if (!is_regular_file(user_path))
					continue;
				bool listed = false;
				for (int i = 0; ; i++) {
					const char *p = PrefsFindString("disk", i);
					if (p == NULL)
						break;
					if (strcmp(p, user_path) == 0) {
						listed = true;
						break;
					}
				}
				if (!listed)
					PrefsAddString("disk", user_path);
			}
			closedir(ud);
		}
	}

	// Drop "disk" entries whose file no longer exists (a removed user
	// disk, an old res/disks path); pending user copies are exempt,
	// AssetsPrepareUserDisks() creates them before the drives open
	for (int i = 0; ; i++) {
		const char *p = PrefsFindString("disk", i);
		if (p == NULL)
			break;
		const char *path = (p[0] == '*') ? p + 1 : p;
		if (is_regular_file(path))
			continue;
		bool pending = false;
		if (pending_dst_dir[0] != '\0' &&
		    strncmp(path, pending_dst_dir, strlen(pending_dst_dir)) == 0) {
			const char *base = strrchr(path, '/');
			if (base != NULL) {
				base++;
				for (int j = 0; j < pending_disk_count; j++)
					if (strcmp(pending_disk_names[j], base) == 0) {
						pending = true;
						break;
					}
			}
		}
		if (pending)
			continue;
		printf("autodetect: dropping missing disk %s\n", p);
		PrefsRemoveItem("disk", i);
		i--;
	}

	// Boot volume selection: every disk image with valid boot blocks
	// (bbID 'lk') and a blessed System Folder is a boot candidate,
	// .dsk volumes and raw installer images alike.  With a single
	// candidate it boots automatically; with TWO OR MORE the choice
	// is deferred: the first candidate becomes the default boot volume
	// now and AssetsBootChoose() asks the user once the window is up
	// (chooser UI in video_xwin.cpp).  The chosen entry is moved to
	// the front of the "disk" list; the list order becomes the drive
	// queue order, so it is drive 1 and the first bootable volume the
	// ROM finds.
	char entries[MAX_PENDING_DISKS][514];
	int count = 0;
	for (int i = 0; ; i++) {
		const char *p = PrefsFindString("disk", i);
		if (p == NULL)
			break;
		if (count < MAX_PENDING_DISKS)
			snprintf(entries[count++], sizeof(entries[0]), "%s", p);
	}
	struct boot_cand {
		char entry[514];
		char name[64];
		bool is_dsk;
		int icon;
	};
	// Static: too large for comfort on EwokOS thread stacks
	static struct boot_cand cands[MAX_BOOT_CHOICES];
	int ncand = 0;
	for (int i = 0; i < count && ncand < MAX_BOOT_CHOICES; i++) {
		const char *path = entries[i];
		if (path[0] == '*')
			path++;
		bool is_dsk = has_suffix_ci(path, ".dsk");
		char sniff[514];
		snprintf(sniff, sizeof(sniff), "%s", path);
		if (is_dsk) {
			// A pending user copy does not exist yet: sniff the
			// shipped file it will be created from
			size_t udl = strlen(user_disks_dir);
			if (!is_regular_file(sniff) && udl != 0 &&
			    strncmp(sniff, user_disks_dir, udl) == 0)
				snprintf(sniff, sizeof(sniff), "%s%s", disks_dir,
					path + udl);
		}
		char vol_name[64];
		if (!is_bootable_hfs_image(sniff, vol_name, sizeof(vol_name)))
			continue;
		const char *label = vol_name[0] != '\0' ?
			vol_name : strrchr(path, '/');
		if (label == NULL)
			label = path;
		else if (vol_name[0] == '\0')
			label++;
		snprintf(cands[ncand].entry, sizeof(cands[ncand].entry), "%s",
			entries[i]);
		snprintf(cands[ncand].name, sizeof(cands[ncand].name), "%s",
			label);
		cands[ncand].is_dsk = is_dsk;
		// Icon: raw installer image = cd; a .dsk is a floppy image
		// only when it is floppy-sized, otherwise a hard disk
		if (!is_dsk) {
			cands[ncand].icon = 2;
		} else {
			struct stat ist;
			cands[ncand].icon =
				(stat(sniff, &ist) == 0 && ist.st_size <= 2*1024*1024)
				? 0 : 1;
		}
		ncand++;
	}
	// Chooser order: .dsk volumes first, then raw images (display
	// order only; the user picks).  The first entry doubles as the
	// default boot volume and the cancel fallback.
	boot_choice_count = 0;
	for (int want_dsk = 1; want_dsk >= 0; want_dsk--) {
		for (int i = 0; i < ncand; i++) {
			if ((int)cands[i].is_dsk != want_dsk)
				continue;
			snprintf(boot_choice_entries[boot_choice_count],
				sizeof(boot_choice_entries[0]), "%s", cands[i].entry);
			snprintf(boot_choice_names[boot_choice_count],
				sizeof(boot_choice_names[0]), "%s", cands[i].name);
			boot_choice_icon[boot_choice_count] = cands[i].icon;
			boot_choice_count++;
		}
	}
	// Disambiguate identical volume names (an installed system and the
	// installer are both called "Mac OS 8") by appending the file name
	for (int i = 0; i < boot_choice_count; i++) {
		for (int j = i + 1; j < boot_choice_count; j++) {
			if (strcmp(boot_choice_names[i], boot_choice_names[j]) != 0)
				continue;
			for (int k = i; k <= j; k += (j - i)) {
				const char *e = boot_choice_entries[k];
				if (e[0] == '*')
					e++;
				const char *base = strrchr(e, '/');
				base = (base != NULL) ? base + 1 : e;
				char tmp[64];
				snprintf(tmp, sizeof(tmp), "%s (%s)",
					boot_choice_names[k], base);
				snprintf(boot_choice_names[k],
					sizeof(boot_choice_names[0]), "%s", tmp);
			}
		}
	}
	if (boot_choice_count > 0) {
		printf("autodetect: boot volume %s\n", boot_choice_entries[0]);
		move_disk_entry_first(boot_choice_entries[0]);
		if (boot_choice_count >= 2) {
			boot_choice_pending = true;
			printf("autodetect: %d bootable volumes, asking the user "
				"once the window is up\n", boot_choice_count);
		}
	} else {
		printf("autodetect: no bootable disk image found\n");
	}
	if (PrefsFindInt32("bootdrive") == 1 &&
	    PrefsFindInt32("bootdriver") == DiskRefNum) {
		// Left over from an old bootcdrom run: back to automatic
		// selection (first bootable volume in the drive queue)
		PrefsReplaceInt32("bootdrive", 0);
		PrefsReplaceInt32("bootdriver", 0);
	}
}


/*
 *  Deferred copy of the shipped .dsk images into the user directory
 *  (EwokOS).  Called from InitAll() after the window is visible and
 *  before DiskInit() opens the drives, so the "disk" prefs already
 *  point at the user copies when they are mounted.  A "copying disk"
 *  icon with byte progress (video_xwin.cpp) is shown while it runs.
 *  On failure the affected "disk" entry is dropped (.dsk images only
 *  mount from the user directory).
 */

// video_xwin.cpp splash: done/total bytes across all pending images,
// total <= 0 hides it again
extern void VideoDiskCopySplash(int done, int total);

static off_t copy_base_bytes = 0;
static off_t copy_total_bytes = 0;
static uint64_t copy_last_splash_ms = 0;

static void copy_progress_cb(off_t done, off_t file_size, void *arg)
{
	(void)arg;
	// A repaint is a synchronous IPC to the x server: throttle it
	uint64_t now = kernel_tic_ms(0);
	if (done != file_size && now - copy_last_splash_ms < 80)
		return;
	copy_last_splash_ms = now;
	off_t all = copy_base_bytes + done;
	if (all > copy_total_bytes)
		all = copy_total_bytes;
	VideoDiskCopySplash((int)all, (int)copy_total_bytes);
}

// A pending copy failed: the user copy will never appear, so drop the
// entry instead of mounting it (.dsk images only mount from the user
// directory)
static void fallback_disk_pref(const char *disk_name)
{
	char user_path[512];

	snprintf(user_path, sizeof(user_path), "%s/%s", pending_dst_dir, disk_name);
	for (int i = 0; ; i++) {
		const char *p = PrefsFindString("disk", i);
		if (p == NULL)
			break;
		if (strcmp(p, user_path) == 0) {
			PrefsRemoveItem("disk", i);
			break;
		}
	}
}

void AssetsPrepareUserDisks(void)
{
	if (pending_disk_count == 0)
		return;

	if (!ensure_dir_exists(pending_dst_dir)) {
		for (int i = 0; i < pending_disk_count; i++)
			fallback_disk_pref(pending_disk_names[i]);
		pending_disk_count = 0;
		return;
	}

	copy_total_bytes = 0;
	for (int i = 0; i < pending_disk_count; i++) {
		char src_path[512];
		struct stat st;
		snprintf(src_path, sizeof(src_path), "%s/%s",
			pending_src_dir, pending_disk_names[i]);
		if (stat(src_path, &st) == 0)
			copy_total_bytes += st.st_size;
	}

	copy_base_bytes = 0;
	copy_last_splash_ms = 0;
	VideoDiskCopySplash(0, (int)copy_total_bytes);
	printf("copying %d disk image(s) to %s\n",
		pending_disk_count, pending_dst_dir);

	for (int i = 0; i < pending_disk_count; i++) {
		char src_path[512];
		char dst_path[512];
		struct stat st;
		snprintf(src_path, sizeof(src_path), "%s/%s",
			pending_src_dir, pending_disk_names[i]);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			pending_dst_dir, pending_disk_names[i]);
		if (copy_file_if_needed(src_path, dst_path, copy_progress_cb, NULL)) {
			if (stat(dst_path, &st) == 0)
				printf("copied %s (%lld bytes)\n", dst_path,
					(long long)st.st_size);
		} else {
			printf("WARNING: cannot copy %s to %s, skipping it\n",
				src_path, dst_path);
			fallback_disk_pref(pending_disk_names[i]);
		}
		if (stat(src_path, &st) == 0)
			copy_base_bytes += st.st_size;
	}

	VideoDiskCopySplash(0, 0);
	pending_disk_count = 0;
}


/*
 *  Boot volume chooser (EwokOS).  Called from InitAll() after
 *  AssetsPrepareUserDisks() and before DiskInit(): when
 *  autodetect_assets() found two or more bootable disk images the
 *  user picks one in the window (mouse or arrow keys + Enter,
 *  VideoBootChooser in video_xwin.cpp).  Cancelling (Esc / window
 *  close) keeps the default, the first candidate.  The chosen entry
 *  is moved to the front of the "disk" list before the drives open.
 */

// video_xwin.cpp: interactive chooser, returns the chosen index or
// -1 on cancel
extern int VideoBootChooser(const char *const *labels, const int *icons,
	int count);

void AssetsBootChoose(void)
{
	if (!boot_choice_pending)
		return;
	boot_choice_pending = false;

	// Drop candidates that are no longer listed as "disk" prefs (a
	// failed user-copy fell back to another path): the chooser could
	// not apply them anyway
	for (int i = 0; i < boot_choice_count; ) {
		bool listed = false;
		for (int j = 0; ; j++) {
			const char *p = PrefsFindString("disk", j);
			if (p == NULL)
				break;
			if (strcmp(p, boot_choice_entries[i]) == 0) {
				listed = true;
				break;
			}
		}
		if (listed) {
			i++;
			continue;
		}
		boot_choice_count--;
		for (int j = i; j < boot_choice_count; j++) {
			snprintf(boot_choice_entries[j],
				sizeof(boot_choice_entries[0]), "%s",
				boot_choice_entries[j + 1]);
			snprintf(boot_choice_names[j],
				sizeof(boot_choice_names[0]), "%s",
				boot_choice_names[j + 1]);
			boot_choice_icon[j] = boot_choice_icon[j + 1];
		}
	}
	if (boot_choice_count < 2)
		return;

	const char *labels[MAX_BOOT_CHOICES];
	for (int i = 0; i < boot_choice_count; i++)
		labels[i] = boot_choice_names[i];
	int sel = VideoBootChooser(labels, boot_choice_icon, boot_choice_count);
	if (sel < 0 || sel >= boot_choice_count)
		return;		// cancelled: keep the default (first candidate)
	printf("boot choice: %s\n", boot_choice_entries[sel]);
	// No-op when the choice already leads the list
	move_disk_entry_first(boot_choice_entries[sel]);
}


/*
 *  Load preferences from settings file
 */

void LoadPrefs(const char *vmdir)
{
	if (vmdir) {
		prefs_path = string(vmdir) + '/' + string("prefs");
		FILE *prefs = fopen(prefs_path.c_str(), "r");
		if (!prefs) {
			printf("No file at %s found.\n", prefs_path.c_str());
			exit(1);
		}
		LoadPrefsFromStream(prefs);
		fclose(prefs);
		return;
	}

	// Construct prefs path
	if (UserPrefsPath.empty()) {
		char *home = getenv("HOME");
		if (home)
			prefs_path = string(home) + '/';
		else
			prefs_path = "/tmp/"; // EwokOS: no HOME, use ramfs
		prefs_path += PREFS_FILE_NAME;
	} else
		prefs_path = UserPrefsPath;

	// Read preferences from settings file
	FILE *f = fopen(prefs_path.c_str(), "r");
	if (f == NULL) {
		// Fall back to the prefs shipped next to the executable (EwokOS)
		char shipped[256];
		snprintf(shipped, sizeof(shipped), "%s/res/%s",
			cmain_get_own_dir(NULL, 0), PREFS_FILE_NAME);
		f = fopen(shipped, "r");
	}
	if (f != NULL) {

		// Prefs file found, load settings
		LoadPrefsFromStream(f);
		fclose(f);

	} else {

		// No prefs file, save defaults
		SavePrefs();
	}

	// EwokOS: pick up ROM/disk images shipped next to the executable
	autodetect_assets();
}


/*
 *  Save preferences to settings file
 */

void SavePrefs(void)
{
	FILE *f;
	if ((f = fopen(prefs_path.c_str(), "w")) != NULL) {
		SavePrefsToStream(f);
		fclose(f);
	}
}


/*
 *  Add defaults of platform-specific prefs items
 *  You may also override the defaults set in PrefsInit()
 */

void AddPlatformPrefsDefaults(void)
{
	PrefsAddBool("keycodes", false);
	PrefsReplaceString("extfs", "/");
	PrefsReplaceInt32("mousewheelmode", 1);
	PrefsReplaceInt32("mousewheellines", 3);
#ifdef __linux__
	if (access("/dev/sound/dsp", F_OK) == 0) {
		PrefsReplaceString("dsp", "/dev/sound/dsp");
	} else {
		PrefsReplaceString("dsp", "/dev/dsp");
	}
	if (access("/dev/sound/mixer", F_OK) == 0) {
		PrefsReplaceString("mixer", "/dev/sound/mixer");
	} else {
		PrefsReplaceString("mixer", "/dev/mixer");
	}
#else
	PrefsReplaceString("dsp", "/dev/dsp");
	PrefsReplaceString("mixer", "/dev/mixer");
#endif
#ifdef HAVE_SIGSEGV_SKIP_INSTRUCTION
	PrefsAddBool("ignoresegv", false);
#endif
	PrefsAddBool("idlewait", true);
}
