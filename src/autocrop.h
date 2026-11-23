/* Automatic border cropping.  GPL-3.0-or-later. */

#ifndef XROAR_LIBRETRO_AUTOCROP_H_
#define XROAR_LIBRETRO_AUTOCROP_H_

#include <stdint.h>

void autocrop_reset(void);

/* Feed one XRGB8888 frame (pitch in pixels) and get the rectangle to show. */
void autocrop_update(const uint32_t *fb, unsigned w, unsigned h, unsigned pitch,
		     unsigned *x, unsigned *y, unsigned *cw, unsigned *ch);

#endif
