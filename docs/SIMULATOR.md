# Testing and debugging BMAmiga on a Mac

BMAmiga isn't tested in QEMU. On the Pi it runs bare metal, on the Circle
kernel with no Linux underneath, and QEMU's Raspberry Pi machines don't
emulate enough of the Pi (its display, HDMI sound, USB and Wi-Fi as
BMAmiga uses them) to run it. There would be nothing to strace either:
strace follows a Linux program's system calls, and BMAmiga makes none.

What BMAmiga has instead is a **PC simulator** (`tools/hostsim`): the same
emulator code that runs on the Pi (Amiberry's libretro core and BMAmiga's
port of it, `src/port/`, with BMAmiga's own cooperative threads, as on the
Pi), built as an ordinary program for macOS, with a simulated
display, sound, keyboard, mouse, joysticks and SD card in place of the
Pi's. Because it's an ordinary program, **lldb and the rest work on it** as on any
other program.

## What it can and can't show

The simulator runs the Amiga emulation, the menu, floppy disks, hard
drives and CDs, models, warp, and BMAmiga's pacing and sound logic. A problem in any of those
usually shows up in it too.

It does **not** run the Pi side: the Circle kernel, the Pi's display and
sound drivers, USB, Wi-Fi and Circle's network stack, the SD card driver,
and what BMAmiga gives Amiberry on Circle alone (memory from `mmap()`,
thread-local storage, newlib's file functions). And a PC is much faster
than a Pi; `BMAMIGA_SIM_FRAME_MS` (below) makes each frame slow, to stand
in for a Pi that can't keep up.

So the simulator is the first place to look, and if a problem doesn't show
up there, the Pi's log (at the end of this page) is the next.

## Setting it up

The simulator builds on macOS for now: its link line
(`tools/hostsim/Makefile`) uses macOS's frameworks and iconv library, which
Amiberry's core needs there. On Linux those would need replacing.

Install Xcode's command line tools (`xcode-select --install`), which
include `clang`, `make`, `git`, zlib and the `lldb` debugger, and Python 3.
No Raspberry Pi toolchain is needed for the simulator.

Get the source, with Amiberry (a git submodule, with its own libco):

```
git clone https://github.com/jhonnaker1/bmamiga.git
cd bmamiga
git submodule update --init --recursive third_party/amiberry
```

Build and run the tests:

```
tools/hostsim/run_tests.sh
```

The first build compiles Amiberry, which takes a few minutes. Kickstart ROMs
can't be distributed, so without any the tests start the Amiga with AROS.
To boot Workbench, point them at a Kickstart 1.3 and a Workbench 1.3 disk
(copies: the tests read them, but it's safest not to hand them the
originals):

```
BMAMIGA_ROMS=/path/to/roms BMAMIGA_WORKBENCH=/path/to/workbench13.adf tools/hostsim/run_tests.sh
```

Each test runs in its own folder under `tools/hostsim/out/`, which stands
in for the SD card, with its log (`log.txt`) and screenshots (`.ppm`).

## Using it live

```
tools/hostsim/live.sh /path/to/roms
```

then open <http://127.0.0.1:8064/> in a browser. The Amiga's screen,
sound, keyboard, mouse and gamepads are in the page, and F12 opens
BMAmiga's menu as on the Pi. The simulator's SD card is
`tools/hostsim/out/live`: copy floppy images into its `disks` folder, and
`bmamiga.cfg` into it if you want the same settings as your Pi's card.
Options from `cmdline.txt` go in `BMAMIGA_CMDLINE`:

```
BMAMIGA_CMDLINE="model=A1200" tools/hostsim/live.sh /path/to/roms
```

Its messages (the same as the Pi's log, Amiberry's included) go to the
terminal. Ctrl+C stops it.

## Debugging it

### Where is it stuck? (lldb)

Start it live as above, get it into the state where it's stuck, and attach
a debugger to it from another terminal.

```
lldb -p $(pgrep -f build/bmamiga_sim)
(lldb) thread backtrace all
```

The backtrace shows where each thread is, in BMAmiga's and Amiberry's
source (the simulator is built with debugging information). When the
emulation is running normally, the main thread is somewhere under
`retro_run()` (the Amiga) or `host_video_present()` (waiting for the next
frame). Amiberry's own threads are BMAmiga's cooperative ones
(`src/port/circle/threads.c`), on libco stacks, so they show up within the
main thread rather than as threads of their own. Type `continue` to let it
go on, or `detach` and `quit` to leave it running.

To run it under the debugger from the start instead, run it from its SD
card folder:

```
cd tools/hostsim/out/live
BMAMIGA_LIVE=8064 BMAMIGA_CMDLINE="root=." lldb ../../build/bmamiga_sim
(lldb) process launch -i /dev/null
```

and press Ctrl+C in lldb when it's stuck.

### Scripts

The simulator can also run a script of key presses, typing, mouse moves
and screenshots on its standard input, with time measured in frames, so
that a problem can be repeated exactly. For example:

```
cd tools/hostsim/out/live
printf '300 shot boot.ppm\n301 quit\n' | BMAMIGA_CMDLINE="root=." ../../build/bmamiga_sim
```

saves the screen at frame 300. The commands are listed at the top of
`tools/hostsim/host_sim.c`, and `tools/hostsim/run_tests.sh` has many
examples.

Useful settings, as environment variables:

| Variable                     | Does                                                        |
|------------------------------|-------------------------------------------------------------|
| `BMAMIGA_CMDLINE`            | Options as in `cmdline.txt` (`root=.` is the SD card)       |
| `BMAMIGA_REFRESH`            | The display's refresh rate (default 60; 50 for a PAL Amiga's lock) |
| `BMAMIGA_SIM_FRAME_MS`       | Make each frame's emulation take this long, like a slow Pi  |
| `BMAMIGA_SIM_RATE`           | The sound output's rate (48000 for a Pi 4 or 5's HDMI)      |
| `BMAMIGA_SIM_WAV`            | Save the sound to this WAV file                             |
| `BMAMIGA_SIM_CPU`            | Print how long each frame takes to emulate on the PC        |
| `BMAMIGA_IP`                 | Pretend the network is up with this address (from frame 100) |
| `BMAMIGA_WIFI`               | Pretend Wi-Fi is running, with a few networks to choose from |
| `BMAMIGA_REFRESH_LATER`      | Change the display's refresh rate to this at frame 30: a display slightly off its first measurement |

## The kernel's start, in QEMU

The simulator runs the emulator, not the Pi's kernel. A fault in the
kernel's own start (before the SD card is read, so before there is a log)
shows on the Pi as a black screen and nothing else. QEMU's Raspberry Pi 4B
(`raspi4b`) runs the Pi 4 kernel that far:

```
./make_all.sh pi4
tools/qemu/run_qemu.sh
```

A good kernel prints Circle's first lines and BMAmiga's version, then waits
on hardware QEMU's model hasn't (the SD card, as Circle drives it): QEMU
shows the start, not the card, USB or the network. The script says how to
find where a kernel stops. The first Pi 5's black screen (0.1.0) was found
this way, against BMNeXT's Pi 4 kernel, which starts in QEMU, as a check.

## The network

`run_tests.sh` tests the Amiga's network without an Amiga stack: bmnet's
own test first (`build/bmnet_test`, from `bmnet_test.c`: sockets, pipes,
names, with a DNS server of its own), then the script command
`netprobe dhcp` / `netprobe dns`, which talks to SLiRP as a network card
would (src/port/bridge.cpp) and logs the answers.

## On the Pi itself

On the Pi there is no debugger or strace, but there is a log. Add
`enable_logging=file` to the line in `cmdline.txt`, and BMAmiga writes
`bmamiga.log` in the root of the SD card (and to the serial port: GPIO14 TX
and GPIO15 RX at 115200 baud, for a USB serial adapter, which keeps working
even when the SD card doesn't).

What the log shows:

- The Pi, the display and sound, the ARM's clock, the Amiga model, and
  Amiberry's messages (the Kickstart it found, the disks it read).
- Every 10 seconds the Amiga's speed, the ARM's clock and temperature, and
  where the time went (see Logging in the [User Guide](USER_GUIDE.md)).
- Every 30 seconds the load: how much of each frame the emulation takes.
  Over 100% means the Pi can't keep up.
- If the emulation stops moving for 3 seconds, `Emulator has not moved for
  3 s; last seen:` and what it was doing (starting the emulator, emulating,
  showing a frame, running Circle's tasks such as the network...). That
  line is the most useful one for a hang.
- A crash on any core: the crash report, written out before the Pi stops.

When something goes wrong, the most helpful report has:

1. What you did, step by step, and what happened.
2. The BMAmiga version and which Pi.
3. `bmamiga.log` from the card, with `enable_logging=file` (taking the card
   out right after the problem; the log is written as it goes).
4. Your `cmdline.txt` and `bmamiga.cfg`.
5. The Amiga model, the Kickstart, and which disk, or the file if it's free
   to share.
