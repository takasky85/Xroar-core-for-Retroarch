/** \file
 *
 *  \brief Host-side glue between the libretro core and XRoar's core.
 *
 *  XRoar expects a frontend to provide three module families: a UI (which owns
 *  a video interface), an audio output module, and optionally joystick
 *  submodules.  This file implements minimal versions of all three that talk
 *  to libretro instead of a desktop toolkit.
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

#ifndef XROAR_LIBRETRO_HOST_H_
#define XROAR_LIBRETRO_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Install our UI / audio / joystick modules into XRoar's module lists.  Must
 * be called before xroar_init() so that machine construction finds them. */
void libretro_host_install_modules(void);

/* Map the two libretro gamepads onto XRoar's joystick ports.  Call once the
 * machine exists (after xroar_init()). */
void libretro_host_connect_joysticks(void);

/* - - - video - - - */

/* Framebuffer contents (native-endian XRGB8888) and dimensions of the last
 * completed frame. */
const void *libretro_host_video_buffer(void);
unsigned libretro_host_video_width(void);
unsigned libretro_host_video_height(void);

/* Returns true (and clears the flag) if a frame has been drawn since the last
 * call. */
bool libretro_host_take_frame(void);

/* - - - audio - - - */

unsigned libretro_host_audio_rate(void);

/* Take up to `max_frames` stereo S16 frames of buffered audio; returns the
 * number taken. */
size_t libretro_host_audio_take(int16_t *out, size_t max_frames);
void libretro_host_reset_audio(void);

/* - - - input - - - */

/* axis value is in XRoar's 0..65535 range (32767 = centred). */
void libretro_host_joystick_axis(unsigned port, unsigned axis, int value);
void libretro_host_joystick_button(unsigned port, unsigned button, bool pressed);

#endif
