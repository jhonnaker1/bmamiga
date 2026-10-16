/*
 * keys.c - USB keyboard keys to libretro key codes for Amiberry's core.
 *
 * The kernel reports keys as USB HID usage codes; the libretro core takes
 * RETROK_* codes (libretro.h), which it turns into SDL scancodes, and those
 * into Amiga keys with Amiberry's usual PC keyboard layout, as in Amiberry
 * on a desktop (src/osdep/keyboard.cpp): the Windows keys (or Insert and
 * Home) are the left and right Amiga keys, Page Down is Help, Delete is Del.
 *
 * F11 and F12 are BMAmiga's own (warp and the menu, platform.c), as the
 * Amiga has no keys for them.
 *
 * This file is part of BMAmiga, a bare metal Amiga emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#include "libretro.h"

#include "bmamiga.h"

/* Indexed by USB HID usage; 0 is "none" (RETROK_UNKNOWN). */
static const unsigned short usage_to_retrok[256] = {
	[0x04] = RETROK_a, [0x05] = RETROK_b, [0x06] = RETROK_c, [0x07] = RETROK_d,
	[0x08] = RETROK_e, [0x09] = RETROK_f, [0x0a] = RETROK_g, [0x0b] = RETROK_h,
	[0x0c] = RETROK_i, [0x0d] = RETROK_j, [0x0e] = RETROK_k, [0x0f] = RETROK_l,
	[0x10] = RETROK_m, [0x11] = RETROK_n, [0x12] = RETROK_o, [0x13] = RETROK_p,
	[0x14] = RETROK_q, [0x15] = RETROK_r, [0x16] = RETROK_s, [0x17] = RETROK_t,
	[0x18] = RETROK_u, [0x19] = RETROK_v, [0x1a] = RETROK_w, [0x1b] = RETROK_x,
	[0x1c] = RETROK_y, [0x1d] = RETROK_z,
	[0x1e] = RETROK_1, [0x1f] = RETROK_2, [0x20] = RETROK_3, [0x21] = RETROK_4,
	[0x22] = RETROK_5, [0x23] = RETROK_6, [0x24] = RETROK_7, [0x25] = RETROK_8,
	[0x26] = RETROK_9, [0x27] = RETROK_0,
	[0x28] = RETROK_RETURN, [0x29] = RETROK_ESCAPE, [0x2a] = RETROK_BACKSPACE,
	[0x2b] = RETROK_TAB, [0x2c] = RETROK_SPACE, [0x2d] = RETROK_MINUS,
	[0x2e] = RETROK_EQUALS, [0x2f] = RETROK_LEFTBRACKET, [0x30] = RETROK_RIGHTBRACKET,
	[0x31] = RETROK_BACKSLASH,
	[0x32] = RETROK_BACKSLASH,        /* non-US #: the key by Return */
	[0x33] = RETROK_SEMICOLON, [0x34] = RETROK_QUOTE, [0x35] = RETROK_BACKQUOTE,
	[0x36] = RETROK_COMMA, [0x37] = RETROK_PERIOD, [0x38] = RETROK_SLASH,
	[0x39] = RETROK_CAPSLOCK,
	[0x3a] = RETROK_F1, [0x3b] = RETROK_F2, [0x3c] = RETROK_F3, [0x3d] = RETROK_F4,
	[0x3e] = RETROK_F5, [0x3f] = RETROK_F6, [0x40] = RETROK_F7, [0x41] = RETROK_F8,
	[0x42] = RETROK_F9, [0x43] = RETROK_F10,
	[0x46] = RETROK_PRINT, [0x47] = RETROK_SCROLLOCK, [0x48] = RETROK_PAUSE,
	[0x49] = RETROK_INSERT, [0x4a] = RETROK_HOME, [0x4b] = RETROK_PAGEUP,
	[0x4c] = RETROK_DELETE, [0x4d] = RETROK_END, [0x4e] = RETROK_PAGEDOWN,
	[0x4f] = RETROK_RIGHT, [0x50] = RETROK_LEFT, [0x51] = RETROK_DOWN, [0x52] = RETROK_UP,
	[0x53] = RETROK_NUMLOCK,
	[0x54] = RETROK_KP_DIVIDE, [0x55] = RETROK_KP_MULTIPLY, [0x56] = RETROK_KP_MINUS,
	[0x57] = RETROK_KP_PLUS, [0x58] = RETROK_KP_ENTER,
	[0x59] = RETROK_KP1, [0x5a] = RETROK_KP2, [0x5b] = RETROK_KP3, [0x5c] = RETROK_KP4,
	[0x5d] = RETROK_KP5, [0x5e] = RETROK_KP6, [0x5f] = RETROK_KP7, [0x60] = RETROK_KP8,
	[0x61] = RETROK_KP9, [0x62] = RETROK_KP0, [0x63] = RETROK_KP_PERIOD,
	[0x64] = RETROK_LESS,             /* the ISO key by left Shift */
	[0x65] = RETROK_MENU,
	[0x67] = RETROK_KP_EQUALS,
	[0xe0] = RETROK_LCTRL, [0xe1] = RETROK_LSHIFT, [0xe2] = RETROK_LALT,
	[0xe3] = RETROK_LSUPER, [0xe4] = RETROK_RCTRL, [0xe5] = RETROK_RSHIFT,
	[0xe6] = RETROK_RALT, [0xe7] = RETROK_RSUPER,
};

unsigned bmamiga_usage_to_retrok(int usage)
{
	if (usage < 0 || usage > 0xff)
		return RETROK_UNKNOWN;
	return usage_to_retrok[usage];
}
