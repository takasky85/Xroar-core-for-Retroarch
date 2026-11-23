/* Smoke test for the XRoar CoCo libretro core.
 *
 * Loads the built core with dlopen (so only its exported retro_* symbols are
 * used), boots a machine with a synthetic ROM, runs some frames, presses a
 * key and checks that video, audio and save states behave.
 *
 * Usage: smoke <core.so> <system_dir> <content_file> [machine option value]
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

static const char *g_system_dir;
static const char *g_machine = "Auto";
static retro_keyboard_event_t g_kbd;

static unsigned frames_seen, frames_nonblack;
static unsigned long long audio_frames_seen;
static unsigned last_w, last_h;
static bool pad_start, pad_select, pad_b, pad_right;
static uint32_t last_frame[800 * 300];

static bool env_cb(unsigned cmd, void *data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = g_system_dir;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		if (!strcmp(v->key, "xroar_machine")) {
			v->value = g_machine;
			return true;
		}
		return false;
	}
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		g_kbd = ((const struct retro_keyboard_callback *)data)->callback;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		return true;
	default:
		return false;
	}
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
	frames_seen++;
	for (unsigned y = 0; y < height && y < 300; y++)
		memcpy(last_frame + y * width, (const char *)data + y * pitch, width * 4);
	last_w = width;
	last_h = height;
	for (unsigned y = 0; y < height; y++) {
		const uint32_t *row = (const uint32_t *)((const char *)data + y * pitch);
		for (unsigned x = 0; x < width; x++) {
			if (row[x] & 0x00ffffff) {
				frames_nonblack++;
				return;
			}
		}
	}
}

static size_t audio_cb(const int16_t *data, size_t frames) {
	(void)data;
	audio_frames_seen += frames;
	return frames;
}

static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void)index;
	if (port == 0 && device == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_START)
		return pad_start;
	if (port == 0 && device == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_SELECT)
		return pad_select;
	if (port == 0 && device == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_B)
		return pad_b;
	if (port == 0 && device == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_RIGHT)
		return pad_right;
	return 0;
}

#define SYM(name) \
	__typeof__(&name) p_##name = (__typeof__(&name))dlsym(h, #name); \
	if (!p_##name) { fprintf(stderr, "FAIL: missing symbol %s\n", #name); return 1; }

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: %s <core.so> <system_dir> <content_file> [machine]\n", argv[0]);
		return 2;
	}
	g_system_dir = argv[2];
	if (argc > 4)
		g_machine = argv[4];

	void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
		return 1;
	}
	SYM(retro_set_environment) SYM(retro_set_video_refresh)
	SYM(retro_set_audio_sample_batch) SYM(retro_set_input_poll)
	SYM(retro_set_input_state) SYM(retro_init) SYM(retro_load_game)
	SYM(retro_run) SYM(retro_serialize_size) SYM(retro_serialize)
	SYM(retro_unserialize) SYM(retro_unload_game) SYM(retro_deinit)
	SYM(retro_get_system_av_info) SYM(retro_get_system_info)

	struct retro_system_info si;
	p_retro_get_system_info(&si);
	printf("core: %s %s [%s]\n", si.library_name, si.library_version, si.valid_extensions);

	p_retro_set_environment(env_cb);
	p_retro_set_video_refresh(video_cb);
	p_retro_set_audio_sample_batch(audio_cb);
	p_retro_set_input_poll(input_poll_cb);
	p_retro_set_input_state(input_state_cb);
	p_retro_init();

	struct retro_game_info game = {0};
	game.path = argv[3];
	if (!p_retro_load_game(&game)) {
		fprintf(stderr, "FAIL: retro_load_game\n");
		return 1;
	}
	struct retro_system_av_info av;
	p_retro_get_system_av_info(&av);
	printf("av: %ux%u @ %.2f fps, %.0f Hz\n", av.geometry.base_width,
	       av.geometry.base_height, av.timing.fps, av.timing.sample_rate);

	size_t state_size = p_retro_serialize_size();
	printf("state size: %zu\n", state_size);

	for (int i = 0; i < 120; i++)
		p_retro_run();

	int rc = 0;
	if (!g_kbd) { fprintf(stderr, "FAIL: no keyboard callback\n"); rc = 1; }
	else {
		g_kbd(true, RETROK_a, 'a', 0);
		p_retro_run();
		g_kbd(false, RETROK_a, 0, 0);
		g_kbd(true, RETROK_RETURN, '\r', 0);
		p_retro_run();
		g_kbd(false, RETROK_RETURN, 0, 0);
	}
	pad_start = true;
	p_retro_run();
	pad_start = false;
	for (int i = 0; i < 116; i++)
		p_retro_run();

	printf("frames: %u (non-black: %u), geometry %ux%u\n",
	       frames_seen, frames_nonblack, last_w, last_h);
	printf("audio frames: %llu (expected about %.0f)\n", audio_frames_seen,
	       frames_seen * av.timing.sample_rate / av.timing.fps);

	if (frames_seen < 200) { fprintf(stderr, "FAIL: too few frames\n"); rc = 1; }
	if (frames_nonblack == 0) { fprintf(stderr, "FAIL: video always black\n"); rc = 1; }
	double expect = frames_seen * av.timing.sample_rate / av.timing.fps;
	if (audio_frames_seen < expect * 0.9 || audio_frames_seen > expect * 1.1) {
		fprintf(stderr, "FAIL: audio rate off\n");
		rc = 1;
	}

	/* Open the on-screen keyboard, move right twice, press a key, and
	 * optionally save the picture as a PPM. */
	pad_select = true; p_retro_run(); pad_select = false; p_retro_run();
	for (int i = 0; i < 2; i++) { pad_right = true; p_retro_run(); pad_right = false; p_retro_run(); }
	pad_b = true; p_retro_run(); p_retro_run();
	const char *ppm = getenv("SMOKE_PPM");
	if (ppm) {
		FILE *f = fopen(ppm, "wb");
		fprintf(f, "P6\n%u %u\n255\n", last_w, last_h);
		for (unsigned i = 0; i < last_w * last_h; i++) {
			uint32_t c = last_frame[i];
			fputc((c >> 16) & 0xff, f); fputc((c >> 8) & 0xff, f); fputc(c & 0xff, f);
		}
		fclose(f);
	}
	pad_b = false; p_retro_run();
	pad_select = true; p_retro_run(); pad_select = false; p_retro_run();

	void *state = malloc(state_size);
	if (!p_retro_serialize(state, state_size)) {
		fprintf(stderr, "FAIL: retro_serialize\n");
		rc = 1;
	} else {
		for (int i = 0; i < 20; i++)
			p_retro_run();
		if (!p_retro_unserialize(state, state_size)) {
			fprintf(stderr, "FAIL: retro_unserialize\n");
			rc = 1;
		} else {
			for (int i = 0; i < 10; i++)
				p_retro_run();
			printf("save state round-trip OK\n");
		}
	}
	free(state);

	p_retro_unload_game();
	p_retro_deinit();

	if (rc == 0)
		printf("SMOKE PASS\n");
	return rc;
}
