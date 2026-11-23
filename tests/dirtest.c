/* Tests which command disk autorun types for synthetic RS-DOS disk images.
 * Builds disk_autorun.c on its own with stubs for the XRoar calls. */

#include "top-config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xroar.h"

struct xroar xroar;
void xroar_load_disk(const char *f, int d, bool a) { (void)f; (void)d; (void)a; }
void ak_parse_type_string(struct auto_kbd *ak, const char *s) { (void)ak; (void)s; }

#include "../src/disk_autorun.c"

struct ent { const char *name; const char *ext; int type; };

static void make_disk(const char *path, const struct ent *e, int n, int header) {
	FILE *f = fopen(path, "wb");
	long size = 35L * 18 * 256;
	unsigned char *img = calloc(1, size + header);
	if (header) { img[0] = 18; img[1] = 1; img[2] = 1; img[3] = 1; }
	unsigned char *dir = img + header + (17 * 18 + 2) * 256;
	memset(dir, 0xff, 9 * 256);
	for (int i = 0; i < n; i++) {
		unsigned char *d = dir + i * 32;
		memset(d, 0, 32);
		memset(d, ' ', 11);
		memcpy(d, e[i].name, strlen(e[i].name));
		memcpy(d + 8, e[i].ext, strlen(e[i].ext));
		d[11] = e[i].type;
		d[13] = i;
	}
	fwrite(img, 1, size + header, f);
	fclose(f);
	free(img);
}

static int check(const char *dir, const char *file, const struct ent *e, int n,
		 int header, const char *want) {
	char path[512], cmd[64] = "";
	snprintf(path, sizeof(path), "%s/%s", dir, file);
	make_disk(path, e, n, header);
	bool ok = disk_autorun_command(path, cmd, sizeof(cmd));
	const char *got = ok ? cmd : "(DOS)";
	int pass = strcmp(got, want) == 0;
	printf("%s %-22s -> %s%s%s\n", pass ? "ok  " : "FAIL", file, got,
	       pass ? "" : "   expected ", pass ? "" : want);
	return pass ? 0 : 1;
}

int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : ".";
	int rc = 0;
	struct ent a[] = { { "GAME", "BIN", 2 }, { "LOADER", "BAS", 0 } };
	rc |= check(dir, "Some Game.dsk", a, 2, 0, "\\025RUN\"LOADER\"\\r");
	struct ent b[] = { { "PACDROID", "BIN", 2 } };
	rc |= check(dir, "Pac Droid (1983).dsk", b, 1, 0, "\\025LOADM\"PACDROID\":EXEC\\r");
	struct ent c[] = { { "TITLE", "BIN", 2 }, { "ZAXXON", "BIN", 2 } };
	rc |= check(dir, "Zaxxon (Datasoft).dsk", c, 2, 0, "\\025LOADM\"ZAXXON\":EXEC\\r");
	struct ent d[] = { { "SCORES", "DAT", 1 }, { "ROGUE", "BAS", 0 } };
	rc |= check(dir, "rogue.dsk", d, 2, 4, "\\025RUN\"ROGUE\"\\r");
	rc |= check(dir, "os9boot.dsk", NULL, 0, 0, "(DOS)");
	return rc;
}
