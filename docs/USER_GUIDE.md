# BMAmiga User Guide

BMAmiga turns a Raspberry Pi 4 or 5 into an Amiga. It runs
[Amiberry](https://github.com/BlitterStudio/amiberry), the Amiga emulator
built on WinUAE, on the bare hardware, so it starts in a few seconds and can
be switched off at any time.

**BMAmiga is in development.** Everything here works in the PC simulator
(see [SIMULATOR.md](SIMULATOR.md)); it hasn't been tried on a real Pi yet.

## The SD card

| File or folder   | What it is                                                  |
|------------------|-------------------------------------------------------------|
| `kernel8-rpi4.img`, `kernel_2712.img` | BMAmiga for the Pi 4 and Pi 400, and the Pi 5, 500 and 500+ |
| `start4.elf`, `fixup4.dat`, `armstub8-rpi4.bin`, `*.dtb`, `overlays/` | Raspberry Pi firmware |
| `config.txt`     | Raspberry Pi firmware settings                              |
| `cmdline.txt`    | BMAmiga's options (see Options)                             |
| `bmamiga.cfg`    | Settings changed in the menu (written by BMAmiga)           |
| `roms/`          | The Kickstart ROMs: you supply them (see Kickstart ROMs)    |
| `disks/`         | For your floppy, hard disk and CD images (any folder works) |
| `saves/`         | What Amiberry saves                                         |
| `firmware/`      | Wi-Fi firmware                                              |

Amiberry writes the Amiga's changes to a floppy straight into its image,
and BMAmiga makes sure they are on the card every second: wait a second
after the Amiga's drive light goes out before switching off.

## Kickstart ROMs

The Amiga's Kickstart ROMs are copyrighted, so they are not included. Copy
yours into the card's `roms` folder. Amiberry recognises a Kickstart by its
contents, so Amiga Forever's files (`amiga-os-130.rom`,
`amiga-os-310-a1200.rom` and so on) work as they are, with their `rom.key`
beside them. It also looks for names such as `kick13.rom` (Amiga 500) and
`kick31.rom` (Amiga 1200 and 4000) first; `roms/ROMS.txt` on the card has
the list for each model.

Without a Kickstart the Amiga starts with AROS, the free replacement
Kickstart that comes with Amiberry. Much Amiga software runs on it, but
not all.

## The menu

**F12** (or a gamepad's **Home** button, or **Select+Start**) opens the
menu; **Esc** goes back a page and F12 closes it. The arrow keys or D-pad
move, **Enter** or **A** chooses; **Space** also ticks or clears a `[ ]`
item. In a list, typing a letter jumps to the next item starting with it.

| Item           | What it does |
|----------------|--------------|
| Floppy Drives  | Insert a floppy image in DF0: to DF3:, or eject one (see Floppy disks) |
| Hard Drives and CD | Hard disk images or folders as DH0: to DH3:, and a CD image (see Hard drives and CD) |
| Model          | Amiga 500 (Kickstart 1.3 or 1.2), 500+, 600, 1200 (2 MB, or with 8 MB Fast RAM), 4000/030, 4000/040, CD32, CDTV. Takes effect after a reboot |
| Amiga Setup    | The Kickstart, CPU, CPU speed, chipset, chip, slow and fast RAM, sound filter and stereo separation, over the model's own (see Amiga Setup) |
| Warp (F11)     | Run as fast as the Pi can, for loading and long waits; F11 turns it on and off |
| Reset, Hard Reset | The Amiga's reset (as Ctrl+Amiga+Amiga), or power off and on |
| Video          | The frame lock, the resolution (Pi 5), the CRT effect and its curvature, smooth scaling |
| Sound          | Volume, and HDMI or the headphone jack |
| Network        | Ethernet or Wi-Fi, and the web UI (see below) |
| About          | Versions, the model, the display, how busy the Pi is, the network |
| Reboot, Shut Down | Restart BMAmiga, or halt the Pi. Shut Down also from a keyboard's power key (Fn+F10 on a Pi 400) or the Pi 5's power button |

The menu's settings are kept in `bmamiga.cfg`.

## Amiga Setup

*Amiga Setup* changes the model's hardware. Each item starts at *Model's*,
what the chosen model has, and choosing anything else is kept in
`bmamiga.cfg`:

| Item              | Choices | `bmamiga.cfg` |
|-------------------|---------|---------------|
| Kickstart         | the model's, or a file in `roms` | `kickstart` (a path) |
| CPU               | 68000, 68010, 68020, 68030, 68040 | `cpu` |
| CPU Speed         | real (cycle-exact), or *Fastest*: as fast as the Pi can | `cpu_speed` (`max`) |
| Chipset           | OCS, ECS, AGA | `chipset` (`ocs`, `ecs`, `aga`) |
| Chip RAM          | 512K, 1M, 2M | `chip_ram` (in KB) |
| Slow RAM          | none, 512K, 1M, 1.5M, 1.8M | `slow_ram` (in KB) |
| Fast RAM          | none, 1M, 2M, 4M, 8M | `fast_ram` (in MB) |
| Sound Filter      | off, emulated, on (the Amiga's low-pass filter) | `sound_filter` |
| Stereo Separation | mono, 30%, 50%, 70%, 100% | `stereo_separation` (0 to 10) |
| Network           | none, bsdsocket.library, uaenet.device, an A2065 card (see The Amiga's network) | `amiga_network` (`bsdsocket`, `uaenet`, `a2065`) |
| JIT (68020+)      | off, on: Amiberry's JIT compiler, which turns the Amiga's code into the Pi's (see below) | `jit` (`on`) |

**The JIT** makes a 68020 or later CPU much faster (several times, more on
code that works in its registers than on code that works in memory), for
Workbench, AmiKit and other software that wants a fast Amiga. It runs the
CPU at full speed and not cycle-exact, so games that need a real A1200's
timing are better without it; a 68000 (the A500's) can't use it. On the Pi
it is Amiberry's JIT in its *indirect* mode: the Amiga's memory through
Amiberry's own access functions, as the Pi can't trap the Amiga's I/O the
way a desktop does.

What the JIT needs, in Amiga Setup:

- **A 68020 or later CPU.** *Amiga 1200, 2 MB + 8 MB Fast*, *Amiga
  4000/030* and *Amiga 4000/040* have one; with another model, set *CPU*
  to 68020, 68030 or 68040. On a 68000 or 68010 *JIT* On does nothing.
- **Fast RAM.** The JIT only compiles code in fast RAM; code in chip RAM
  runs as before, so *Amiga 1200, 2 MB* (no fast RAM) gains nothing until
  *Fast RAM* is set (8M). AmigaOS loads programs into fast RAM when there
  is some.
- **JIT (68020+) On, then the hard reset** the menu offers: the JIT starts
  with the Amiga.

On sets the rest itself, and these need not be changed: *CPU Speed* as
*Fastest*, not cycle-exact, Amiberry's "more compatible" CPU off (with it
the ARM JIT compiles nothing), a 16 MB cache, and the indirect mode. Don't
undo them in `bmamiga.uae`: `cachesize=0`, `cpu_compatible=true` or
`cycle_exact=true` there turn the JIT off again, and an MMU (`mmu_model`)
is left out while the JIT is on. A Kickstart 2.04 or later is needed for a 68030 or
68040 (3.x for the A1200 and A4000).

The log says whether it is on: `JIT=CPU=16384` and `actual translation
cache size : 16384 KB` (`JIT=0`, or `prefetch` in that line, means it is
off). To measure it, `tools/bench/make_bench_adf.py` makes two boot
floppies (see `bench` in the options below). On the Pi, in 1000s of loops
a second (register / memory): a Pi 4's interpreter 5 / 5, its JIT 92 / 37,
a Pi 5's JIT 183 / 79.

The hardware changes take effect from a hard reset, which the menu offers
at once (as switching a real Amiga off and on); the sound's at once. Not
every mix works: an AGA chipset wants a Kickstart 3.x, and a 68030 or
68040 wants a Kickstart 2.04 or later.

### bmamiga.uae: the rest of Amiberry's settings

Any of Amiberry's (and WinUAE's) settings can go in `bmamiga.uae` in the
card's root, one a line as in a `.uae` configuration file, for example:

```
# 8 MB of Zorro III RAM and a faster blitter
z3mem_size=8
immediate_blits=true
```

They go over the model and the Amiga Setup, at each start and hard reset;
lines starting with `#` or `;` are comments. Drives can be added this way
too, after the menu's DH0: to DH3:, with names of their own, for example a
folder on the card as `AK0:`, called `AmiKit`, that the Amiga boots from:

```
filesystem2=rw,AK0:AmiKit:disks/AmiKit,0
```

(paths are from the card's root). The Amiga's memory can be up to 2 GB in
all, on a Pi with the memory for it (a Pi 4 or 5 with 4 GB or more), as
for 512 MB of Zorro III RAM (`z3mem_size=512`) and an RTG card. `bmamiga.uae.example` on the
card has more. A wrong name or value is ignored (with
`enable_logging=file`, the log says which).

## Floppy disks

*Floppy Drives* shows the four drives. Choose one to insert a disk: the
file browser starts in the `disks` folder (then wherever the last disk came
from), and shows floppy images: `.adf` (plain), `.adz` (gzipped), `.dms`,
`.ipf` (the copy-protected originals' format) and `.zip`. Choosing a drive
that holds a disk offers to eject it. A disk put in DF0: takes you back to
the Amiga, which reads it at once.

The disks in the drives are remembered in `bmamiga.cfg` (`df0` to `df3`)
and put back in at the next start.

*Speed* sets how fast the drives read: *Real* is a real Amiga drive's
speed (Workbench 1.3 takes about a minute to start from a floppy, as it
did), 2x, 4x and 8x are faster, and *Turbo* as fast as the Amiga takes
the data. Some copy-protected games need *Real*. It is `floppy_speed` in
`bmamiga.cfg`, and changes at once.

From the web UI, clicking a floppy image puts it in DF0: and resets the
Amiga, to start a game or a boot disk from a browser.

## Hard drives and CD

*Hard Drives and CD* shows DH0: to DH3: and the CD drive.

A hard drive is either:

- **A hard disk image** (*Hard Disk Image...*): `.hdf`, or `.hdz`
  (compressed). Images with a Rigid Disk Block (made with HDToolBox, or
  Amiga Forever's `workbench-311.hdf` and the like) have their own
  partitions and names; others are one partition.
- **A folder on the card** (*Folder...*): browse into the folder and
  choose *Use This Folder*. The Amiga sees it as a disk named after the
  folder, and can read and write it: files the Amiga saves there are
  ordinary files on the card, which a PC can read too. Amiberry keeps
  what a PC's FAT file system can't (the Amiga's protection bits, dates
  and comments) in small `.uaem` files beside them; leave those where
  they are.

A hard disk image can be up to 4 GB, the most a file on FAT32 can be;
bigger ones go on an exFAT second partition on the card, **SD2:**, which the
file browser shows at the card's top (see [Installation](INSTALLATION.md),
*A second partition for big disk images*).

**Copying an Amiga folder to the card.** The Amiga allows characters in
file names that FAT32 doesn't (`\ * ? " < > |`, and a name ending in a dot
or a space). Amiberry stores those characters as `%xx` and gives the Amiga
the names back as they were, so files the Amiga saves are fine. A folder
made on a Mac or Linux PC (an Amiga distribution, or AmiKit's), whose
files never went through Amiberry, can still have such names, and copying
it to the card loses them. Copy it with `tools/copy_amiga_folder.py`
(Python 3; macOS, Linux or Windows), which names them as Amiberry would:

```
python3 tools/copy_amiga_folder.py /path/to/MyAmigaFolder /Volumes/BMAMIGA/disks/MyAmigaFolder
```

`--dry-run` lists the names it would change, copying nothing. Accented
letters (ä, é, ß) in names show on the Amiga as they are, and on a Mac or
PC too.

A change to the hard drives takes effect at a **Hard Reset**, which the
menu offers. The Amiga boots from DH0: when DF0: is empty; a floppy in
DF0: comes first. Amiga Forever's `workbench-135.hdf` (with Kickstart 1.3)
and `workbench-311.hdf` (with Kickstart 3.1 on an Amiga 1200) both boot
as DH0:.

The **CD** drive takes `.iso`, `.cue` (with its `.bin`), `.ccd`, `.nrg`,
`.mds` and `.chd` images:

- On a **CD32** or **CDTV** it is the console's own drive, and a disc put
  in is used at once.
- On the **other Amigas** it is a CD drive on Amiberry's SCSI device
  (`uaescsi.device`, unit 0), which Amiberry mounts as **CD0:**, a
  read-only volume named as the disc is, so the Amiga needs no CD file
  system of its own. The first disc needs a Hard Reset (the menu offers
  one); after that discs can be changed as the Amiga runs. Software that
  talks to the drive itself (a CD player, or another CD file system) can
  use `uaescsi.device` unit 0.

The drives are remembered in `bmamiga.cfg` (`dh0` to `dh3`, `cd0`) and
mounted when BMAmiga starts.

From the web UI, clicking a hard disk image starts the Amiga from it as
DH0:, and clicking a CD image starts it with that disc in the CD drive.

## Keyboard

USB keyboards work as an Amiga keyboard, with Amiberry's usual PC layout:

| PC key                   | Amiga                                     |
|--------------------------|-------------------------------------------|
| Left and right Windows (or Cmd) keys, or Insert and Home | Left and right Amiga |
| Page Down                | Help                                      |
| Delete                   | Del                                       |
| Alt                      | Alt                                       |
| F1 to F10                | F1 to F10                                 |
| F11                      | BMAmiga: Warp on and off                  |
| F12                      | BMAmiga: the menu                         |

Ctrl with both Amiga keys resets the Amiga, as on the real one.

## Mouse and joystick

The USB mouse is the Amiga's mouse, in its first port. `mouse_speed` in
`cmdline.txt` (percent, 100 by default) changes how far it moves.

The first gamepad is the joystick in the second port: the left stick or the
D-pad moves, **A** fires (the Amiga's one button), **B** is the second
button some games use. A joystick wired to the GPIO header works too, with
`gpio_joysticks=1` in `cmdline.txt` (the wiring is BMC64's).

## Display and timing

The Amiga's picture is shown locked to the display when it runs at the
Amiga's own rate: 50 Hz for a PAL Amiga, 60 Hz for an NTSC one. A Pi 4 or
5 switches its display to that rate itself, so every Amiga frame is shown
once and scrolling is smooth; Amiberry's clock then follows the frames
shown. Otherwise the Amiga keeps its own time and frames are shown as they
come. *About* shows which is in use, and `vsync=on` or `vsync=off` in
`cmdline.txt` (or *Frame Lock* in the menu) forces one or the other.

`video_standard=pal` or `ntsc` in `cmdline.txt` chooses the Amiga's video
standard; `auto` (the default) leaves it to the model.

**When the Pi can't keep up**: if frames take longer to emulate than the
display shows them for (a fast Amiga model, or a demanding game: the
68000 is emulated without a JIT on the Pi), waiting for the display would
halve the speed. BMAmiga then stops waiting for it and runs as fast as the
Pi can until frames fit again; the log says so, and every 30 seconds it
logs how busy the Pi is. *About* shows the load too. The Amiga 500 is the
easiest for the Pi; the Amiga 4000/040 the hardest.

`statusline=1` shows Amiberry's status line: the drive lights and the
speed.

## Networking and the web UI

BMAmiga can join your network over Ethernet or Wi-Fi and serve a web page
for managing the SD card from any browser on the same network, so files can
be copied to and from the Pi without taking the card out. It is the web UI
from BMC64. The Amiga can use the network too (see The Amiga's network).

Turn it on in the menu's *Network*. For Ethernet, set *Network* to Ethernet
and reboot. For Wi-Fi, choose *Scan for Wi-Fi Networks*: the first time,
it offers to switch Wi-Fi on and reboot; after that it lists the networks
it can hear, strongest first. Pick yours: it fills in *Wi-Fi Network*, and
if the network needs a password, *Wi-Fi Password* is next. Then reboot.
Set *Wi-Fi Country* to your two letter country code too; it decides which
channels are allowed. A hidden network can be typed in under *Wi-Fi
Network* instead. Or, in `cmdline.txt` (on the same single line as the
other options):

- Ethernet: `network=ethernet`
- Wi-Fi: `network=wifi`, and copy `wpa_supplicant.conf.example` to
  `wpa_supplicant.conf` in the root of the card with your network's name,
  password and country.

Wi-Fi needs the files in the `firmware` folder, which `build_sdcard.sh` puts
there.

Once the Pi has an address, it is shown at the bottom of the screen for a
few seconds (`WEB UI: HTTP://192.168.1.50/`) and written to the log.
Open that address in a browser.

The web UI has:

- **Dashboard**: the Pi's model, address, uptime, temperature, power and
  throttling warnings and free space on the card. Buttons to **Hard Reset**
  the Amiga, **Reboot** BMAmiga, **Shut Down** (the Pi halts; switch it
  off, then on again to restart; a Pi 400 switches itself off, and its
  power key starts it again), and **Disable Web UI** until the next reboot.
- **Files**: browse the SD card; upload (or drag and drop) files into a
  folder, download, rename, delete, and create folders. Clicking a floppy,
  hard disk or CD image **starts it**: it goes in DF0:, DH0: or the CD
  drive, and the Amiga is reset.
  `cmdline.txt`, `config.txt`, `bmamiga.cfg` and `wpa_supplicant.conf` can
  be **edited** in the browser; changes take effect after a reboot, and
  the previous version is kept as `.bak`.

Uploads can't overwrite `cmdline.txt`, `config.txt` or `wpa_supplicant.conf`
(edit them instead), or anything in `firmware`. Everything else, including
the kernels, can be replaced, so a new build of BMAmiga can be uploaded and
started with **Reboot**. A mistake in `cmdline.txt` or `config.txt` can stop
the Pi starting, and then needs the card in a computer (the `.bak` copy
makes that easy).

The web UI is open to anyone on your network. To protect it with a PIN, set
`webui_pin=1234` in `cmdline.txt`; the browser then asks for a user name
(anything) and the PIN. To network without the web UI, set `webui=0`.

The network also sets the Pi's clock, so files saved on the card get the
right date and time. Set `timezone` to your offset from UTC in minutes, for
example `timezone=600` for UTC+10.

## The Amiga's network

The Amiga can share the Pi's network (Ethernet or Wi-Fi, set up in the
menu's *Network*). In *Amiga Setup*, set *Network* to:

- **bsdsocket.library**: Amiberry's emulation of the Amiga's socket
  library, for Amiga programs (web browsers, mail, IRC, FTP) without a
  TCP/IP stack on the Amiga: their connections are the Pi's, and their
  names are looked up with the Pi's DNS server. This is how AmiKit is set
  up. The Amiga can't listen for connections from other computers this
  way, and `ping` doesn't work.

or, for an Amiga with its own TCP/IP stack, SLiRP: the Pi is a router for
it, which hands its connections on as its own (NAT):

- **uaenet.device**: a SANA-II network device, for any Amiga model. Point
  the Amiga's TCP/IP stack (Roadshow, AmiTCP, Miami) at `uaenet.device`,
  unit 0.
- **A2065 Card**: Commodore's Zorro II Ethernet card, for software that
  wants real hardware; its driver is `a2065.device`.

Then hard reset. With SLiRP, the Amiga gets its address by DHCP: 10.0.2.15, with the
router 10.0.2.2 and the DNS server 10.0.2.3 (to set by hand if the stack
has no DHCP; the netmask is 255.255.255.0). It can reach the Internet and
your network (web, mail, FTP, the Amiga's own clients), but nothing can
connect to it from outside, and `ping` to the Internet doesn't work (as
in SLiRP everywhere). The Pi's
network must be up before the Amiga's stack starts.

For SLiRP the Amiga needs a TCP/IP stack of its own, such as Roadshow
(whose demo is on Aminet), AmiTCP or Miami; with bsdsocket.library it
needs none (and none should be started).

## Options

`cmdline.txt` is a single line of `name=value` options separated by spaces.
The menu saves the options it changes to `bmamiga.cfg`, one `name=value`
per line, and those win over `cmdline.txt`. Either file can be edited from
the web UI.

| Option              | Values                       | Default  | What it does |
|---------------------|------------------------------|----------|--------------|
| `model`             | `A500`, `A500OG`, `A500+`, `A600`, `A1200OG`, `A1200`, `A4030`, `A4040`, `CD32`, `CDTV` | `A500` | The Amiga to emulate (the menu's *Model*) |
| `kickstart`, `cpu`, `cpu_speed`, `chipset`, `chip_ram`, `slow_ram`, `fast_ram`, `sound_filter`, `stereo_separation`, `amiga_network` | see Amiga Setup | the model's | The menu's Amiga Setup |
| `df0` ... `df3`     | paths                        | none     | The floppy images in the drives (the menu's *Floppy Drives*) |
| `dh0` ... `dh3`     | paths                        | none     | The hard drives: hard disk images or folders (the menu's *Hard Drives and CD*) |
| `cd0`               | path                         | none     | The CD image in the CD drive |
| `video_standard`    | `auto`, `pal`, `ntsc`        | `auto`   | The Amiga's video standard |
| `floppy_speed`      | `100`, `200`, `400`, `800`, `0` | `100` | The floppy drives' speed: a real drive's, faster, or `0` turbo |
| `bench`             | `0`, `1`                     | `0`      | With a floppy from `tools/bench/make_bench_adf.py`: the log's speed line every 10 s adds its loops a second, to measure the CPU (with and without the JIT) |
| `statusline`        | `0`, `1`                     | `0`      | Amiberry's status line: drive lights and speed |
| `aspect`            | number                       | `1.0`    | Multiplies the width of the picture |
| `scale`             | 0.1 to 1.0                   | `1.0`    | Height of the picture as a fraction of the screen (less for a TV that cuts off the edges) |
| `smooth`            | `0`, `1`                     | `0`      | Bilinear filtering instead of sharp pixels |
| `crt_shader`        | `0`, `1`                     | `0`      | CRT look (scanlines) from BMC64 |
| `crt_curvature`     | `0`, `1`                     | `0`      | With `crt_shader`, a curved screen |
| `crt_gpu`           | `0`, `1`                     | `1`      | The 3D GPU draws the smooth and CRT pictures; `0` has the ARM draw them (see [PI4.md](PI4.md)) |
| `vsync`             | `auto`, `on`, `off`          | `auto`   | Lock frames to the display (see Display and timing) |
| `resolution`        | `auto`, `1920x1080`, `1280x720` | `auto` | Pi 5 only, with `video_driver=kms`: the display's resolution; `auto` keeps the one the firmware chose (the Pi 5's firmware ignores `hdmi_mode`) |
| `video_driver`      | `kms`, `hvs`, `firmware`     | `kms`    | `kms` runs the display at the Amiga's exact frame rate; `hvs` leaves the mode set in `config.txt`; `firmware` uses the firmware's framebuffer, if the others give trouble (see [PI4.md](PI4.md) and [PI5.md](PI5.md)) |
| `audio_out`         | `auto`, `hdmi`, `headphones` | `auto`   | Where the sound goes (`auto`: HDMI, or the headphone jack if the display takes no sound; the Pi 5 has no jack) |
| `volume`            | 0 to 100                     | `100`    | Sound volume |
| `audio_buffer_ms`   | 20 to 500                    | `60`     | Sound buffer; raise it if the sound breaks up |
| `mouse_speed`       | percent                      | `100`    | How far the mouse moves |
| `gpio_joysticks`    | `0`, `1`                     | `0`      | A joystick on the GPIO header |
| `enable_logging`    | `off`, `uart`, `file`        | `off`    | Where the log goes (see Logging) |
| `network`           | `off`, `ethernet`, `wifi`    | `off`    | Networking (see Networking and the web UI) |
| `webui`             | `0`, `1`                     | `1`      | Web UI when networking is on |
| `webui_pin`         | digits                       | none     | PIN for the web UI |
| `timezone`          | minutes east of UTC          | `0`      | Time zone for the network clock |
| `usbpowerdelay`     | milliseconds                 | Circle's | Circle: wait for USB devices to power up (by default 300 ms, 510 ms behind a hub) |

## Logging

BMAmiga logs what it is doing, which helps when something doesn't work:
the version, the display and sound set up, gamepads coming and going,
network status, and Amiberry's own messages (which Kickstart it found,
disks inserted). Choose where it goes with `enable_logging` in
`cmdline.txt`:

- **Off** (the default, also when `enable_logging` isn't set): no log.
- **UART**: the serial port, GPIO14 TX and GPIO15 RX at 115200 baud.
  Needs a USB serial adapter.
- **File**: the serial port, and `bmamiga.log` in the root of the SD card.
  The file starts with what was logged since boot, is written to the card
  every few seconds (and is complete after a crash, a hang or switching
  off), and starts again at boot once it is over 1 MB. Download it from
  the web UI's file manager to send with a bug report. Writing it costs a
  little time on each frame, so switch it off once you have what you need.

Every 10 seconds the log has a line on how fast the Amiga runs:

```
BMAmiga: speed 100% (500 frames in 10.0 s), ARM 1800 MHz 52 C; Amiga 61%, frames: tasks 1% log 0% picture 30%
```

`speed` is the Amiga against a real one (100% is its own speed); then the
ARM's clock and the chip's temperature, and where the time went: emulating
the Amiga, and showing frames (Circle's tasks, writing the log, and the
picture, which includes waiting for the display). On a Pi that can't keep
up, `speed` stays below 100% and `Amiga` near 100%.

## Troubleshooting

**The Amiga starts with AROS, not Kickstart**: there is no Kickstart for
the model in `roms` (see Kickstart ROMs). With Amiga Forever's files,
`rom.key` must be there too.

**No picture**: uncomment `hdmi_safe=1` in `config.txt`, or try
`video_driver=hvs`, then `video_driver=firmware`, in `cmdline.txt`.

**No sound over HDMI**: try `hdmi_drive=2` in `config.txt`, or
`audio_out=hdmi` in `cmdline.txt`.

**Sound breaks up, or games run slowly**: the Pi may be falling behind
(see Display and timing): *About* and the log show the load. Choose a
lighter model (an Amiga 500 or 1200 rather than a 4000/040). Otherwise
raise `audio_buffer_ms` in `cmdline.txt`, e.g. to 100.

**Keyboard or gamepad not working**: unplug it and plug it in again; devices
can come and go while the emulator runs. Some keyboards need a longer
`usbpowerdelay` (add `usbpowerdelay=1000` to `cmdline.txt`).

**8BitDo M30 2.4G buttons are wrong**: its receiver can be left in Switch
mode, where it acts as a Switch Pro controller. With the pad connected,
hold Home (the heart) and D-pad Down for 5 seconds until the LEDs flash; it
goes back to its own mode and keeps it.

**The TV cuts off the edges of the picture or the menu**: many TVs enlarge
the picture and crop its edges (overscan). Set `scale=0.9` in
`cmdline.txt`, or turn overscan off on the TV ("Just Scan", "Screen Fit"
or "1:1").

**A disk won't load**: check it is in DF0: (*Floppy Drives*), and that the
model suits it: an old game may need an Amiga 500 with Kickstart 1.3, a
newer one an Amiga 1200. Hard Reset after changing the disk.
