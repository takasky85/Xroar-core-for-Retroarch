/* Smart border: standard CoCo screen area, grows for bigger modes, never
 * shrinks until reset.  GPL-3.0-or-later. */

#ifndef XROAR_LIBRETRO_SMARTCROP_H_
#define XROAR_LIBRETRO_SMARTCROP_H_

#include <stdint.h>

void smartcrop_reset(void);

/* Feed one XRGB8888 frame (pitch in pixels) and get the rectangle to show. */
void smartcrop_update(const uint32_t *fb, unsigned w, unsigned h, unsigned pitch,
		     unsigned *x, unsigned *y, unsigned *cw, unsigned *ch);

#endif
