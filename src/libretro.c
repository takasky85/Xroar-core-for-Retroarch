/** \file
 *
 *  \brief libretro core for XRoar, Tandy Color Computer 1, 2 and 3 only.
 *
 *  Drives XRoar's emulation core through a minimal host implementation (see
 *  libretro_host.c) that provides video, audio and joystick modules.
 *
 *  Based on the libretro glue from konsumer/xroar-libretro.
 *
 *  \licenseblock This file is part of xroar-core-for-retroarch.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  \endlicenseblock
 */

#include "top-config.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "libretro.h"

#include "dkbd.h"
#include "events.h"
#include "keyboard.h"
#include "machine.h"
#include "snapshot.h"
#include "ui.h"
#include "vo.h"
#include "xroar.h"

#include "libretro_host.h"
#include "disk_autorun.h"
#include "osk.h"
#include "autocrop.h"
#include "smartcrop.h"

#define RETRO_EXPORT __attribute__((visibility("default")))

#define LB_MAX_SLICES   (64)
#define LB_MAX_WIDTH    (800)
#define LB_MAX_HEIGHT   (300)
/* Output size limit: the picture is doubled while the keyboard is up. */
#define LB_OUT_MAX_W    (2200)
#define LB_OUT_MAX_H    (800)
/* Largest picture handed to the zoom stage (70% zoom of a 736x276 picture). */
#define LB_ZOOM_MAX_W   (1100)
#define LB_ZOOM_MAX_H   (420)
#ifndef LB_BUILD
#define LB_BUILD "local"
#endif
#define LB_CORE_VERSION "1.13.0-dev (93842a7), build " LB_BUILD

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static struct retro_log_callback log_cb;
static bool have_log;

static bool core_active;
static bool game_loaded;
static double lb_fps = 59.92;
static unsigned lb_last_width, lb_last_height;
static char lb_state_path[4096];
static size_t lb_state_size;

/* Options */
static bool opt_translate = true;
static bool opt_swap_ports;
static int opt_tv_input = -1;   /* -1 = XRoar default */
static int opt_ccr = VO_CMP_CCR_5BIT;
static unsigned lb_out_w, lb_out_h;

/* Border mode and zoom.  The core options give the starting values; the
 * on-screen keyboard buttons change them while the core runs. */
enum { BORDER_THIN, BORDER_NONE, BORDER_FULL, BORDER_SMART, BORDER_AUTO, BORDER_COUNT };
static const char *const border_names[BORDER_COUNT] = {
	"THIN", "NONE", "FULL", "SMART", "AUTO-CROP"
};
static const unsigned zoom_levels[] = { 70, 80, 90, 100, 110, 125, 150, 175, 200 };
#define N_ZOOM    (sizeof(zoom_levels) / sizeof(zoom_levels[0]))
#define ZOOM_100  (3)
static int rt_border = BORDER_THIN;
static int rt_zoom = ZOOM_100;
static int last_opt_border = -2, last_opt_zoom = -2;

/* Scratch buffer for zoomed-out pictures (padded with black). */
static uint32_t *lb_zoom;

/* Output buffer used when the on-screen keyboard is drawn over the picture. */
static uint32_t *lb_out;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void lb_log(enum retro_log_level level, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	if (have_log) {
		char buf[1024];
		vsnprintf(buf, sizeof(buf), fmt, va);
		log_cb.log(level, "[XRoar] %s", buf);
	}
	va_end(va);
}

static void lb_notify(const char *msg, unsigned frames) {
	struct retro_message m = { msg, frames };
	environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Core options.  The legacy SET_VARIABLES interface is used so the core
 * works with any RetroArch build; the first value listed is the default. */

static const struct retro_variable lb_vars[] = {
	{ "xroar_machine",
	  "Machine (restart content); Auto (CoCo 3 unless the path says CoCo 1 or 2)|CoCo 1/2 (NTSC)|CoCo 2B (NTSC, lowercase)|CoCo 3 (NTSC)|CoCo 3 (NTSC, 6309 CPU)|CoCo 1/2 (PAL)|CoCo 2B (PAL)|CoCo 3 (PAL)" },
	{ "xroar_osk_opacity",
	  "On-screen keyboard opacity; 50%|25%|75%|100%|0%" },
	{ "xroar_border",
	  "Border; Thin|None (256x192 only)|Full|Smart (grows once for big CoCo 3 screens)|Auto-crop (like VICE, can jiggle)" },
	{ "xroar_zoom",
	  "Zoom (also on the on-screen keyboard); 100%|70%|80%|90%|110%|125%|150%|175%|200%" },
	{ "xroar_tv_input",
	  "Video input; Default|Composite, blue-red artifacts|Composite, red-blue artifacts|S-Video (no artifacts)|RGB" },
	{ "xroar_ccr",
	  "Artifact colour renderer; 5-bit (fast)|Simple|Partial NTSC|Simulated (slow)|None" },
	{ "xroar_kbd_mode",
	  "Keyboard mapping; Translated (type what you see)|Positional (real CoCo layout)" },
	{ "xroar_player1_port",
	  "Player 1 joystick port; Right|Left" },
	{ "xroar_tape_fast",
	  "Fast cassette loading; enabled|disabled" },
	{ "xroar_autorun",
	  "Autorun content (restart content); enabled|disabled" },
	{ NULL, NULL },
};

static const char *lb_get_var(const char *key) {
	struct retro_variable var = { key, NULL };
	if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		return var.value;
	return NULL;
}

static bool lb_var_is(const char *key, const char *prefix) {
	const char *v = lb_get_var(key);
	return v && strncmp(v, prefix, strlen(prefix)) == 0;
}

/* Machine for the content path.  "Auto" picks a CoCo 3, unless the file name
 * or a folder in its path names a CoCo 1 or 2 (e.g. "Games/CoCo 2/foo.cas"),
 * in which case it picks a CoCo 2. */
static const char *lb_machine_for(const char *path) {
	const char *v = lb_get_var("xroar_machine");
	if (v) {
		if (!strcmp(v, "CoCo 1/2 (NTSC)")) return "cocous";
		if (!strcmp(v, "CoCo 2B (NTSC, lowercase)")) return "coco2bus";
		if (!strcmp(v, "CoCo 3 (NTSC)")) return "coco3";
		if (!strcmp(v, "CoCo 3 (NTSC, 6309 CPU)")) return "coco3h";
		if (!strcmp(v, "CoCo 1/2 (PAL)")) return "coco";
		if (!strcmp(v, "CoCo 2B (PAL)")) return "coco2b";
		if (!strcmp(v, "CoCo 3 (PAL)")) return "coco3p";
	}
	if (path) {
		static const char *const hints[] = {
			"coco1", "coco 1", "coco_1", "coco-1",
			"coco2", "coco 2", "coco_2", "coco-2", NULL
		};
		char low[4096];
		size_t i;
		for (i = 0; path[i] && i < sizeof(low) - 1; i++) {
			char c = path[i];
			low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
		}
		low[i] = 0;
		for (int h = 0; hints[h]; h++) {
			if (strstr(low, hints[h]))
				return "cocous";
		}
	}
	return "coco3";
}

static void lb_apply_border(void) {
	int picture;
	switch (rt_border) {
	case BORDER_NONE: picture = VO_PICTURE_ZOOMED; break;
	case BORDER_FULL: picture = UI_AUTO; break;
	case BORDER_SMART:
	case BORDER_AUTO: picture = VO_PICTURE_UNDERSCAN; break;
	default: picture = VO_PICTURE_TITLE; break;
	}
	ui_update_state(-1, ui_tag_picture, picture, NULL);
	smartcrop_reset();
	autocrop_reset();
}

/* On-screen keyboard buttons. */
static void lb_osk_action(int action) {
	switch (action) {
	case OSK_ACT_BORDER:
		rt_border = (rt_border + 1) % BORDER_COUNT;
		lb_apply_border();
		break;
	case OSK_ACT_VIDEO:
		ui_update_state(-1, ui_tag_tv_input, UI_NEXT, NULL);
		break;
	case OSK_ACT_ZOOM_IN:
		if (rt_zoom < (int)N_ZOOM - 1)
			rt_zoom++;
		break;
	case OSK_ACT_ZOOM_OUT:
		if (rt_zoom > 0)
			rt_zoom--;
		break;
	case OSK_ACT_ZOOM_RESET:
		rt_zoom = ZOOM_100;
		break;
	}
}

/* Status bar text: machine, border, video input, zoom. */
static void lb_update_status(void) {
	static const char *const tvn[] = { "S-VIDEO", "CMP BLUE-RED", "CMP RED-BLUE", "RGB" };
	char mach[40] = "?";
	int tv = -1;
	if (xroar.machine_config) {
		const char *d = xroar.machine_config->description;
		if (d) {
			if (!strncasecmp(d, "Tandy ", 6))
				d += 6;
			size_t i;
			for (i = 0; d[i] && i < sizeof(mach) - 1; i++)
				mach[i] = (d[i] >= 'a' && d[i] <= 'z') ? (char)(d[i] - 32) : d[i];
			mach[i] = 0;
		}
		tv = xroar.machine_config->tv_input;
	}
	char buf[96];
	snprintf(buf, sizeof(buf), "%s  BORDER: %s  VIDEO: %s  ZOOM: %u%%", mach,
		 border_names[rt_border], (tv >= 0 && tv <= 3) ? tvn[tv] : "?",
		 zoom_levels[rt_zoom]);
	osk_set_status(buf);
}

static void lb_read_runtime_options(bool apply) {
	opt_translate = !lb_var_is("xroar_kbd_mode", "Positional");
	opt_swap_ports = lb_var_is("xroar_player1_port", "Left");

	int tv = -1;
	const char *v = lb_get_var("xroar_tv_input");
	if (v) {
		if (strstr(v, "blue-red")) tv = TV_INPUT_CMP_KBRW;
		else if (strstr(v, "red-blue")) tv = TV_INPUT_CMP_KRBW;
		else if (!strncmp(v, "S-Video", 7)) tv = TV_INPUT_SVIDEO;
		else if (!strcmp(v, "RGB")) tv = TV_INPUT_RGB;
	}

	int ccr = VO_CMP_CCR_5BIT;
	v = lb_get_var("xroar_ccr");
	if (v) {
		if (!strcmp(v, "Simple")) ccr = VO_CMP_CCR_2BIT;
		else if (!strcmp(v, "Partial NTSC")) ccr = VO_CMP_CCR_PARTIAL;
		else if (!strncmp(v, "Simulated", 9)) ccr = VO_CMP_CCR_SIMULATED;
		else if (!strcmp(v, "None")) ccr = VO_CMP_CCR_PALETTE;
	}

	bool tape_fast = !lb_var_is("xroar_tape_fast", "disabled");

	const char *op = lb_get_var("xroar_osk_opacity");
	osk_set_opacity(op ? (unsigned)atoi(op) : 50);

	int border = BORDER_THIN;
	if (lb_var_is("xroar_border", "None")) border = BORDER_NONE;
	else if (lb_var_is("xroar_border", "Full")) border = BORDER_FULL;
	else if (lb_var_is("xroar_border", "Smart")) border = BORDER_SMART;
	else if (lb_var_is("xroar_border", "Auto")) border = BORDER_AUTO;

	int zoom = ZOOM_100;
	const char *zv = lb_get_var("xroar_zoom");
	if (zv) {
		unsigned z = (unsigned)atoi(zv);
		for (unsigned i = 0; i < N_ZOOM; i++)
			if (zoom_levels[i] == z)
				zoom = (int)i;
	}

	if (!apply || !core_active)
		return;

	if (tv >= 0 && tv != opt_tv_input)
		ui_update_state(-1, ui_tag_tv_input, tv, NULL);
	opt_tv_input = tv;
	if (ccr != opt_ccr)
		ui_update_state(-1, ui_tag_ccr, ccr, NULL);
	opt_ccr = ccr;
	/* A core option only overrides what the keyboard buttons set when the
	 * option itself changes. */
	if (border != last_opt_border) {
		rt_border = border;
		lb_apply_border();
	}
	last_opt_border = border;
	if (zoom != last_opt_zoom)
		rt_zoom = zoom;
	last_opt_zoom = zoom;
	ui_update_state(-1, ui_tag_tape_flag_fast, tape_fast, NULL);
	libretro_host_connect_joysticks();
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Keyboard.  Special keys always map by position.  In translated mode, keys
 * that produce a printable character are typed as that character (so
 * Shift+2 on a PC/Android keyboard gives '"' or '@' as printed on it);
 * in positional mode they map to the key in the same place on the CoCo. */

static int8_t lb_special_key(unsigned key) {
	switch (key) {
	case RETROK_RETURN:
	case RETROK_KP_ENTER: return DSCAN_ENTER;
	case RETROK_SPACE: return DSCAN_SPACE;
	case RETROK_BACKSPACE: return DSCAN_BACKSPACE;
	case RETROK_ESCAPE:
	case RETROK_END:
	case RETROK_PAUSE: return DSCAN_BREAK;
	case RETROK_HOME:
	case RETROK_DELETE: return DSCAN_CLEAR;
	case RETROK_UP: return DSCAN_UP;
	case RETROK_DOWN: return DSCAN_DOWN;
	case RETROK_LEFT: return DSCAN_LEFT;
	case RETROK_RIGHT: return DSCAN_RIGHT;
	case RETROK_LCTRL:
	case RETROK_RCTRL: return DSCAN_CTRL;
	case RETROK_LALT:
	case RETROK_RALT: return DSCAN_ALT;
	case RETROK_F1: return DSCAN_F1;
	case RETROK_F2: return DSCAN_F2;
	default: return DSCAN_INVALID;
	}
}

static int8_t lb_positional_key(unsigned key) {
	if (key >= RETROK_a && key <= RETROK_z)
		return (int8_t)(DSCAN_A + (key - RETROK_a));
	if (key >= RETROK_0 && key <= RETROK_9)
		return (int8_t)(DSCAN_0 + (key - RETROK_0));
	if (key >= RETROK_KP0 && key <= RETROK_KP9)
		return (int8_t)(DSCAN_0 + (key - RETROK_KP0));
	switch (key) {
	case RETROK_LSHIFT:
	case RETROK_RSHIFT: return DSCAN_SHIFT;
	case RETROK_COMMA: return DSCAN_COMMA;
	case RETROK_PERIOD:
	case RETROK_KP_PERIOD: return DSCAN_FULL_STOP;
	case RETROK_SLASH:
	case RETROK_KP_DIVIDE: return DSCAN_SLASH;
	case RETROK_SEMICOLON: return DSCAN_SEMICOLON;
	case RETROK_QUOTE:
	case RETROK_COLON: return DSCAN_COLON;
	case RETROK_MINUS:
	case RETROK_KP_MINUS: return DSCAN_MINUS;
	case RETROK_LEFTBRACKET:
	case RETROK_AT: return DSCAN_AT;
	default: return DSCAN_INVALID;
	}
}

/* Character typed by each held key in translated mode, so the release
 * matches the press even if Shift changed in between. */
static uint32_t lb_held_char[RETROK_LAST];

static void lb_keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t mod) {
	(void)mod;
	struct keyboard_interface *ki = xroar.keyboard_interface;
	if (!ki || keycode >= RETROK_LAST)
		return;

	int8_t dscan = lb_special_key(keycode);
	if (dscan != DSCAN_INVALID) {
		if (down)
			keyboard_press(ki, dscan);
		else
			keyboard_release(ki, dscan);
		return;
	}

	if (opt_translate) {
		if (down) {
			if (character >= 0x20 && character < 0x7f &&
			    ki->keymap.unicode_to_dkey[character].dk_key != DSCAN_INVALID) {
				if (lb_held_char[keycode])
					keyboard_unicode_release(ki, lb_held_char[keycode]);
				lb_held_char[keycode] = character;
				keyboard_unicode_press(ki, character);
			}
		} else if (lb_held_char[keycode]) {
			keyboard_unicode_release(ki, lb_held_char[keycode]);
			lb_held_char[keycode] = 0;
		}
		return;
	}

	dscan = lb_positional_key(keycode);
	if (dscan == DSCAN_INVALID)
		return;
	if (down)
		keyboard_press(ki, dscan);
	else
		keyboard_release(ki, dscan);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Gamepads.  RetroPad 1 drives the joystick in the player 1 port (right by
 * default, as most CoCo games expect), RetroPad 2 the other one.  Spare
 * buttons on pad 1 press keys games commonly ask for, and Select opens the
 * on-screen keyboard.  On a TV remote, OK is RetroPad B, Channel Up is Start
 * and Channel Down is Select. */

struct lb_padkey {
	unsigned id;
	int8_t dscan;
};

static const struct lb_padkey lb_padkeys[] = {
	{ RETRO_DEVICE_ID_JOYPAD_START,  DSCAN_ENTER },
	{ RETRO_DEVICE_ID_JOYPAD_X,      DSCAN_1 },
	{ RETRO_DEVICE_ID_JOYPAD_Y,      DSCAN_2 },
	{ RETRO_DEVICE_ID_JOYPAD_L2,     DSCAN_Y },
	{ RETRO_DEVICE_ID_JOYPAD_R2,     DSCAN_N },
	{ RETRO_DEVICE_ID_JOYPAD_L3,     DSCAN_BREAK },
	{ RETRO_DEVICE_ID_JOYPAD_R3,     DSCAN_CLEAR },
};
#define LB_NUM_PADKEYS (sizeof(lb_padkeys) / sizeof(lb_padkeys[0]))
static bool lb_padkey_down[LB_NUM_PADKEYS];

static int lb_axis(int analog, bool neg, bool pos) {
	if (neg)
		return 0;
	if (pos)
		return 65535;
	return analog + 32768;
}

static void lb_release_padkeys(void) {
	for (unsigned i = 0; i < LB_NUM_PADKEYS; i++) {
		if (lb_padkey_down[i] && xroar.keyboard_interface)
			keyboard_release(xroar.keyboard_interface, lb_padkeys[i].dscan);
		lb_padkey_down[i] = false;
	}
}

static int16_t lb_btn(unsigned pad, unsigned id) {
	return input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, id);
}

/* Select toggles the on-screen keyboard; while it is up, pad 1 drives it
 * instead of the joystick. */
static bool lb_select_down;

static void lb_poll_osk(void) {
	bool sel = lb_btn(0, RETRO_DEVICE_ID_JOYPAD_SELECT) != 0;
	if (sel && !lb_select_down) {
		osk_toggle();
		if (osk_active())
			lb_release_padkeys();
	}
	lb_select_down = sel;

	unsigned b = 0;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_UP)) b |= OSK_UP;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_DOWN)) b |= OSK_DOWN;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_LEFT)) b |= OSK_LEFT;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) b |= OSK_RIGHT;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_B)) b |= OSK_PRESS;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_A)) b |= OSK_BACKSPACE;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_Y)) b |= OSK_SHIFT;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_X)) b |= OSK_MOVE;
	if (lb_btn(0, RETRO_DEVICE_ID_JOYPAD_START)) b |= OSK_ENTER;
	osk_frame(b);
}

static void lb_poll_pad(unsigned pad) {
	if (pad == 0 && osk_active()) {
		/* Centre the joystick and let go of the fire buttons. */
		unsigned port = opt_swap_ports ? 1 : 0;
		libretro_host_joystick_axis(port, 0, 32767);
		libretro_host_joystick_axis(port, 1, 32767);
		libretro_host_joystick_button(port, 0, false);
		libretro_host_joystick_button(port, 1, false);
		return;
	}
	int lx = input_state_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
	int ly = input_state_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
	bool l = input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
	bool r = input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
	bool u = input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
	bool d = input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);

	/* Port 0 is XRoar's right joystick port. */
	unsigned port = pad;
	if (opt_swap_ports)
		port ^= 1;

	libretro_host_joystick_axis(port, 0, lb_axis(lx, l, r));
	libretro_host_joystick_axis(port, 1, lb_axis(ly, u, d));
	libretro_host_joystick_button(port, 0,
		input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B));
	libretro_host_joystick_button(port, 1,
		input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A));

	if (pad != 0 || !xroar.keyboard_interface)
		return;
	if (osk_active())
		return;
	for (unsigned i = 0; i < LB_NUM_PADKEYS; i++) {
		bool down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, lb_padkeys[i].id) != 0;
		if (down == lb_padkey_down[i])
			continue;
		lb_padkey_down[i] = down;
		if (down)
			keyboard_press(xroar.keyboard_interface, lb_padkeys[i].dscan);
		else
			keyboard_release(xroar.keyboard_interface, lb_padkeys[i].dscan);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

RETRO_EXPORT unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

RETRO_EXPORT void retro_get_system_info(struct retro_system_info *info) {
	memset(info, 0, sizeof(*info));
	info->library_name = "XRoar CoCo";
	info->library_version = LB_CORE_VERSION;
	info->valid_extensions = "ccc|rom|cas|dsk|jvc|dmk|os9|vdk|bin|cco|bas|asc|sn|vhd";
	info->need_fullpath = true;
	info->block_extract = false;
}

RETRO_EXPORT void retro_get_system_av_info(struct retro_system_av_info *info) {
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = libretro_host_video_width();
	info->geometry.base_height = libretro_host_video_height();
	info->geometry.max_width = LB_OUT_MAX_W;
	info->geometry.max_height = LB_OUT_MAX_H;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = lb_fps;
	info->timing.sample_rate = libretro_host_audio_rate();
}

RETRO_EXPORT void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;

	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb))
		have_log = true;

	environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)lb_vars);

	static const struct retro_controller_description controllers[] = {
		{ "CoCo joystick", RETRO_DEVICE_JOYPAD },
	};
	static const struct retro_controller_info ports[] = {
		{ controllers, 1 },
		{ controllers, 1 },
		{ NULL, 0 },
	};
	environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

	bool no_game = false;
	environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

RETRO_EXPORT void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_EXPORT void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
RETRO_EXPORT void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_EXPORT void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_EXPORT void retro_init(void) {
	osk_set_action_handler(lb_osk_action);
	libretro_host_install_modules();
}

RETRO_EXPORT void retro_deinit(void) {
	/* Deliberately no xroar_shutdown(): XRoar's teardown is one-shot and a
	 * frontend may re-init the core later in the same process.  The
	 * emulated machine is reused on the next load. */
	if (lb_state_path[0])
		unlink(lb_state_path);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void lb_set_input_descriptors(void) {
	static const struct retro_input_descriptor desc[] = {
#define PAD(p) \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Joystick left" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick right" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Joystick up" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Joystick down" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Fire 1" }, \
		{ p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Fire 2 (CoCo 3)" }, \
		{ p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Joystick X" }, \
		{ p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Joystick Y" }
		PAD(0),
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "ENTER key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "On-screen keyboard" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "1 key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "2 key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Y key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "N key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "BREAK key" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "CLEAR key" },
		PAD(1),
#undef PAD
		{ 0, 0, 0, 0, NULL },
	};
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)desc);
}

static bool lb_rom_present(const char *sysdir, const char *const *names) {
	static const char *const subdirs[] = { "", "/xroar", "/coco", "/bios" };
	static const char *const exts[] = { ".rom", ".ROM", ".bin", ".BIN", "" };
	char path[4096];
	for (int n = 0; names[n]; n++)
		for (unsigned s = 0; s < 4; s++)
			for (unsigned e = 0; e < 5; e++) {
				snprintf(path, sizeof(path), "%s%s/%s%s", sysdir, subdirs[s], names[n], exts[e]);
				if (access(path, R_OK) == 0)
					return true;
			}
	return false;
}

static void lb_check_roms(const char *sysdir, const char *machine) {
	static const char *const coco3[] = { "coco3", "coco3p", NULL };
	static const char *const bas[] = { "bas13", "bas12", "bas11", "bas10", NULL };
	static const char *const extbas[] = { "extbas11", "extbas10", NULL };
	static const char *const disk[] = { "disk11", "disk10", NULL };
	bool is3 = !strncmp(machine, "coco3", 5);
	char msg[256] = "";

	if (is3 && !lb_rom_present(sysdir, coco3))
		snprintf(msg, sizeof(msg), "XRoar: CoCo 3 ROM (coco3.rom) not found in the system folder");
	else if (!is3 && !lb_rom_present(sysdir, bas))
		snprintf(msg, sizeof(msg), "XRoar: Color BASIC ROM (bas13.rom) not found in the system folder");
	else if (!is3 && !lb_rom_present(sysdir, extbas))
		snprintf(msg, sizeof(msg), "XRoar: Extended BASIC ROM (extbas11.rom) not found; booting without it");
	if (!msg[0] && !lb_rom_present(sysdir, disk))
		lb_log(RETRO_LOG_WARN, "Disk BASIC ROM (disk11.rom) not found; .dsk images will not boot\n");
	if (msg[0]) {
		lb_log(RETRO_LOG_WARN, "%s\n", msg);
		lb_notify(msg, 600);
	}
}

static void lb_setup_state_path(void) {
	if (lb_state_path[0])
		return;
	const char *dir = NULL;
	if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) || !dir)
		environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir);
	if (!dir)
		dir = ".";
	snprintf(lb_state_path, sizeof(lb_state_path), "%s/.xroar_libretro_state.tmp", dir);
}

RETRO_EXPORT bool retro_load_game(const struct retro_game_info *game) {
	const char *path = game && game->path ? game->path : NULL;
	if (!path) {
		lb_log(RETRO_LOG_ERROR, "no content path\n");
		return false;
	}

	unsigned fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		lb_log(RETRO_LOG_ERROR, "XRGB8888 not supported\n");
		return false;
	}

	static const struct retro_keyboard_callback kcb = { lb_keyboard_event };
	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&kcb);
	lb_set_input_descriptors();

	const char *sysdir = NULL;
	environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir);
	if (!sysdir)
		sysdir = ".";

	static char rompath[4096 * 4];
	snprintf(rompath, sizeof(rompath), "%s/xroar:%s/coco:%s:%s/bios",
		 sysdir, sysdir, sysdir, sysdir);

	const char *machine = lb_machine_for(path);
	bool autorun = !lb_var_is("xroar_autorun", "disabled");
	lb_check_roms(sysdir, machine);
	lb_read_runtime_options(false);

	libretro_host_install_modules();

	if (!core_active) {
		char *argv[32];
		int argc = 0;
		argv[argc++] = (char *)"xroar";
		argv[argc++] = (char *)"-no-c";
		argv[argc++] = (char *)"-ui";
		argv[argc++] = (char *)"libretro";
		argv[argc++] = (char *)"-ao";
		argv[argc++] = (char *)"libretro";
		argv[argc++] = (char *)"-default-machine";
		argv[argc++] = (char *)machine;
		argv[argc++] = (char *)"-machine";
		argv[argc++] = (char *)machine;
		argv[argc++] = (char *)"-rompath";
		argv[argc++] = rompath;
		argv[argc++] = (char *)"-fskip";
		argv[argc++] = (char *)"0";
		argv[argc] = NULL;

		if (!xroar_init(argc, argv)) {
			lb_log(RETRO_LOG_ERROR, "xroar_init failed\n");
			return false;
		}
		xroar_init_finish();
		core_active = true;
		/* Force the options to be applied below. */
		opt_tv_input = -2;
		opt_ccr = -1;
		last_opt_border = -2;
		last_opt_zoom = -2;
	} else {
		struct machine_config *mc = machine_config_by_name(machine);
		if (mc) {
			machine_config_complete(mc);
			ui_update_state(-1, ui_tag_machine, mc->id, NULL);
		}
		opt_tv_input = -2;
		opt_ccr = -1;
		last_opt_border = -2;
		last_opt_zoom = -2;
	}

	lb_read_runtime_options(true);
	libretro_host_reset_audio();

	/* Attach the content, autorunning where that makes sense. */
	if (!disk_autorun_load(path, autorun))
		xroar_load_file_by_type(path, autorun ? 1 : 0);

	unsigned std = (unsigned)xroar.machine_config->tv_standard;
	lb_fps = (std == TV_PAL) ? 50.0 : 59.92;

	lb_last_width = libretro_host_video_width();
	lb_last_height = libretro_host_video_height();

	/* Measure a snapshot so we can advertise a state size up front. */
	lb_setup_state_path();
	lb_state_size = 0;
	if (write_snapshot(lb_state_path) == 0) {
		FILE *f = fopen(lb_state_path, "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			lb_state_size = (size_t)ftell(f) + 65536;
			fclose(f);
		}
		unlink(lb_state_path);
	}
	if (lb_state_size == 0)
		lb_state_size = 4 * 1024 * 1024;

	memset(lb_held_char, 0, sizeof(lb_held_char));
	memset(lb_padkey_down, 0, sizeof(lb_padkey_down));
	osk_reset();
	autocrop_reset();
	smartcrop_reset();
	lb_out_w = lb_out_h = 0;
	game_loaded = true;
	lb_log(RETRO_LOG_INFO, "machine %s, content %s\n", machine, path);
	return true;
}

RETRO_EXPORT void retro_unload_game(void) {
	game_loaded = false;
}

RETRO_EXPORT unsigned retro_get_region(void) {
	return (lb_fps < 55.0) ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

RETRO_EXPORT void retro_reset(void) {
	xroar_hard_reset();
}

RETRO_EXPORT void retro_run(void) {
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		lb_read_runtime_options(true);

	input_poll_cb();
	lb_poll_osk();
	lb_poll_pad(0);
	lb_poll_pad(1);

	for (int i = 0; i < LB_MAX_SLICES; i++) {
		xroar_run(EVENT_MS(1));
		if (libretro_host_take_frame())
			break;
	}

	lb_last_width = libretro_host_video_width();
	lb_last_height = libretro_host_video_height();
	const uint32_t *fb = libretro_host_video_buffer();
	unsigned pitch = lb_last_width;

	/* 1. Border mode: which part of the emulator's picture to use. */
	unsigned cx = 0, cy = 0, cw = lb_last_width, ch = lb_last_height;
	if (rt_border == BORDER_SMART)
		smartcrop_update(fb, lb_last_width, lb_last_height, pitch, &cx, &cy, &cw, &ch);
	else if (rt_border == BORDER_AUTO)
		autocrop_update(fb, lb_last_width, lb_last_height, pitch, &cx, &cy, &cw, &ch);

	/* 2. Zoom: a rectangle sw x sh around the centre of that part.  Zoomed
	 * in it is smaller (RetroArch scales it up); zoomed out it is bigger
	 * and takes in more of the picture, padded with black beyond it. */
	unsigned zoom = zoom_levels[rt_zoom];
	unsigned sw = (cw * 100 / zoom) & ~1u, sh = ch * 100 / zoom;
	if (sw > LB_ZOOM_MAX_W) sw = LB_ZOOM_MAX_W & ~1u;
	if (sh > LB_ZOOM_MAX_H) sh = LB_ZOOM_MAX_H;
	if (sw < 16) sw = 16;
	if (sh < 16) sh = 16;
	int sx = ((int)cx + ((int)cw - (int)sw) / 2) & ~1;
	int sy = (int)cy + ((int)ch - (int)sh) / 2;
	const uint32_t *frame;
	unsigned fpitch;
	if (sx >= 0 && sy >= 0 && sx + (int)sw <= (int)lb_last_width &&
	    sy + (int)sh <= (int)lb_last_height) {
		frame = fb + (size_t)sy * pitch + sx;
		fpitch = pitch;
	} else {
		if (!lb_zoom)
			lb_zoom = malloc((size_t)LB_ZOOM_MAX_W * LB_ZOOM_MAX_H * 4);
		if (lb_zoom) {
			for (unsigned y = 0; y < sh; y++) {
				uint32_t *d = lb_zoom + (size_t)y * sw;
				int py = sy + (int)y;
				for (unsigned x = 0; x < sw; x++) {
					int px = sx + (int)x;
					d[x] = (py < 0 || py >= (int)lb_last_height ||
						px < 0 || px >= (int)lb_last_width)
						? 0 : fb[(size_t)py * pitch + px];
				}
			}
			frame = lb_zoom;
			fpitch = sw;
		} else {
			sw = cw; sh = ch;
			frame = fb + (size_t)cy * pitch + cx;
			fpitch = pitch;
		}
	}
	unsigned fw = sw, fh = sh;

	/* 3. On-screen keyboard: draw it over a copy of the picture, doubled
	 * when there is room so its letters are sharper. */
	if (osk_active()) {
		lb_update_status();
		if (!lb_out)
			lb_out = malloc((size_t)LB_OUT_MAX_W * LB_OUT_MAX_H * 4);
		if (lb_out) {
			bool dbl = sw * 2 <= LB_OUT_MAX_W && sh * 2 <= LB_OUT_MAX_H;
			if (dbl) {
				fw = sw * 2;
				fh = sh * 2;
				for (unsigned y = 0; y < sh; y++) {
					uint32_t *d = lb_out + (size_t)y * 2 * fw;
					const uint32_t *sr = frame + (size_t)y * fpitch;
					for (unsigned x = 0; x < sw; x++)
						d[2 * x] = d[2 * x + 1] = sr[x];
					memcpy(d + fw, d, (size_t)fw * 4);
				}
			} else {
				for (unsigned y = 0; y < sh; y++)
					memcpy(lb_out + (size_t)y * fw, frame + (size_t)y * fpitch,
					       (size_t)fw * 4);
			}
			osk_draw(lb_out, fw, fh, fw);
			frame = lb_out;
			fpitch = fw;
		}
	}

	if (fw != lb_out_w || fh != lb_out_h) {
		lb_out_w = fw;
		lb_out_h = fh;
		/* Picture lines show twice as tall as pixels are wide (640x240 is
		 * 4:3), whatever the crop or zoom. */
		struct retro_game_geometry geom = {
			.base_width = fw,
			.base_height = fh,
			.max_width = LB_OUT_MAX_W,
			.max_height = LB_OUT_MAX_H,
			.aspect_ratio = (float)((double)sw / (double)sh * 0.5),
		};
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
	}

	video_cb(frame, fw, fh, fpitch * 4);

	/* Hand over whatever audio the emulated frame produced; RetroArch's
	 * dynamic rate control absorbs the small drift. */
	static int16_t audio_buf[4096 * 2];
	size_t frames;
	while ((frames = libretro_host_audio_take(audio_buf, 4096)) > 0)
		audio_cb(audio_buf, frames);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Save states go through XRoar's own snapshot format via a temporary file.
 * That makes them slow enough that rewind is not recommended. */

RETRO_EXPORT size_t retro_serialize_size(void) {
	return lb_state_size;
}

RETRO_EXPORT bool retro_serialize(void *data, size_t size) {
	if (!core_active || !game_loaded)
		return false;
	if (write_snapshot(lb_state_path) != 0)
		return false;
	FILE *f = fopen(lb_state_path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len < 0 || (size_t)len + 4 > size) {
		fclose(f);
		unlink(lb_state_path);
		lb_log(RETRO_LOG_ERROR, "snapshot too large (%ld bytes)\n", len);
		return false;
	}
	/* 4-byte little-endian length prefix, then the snapshot. */
	uint8_t *p = data;
	p[0] = (uint8_t)len; p[1] = (uint8_t)(len >> 8);
	p[2] = (uint8_t)(len >> 16); p[3] = (uint8_t)(len >> 24);
	size_t n = fread(p + 4, 1, (size_t)len, f);
	fclose(f);
	unlink(lb_state_path);
	if (n != (size_t)len)
		return false;
	memset(p + 4 + n, 0, size - 4 - n);
	return true;
}

RETRO_EXPORT bool retro_unserialize(const void *data, size_t size) {
	if (!core_active || !game_loaded || size < 4)
		return false;
	const uint8_t *p = data;
	size_t len = (size_t)p[0] | ((size_t)p[1] << 8) |
		     ((size_t)p[2] << 16) | ((size_t)p[3] << 24);
	if (len == 0 || len > size - 4)
		return false;
	FILE *f = fopen(lb_state_path, "wb");
	if (!f)
		return false;
	size_t n = fwrite(p + 4, 1, len, f);
	fclose(f);
	bool ok = (n == len) && read_snapshot(lb_state_path) == 0;
	unlink(lb_state_path);
	if (ok)
		libretro_host_connect_joysticks();
	return ok;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

RETRO_EXPORT void *retro_get_memory_data(unsigned id) {
	(void)id;
	return NULL;
}

RETRO_EXPORT size_t retro_get_memory_size(unsigned id) {
	(void)id;
	return 0;
}

RETRO_EXPORT void retro_set_controller_port_device(unsigned port, unsigned device) {
	(void)port;
	(void)device;
}

RETRO_EXPORT bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
	(void)type;
	(void)info;
	(void)num;
	return false;
}

RETRO_EXPORT void retro_cheat_reset(void) {}
RETRO_EXPORT void retro_cheat_set(unsigned index, bool enabled, const char *code) {
	(void)index; (void)enabled; (void)code;
}
