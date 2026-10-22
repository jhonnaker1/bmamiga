#
# Builds Amiberry's core for the Pi 4 and 5 on its own (src/port/circle/
# circle.mk), and checks that it needs nothing more than Circle, newlib,
# libstdc++, libgcc and BMAmiga's kernel give it:
#
#   make -C tools/picore
#
# circle-stdlib must be built for the Pi 4 or 5 first (./make_all.sh pi4).
# The aarch64-none-elf toolchain is found on PATH, or set TOOLCHAIN.
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#

ROOT := $(abspath ../..)
CIRCLE_BUILD ?= $(ROOT)/build/pi
RASPPI ?= 4
TOOLCHAIN ?= $(abspath $(dir $(shell command -v aarch64-none-elf-gcc))/..)

.DEFAULT_GOAL := all
include $(ROOT)/src/port/circle/circle.mk

all: $(CIRCLE_LIBS)
	@ROOT=$(ROOT) TOOLCHAIN=$(TOOLCHAIN) ./check_link.sh $(CIRCLE_LIBS)

clean:
	rm -rf $(CIRCLE_BUILD)

.PHONY: all clean
