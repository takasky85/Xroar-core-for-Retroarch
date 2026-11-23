/* Hand-written XRoar config.h for the libretro core.
 *
 * Replaces the autoconf-generated config.h so the same sources build with the
 * Android NDK (no ./configure run) and on desktop Linux.  Emulation only: no
 * desktop toolkit, audio backend, OpenGL, zlib, libpng or sndfile.
 *
 * Dragon and MC-10 machine code is still compiled because XRoar's CoCo 1/2
 * lives in the Dragon-family architecture; the core only offers CoCo machines.
 */

#ifndef XROAR_LIBRETRO_CONFIG_H
#define XROAR_LIBRETRO_CONFIG_H

#define PACKAGE "xroar"
#define PACKAGE_NAME "xroar"
#define PACKAGE_TARNAME "xroar"
#define PACKAGE_VERSION "1.13.0.dev"
#define PACKAGE_STRING "xroar 1.13.0.dev"
#define PACKAGE_TEXT "XRoar 1.13.0.dev"
#define PACKAGE_YEAR "2026"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define VERSION "1.13.0.dev"
#define RC_REV_MAJOR 0
#define RC_REV_MINOR 0

#define STDC_HEADERS 1
#define HAVE_ENDIAN_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_REGEX_H 1
#define HAVE_STRNLEN 1
#define HAVE_STRSEP 1
#define HAVE_PTHREADS 1

#define HAVE_FUNC_ATTRIBUTE_CONST 1
#define HAVE_FUNC_ATTRIBUTE_FORMAT 1
#define HAVE_FUNC_ATTRIBUTE_MALLOC 1
#define HAVE_FUNC_ATTRIBUTE_NONNULL 1
#define HAVE_FUNC_ATTRIBUTE_NORETURN 1
#define HAVE_FUNC_ATTRIBUTE_PURE 1
#define HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL 1
#define HAVE_VAR_ATTRIBUTE_NONSTRING 1
#define HAVE_VAR_ATTRIBUTE_PACKED 1
#define HAVE___BUILTIN_EXPECT 1
#define HAVE___BUILTIN_PARITY 1

#define SIZEOF_DOUBLE 8
#define SIZEOF_FLOAT 4

#define LOGGING 1
#define HAVE_NULL_AUDIO 1
#define X_DISPLAY_MISSING 1

#define WANT_MACHINE_ARCH_DRAGON 1
#define WANT_MACHINE_ARCH_COCO3 1
#define WANT_MACHINE_ARCH_MC10 1
#define WANT_CART_ARCH_DRAGON 1
#define WANT_CART_ARCH_MC10 1
#define WANT_PART_MC6801 1
#define WANT_PART_MC6809 1
#define WANT_PART_MC6821 1
#define WANT_PART_MC6847 1
#define WANT_PART_MC6883 1
#define WANT_PART_TCC1014 1

#endif
