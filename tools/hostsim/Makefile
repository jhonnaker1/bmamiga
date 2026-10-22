#
# Builds the PC test harness for the emulator port (see host_sim.c).
#
#   make -C tools/hostsim
#   tools/hostsim/run_tests.sh
#
# Amiberry's libretro core is built as a static library first, with
# Amiberry's own libretro/Makefile and src/port/amiberry.mk, under
# build/amiberry (it takes a few minutes the first time).
#

ROOT := $(abspath ../..)
AMIBERRY := $(ROOT)/third_party/amiberry
OUT := $(abspath build)

CC ?= cc
CXX ?= c++

# Amiberry's time and sleeps are BMAmiga's (src/port/clock.c), and its
# threads (src/port/circle/threads.c, the Pi's cooperative ones, tested
# here too).
BMA_THREADS := $(foreach f,create join detach self mutex_init mutex_destroy mutex_lock \
                 mutex_trylock mutex_unlock cond_init cond_destroy cond_signal \
                 cond_broadcast cond_wait cond_timedwait,-Dpthread_$(f)=bma_pthread_$(f)) \
               -Dsched_yield=bma_sched_yield
BMA_FLAGS := -Dclock_gettime=bma_clock_gettime -Dclock_gettime_nsec_np=bma_clock_gettime_nsec_np \
             -Dnanosleep=bma_nanosleep -Dusleep=bma_usleep $(BMA_THREADS)

CFLAGS = -O2 -g -std=gnu11 -I $(ROOT)/src/port -I $(AMIBERRY)/libretro \
         -I $(AMIBERRY)/libretro/libretro-common/include -I $(AMIBERRY)/src \
         -DBMAMIGA_HOST_LIBC

AMIBERRY_LIB := $(OUT)/amiberry/libamiberry.a
PORT_OBJS := $(addprefix $(OUT)/port/,platform.o keys.o osd.o menu.o clock.o) $(OUT)/port/threads.o
OBJS := $(PORT_OBJS) $(OUT)/host_sim.o $(OUT)/live.o

all: $(OUT)/bmamiga_sim

$(OUT)/bmamiga_sim: $(OBJS) $(AMIBERRY_LIB)
	$(CXX) -o $@ $(OBJS) $(AMIBERRY_LIB) -lz -liconv -lpthread -lm \
	    -framework CoreFoundation -framework DiskArbitration -framework IOKit

# The core is built again from scratch when BMA_FLAGS change (its objects
# don't depend on the flags).
$(OUT)/amiberry/flags: FORCE
	@mkdir -p $(dir $@)
	@if [ "$$(cat $@ 2>/dev/null)" != "$(BMA_FLAGS)" ]; then \
	    rm -rf $(OUT)/amiberry; mkdir -p $(OUT)/amiberry; echo "$(BMA_FLAGS)" > $@; fi

$(AMIBERRY_LIB): $(OUT)/amiberry/flags FORCE
	@$(MAKE) -s -C $(AMIBERRY)/libretro -f Makefile -f $(ROOT)/src/port/amiberry.mk \
	    BMA_OBJDIR=$(OUT)/amiberry BMA_LIB=$@ BMA_FLAGS="$(BMA_FLAGS)" $@

$(OUT)/port/%.o: $(ROOT)/src/port/%.c $(ROOT)/src/port/bmamiga.h $(ROOT)/src/port/osd.h \
                 $(ROOT)/src/host.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -Wno-unused-parameter -c -o $@ $<

$(OUT)/port/threads.o: $(ROOT)/src/port/circle/threads.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BMA_THREADS) -I $(AMIBERRY)/libretro/libco -Wall -Wextra -c -o $@ $<

$(OUT)/host_sim.o: host_sim.c live.h $(ROOT)/src/host.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -c -o $@ $<

$(OUT)/live.o: live.c live.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wall -Wextra -DLIVE_HTML=\"$(abspath live.html)\" -c -o $@ $<

clean:
	rm -rf $(OUT)

.PHONY: all clean FORCE
