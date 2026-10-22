#!/bin/bash
#
# check_link.sh LIB...: lists the symbols the libraries need that neither
# they nor Circle, newlib, libstdc++ and libgcc define, and fails if any
# isn't one BMAmiga's kernel (src/host.h) or Circle's linker script gives.
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#
set -euo pipefail

NM=$TOOLCHAIN/bin/aarch64-none-elf-nm
CS=$ROOT/third_party/circle-stdlib
LIBS=("$@")
SYSTEM=("$CS/install/aarch64-none-circle/lib/libc.a" "$CS/install/aarch64-none-circle/lib/libm.a"
        "$CS/install/aarch64-none-circle/lib/libcirclenewlib.a" "$CS/libs/circle/lib/libcircle.a"
        "$TOOLCHAIN/aarch64-none-elf/lib/libstdc++.a"
        "$TOOLCHAIN/lib/gcc/aarch64-none-elf/15.2.1/libgcc.a")
TMP=$(mktemp -d)
"$NM" -u "${LIBS[@]}" 2>/dev/null | awk 'NF==2{print $2}' | sort -u > "$TMP/undef"
"$NM" --defined-only "${LIBS[@]}" "${SYSTEM[@]}" 2>/dev/null |
    awk 'NF==3 && $2 ~ /[TtDdBbRrVWiu]/ {print $3}' | sort -u > "$TMP/def"
comm -23 "$TMP/undef" "$TMP/def" > "$TMP/missing"
# Circle's linker script (circle.ld), and BMAmiga's kernel (src/host.h).
grep -v -x -e '__tdata_start' -e '__tdata_end' -e '__tbss_start' -e '__tbss_end' \
    -e 'host_[a-z_]*' "$TMP/missing" > "$TMP/left" || true
if [ -s "$TMP/left" ]; then
    echo "Missing for the Pi:"
    sed 's/^/  /' "$TMP/left"
    rm -rf "$TMP"
    exit 1
fi
echo "Amiberry's core links for the Pi: it needs only $(tr '\n' ' ' < "$TMP/missing")"
rm -rf "$TMP"
