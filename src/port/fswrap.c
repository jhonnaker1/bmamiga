/*
 * BMA800XL - A bare metal Atari 800XL emulator for Raspberry Pi
 * Copyright (C) 2026 aminch <https://github.com/aminch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Modified for BMCoco.
 *
 * Modified for BMAmiga.
 *
 * fswrap.c - file system fixes for Circle's FatFs.
 *
 * FatFs cannot stat() the root directory of a volume, so BMAmiga's file
 * browser would think "/" doesn't exist and refuse to go up to it. The
 * kernel is linked with --wrap=stat, so every call to stat() comes here
 * first; the root (and ".") are reported as directories, everything else
 * goes to the real stat().
 *
 * readdir() (--wrap=readdir) leaves out the Mac's files: "._" ones (macOS
 * keeps a file's extended attributes in them on FAT32, and adds one to
 * files it writes, a Finder copy or not) and .DS_Store. The Amiga saw
 * DEVS:Monitors/._PAL and tried to run it from its Startup-Sequence; the
 * menu's file browser listed them too.
 *
 * statvfs() (--wrap=statvfs) is FatFs's free space: circle-newlib's is a
 * stub that fails without setting errno, so Amiberry's folder drives
 * answered every ACTION_INFO and ACTION_DISK_INFO (Workbench's window
 * titles, Info, a program asking how much room there is) with an error,
 * whatever errno was left over ("Unimplemented error Bad file number" in
 * the log). FatFs counts the free space once (from the FAT's FSInfo, or
 * the whole FAT) and keeps it up to date after that.
 */

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statvfs.h>
#include "ff.h"

int __real_stat(const char *path, struct stat *st);

static int is_root(const char *path)
{
	const char *colon = strchr(path, ':');
	/* Skip a volume name such as "SD:" */
	if (colon != NULL)
		path = colon + 1;
	while (*path == '/')
		path++;
	return *path == '\0' || strcmp(path, ".") == 0 || strcmp(path, "./") == 0;
}

int __wrap_stat(const char *path, struct stat *st)
{
	if (path != NULL && st != NULL && is_root(path)) {
		memset(st, 0, sizeof(*st));
		st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
		st->st_nlink = 1;
		return 0;
	}
	return __real_stat(path, st);
}

struct dirent *__real_readdir(DIR *dir);

static int is_mac_file(const char *name)
{
	return (name[0] == '.' && name[1] == '_' && name[2] != '\0') || strcmp(name, ".DS_Store") == 0;
}

struct dirent *__wrap_readdir(DIR *dir)
{
	struct dirent *entry;
	while ((entry = __real_readdir(dir)) != NULL && is_mac_file(entry->d_name))
		;
	return entry;
}

int __wrap_statvfs(const char *path, struct statvfs *buf)
{
	DWORD free_clusters = 0;
	FATFS *fs = NULL;
	unsigned long cluster;
	if (path == NULL || buf == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (f_getfree(path, &free_clusters, &fs) != FR_OK || fs == NULL) {
		errno = ENOENT;
		return -1;
	}
#if FF_MAX_SS != FF_MIN_SS
	cluster = (unsigned long) fs->csize * fs->ssize;
#else
	cluster = (unsigned long) fs->csize * FF_MAX_SS;
#endif
	memset(buf, 0, sizeof *buf);
	buf->f_bsize = cluster;
	buf->f_frsize = cluster;
	buf->f_blocks = fs->n_fatent > 2 ? fs->n_fatent - 2 : 0;
	buf->f_bfree = free_clusters;
	buf->f_bavail = free_clusters;
	buf->f_namemax = 255;
	return 0;
}
