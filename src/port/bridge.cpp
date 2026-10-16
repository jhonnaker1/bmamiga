/*
 * bridge.cpp - BMAmiga's calls into Amiberry, beyond the libretro API.
 *
 * The libretro API has one disk drive (a list of images to swap) and no
 * way to reset or warp the Amiga; BMAmiga's menu has DF0: to DF3:, Reset,
 * Hard Reset and Warp (F11). Amiberry is linked into BMAmiga rather than
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
#include "inputdevice.h"
#include "uae.h"

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
	uae_tcslcpy(changed_prefs.floppyslots[drive].df, name, MAX_DPATH);
	disk_eject(drive);
	disk_insert(drive, name);
	return 1;
}

void bmamiga_core_eject_floppy(int drive)
{
	if (drive < 0 || drive >= 4)
		return;
	changed_prefs.floppyslots[drive].df[0] = 0;
	disk_eject(drive);
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
