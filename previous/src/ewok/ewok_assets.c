/*
 *  ewok_assets.c - EwokOS asset handling for Previous
 *
 *  Bundled disk images in /apps/previous/res/disks are copied to the
 *  user's home directory (<home>/docs/previous/disks) on first run and
 *  mounted from the user copies, so that guest writes persist across
 *  reboots (same convention as minivmac/macemu).
 */

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ewoksys/session.h>

#include "configuration.h"

#define RES_DISKS_DIR "/apps/previous/res/disks"

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

static void ensure_dir_exists(const char *path)
{
	char tmp[512];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static bool is_regular_file(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return false;
	return S_ISREG(st.st_mode);
}

static void copy_file_if_needed(const char *src, const char *dst)
{
	char buf[4096];
	FILE *fin, *fout;
	size_t n;

	if (is_regular_file(dst))
		return; /* already copied */

	fin = fopen(src, "rb");
	if (fin == NULL)
		return;
	fout = fopen(dst, "wb");
	if (fout == NULL) {
		fclose(fin);
		return;
	}
	while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
		if (fwrite(buf, 1, n, fout) != n) {
			/* failed: drop the half-written copy */
			fclose(fin);
			fclose(fout);
			unlink(dst);
			return;
		}
	}
	fclose(fin);
	fclose(fout);
}

/* optical-disk-ish images mount read-only as CD devices */
static SCSI_DEVTYPE devtype_for(const char *name)
{
	if (has_suffix_ci(name, ".iso") || has_suffix_ci(name, ".iso.zip") ||
	    has_suffix_ci(name, ".od") || has_suffix_ci(name, ".od.zip"))
		return DEVTYPE_CD;
	return DEVTYPE_HARDDISK;
}

void Ewok_AutoMountDisks(void)
{
	char user_disks_dir[512];
	DIR *d;
	struct dirent *de;
	int target = 0;

	if (!get_user_disks_dir(user_disks_dir, sizeof(user_disks_dir)))
		return;
	ensure_dir_exists(user_disks_dir);

	/* copy the bundled images into the user dir first */
	d = opendir(RES_DISKS_DIR);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			char src_path[512], dst_path[512];
			if (de->d_name[0] == '.')
				continue;
			snprintf(src_path, sizeof(src_path), "%s/%s",
			         RES_DISKS_DIR, de->d_name);
			if (!is_regular_file(src_path))
				continue;
			snprintf(dst_path, sizeof(dst_path), "%s/%s",
			         user_disks_dir, de->d_name);
			copy_file_if_needed(src_path, dst_path);
		}
		closedir(d);
	}

	/* mount the user copies onto the free SCSI targets */
	d = opendir(user_disks_dir);
	if (d == NULL)
		return;
	while ((de = readdir(d)) != NULL && target < ESP_MAX_DEVS) {
		char path[512];
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s",
		         user_disks_dir, de->d_name);
		if (!is_regular_file(path))
			continue;
		/* find the next target the config left empty */
		while (target < ESP_MAX_DEVS &&
		       ConfigureParams.SCSI.target[target].szImageName[0] != '\0')
			target++;
		if (target >= ESP_MAX_DEVS)
			break;
		snprintf(ConfigureParams.SCSI.target[target].szImageName, FILENAME_MAX,
		         "%s", path);
		ConfigureParams.SCSI.target[target].nDeviceType = devtype_for(de->d_name);
		ConfigureParams.SCSI.target[target].bDiskInserted = true;
		ConfigureParams.SCSI.target[target].bWriteProtected = false;
		target++;
	}
	closedir(d);
}
