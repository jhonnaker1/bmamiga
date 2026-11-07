#
# circle.mk - builds Amiberry's core for the Pi (Circle, newlib, the
# toolchain's libstdc++), with BMAmiga's Circle layer (src/port/circle) and
# zlib, as three static libraries in CIRCLE_BUILD:
#
#   libamiberry.a   Amiberry's libretro core, built by Amiberry's
#                   libretro/Makefile, with bridge.cpp
#   libbmacircle.a  BMAmiga's threads, POSIX functions, thread-local storage
#                   and clock (src/port/circle/*.c, src/port/clock.c)
#   libz.a          zlib (third_party/zlib)
#
# Included by the Makefile; set before it:
#   ROOT          BMAmiga's folder (absolute)
#   CIRCLE_BUILD  where to build (absolute)
#   TOOLCHAIN     the aarch64-none-elf toolchain's folder (absolute)
#
# Amiberry is built unmodified: what it needs that Circle hasn't is in
# src/port/circle/include (headers, and bma_circle.h included before each
# file), and Amiberry's build leaves out what needs threads, sockets or
# signals (NO_STD_THREAD, NO_HOST_SIGNALS, NO_CHD_SPECIAL_FILES). See
# src/port/circle/README.md.
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#

CIRCLE_STDLIB := $(ROOT)/third_party/circle-stdlib
CIRCLE_PREFIX := $(TOOLCHAIN)/bin/aarch64-none-elf-
CIRCLE_GCC_INC := $(TOOLCHAIN)/lib/gcc/aarch64-none-elf/15.2.1/include
CIRCLE_CXX_INC := $(TOOLCHAIN)/aarch64-none-elf/include/c++/15.2.1

# The Pi 4's and 5's ARM; Circle's and newlib's headers.
CIRCLE_TARGET := -mcpu=cortex-a72 -mlittle-endian -D__circle__ -DAARCH=64 -DRASPPI=$(RASPPI) -D_GNU_SOURCE
CIRCLE_C_INCLUDES := -nostdinc -I$(ROOT)/src/port/circle/include \
	-isystem $(CIRCLE_STDLIB)/install/aarch64-none-circle/include -isystem $(CIRCLE_GCC_INC) \
	-isystem $(CIRCLE_STDLIB)/include -isystem $(CIRCLE_STDLIB)/libs/circle/include \
	-isystem $(CIRCLE_STDLIB)/libs/circle/addon
CIRCLE_CXX_INCLUDES := -nostdinc -I$(ROOT)/src/port/circle/include \
	-isystem $(CIRCLE_CXX_INC) -isystem $(CIRCLE_CXX_INC)/aarch64-none-elf -isystem $(CIRCLE_CXX_INC)/backward \
	-isystem $(CIRCLE_STDLIB)/install/aarch64-none-circle/include -isystem $(CIRCLE_GCC_INC) \
	-isystem $(CIRCLE_STDLIB)/include -isystem $(CIRCLE_STDLIB)/libs/circle/include \
	-isystem $(CIRCLE_STDLIB)/libs/circle/addon
CIRCLE_FORCED := -include $(ROOT)/src/port/circle/include/bma_circle.h

# Amiberry's time and sleeps are BMAmiga's (src/port/clock.c). Its JIT is
# on: in indirect mode (memory through Amiberry's banks: direct mode is
# chosen when the emulator starts, before the Amiga Setup's hard reset, and
# needs accesses to the I/O holes trapped, which on bare metal has no
# SIGSEGV), in the heap, which circle_jit_exec_patch.diff lets hold code.
CIRCLE_AMIBERRY_FLAGS := -Dclock_gettime=bma_clock_gettime -Dclock_gettime_nsec_np=bma_clock_gettime_nsec_np \
	-Dnanosleep=bma_nanosleep -Dusleep=bma_usleep \
	$(CIRCLE_CXX_INCLUDES) $(CIRCLE_TARGET) -DBMAMIGA_BMNET=3 -DBMAMIGA_JIT=1 $(CIRCLE_FORCED)

CIRCLE_AMIBERRY_LIB := $(CIRCLE_BUILD)/libamiberry.a
CIRCLE_PORT_LIB := $(CIRCLE_BUILD)/libbmacircle.a
CIRCLE_ZLIB_LIB := $(CIRCLE_BUILD)/libz.a
CIRCLE_LIBS := $(CIRCLE_AMIBERRY_LIB) $(CIRCLE_PORT_LIB) $(CIRCLE_ZLIB_LIB)

# The core is built again from scratch when its flags change (its
# objects don't depend on them), as for the simulator.
$(CIRCLE_BUILD)/amiberry.flags: FORCE
	@mkdir -p $(CIRCLE_BUILD)
	@if [ "$$(cat $@ 2>/dev/null)" != "$(CIRCLE_AMIBERRY_FLAGS)" ]; then \
	    rm -rf $(CIRCLE_BUILD)/amiberry; echo "$(CIRCLE_AMIBERRY_FLAGS)" > $@; fi

# Amiberry's libretro/Makefile builds the core as a static library (as for
# the simulator), with what Circle hasn't left out: std::thread (CHD's work
# queues run at once), signals, and MAME's socket and terminal file names.
# bridge.cpp is built with the core's flags; SLiRP's and bsdsocket's socket
# calls go to bmnet (src/port/bmnet.h).
$(CIRCLE_AMIBERRY_LIB): $(CIRCLE_BUILD)/amiberry.flags FORCE
	@mkdir -p $(CIRCLE_BUILD)/amiberry
	@$(MAKE) -s -C $(ROOT)/third_party/amiberry/libretro \
	    platform=unix ARCH=aarch64 fpic= \
	    CC=$(CIRCLE_PREFIX)gcc CXX=$(CIRCLE_PREFIX)g++ AR=$(CIRCLE_PREFIX)ar \
	    ZLIB_CFLAGS=-I$(ROOT)/third_party/zlib ZLIB_LIBS= \
	    STATIC_LINKING=1 OBJDIR=$(CIRCLE_BUILD)/amiberry TARGET=$@ \
	    NO_STD_THREAD=1 NO_HOST_SIGNALS=1 NO_CHD_SPECIAL_FILES=1 \
	    NLOHMANN_JSON_INCLUDE=$(ROOT)/third_party/nlohmann-json \
	    EXTRA_SOURCES=$(ROOT)/src/port/bridge.cpp \
	    CPPFLAGS="$(CIRCLE_AMIBERRY_FLAGS)" NET_CFLAGS="-include $(ROOT)/src/port/bmnet.h" $@

# BMAmiga's Circle layer.
CIRCLE_PORT_SRCS := circle/threads.c circle/posix.c circle/tls.c clock.c bmnet.c
CIRCLE_PORT_OBJS := $(addprefix $(CIRCLE_BUILD)/port/,$(notdir $(CIRCLE_PORT_SRCS:.c=.o)))

$(CIRCLE_BUILD)/port/%.o: $(ROOT)/src/port/circle/%.c $(wildcard $(ROOT)/src/port/circle/include/*.h)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CIRCLE_PREFIX)gcc $(CIRCLE_C_INCLUDES) $(CIRCLE_TARGET) $(CIRCLE_FORCED) -O2 -g -std=gnu11 \
	    -I$(ROOT)/third_party/amiberry/libretro/libco -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

$(CIRCLE_BUILD)/port/clock.o: $(ROOT)/src/port/clock.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CIRCLE_PREFIX)gcc $(CIRCLE_C_INCLUDES) $(CIRCLE_TARGET) $(CIRCLE_FORCED) -O2 -g -std=gnu11 \
	    -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

# The Amiga's network's sockets (SLiRP's, src/port/bmnet.h).
$(CIRCLE_BUILD)/port/bmnet.o: $(ROOT)/src/port/bmnet.c $(ROOT)/src/port/bmnet.h $(ROOT)/src/host.h
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CIRCLE_PREFIX)gcc $(CIRCLE_C_INCLUDES) $(CIRCLE_TARGET) $(CIRCLE_FORCED) -O2 -g -std=gnu11 \
	    -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

$(CIRCLE_PORT_LIB): $(CIRCLE_PORT_OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(CIRCLE_PREFIX)ar rcs $@ $^

# zlib: what Amiberry uses (inflate, deflate, crc32, compress), not gz*.
CIRCLE_ZLIB_SRCS := adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil
CIRCLE_ZLIB_OBJS := $(addprefix $(CIRCLE_BUILD)/zlib/,$(addsuffix .o,$(CIRCLE_ZLIB_SRCS)))

$(CIRCLE_BUILD)/zlib/%.o: $(ROOT)/third_party/zlib/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CIRCLE_PREFIX)gcc $(CIRCLE_C_INCLUDES) $(CIRCLE_TARGET) -O2 -g -std=gnu11 -w -c -o $@ $<

$(CIRCLE_ZLIB_LIB): $(CIRCLE_ZLIB_OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(CIRCLE_PREFIX)ar rcs $@ $^

.PHONY: FORCE
