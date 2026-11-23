/** \file
 *
 *  \brief Automatic border cropping for the XRoar libretro core.
 *
 *  Like VICE's auto-crop: the emulator renders the full picture with its
 *  border, and this finds the rectangle that holds anything other than the
 *  border colour.  The crop grows at once when the picture needs more room
 *  and shrinks only after the smaller picture has been stable for a couple
 *  of seconds, so it does not flicker between screens.
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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "autocrop.h"

#define CHECK_EVERY   4   /* frames between scans */
#define SHRINK_AFTER  30  /* stable scans (~2 s) before shrinking */
#define MIN_W         64
#define MIN_H         48
#define TOLERANCE     24  /* per-channel difference that counts as content */

static unsigned cur_x, cur_y, cur_w, cur_h;
static unsigned last_fw, last_fh;
static bool have_crop;
static unsigned frame_count;
static unsigned shrink_count;
static unsigned pend_x0, pend_y0, pend_x1, pend_y1;

void autocrop_reset(void) {
	have_crop = false;
	frame_count = 0;
	shrink_count = 0;
	last_fw = last_fh = 0;
}

static bool differs(uint32_t a, uint32_t b) {
	int dr = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
	int dg = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
	int db = (int)(a & 0xff) - (int)(b & 0xff);
	return abs(dr) > TOLERANCE || abs(dg) > TOLERANCE || abs(db) > TOLERANCE;
}

static bool row_has_content(const uint32_t *row, unsigned w, uint32_t border) {
	for (unsigned x = 0; x < w; x++)
		if (differs(row[x], border))
			return true;
	return false;
}

/* Finds the content rectangle [x0,x1) x [y0,y1).  Returns false if the
 * picture is (nearly) all border. */
static bool scan(const uint32_t *fb, unsigned w, unsigned h, unsigned pitch,
		 unsigned *x0, unsigned *y0, unsigned *x1, unsigned *y1) {
	uint32_t border = fb[0];
	unsigned top = 0, bot = h;
	while (top < h && !row_has_content(fb + (size_t)top * pitch, w, border))
		top++;
	if (top == h)
		return false;
	while (bot > top && !row_has_content(fb + (size_t)(bot - 1) * pitch, w, border))
		bot--;

	unsigned left = w, right = 0;
	for (unsigned y = top; y < bot; y++) {
		const uint32_t *row = fb + (size_t)y * pitch;
		unsigned x = 0;
		while (x < left && !differs(row[x], border))
			x++;
		if (x < left)
			left = x;
		x = w;
		while (x > right && !differs(row[x - 1], border))
			x--;
		if (x > right)
			right = x;
	}
	if (right <= left)
		return false;

	/* Even edges keep the doubled-pixel artifact patterns intact. */
	*x0 = left & ~1u;
	*x1 = (right + 1) & ~1u;
	if (*x1 > w) *x1 = w;
	*y0 = top;
	*y1 = bot;
	return (*x1 - *x0) >= MIN_W && (*y1 - *y0) >= MIN_H;
}

void autocrop_update(const uint32_t *fb, unsigned w, unsigned h, unsigned pitch,
		     unsigned *x, unsigned *y, unsigned *cw, unsigned *ch) {
	if (w != last_fw || h != last_fh) {
		autocrop_reset();
		last_fw = w;
		last_fh = h;
	}

	if (frame_count++ % CHECK_EVERY == 0) {
		unsigned x0, y0, x1, y1;
		if (scan(fb, w, h, pitch, &x0, &y0, &x1, &y1)) {
			if (!have_crop) {
				cur_x = x0; cur_y = y0; cur_w = x1 - x0; cur_h = y1 - y0;
				have_crop = true;
				shrink_count = 0;
			} else {
				unsigned cx1 = cur_x + cur_w, cy1 = cur_y + cur_h;
				bool grow = x0 < cur_x || y0 < cur_y || x1 > cx1 || y1 > cy1;
				if (grow) {
					/* Grow to cover both at once. */
					if (x0 < cur_x) cur_x = x0;
					if (y0 < cur_y) cur_y = y0;
					if (x1 > cx1) cx1 = x1;
					if (y1 > cy1) cy1 = y1;
					cur_w = cx1 - cur_x;
					cur_h = cy1 - cur_y;
					shrink_count = 0;
				} else if (x0 != cur_x || y0 != cur_y || x1 != cx1 || y1 != cy1) {
					/* Smaller: wait until it is the same for a while. */
					if (shrink_count && x0 == pend_x0 && y0 == pend_y0 &&
					    x1 == pend_x1 && y1 == pend_y1) {
						if (++shrink_count >= SHRINK_AFTER) {
							cur_x = x0; cur_y = y0;
							cur_w = x1 - x0; cur_h = y1 - y0;
							shrink_count = 0;
						}
					} else {
						pend_x0 = x0; pend_y0 = y0;
						pend_x1 = x1; pend_y1 = y1;
						shrink_count = 1;
					}
				} else {
					shrink_count = 0;
				}
			}
		}
	}

	if (!have_crop) {
		*x = 0; *y = 0; *cw = w; *ch = h;
		return;
	}
	*x = cur_x; *y = cur_y; *cw = cur_w; *ch = cur_h;
}
