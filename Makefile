#
# Makefile for the BMAmiga kernel image.
#
# Run ./make_all.sh once first; it builds circle-stdlib and then this.
# After that "make" rebuilds the kernel image for the board circle-stdlib is
# built for: kernel8-rpi4.img (Pi 4) or kernel_2712.img (Pi 5). BMAmiga
# needs a 64-bit Pi 4 or 5.
# ./build_sdcard.sh stages everything for the SD card in build/sdcard.
#
# Amiberry's core, BMAmiga's Circle layer (src/port/circle) and zlib are
# built by src/port/circle/circle.mk into build/pi4 or build/pi5.
#

ROOT := $(CURDIR)

ifeq ($(wildcard third_party/circle-stdlib/Config.mk),)
$(error circle-stdlib is not configured. Run ./make_all.sh first)
endif

include third_party/circle-stdlib/Config.mk
# RASPPI (the board) is in Circle's own Config.mk, which circle-stdlib's
# configure writes and Circle's Rules.mk reads; it's needed here first to
# pick the board's display and sound code.
include $(CIRCLEHOME)/Config.mk

ifneq ($(filter 4 5,$(RASPPI))-$(AARCH),$(RASPPI)-64)
$(error BMAmiga needs a 64-bit Pi 4 or 5: ./make_all.sh pi4 or pi5)
endif

BMAMIGA_VERSION ?= $(shell tools/version.sh --semver)
BMAMIGA_GIT ?= $(shell tools/version.sh --git)
AMIBERRY := third_party/amiberry
AMIBERRY_VERSION := $(shell tools/amiberry_version.sh)

# circle.mk's settings: where to build, and the toolchain's folder (for
# its C++ headers).
CIRCLE_BUILD := $(ROOT)/build/pi$(RASPPI)
TOOLCHAIN := $(abspath $(dir $(shell command -v $(PREFIX64)gcc))/..)

OBJS = src/main.o src/kernel.o src/logging.o src/options.o \
       src/port/platform.o src/port/keys.o src/port/menu.o src/port/osd.o \
       src/port/fswrap.o \
       src/network.o src/network_time_sync.o \
       src/webui/webui.o src/webui/webui_http.o src/webui/webui_fs.o \
       src/webui/webui_machine.o src/webui/webui_assets.o \
       src/sdcard/sd_fs_fatfs.o src/sdcard/sd_fs_file.o

# Display and sound: Circle's framebuffer and HDMI sound drivers (src/av/,
# see docs/PI4.md and docs/PI5.md), and the 3D GPU's CRT effect.
OBJS += src/av/video.o src/av/sound.o src/av/hdmisound.o src/av/scaler.o \
        src/kms/common/filter.o src/kms/common/modes.o \
        src/v3d/gpu.o src/v3d/draw.o src/v3d/crt.o \
        src/v3d/shaders.o
# The direct display driver: the Pi 4's (src/kms/pi4/) or the Pi 5's
# (src/kms/pi5/), with shared filter kernels and mode calculations.
ifeq ($(RASPPI),4)
OBJS += src/kms/pi4/dlist.o src/kms/pi4/hvs.o src/kms/pi4/display.o \
        src/kms/pi4/phy.o src/kms/pi4/modeset.o
else
OBJS += src/kms/pi5/dlist.o src/kms/pi5/hvs.o src/kms/pi5/display.o \
        src/kms/pi5/phy.o src/kms/pi5/modeset.o \
        src/pi45/board.o src/pi45/fan.o
endif
# Smooth scaling and the CRT shader are drawn by the ARM: -O3 lets GCC turn
# their per-pixel loops into NEON code (tools/pi4scaler tests the same).
src/av/scaler.o: OPTIMIZE = -O3

include $(CIRCLEHOME)/Rules.mk
include src/port/circle/circle.mk

INCLUDE := $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE) -I $(CIRCLEHOME)/addon/fatfs

# See src/port/fswrap.c, and posix_memalign(), gettimeofday() and realpath()
# in src/port/circle/posix.c
LDFLAGS += --wrap=stat --wrap=free --wrap=gettimeofday --wrap=realpath

VERSION_DEFINES = -DBMAMIGA_VERSION=\"$(BMAMIGA_VERSION)\" \
                  -DBMAMIGA_GIT=\"$(BMAMIGA_GIT)\" -DAMIBERRY_VERSION=\"$(AMIBERRY_VERSION)\"
CFLAGS += $(VERSION_DEFINES)
CPPFLAGS += $(VERSION_DEFINES)

# The port's C files talk to Amiberry through libretro.h, and the menu uses
# Amiberry's on-screen keyboard font (osdep/imgui_osk_font.h).
src/port/%.o src/port/%.d: CFLAGS += -I src/port -I $(AMIBERRY)/libretro \
	-I $(AMIBERRY)/libretro/libretro-common/include -I $(AMIBERRY)/src -std=gnu11

# The web UI's pages are compiled in. Regenerate them when they change.
WEBUI_ASSETS = $(shell find src/webui/assets -type f)
src/webui/webui_assets.c: $(WEBUI_ASSETS) tools/gen_webui_assets.py
	@echo "  GEN   $@"
	@python3 tools/gen_webui_assets.py

$(TARGET).img: $(CIRCLE_LIBS)

# Amiberry's core first: BMAmiga's Circle layer and zlib give it what
# newlib and Circle don't, and must come before them.
LIBS := $(CIRCLE_LIBS) \
	$(CIRCLE_STDLIB_INSTALL_DIR)/lib/libm.a \
	$(CIRCLE_STDLIB_INSTALL_DIR)/lib/libc.a \
	$(CIRCLE_STDLIB_INSTALL_DIR)/lib/libcirclenewlib.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/net/libnet.a \
	$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
	$(CIRCLEHOME)/addon/wlan/libwlan.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

EXTRACLEAN += $(OBJS) src/*.o src/port/*.d src/*.d src/webui/*.d src/sdcard/*.d \
              src/av/*.o src/av/*.d src/kms/common/*.o src/kms/common/*.d \
              src/kms/pi4/*.o src/kms/pi4/*.d \
              src/kms/pi5/*.o src/kms/pi5/*.d src/v3d/*.o src/v3d/*.d \
              src/pi45/*.o src/pi45/*.d
.PHONY: distclean

distclean: clean
	rm -rf build/pi4 build/pi5

-include $(DEPS)
