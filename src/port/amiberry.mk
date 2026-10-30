#
# amiberry.mk - builds Amiberry's libretro core as a static library.
#
# BMAmiga runs Amiberry through its libretro core (third_party/amiberry/
# libretro): the emulator without SDL3 or its GUI, giving pictures, sound
# and input through the libretro API, which src/port/platform.c provides.
# Amiberry's own libretro/Makefile knows the sources and flags; this file
# is read after it (make -f Makefile -f amiberry.mk) and adds a target that
# archives the objects into a static library instead of a shared one,
# with the objects in BMA_OBJDIR, so that builds for different computers
# (the PC simulator, the Pi) don't mix.
#
#   make -C third_party/amiberry/libretro -f Makefile \
#        -f $(abspath src/port/amiberry.mk) BMA_OBJDIR=/abs/dir \
#        BMA_LIB=/abs/libamiberry.a /abs/libamiberry.a
#
# This file is part of BMAmiga, a bare metal Amiga emulator for the
# Raspberry Pi. See LICENSE for licensing details.
#

ifeq ($(BMA_OBJDIR),)
$(error BMA_OBJDIR must be set)
endif
ifeq ($(BMA_LIB),)
$(error BMA_LIB must be set)
endif

# The core's objects, moved under BMA_OBJDIR: "../src/x.o" (AMIBERRY_DIR)
# becomes BMA_OBJDIR/src/x.o, "./x.o" (LIBRETRO_DIR) BMA_OBJDIR/libretro/x.o.
bma_obj = $(patsubst ./%,$(BMA_OBJDIR)/libretro/%,$(patsubst ../%,$(BMA_OBJDIR)/%,$(1)))
# BMA_EXCLUDE: objects left out (relative to BMA_OBJDIR, "src/x.o"), when
# BMA_EXTRA has BMAmiga's own in their place (the Pi's, src/port/circle).
BMA_OBJECTS := $(filter-out $(addprefix $(BMA_OBJDIR)/,$(BMA_EXCLUDE)),$(call bma_obj,$(OBJECTS)))

# BMAmiga's own calls into Amiberry (src/port/bridge.cpp), and the files
# in BMA_EXTRA (relative to src/port, without .cpp), built with the core's
# flags so that they can use its headers.
BMA_PORT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# nlohmann's json.hpp: BMAmiga's copy (third_party/nlohmann-json), first on
# the include path, so that the Makefile's rule that fetches it from GitHub
# into libretro/deps is never needed.
NLOHMANN_JSON_HEADER := $(abspath $(BMA_PORT)/../../third_party/nlohmann-json/nlohmann/json.hpp)
INCFLAGS := -I$(dir $(NLOHMANN_JSON_HEADER)).. $(INCFLAGS)
BMA_OBJECTS += $(BMA_OBJDIR)/bmamiga/bridge.o $(addprefix $(BMA_OBJDIR)/bmamiga/,$(addsuffix .o,$(BMA_EXTRA)))

$(BMA_OBJDIR)/bmamiga/%.o: $(BMA_PORT)/%.cpp | $(NLOHMANN_JSON_HEADER)
	@mkdir -p $(dir $@)
	@$(CXX) $(INCFLAGS) $(FLAGS) $(BMA_FLAGS) -std=gnu++17 -c -o $@ $<

$(BMA_LIB): $(BMA_OBJECTS)
	@mkdir -p $(dir $@)
	@rm -f $@
	$(AR) rcs $@ $^

$(BMA_OBJDIR)/libretro/%.o: ./%.cpp | $(NLOHMANN_JSON_HEADER)
	@mkdir -p $(dir $@)
	@$(CXX) $(INCFLAGS) $(FLAGS) $(BMA_FLAGS) -std=gnu++17 -c -o $@ $<

$(BMA_OBJDIR)/libretro/%.o: ./%.c
	@mkdir -p $(dir $@)
	@$(CC) $(INCFLAGS) $(FLAGS) $(BMA_FLAGS) -std=gnu99 -c -o $@ $<

# SLiRP's and bsdsocket.library's socket calls are bmnet's (src/port/
# bmnet.h): on the Pi the C library's would wait, and hold its table of
# open files while they do.
$(BMA_OBJDIR)/src/slirp/%.o $(BMA_OBJDIR)/src/slirp_uae.o $(BMA_OBJDIR)/src/osdep/bsdsocket_host.o: \
	BMA_NET_FLAGS = -include $(BMA_PORT)/bmnet.h
$(filter $(BMA_OBJDIR)/src/slirp/%,$(BMA_OBJECTS)) $(BMA_OBJDIR)/src/slirp_uae.o \
	$(BMA_OBJDIR)/src/osdep/bsdsocket_host.o: $(BMA_PORT)/bmnet.h

$(BMA_OBJDIR)/%.o: ../%.cpp | $(NLOHMANN_JSON_HEADER)
	@mkdir -p $(dir $@)
	@$(CXX) $(INCFLAGS) $(FLAGS) $(BMA_FLAGS) $(BMA_NET_FLAGS) -std=gnu++17 -c -o $@ $<

$(BMA_OBJDIR)/%.o: ../%.c
	@mkdir -p $(dir $@)
	@$(CC) $(INCFLAGS) $(FLAGS) $(BMA_FLAGS) -std=gnu99 -c -o $@ $<
