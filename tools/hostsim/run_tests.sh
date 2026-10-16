#!/bin/bash
#
# Runs the emulator port on the PC (see host_sim.c) through a set of
# scripted tests and checks the results.
#
#   tools/hostsim/run_tests.sh
#   BMAMIGA_ROMS=/path/to/roms BMAMIGA_WORKBENCH=/path/to/workbench13.adf \
#       tools/hostsim/run_tests.sh
#
# Kickstart ROMs can't be shipped, so the tests start the Amiga with AROS,
# the free replacement Kickstart that Amiberry comes with. With
# BMAMIGA_ROMS set to a folder holding a Kickstart 1.3 ROM (kick13.rom, or
# Amiga Forever's amiga-os-130.rom with its rom.key) and BMAMIGA_WORKBENCH
# to a Workbench 1.3 disk, Workbench is booted, typed at and clicked on
# too. Output, including screenshots (.ppm), goes to tools/hostsim/out/.
#

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$HERE/out
SIM=$HERE/build/bmamiga_sim

make -s -C "$HERE" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)" || exit 1

rm -rf "$OUT"
mkdir -p "$OUT/aros"
cp "$ROOT"/third_party/amiberry/roms/aros-*.bin "$OUT/aros/"

FAILED=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1: $2"; FAILED=$((FAILED + 1)); }

# run NAME REFRESH ROMS CMDLINE SCRIPT: runs the simulator in out/NAME, a
# stand-in SD card with ROMS as its roms folder (and a disks folder), for
# at most 5 minutes.
run() {
    local name=$1 refresh=$2 roms=$3 cmdline=$4 script=$5
    mkdir -p "$OUT/$name/disks"
    [ -e "$OUT/$name/roms" ] || ln -s "$roms" "$OUT/$name/roms"
    (cd "$OUT/$name" && printf "$script" | BMAMIGA_REFRESH=$refresh \
        BMAMIGA_CMDLINE="root=. $cmdline" perl -e 'alarm 300; exec @ARGV' "$SIM" > log.txt 2>&1)
}

# check_log NAME REGEX MESSAGE: the log must contain REGEX
check_log() {
    if grep -Eq "$2" "$OUT/$1/log.txt"; then
        pass "$1: $3"
    else
        fail "$1" "$3 (see out/$1/log.txt)"
    fi
}

# check_cfg NAME LINE MESSAGE: bmamiga.cfg must have LINE (a regex)
check_cfg() {
    if grep -Eq "$2" "$OUT/$1/bmamiga.cfg" 2>/dev/null; then
        pass "$1: $3"
    else
        fail "$1" "$3 (bmamiga.cfg: $(tr '\n' ' ' < "$OUT/$1/bmamiga.cfg" 2>/dev/null))"
    fi
}

# check_frames NAME: no audio underruns once started (frame 300), and every
# frame presented
check_frames() {
    local log=$OUT/$1/log.txt line frame presented late
    late=$(grep "audio underrun" "$log" | sed 's/.*frame \([0-9]*\):.*/\1/' | awk '$1 > 300' | wc -l)
    if [ "$late" -eq 0 ]; then
        pass "$1: no audio underruns once started"
    else
        fail "$1" "$late audio underruns after frame 300 (see out/$1/log.txt)"
    fi
    line=$(grep "frames presented" "$log" | tail -1)
    frame=$(echo "$line" | sed 's/.*frame \([0-9]*\):.*/\1/')
    presented=$(echo "$line" | sed 's/.*: \([0-9]*\) frames presented/\1/')
    if [ -n "$frame" ] && [ "${presented:-0}" -ge "$frame" ]; then
        pass "$1: no dropped frames ($presented/$frame)"
    else
        fail "$1" "frames presented: $line"
    fi
}

# PPM helpers: python3 ppm.py FILE ...
cat > "$OUT/ppm.py" <<'EOF'
import sys
def load(path):
    data = open(path, 'rb').read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    return int(fields[1]), int(fields[2]), data[pos + 1:]
cmd = sys.argv[1]
if cmd == 'colour':            # FILE R G B: has a pixel of that colour
    w, h, px = load(sys.argv[2])
    want = bytes(int(v) for v in sys.argv[3:6])
    sys.exit(0 if any(px[i:i + 3] == want for i in range(0, len(px), 3)) else 1)
elif cmd == 'colours':         # FILE N: has at least N colours
    w, h, px = load(sys.argv[2])
    seen = {px[i:i + 3] for i in range(0, len(px), 3)}
    sys.exit(0 if len(seen) >= int(sys.argv[3]) else 1)
elif cmd == 'size':            # FILE W H
    w, h, px = load(sys.argv[2])
    sys.exit(0 if (w, h) == (int(sys.argv[3]), int(sys.argv[4])) else 1)
elif cmd == 'pointer':         # FILE: where the (red) mouse pointer is
    w, h, px = load(sys.argv[2])
    pts = [(i % w, i // w) for i in range(w * h)
           if px[3 * i] > 200 and px[3 * i + 1] < 90 and px[3 * i + 2] < 90]
    print('%d %d' % (min(p[0] for p in pts), min(p[1] for p in pts)) if pts else 'none')
elif cmd == 'differ':          # A B: the pictures differ
    sys.exit(0 if load(sys.argv[2])[2] != load(sys.argv[3])[2] else 1)
EOF
ppm() { python3 "$OUT/ppm.py" "$@"; }

echo "AROS, with no Kickstart, on a 50 Hz display"
run aros 50 "$OUT/aros" "" '1000 shot boot.ppm\n1000 audio\n1001 quit\n'
check_log aros "machine 50\.[0-9]+ Hz, synchronised to the display" "a PAL Amiga locked to 50 Hz"
check_log aros "Amiga 500" "an Amiga 500 (the default model)"
check_frames aros
if ppm size "$OUT/aros/boot.ppm" 752 576 && ppm colours "$OUT/aros/boot.ppm" 8; then
    pass "aros: AROS's boot screen, 752x576"
else
    fail aros "no AROS boot screen (see out/aros/boot.ppm)"
fi
if grep -q "migration" "$OUT/aros/log.txt"; then
    fail aros "imported a desktop Amiberry's settings"
else
    pass "aros: no desktop Amiberry settings imported"
fi

echo "On a 60 Hz display"
run timer 60 "$OUT/aros" "" '1900 quit\n'
check_log timer "display 60\.00 Hz, machine 50\.[0-9]+ Hz, timer paced" "a PAL Amiga timer paced on 60 Hz"
# Amiberry's own waits, pacing itself, don't count as the Pi's work.
check_log timer "load [0-9]+% .*, 0 of [0-9]+ frames late" "its pacing isn't counted as load"

echo "Menu"
# F12 opens it: the selection bar is orange (Workbench's).
run menu 50 "$OUT/aros" "" '200 key 0x45 1\n201 key 0x45 0\n230 shot menu.ppm\n240 key 0x29 1\n241 key 0x29 0\n280 shot closed.ppm\n281 quit\n'
if ppm size "$OUT/menu/menu.ppm" 640 480 && ppm colour "$OUT/menu/menu.ppm" 255 137 0; then
    pass "menu: F12 opens the menu"
else
    fail menu "no menu (see out/menu/menu.ppm)"
fi
if ppm size "$OUT/menu/closed.ppm" 752 576; then
    pass "menu: Esc closes it, back to the Amiga"
else
    fail menu "the menu didn't close (see out/menu/closed.ppm)"
fi

echo "Floppy disks"
# Menu, Floppy Drives, DF0:, the disk (the browser starts in disks): kept
# in bmamiga.cfg, and put in DF0: at the next boot.
mkdir -p "$OUT/floppy/disks"
head -c 901120 /dev/zero > "$OUT/floppy/disks/blank.adf"
run floppy 50 "$OUT/aros" "" '200 key 0x45 1\n201 key 0x45 0\n220 key 0x28 1\n221 key 0x28 0\n240 key 0x28 1\n241 key 0x28 0\n260 key 0x28 1\n261 key 0x28 0\n300 quit\n'
check_cfg floppy "^df0=.*disks/blank\.adf" "the disk is kept in bmamiga.cfg"
(cd "$OUT/floppy" && printf '100 quit\n' | BMAMIGA_REFRESH=50 BMAMIGA_CMDLINE="root=." \
    perl -e 'alarm 300; exec @ARGV' "$SIM" > log2.txt 2>&1)
if grep -q "DF0: .*blank.adf" "$OUT/floppy/log2.txt"; then
    pass "floppy: back in DF0: at the next boot"
else
    fail floppy "not in DF0: at the next boot (see out/floppy/log2.txt)"
fi

echo "Model"
# Menu, down to Model, Enter, down two (Amiga 500+): kept for the next
# boot, which then starts that model.
run model 50 "$OUT/aros" "" '200 key 0x45 1\n201 key 0x45 0\n220 key 0x51 1\n221 key 0x51 0\n240 key 0x28 1\n241 key 0x28 0\n260 key 0x51 1\n261 key 0x51 0\n270 key 0x51 1\n271 key 0x51 0\n280 key 0x28 1\n281 key 0x28 0\n300 quit\n'
check_cfg model "^model=A500\+" "Amiga 500+ kept in bmamiga.cfg"
(cd "$OUT/model" && printf '100 quit\n' | BMAMIGA_REFRESH=50 BMAMIGA_CMDLINE="root=." \
    perl -e 'alarm 300; exec @ARGV' "$SIM" > log2.txt 2>&1)
if grep -q "BMAmiga: Amiga 500+" "$OUT/model/log2.txt"; then
    pass "model: the next boot is an Amiga 500+"
else
    fail model "the next boot isn't an Amiga 500+ (see out/model/log2.txt)"
fi

echo "Warp"
run warp 50 "$OUT/aros" "" '200 key 0x44 1\n201 key 0x44 0\n400 shot warp.ppm\n401 key 0x44 1\n402 key 0x44 0\n600 quit\n'
check_log warp "BMAmiga: warp on" "F11 turns warp on"
check_log warp "BMAmiga: warp off" "and off again"

if [ -n "${BMAMIGA_ROMS:-}" ] && [ -n "${BMAMIGA_WORKBENCH:-}" ]; then
    echo "Workbench 1.3 (Kickstart from $BMAMIGA_ROMS)"
    mkdir -p "$OUT/wb/disks"
    cp "$BMAMIGA_WORKBENCH" "$OUT/wb/disks/wb13.adf"
    chmod u+w "$OUT/wb/disks/wb13.adf"
    printf 'df0=disks/wb13.adf\n' > "$OUT/wb/bmamiga.cfg"
    # Ctrl+D stops the Startup-Sequence; echo hello at the CLI; then the
    # mouse moves 300 pixels across and 80 down.
    script='2600 key 0xe0 1\n2601 key 0x07 1\n2602 key 0x07 0\n2603 key 0xe0 0\n2650 shot before.ppm\n2700 type echo hello\\n\n2950 shot typed.ppm\n'
    for i in $(seq 0 19); do script="$script$((3000 + i)) mouse 15 4 0\n"; done
    script="${script}3050 shot moved.ppm\n3050 audio\n3051 quit\n"
    run wb 50 "$BMAMIGA_ROMS" "" "$script"
    check_log wb "KS ROM v1\.3|Kickstart ROM.*1\.3|kick13" "Kickstart 1.3 found"
    check_log wb "DF0: disks/wb13\.adf" "Workbench in DF0:"
    check_frames wb
    if ppm differ "$OUT/wb/before.ppm" "$OUT/wb/typed.ppm"; then
        pass "wb: typing reached the CLI"
    else
        fail wb "the CLI didn't change when typed at (see out/wb/typed.ppm)"
    fi
    before=$(ppm pointer "$OUT/wb/typed.ppm")
    after=$(ppm pointer "$OUT/wb/moved.ppm")
    if [ "$before" != none ] && [ "$after" != none ] && [ "$before" != "$after" ] \
        && [ $(( ${after% *} - ${before% *} )) -ge 200 ]; then
        pass "wb: the mouse moves the pointer ($before -> $after)"
    else
        fail wb "the pointer didn't follow the mouse ($before -> $after)"
    fi
fi

echo
if [ $FAILED -eq 0 ]; then
    echo "All tests passed."
else
    echo "$FAILED test(s) failed."
fi
exit $((FAILED > 0))
