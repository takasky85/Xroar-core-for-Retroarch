/* Smarter disk autorun.  GPL-3.0-or-later. */

#ifndef XROAR_LIBRETRO_DISK_AUTORUN_H_
#define XROAR_LIBRETRO_DISK_AUTORUN_H_

#include <stdbool.h>
#include <stddef.h>

/* For an RS-DOS disk image: insert it in drive 0 and type the command that
 * runs its program.  Returns false (doing nothing) when the file is not such
 * a disk, autorun is off, or no program was found, so XRoar's own loader
 * should handle it. */
bool disk_autorun_load(const char *path, bool autorun);

/* The command that would be typed, for testing. */
bool disk_autorun_command(const char *path, char *cmd, size_t len);

#endif
