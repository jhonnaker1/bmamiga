#!/bin/bash
#
# Boots a BMAmiga Pi 4 kernel in QEMU's raspi4b for a while and prints what
# it sent to the serial port, for faults in the kernel's start up (before
# the SD card), which the PC simulator can't show: it runs the emulator,
# not Circle. That is how the first Pi 5's black screen was found (an
# assert in a static constructor, before the log was set up).
#
#   tools/qemu/run_qemu.sh [KERNEL] [SECONDS]
#
# KERNEL is kernel8-rpi4.img (./make_all.sh pi4 builds it; default the
# one in the root); a working kernel prints Circle's first lines and
# BMAmiga's version, then waits on hardware QEMU's model hasn't (its SD
# card, as Circle drives it): QEMU tests the start, not the card, USB or
# network. To see where a kernel stops, run QEMU with
# -monitor telnet:127.0.0.1:45454,server,nowait, ask the monitor for
# "info registers", and look the PC and the frame pointer chain (X29) up
# in kernel8-rpi4.elf with aarch64-none-elf-addr2line.
#
# Needs qemu-system-aarch64 (brew install qemu); the device tree comes
# from Circle's boot files.
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#

set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
KERNEL=${1:-$ROOT/kernel8-rpi4.img}
SECONDS_TO_RUN=${2:-20}
# The device tree: the SD card build's (build_sdcard.sh), or Circle's boot
# files (downloaded by its boot/Makefile).
DTB=$ROOT/build/sdcard/bcm2711-rpi-4-b.dtb
if [ ! -f "$DTB" ]; then
    DTB=$ROOT/third_party/circle-stdlib/libs/circle/boot/bcm2711-rpi-4-b.dtb
    [ -f "$DTB" ] || make -s -C "$ROOT/third_party/circle-stdlib/libs/circle/boot" > /dev/null
fi
OUT=${TMPDIR:-/tmp}/bmamiga-qemu.$$
mkdir -p "$OUT"
qemu-system-aarch64 -M raspi4b -kernel "$KERNEL" -dtb "$DTB" -serial file:"$OUT/serial.txt" \
    -display none -monitor none -daemonize -pidfile "$OUT/qemu.pid" 2>/dev/null || exit 1
perl -e "select(undef, undef, undef, $SECONDS_TO_RUN)"
kill "$(cat "$OUT/qemu.pid")" 2>/dev/null
cat "$OUT/serial.txt"
rm -rf "$OUT"
