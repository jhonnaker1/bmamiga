#!/bin/sh
#
# Prints the version of Amiberry in third_party/amiberry, from its
# CMakeLists.txt (for example "8.4.1"), as its libretro Makefile reads it.
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#
cd "$(dirname "$0")/.." || exit 1
sed -n 's/^.*VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' third_party/amiberry/CMakeLists.txt | head -1
