# XRoar CoCo core for RetroArch

A libretro core for the **Tandy Color Computer 1, 2 and 3**, built from
[XRoar](https://www.6809.org.uk/xroar/) by Ciaran Anscomb. It is made first
for 32-bit ARM Android (armeabi-v7a), to run in RetroArch on Google TV.

Status: **first version, not yet tested on a TV.**

## Install on Android / Google TV

1. Open the latest run under **Actions > Build core** and download the
   artifact `xroar_libretro_android-armeabi-v7a`. The downloaded zip holds
   `xroar_libretro_android.so` and `xroar_libretro.info`.
2. RetroArch TV Lite: drop the zip in `/storage/emulated/0/RetroArch/cores`
   and restart RetroArch. Stock RetroArch: unzip it and use
   *Load Core > Install or Restore a Core* for the `.so`, and copy the `.info`
   into the info folder.
3. Put the ROMs in the system folder (below).

## ROMs (BIOS)

Tandy's ROMs are not included. Put them in the RetroArch system folder
(`/storage/emulated/0/RetroArch/system`), or in a `xroar/` or `coco/` folder
inside it. File names (`.rom` extension):

| Machine | Needed | Optional |
|---|---|---|
| CoCo 1/2 | `bas13.rom` (or `bas12`, `bas11`, `bas10`) | `extbas11.rom` (Extended BASIC, most games want it) |
| CoCo 3 NTSC | `coco3.rom` | |
| CoCo 3 PAL | `coco3p.rom` | |
| Disks (`.dsk`), any CoCo | | `disk11.rom` (or `disk10`) |

If a needed ROM is missing the core shows a message and boots with an empty
ROM. XRoar also matches ROMs by checksum, so other common names often work.

## Content

| Extension | What it is |
|---|---|
| `.ccc`, `.rom` | Cartridge |
| `.cas` | Cassette (WAV tapes are not supported in this build) |
| `.dsk`, `.jvc`, `.dmk`, `.os9`, `.vdk` | Floppy disk |
| `.bin`, `.cco` | DECB binary |
| `.bas`, `.asc` | BASIC program as text |
| `.sn` | XRoar snapshot |
| `.vhd` | Hard disk image |

Zipped content works too. With **Autorun** on (the default), cassettes are
CLOADed and run, binaries are run, and disks run their program: the core
reads the disk's directory and types `RUN"NAME"` for a BASIC loader or
`LOADM"NAME":EXEC` for a machine code program. It prefers a BASIC program
called AUTOEXEC, BOOT, LOADER, MENU and the like, then a program named like
the disk file, then the first BASIC program, then the first machine code
one. Disks with no Disk BASIC directory (OS-9, boot disks) get `DOS`.

## Machine

**Auto** (the default) boots a **CoCo 3**, which runs most CoCo 1/2
software too. If the file name or any folder in its path says `CoCo 1` or
`CoCo 2` (also `coco2`, `CoCo_2`...), it boots a **CoCo 2** instead, so a
`CoCo 2` folder is enough for games that need one. To force a machine, use
*Quick Menu > Core Options > Machine* and restart the content.

## Controls

RetroPad 1 is the joystick in the **right** port, which most games use;
RetroPad 2 is the left one. *Player 1 joystick port* swaps them.

| RetroPad | CoCo | TV remote |
|---|---|---|
| D-pad / left stick | Joystick | D-pad |
| B | Fire 1 | OK |
| A | Fire 2 (CoCo 3 joysticks) | |
| Start | ENTER | Channel Up |
| Select | On-screen keyboard | Channel Down |
| X | 1 | |
| Y | 2 | |
| L2 / R2 | Y / N | |
| L3 / R3 | BREAK / CLEAR | |

L1 and R1 are left free because L1+R1 opens the menu in TV Lite.

### On-screen keyboard

Press **Select** (Channel Down on the remote) to show a CoCo keyboard over
the picture, and Select again (or the HIDE key) to put it away. While it is
up the pad drives the keyboard instead of the joystick:

| RetroPad | Remote | Does |
|---|---|---|
| D-pad | D-pad | Move |
| B | OK | Type the key |
| A | | Delete (left arrow) |
| Y | | SHIFT on/off |
| X | | Move the keyboard to the top or bottom |
| Start | Channel Up | ENTER |

SHIFT, CTRL and ALT stay down until pressed again. The bottom row types
whole commands: DIR, RUN, CLOAD, EXEC and DOS (each with ENTER), plus `"`,
`=` and `$`.

Keyboard: by default (**Translated**) a key types what is printed on it, so
Shift+2 types `"` or `@` the way your keyboard shows. Leave Caps Lock off:
BASIC wants unshifted letters, and shifted ones come out as lowercase or
inverse. **Positional** puts
the keys where they are on a real CoCo instead. Either way:

| PC key | CoCo key |
|---|---|
| Esc, End, Pause | BREAK |
| Home, Delete | CLEAR |
| Backspace | Left arrow (delete) |
| Ctrl, Alt, F1, F2 | CTRL, ALT, F1, F2 (CoCo 3) |

Turn on RetroArch's **Game Focus** (Scroll Lock by default) to type freely
without keys triggering hotkeys.

## Core options

| Option | Values |
|---|---|
| Border | Smart (default), None (256x192 only), Thin, Full |
| On-screen keyboard opacity | 50% (default), 25%, 75%, 100%, 0% (labels only) |
| Machine | Auto, CoCo 1/2, CoCo 2B (lowercase), CoCo 3, CoCo 3 with 6309 CPU, and PAL versions |
| Video input | Default, composite blue-red artifacts, red-blue artifacts, S-Video (no artifacts), RGB |
| Artifact colour renderer | 5-bit (fast), Simple, Partial NTSC, Simulated (slow), None |
| Keyboard mapping | Translated, Positional |
| Player 1 joystick port | Right, Left |
| Fast cassette loading | On, Off |
| Autorun content | On, Off |

Many CoCo 1/2 games show colour through NTSC artifacts. If a game looks
black and white or the colours are swapped, change *Video input* (or press
reset, as on the real machine, which can flip the artifact phase).

## Limitations of this first version

- Only one disk per game: no disk swapping from the RetroArch menu yet.
- Save states work but go through a temporary file, so leave rewind off.
- No WAV cassettes, printer, Becker port/DriveWire or GDB.
- Border: Smart shows the standard 256x192 screen area with no border. If
  a game draws outside it (taller or wider CoCo 3 modes), the picture
  widens once to the next standard size (320 across; 200, 210 or 225 lines)
  and stays that way until you load another game, so it never jiggles.
  None always shows just 256x192 and cuts off bigger CoCo 3 screens.

## Building

```
git clone --recursive https://github.com/takasky85/Xroar-core-for-Retroarch
make && make test       # desktop core plus smoke test
make android            # needs the Android NDK (ndk-build)
```

There is no `./configure` step: `src/config.h` replaces XRoar's generated
`config.h`. XRoar is the `upstream/` submodule, pinned to commit `93842a7`
(October 2026) of the [stahta01/xroar](https://github.com/stahta01/xroar)
mirror. The libretro glue in `src/` started from
[konsumer/xroar-libretro](https://github.com/konsumer/xroar-libretro).

## Licence

GPL-3.0-or-later, the same as XRoar. See `COPYING` and `NOTICE`.
