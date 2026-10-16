/*
 * bmamiga.h - shared declarations for the BMAmiga port of Amiberry.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMAMIGA_PORT_H_
#define BMAMIGA_PORT_H_

#include <stdint.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* USB HID usage codes (keyboard page) that BMAmiga itself uses. */
enum {
	USAGE_A = 0x04,
	USAGE_RETURN = 0x28, USAGE_ESCAPE = 0x29, USAGE_BACKSPACE = 0x2a,
	USAGE_TAB = 0x2b, USAGE_SPACE = 0x2c,
	USAGE_F1 = 0x3a, USAGE_F10 = 0x43, USAGE_F11 = 0x44, USAGE_F12 = 0x45,
	USAGE_PAUSE = 0x48, USAGE_HOME = 0x4a, USAGE_PAGEUP = 0x4b,
	USAGE_DELETE = 0x4c, USAGE_END = 0x4d, USAGE_PAGEDOWN = 0x4e,
	USAGE_RIGHT = 0x4f, USAGE_LEFT = 0x50, USAGE_DOWN = 0x51, USAGE_UP = 0x52,
	USAGE_KP_ENTER = 0x58, USAGE_POWER = 0x66,
	USAGE_LCTRL = 0xe0, USAGE_LSHIFT = 0xe1, USAGE_LALT = 0xe2,
	USAGE_RCTRL = 0xe4, USAGE_RSHIFT = 0xe5, USAGE_RALT = 0xe6
};

/* The Amiga has F1-F10 (and Help, Del); F11 and F12 are free. */
#define KEY_MENU   USAGE_F12
#define KEY_WARP   USAGE_F11

/* platform.c */
void bmamiga_main(void);
const char *bmamiga_sd_root(void);
/* The frame buffer, for the on-screen display. */
uint16_t *bmamiga_framebuffer(int *pitch_px);
/* Shows the on-screen display's 640x480 area (TRUE) or the Amiga's
   picture (FALSE). */
void bmamiga_osd_view(int on);
/* Called while a menu waits: keeps sound fed and runs network requests. */
void bmamiga_idle(void);
double bmamiga_refresh_rate(void);
double bmamiga_machine_rate(void);
int bmamiga_vsync_locked(void);
int bmamiga_overloaded(void);
void bmamiga_load(int *average, int *maximum);
void bmamiga_video_settings_changed(void);
/* The display's resolution setting changed (the Pi 5). */
void bmamiga_display_mode_changed(void);
/* Warp: the Amiga as fast as the Pi can run it (F11). */
int bmamiga_warp(void);
void bmamiga_set_warp(int on);
/* The models, as Amiberry's libretro core names them, for people, and
   choosing one (the Amiga starts again; kept in bmamiga.cfg). */
int bmamiga_models(void);
const char *bmamiga_model_id(int model);
const char *bmamiga_model_name(int model);
int bmamiga_model(void);
void bmamiga_set_model(int model);
/* The machine running, for people and the web UI, or "". */
const char *bmamiga_machine_description(void);
/* Floppy drives DF0: to DF3: (kept in bmamiga.cfg as df0 to df3). */
#define BMAMIGA_FLOPPIES 4
const char *bmamiga_floppy(int drive);
int bmamiga_insert_floppy(int drive, const char *path);
void bmamiga_eject_floppy(int drive);
/* Resets: the reset key combination (Ctrl+Amiga+Amiga), or switching the
   Amiga off and on. */
void bmamiga_reset(void);
void bmamiga_hard_reset(void);
/* The sound volume, 0-100 (kept in bmamiga.cfg). */
int bmamiga_volume(void);
void bmamiga_set_volume(int volume);
/* Reboot or power off, closing files first. */
void bmamiga_request_power(int power_off);
/* Starts the Amiga again (the web UI). */
void bmamiga_request_coldstart(void);
/* Web UI requests waiting to be done; menus close for them. */
int bmamiga_pending_requests(void);

/* keys.c: USB HID usage to libretro key code (RETROK_*), or 0. */
unsigned bmamiga_usage_to_retrok(int usage);

/* menu.c */
void bmamiga_menu(void);

#endif /* BMAMIGA_PORT_H_ */
