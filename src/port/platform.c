/*
 * platform.c - the libretro host that runs Amiberry for BMAmiga.
 *
 * Amiberry is built as its libretro core (third_party/amiberry/libretro)
 * and linked in, as a static library. This file is the "frontend" the
 * core talks to, on top of the Circle kernel services in src/host.h: it
 * answers the core's questions (where the ROMs are, the model, ...), takes
 * its pictures and sound, gives it the keyboard, mouse and joysticks, and
 * runs it a frame at a time (retro_run). The menu is in menu.c and osd.c;
 * disks, resets and warp go to Amiberry directly (bridge.cpp).
 *
 * Kickstart ROMs are in the SD card's roms folder (the core's "system
 * directory"): kick13.rom, kick31.rom and so on, or Amiga Forever's
 * amiga-os-*.rom files with their rom.key, which Amiberry recognises by
 * their contents. Without any, the Amiga starts with AROS, the free
 * replacement Kickstart Amiberry comes with (roms/aros-rom.bin and
 * aros-ext.bin).
 *
 * Timing: when the display refreshes at (close to) the Amiga's frame rate
 * (50 Hz for a PAL Amiga), every frame is shown on its own display refresh
 * and paced by it: Amiberry's internal vsync is on (the core option
 * amiberry_internal_vsync), with which it shows each frame and goes on at
 * the next retro_run() without a wait of its own. Otherwise Amiberry paces
 * itself by the clock (clock.c); locked but behind the display, BMAmiga
 * does (pace_by_clock()). Either way the sound is kept in step by dropping or
 * repeating a sample now and then.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "libretro.h"
#include "amiberry_embed.h"

#include "../host.h"
#include "bmamiga.h"
#include "osd.h"

#ifndef BMAMIGA_VERSION
#define BMAMIGA_VERSION "dev"
#endif

/* bridge.cpp */
int bmamiga_core_insert_floppy(int drive, const char *path);
void bmamiga_core_eject_floppy(int drive);
void bmamiga_core_set_floppy_speed(int speed);
uint32_t bmamiga_core_peek_long(uint32_t addr);
/* circle_fatfs_sectorcache_patch.diff's counters (none in the simulator). */
void fatfs_disk_stats(unsigned *reads, unsigned *hits, unsigned long long *read_us, unsigned *writes)
	__attribute__((weak));
int bmamiga_core_set_config(const char *lines);
int bmamiga_core_set_hard_drives(const char *const paths[4], const char *const names[4]);
void bmamiga_core_set_cd(const char *path, int builtin);
void bmamiga_core_cd32_pad(void);
void bma_threads_yield(void);   /* circle/threads.c */
void bmamiga_core_reset(int hard);
void bmamiga_core_warp(int on);
double bmamiga_core_frame_rate(void);
int bmamiga_core_rtg_on(void);
void bmamiga_core_sync_clock(void);
/* clock.c */
void bmamiga_clock_frame(int display_paces, double period);
extern double bmamiga_clock_slept;

#define LOG(...) do { printf("BMAmiga: " __VA_ARGS__); } while (0)

/* ---- Settings ---------------------------------------------------------------- */

const char *bmamiga_sd_root(void)
{
	return host_option("root", "/");
}

static int option_on(const char *name, int default_value)
{
	const char *value = host_option(name, NULL);
	if (value == NULL || *value == '\0')
		return default_value;
	return strcmp(value, "1") == 0 || strcmp(value, "on") == 0
	    || strcmp(value, "yes") == 0 || strcmp(value, "true") == 0;
}

static void save_setting(const char *name, const char *value)
{
	if (!host_option_set(name, value) || !host_options_save())
		LOG("cannot save %s= in bmamiga.cfg\n", name);
}

/* SD card folders: the root (root= in the simulator) and NAME. */
static void sd_path(char *out, size_t size, const char *name)
{
	const char *root = bmamiga_sd_root();
	size_t len = strlen(root);
	snprintf(out, size, "%s%s%s", root, len > 0 && root[len - 1] == '/' ? "" : "/", name);
}

/* ---- Models ----------------------------------------------------------------- */

/* The libretro core's amiberry_model values (libretro.cpp). */
static const struct {
	const char *id, *name;
} models[] = {
	{"A500", "Amiga 500 (Kickstart 1.3)"},
	{"A500OG", "Amiga 500 (Kickstart 1.2)"},
	{"A500+", "Amiga 500+ (Kickstart 2.04)"},
	{"A600", "Amiga 600 (Kickstart 2.05)"},
	{"A1200OG", "Amiga 1200, 2 MB"},
	{"A1200", "Amiga 1200, 2 MB + 8 MB Fast"},
	{"A4030", "Amiga 4000/030"},
	{"A4040", "Amiga 4000/040"},
	{"CD32", "CD32"},
	{"CDTV", "CDTV"},
};
#define N_MODELS ((int) (sizeof(models) / sizeof(models[0])))

int bmamiga_models(void) { return N_MODELS; }

const char *bmamiga_model_id(int model)
{
	return model >= 0 && model < N_MODELS ? models[model].id : "A500";
}

const char *bmamiga_model_name(int model)
{
	return model >= 0 && model < N_MODELS ? models[model].name : "";
}

int bmamiga_model(void)
{
	const char *id = host_option("model", "A500");
	int i;
	for (i = 0; i < N_MODELS; i++)
		if (strcmp(id, models[i].id) == 0)
			return i;
	return 0;
}

/* The core reads the model when the Amiga is started (retro_load_game), so
   a new one is kept for the next boot, which the menu offers. */
void bmamiga_set_model(int model)
{
	save_setting("model", bmamiga_model_id(model));
}

const char *bmamiga_machine_description(void)
{
	return bmamiga_model_name(bmamiga_model());
}

/* ---- Video -------------------------------------------------------------------- */

/* Big enough for the core's largest picture (its max_width/max_height). */
#define FB_W 1920
#define FB_H 1280

static uint16_t *fb_pixels;
static int fb_pitch_px;
static int pic_w = 640, pic_h = 480;   /* the core's last picture */
static double pic_aspect = 4.0 / 3.0;  /* its shape on a TV */
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static int osd_view;

static double refresh_rate = 50.0;     /* measured display refresh, Hz */
static double machine_rate = 50.0;     /* the Amiga's frame rate, Hz */
static int vsync_lock;
static int warp;

uint16_t *bmamiga_framebuffer(int *pitch_px)
{
	*pitch_px = fb_pitch_px;
	return fb_pixels;
}

static void apply_view(void)
{
	if (osd_view)
		host_video_set_view(0, 0, 640, 480, 4.0 / 3.0);
	else {
		/* The core gives 4:3 for every picture; an RTG card's screen
		   (1920x1080, 1280x720 ...) has square pixels, and squeezed to
		   4:3 it lost one column in four and its text was hard to read
		   (AmigaSYS, 2026-11-13). */
		double aspect = pic_aspect;
		if (bmamiga_core_rtg_on() && pic_w > 0 && pic_h > 0)
			aspect = (double) pic_w / pic_h;
		host_video_set_view(0, 0, pic_w, pic_h,
		                    aspect * host_option_double("aspect", 1.0));
	}
}

void bmamiga_osd_view(int on)
{
	osd_view = on;
	apply_view();
}

static void clear_framebuffer(void)
{
	int y;
	for (y = 0; y < FB_H; y++)
		memset(fb_pixels + y * fb_pitch_px, 0, FB_W * 2);
}

/* Times a few vertical blanks to find the display's refresh rate. */
static void measure_refresh_rate(void)
{
	enum { SKIP = 3, COUNT = 20 };
	double start = 0.0, rate;
	int i;
	for (i = 0; i < SKIP + COUNT; i++) {
		host_video_present(TRUE);
		if (i == SKIP - 1)
			start = host_time();
	}
	rate = COUNT / (host_time() - start);
	if (rate > 20.0 && rate < 250.0)
		refresh_rate = rate;
	LOG("display refresh rate %.3f Hz\n", refresh_rate);
}

static int options_dirty;

static void update_sync_mode(void)
{
	static int decided;
	const char *mode = host_option("vsync", "auto");
	int lock;
	if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0)
		lock = FALSE;
	else if (strcmp(mode, "on") == 0 || strcmp(mode, "1") == 0)
		lock = TRUE;
	else {
		/* 50 Hz for a PAL Amiga, 60 Hz for an NTSC one. */
		double ratio = machine_rate > 0.0 ? refresh_rate / machine_rate : 0.0;
		lock = ratio > 0.98 && ratio < 1.02;
	}
	if (lock != vsync_lock || !decided) {
		decided = TRUE;
		LOG("display %.2f Hz, machine %.2f Hz, %s\n", refresh_rate, machine_rate,
		    lock ? "synchronised to the display" : "timer paced");
	}
	if (lock != vsync_lock)
		options_dirty = TRUE;   /* amiberry_internal_vsync follows it */
	vsync_lock = lock;
}

/* The Amiga's frame rate changed (a new model, PAL or NTSC). */
static void take_frame_rate(double rate)
{
	if (rate < 20.0 || rate > 100.0 || (rate > machine_rate - 0.005 && rate < machine_rate + 0.005))
		return;
	machine_rate = rate;
	/* Where the host can set the video mode (the Pi 4 and 5), run the
	   display at the Amiga's rate, then time it. */
	if (host_display_match_rate(machine_rate))
		measure_refresh_rate();
	update_sync_mode();
}

/* Amiberry's frame rate, frame by frame (a PAL Amiga's frames are 312 or
   313 lines: 50.08 or 49.92 Hz), smoothed: a change of more than 0.05 Hz
   that lasts 100 frames is a new rate (the display's mode is then set
   again on a Pi 4 or 5, which the boot's mix of frames mustn't do). */
static void follow_frame_rate(void)
{
	static double avg;
	static int steady;
	double rate = bmamiga_core_frame_rate();
	if (rate < 20.0 || rate > 100.0)
		return;
	avg = avg == 0.0 ? rate : avg + (rate - avg) * 0.05;
	if (avg > machine_rate - 0.05 && avg < machine_rate + 0.05) {
		steady = 0;
		return;
	}
	if (++steady >= 100) {
		steady = 0;
		take_frame_rate(avg);
	}
}

/* The core's frame rate, rounded by the libretro API (Amiberry's own is
   taken once it runs, bmamiga_core_frame_rate()), and picture shape. */
static void take_av_info(const struct retro_system_av_info *av)
{
	take_frame_rate(av->timing.fps);
	if (av->geometry.aspect_ratio > 0.1f)
		pic_aspect = av->geometry.aspect_ratio;
}

double bmamiga_refresh_rate(void) { return refresh_rate; }
double bmamiga_machine_rate(void) { return machine_rate; }
int bmamiga_vsync_locked(void) { return vsync_lock; }

void bmamiga_video_settings_changed(void)
{
	update_sync_mode();
	apply_view();
}

static void open_sound(void);

void bmamiga_display_mode_changed(void)
{
	if (host_display_match_rate(machine_rate))
		measure_refresh_rate();
	update_sync_mode();
	open_sound();
	apply_view();
}

/* A picture from the core: into the frame buffer as RGB565. */
static void core_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
	unsigned x, y;
	if (data == NULL || osd_view)
		return;  /* the same picture again, or the menu is showing */
	if (w > FB_W)
		w = FB_W;
	if (h > FB_H)
		h = FB_H;
	for (y = 0; y < h; y++) {
		uint16_t *out = fb_pixels + y * fb_pitch_px;
		const uint8_t *row = (const uint8_t *) data + y * pitch;
		if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
			const uint32_t *in = (const uint32_t *) row;
			for (x = 0; x < w; x++) {
				uint32_t p = in[x];
				out[x] = (uint16_t) (((p >> 8) & 0xf800) | ((p >> 5) & 0x07e0) | ((p >> 3) & 0x001f));
			}
		} else if (pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
			memcpy(out, row, w * 2);
		} else {
			const uint16_t *in = (const uint16_t *) row;
			for (x = 0; x < w; x++) {
				uint16_t p = in[x];
				out[x] = (uint16_t) (((p << 1) & 0xffc0) | ((p >> 4) & 0x0020) | (p & 0x001f));
			}
		}
	}
	if ((int) w != pic_w || (int) h != pic_h) {
		pic_w = (int) w;
		pic_h = (int) h;
		apply_view();
	}
}

/* ---- Load -------------------------------------------------------------------- */

/* How long each frame takes to emulate and draw, against the display's
   refresh (as in BMCoco): locked to the display, a frame longer than one
   refresh would wait for the next and halve the speed, so then BMAmiga lets
   Amiberry pace itself by the clock, as fast as the Pi can, until frames
   fit again. */
static double frame_start;
static double frame_slept;   /* bmamiga_clock_slept at frame_start */
static double load_avg;
static int overloaded;
static double stats_start, stats_sum, stats_max;
static unsigned stats_frames, stats_late;

/* Locked to the display but behind it: the core doesn't wait (its internal
   vsync is on), so one frame an Amiga frame's time, by the clock, without
   waiting for the display; a frame late starts the count again. */
static void pace_by_clock(double now)
{
	static double next;
	double period = 1.0 / bmamiga_core_frame_rate();
	if (period <= 0.0 || period > 0.1)
		return;
	next += period;
	if (next < now - period || next > now + period)
		next = now;
	else if (next > now)
		host_sleep_us((unsigned) ((next - now) * 1e6));
}

static int display_paces(void)
{
	return vsync_lock && !overloaded && !warp;
}

int bmamiga_overloaded(void) { return overloaded; }

void bmamiga_load(int *average, int *maximum)
{
	*average = (int) (load_avg * 100.0 + 0.5);
	*maximum = (int) (stats_max * 100.0 + 0.5);
}

static void measure_load(double now)
{
	double period = 1.0 / refresh_rate;
	/* Amiberry's own waits, pacing itself by the clock, aren't work. */
	double work = now - frame_start - (bmamiga_clock_slept - frame_slept);
	double load;
	if (frame_start == 0.0 || work < 0.0 || work > 0.25)
		return;
	load = work / period;
	load_avg += (load - load_avg) * 0.05;
	stats_sum += load;
	if (load > stats_max)
		stats_max = load;
	stats_frames++;
	if (load > 1.0)
		stats_late++;
	if (vsync_lock && !overloaded && load_avg > 0.97) {
		overloaded = TRUE;
		LOG("frames take %.1f ms, longer than the display's %.1f ms: "
		    "timer paced until they fit\n", load_avg * period * 1000.0, period * 1000.0);
	} else if (overloaded && load_avg < 0.85) {
		overloaded = FALSE;
		LOG("frames fit the display again (%.1f ms), synchronised to it\n",
		    load_avg * period * 1000.0);
	}
	if (stats_start == 0.0)
		stats_start = now;
	if (now - stats_start >= 30.0 && stats_frames > 0) {
		LOG("load %.0f%% of a frame on average, %.0f%% at most, %u of %u frames late%s\n",
		    stats_sum * 100.0 / stats_frames, stats_max * 100.0, stats_late, stats_frames,
		    overloaded ? " (timer paced)" : "");
		stats_start = now;
		stats_sum = stats_max = 0.0;
		stats_frames = stats_late = 0;
	}
}

/* ---- Sound -------------------------------------------------------------------- */

static unsigned ao_rate = 44100;   /* the core's */
static unsigned ao_open;           /* the host's rate, 0 if there's no sound */
static unsigned ao_latency;        /* frames */
static int volume = -1;
static unsigned audio_this_frame;  /* frames the core gave this frame */
static const int16_t silence[2048];

int bmamiga_volume(void)
{
	if (volume < 0) {
		volume = host_option_int("volume", 100);
		if (volume < 0 || volume > 100)
			volume = 100;
	}
	return volume;
}

void bmamiga_set_volume(int v)
{
	char text[8];
	volume = v < 0 ? 0 : v > 100 ? 100 : v;
	snprintf(text, sizeof text, "%d", volume);
	save_setting("volume", text);
}

static void open_sound(void)
{
	unsigned ms = (unsigned) host_option_int("audio_buffer_ms", 60);
	if (ms < 20)
		ms = 20;
	if (ms > 500)
		ms = 500;
	ao_latency = ao_rate * ms / 1000;
	ao_open = host_sound_open(ao_rate, 2, ao_latency);
	if (ao_open == 0)
		LOG("no sound\n");
}

static void feed_silence(void);

/* Keeps the sound output fed while the emulation isn't running. */
static void feed_silence(void)
{
	unsigned frames;
	if (ao_open == 0)
		return;
	frames = host_sound_free_frames();
	if (frames > ao_latency / 2) {
		frames -= ao_latency / 2;
		while (frames > 0) {
			unsigned n = frames > 1024 ? 1024 : frames;
			host_sound_write(silence, n);
			frames -= n;
		}
	}
}

/* Sound from the core, stereo. The Amiga runs at the display's pace or a
   timer's, never quite the sound output's, so the buffer drifts. It is
   kept about half full by dynamic rate control (as RetroArch does): the
   sound is resampled all the time, by linear interpolation carried from
   batch to batch, at a rate a little over or under the core's, by up to
   0.5% (inaudible) as the buffer is under or over half full; 2% when it
   is nearly dry or full (the Amiga starting, or a Pi too slow for it).
   Dropping a sample when the buffer got full and stretching a whole batch
   when it ran low, as before, made pings and blips in the music. */
#define AO_MAX 4096
#define AO_OUT_MAX (AO_MAX * 2 + 16)

static double ao_phase;           /* next output's position, in input frames from the batch's start */
static int16_t ao_last[2];        /* the previous batch's last frame (position -1) */
static double ao_ratio_sum;       /* for the log: the rate adjustments, */
static unsigned ao_ratio_count;   /* how many, */
static unsigned ao_fill_min = ~0u, ao_fill_max;   /* and the buffer's lowest and highest, in % */

static size_t core_audio_batch(const int16_t *data, size_t frames)
{
	static int16_t buf[AO_OUT_MAX * 2];
	unsigned free_frames, used, n = (unsigned) frames, out = 0, c;
	int v = bmamiga_volume();
	double error, ratio, step, p;
	if (ao_open == 0 || frames == 0)
		return frames;
	audio_this_frame += (unsigned) frames;
	if (n > AO_MAX)
		n = AO_MAX;
	free_frames = host_sound_free_frames();
	used = free_frames < ao_latency ? ao_latency - free_frames : 0;
	if (ao_latency > 0) {
		unsigned fill = used * 100 / ao_latency;
		if (fill < ao_fill_min)
			ao_fill_min = fill;
		if (fill > ao_fill_max)
			ao_fill_max = fill;
	}
	/* Output frames per input frame: over 1 when the buffer is under half. */
	error = ao_latency > 0 ? ((double) ao_latency / 2 - used) / ((double) ao_latency / 2) : 0.0;
	if (error > 1.0)
		error = 1.0;
	if (error < -1.0)
		error = -1.0;
	ratio = 1.0 + error * (error > 0.75 || error < -0.75 ? 0.02 : 0.005);
	if (warp)
		ratio = 1.0;
	ao_ratio_sum += ratio;
	ao_ratio_count++;
	step = 1.0 / ratio;
	/* Input positions -1 (the last batch's last frame) to n - 1. */
	for (p = ao_phase; p <= (double) (n - 1) && out < AO_OUT_MAX; p += step, out++) {
		int j = (int) (p < 0.0 ? -1 : p);
		double frac = p - j;
		for (c = 0; c < 2; c++) {
			int a = j < 0 ? ao_last[c] : data[j * 2 + c];
			int b = data[(j + 1 < (int) n ? j + 1 : (int) n - 1) * 2 + c];
			buf[out * 2 + c] = (int16_t) ((a + (b - a) * frac) * v / 100);
		}
	}
	ao_phase = p - n;   /* from -1 up: carried into the next batch */
	if (ao_phase < -1.0)
		ao_phase = -1.0;
	ao_last[0] = data[(n - 1) * 2];
	ao_last[1] = data[(n - 1) * 2 + 1];
	if (out > free_frames)
		out = free_frames;
	if (out > 0)
		host_sound_write(buf, out);
	return frames;
}

/* For the log every 10 s: how full the sound buffer stayed and the
   average rate adjustment; then started again. */
static void sound_report(void)
{
	if (ao_open == 0 || ao_ratio_count == 0)
		return;
	LOG("sound: buffer %u%% to %u%% full, rate %+.2f%%\n", ao_fill_min, ao_fill_max,
	    (ao_ratio_sum / ao_ratio_count - 1.0) * 100.0);
	ao_ratio_sum = 0.0;
	ao_ratio_count = 0;
	ao_fill_min = ~0u;
	ao_fill_max = 0;
}

static void core_audio_sample(int16_t left, int16_t right)
{
	int16_t frame[2] = {left, right};
	core_audio_batch(frame, 1);
}

/* ---- Floppies ----------------------------------------------------------------- */

static char floppy[BMAMIGA_FLOPPIES][512];
static int core_running;   /* the core has run its first frame */

const char *bmamiga_floppy(int drive)
{
	if (drive < 0 || drive >= BMAMIGA_FLOPPIES || floppy[drive][0] == '\0')
		return NULL;
	return floppy[drive];
}

int bmamiga_insert_floppy(int drive, const char *path)
{
	char key[8];
	if (drive < 0 || drive >= BMAMIGA_FLOPPIES || path == NULL)
		return 0;
	snprintf(floppy[drive], sizeof floppy[drive], "%s", path);
	snprintf(key, sizeof key, "df%d", drive);
	save_setting(key, path);
	if (core_running && !bmamiga_core_insert_floppy(drive, path))
		return 0;
	LOG("DF%d: %s\n", drive, path);
	return 1;
}

void bmamiga_eject_floppy(int drive)
{
	char key[8];
	if (drive < 0 || drive >= BMAMIGA_FLOPPIES)
		return;
	floppy[drive][0] = '\0';
	snprintf(key, sizeof key, "df%d", drive);
	save_setting(key, "");
	if (core_running)
		bmamiga_core_eject_floppy(drive);
}

/* floppy_speed: 100 (a real drive), 200, 400, 800, or 0 (turbo). */
int bmamiga_floppy_speed(void)
{
	int speed = host_option_int("floppy_speed", 100);
	return speed == 0 || speed == 200 || speed == 400 || speed == 800 ? speed : 100;
}

void bmamiga_set_floppy_speed(int speed)
{
	char text[8];
	snprintf(text, sizeof text, "%d", speed);
	save_setting("floppy_speed", text);
	if (core_running)
		bmamiga_core_set_floppy_speed(bmamiga_floppy_speed());
}

/* ---- Hard drives and the CD -------------------------------------------- */

/* DH0: to DH3: and the CD, kept in bmamiga.cfg (dh0 to dh3, cd0). */
static char hard_drive[BMAMIGA_HARD_DRIVES][512];
static char cd_image[512];

const char *bmamiga_hard_drive(int drive)
{
	if (drive < 0 || drive >= BMAMIGA_HARD_DRIVES || hard_drive[drive][0] == '\0')
		return NULL;
	return hard_drive[drive];
}

const char *bmamiga_cd(void)
{
	return cd_image[0] != '\0' ? cd_image : NULL;
}

int bmamiga_model_has_cd(void)
{
	const char *id = bmamiga_model_id(bmamiga_model());
	return strncmp(id, "CD32", 4) == 0 || strcmp(id, "CDTV") == 0;
}

/* The Amiga's name for a folder: its own name, without what a volume name
   can't have (':' and '/', and ',' in Amiberry's configuration line). */
static void volume_name(char *out, size_t size, const char *path)
{
	const char *slash = strrchr(path, '/');
	size_t n = 0;
	const char *p = slash != NULL && slash[1] != '\0' ? slash + 1 : path;
	for (; *p != '\0' && n + 1 < size; p++)
		if (*p != ':' && *p != '/' && *p != ',' && *p != '"')
			out[n++] = *p;
	out[n] = '\0';
	if (n == 0)
		snprintf(out, size, "Work");
}

/* Gives Amiberry the hard drives (mounted at the next hard reset). */
static int apply_hard_drives(void)
{
	char names[BMAMIGA_HARD_DRIVES][32];
	const char *paths[BMAMIGA_HARD_DRIVES], *name_ptrs[BMAMIGA_HARD_DRIVES];
	int d, n;
	for (d = 0; d < BMAMIGA_HARD_DRIVES; d++) {
		paths[d] = hard_drive[d][0] != '\0' ? hard_drive[d] : NULL;
		volume_name(names[d], sizeof names[d], hard_drive[d]);
		name_ptrs[d] = names[d];
	}
	n = bmamiga_core_set_hard_drives(paths, name_ptrs);
	for (d = 0; d < BMAMIGA_HARD_DRIVES; d++)
		if (paths[d] != NULL)
			LOG("DH%d: %s\n", d, paths[d]);
	return n;
}

void bmamiga_set_hard_drive(int drive, const char *path)
{
	char key[8];
	if (drive < 0 || drive >= BMAMIGA_HARD_DRIVES)
		return;
	snprintf(hard_drive[drive], sizeof hard_drive[drive], "%s", path ? path : "");
	snprintf(key, sizeof key, "dh%d", drive);
	save_setting(key, hard_drive[drive]);
	if (core_running)
		apply_hard_drives();
}

void bmamiga_set_cd(const char *path)
{
	snprintf(cd_image, sizeof cd_image, "%s", path ? path : "");
	save_setting("cd0", cd_image);
	if (core_running) {
		bmamiga_core_set_cd(cd_image, bmamiga_model_has_cd());
		if (cd_image[0] != '\0')
			LOG("CD: %s\n", cd_image);
	}
}

/* ---- The Amiga's setup ---------------------------------------------------- */

/* The menu's Amiga Setup (bmamiga.cfg; empty or missing is the model's own
   setting) as Amiberry's settings, one a line, then bmamiga.uae's lines on
   the card (any of Amiberry's .uae settings), which win. */
static void setup_lines(char *out, size_t size)
{
	static const struct {
		const char *key, *format;
		int divide;
	} numbers[] = {
		{"chip_ram", "chipmem_size=%d\n", 512},   /* KB, in 512K */
		{"slow_ram", "bogomem_size=%d\n", 256},   /* KB, in 256K */
		{"fast_ram", "fastmem_size=%d\n", 1},     /* MB */
		{"z3_ram", "z3mem_size=%d\n", 1},         /* MB */
		{"stereo_separation", "sound_stereo_separation=%d\n", 1},   /* 0-10 */
	};
	static const struct {
		const char *key, *format;
	} words[] = {
		{"kickstart", "kickstart_rom_file=%s\n"},
		{"cpu", "cpu_model=%s\n"},
		{"fpu", "fpu_model=%s\n"},
		{"chipset", "chipset=%s\n"},
		{"sound_filter", "sound_filter=%s\n"},
	};
	char path[512];
	size_t len = 0;
	unsigned i;
	FILE *f;
	out[0] = '\0';
#define ADD(...) do { if (len < size) len += (size_t) snprintf(out + len, size - len, __VA_ARGS__); } while (0)
	for (i = 0; i < sizeof words / sizeof words[0]; i++) {
		const char *value = host_option(words[i].key, "");
		if (value[0] != '\0')
			ADD(words[i].format, value);
	}
	for (i = 0; i < sizeof numbers / sizeof numbers[0]; i++) {
		const char *value = host_option(numbers[i].key, "");
		if (value[0] != '\0')
			ADD(numbers[i].format, atoi(value) / numbers[i].divide);
	}
	/* Zorro III RAM and an RTG card (Picasso96's uaegfx, on Zorro III):
	   both are above the 24 bits' 16 MB, so the CPU (a 68020 or later)
	   addresses 32 bits. Picasso96 must be on the Amiga's disk (uaegfx in
	   DEVS:Monitors) for the card to show anything. */
	{
		const char *rtg = host_option("rtg", "");
		if (rtg[0] != '\0')
			ADD("gfxcard_type=ZorroIII\ngfxcard_size=%d\n", atoi(rtg));
		if (rtg[0] != '\0' || atoi(host_option("z3_ram", "0")) > 0)
			ADD("cpu_24bit_addressing=false\n");
	}
	/* The Amiga's network, through the Pi's (src/port/bmnet.c): Amiberry's
	   bsdsocket.library, for programs without a TCP/IP stack on the Amiga
	   (as AmiKit has it); or SLiRP (NAT), for one, on uaenet.device (a
	   SANA-II device, on any Amiga) or an A2065 card. */
	{
		const char *net = host_option("amiga_network", "");
		if (strcmp(net, "bsdsocket") == 0)
			ADD("bsdsocket_emu=true\n");
		else if (strcmp(net, "uaenet") == 0)
			ADD("sana2=true\n");
		else if (strcmp(net, "a2065") == 0)
			ADD("a2065=slirp\n");
	}
	/* The JIT: Amiberry's, compiling the Amiga's code to the ARM's, for a
	   68020 or later at full speed (not cycle-exact, which it can't be, nor
	   "more compatible", cpu_compatible, with which Amiberry's ARM64 JIT
	   compiles nothing: the A1200's preset has it on).
	   Indirect: memory through Amiberry's banks, as the Pi can't reserve
	   4 GB for the Amiga's memory or trap its I/O as a desktop does. */
	if (strcmp(host_option("jit", ""), "on") == 0)
		ADD("cachesize=16384\ncomp_trustbyte=indirect\ncomp_trustword=indirect\n"
		    "comp_trustlong=indirect\ncomp_trustnaddr=indirect\n"
		    "cpu_speed=max\ncycle_exact=false\ncpu_compatible=false\n");
	/* The JIT keeps the host address of the code it compiles in 32-bit
	   registers, read back sign-extended: every Amiga address it meets must
	   be below 2 GB on the Pi. BMAmiga's block for the Amiga's memory starts
	   at about 1 GB, so Zorro III RAM at 0x40000000 (Amiberry's "real"
	   mapping, which it picks thinking that block starts near 0) was past
	   2 GB, and the JIT crashed compiling code there (AmiKit, xSysInfo with
	   512 MB, 2026-11-07). Amiberry's "UAE" mapping puts Zorro III at
	   0x10000000: 512 MB of it and a 64 MB RTG card end at 1.8 GB.
	   bmamiga.uae can still say otherwise. */
	if (strcmp(host_option("jit", ""), "on") == 0
	    && (atoi(host_option("z3_ram", "0")) > 0 || host_option("rtg", "")[0] != '\0'))
		ADD("z3mapping=uae\n");
	/* The fastest: as fast as the Pi can, which isn't cycle-exact. */
	if (strcmp(host_option("cpu_speed", ""), "max") == 0)
		ADD("cpu_speed=max\ncycle_exact=false\n");
	/* Compatible, the default (empty, or "approx" as 0.3.1's test kernels
	   wrote it): the model's speed and "more compatible" CPU, but not
	   cycle-exact (the CPU's and the blitter's every cycle): far lighter,
	   which most games don't notice. An A500's cycle-exact 68000 took 12 ms
	   of the Pi 5's 20 ms a frame, and more than a Pi 4 or 400 has (Indy
	   Heat, 2026-11-07). "exact" keeps the model's cycle-exact timing. The
	   JIT sets the speed itself. */
	else if (strcmp(host_option("cpu_speed", ""), "exact") != 0
	         && strcmp(host_option("jit", ""), "on") != 0)
		ADD("cycle_exact=false\n");
	sd_path(path, sizeof path, "bmamiga.uae");
	f = fopen(path, "r");
	if (f != NULL) {
		if (len < size - 1)
			len += fread(out + len, 1, size - 1 - len, f);
		out[len] = '\0';
		fclose(f);
	}
#undef ADD
}

/* The setup's settings that Amiberry takes only as it starts: the Amiga's
   network, bsdsocket.library or uaenet.device, from the menu or
   bmamiga.uae. Amiberry puts them in the UAE Boot ROM once, by
   memory_init(), from the settings then, so a hard reset with them turned
   on (apply_setup()) is too late; the Amiga had no TCP stack. They go on
   the command line the core starts Amiberry with ("-s" before each, see
   amiberry_embed.h); a later hard reset turns them on or off. */
static void set_start_options(void)
{
	static char lines[8192];
	char start[512];
	const char *argv[32];
	const char *p = lines;
	size_t len = 0;
	int argc = 0;
	setup_lines(lines, sizeof lines);
	while (*p != '\0') {
		const char *end = strchr(p, '\n');
		size_t n = end != NULL ? (size_t) (end - p) : strlen(p);
		while (n > 0 && (*p == ' ' || *p == '\t'))
			p++, n--;
		if ((strncmp(p, "bsdsocket_emu=", 14) == 0 || strncmp(p, "sana2=", 6) == 0)
		    && len + n + 1 < sizeof start && argc + 2 <= (int) (sizeof argv / sizeof argv[0])) {
			memcpy(start + len, p, n);
			start[len + n] = '\0';
			LOG("start with %s\n", start + len);
			argv[argc++] = "-s";
			argv[argc++] = start + len;
			len += n + 1;
		}
		p = end != NULL ? end + 1 : p + n;
	}
	amiberry_libretro_set_extra_args(argc, argv);
}

/* Gives Amiberry the setup; a hard reset then uses it (the sound's
   settings it takes at once). Returns how many settings there are. */
static int apply_setup(void)
{
	static char lines[8192];
	int n;
	setup_lines(lines, sizeof lines);
	n = bmamiga_core_set_config(lines);
	if (n > 0)
		LOG("Amiga setup: %d setting(s)\n", n);
	return n;
}

void bmamiga_setup_changed(void)
{
	if (core_running)
		apply_setup();
}

/* The disks kept in bmamiga.cfg, put in once the Amiga is running: the
   floppies, and a CD32's or CDTV's disc, at once; the hard drives and
   another Amiga's CD with a hard reset, from which the Amiga starts with
   them (it has run only a frame). */
static void insert_saved_media(void)
{
	int d, drives, reset, setup = apply_setup();
	for (d = 0; d < BMAMIGA_FLOPPIES; d++) {
		if (floppy[d][0] != '\0' && access(floppy[d], R_OK) == 0) {
			bmamiga_core_insert_floppy(d, floppy[d]);
			LOG("DF%d: %s\n", d, floppy[d]);
		}
	}
	drives = apply_hard_drives();
	/* The setup's memory, CPU and Kickstart are for a hard reset too. A
	   CD32's or CDTV's own drive takes a disc as it runs: after the reset,
	   if there is one (bmamiga_hard_reset() puts it back); another
	   Amiga's CD drive is there from a hard reset. */
	reset = drives > 0 || setup > 0 || (cd_image[0] != '\0' && !bmamiga_model_has_cd());
	if (cd_image[0] != '\0' && !(reset && bmamiga_model_has_cd())) {
		bmamiga_core_set_cd(cd_image, bmamiga_model_has_cd());
		LOG("CD: %s\n", cd_image);
	}
	if (reset)
		bmamiga_hard_reset();
}

/* ---- Input -------------------------------------------------------------------- */

static retro_keyboard_event_t keyboard_cb;
static int open_menu;
static int pad_last_menu[HOST_PADS];
static int mouse_dx, mouse_dy, mouse_buttons;

static void core_input_poll(void) {}

/* The joystick in the Amiga's port 2: the first gamepad, or a GPIO
   joystick. A face buttons is fire, as the Amiga's one button; B the
   second (CD32 pads and some games). The left stick's direction is in the
   pad's state already (with a dead zone); host_pad_axis()'s position runs
   from 0 to 65535, so read as -32768 to 32767 a stick at rest (32767) was
   right and down held (a Pi 500, 2026-11-07). */
static int16_t joypad(unsigned id)
{
	int state = host_pad_state(0), gpio = host_gpio_joystick(1);
	if (state < 0)
		state = 0;
	switch (id) {
	case RETRO_DEVICE_ID_JOYPAD_UP:
		return (state & HOST_PAD_UP) || (gpio & HOST_JOY_UP);
	case RETRO_DEVICE_ID_JOYPAD_DOWN:
		return (state & HOST_PAD_DOWN) || (gpio & HOST_JOY_DOWN);
	case RETRO_DEVICE_ID_JOYPAD_LEFT:
		return (state & HOST_PAD_LEFT) || (gpio & HOST_JOY_LEFT);
	case RETRO_DEVICE_ID_JOYPAD_RIGHT:
		return (state & HOST_PAD_RIGHT) || (gpio & HOST_JOY_RIGHT);
	case RETRO_DEVICE_ID_JOYPAD_B:
		return (state & HOST_PAD_A) || (gpio & HOST_JOY_FIRE);
	case RETRO_DEVICE_ID_JOYPAD_A:
		return (state & HOST_PAD_B) != 0;
	case RETRO_DEVICE_ID_JOYPAD_Y:
		return (state & HOST_PAD_X) != 0;
	case RETRO_DEVICE_ID_JOYPAD_X:
		return (state & HOST_PAD_Y) != 0;
	case RETRO_DEVICE_ID_JOYPAD_L:
		return (state & HOST_PAD_L) != 0;
	case RETRO_DEVICE_ID_JOYPAD_R:
		return (state & HOST_PAD_R) != 0;
	case RETRO_DEVICE_ID_JOYPAD_START:
		return (state & HOST_PAD_START) != 0;
	case RETRO_DEVICE_ID_JOYPAD_SELECT:
		return (state & HOST_PAD_SELECT) != 0;
	default:
		return 0;
	}
}

static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void) index;
	if (device == RETRO_DEVICE_MOUSE && port == 0) {
		switch (id) {
		case RETRO_DEVICE_ID_MOUSE_X: return (int16_t) mouse_dx;
		case RETRO_DEVICE_ID_MOUSE_Y: return (int16_t) mouse_dy;
		case RETRO_DEVICE_ID_MOUSE_LEFT: return (mouse_buttons & 1) != 0;
		case RETRO_DEVICE_ID_MOUSE_RIGHT: return (mouse_buttons & 2) != 0;
		case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (mouse_buttons & 4) != 0;
		default: return 0;
		}
	}
	/* The core reads the gamepad of its port 0 (the player's, beside the
	   mouse) as its joystick 0, which it puts in the Amiga's port 2; its
	   port 1's pad would be a joystick in no port. */
	if (device == RETRO_DEVICE_JOYPAD && port == 0) {
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
			int16_t mask = 0;
			unsigned b;
			for (b = 0; b < 16; b++)
				if (joypad(b))
					mask |= (int16_t) (1 << b);
			return mask;
		}
		return joypad(id);
	}
	return 0;
}

static void process_input(void)
{
	int usage, pressed, pad, dx, dy, buttons;
	/* The mouse moves since the last frame, as the core reads them. */
	mouse_dx = mouse_dy = 0;
	if (host_mouse_read(&dx, &dy, &buttons)) {
		double speed = host_option_int("mouse_speed", 100) / 100.0;
		mouse_dx = (int) (dx * speed);
		mouse_dy = (int) (dy * speed);
		mouse_buttons = buttons;
	}
	while (host_key_event(&usage, &pressed)) {
		/* The power key: Shut Down (the Pi 400 sends it for Fn+F10). */
		if (usage == USAGE_POWER) {
			if (pressed)
				bmamiga_request_power(1);
			continue;
		}
		if (usage == KEY_MENU) {
			if (pressed)
				open_menu = TRUE;
			continue;
		}
		if (usage == KEY_WARP) {
			if (pressed)
				bmamiga_set_warp(!warp);
			continue;
		}
		if (keyboard_cb != NULL) {
			unsigned code = bmamiga_usage_to_retrok(usage);
			if (code != RETROK_UNKNOWN)
				keyboard_cb(pressed != 0, code, 0, 0);
		}
	}
	/* Gamepad Home, or Select and Start together, open the menu. */
	for (pad = 0; pad < HOST_PADS; pad++) {
		int state = host_pad_state(pad);
		int menu;
		if (state < 0)
			state = 0;
		menu = (state & HOST_PAD_HOME)
		       || ((state & HOST_PAD_SELECT) && (state & HOST_PAD_START));
		if (menu && !pad_last_menu[pad])
			open_menu = TRUE;
		pad_last_menu[pad] = menu;
	}
}

/* Keys held when the menu opened are let go, so that none stick. */
static void release_keys(void)
{
	int usage;
	if (keyboard_cb == NULL)
		return;
	for (usage = 0; usage < 256; usage++) {
		unsigned code = bmamiga_usage_to_retrok(usage);
		if (code != RETROK_UNKNOWN)
			keyboard_cb(false, code, 0, 0);
	}
}

/* ---- The libretro core's questions --------------------------------------------- */

static char system_dir[512];
static char save_dir[512];

static const char *core_option(const char *key)
{
	if (strcmp(key, "amiberry_model") == 0)
		return bmamiga_model_id(bmamiga_model());
	/* On while the display runs at the Amiga's rate: the core then shows each
	   frame and goes on at the next retro_run() without a wait of its own
	   (Amiberry #2629), so the display alone paces it. It changes only with
	   the display's rate, not when the Pi falls behind and catches up: each
	   change reconfigures Amiberry (set_config_changed(), its joystick ports
	   again), which a game at the limit did every few seconds and the sound
	   broke up (a Pi 500 with Indy Heat, 2026-11-07). Behind, BMAmiga paces
	   it by the clock itself (pace_by_clock()). */
	if (strcmp(key, "amiberry_internal_vsync") == 0)
		return vsync_lock ? "standard" : "disabled";
	/* The core's sound at the output's rate: made at 44.1 kHz and played at
	   the Pi's 48 kHz, 9% of it was missing, and the batches stretched to
	   fill the gap made the sound throb (a Pi 500 with Indy Heat,
	   2026-11-07). */
	if (strcmp(key, "amiberry_audio_rate") == 0)
		return ao_open == 48000 ? "48000" : ao_open == 44100 ? "44100" : "auto";
	if (strcmp(key, "amiberry_video_standard") == 0)
		return host_option("video_standard", "auto");
	if (strcmp(key, "amiberry_floppy_speed") == 0) {
		static char speed[8];
		snprintf(speed, sizeof speed, "%d", bmamiga_floppy_speed());
		return speed;
	}
	if (strcmp(key, "amiberry_statusline") == 0)
		return option_on("statusline", FALSE) ? "enabled" : "disabled";
	return NULL;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	char text[512];
	va_list ap;
	if (level < RETRO_LOG_INFO && !option_on("core_debug", FALSE))
		return;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	printf("[core] %s", text);
}

static bool core_environment(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *) data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		enum retro_pixel_format f = *(const enum retro_pixel_format *) data;
		if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565
		    && f != RETRO_PIXEL_FORMAT_0RGB1555)
			return false;
		pixel_format = f;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **) data = system_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **) data = save_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		v->value = v->key != NULL ? core_option(v->key) : NULL;
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *) data = options_dirty != 0;
		options_dirty = FALSE;
		return true;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		keyboard_cb = ((const struct retro_keyboard_callback *) data)->callback;
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *) data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		return true;
	case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
		*(bool *) data = false;   /* warp is set directly (bridge.cpp) */
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		take_av_info(data);
		return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY: {
		const struct retro_game_geometry *g = data;
		if (g->aspect_ratio > 0.1f)
			pic_aspect = g->aspect_ratio;
		return true;
	}
	default:
		return false;
	}
}

/* ---- Warp and resets ---------------------------------------------------------- */

int bmamiga_warp(void) { return warp; }

void bmamiga_set_warp(int on)
{
	warp = on;
	bmamiga_core_warp(on);
	options_dirty = TRUE;
	LOG("warp %s\n", on ? "on" : "off");
	osd_notify(on ? "Warp on" : "Warp off");
}

void bmamiga_reset(void)
{
	bmamiga_core_reset(FALSE);
}

/* With the Amiga Setup and the drives as they are now (a hard reset is
   when Amiberry takes the memory, CPU, chipset, Kickstart and drives). */
/* A CD32's or CDTV's disc goes back in this many frames after a hard
   reset: the reset (which comes at the next frame) empties its drive,
   before Amiberry has seen a disc put in just before it (0.2.0's CD32
   started without Battle Chess this way). */
#define CD_BACK_FRAMES 50
static int cd_back;

void bmamiga_hard_reset(void)
{
	if (core_running) {
		apply_setup();
		/* The drives after the setup, which has bmamiga.uae's. */
		apply_hard_drives();
		if (cd_image[0] != '\0' && bmamiga_model_has_cd()) {
			bmamiga_core_set_cd(NULL, TRUE);
			cd_back = CD_BACK_FRAMES;
		}
	}
	bmamiga_core_reset(TRUE);
}

/* Each frame: a CD32's or CDTV's disc back after a hard reset. */
static void cd_back_in(void)
{
	if (cd_back > 0 && --cd_back == 0 && cd_image[0] != '\0') {
		bmamiga_core_set_cd(cd_image, TRUE);
		LOG("CD: %s\n", cd_image);
	}
}

/* ---- Files ------------------------------------------------------------------- */

/* Amiberry writes floppies as the Amiga writes them; since the Pi can be
   switched off at any time, push everything out to the card each second. */
/* Every open file's data, directory entry and FAT out to the card: all of
   the C library's 32 (newlib_open_files_patch.diff; Amiberry keeps the
   Amiga's open files on folder drives open, and they reach past 20, which
   were never written out, even at a shut down). */
#define OPEN_FILES 32

static void sync_files(void)
{
	int fd;
	fflush(NULL);
	for (fd = 3; fd < OPEN_FILES; fd++)
		fsync(fd);
}

/* ---- Requests from the web UI ------------------------------------------ */

/* The web UI's server runs as a Circle task, which only runs when the
   emulator yields, so these are only ever set between frames. */
static volatile int request_power = -1;   /* 0 reboot, 1 power off */
static volatile int request_coldstart = FALSE;
static volatile int request_autostart = FALSE;
static char autostart_path[512];

void bmamiga_request_power(int power_off)
{
	request_power = power_off ? 1 : 0;
}

void bmamiga_request_coldstart(void)
{
	request_coldstart = TRUE;
}

/* Name kept from BMC64, whose web UI code calls it: a disk image to start,
   put in DF0: and the Amiga started again. */
void emu_autostart_interrupt(const char *path)
{
	snprintf(autostart_path, sizeof(autostart_path), "%s", path);
	request_autostart = TRUE;
}

int bmamiga_pending_requests(void)
{
	return request_power >= 0 || request_coldstart || request_autostart;
}

static void handle_requests(void)
{
	/* A CD32's or CDTV's disc goes in after its hard reset: Amiberry
	   takes a few seconds to change a disc, and a reset meanwhile loses
	   it. */
	static char cd_after_reset[512];
	if (cd_after_reset[0] != '\0') {
		bmamiga_set_cd(cd_after_reset);
		cd_after_reset[0] = '\0';
	}
	if (request_autostart) {
		request_autostart = FALSE;
		const char *dot = strrchr(autostart_path, '.');
		const char *ext = dot != NULL ? dot + 1 : "";
		LOG("web UI: starting %s\n", autostart_path);
		/* A hard disk image as DH0:, a CD image in the CD drive, a
		   floppy in DF0:; then the Amiga starts from it. */
		if (strcasecmp(ext, "hdf") == 0 || strcasecmp(ext, "hdz") == 0)
			bmamiga_set_hard_drive(0, autostart_path);
		else if (strcasecmp(ext, "iso") == 0 || strcasecmp(ext, "cue") == 0
		         || strcasecmp(ext, "ccd") == 0 || strcasecmp(ext, "nrg") == 0
		         || strcasecmp(ext, "mds") == 0 || strcasecmp(ext, "chd") == 0) {
			if (bmamiga_model_has_cd()) {
				bmamiga_set_cd(NULL);
				snprintf(cd_after_reset, sizeof(cd_after_reset), "%s", autostart_path);
			} else {
				bmamiga_set_cd(autostart_path);
			}
		} else
			bmamiga_insert_floppy(0, autostart_path);
		bmamiga_hard_reset();
	}
	if (request_coldstart) {
		request_coldstart = FALSE;
		LOG("web UI: hard reset\n");
		bmamiga_hard_reset();
	}
	if (request_power >= 0) {
		int power_off = request_power;
		request_power = -1;
		LOG("%s\n", power_off ? "shutting down" : "rebooting");
		sync_files();
		/* Tell any server a connection is over (BMAmiga makes none yet;
		   the web UI's own are the browser's to drop). */
		host_tcp_close_all();
		if (power_off)
			host_power_off();
		else
			host_reboot();
	}
}

static double last_sync_time;

void bmamiga_idle(void)
{
	double now = host_time();
	feed_silence();
	if (now - last_sync_time >= 1.0) {
		last_sync_time = now;
		sync_files();
	}
	if (request_power >= 0)
		handle_requests();
}

/* ---- Main loop ---------------------------------------------------------------- */

static void show_network_address(void)
{
	static int shown;
	char address[32];
	if (shown || !option_on("webui", TRUE))
		return;
	if (host_network_address(address, sizeof(address))) {
		char text[64];
		snprintf(text, sizeof(text), "Web UI: http://%s/", address);
		osd_notify(text);
		shown = TRUE;
	}
}

static void fatal(const char *text)
{
	LOG("%s\n", text);
	osd_open();
	osd_message(text, FALSE);
	for (;;) {
		bmamiga_idle();
		host_video_present(TRUE);
	}
}

/* ---- Speed, for the log ----------------------------------------------------- */

/* Every 10 s, as BMNeXT does: how fast the Amiga runs against a real one,
   the ARM's clock and temperature, and where the time went (the Amiga's
   emulation, and the kernel's three parts of showing a frame: Circle's
   tasks, the log, the picture, its wait for the display included). */
static unsigned speed_frames;
static double speed_since, speed_amiga;

static void report_speed(double now)
{
	double span = now - speed_since, tasks, log, picture;
	unsigned mhz, celsius;
	if (span <= 0.0)
		return;
	host_cpu_status(&mhz, &celsius);
	host_video_times(&tasks, &log, &picture);
	LOG("speed %.0f%% (%u frames in %.1f s), ARM %u MHz %u C; Amiga %.0f%%, "
	    "frames: tasks %.0f%% log %.0f%% picture %.0f%%%s\n",
	    speed_frames * 100.0 / (span * bmamiga_core_frame_rate()), speed_frames, span,
	    mhz, celsius, speed_amiga * 100.0 / span, tasks * 100.0 / span,
	    log * 100.0 / span, picture * 100.0 / span, warp ? " (warp)" : "");
	/* The card's reads in the last 10 s (circle_fatfs_sectorcache_patch.diff;
	   not in the PC simulator): how many, how many the cache had, and the
	   time spent reading the card, when there were any. */
	if (fatfs_disk_stats != NULL) {
		static unsigned last_reads, last_hits, last_writes;
		static unsigned long long last_us;
		unsigned reads, hits, writes;
		unsigned long long us;
		fatfs_disk_stats(&reads, &hits, &us, &writes);
		if (reads != last_reads || writes != last_writes)
			LOG("card: %u reads (%u cached), %.1f s reading, %u writes\n",
			    reads - last_reads, hits - last_hits, (us - last_us) / 1e6, writes - last_writes);
		last_reads = reads;
		last_hits = hits;
		last_us = us;
		last_writes = writes;
	}
	sound_report();
	/* bench=1: tools/bench's floppy counts its loops at $F0; how many a
	   second, to compare the CPU's speed with and without the JIT. */
	if (option_on("bench", FALSE)) {
		static uint32_t last;
		uint32_t count = bmamiga_core_peek_long(0xF0);
		LOG("bench: %.0f loops a second\n", (uint32_t) (count - last) / span);
		last = count;
	}
	speed_since = now;
	speed_frames = 0;
	speed_amiga = 0.0;
}

void bmamiga_main(void)
{
	struct retro_system_av_info av;
	int pitch, d;
	double last_second;

	if (host_video_init(FB_W, FB_H, &fb_pixels, &pitch) != 0) {
		LOG("no display\n");
		for (;;)
			host_sleep_us(1000000);
	}
	fb_pitch_px = pitch / 2;
	clear_framebuffer();
	apply_view();
	measure_refresh_rate();

	sd_path(system_dir, sizeof system_dir, "roms");
	sd_path(save_dir, sizeof save_dir, "saves");
	for (d = 0; d < BMAMIGA_FLOPPIES; d++) {
		char key[8];
		snprintf(key, sizeof key, "df%d", d);
		snprintf(floppy[d], sizeof floppy[d], "%s", host_option(key, ""));
	}
	for (d = 0; d < BMAMIGA_HARD_DRIVES; d++) {
		char key[8];
		snprintf(key, sizeof key, "dh%d", d);
		snprintf(hard_drive[d], sizeof hard_drive[d], "%s", host_option(key, ""));
	}
	snprintf(cd_image, sizeof cd_image, "%s", host_option("cd0", ""));

	{
		char version[64];
		snprintf(version, sizeof version, "Version %s", BMAMIGA_VERSION);
		osd_splash(version, "Starting the Amiga...");
		host_video_present(TRUE);
	}

	retro_set_environment(core_environment);
	retro_set_video_refresh(core_video);
	retro_set_audio_sample(core_audio_sample);
	retro_set_audio_sample_batch(core_audio_batch);
	retro_set_input_poll(core_input_poll);
	retro_set_input_state(core_input_state);
	retro_init();
	set_start_options();
	/* The sound output's rate (48 kHz on the Pi's HDMI), for the core to
	   make its sound at (amiberry_audio_rate). */
	open_sound();
	if (!retro_load_game(NULL))
		fatal("Amiberry did not start. The log (see the User Guide) says why.");
	retro_set_controller_port_device(0, RETRO_DEVICE_MOUSE);
	retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
	retro_get_system_av_info(&av);
	ao_rate = av.timing.sample_rate > 8000.0 ? (unsigned) (av.timing.sample_rate + 0.5) : 44100;
	open_sound();
	feed_silence();   /* start half full */
	take_av_info(&av);
	update_sync_mode();
	LOG("%s, %.2f Hz, sound %u Hz\n", bmamiga_machine_description(), machine_rate, ao_rate);

	last_second = speed_since = host_time();
	for (;;) {
		static int speed_seconds;
		double now, amiga_start;

		process_input();
		if (open_menu) {
			open_menu = FALSE;
			release_keys();
			bmamiga_menu();
			release_keys();
			frame_start = 0.0;
			bmamiga_core_sync_clock();   /* the Amiga stood still */
		}

		audio_this_frame = 0;
		amiga_start = host_time();
		retro_run();
		/* Amiberry's other threads (the hard drives', the file
		   systems'), which take turns with the emulation, run at least
		   once a frame. */
		bma_threads_yield();
		bmamiga_core_cd32_pad();
		cd_back_in();
		speed_amiga += host_time() - amiga_start;
		speed_frames++;
		/* Frames without sound (the Amiga starting): silence instead,
		   so that the output doesn't run dry. */
		if (audio_this_frame == 0)
			feed_silence();
		if (!core_running) {
			core_running = TRUE;
			bmamiga_osd_view(FALSE);   /* the start-up screen goes */
			insert_saved_media();
		}
		follow_frame_rate();

		osd_draw_notification(fb_pixels, fb_pitch_px, pic_w, pic_h);
		now = host_time();
		if (!warp)
			measure_load(now);
		if (vsync_lock && overloaded && !warp)
			pace_by_clock(now);
		host_video_present(display_paces());
		frame_start = host_time();
		frame_slept = bmamiga_clock_slept;
		bmamiga_clock_frame(display_paces(), 1.0 / bmamiga_core_frame_rate());

		now = host_time();
		if (now - last_second >= 1.0) {
			last_second = now;
			sync_files();
			if (host_save_clock())
				bmamiga_core_sync_clock();   /* the network set the time */
			show_network_address();
			if (++speed_seconds >= 10) {
				speed_seconds = 0;
				report_speed(now);
			}
		}
		handle_requests();
	}
}
