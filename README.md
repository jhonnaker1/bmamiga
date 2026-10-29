# BMAmiga

BMAmiga is a bare metal Amiga emulator for the Raspberry Pi 4 and 5. It
runs [Amiberry](https://github.com/BlitterStudio/amiberry), the Amiga
emulator built on WinUAE, directly on the hardware, with no Linux
underneath, the same way [BMC64](https://github.com/randyrossi/bmc64) does
for the Commodore 64, [BMA800XL](https://github.com/aminch/bma800xl) for the
Atari 800XL and [BMCoco](https://github.com/jhonnaker1/bmcoco) for the Tandy
Color Computer.

BMAmiga is based on [BMCoco](https://github.com/jhonnaker1/bmcoco), which is
based on [BMA800XL](https://github.com/aminch/bma800xl) by Adam Minchinton,
which is based on [BMC64](https://github.com/randyrossi/bmc64) by Randy
Rossi. The emulator is replaced by Amiberry; the kernel, display, sound,
network and web UI come from those projects.

**BMAmiga is in development.** It runs in the PC simulator
(`tools/hostsim`), and its Raspberry Pi 4 and 5 kernels build, but it
hasn't been tried on a real Pi yet. See [the User Guide](docs/USER_GUIDE.md),
[Installation](docs/INSTALLATION.md) and [the other documents](docs/README.md).

## How it works

Amiberry is built as its libretro core (`third_party/amiberry/libretro`):
the emulator without its SDL3 window and its GUI, which gives its pictures
and sound and takes its input through the libretro API. It is linked into
BMAmiga as a library (`src/port/amiberry.mk`), and `src/port/platform.c` is
the "frontend" it talks to, on top of the kernel services in `src/host.h`:

- Pictures go to the display, locked to its refresh when it runs at the
  Amiga's rate (50 Hz for a PAL Amiga), with Amiberry's clock following the
  frames shown (`src/port/clock.c`).
- Sound is kept in step with the display.
- USB keyboards (Amiberry's usual PC layout: the Windows keys are the Amiga
  keys), the mouse, and gamepads or GPIO joysticks as the joystick.
- F12 opens BMAmiga's menu: floppy drives DF0: to DF3:, the model (Amiga
  500, 500+, 600, 1200, 4000, CD32, CDTV), warp (also F11), resets, video,
  sound, network.

## ROMs

Kickstart ROMs are copyrighted, so they are not included. Copy yours into
the SD card's `roms` folder: `kick13.rom`, `kick31.rom` and so on, or Amiga
Forever's `amiga-os-*.rom` files with their `rom.key`, which Amiberry
recognises by their contents. Without any, the Amiga starts with AROS, the
free replacement Kickstart that Amiberry comes with.

## The PC simulator

```
tools/hostsim/run_tests.sh
BMAMIGA_ROMS=/path/to/roms BMAMIGA_WORKBENCH=/path/to/workbench13.adf tools/hostsim/run_tests.sh
```

The simulator builds on macOS. The first build compiles Amiberry, which
takes a few minutes. The tests
boot AROS, open the menu, choose disks and models and check the display
lock and the sound; with a Kickstart 1.3 ROM and a Workbench 1.3 disk they
boot Workbench, type at its CLI and move the mouse.

## License

GPL 3 (see LICENSE). Amiberry is GPL 3 too; the code it and BMAmiga
include from other projects is listed in
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md), with its notices in
`sdcard/licenses`.
