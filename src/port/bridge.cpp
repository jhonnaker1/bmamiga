/*
 * bridge.cpp - BMAmiga's calls into Amiberry, beyond the libretro API.
 *
 * The libretro API has one disk drive (a list of images to swap) and no
 * way to reset or warp the Amiga; BMAmiga's menu has DF0: to DF3:, hard
 * drives DH0: to DH3: (disk images or folders), a CD drive, Reset, Hard
 * Reset and Warp (F11). Amiberry is linked into BMAmiga rather than
 * loaded as a libretro core, so these call it directly. This file is
 * compiled with the core's own flags (src/port/amiberry.mk), so that it
 * can use its headers; platform.c sees only the C functions below.
 *
 * They are called between retro_run() calls, as the core itself does with
 * its disk swaps.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "custom.h"
#include "disk.h"
#include "blkdev.h"
#include "inputdevice.h"
#include "uae.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

extern "C" {

/* Puts PATH in floppy drive DRIVE (0-3), first connecting the drive if it
   isn't (a 3.5" double density drive, as the A500 and A1200 had). */
int bmamiga_core_insert_floppy(int drive, const char *path)
{
	TCHAR name[MAX_DPATH];
	if (drive < 0 || drive >= 4 || path == nullptr)
		return 0;
	if (changed_prefs.floppyslots[drive].dfxtype < 0) {
		changed_prefs.floppyslots[drive].dfxtype = DRV_35_DD;
		if (changed_prefs.nr_floppies < drive + 1)
			changed_prefs.nr_floppies = drive + 1;
		set_config_changed();
	}
	uae_tcslcpy(name, path, MAX_DPATH);
	disk_eject(drive);
	disk_insert(drive, name);
	/* After disk_eject(), which empties the name in changed_prefs too: a
	   hard reset puts changed_prefs' disk in the drive. */
	uae_tcslcpy(changed_prefs.floppyslots[drive].df, name, MAX_DPATH);
	return 1;
}

void bmamiga_core_eject_floppy(int drive)
{
	if (drive < 0 || drive >= 4)
		return;
	changed_prefs.floppyslots[drive].df[0] = 0;
	disk_eject(drive);
}

/* ---- Hard drives and the CD -------------------------------------------- */

/* True if the image at PATH has a Rigid Disk Block (a partitioned disk,
   as HDToolBox makes) in its first 16 blocks, as the libretro core checks:
   then Amiberry reads its geometry and partitions from it. */
static bool has_rdb(const char *path)
{
	unsigned char block[512];
	FILE *f = fopen(path, "rb");
	bool found = false;
	if (f == nullptr)
		return false;
	for (int i = 0; i < 16 && !found; i++)
		if (fread(block, 1, sizeof block, f) == sizeof block && memcmp(block, "RDSK", 4) == 0)
			found = true;
	fclose(f);
	return found;
}

/* A path for a configuration line: in quotes if it has a comma, which
   would end it otherwise. */
static void quoted(char *out, size_t size, const char *path)
{
	if (strchr(path, ',') != nullptr || strchr(path, '"') != nullptr)
		snprintf(out, size, "\"%s\"", path);
	else
		snprintf(out, size, "%s", path);
}

/* The hard drives: PATHS[0] to PATHS[3] are DH0: to DH3: (NULL or "" for
   none): a disk image (HDF), or a folder, which the Amiga sees as a volume
   named NAMES[n]. They are mounted at the next hard reset, as Amiberry's
   own GUI does (its "filesys hack": the list goes to the running
   configuration too, as filesys.cpp reads it there). DH0: boots first
   (priority 0), the others after (-1), and a floppy in DF0: before them
   all. Returns the number of drives. */
int bmamiga_core_set_hard_drives(const char *const paths[4], const char *const names[4])
{
	int n = 0;
	changed_prefs.mountitems = 0;
	for (auto &m : changed_prefs.mountconfig) {
		m.configoffset = -1;
		m.unitnum = -1;
	}
	for (int d = 0; d < 4; d++) {
		struct stat st;
		char path[MAX_DPATH + 2], line[MAX_DPATH * 2];
		if (paths[d] == nullptr || paths[d][0] == '\0' || stat(paths[d], &st) != 0)
			continue;
		quoted(path, sizeof path, paths[d]);
		if (S_ISDIR(st.st_mode))
			snprintf(line, sizeof line, "filesystem2=rw,DH%d:%s:%s,%d", d,
			         names[d] && names[d][0] ? names[d] : "Work", path, d == 0 ? 0 : -1);
		else
			snprintf(line, sizeof line, "hardfile2=rw,DH%d:%s,%s,%d", d, path,
			         has_rdb(paths[d]) ? "0,0,0,512" : "32,1,2,512", d == 0 ? 0 : -1);
		cfgfile_parse_line(&changed_prefs, line, 0);
		n++;
	}
	currprefs.mountitems = changed_prefs.mountitems;
	memcpy(&currprefs.mountconfig, &changed_prefs.mountconfig, sizeof currprefs.mountconfig);
	return n;
}

/* The CD drive's image (NULL or "" for none). On a CD32 or CDTV it is the
   built-in drive; on the other Amigas it is uaescsi.device's unit 0, which
   Amiberry's file system mounts as CD0: (so the Amiga needs no CD file
   system), and which needs a hard reset the first time: Amiberry only
   turns its SCSI device on as the Amiga starts. Afterwards Amiberry
   notices a disc changed in changed_prefs and swaps it, as on a CD32. */
void bmamiga_core_set_cd(const char *path, int builtin)
{
	if (path != nullptr && path[0] != '\0') {
		uae_tcslcpy(changed_prefs.cdslots[0].name, path, MAX_DPATH);
		changed_prefs.cdslots[0].inuse = true;
		changed_prefs.cdslots[0].type = SCSI_UNIT_IMAGE;
		if (!builtin && currprefs.scsi != 1) {
			changed_prefs.scsi = currprefs.scsi = 1;
			changed_prefs.automount_cddrives = currprefs.automount_cddrives = true;
			currprefs.cdslots[0] = changed_prefs.cdslots[0];
		}
	} else {
		changed_prefs.cdslots[0].name[0] = 0;
	}
	set_config_changed();
}

/* On a CD32, the joystick port is a CD32 pad (its seven buttons, which
   CD32 games read as the pad sends them), as Amiberry itself would have
   it: its libretro core makes it a plain joystick. Called each frame,
   since the core sets the port again when its options change. */
void bmamiga_core_cd32_pad(void)
{
	if (!currprefs.cs_cd32cd || currprefs.jports[1].mode == JSEM_MODE_JOYSTICK_CD32)
		return;
	changed_prefs.jports[1].mode = currprefs.jports[1].mode = JSEM_MODE_JOYSTICK_CD32;
	inputdevice_updateconfig(&changed_prefs, &currprefs);
}

/* HARD: as if switched off and on; otherwise the reset keys. */
void bmamiga_core_reset(int hard)
{
	uae_reset(hard ? 1 : 0, hard ? 0 : 1);
}

void bmamiga_core_warp(int on)
{
	warpmode(on ? 1 : 0);
}

/* The Amiga's frame rate as Amiberry times its frames (49.92 Hz for PAL),
   which the libretro API rounds (to 50). */
double bmamiga_core_frame_rate(void)
{
	return fake_vblank_hz > 1.0f ? fake_vblank_hz : vblank_hz;
}

}
