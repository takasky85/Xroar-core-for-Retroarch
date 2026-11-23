/** \file
 *
 *  \brief On-screen CoCo keyboard for the XRoar libretro core.
 *
 *  Drawn over the emulator picture and driven from the gamepad, so a TV
 *  remote or pad can type BASIC commands.  Labels use the MC6847's own
 *  character set.
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "auto_kbd.h"
#include "dkbd.h"
#include "keyboard.h"
#include "mc6847/font-6847.h"
#include "xroar.h"

#include "osk.h"

enum osk_kind { K_KEY, K_SHIFT, K_CTRL, K_ALT, K_MACRO, K_CLOSE };

struct osk_key {
	const char *label;
	uint8_t units;      /* width in grid units */
	uint8_t kind;
	int8_t dscan;
	const char *macro;  /* typed through XRoar's auto keyboard */
};

#define KEY(l, d)    { l, 1, K_KEY, d, NULL }
#define WKEY(l, u, d) { l, u, K_KEY, d, NULL }
#define MACRO(l, u, m) { l, u, K_MACRO, DSCAN_INVALID, m }

#define OSK_COLS 13

static const struct osk_key row0[] = {
	KEY("1", DSCAN_1), KEY("2", DSCAN_2), KEY("3", DSCAN_3), KEY("4", DSCAN_4),
	KEY("5", DSCAN_5), KEY("6", DSCAN_6), KEY("7", DSCAN_7), KEY("8", DSCAN_8),
	KEY("9", DSCAN_9), KEY("0", DSCAN_0), KEY(":", DSCAN_COLON), KEY("-", DSCAN_MINUS),
	KEY("BRK", DSCAN_BREAK),
};
static const struct osk_key row1[] = {
	KEY("Q", DSCAN_Q), KEY("W", DSCAN_W), KEY("E", DSCAN_E), KEY("R", DSCAN_R),
	KEY("T", DSCAN_T), KEY("Y", DSCAN_Y), KEY("U", DSCAN_U), KEY("I", DSCAN_I),
	KEY("O", DSCAN_O), KEY("P", DSCAN_P), KEY("@", DSCAN_AT), KEY("CLR", DSCAN_CLEAR),
	KEY("DEL", DSCAN_BACKSPACE),
};
static const struct osk_key row2[] = {
	KEY("A", DSCAN_A), KEY("S", DSCAN_S), KEY("D", DSCAN_D), KEY("F", DSCAN_F),
	KEY("G", DSCAN_G), KEY("H", DSCAN_H), KEY("J", DSCAN_J), KEY("K", DSCAN_K),
	KEY("L", DSCAN_L), KEY(";", DSCAN_SEMICOLON), WKEY("ENTER", 3, DSCAN_ENTER),
};
static const struct osk_key row3[] = {
	{ "SHIFT", 2, K_SHIFT, DSCAN_SHIFT, NULL },
	KEY("Z", DSCAN_Z), KEY("X", DSCAN_X), KEY("C", DSCAN_C), KEY("V", DSCAN_V),
	KEY("B", DSCAN_B), KEY("N", DSCAN_N), KEY("M", DSCAN_M), KEY(",", DSCAN_COMMA),
	KEY(".", DSCAN_FULL_STOP), KEY("/", DSCAN_SLASH), KEY("UP", DSCAN_UP),
};
static const struct osk_key row4[] = {
	{ "CTRL", 2, K_CTRL, DSCAN_CTRL, NULL },
	{ "ALT", 1, K_ALT, DSCAN_ALT, NULL },
	KEY("F1", DSCAN_F1), KEY("F2", DSCAN_F2),
	WKEY("SPACE", 3, DSCAN_SPACE),
	KEY("LT", DSCAN_LEFT), KEY("RT", DSCAN_RIGHT), KEY("DN", DSCAN_DOWN),
	{ "HIDE", 2, K_CLOSE, DSCAN_INVALID, NULL },
};
static const struct osk_key row5[] = {
	MACRO("DIR", 2, "\\025DIR\\r"),
	MACRO("RUN", 2, "RUN\\r"),
	MACRO("CLOAD", 2, "\\025CLOAD\\r"),
	MACRO("EXEC", 2, "EXEC\\r"),
	MACRO("DOS", 2, "\\025DOS\\r"),
	MACRO("\"", 1, "\""),
	MACRO("=", 1, "="),
	MACRO("$", 1, "$"),
};

struct osk_row {
	const struct osk_key *keys;
	unsigned n;
};

#define ROW(r) { r, sizeof(r) / sizeof(r[0]) }
static const struct osk_row rows[] = {
	ROW(row0), ROW(row1), ROW(row2), ROW(row3), ROW(row4), ROW(row5),
};
#define OSK_ROWS (sizeof(rows) / sizeof(rows[0]))

static bool active;
static bool at_top;
static unsigned cur_row, cur_col;
static bool shift_lock, ctrl_lock, alt_lock;

/* Key currently held down through the keyboard, and how many more frames to
 * keep it down even after the button is released (BASIC polls the keyboard
 * only so often). */
static int8_t held_dscan = DSCAN_INVALID;
static int held_frames;
static bool held_button;

static unsigned prev_buttons;
static unsigned repeat_dir, repeat_count;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void set_mod(int8_t dscan, bool on) {
	struct keyboard_interface *ki = xroar.keyboard_interface;
	if (!ki)
		return;
	if (on)
		keyboard_press(ki, dscan);
	else
		keyboard_release(ki, dscan);
}

static void release_held(void) {
	if (held_dscan != DSCAN_INVALID && xroar.keyboard_interface)
		keyboard_release(xroar.keyboard_interface, held_dscan);
	held_dscan = DSCAN_INVALID;
	held_frames = 0;
}

static void clear_mods(void) {
	if (shift_lock) set_mod(DSCAN_SHIFT, false);
	if (ctrl_lock) set_mod(DSCAN_CTRL, false);
	if (alt_lock) set_mod(DSCAN_ALT, false);
	shift_lock = ctrl_lock = alt_lock = false;
}

bool osk_active(void) {
	return active;
}

void osk_reset(void) {
	release_held();
	clear_mods();
	active = false;
	held_button = false;
	prev_buttons = 0;
}

void osk_toggle(void) {
	if (active) {
		release_held();
		clear_mods();
		active = false;
	} else {
		active = true;
		/* Whatever buttons are down now opened the keyboard; ignore them
		 * until released. */
		prev_buttons = ~0u;
	}
}

static unsigned col_of_unit(unsigned row, unsigned unit) {
	unsigned u = 0;
	for (unsigned i = 0; i < rows[row].n; i++) {
		u += rows[row].keys[i].units;
		if (unit < u)
			return i;
	}
	return rows[row].n - 1;
}

static unsigned unit_of_col(unsigned row, unsigned col) {
	unsigned u = 0;
	for (unsigned i = 0; i < col; i++)
		u += rows[row].keys[i].units;
	/* Middle of the key, so moving up/down lands on the key above. */
	return u + (rows[row].keys[col].units - 1) / 2;
}

static void move(unsigned dir) {
	switch (dir) {
	case OSK_LEFT:
		cur_col = cur_col ? cur_col - 1 : rows[cur_row].n - 1;
		break;
	case OSK_RIGHT:
		cur_col = (cur_col + 1) % rows[cur_row].n;
		break;
	case OSK_UP:
	case OSK_DOWN: {
		unsigned unit = unit_of_col(cur_row, cur_col);
		if (dir == OSK_UP)
			cur_row = cur_row ? cur_row - 1 : OSK_ROWS - 1;
		else
			cur_row = (cur_row + 1) % OSK_ROWS;
		cur_col = col_of_unit(cur_row, unit);
		break;
	}
	}
}

static void activate(void) {
	const struct osk_key *k = &rows[cur_row].keys[cur_col];
	struct keyboard_interface *ki = xroar.keyboard_interface;
	switch (k->kind) {
	case K_KEY:
		if (!ki)
			return;
		release_held();
		keyboard_press(ki, k->dscan);
		held_dscan = k->dscan;
		held_frames = 4;
		held_button = true;
		break;
	case K_SHIFT:
		shift_lock = !shift_lock;
		set_mod(DSCAN_SHIFT, shift_lock);
		break;
	case K_CTRL:
		ctrl_lock = !ctrl_lock;
		set_mod(DSCAN_CTRL, ctrl_lock);
		break;
	case K_ALT:
		alt_lock = !alt_lock;
		set_mod(DSCAN_ALT, alt_lock);
		break;
	case K_MACRO:
		if (xroar.auto_kbd)
			ak_parse_type_string(xroar.auto_kbd, k->macro);
		break;
	case K_CLOSE:
		osk_toggle();
		break;
	}
}

void osk_frame(unsigned buttons) {
	if (!active) {
		prev_buttons = buttons;
		return;
	}

	unsigned pressed = buttons & ~prev_buttons;
	prev_buttons = buttons;

	/* Held key: keep it down while B is held, plus a few frames. */
	if (held_dscan != DSCAN_INVALID) {
		if (held_button && !(buttons & OSK_PRESS))
			held_button = false;
		if (!held_button) {
			if (held_frames > 0)
				held_frames--;
			else
				release_held();
		}
	}

	/* D-pad with auto-repeat. */
	unsigned dirs = buttons & (OSK_UP | OSK_DOWN | OSK_LEFT | OSK_RIGHT);
	unsigned dir = 0;
	if (pressed & dirs) {
		dir = pressed & dirs;
		repeat_dir = dir;
		repeat_count = 0;
	} else if (dirs && dirs == repeat_dir) {
		if (++repeat_count > 15 && (repeat_count % 4) == 0)
			dir = repeat_dir;
	} else {
		repeat_dir = 0;
	}
	if (dir & OSK_UP) move(OSK_UP);
	else if (dir & OSK_DOWN) move(OSK_DOWN);
	if (dir & OSK_LEFT) move(OSK_LEFT);
	else if (dir & OSK_RIGHT) move(OSK_RIGHT);

	if (pressed & OSK_PRESS)
		activate();
	if (pressed & OSK_SHIFT) {
		shift_lock = !shift_lock;
		set_mod(DSCAN_SHIFT, shift_lock);
	}
	if (pressed & OSK_MOVE)
		at_top = !at_top;
	if ((pressed & OSK_ENTER) && xroar.keyboard_interface) {
		release_held();
		keyboard_press(xroar.keyboard_interface, DSCAN_ENTER);
		held_dscan = DSCAN_ENTER;
		held_frames = 4;
		held_button = false;
	}
	if ((pressed & OSK_BACKSPACE) && xroar.keyboard_interface) {
		release_held();
		keyboard_press(xroar.keyboard_interface, DSCAN_BACKSPACE);
		held_dscan = DSCAN_BACKSPACE;
		held_frames = 4;
		held_button = false;
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Drawing. */

#define C_PANEL   0x101820u
#define C_KEY     0x384858u
#define C_KEYDOWN 0x70a040u
#define C_CURSOR  0xf0c020u
#define C_TEXT    0xf0f0f0u
#define C_TEXTCUR 0x101010u

/* Keyboard opacity, 0..256. */
static unsigned opacity = 128;

void osk_set_opacity(unsigned percent) {
	if (percent > 100)
		percent = 100;
	opacity = percent * 256 / 100;
}

/* Blend colour c over the picture with alpha a (0..256). */
static void blend(uint32_t *buf, unsigned pitch, int x, int y, int w, int h, uint32_t c, unsigned a) {
	if (a >= 256) {
		for (int j = 0; j < h; j++) {
			uint32_t *p = buf + (size_t)(y + j) * pitch + x;
			for (int i = 0; i < w; i++)
				p[i] = c;
		}
		return;
	}
	uint32_t crb = c & 0xff00ffu, cg = c & 0x00ff00u;
	unsigned na = 256 - a;
	for (int j = 0; j < h; j++) {
		uint32_t *p = buf + (size_t)(y + j) * pitch + x;
		for (int i = 0; i < w; i++) {
			uint32_t d = p[i];
			uint32_t rb = (((d & 0xff00ffu) * na + crb * a) >> 8) & 0xff00ffu;
			uint32_t g = (((d & 0x00ff00u) * na + cg * a) >> 8) & 0x00ff00u;
			p[i] = rb | g;
		}
	}
}

static void fill(uint32_t *buf, unsigned pitch, int x, int y, int w, int h, uint32_t c) {
	for (int j = 0; j < h; j++) {
		uint32_t *p = buf + (size_t)(y + j) * pitch + x;
		for (int i = 0; i < w; i++)
			p[i] = c;
	}
}

/* Character code in the 6847 font for an ASCII character. */
static int font_index(char c) {
	if (c >= '@' && c <= '_')
		return c - '@';
	if (c >= ' ' && c <= '?')
		return c - ' ' + 32;
	return 0;
}

/* The 6847 glyphs sit in rows 3..9 of 12, 6 pixels wide in an 8-pixel
 * cell.  Draw rows 2..10 to keep a little space. */
/* Draw text with each glyph scaled to gw x gh pixels (any size, nearest
 * neighbour), so the keyboard keeps the same size on screen whatever the
 * picture size. */
static void draw_text_1(uint32_t *buf, unsigned pitch, int x, int y, int gw, int gh,
			const char *s, uint32_t c) {
	for (; *s; s++, x += gw) {
		const uint8_t *g = &font_6847[font_index(*s) * 12];
		for (int py = 0; py < gh; py++) {
			uint8_t bits = g[3 + py * 7 / gh];
			uint32_t *p = buf + (size_t)(y + py) * pitch + x;
			for (int px = 0; px < gw; px++) {
				if (bits & (0x40 >> (px * 7 / gw)))
					p[px] = c;
			}
		}
	}
}

/* Text with a dark shadow so it stays readable over the picture. */
static void draw_text(uint32_t *buf, unsigned pitch, int x, int y, int gw, int gh,
		      const char *s, uint32_t c, bool shadow) {
	if (shadow) {
		int d = gh / 8 > 0 ? gh / 8 : 1;
		draw_text_1(buf, pitch, x + d, y + d, gw, gh, s, 0x000000u);
	}
	draw_text_1(buf, pitch, x, y, gw, gh, s, c);
}

static bool key_lit(const struct osk_key *k) {
	switch (k->kind) {
	case K_SHIFT: return shift_lock;
	case K_CTRL: return ctrl_lock;
	case K_ALT: return alt_lock;
	case K_KEY: return held_dscan != DSCAN_INVALID && k->dscan == held_dscan;
	default: return false;
	}
}

void osk_draw(uint32_t *buf, unsigned w, unsigned h, unsigned pitch) {
	if (!active || w < 200 || h < 100)
		return;

	/* Sizes are fractions of the picture, so the keyboard looks the same
	 * on screen whatever the picture size.  The picture is shown at about
	 * 4:3, and a glyph is 7x7 dots with its spacing built in. */
	int key_h = (int)h * 56 / 1000;
	if (key_h < 9)
		key_h = 9;
	int gh = key_h * 62 / 100;
	int gw = (int)w * 21 / 1000;
	/* Whole dots look cleanest: round glyph sizes to multiples of 7. */
	gh = gh >= 7 ? (gh + 3) / 7 * 7 : 7;
	gw = gw >= 7 ? (gw + 3) / 7 * 7 : 7;
	int gap = 1;
	int margin = (int)h / 100 > 2 ? (int)h / 100 : 2;
	int panel_h = (int)OSK_ROWS * (key_h + gap) + 2 * margin;
	int unit_w = ((int)w - 2 * margin) / OSK_COLS;
	int panel_w = unit_w * OSK_COLS + 2 * margin;
	int px = ((int)w - panel_w) / 2;
	int py = at_top ? 2 : (int)h - panel_h - 2;
	if (py < 0)
		py = 0;

	/* Keep 2 pixels clear at the far edge so the text shadow fits. */
	if (py + panel_h > (int)h - 1)
		py = (int)h - 1 - panel_h;
	blend(buf, pitch, px, py, panel_w, panel_h, C_PANEL, opacity / 2);

	for (unsigned r = 0; r < OSK_ROWS; r++) {
		int x = px + margin;
		int y = py + margin + (int)r * (key_h + gap);
		for (unsigned i = 0; i < rows[r].n; i++) {
			const struct osk_key *k = &rows[r].keys[i];
			int kw = unit_w * k->units - gap;
			bool cur = (r == cur_row && i == cur_col);
			uint32_t bg = cur ? C_CURSOR : key_lit(k) ? C_KEYDOWN : C_KEY;
			unsigned a = opacity;
			if (cur || key_lit(k))
				a = opacity < 200 ? 200 : opacity;
			blend(buf, pitch, x, y, kw, key_h, bg, a);
			int tw = (int)strlen(k->label) * gw;
			int tx = x + (kw - tw) / 2;
			if (tx < x)
				tx = x;
			draw_text(buf, pitch, tx, y + (key_h - gh) / 2, gw, gh, k->label,
				  cur ? C_TEXTCUR : C_TEXT, !cur && opacity < 256);
			x += unit_w * k->units;
		}
	}
}
