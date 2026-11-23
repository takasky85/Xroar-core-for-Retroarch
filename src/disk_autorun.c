/** \file
 *
 *  \brief Smarter disk autorun for the XRoar libretro core.
 *
 *  XRoar's own disk autorun types DOS, which only boots disks that have a
 *  boot track.  Most CoCo game disks are plain Disk BASIC (RS-DOS) disks
 *  instead, so this reads the disk's directory and types RUN"PROGRAM" or
 *  LOADM"PROGRAM":EXEC for the most likely program, falling back to DOS
 *  when the disk has no RS-DOS directory (OS-9 and boot disks).
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

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "auto_kbd.h"
#include "xroar.h"

#include "disk_autorun.h"

#define SECTOR 256

struct dir_entry {
	char name[9];
	char ext[4];
	uint8_t type;   /* 0 BASIC, 1 data, 2 machine code, 3 text */
	bool ascii;
};

static const char *ext_of(const char *path) {
	const char *slash = strrchr(path, '/');
	const char *dot = strrchr(path, '.');
	if (!dot || (slash && dot < slash))
		return "";
	return dot + 1;
}

/* Byte offset of track 17 (the RS-DOS directory track), sector `sec`
 * (1-based), side 0.  Returns -1 for formats not handled here. */
static long dir_sector_offset(FILE *f, const char *ext, unsigned sec) {
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	if (size <= 0)
		return -1;

	if (!strcasecmp(ext, "vdk")) {
		uint8_t h[12];
		fseek(f, 0, SEEK_SET);
		if (fread(h, 1, sizeof(h), f) != sizeof(h) || h[0] != 'd' || h[1] != 'k')
			return -1;
		long hlen = h[2] | (h[3] << 8);
		unsigned sides = h[9] ? h[9] : 1;
		return hlen + ((long)(17 * sides) * 18 + (sec - 1)) * SECTOR;
	}

	if (!strcasecmp(ext, "dsk") || !strcasecmp(ext, "jvc")) {
		/* JVC: optional header of (size % 256) bytes. */
		long hlen = size % SECTOR;
		unsigned spt = 18, sides = 1, first = 1;
		if (hlen > 0) {
			uint8_t h[4] = { 18, 1, 1, 1 };
			fseek(f, 0, SEEK_SET);
			if (fread(h, 1, hlen < 4 ? (size_t)hlen : 4, f) == 0)
				return -1;
			spt = h[0] ? h[0] : 18;
			if (hlen > 1) sides = h[1] ? h[1] : 1;
			if (hlen > 2 && h[2] != 1) return -1;  /* not 256-byte sectors */
			if (hlen > 3) first = h[3];
		}
		if (spt != 18 || sec < first)
			return -1;
		long body = size - hlen;
		/* Single-sided 35/40/80 track images are most common; a
		 * double-sided header is honoured if present. */
		if (hlen <= 1 && body >= 2L * 80 * 18 * SECTOR)
			sides = 2;
		return hlen + ((long)(17 * sides) * spt + (sec - first)) * SECTOR;
	}

	return -1;  /* DMK, OS-9 images: leave to XRoar */
}

static int read_dir(const char *path, struct dir_entry *out, int max) {
	const char *ext = ext_of(path);
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	int n = 0;
	bool valid = true;
	for (unsigned sec = 3; sec <= 11 && valid; sec++) {
		long off = dir_sector_offset(f, ext, sec);
		uint8_t buf[SECTOR];
		if (off < 0 || fseek(f, off, SEEK_SET) != 0 || fread(buf, 1, SECTOR, f) != SECTOR) {
			valid = (sec > 3);
			break;
		}
		for (int e = 0; e < SECTOR / 32; e++) {
			const uint8_t *d = buf + e * 32;
			if (d[0] == 0xff) {
				sec = 12;  /* end of directory */
				break;
			}
			if (d[0] == 0x00)
				continue;  /* deleted */
			/* Sanity: printable names and a sane granule number. */
			bool ok = d[11] <= 3 && d[13] < 68;
			for (int i = 0; i < 11 && ok; i++)
				ok = d[i] >= 0x20 && d[i] < 0x7f;
			if (!ok) {
				valid = false;
				break;
			}
			if (n >= max)
				continue;
			struct dir_entry *de = &out[n++];
			memcpy(de->name, d, 8);
			de->name[8] = 0;
			for (int i = 7; i >= 0 && de->name[i] == ' '; i--)
				de->name[i] = 0;
			memcpy(de->ext, d + 8, 3);
			de->ext[3] = 0;
			for (int i = 2; i >= 0 && de->ext[i] == ' '; i--)
				de->ext[i] = 0;
			de->type = d[11];
			de->ascii = d[12] != 0;
		}
	}
	fclose(f);
	return valid ? n : -1;
}

static bool is_basic(const struct dir_entry *d) {
	return d->type == 0 || !strcasecmp(d->ext, "BAS");
}

static bool is_binary(const struct dir_entry *d) {
	return d->type == 2 || !strcasecmp(d->ext, "BIN");
}

/* Does a directory name match the disk image's file name?  Compares the
 * letters and digits of the image name, up to any extension or tag, and
 * accepts either one being a prefix of the other (at least 3 characters). */
static bool matches_image_name(const char *name, const char *path) {
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	char stem[9];
	size_t n = 0;
	for (const char *p = base; *p && *p != '.' && *p != '(' && *p != '[' && n < 8; p++) {
		if (isalnum((unsigned char)*p))
			stem[n++] = (char)toupper((unsigned char)*p);
	}
	stem[n] = 0;
	size_t nl = strlen(name);
	if (n < 3 || nl < 3)
		return false;
	size_t m = nl < n ? nl : n;
	return strncasecmp(name, stem, m) == 0;
}

static const char *const loader_names[] = {
	"AUTOEXEC", "BOOT", "START", "LOADER", "LOAD", "MENU", "RUN", "GO",
	"INTRO", "HELLO", "MAIN", "PLAY", "GAME", NULL
};

static bool is_loader_name(const char *name) {
	for (int i = 0; loader_names[i]; i++)
		if (!strcasecmp(name, loader_names[i]))
			return true;
	return false;
}

/* Choose the command to type; returns false to fall back to DOS. */
static bool choose_command(const char *path, char *cmd, size_t len) {
	struct dir_entry dir[72];
	int n = read_dir(path, dir, 72);
	if (n <= 0)
		return false;

	const struct dir_entry *pick = NULL;
	bool basic = false;

	/* 1. A BASIC program with a loader-like name. */
	for (int i = 0; i < n && !pick; i++)
		if (is_basic(&dir[i]) && is_loader_name(dir[i].name))
			pick = &dir[i], basic = true;
	/* 2. A BASIC program named like the disk image. */
	for (int i = 0; i < n && !pick; i++)
		if (is_basic(&dir[i]) && matches_image_name(dir[i].name, path))
			pick = &dir[i], basic = true;
	/* 3. A machine code program named like the disk image. */
	for (int i = 0; i < n && !pick; i++)
		if (is_binary(&dir[i]) && matches_image_name(dir[i].name, path))
			pick = &dir[i];
	/* 4. The first BASIC program (usually the loader). */
	for (int i = 0; i < n && !pick; i++)
		if (is_basic(&dir[i]))
			pick = &dir[i], basic = true;
	/* 5. The first machine code program. */
	for (int i = 0; i < n && !pick; i++)
		if (is_binary(&dir[i]))
			pick = &dir[i];

	if (!pick)
		return false;

	char fname[16];
	if (pick->ext[0] && strcasecmp(pick->ext, basic ? "BAS" : "BIN") != 0)
		snprintf(fname, sizeof(fname), "%s.%s", pick->name, pick->ext);
	else
		snprintf(fname, sizeof(fname), "%s", pick->name);

	if (basic)
		snprintf(cmd, len, "\\025RUN\"%s\"\\r", fname);
	else
		snprintf(cmd, len, "\\025LOADM\"%s\":EXEC\\r", fname);
	return true;
}

bool disk_autorun_command(const char *path, char *cmd, size_t len) {
	return choose_command(path, cmd, len);
}

bool disk_autorun_load(const char *path, bool autorun) {
	const char *ext = ext_of(path);
	if (strcasecmp(ext, "dsk") && strcasecmp(ext, "jvc") && strcasecmp(ext, "vdk"))
		return false;

	char cmd[64];
	if (!autorun || !choose_command(path, cmd, sizeof(cmd)))
		return false;  /* XRoar loads it (and types DOS if autorunning) */

	xroar_load_disk(path, 0, false);
	if (xroar.auto_kbd)
		ak_parse_type_string(xroar.auto_kbd, cmd);
	return true;
}
