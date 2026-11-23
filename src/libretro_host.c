/** \file
 *
 *  \brief Host-side glue between the libretro core and XRoar's core.
 *
 *  Based on konsumer/xroar-libretro.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "ao.h"
#include "joystick.h"
#include "module.h"
#include "sound.h"
#include "ui.h"
#include "vo.h"
#include "vo_render.h"

#include "libretro_host.h"

/* XRoar's largest viewport is the "underscan" picture at 736x276 pixels; a
 * little headroom covers anything the machine might request. */
#define LB_FB_MAX_WIDTH  (800)
#define LB_FB_MAX_HEIGHT (300)

#define LB_AUDIO_RATE      (44100)
#define LB_AUDIO_CHANNELS  (2)
#define LB_AUDIO_CHUNK     (1024)
#define LB_AUDIO_RING_FRAMES (16384)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Video: one framebuffer, filled by XRoar's renderer. */

static void *lb_fb;
static unsigned lb_fb_width = 640;
static unsigned lb_fb_height = 240;
static bool lb_frame_ready;

static void lb_vo_draw(void *sptr) {
	(void)sptr;
	lb_frame_ready = true;
}

/* XRoar's UI asks us to (re)configure the viewport whenever the machine
 * reports a different picture area or 60Hz scaling is toggled. */
static void lb_vo_set_viewport(void *sptr, int w, int h) {
	struct vo_interface *vo = sptr;
	struct vo_render *vr = vo->renderer;
	if (!vr)
		return;
	if (w <= 0 || h <= 0)
		return;
	if (w > LB_FB_MAX_WIDTH)
		w = LB_FB_MAX_WIDTH;
	if (h > LB_FB_MAX_HEIGHT)
		h = LB_FB_MAX_HEIGHT;
	vo_render_set_viewport(vr, w, h);
	vr->buffer_pitch = vr->viewport.w;
	lb_fb_width = vr->viewport.w;
	lb_fb_height = vr->viewport.h;
}

static void lb_vo_free(void *sptr) {
	struct vo_interface *vo = sptr;
	free(lb_fb);
	lb_fb = NULL;
	free(vo);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Audio: a single staging buffer that XRoar fills, drained into a ring that
 * the libretro core pulls from once per frame. */

static int16_t lb_audio_stage[LB_AUDIO_CHUNK * LB_AUDIO_CHANNELS];
static int16_t lb_audio_ring[LB_AUDIO_RING_FRAMES * LB_AUDIO_CHANNELS];
static size_t lb_audio_head;     /* next write frame */
static size_t lb_audio_tail;     /* next read frame */
static size_t lb_audio_count;    /* frames available */

static void lb_audio_push(const int16_t *src, size_t frames) {
	for (size_t i = 0; i < frames; i++) {
		if (lb_audio_count == LB_AUDIO_RING_FRAMES) {
			/* Overrun: drop the oldest frame. */
			lb_audio_tail = (lb_audio_tail + 1) % LB_AUDIO_RING_FRAMES;
			lb_audio_count--;
		}
		lb_audio_ring[lb_audio_head * LB_AUDIO_CHANNELS + 0] = src[i * LB_AUDIO_CHANNELS + 0];
		lb_audio_ring[lb_audio_head * LB_AUDIO_CHANNELS + 1] = src[i * LB_AUDIO_CHANNELS + 1];
		lb_audio_head = (lb_audio_head + 1) % LB_AUDIO_RING_FRAMES;
		lb_audio_count++;
	}
}

/* XRoar hands us a freshly mixed buffer and expects the next one to fill.  We
 * copy the contents out and hand the same staging buffer straight back. */
static void *lb_ao_write_buffer(void *sptr, void *buffer) {
	(void)sptr;
	if (buffer) {
		lb_audio_push(buffer, LB_AUDIO_CHUNK);
	}
	return lb_audio_stage;
}

struct lb_ao_interface {
	struct ao_interface public;
};

static void lb_ao_free(void *sptr) {
	struct lb_ao_interface *ao = sptr;
	sound_interface_free(ao->public.sound_interface);
	free(ao);
}

static void *lb_ao_new(void *cfg) {
	(void)cfg;
	struct lb_ao_interface *ao = xmalloc(sizeof(*ao));
	*ao = (struct lb_ao_interface){0};
	ao->public.free = DELEGATE_AS0(void, lb_ao_free, ao);
	ao->public.sound_interface = sound_interface_new(NULL, SOUND_FMT_S16_HE,
							 LB_AUDIO_RATE, LB_AUDIO_CHANNELS,
							 LB_AUDIO_CHUNK);
	if (!ao->public.sound_interface) {
		free(ao);
		return NULL;
	}
	ao->public.sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, lb_ao_write_buffer, ao);
	sound_set_ratelimit(ao->public.sound_interface, false);
	return ao;
}

static struct module lb_ao_module = {
	.name = "libretro", .description = "libretro audio", .new = lb_ao_new,
};

static struct module * const lb_ao_module_list[] = {
	&lb_ao_module, NULL
};

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Joystick: two ports, each with two axes and two buttons, fed from libretro
 * gamepad state. */

static int lb_js_axis[2][JOYSTICK_NUM_AXES] = {
	{ 32767, 32767 }, { 32767, 32767 }
};
static int lb_js_button[2][JOYSTICK_NUM_BUTTONS];

void libretro_host_joystick_axis(unsigned port, unsigned axis, int value) {
	if (port >= 2 || axis >= JOYSTICK_NUM_AXES)
		return;
	if (value < 0)
		value = 0;
	if (value > 65535)
		value = 65535;
	lb_js_axis[port][axis] = value;
}

void libretro_host_joystick_button(unsigned port, unsigned button, bool pressed) {
	if (port >= 2 || button >= JOYSTICK_NUM_BUTTONS)
		return;
	lb_js_button[port][button] = pressed ? 1 : 0;
}

struct lb_js_control {
	struct joystick_control control;
	unsigned port;
	unsigned index;
};

static int lb_js_axis_read(void *sptr) {
	struct lb_js_control *c = sptr;
	return lb_js_axis[c->port][c->index];
}

static int lb_js_button_read(void *sptr) {
	struct lb_js_control *c = sptr;
	return lb_js_button[c->port][c->index];
}

static void lb_js_control_free(void *sptr) {
	free(sptr);
}

/* Specs are "PORT,INDEX" - the submodule name has already been stripped. */
static struct joystick_control *lb_js_configure_axis(char *spec, unsigned jaxis) {
	(void)jaxis;
	unsigned port, index;
	if (!spec || sscanf(spec, "%u,%u", &port, &index) != 2)
		return NULL;
	if (port >= 2 || index >= JOYSTICK_NUM_AXES)
		return NULL;
	struct lb_js_control *c = xmalloc(sizeof(*c));
	*c = (struct lb_js_control){0};
	c->port = port;
	c->index = index;
	c->control.read = DELEGATE_AS0(int, lb_js_axis_read, c);
	c->control.free = DELEGATE_AS0(void, lb_js_control_free, c);
	return &c->control;
}

static struct joystick_control *lb_js_configure_button(char *spec, unsigned jbutton) {
	(void)jbutton;
	unsigned port, index;
	if (!spec || sscanf(spec, "%u,%u", &port, &index) != 2)
		return NULL;
	if (port >= 2 || index >= JOYSTICK_NUM_BUTTONS)
		return NULL;
	struct lb_js_control *c = xmalloc(sizeof(*c));
	*c = (struct lb_js_control){0};
	c->port = port;
	c->index = index;
	c->control.read = DELEGATE_AS0(int, lb_js_button_read, c);
	c->control.free = DELEGATE_AS0(void, lb_js_control_free, c);
	return &c->control;
}

static struct joystick_submodule lb_js_submodule = {
	.name = "libretro",
	.init = NULL,
	.configure_axis = lb_js_configure_axis,
	.configure_button = lb_js_configure_button,
};

static struct joystick_submodule *lb_js_submodule_list[] = {
	&lb_js_submodule, NULL
};

static struct joystick_module lb_js_module = {
	.common = { .name = "libretro", .description = "libretro gamepads", .new = NULL },
	.submodule_list = lb_js_submodule_list,
};

static struct joystick_module *lb_js_module_list[] = {
	&lb_js_module, NULL
};

void libretro_host_connect_joysticks(void) {
	for (unsigned port = 0; port < 2; port++) {
		const char *name = (port == 0) ? "libretro0" : "libretro1";
		struct joystick_config *jc = joystick_config_by_name(name);
		if (!jc) {
			char buf[32];
			jc = joystick_config_new();
			jc->name = xstrdup(name);
			jc->description = xstrdup("libretro gamepad");
			for (unsigned i = 0; i < 2; i++) {
				snprintf(buf, sizeof(buf), "libretro:%u,%u", port, i);
				jc->axis_specs[i] = xstrdup(buf);
				jc->button_specs[i] = xstrdup(buf);
			}
		}
		ui_update_state(-1, ui_tag_joystick_port, (int)port,
				(const void *)(intptr_t)jc->id);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* UI module: owns the video interface. */

static void lb_ui_free(void *sptr) {
	free(sptr);
}

struct lb_ui_interface {
	struct ui_interface public;
};

static void *lb_ui_new(void *cfg) {
	(void)cfg;
	struct lb_ui_interface *ui = xmalloc(sizeof(*ui));
	*ui = (struct lb_ui_interface){0};
	ui->public.free = DELEGATE_AS0(void, lb_ui_free, ui);

	struct vo_interface *vo = vo_interface_new(sizeof(*vo));
	*vo = (struct vo_interface){0};
	ui->public.vo_interface = vo;

	vo_interface_init(vo);

	/* map_argb8 packs to 0xFFRRGGBB, which is libretro's XRGB8888. */
	struct vo_render *vr = vo_render_new(VO_RENDER_FMT_ARGB8);
	vo_set_renderer(vo, vr);

	lb_fb = xmalloc((size_t)LB_FB_MAX_WIDTH * LB_FB_MAX_HEIGHT * 4);
	memset(lb_fb, 0, (size_t)LB_FB_MAX_WIDTH * LB_FB_MAX_HEIGHT * 4);
	vo_render_set_buffer(vr, lb_fb);
	vr->buffer_pitch = vr->viewport.w;
	lb_fb_width = vr->viewport.w;
	lb_fb_height = vr->viewport.h;

	vo->draw = DELEGATE_AS0(void, lb_vo_draw, vo);
	vo->set_viewport = DELEGATE_AS2(void, int, int, lb_vo_set_viewport, vo);
	vo->free = DELEGATE_AS0(void, lb_vo_free, vo);

	return ui;
}

static struct ui_module lb_ui_module = {
	.common = { .name = "libretro", .description = "libretro frontend", .new = lb_ui_new },
	.filereq_module_list = default_filereq_module_list,
	.vo_module_list = NULL,
	.ao_module_list = lb_ao_module_list,
	.joystick_module_list = lb_js_module_list,
};

static struct ui_module * const lb_ui_module_list[] = {
	&lb_ui_module, NULL
};

void libretro_host_install_modules(void) {
	ui_module_list = lb_ui_module_list;
	ao_module_list = lb_ao_module_list;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

const void *libretro_host_video_buffer(void) {
	return lb_fb;
}

unsigned libretro_host_video_width(void) {
	return lb_fb_width;
}

unsigned libretro_host_video_height(void) {
	return lb_fb_height;
}

bool libretro_host_take_frame(void) {
	bool r = lb_frame_ready;
	lb_frame_ready = false;
	return r;
}

unsigned libretro_host_audio_rate(void) {
	return LB_AUDIO_RATE;
}

size_t libretro_host_audio_take(int16_t *out, size_t max_frames) {
	size_t have = lb_audio_count < max_frames ? lb_audio_count : max_frames;
	for (size_t i = 0; i < have; i++) {
		out[i * LB_AUDIO_CHANNELS + 0] = lb_audio_ring[lb_audio_tail * LB_AUDIO_CHANNELS + 0];
		out[i * LB_AUDIO_CHANNELS + 1] = lb_audio_ring[lb_audio_tail * LB_AUDIO_CHANNELS + 1];
		lb_audio_tail = (lb_audio_tail + 1) % LB_AUDIO_RING_FRAMES;
	}
	lb_audio_count -= have;
	return have;
}

void libretro_host_reset_audio(void) {
	lb_audio_head = lb_audio_tail = lb_audio_count = 0;
}
