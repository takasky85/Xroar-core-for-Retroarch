/** \file
 *
 *  \brief Smart border for the XRoar libretro core.
 *
 *  The emulator renders the full underscan picture (736x276, two picture
 *  pixels per CoCo pixel across).  The core shows only the standard CoCo
 *  display area, 256x192, and if a game ever draws outside it (the CoCo 3's
 *  taller or wider GIME modes, or graphics in the border) it widens once to
 *  the next standard size that fits and stays there until the next game.
 *  It never shrinks, so the picture does not jump around.
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

#include "smartcrop.h"

/* Centre of the 256x192 display area within the 736x276 underscan picture
 * (measured: it sits at x 112..624, y 38..230). */
#define CENTRE_X2  (2 * 368)
#define CENTRE_Y2  (2 * 134)

/* Standard sizes in picture pixels: 256 or 320 CoCo pixels across, and the
 * GIME's 192/200/210/225 line modes. */
static const unsigned widths[] = { 512, 640 };
static const unsigned heights[] = { 192, 200, 210, 225 };
#define N_W (sizeof(widths) / sizeof(widths[0]))
#define N_H (sizeof(heights) / sizeof(heights[0]))

#define CHECK_EVERY  4   /* frames between scans */
#define CONFIRM      2   /* scans in a row before growing */
#define SLACK        8   /* pixels of composite blur allowed past an edge */
#define TOLERANCE    24  /* per-channel difference that counts as content */
#define SETTLE       120 /* frames to ignore after loading (machine start-up) */

static unsigned wi, hi;          /* current size indexes */
static unsigned want_wi, want_hi, want_count;
static unsigned last_fw, last_fh;
static unsigned frame_count;

void smartcrop_reset(void) {
	wi = hi = 0;
	want_count = 0;
	frame_count = 0;
	last_fw = last_fh = 0;
}

static bool differs(uint32_t a, uint32_t b) {
	int dr = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
	int dg = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
	int db = (int)(a & 0xff) - (int)(b & 0xff);
	return abs(dr) > TOLERANCE || abs(dg) > TOLERANCE || abs(db) > TOLERANCE;
}

/* Rectangle of size (w,h) centred on the display area, clipped to the
 * picture. */
static void rect_for(unsigned fw, unsigned fh, unsigned w, unsigned h,
		     unsigned *x, unsigned *y, unsigned *cw, unsigned *ch) {
	int x0 = ((int)CENTRE_X2 - (int)w) / 2;
	int y0 = ((int)CENTRE_Y2 - (int)h) / 2;
	/* Scale the centre if the picture is not the usual underscan size. */
	if (fw != 736 || fh != 276) {
		x0 = ((int)fw - (int)w) / 2;
		y0 = ((int)fh - (int)h) / 2;
	}
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if ((unsigned)x0 + w > fw) w = fw - (unsigned)x0;
	if ((unsigned)y0 + h > fh) h = fh - (unsigned)y0;
	*x = (unsigned)x0 & ~1u;
	*y = (unsigned)y0;
	*cw = w;
	*ch = h;
}

/* Is there content in the picture outside rectangle r (ignoring SLACK
 * pixels of blur) but inside the largest standard rectangle? */
static void scan(const uint32_t *fb, unsigned fw, unsigned fh, unsigned pitch,
		 bool *need_w, bool *need_h) {
	unsigned x, y, w, h, bx, by, bw, bh;
	rect_for(fw, fh, widths[wi], heights[hi], &x, &y, &w, &h);
	rect_for(fw, fh, widths[N_W - 1], heights[N_H - 1], &bx, &by, &bw, &bh);
	uint32_t border = fb[0];
	*need_w = *need_h = false;

	/* Rows above and below the current area, within the biggest area. */
	for (unsigned yy = by; yy < by + bh; yy++) {
		bool outside_y = yy + SLACK < y || yy >= y + h + SLACK;
		const uint32_t *row = fb + (size_t)yy * pitch;
		for (unsigned xx = bx; xx < bx + bw; xx++) {
			bool outside_x = xx + SLACK < x || xx >= x + w + SLACK;
			if (!outside_x && !outside_y)
				continue;
			if (!differs(row[xx], border))
				continue;
			if (outside_y && xx + SLACK >= x && xx < x + w + SLACK)
				*need_h = true;
			else if (outside_x && !outside_y)
				*need_w = true;
			else {
				*need_w = true;
				*need_h = true;
			}
		}
	}
}

void smartcrop_update(const uint32_t *fb, unsigned w, unsigned h, unsigned pitch,
		     unsigned *x, unsigned *y, unsigned *cw, unsigned *ch) {
	if (w != last_fw || h != last_fh) {
		smartcrop_reset();
		last_fw = w;
		last_fh = h;
	}

	if (frame_count++ % CHECK_EVERY == 0 && frame_count > SETTLE && (wi < N_W - 1 || hi < N_H - 1)) {
		bool need_w, need_h;
		scan(fb, w, h, pitch, &need_w, &need_h);
		unsigned nwi = wi, nhi = hi;
		if (need_w && nwi < N_W - 1) nwi++;
		if (need_h && nhi < N_H - 1) nhi++;
		if (nwi != wi || nhi != hi) {
			if (want_count && nwi == want_wi && nhi == want_hi) {
				if (++want_count >= CONFIRM) {
					wi = nwi;
					hi = nhi;
					want_count = 0;
				}
			} else {
				want_wi = nwi;
				want_hi = nhi;
				want_count = 1;
			}
		} else {
			want_count = 0;
		}
	}

	rect_for(w, h, widths[wi], heights[hi], x, y, cw, ch);
}
