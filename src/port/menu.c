/*
 * menu.c - the BMAmiga menu.
 *
 * F12, a gamepad's Home button or Select+Start open it: floppy disks, the
 * Amiga model, resets and warp, and BMAmiga's own settings (video, sound,
 * network, power). Settings are saved in bmamiga.cfg and used again at the
 * next boot.
 *
 * From BMCoco's and BMAmiga's menu.c.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../host.h"
#include "bmamiga.h"
#include "osd.h"

#ifndef BMAMIGA_VERSION
#define BMAMIGA_VERSION "dev"
#endif

#define WPA_CONFIG "wpa_supplicant.conf"

/* Something changed that only takes effect after a reboot. */
static int reboot_needed;

/* ---- Settings ---------------------------------------------------------- */

static void save_setting(const char *name, const char *value)
{
	if (!host_option_set(name, value) || !host_options_save())
		osd_message("Cannot save bmamiga.cfg", TRUE);
}

static void save_setting_int(const char *name, int value)
{
	char text[16];
	snprintf(text, sizeof(text), "%d", value);
	save_setting(name, text);
}

static int setting_on(const char *name, int default_value)
{
	const char *value = host_option(name, NULL);
	if (value == NULL || *value == '\0')
		return default_value;
	return strcmp(value, "1") == 0 || strcmp(value, "yes") == 0
	    || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
}

static unsigned check(int on)
{
	return OSD_ITEM_CHECK | (on ? OSD_ITEM_CHECKED : 0);
}

static void power(int off)
{
	bmamiga_request_power(off);
	bmamiga_idle();
}

/* ---- Video -------------------------------------------------------------- */

static const char *vsync_mode(void)
{
	const char *mode = host_option("vsync", "auto");
	if (strcmp(mode, "on") == 0 || strcmp(mode, "1") == 0)
		return "On";
	if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0)
		return "Off";
	return bmamiga_vsync_locked() ? "Auto (locked)" : "Auto (timer)";
}

static void video_menu(void)
{
	enum { V_VSYNC, V_RESOLUTION, V_SHADER, V_CURVATURE, V_SMOOTH };
	/* The Pi 5 only: its firmware ignores hdmi_mode (from BMA800XL). */
	static const char *const resolutions[] = {"auto", "1920x1080", "1280x720", NULL};
	static const char *const resolution_names[] = {"Auto", "1920x1080", "1280x720", NULL};
	int sel = 0;
	for (;;) {
		const char *res = host_option("resolution", "auto");
		int res_index = 0, n_items, k;
		for (k = 0; resolutions[k] != NULL; k++)
			if (strcmp(res, resolutions[k]) == 0)
				res_index = k;
		struct osd_item items[] = {
			{"Frame Lock", vsync_mode(), OSD_ITEM_SUBMENU, V_VSYNC},
			{"Resolution", resolution_names[res_index], OSD_ITEM_SUBMENU, V_RESOLUTION},
			{"CRT Effect (reboot)", NULL, check(setting_on("crt_shader", FALSE)), V_SHADER},
			{"CRT Curvature (reboot)", NULL, check(setting_on("crt_curvature", FALSE)), V_CURVATURE},
			{"Smooth Scaling (reboot)", NULL, check(setting_on("smooth", FALSE)), V_SMOOTH},
		};
		int chosen, i;
		n_items = sizeof(items) / sizeof(items[0]);
		if (!host_display_resolution_choice()) {
			/* Resolution is only for the Pi 5 (with video_driver=kms). */
			for (k = 1; k < n_items - 1; k++)
				items[k] = items[k + 1];
			n_items--;
		}
		chosen = osd_menu("Video", items, n_items, &sel);
		switch (chosen) {
		case V_RESOLUTION:
			i = osd_choose("Resolution", resolution_names, res_index);
			if (i >= 0 && i != res_index) {
				save_setting("resolution", resolutions[i]);
				bmamiga_display_mode_changed();
			}
			break;
		case V_CURVATURE:
			save_setting("crt_curvature", setting_on("crt_curvature", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case V_VSYNC: {
			static const char *const choices[] = {
				"Automatic", "On: follow the display", "Off: keep exact time", NULL
			};
			static const char *const values[] = {"auto", "on", "off"};
			const char *mode = host_option("vsync", "auto");
			int current = strcmp(mode, "on") == 0 ? 1 : strcmp(mode, "off") == 0 ? 2 : 0;
			i = osd_choose("Frame Lock", choices, current);
			if (i >= 0) {
				save_setting("vsync", values[i]);
				bmamiga_video_settings_changed();
			}
			break;
		}
		case V_SHADER:
			save_setting("crt_shader", setting_on("crt_shader", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case V_SMOOTH:
			save_setting("smooth", setting_on("smooth", FALSE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		default:
			return;
		}
	}
}

/* ---- Sound ---------------------------------------------------------------- */

static void sound_menu(void)
{
	enum { S_VOLUME, S_OUTPUT };
	static const char *const volumes[] = {
		"100%", "90%", "80%", "70%", "60%", "50%", "40%", "30%", "20%", "10%", "Off", NULL
	};
	static const char *const outputs[] = {"auto", "hdmi", "analog"};
	static const char *const output_names[] = {"Automatic", "HDMI", "Headphone jack", NULL};
	int sel = 0;
	for (;;) {
		const char *out = host_option("audio_out", "auto");
		int out_index = strcmp(out, "hdmi") == 0 ? 1
		                : strcmp(out, "analog") == 0 || strcmp(out, "headphones") == 0 ? 2 : 0;
		char volume[8];
		struct osd_item items[] = {
			{"Volume", volume, OSD_ITEM_SUBMENU, S_VOLUME},
			{"Output (reboot)", output_names[out_index], OSD_ITEM_SUBMENU, S_OUTPUT},
		};
		int chosen, i;
		if (bmamiga_volume() == 0)
			snprintf(volume, sizeof volume, "Off");
		else
			snprintf(volume, sizeof volume, "%d%%", bmamiga_volume());
		chosen = osd_menu("Sound", items, sizeof(items) / sizeof(items[0]), &sel);
		if (chosen == S_VOLUME) {
			i = osd_choose("Volume", volumes, (100 - bmamiga_volume() + 5) / 10);
			if (i >= 0)
				bmamiga_set_volume(100 - i * 10);
			continue;
		}
		if (chosen != S_OUTPUT)
			return;
		i = osd_choose("Sound Output", output_names, out_index);
		if (i >= 0 && i != out_index) {
			save_setting("audio_out", outputs[i]);
			reboot_needed = TRUE;
		}
	}
}

/* ---- Network --------------------------------------------------------------- */

static char wifi_ssid[33], wifi_psk[64], wifi_country[3];

static void wpa_path(char *path, size_t size)
{
	const char *root = bmamiga_sd_root();
	size_t len = strlen(root);
	snprintf(path, size, "%s%s%s", root, len > 0 && root[len - 1] == '/' ? "" : "/", WPA_CONFIG);
}

static void copy_quoted(char *dst, size_t size, const char *src)
{
	const char *end;
	if (*src == '"')
		src++;
	end = strchr(src, '"');
	if (end == NULL)
		end = src + strcspn(src, "\r\n");
	if ((size_t) (end - src) >= size)
		end = src + size - 1;
	memcpy(dst, src, end - src);
	dst[end - src] = '\0';
}

static void read_wifi_config(void)
{
	char path[256], line[160];
	FILE *f;
	wifi_ssid[0] = wifi_psk[0] = '\0';
	strcpy(wifi_country, "US");
	wpa_path(path, sizeof(path));
	f = fopen(path, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *p = line + strspn(line, " \t");
		if (strncmp(p, "country=", 8) == 0)
			copy_quoted(wifi_country, sizeof(wifi_country), p + 8);
		else if (strncmp(p, "ssid=", 5) == 0 && wifi_ssid[0] == '\0')
			copy_quoted(wifi_ssid, sizeof(wifi_ssid), p + 5);
		else if (strncmp(p, "psk=", 4) == 0 && wifi_psk[0] == '\0')
			copy_quoted(wifi_psk, sizeof(wifi_psk), p + 4);
	}
	fclose(f);
}

static int write_wifi_config(void)
{
	char path[256];
	FILE *f;
	wpa_path(path, sizeof(path));
	f = fopen(path, "w");
	if (f == NULL)
		return FALSE;
	fprintf(f, "country=%s\n\nnetwork={\n\tssid=\"%s\"\n", wifi_country, wifi_ssid);
	if (wifi_psk[0] != '\0')
		fprintf(f, "\tpsk=\"%s\"\n\tkey_mgmt=WPA-PSK\n", wifi_psk);
	else
		fprintf(f, "\tkey_mgmt=NONE\n");
	fprintf(f, "}\n");
	return fclose(f) == 0;
}

static void edit_wifi(const char *title, char *value, size_t size, int secret)
{
	char text[64];
	snprintf(text, sizeof(text), "%s", value);
	if (osd_edit(title, text, size < sizeof(text) ? size : sizeof(text), secret)) {
		snprintf(value, size, "%s", text);
		if (write_wifi_config())
			reboot_needed = TRUE;
		else
			osd_message("Cannot write " WPA_CONFIG, TRUE);
	}
}

static int compare_signal(const void *a, const void *b)
{
	return ((const struct host_wifi_ap *) b)->signal - ((const struct host_wifi_ap *) a)->signal;
}

/* Returns 1 if a network was chosen, 2 if it also needs a password. */
static int scan_wifi(void)
{
	enum { MAX_APS = 20 };
	static struct host_wifi_ap aps[MAX_APS];
	static char signal[MAX_APS][16];
	struct osd_item items[MAX_APS];
	int n, i, sel = 0, chosen;

	osd_message("Scanning for Wi-Fi networks...", FALSE);
	n = host_wifi_scan(aps, MAX_APS);
	if (n < 0) {
		if (osd_confirm("Wi-Fi is Off", "Switch to Wi-Fi and reboot?")) {
			save_setting("network", "wifi");
			power(0);
		}
		return 0;
	}
	if (n == 0) {
		osd_message("No Wi-Fi networks found.", TRUE);
		return 0;
	}
	qsort(aps, n, sizeof(aps[0]), compare_signal);
	for (i = 0; i < n; i++) {
		snprintf(signal[i], sizeof(signal[i]), "%s%d", aps[i].secure ? "* " : "", aps[i].signal);
		items[i] = (struct osd_item){aps[i].ssid, signal[i], 0, i};
	}
	chosen = osd_menu("Choose a Wi-Fi Network", items, n, &sel);
	if (chosen < 0)
		return 0;
	if (!aps[chosen].secure || strcmp(wifi_ssid, aps[chosen].ssid) != 0)
		wifi_psk[0] = '\0';
	snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", aps[chosen].ssid);
	if (!write_wifi_config()) {
		osd_message("Cannot write " WPA_CONFIG, TRUE);
		return 0;
	}
	save_setting("network", "wifi");
	reboot_needed = TRUE;
	return aps[chosen].secure && wifi_psk[0] == '\0' ? 2 : 1;
}

static void network_menu(void)
{
	enum { N_DEVICE, N_SCAN, N_SSID, N_PASSWORD, N_COUNTRY, N_WEBUI, N_PIN, N_TIMEZONE, N_REBOOT };
	static const char *const devices[] = {"off", "ethernet", "wifi", NULL};
	static const char *const device_names[] = {"Off", "Ethernet", "Wi-Fi", NULL};
	int sel = 2;
	for (;;) {
		char status[64], tz[16];
		const char *net = host_option("network", "off");
		const char *pin = host_option("webui_pin", "");
		int device = strcmp(net, "ethernet") == 0 ? 1 : strcmp(net, "wifi") == 0 ? 2 : 0;
		int minutes = host_option_int("timezone", 0);
		int chosen, i;

		read_wifi_config();
		host_network_status(status, sizeof(status));
		if (minutes == 0)
			strcpy(tz, "UTC");
		else
			snprintf(tz, sizeof(tz), "UTC%+d", minutes / 60);
		{
			struct osd_item items[] = {
				{status, NULL, OSD_ITEM_LABEL, -1},
				{"", NULL, OSD_ITEM_LABEL, -1},
				{"Network", device_names[device], OSD_ITEM_SUBMENU, N_DEVICE},
				{"Scan for Wi-Fi Networks", NULL, OSD_ITEM_SUBMENU, N_SCAN},
				{"Wi-Fi Network", wifi_ssid[0] ? wifi_ssid : "(none)", OSD_ITEM_SUBMENU, N_SSID},
				{"Wi-Fi Password", wifi_psk[0] ? "********" : "(none)", OSD_ITEM_SUBMENU, N_PASSWORD},
				{"Wi-Fi Country", wifi_country, OSD_ITEM_SUBMENU, N_COUNTRY},
				{"Web UI", NULL, check(setting_on("webui", TRUE)), N_WEBUI},
				{"Web UI PIN", pin[0] ? pin : "(none)", OSD_ITEM_SUBMENU, N_PIN},
				{"Time Zone", tz, OSD_ITEM_SUBMENU, N_TIMEZONE},
				{"", NULL, OSD_ITEM_LABEL, -1},
				{"Changes apply after a reboot.", NULL, OSD_ITEM_LABEL, -1},
				{"Reboot Now", NULL, 0, N_REBOOT},
			};
			chosen = osd_menu("Network", items, sizeof(items) / sizeof(items[0]), &sel);
		}
		switch (chosen) {
		case N_DEVICE:
			i = osd_choose("Network", device_names, device);
			if (i >= 0 && i != device) {
				save_setting("network", devices[i]);
				reboot_needed = TRUE;
			}
			break;
		case N_SCAN:
			if (scan_wifi() == 2) {
				wifi_psk[0] = '\0';
				edit_wifi("Wi-Fi Password", wifi_psk, sizeof(wifi_psk), TRUE);
			}
			break;
		case N_SSID:
			edit_wifi("Wi-Fi Network Name", wifi_ssid, sizeof(wifi_ssid), FALSE);
			break;
		case N_PASSWORD:
			wifi_psk[0] = '\0';
			edit_wifi("Wi-Fi Password (blank: none)", wifi_psk, sizeof(wifi_psk), TRUE);
			break;
		case N_COUNTRY:
			edit_wifi("Two Letter Country Code", wifi_country, sizeof(wifi_country), FALSE);
			break;
		case N_WEBUI:
			save_setting("webui", setting_on("webui", TRUE) ? "0" : "1");
			reboot_needed = TRUE;
			break;
		case N_PIN: {
			char text[9];
			snprintf(text, sizeof(text), "%s", pin);
			if (osd_edit("Web UI PIN (blank: none)", text, sizeof(text), FALSE)) {
				save_setting("webui_pin", text);
				reboot_needed = TRUE;
			}
			break;
		}
		case N_TIMEZONE: {
			static char labels[27][8];
			static const char *zones[28];
			int h, current = 12;
			for (h = -12; h <= 14; h++) {
				if (h == 0)
					strcpy(labels[h + 12], "UTC");
				else
					snprintf(labels[h + 12], sizeof(labels[0]), "UTC%+d", h);
				zones[h + 12] = labels[h + 12];
				if (h * 60 == minutes)
					current = h + 12;
			}
			zones[27] = NULL;
			h = osd_choose("Time Zone", zones, current);
			if (h >= 0 && h != current) {
				save_setting_int("timezone", (h - 12) * 60);
				reboot_needed = TRUE;
			}
			break;
		}
		case N_REBOOT:
			power(0);
			return;
		default:
			return;
		}
	}
}

/* ---- Information ----------------------------------------------------------- */

/* ---- About ---------------------------------------------------------------- */

static void about(void)
{
	char text[640], status[64];
	int w, h, avg, max;
	host_display_size(&w, &h);
	host_network_status(status, sizeof(status));
	bmamiga_load(&avg, &max);
	snprintf(text, sizeof(text),
	         "BMAmiga " BMAMIGA_VERSION "\n"
	         "Bare metal Amiga emulator\n"
	         "for the Raspberry Pi.\n\n"
	         "Emulation: Amiberry (GPL 3),\n"
	         "from WinUAE.\n"
	         "Based on BMCoco, BMA800XL and BMC64.\n\n"
	         "Model: %s\n"
	         "Display: %dx%d %.2f Hz\n"
	         "Frames: %.2f Hz, %s\n"
	         "Load: %d%% (at most %d%%)\n"
	         "Network: %s",
	         bmamiga_machine_description(), w, h, bmamiga_refresh_rate(),
	         bmamiga_machine_rate(),
	         bmamiga_warp() ? "warp" : bmamiga_overloaded() ? "Pi too slow: timer paced"
	         : bmamiga_vsync_locked() ? "locked to display" : "timer paced",
	         avg, max, status);
	osd_info("About BMAmiga", text);
}

static void offer_reboot(void)
{
	if (osd_confirm("Reboot?", "Settings changed that need a reboot. Reboot now?"))
		power(0);
}

/* ---- Floppy disks -------------------------------------------------------- */

static const char *const floppy_exts[] = {"adf", "adz", "dms", "ipf", "zip", NULL};

static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash != NULL ? slash + 1 : path;
}

/* The folder the file browser starts in: the disks folder, or where the
   last disk came from. */
static char browse_dir[512];

static void start_browsing(void)
{
	if (browse_dir[0] == '\0') {
		const char *root = bmamiga_sd_root();
		size_t len = strlen(root);
		snprintf(browse_dir, sizeof browse_dir, "%s%sdisks", root,
		         len > 0 && root[len - 1] == '/' ? "" : "/");
	}
}

static int browse(const char *title, char *path, size_t size, const char *const exts[])
{
	start_browsing();
	return osd_browse(title, browse_dir, sizeof browse_dir, exts, path, size);
}

static int browse_folder(const char *title, char *path, size_t size)
{
	start_browsing();
	return osd_browse_dir(title, browse_dir, sizeof browse_dir, path, size);
}

static void floppy_menu(void)
{
	static char names[BMAMIGA_FLOPPIES][48];
	static const char *const labels[BMAMIGA_FLOPPIES] = {"DF0:", "DF1:", "DF2:", "DF3:"};
	static const char *const choices[] = {"Insert Disk...", "Eject", NULL};
	int sel = 0;
	for (;;) {
		struct osd_item items[BMAMIGA_FLOPPIES + 3];
		int n = 0, d, chosen;
		char path[512];
		for (d = 0; d < BMAMIGA_FLOPPIES; d++) {
			const char *disk = bmamiga_floppy(d);
			snprintf(names[d], sizeof names[d], "%s", disk ? base_name(disk) : "(empty)");
			items[n++] = (struct osd_item){labels[d], names[d], OSD_ITEM_SUBMENU, d};
		}
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"ADF, ADZ, DMS, IPF or ZIP.", NULL, OSD_ITEM_LABEL, -1};
		chosen = osd_menu("Floppy Drives", items, n, &sel);
		if (chosen < 0 || chosen >= BMAMIGA_FLOPPIES)
			return;
		d = chosen;
		if (bmamiga_floppy(d) != NULL) {
			int i = osd_choose(labels[d], choices, 0);
			if (i < 0)
				continue;
			if (i == 1) {
				bmamiga_eject_floppy(d);
				continue;
			}
		}
		if (browse(labels[d], path, sizeof path, floppy_exts)) {
			if (!bmamiga_insert_floppy(d, path))
				osd_message("That disk can't be used.", TRUE);
			else if (d == 0)
				return;   /* back to the Amiga, which reads it */
		}
	}
}

/* ---- Hard drives and the CD ------------------------------------------------ */

static const char *const hdf_exts[] = {"hdf", "hdz", NULL};
static const char *const cd_exts[] = {"iso", "cue", "ccd", "nrg", "mds", "chd", NULL};

/* After a change the Amiga needs: a hard reset now, if wanted. Returns
   TRUE if it was reset (back to the Amiga). */
static int offer_hard_reset(void)
{
	if (!osd_confirm("Hard Reset", "Hard reset the Amiga now to use it?"))
		return FALSE;
	bmamiga_hard_reset();
	return TRUE;
}

/* DH0: to DH3: (a hard disk image, or a folder on the card, which the
   Amiga sees as a volume of its name) and the CD drive. */
static int hard_drive_menu(void)
{
	static char names[BMAMIGA_HARD_DRIVES + 1][48];
	static const char *const labels[BMAMIGA_HARD_DRIVES] = {"DH0:", "DH1:", "DH2:", "DH3:"};
	enum { H_CD = BMAMIGA_HARD_DRIVES };
	int sel = 0;
	for (;;) {
		struct osd_item items[BMAMIGA_HARD_DRIVES + 6];
		int n = 0, d, chosen, i;
		char path[512];
		const char *cd = bmamiga_cd();
		for (d = 0; d < BMAMIGA_HARD_DRIVES; d++) {
			const char *drive = bmamiga_hard_drive(d);
			struct stat st;
			if (drive == NULL)
				snprintf(names[d], sizeof names[d], "(none)");
			else if (stat(drive, &st) == 0 && S_ISDIR(st.st_mode))
				snprintf(names[d], sizeof names[d], "%s/", base_name(drive));
			else
				snprintf(names[d], sizeof names[d], "%s", base_name(drive));
			items[n++] = (struct osd_item){labels[d], names[d], OSD_ITEM_SUBMENU, d};
		}
		snprintf(names[H_CD], sizeof names[H_CD], "%s", cd ? base_name(cd) : "(empty)");
		items[n++] = (struct osd_item){"CD", names[H_CD], OSD_ITEM_SUBMENU, H_CD};
		items[n++] = (struct osd_item){"", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"A hard drive is an HDF image or a", NULL, OSD_ITEM_LABEL, -1};
		items[n++] = (struct osd_item){"folder; it is used from a Hard Reset.", NULL, OSD_ITEM_LABEL, -1};
		chosen = osd_menu("Hard Drives and CD", items, n, &sel);
		if (chosen < 0)
			return FALSE;
		if (chosen == H_CD) {
			static const char *const cd_choices[] = {"Insert CD Image...", "Eject", NULL};
			int was_empty = cd == NULL;
			i = cd ? osd_choose("CD", cd_choices, 0) : 0;
			if (i < 0)
				continue;
			if (i == 1) {
				bmamiga_set_cd(NULL);
				continue;
			}
			if (!browse("CD", path, sizeof path, cd_exts))
				continue;
			bmamiga_set_cd(path);
			/* A CD32's or CDTV's drive takes it at once; the other Amigas'
			   CD drive (uaescsi.device) is there from a hard reset, after
			   which discs can be changed as the Amiga runs. */
			if (!bmamiga_model_has_cd() && was_empty && offer_hard_reset())
				return TRUE;
			continue;
		}
		d = chosen;
		{
			static const char *const choices[] = {"Hard Disk Image...", "Folder...", "Remove", NULL};
			static const char *const new_choices[] = {"Hard Disk Image...", "Folder...", NULL};
			i = osd_choose(labels[d], bmamiga_hard_drive(d) ? choices : new_choices, 0);
		}
		if (i < 0)
			continue;
		if (i == 2)
			bmamiga_set_hard_drive(d, NULL);
		else if (i == 0 ? browse(labels[d], path, sizeof path, hdf_exts)
		                : browse_folder(labels[d], path, sizeof path))
			bmamiga_set_hard_drive(d, path);
		else
			continue;
		if (offer_hard_reset())
			return TRUE;
	}
}

/* ---- Model ------------------------------------------------------------------ */

static void model_menu(void)
{
	const char *names[32];
	int m, n = bmamiga_models();
	for (m = 0; m < n && m < 31; m++)
		names[m] = bmamiga_model_name(m);
	names[m] = NULL;
	m = osd_choose("Model", names, bmamiga_model());
	if (m < 0 || m == bmamiga_model())
		return;
	bmamiga_set_model(m);
	reboot_needed = TRUE;
}

/* ---- Main menu ---------------------------------------------------------------- */

void bmamiga_menu(void)
{
	enum { M_FLOPPY, M_HARD_DRIVES, M_MODEL, M_WARP, M_RESET, M_HARD_RESET, M_VIDEO, M_SOUND, M_NETWORK,
	       M_ABOUT, M_REBOOT, M_OFF };
	static int sel = 0;
	char title[48];

	snprintf(title, sizeof(title), "BMAmiga %s", BMAMIGA_VERSION);
	reboot_needed = FALSE;
	osd_open();
	for (;;) {
		struct osd_item items[] = {
			{"Floppy Drives", NULL, OSD_ITEM_SUBMENU, M_FLOPPY},
			{"Hard Drives and CD", NULL, OSD_ITEM_SUBMENU, M_HARD_DRIVES},
			{"Model (reboot)", bmamiga_model_id(bmamiga_model()), OSD_ITEM_SUBMENU, M_MODEL},
			{"Warp (F11)", NULL, check(bmamiga_warp()), M_WARP},
			{"Reset", NULL, 0, M_RESET},
			{"Hard Reset", NULL, 0, M_HARD_RESET},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Video", NULL, OSD_ITEM_SUBMENU, M_VIDEO},
			{"Sound", NULL, OSD_ITEM_SUBMENU, M_SOUND},
			{"Network", NULL, OSD_ITEM_SUBMENU, M_NETWORK},
			{"About", NULL, OSD_ITEM_SUBMENU, M_ABOUT},
			{"", NULL, OSD_ITEM_LABEL, -1},
			{"Reboot", NULL, 0, M_REBOOT},
			{"Shut Down", NULL, 0, M_OFF},
		};
		int chosen = osd_menu(title, items, sizeof(items) / sizeof(items[0]), &sel);
		int leave = FALSE;

		switch (chosen) {
		case M_FLOPPY:
			floppy_menu();
			leave = TRUE;
			break;
		case M_HARD_DRIVES:
			leave = hard_drive_menu();
			break;
		case M_MODEL: model_menu(); break;
		case M_WARP: bmamiga_set_warp(!bmamiga_warp()); leave = TRUE; break;
		case M_RESET: bmamiga_reset(); leave = TRUE; break;
		case M_HARD_RESET: bmamiga_hard_reset(); leave = TRUE; break;
		case M_VIDEO: video_menu(); break;
		case M_SOUND: sound_menu(); break;
		case M_NETWORK: network_menu(); break;
		case M_ABOUT: about(); break;
		case M_REBOOT:
			if (osd_confirm("Reboot", "Reboot BMAmiga?"))
				power(0);
			break;
		case M_OFF:
			if (osd_confirm("Shut Down", "Shut down? It's then safe to switch off."))
				power(1);
			break;
		default:
			leave = TRUE;
			break;
		}
		if (leave || osd_closing())
			break;
	}
	if (reboot_needed && !osd_closing())
		offer_reboot();
	osd_close();
}
