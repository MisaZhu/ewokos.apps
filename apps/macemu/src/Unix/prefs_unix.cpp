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
 *  <app dir>/res/roms; add every regular file in <app dir>/res/disks as a
 *  "disk" volume unless it is already listed.
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

	// Disk volumes
	char disks_dir[256];
	snprintf(disks_dir, sizeof(disks_dir), "%s/disks", res_dir);
	DIR *d = opendir(disks_dir);
	if (d != NULL) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.')
				continue;
			char path[256];
			snprintf(path, sizeof(path), "%s/%s", disks_dir, de->d_name);
			if (!is_regular_file(path))
				continue;
			bool listed = false;
			for (int i = 0; ; i++) {
				const char *p = PrefsFindString("disk", i);
				if (p == NULL)
					break;
				if (strcmp(p, path) == 0) {
					listed = true;
					break;
				}
			}
			if (!listed)
				PrefsAddString("disk", path);
		}
		closedir(d);
	}
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
