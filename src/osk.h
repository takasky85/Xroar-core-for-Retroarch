/* On-screen CoCo keyboard.  GPL-3.0-or-later. */

#ifndef XROAR_LIBRETRO_OSK_H_
#define XROAR_LIBRETRO_OSK_H_

#include <stdbool.h>
#include <stdint.h>

/* Button bits passed to osk_frame(). */
#define OSK_UP        (1u << 0)
#define OSK_DOWN      (1u << 1)
#define OSK_LEFT      (1u << 2)
#define OSK_RIGHT     (1u << 3)
#define OSK_PRESS     (1u << 4)  /* press the selected key */
#define OSK_SHIFT     (1u << 5)  /* toggle SHIFT */
#define OSK_MOVE      (1u << 6)  /* move the keyboard to the top/bottom */
#define OSK_ENTER     (1u << 7)
#define OSK_BACKSPACE (1u << 8)

bool osk_active(void);
void osk_toggle(void);
void osk_reset(void);
void osk_frame(unsigned buttons);
/* Draw over an XRGB8888 picture; pitch is in pixels. */
void osk_draw(uint32_t *buf, unsigned w, unsigned h, unsigned pitch);

#endif
