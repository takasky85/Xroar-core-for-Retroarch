#!/bin/sh
# Run the libretro smoke test on a CoCo 2 and a CoCo 3 with synthetic ROMs
# (a 6809 "BRA *" loop at the reset vector), so no real Tandy ROMs are needed.
#
# Usage: run-smoke.sh <smoke-binary> <core.so>
set -e

SMOKE="$1"
CORE="$2"
if [ ! -x "$SMOKE" ] || [ ! -f "$CORE" ]; then
	echo "usage: $0 <smoke-binary> <core.so>" >&2
	exit 2
fi

SYS=$(mktemp -d)
trap 'rm -r "$SYS"' EXIT

# make_rom FILE SIZE: fill with 6809 NOPs, a tiny program at the start, and
# the reset vector (last two bytes) pointing at it ($A000 for an 8K Color
# BASIC ROM, $8000 for the 32K CoCo 3 ROM).  The program sets all 16 CoCo 3
# palette registers to white (harmless on a CoCo 2), then loops forever:
#   LDA #$3F / LDX #$FFB0 / loop: STA ,X+ / CMPX #$FFC0 / BNE loop / BRA *
make_rom() {
	head -c "$2" /dev/zero | tr '\000' '\022' > "$1"
	printf '\206\077\216\377\260\247\200\214\377\300\046\371\040\376' |
		dd of="$1" bs=1 conv=notrunc 2>/dev/null
	if [ "$2" -eq 8192 ]; then vec='\240\000'; else vec='\200\000'; fi
	printf "$vec" | dd of="$1" bs=1 seek=$(($2 - 2)) conv=notrunc 2>/dev/null
	if [ "$2" -eq 32768 ]; then
		printf "$vec" | dd of="$1" bs=1 seek=16382 conv=notrunc 2>/dev/null
	fi
}

mkdir -p "$SYS/xroar"
make_rom "$SYS/xroar/bas13.rom" 8192
make_rom "$SYS/xroar/extbas11.rom" 8192
make_rom "$SYS/coco3.rom" 32768

# An empty cassette image as content.
: > "$SYS/game.cas"

"$SMOKE" "$CORE" "$SYS" "$SYS/game.cas" "CoCo 1/2 (NTSC)"
echo
"$SMOKE" "$CORE" "$SYS" "$SYS/game.cas" "CoCo 3 (NTSC)"
