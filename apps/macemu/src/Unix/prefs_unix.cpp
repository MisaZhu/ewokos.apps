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
 *  <app dir>/res/roms; every regular file in <app dir>/res/disks gets a
 *  copy in <home>/docs/macemu/disks (like minivmac) which is added as a
 *  "disk" volume unless it is already listed, so guest writes persist.
 *  Missing user copies are not created here (LoadPrefs runs before the
 *  window exists); they are recorded and copied by
 *  AssetsPrepareUserDisks() once the window is visible, with a copy
 *  icon shown in the window while it runs.
 */

static bool is_regular_file(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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
	char buffer[1024*32];
	ssize_t nread;
	struct stat st;
	off_t copied = 0;
	off_t file_size = 0;

	if (access(dst_path, F_OK) == 0)
		return true;

	int src_fd = open(src_path, O_RDONLY);
	if (src_fd < 0)
		return false;
	if (fstat(src_fd, &st) == 0)
		file_size = st.st_size;

	int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dst_fd < 0) {
		close(src_fd);
		return false;
	}

	while ((nread = read(src_fd, buffer, sizeof(buffer))) > 0) {
		if (write_all_fd(dst_fd, buffer, (size_t)nread) != (size_t)nread) {
			close(dst_fd);
			close(src_fd);
			unlink(dst_path);
			return false;
		}
		copied += nread;
		if (progress != NULL)
			progress(copied, file_size, arg);
	}

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

	// Disk volumes: prefer a writable copy in <home>/docs/macemu/disks
	// (like minivmac), so guest writes persist.  Missing user copies are
	// deferred to AssetsPrepareUserDisks() which runs once the window is
	// visible; the prefs already point at the future user copy, and
	// AssetsPrepareUserDisks() finishes before DiskInit() opens the drives.
	char disks_dir[256];
	char user_disks_dir[256];
	snprintf(disks_dir, sizeof(disks_dir), "%s/disks", res_dir);
	if (!get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir)))
		user_disks_dir[0] = '\0';
	DIR *d = opendir(disks_dir);
	if (d != NULL) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.')
				continue;
			char src_path[512];
			snprintf(src_path, sizeof(src_path), "%s/%s", disks_dir, de->d_name);
			if (!is_regular_file(src_path))
				continue;

			// Prefer the writable user copy; fall back to the shipped file
			char user_path[512];
			const char *mount_path = src_path;
			if (user_disks_dir[0] != '\0') {
				snprintf(user_path, sizeof(user_path), "%s/%s",
					user_disks_dir, de->d_name);
				if (is_regular_file(user_path)) {
					mount_path = user_path;
				} else if (pending_disk_count < MAX_PENDING_DISKS) {
					// No user copy yet: create it once the window is up
					snprintf(pending_disk_names[pending_disk_count],
						sizeof(pending_disk_names[0]), "%s", de->d_name);
					snprintf(pending_src_dir, sizeof(pending_src_dir),
						"%s", disks_dir);
					snprintf(pending_dst_dir, sizeof(pending_dst_dir),
						"%s", user_disks_dir);
					pending_disk_count++;
					mount_path = user_path;
				}
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
}


/*
 *  Deferred copy of the shipped disk images into the user directory
 *  (EwokOS).  Called from InitAll() after the window is visible and
 *  before DiskInit() opens the drives, so the "disk" prefs already
 *  point at the user copies when they are mounted.  A "copying disk"
 *  icon with byte progress (video_xwin.cpp) is shown while it runs.
 *  On failure the affected "disk" entry falls back to the shipped file.
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

static void fallback_disk_pref(const char *disk_name)
{
	char src_path[512];
	char user_path[512];

	snprintf(src_path, sizeof(src_path), "%s/%s", pending_src_dir, disk_name);
	snprintf(user_path, sizeof(user_path), "%s/%s", pending_dst_dir, disk_name);
	for (int i = 0; ; i++) {
		const char *p = PrefsFindString("disk", i);
		if (p == NULL)
			break;
		if (strcmp(p, user_path) == 0) {
			PrefsReplaceString("disk", src_path, i);
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
		if (!copy_file_if_needed(src_path, dst_path, copy_progress_cb, NULL)) {
			printf("WARNING: cannot copy %s to %s, using the shipped file\n",
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
