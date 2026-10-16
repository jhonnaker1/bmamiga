#!/bin/bash
#
# Runs BMAmiga in the host simulator in real time, shown in a web page:
#
#   tools/hostsim/live.sh [ROM_DIRECTORY] [PORT]
#
# then open http://127.0.0.1:PORT/ (default port 8064). ROM_DIRECTORY holds
# your Kickstart ROMs (kick13.rom, kick31.rom, or Amiga Forever's
# amiga-os-*.rom with rom.key); without it the Amiga starts with AROS. The
# simulator's SD card is tools/hostsim/out/live: put floppy disks (ADF) in
# its disks folder. Choosing Reboot (or Shut Down) in the menu starts it
# again. Ctrl+C stops it.
#
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROMS=${1:-}
PORT=${2:-8064}
CARD=$HERE/out/live

make -s -C "$HERE" -j4 || exit 1

mkdir -p "$CARD/disks"
rm -rf "$CARD/roms"
if [ -n "$ROMS" ]; then
    ln -s "$ROMS" "$CARD/roms"
else
    mkdir -p "$CARD/roms"
    cp "$HERE"/../../third_party/amiberry/roms/aros-*.bin "$CARD/roms/"
fi

cd "$CARD" || exit 1
while true; do
    BMAMIGA_LIVE=$PORT BMAMIGA_CMDLINE="root=$CARD ${BMAMIGA_CMDLINE:-}" \
        "$HERE/build/bmamiga_sim" < /dev/null
    status=$?
    [ $status -ne 0 ] && exit $status
    echo "[live] restarting"
    sleep 1
done
