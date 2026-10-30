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
#include "rommgr.h"
#include "ethernet.h"

#include <stdio.h>
#include <string.h>
#include <string>
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

/* The drives' speed: 100 is a real drive's, 200 to 800 faster, 0 turbo
   (as fast as the Amiga reads). DISK_check_change() in disk.cpp takes it
   from changed_prefs. */
void bmamiga_core_set_floppy_speed(int speed)
{
	changed_prefs.floppy_speed = speed;
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

/* bmamiga.uae's drives (filesystem2=, hardfile2=, uaehf0=...), one a line,
   kept by bmamiga_core_set_config(): they go on the list after the menu's
   DH0: to DH3:. */
static std::string mounts;

/* The hard drives: PATHS[0] to PATHS[3] are DH0: to DH3: (NULL or "" for
   none): a disk image (HDF), or a folder, which the Amiga sees as a volume
   named NAMES[n]. They are mounted at the next hard reset, as Amiberry's
   own GUI does (its "filesys hack": the list goes to the running
   configuration too, as filesys.cpp reads it there). DH0: boots first
   (priority 0), the others after (-1), and a floppy in DF0: before them
   all; then bmamiga.uae's, as they are there. Returns the number of
   drives. */
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
	for (size_t at = 0; at < mounts.size();) {
		size_t end = mounts.find('\n', at);
		std::string m = mounts.substr(at, end - at);
		TCHAR line[MAX_DPATH * 2];
		uae_tcslcpy(line, m.c_str(), sizeof line / sizeof line[0]);
		cfgfile_parse_line(&changed_prefs, line, 0);
		n++;
		at = end == std::string::npos ? mounts.size() : end + 1;
	}
	for (int i = 0; i < changed_prefs.mountitems; i++) {
		const struct uaedev_config_info *ci = &changed_prefs.mountconfig[i].ci;
		write_log(_T("Drive %s: %s (%s), boot priority %d\n"), ci->devname,
		          ci->volname[0] ? ci->volname : ci->rootdir, ci->rootdir, ci->bootpri);
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

/* ---- The Amiga's setup ------------------------------------------------------ */

/* The settings the menu's Amiga Setup changes, as the model's preset made
   them (taken once the core has started), so that "Model default" puts
   them back. */
static struct {
	bool taken;
	TCHAR romfile[MAX_DPATH];
	int cpu_model, fpu_model, m68k_speed;
	bool address_space_24, cpu_cycle_exact, cpu_memory_cycle_exact, blitter_cycle_exact;
	unsigned int chipset_mask;
	uae_u32 chipmem, bogomem, fastmem;
	int sound_filter, sound_stereo_separation;
	bool sana2, socket_emu;
} preset;

static void take_preset(void)
{
	preset.taken = true;
	uae_tcslcpy(preset.romfile, changed_prefs.romfile, MAX_DPATH);
	preset.cpu_model = changed_prefs.cpu_model;
	preset.fpu_model = changed_prefs.fpu_model;
	preset.m68k_speed = changed_prefs.m68k_speed;
	preset.address_space_24 = changed_prefs.address_space_24;
	preset.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	preset.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact;
	preset.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
	preset.chipset_mask = changed_prefs.chipset_mask;
	preset.chipmem = changed_prefs.chipmem.size;
	preset.bogomem = changed_prefs.bogomem.size;
	preset.fastmem = changed_prefs.fastmem[0].size;
	preset.sound_filter = changed_prefs.sound_filter;
	preset.sound_stereo_separation = changed_prefs.sound_stereo_separation;
	preset.sana2 = changed_prefs.sana2;
	preset.socket_emu = changed_prefs.socket_emu;
}

static void put_back_preset(void)
{
	uae_tcslcpy(changed_prefs.romfile, preset.romfile, MAX_DPATH);
	changed_prefs.cpu_model = preset.cpu_model;
	changed_prefs.fpu_model = preset.fpu_model;
	changed_prefs.m68k_speed = preset.m68k_speed;
	changed_prefs.address_space_24 = preset.address_space_24;
	changed_prefs.cpu_cycle_exact = preset.cpu_cycle_exact;
	changed_prefs.cpu_memory_cycle_exact = preset.cpu_memory_cycle_exact;
	changed_prefs.blitter_cycle_exact = preset.blitter_cycle_exact;
	changed_prefs.chipset_mask = preset.chipset_mask;
	changed_prefs.chipmem.size = preset.chipmem;
	changed_prefs.bogomem.size = preset.bogomem;
	changed_prefs.fastmem[0].size = preset.fastmem;
	changed_prefs.sound_filter = preset.sound_filter;
	changed_prefs.sound_stereo_separation = preset.sound_stereo_separation;
	/* The network: no model has one. */
	changed_prefs.sana2 = preset.sana2;
	changed_prefs.socket_emu = preset.socket_emu;
	if (get_device_romconfig(&changed_prefs, ROMTYPE_A2065, 0) != nullptr)
		clear_device_rom(&changed_prefs, ROMTYPE_A2065, 0, true);
	changed_prefs.a2065name[0] = 0;
}

static bool is_mount_line(const TCHAR *s)
{
	static const char *const keys[] = {"filesystem=", "filesystem2=", "hardfile=", "hardfile2=",
	                                   "uaehf"};
	for (const char *k : keys)
		if (_tcsnicmp(s, k, strlen(k)) == 0)
			return true;
	return false;
}

/* LINES: Amiberry's own settings, as in a .uae file, one a line ("#" or ";"
   starts a comment): the menu's Amiga Setup, then bmamiga.uae's. They go
   over the model's preset (put back first) into changed_prefs, which a
   hard reset then uses, as Amiberry's own settings window does. Returns
   how many lines Amiberry took. */
int bmamiga_core_set_config(const char *lines)
{
	TCHAR line[MAX_DPATH * 2];
	const char *p = lines;
	int taken = 0;
	if (!preset.taken)
		take_preset();
	put_back_preset();
	mounts.clear();
	while (p != nullptr && *p != '\0') {
		const char *end = strchr(p, '\n');
		size_t n = end != nullptr ? (size_t) (end - p) : strlen(p);
		if (n >= sizeof line / sizeof line[0])
			n = sizeof line / sizeof line[0] - 1;
		memcpy(line, p, n);
		line[n] = 0;
		p = end != nullptr ? end + 1 : nullptr;
		/* Trim, and drop comments. */
		TCHAR *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		size_t len = _tcslen(s);
		while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
			s[--len] = 0;
		if (*s == 0 || *s == '#' || *s == ';' || _tcschr(s, '=') == nullptr)
			continue;
		/* Drives: kept for bmamiga_core_set_hard_drives(), which puts the
		   list together. */
		if (is_mount_line(s)) {
			if (mounts.size() < 16384)
				mounts += std::string(s) + "\n";
			taken++;
			continue;
		}
		cfgfile_parse_line(&changed_prefs, s, 0);
		taken++;
	}
	fixup_prefs(&changed_prefs, true);
	set_config_changed();
	return taken;
}

/* ---- The network's test (the PC simulator's "netprobe") ------------------ */

/* Talks to SLiRP as an Amiga's network card would (ethernet.cpp's driver,
   which uaenet.device and the A2065 use), and logs what comes back: STEP
   0 asks for an address (DHCP), 1 looks example.com up (DNS, which SLiRP
   passes to the network's DNS server through bmnet). */
static struct netdriverdata *probe_ndd;
static void *probe_data;
static uae_u8 probe_frame[600];
static int probe_len;
static const uae_u8 probe_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static void probe_got(void *dev, const uae_u8 *d, int len)
{
	(void) dev;
	/* Ethernet, IPv4, UDP: the source port says what answered. */
	if (len < 42 || d[12] != 0x08 || d[13] != 0x00 || d[23] != 17)
		return;
	int ihl = (d[14] & 15) * 4, udp = 14 + ihl, sport = (d[udp] << 8) | d[udp + 1];
	if (sport == 67 && len >= udp + 8 + 20)
		write_log(_T("netprobe: DHCP offer of %d.%d.%d.%d\n"), d[udp + 24], d[udp + 25],
		          d[udp + 26], d[udp + 27]);
	else if (sport == 53 && len >= udp + 8 + 12)
		write_log(_T("netprobe: DNS answer, %d answer(s), code %d\n"),
		          (d[udp + 14] << 8) | d[udp + 15], d[udp + 11] & 15);
}

static int probe_get(void *dev, uae_u8 *d, int *len)
{
	(void) dev;
	if (probe_len == 0 || probe_len > *len)
		return 0;
	memcpy(d, probe_frame, probe_len);
	*len = probe_len;
	probe_len = 0;
	return 1;
}

/* An Ethernet frame of a UDP datagram from SRC:SPORT to DST:DPORT. */
static void probe_udp(const uae_u8 *dst_mac, uae_u32 src, int sport, uae_u32 dst, int dport,
                      const uae_u8 *payload, int n)
{
	uae_u8 *f = probe_frame, *ip = f + 14, *udp = ip + 20;
	unsigned sum = 0;
	memset(f, 0, sizeof probe_frame);
	memcpy(f, dst_mac, 6);
	memcpy(f + 6, probe_mac, 6);
	f[12] = 0x08;
	ip[0] = 0x45;
	ip[2] = (20 + 8 + n) >> 8, ip[3] = (20 + 8 + n) & 255;
	ip[8] = 64, ip[9] = 17;
	for (int i = 0; i < 4; i++)
		ip[12 + i] = src >> (24 - 8 * i), ip[16 + i] = dst >> (24 - 8 * i);
	for (int i = 0; i < 20; i += 2)
		sum += (ip[i] << 8) | ip[i + 1];
	sum = (sum & 0xffff) + (sum >> 16);
	sum = ~((sum & 0xffff) + (sum >> 16)) & 0xffff;
	ip[10] = sum >> 8, ip[11] = sum & 255;
	udp[0] = sport >> 8, udp[1] = sport & 255, udp[2] = dport >> 8, udp[3] = dport & 255;
	udp[4] = (8 + n) >> 8, udp[5] = (8 + n) & 255;
	memcpy(udp + 8, payload, n);
	probe_len = 14 + 20 + 8 + n;
}

void bmamiga_core_net_probe(int step)
{
	static const uae_u8 broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	static const uae_u8 slirp_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x35, 0x02};
	uae_u8 p[300];
	if (probe_ndd == nullptr) {
		probe_ndd = ethernet_find_driver(_T("slirp"));
		if (probe_ndd == nullptr)
			return;
		probe_data = xcalloc(uae_u8, ethernet_getdatalength(probe_ndd));
		if (!ethernet_open(probe_ndd, probe_data, nullptr, probe_got, probe_get, 0, probe_mac)) {
			write_log(_T("netprobe: SLiRP didn't open\n"));
			probe_ndd = nullptr;
			return;
		}
	}
	memset(p, 0, sizeof p);
	if (step == 0) {
		/* BOOTP request, DHCP DISCOVER */
		p[0] = 1, p[1] = 1, p[2] = 6, p[4] = 0x12, p[5] = 0x34, p[10] = 0x80;
		memcpy(p + 28, probe_mac, 6);
		p[236] = 99, p[237] = 130, p[238] = 83, p[239] = 99;
		p[240] = 53, p[241] = 1, p[242] = 1, p[243] = 255;
		probe_udp(broadcast, 0, 68, 0xffffffff, 67, p, 244);
	} else {
		/* example.com, A, from 10.0.2.15 to SLiRP's DNS, 10.0.2.3 */
		static const uae_u8 q[] = {0x42, 0x42, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
		                           7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1};
		memcpy(p, q, sizeof q);
		probe_udp(slirp_mac, 0x0a00020f, 40000, 0x0a000203, 53, p, sizeof q);
	}
	ethernet_trigger(probe_ndd, probe_data);
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
