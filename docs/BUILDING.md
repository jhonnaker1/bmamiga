# Build Instructions

BMAmiga's kernels build on Linux (Debian, Ubuntu, WSL) and macOS, for the
Raspberry Pi 4 and 5 (64-bit). The PC simulator (below) builds on macOS
only for now.

## Prerequisites

On Debian or Ubuntu:

```
sudo apt-get install build-essential git python3 curl xz-utils
```

On macOS (Apple silicon or Intel), with the Xcode command line tools and
[Homebrew](https://brew.sh):

```
xcode-select --install
brew install bash gnu-getopt
```

circle-stdlib's `configure` needs bash 4 or later and GNU `getopt`, which
`make_all.sh` puts first on the PATH on macOS. newlib's build would like
`makeinfo` (texinfo) for its manuals and Circle's Wi-Fi firmware download
uses `wget`; without them, `make_all.sh` and `make_wifi_firmware.sh` use
stand-ins (the manuals aren't needed, and `curl` does the download).

The Arm GNU Toolchain 15.2.rel1 is downloaded into
`~/.cache/bmamiga/toolchains` the first time a build needs it
(`get_gnu_toolchain.sh`), built for your computer (Linux or macOS):
`aarch64-none-elf`, for the 64-bit kernels. If it can't be downloaded,
`BMAMIGA_ALLOW_ANY_TOOLCHAIN=1` lets the build use whatever
`aarch64-none-elf-gcc` is on the PATH.

A build needs no download beyond the toolchain and the Wi-Fi firmware:
nlohmann's `json.hpp`, which Amiberry's own build would fetch from GitHub,
is kept in `third_party/nlohmann-json`.

## Downloading the files

```
git clone --recursive <BMAmiga repository URL>
```

or, in an existing clone, `git submodule update --init --recursive`.

## Building an SD card

```
./build_sdcard.sh
```

builds everything from clean and stages the boot partition in
`build/sdcard`, with a kernel for each board: `kernel8-rpi4.img` (with
`armstub8-rpi4.bin`) for the Pi 4 B and Pi 400, and `kernel_2712.img` for
the Pi 5, Pi 500 and Pi 500+. `./build_sdcard.sh pi4` or
`./build_sdcard.sh pi5` builds just one. Each kernel is about 30 MB, and
62 MB in memory with its BSS: Amiberry's 68000 interpreter alone is 24 MB
of code. Circle's `KERNEL_MAX_SIZE` is set to 80 MB (`make_all.sh`); a
kernel past it would halt at start without a word, so `build_sdcard.sh`
checks each one. Copy the contents of that folder to the root of a FAT32
formatted SD card.

While developing, `./build_sdcard.sh --skip-patches` keeps the third party
libraries already built and only rebuilds the kernel, which takes seconds.
The libraries are built for one board at a time, so without a board it
rebuilds the kernel for the board built last; a board they aren't built for
is built from clean. With
[networking](USER_GUIDE.md#networking-and-the-web-ui) on, a new kernel can
then be uploaded to the Pi from the web UI and the Pi
rebooted from there, without touching the SD card.

## The individual steps

1. `./clean_all.sh` cleans everything, including the third party builds,
   and resets Circle to its pinned revision.
2. `./make_all.sh [pi4|pi5] [--skip-patches]` applies `src/patches/` to
   Circle and circle-newlib, builds circle-stdlib (Circle and newlib) and
   the Circle addons for one board (pi4 if none is given), then the
   kernel: `kernel8-rpi4.img` for pi4, which also builds Circle's
   `armstub8-rpi4.bin`, or `kernel_2712.img` for pi5. The Makefile builds
   Amiberry's core, BMAmiga's Circle layer and zlib first
   (`src/port/circle/circle.mk`, into `build/pi4` or `build/pi5`). It only needs running again after
   `clean_all.sh` or a change to the patches or circle-stdlib. To change
   board, run `clean_all.sh` first (make_all.sh refuses otherwise).
3. `BMAMIGA_AARCH=64 source get_gnu_toolchain.sh && make` (in bash)
   rebuilds just the kernel.
4. `./make_wifi_firmware.sh DIR` puts the Wi-Fi firmware in `DIR/firmware`
   (`build_sdcard.sh` does this).

`config.txt` has a section per board that picks its kernel. The emulator
runs on core 1, core 0 looks after USB, and cores 2 and 3 help draw the
smoothed and CRT pictures. `make -C tools/picore` builds Amiberry's core on
its own and checks that it needs nothing Circle, newlib and the kernel
don't give it. See [the Circle layer](../src/port/circle/README.md) for
what Amiberry needs on Circle.

## Testing on a PC

`tools/hostsim` builds the emulator port (Amiberry's core plus
`src/port/`, with BMAmiga's cooperative threads) for the PC, with a
simulated display, sound and input in place of the Raspberry Pi. It runs
scripted tests: booting AROS, the display lock and timer pacing, a Pi too
slow for the frame lock (`BMAMIGA_SIM_FRAME_MS` makes each frame take
that long), the menu, floppy disks, models, warp, and checking the sound
stays in step with the display. The first build compiles Amiberry, which
takes a few minutes.

```
tools/hostsim/run_tests.sh
BMAMIGA_ROMS=/path/to/roms BMAMIGA_WORKBENCH=/path/to/workbench13.adf tools/hostsim/run_tests.sh
```

Kickstart ROMs can't be distributed, so the tests start the Amiga with
AROS. With `BMAMIGA_ROMS` set to a folder holding a Kickstart 1.3 (as
`kick13.rom`, or Amiga Forever's `amiga-os-130.rom` with its `rom.key`)
and `BMAMIGA_WORKBENCH` to a Workbench 1.3 disk, they boot Workbench, type
at its CLI and move the mouse too. Screenshots (`.ppm`) are written to
`tools/hostsim/out/`.

To play with the port on the PC, run it live:

```
tools/hostsim/live.sh /path/to/your/roms
```

and open http://127.0.0.1:8064/ in a browser: the screen, keyboard, mouse,
sound and gamepads (through the browser's Gamepad API) are in the page.
The simulator's SD card is `tools/hostsim/out/live`; floppy images copied
there can be inserted from the menu. This runs BMAmiga's port and Amiberry
on the PC, not the Pi kernel. The web page is served on 127.0.0.1 only.

The simulator can also be driven by a script of key presses and
screenshots; see the top of `tools/hostsim/host_sim.c` for the commands.
[SIMULATOR.md](SIMULATOR.md) has more: setting it up, debugging it with
gdb, lldb or strace, and getting a log from the Pi.

`tools/pi4scaler` tests the shared Pi 4/5 ARM smooth scaling and CRT shader
(`src/av/scaler.*`): every pixel is checked against crt-pi worked out in
floating point, and the pictures are written to `tools/pi4scaler/out/`.
`tools/pi4kms` tests the Pi 4/5 direct display drivers (`src/kms/`, see
[PI4.md](PI4.md) and [PI5.md](PI5.md)): shared filter kernels, EDID, mode
choice, HDMI packets and audio clocks, and each board's display lists and
PHY settings.
Both come from BMA800XL.

```
tools/pi4scaler/run_tests.sh
tools/pi4kms/run_tests.sh
```

`build_sdcard.sh` runs all three first and stops if any test fails;
`--skip-tests` leaves them out.

## The web UI

The web UI's pages are in `src/webui/assets/`. They are compiled into the
kernel through `src/webui/webui_assets.c`, which the Makefile regenerates
with `tools/gen_webui_assets.py` whenever an asset changes.

## Layout

| Path                         | What                                        |
|------------------------------|---------------------------------------------|
| `src/main.cpp`, `kernel.*`   | Circle kernel; USB, GPIO, SD card, cores    |
| `src/network.*`              | Ethernet / Wi-Fi start up (from BMC64)      |
| `src/network_time_sync.*`    | Clock from the network (from BMC64)         |
| `src/webui/`                 | Web UI server, file manager and pages (from BMC64) |
| `src/sdcard/`                | SD card helpers used by the web UI (from BMC64) |
| `src/options.*`              | `cmdline.txt` options                       |
| `src/host.h`                 | Interface between kernel and emulator       |
| `src/port/platform.c`        | The libretro frontend Amiberry's core talks to: video, sound, input, timing, the main loop |
| `src/port/bridge.cpp`        | BMAmiga's direct calls into Amiberry: disks, resets, warp |
| `src/port/keys.c`            | USB keys to libretro keys (Amiberry's PC layout) |
| `src/port/clock.c`           | Amiberry's clock, following the frames shown when locked to the display |
| `src/port/menu.c`, `osd.c`   | The menu and its on-screen display          |
| `src/port/fswrap.c`          | `stat()` of the SD card's root, which FatFs can't do (`--wrap=stat`) |
| `src/port/amiberry.mk`       | Builds Amiberry's libretro core as a library |
| `src/port/circle/`           | What Amiberry needs on Circle: threads, POSIX functions, thread-local storage, headers (see its README) |
| `src/pi45/`                  | The Pi 5's power button and fan            |
| `src/patches/`               | Changes applied to Circle and circle-newlib by `make_all.sh` |
| `sdcard/`                    | Files for the SD card                       |
| `third_party/amiberry`       | Amiberry (submodule)                        |
| `third_party/zlib`           | zlib (submodule)                            |
| `third_party/circle-stdlib`  | Circle and newlib (submodule)               |
| `src/av/`                    | Pi 4/5 video, ARM scaling / CRT shader and sound |
| `src/v3d/`                   | Pi 4/5 GPU driver and CRT rendering         |
| `src/kms/common/`            | Pi 4/5 filter kernels and mode calculations |
| `src/kms/pi4/`               | Pi 4 direct display driver and PHY          |
| `src/kms/pi5/`               | Pi 5 direct display driver and PHY          |
| `tools/hostsim`              | PC test harness                             |
| `tools/picore`               | Builds Amiberry's core for the Pi alone and checks its link |
| `tools/pi4scaler`            | PC test of the Pi 4/5 ARM scaling and CRT rendering |
| `tools/pi4kms`               | PC test of the Pi 4 display driver's calculations |
