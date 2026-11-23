XRoar CoCo libretro core for RetroArch: Tandy Color Computer 1, 2 and 3, 32-bit ARM Android (armeabi-v7a).

**Install:** download the zip below. In RetroArch TV Lite, drop the zip in `/storage/emulated/0/RetroArch/cores` and fully restart RetroArch. In stock RetroArch, unzip it, install the `.so` with *Load Core > Install or Restore a Core* and copy the `.info` to the info folder.

**ROMs (not included)** go in `RetroArch/system` (or `system/coco`): `bas13.rom` + `extbas11.rom` for CoCo 1/2, `coco3.rom` for CoCo 3, `disk11.rom` for disks.

**Features**
- CoCo 3 by default; a file or folder name saying CoCo 1 or CoCo 2 boots a CoCo 2.
- Cartridges, cassettes and disks autorun; disks run their program (RUN or LOADM/EXEC) from the disk directory.
- On-screen keyboard on Select (Channel Down on a TV remote), see-through, with DIR/RUN/CLOAD/EXEC/DOS keys.
- Remote/pad: D-pad joystick, OK/B fire, Start = ENTER; keyboard typing in translated or real-CoCo layout.
- Core options: machine, border (Thin/None/Full), video input and artifact colours, keyboard opacity, joystick port, fast tape, autorun.

Built from XRoar by Ciaran Anscomb (October 2026 source), GPL-3.0-or-later. See the README for details.
