# Changelog

## 0.3.0 – 2026-11-06

Tried on a Pi 5 (8 GB): Workbench 3.1 from a folder, a Shell with the JIT,
and Amelinium showing frogfind.de over the network with 64 MB of Zorro
III RAM. Only in the simulator so far: the RTG card (a Workbench at
1280x720, 24-bit), the FPU item and folder drives' disk info. None of
these fixes has been tried on a Pi 4 yet. Thanks to wiisoldier for the
test images and testing.

- bsdsocket.library was never there (Amelinium: "no TCP stack"): Amiberry
  puts it, and uaenet.device, in the UAE Boot ROM once, as it starts, from
  the settings then, and BMAmiga gave it the Amiga Setup only after, with
  a hard reset. libretro.cpp's amiberry_main() is now BMAmiga's
  bmamiga_amiberry_main() (amiberry.mk renames it), which adds the
  setup's network to the start's command line; Network is now "(reboot)"
  in the menu. In the simulator, a slim Workbench 3.1 with Amelinium
  (from wiisoldier) shows example.com
- The JIT stopped, and the Amiga with it, once it had compiled enough
  (opening a Shell on Workbench 3.1, on a Pi 5): it takes a 16K pool for
  its blocks' records each time the last is full, and the Pi's mmap() had
  256 mappings. 4096 now, and small ones aligned to 4K, not 64K
- A folder as a hard drive (Workbench from a folder) ran at 3% on the Pi,
  with 12 s stops: FatFs has no cache, and Amiberry's folder drives look
  names up all the time. circle_fatfs_sectorcache_patch.diff: a 4 MB
  write-through cache of single sectors; the log says each 10 s how many
  reads the card had, how many the cache, and the time reading (on a Pi 5,
  87% from the cache)
- The Mac's "._" files (macOS keeps a file's extended attributes in one
  on FAT32, and adds one to files it writes) are left out of directory
  listings (readdir, wrapped): the Amiga ran DEVS:Monitors/._PAL from its
  Startup-Sequence. tools/copy_amiga_folder.py removes them from the card
  after copying
- On the Pi the Amiga's every socket() failed (errno 47, Amelinium's "no
  socket"): bsdsocket.library passes the Amiga's numbers on as they are,
  BSD's (AF_INET 2, SOCK_STREAM 1, SOCK_DGRAM 2), and Circle's C library
  has others (1, 4, 1); the same numbers on a Mac, so the simulator
  showed nothing. bmnet takes BSD's from the Amiga's sockets (and its
  MSG_PEEK) and gives BSD's AF_INET back in their addresses and host
  entries
- Amiga Setup: Zorro III RAM (16 to 256 MB, z3_ram) and an RTG card
  (Picasso96's uaegfx on Zorro III, 8 to 64 MB, rtg); either turns on the
  CPU's 32-bit addressing. Amelinium with JavaScript needs more than 8 MB
  (on a Pi 5 with 64 MB of Zorro III RAM it shows frogfind.de)
- Folder drives answered every ACTION_INFO and ACTION_DISK_INFO (Workbench's
  titles, Info, a program asking for free space) with an error on the Pi:
  circle-newlib's statvfs() is a stub that fails without setting errno
  (the log's "Unimplemented error Bad file number", errno left over).
  statvfs() is wrapped and gives FatFs's free space
- Amiga Setup: FPU (none, 68881, 68882), Chip RAM 8M, Zorro III RAM up to
  512M: what AmiKit has. A Workbench with Picasso96 failed (#80000003)
  opening its RTG screen without an FPU
- The RTG card offers the HDMI sizes too (1280x720 to 1920x1200, 24 modes
  in all): libretro's build listed only 1920x1080 and Amiberry's classic
  ones, and a Workbench set to 1280x720 on desktop Amiberry fell back to
  PAL. In the simulator wiisoldier's Workbench now opens at 1280x720, 24-bit
- docs: the JIT's settings; the reason it is in indirect mode

## 0.2.4 – 2026-11-04

Tried on a Pi 4 (4 GB) and a Pi 5 (8 GB): the bench floppies with and
without the JIT, and Workbench 3.1 with it.

- The JIT (Amiga Setup, JIT): Amiberry's ARM64 JIT on the Pi, in its
  indirect mode (memory through Amiberry's banks; direct mode is chosen
  when the emulator starts, before the setup's hard reset, and traps I/O
  with SIGSEGV, which bare metal hasn't), with a 16 MB
  cache, at full speed, not cycle-exact, and not "more compatible"
  (cpu_compatible, with which the ARM64 JIT compiles nothing).
  circle_jit_exec_patch.diff lets the heap hold code (Circle made memory
  after its own code never executable). In the PC simulator, on a Mac, it
  ran a register loop 9 times and a memory loop 5 times as fast as the
  fastest interpreter (23 and 14 times the A1200's default). On the Pi
  (1000s of loops a second, register / memory): Pi 5 183 / 79, Pi 4 92 /
  37, against the Pi 4's interpreter's 5 / 5 and the Pi 5's A1200 default's
  2.6 (register)
- mmap() on the Pi: after the Amiga's memory, which leaves 256 MB of the
  heap over, smaller mappings (the JIT's code) come from those 256 MB (a
  4 GB Pi 4 had none left, and its JIT stayed off); and a mapping given
  back is kept for the next that fits, as Circle never takes back a block
  over 512K (each hard reset with the JIT lost 16 MB)
- tools/bench/make_bench_adf.py: bench-cpu.adf and bench-mem.adf, boot
  floppies that loop in fast RAM and count at $F0; with bench=1 the log
  says how many 1000s of loops a second, every 10 s ("peek" in the
  simulator)

## 0.2.3 – 2026-10-30

- Disk images past 2 GB: Circle's C library cut a file position to 32
  bits (`lseek()`'s `int`), so an image's data past 2 GB couldn't be
  reached, though FAT32's files go to 4 GB. 64 bits now
  (newlib_seek64_patch.diff)
- File names in UTF-8 on the SD card (circle_fatfs_utf8_patch.diff):
  FatFs used code page 850, while Amiberry gives and expects UTF-8, so a
  name with an accented letter was garbled on the card, for a Mac or PC,
  and one made on a Mac didn't reach the Amiga as it was
- exFAT, and the card's second partition as SD2: (circle_fatfs_exfat_patch
  .diff: FatFs's exFAT on, and its SD2 volume the card's partition 2
  rather than a second SD controller): a card made with a FAT32 partition
  for BMAmiga and an exFAT one holds disk images over 4 GB. The menu's
  file browser shows SD2: at the card's top; Installation says how to
  partition a card on macOS, Windows and Linux
- The web UI has SD2: too: Files shows *SD:* and *SD2:* at the top to go
  between them (its requests' vol=SD2), and the dashboard both volumes'
  space. Its sizes are 64 bits (an exFAT partition's files can be over
  4 GB), as are sd_fs's (sd_info.size, sd_seek(), sd_size()); sd_fs takes
  a path with its own volume ("SD2:/x") as it is
- The web UI showed names with accents garbled once FatFs's names were
  UTF-8 (it still converted code page 850), and refused them in New folder
  and Rename; it passes UTF-8 through now
- A setting longer than 71 characters (a disk's path in folders) was
  refused, so bmamiga.cfg lost the drive: 255 now, and 64 settings, not 48
- tools/copy_amiga_folder.py: copies an Amiga folder (a folder drive, such
  as AmiKit's) to the card with the names Amiberry would have given it:
  the Amiga's characters FAT32 can't hold as %xx, a file's .uaem with it,
  accented letters as Amiberry needs them. All of AmiKit's 78,647 names
  come out safe for FAT32 and back to the Amiga as they were

## 0.2.2 – 2026-10-30

From the Pi 5 test of 0.2.1 (the CD32 with Battle Chess):

- A CD image's tracks from 17 on failed to open (`[FILE ERROR]`): Circle's
  C library had room for 20 open files, and a `.cue` keeps each track's
  file open. Now 32 files (the most its select() allows; about 28 tracks)
  and 64 open directories, which folder drives use too
  (newlib_open_files_patch.diff)
- Amiberry couldn't make its folders in saves (saves/NVRAM, where a CD32
  keeps its games' saves): Circle's C library said "not a directory" for a
  path whose folder didn't exist, and C++'s create_directories() gave up
  on that. It says "no such file" now, as POSIX has it
  (newlib_paths_patch.diff)
- A start-up screen while Amiberry starts (BMAmiga, its version and
  "Starting the Amiga..."), in place of a black screen for the seconds it
  takes to read the Kickstarts
- The log's thread-local storage line has its numbers (newlib's printf
  has no %zu)

## 0.2.1 – 2026-10-29

- A CD32 or CDTV started without its disc when the Amiga Setup had
  anything set (Battle Chess on the first Pi 5 test of 0.2.0): the setup's
  hard reset, as the Amiga starts, emptied the drive before Amiberry had
  seen the disc. The disc now goes back in after every hard reset
  (src/port/platform.c, cd_back_in()); a simulator test for it

## 0.2.0 – 2026-10-29

From the first test on a real Pi 5:

- The Pi stayed black, with no log: one of Amiberry's static objects reads
  the clock (std::chrono) while Circle starts, before its timer exists, and
  Circle's assert stopped every core before the serial port and the log
  were set up. gettimeofday() gives 0 until the kernel has its timer
  (wrapped, src/port/circle/posix.c). Found by running the Pi 4 kernel in
  QEMU's raspi4b, which stopped the same way
- No Kickstart was found, so the Amiga started AROS as an A500 and crashed
  (a black screen): Amiberry only scans a folder that realpath() resolves,
  and circle-newlib's realpath() always fails. BMAmiga has its own now
- Floppy Drives, Speed: a real drive's, 2x, 4x, 8x or turbo
  (`floppy_speed`)
- Amiga Setup in the menu: the Kickstart, CPU, CPU speed, chipset, chip,
  slow and fast RAM, sound filter and stereo separation over the model's,
  from a hard reset (src/port/bridge.cpp puts them in Amiberry's
  changed_prefs, as its own settings window does)
- bmamiga.uae on the card: any of Amiberry's .uae settings, one a line
- The Amiga's network: Amiberry's SLiRP (NAT through the Pi's Ethernet or
  Wi-Fi) on uaenet.device or an A2065 card (Amiga Setup, Network). SLiRP's
  sockets are bmnet's (src/port/bmnet.c, from BMNeXT) over the kernel's
  TCP and UDP (UDP and the DNS server added to src/host.h), never waiting,
  as on the Pi the C library's would; SLiRP's /etc/resolv.conf is the DNS
  server the Pi's DHCP gave. Simulator tests: bmnet's own, the A2065, and
  SLiRP's DHCP and DNS through bmnet ("netprobe")
- bsdsocket.library (Amiga Setup, Network; bsdsocket_emu): Amiberry's own
  osdep/bsdsocket_host.cpp, no longer replaced by a stub, on bmnet too,
  which gained pipes, MSG_PEEK, select() waiting its whole timeout, and
  names: gethostbyname() and gethostbyaddr() through the Pi's DNS server,
  and the common protocols and services
- The Amiga's memory: Amiberry's reservation (mmap()) comes from all of
  the Pi's memory (host_large_alloc(), Circle's HEAP_ANY), up to 2 GB,
  rather than 320 MB from the first 1 GB: room for AmiKit's 512 MB of
  Zorro III RAM and its RTG card
- bmamiga.uae's drives (filesystem2=, hardfile2=, uaehf...) go on the list
  after the menu's DH0: to DH3:, once, at every hard reset; the log names
  each drive

## 0.1.0 – 2026-10-29

The first release: not yet tried on a real Pi.

- BMAmiga starts, from BMCoco: Amiberry (its libretro core) in place of
  XRoar, running in the PC simulator. AROS without Kickstart ROMs, or
  Kickstarts from roms (Amiga Forever's too); floppies DF0: to DF3:, the model,
  warp, resets in the F12 menu; the keyboard, mouse and joystick; the frame
  lock, with Amiberry's clock following the frames shown
- Rebuilt on BMCoco 0.5.5 (it started from 0.5.1): its BMA800XL 1.4.2 merge
  (the display code in src/av, src/v3d and src/kms, GPIO menu navigation),
  the CPU clock and smooth sound fixes, Screen Size, and the rest

- Amiberry's core builds for the Pi 4 and 5 (step 2, the Circle side;
  the kernels are next): with circle-stdlib's newlib and the toolchain's
  libstdc++, Amiberry unmodified, and BMAmiga's src/port/circle for what
  they lack: cooperative POSIX threads on libco (tested in the simulator,
  which runs them too), std::mutex and std::condition_variable over them,
  thread-local storage, mmap() from the heap, iconv, the missing headers;
  networking, crash handlers and the JIT left out for now. zlib 1.3.1 is a
  submodule. make -C tools/picore builds it and checks the link

- The Pi 4 and Pi 5 kernels (step 3; not yet run on a Pi): kernel8-rpi4.img
  and kernel_2712.img, with Amiberry's core, BMAmiga's Circle layer and
  zlib in place of XRoar. Only the 64-bit Pi 4 and 5 are built. The kernel
  is about 63 MB with its BSS (Amiberry's 68000 interpreter is 24 MB of
  code): KERNEL_MAX_SIZE is 80 MB, and build_sdcard.sh stops if a kernel
  doesn't fit, since Circle halts without a word then. Thread-local storage
  is set up before Amiberry starts; a Circle patch puts its two sections
  together, as the linker needs; usleep() and sleep() for libstdc++. The
  web UI starts floppy images (in DF0:) and edits bmamiga.cfg. XRoar's
  regular expressions (posix_extra.c) are gone

- From BMCoco 0.5.6 to 0.5.8 (and BMA800XL 1.4.3 with it): the Pi 5's fan
  run by its temperature, the Pi 5 power button's code in src/pi45, web
  UI uploads about 5 times faster, network connections closed before a
  reboot or shut down, Space ticking a [ ] item in the menu, and
  BMA800XL's licence notices in the files that come from it (then
  "Modified for BMCoco" and, where BMAmiga changed them, "Modified for
  BMAmiga"). The CoCo's own changes are left out, and F11 keeps
  Amiberry's name, Warp

- Before the first test on a Pi, from what BMNeXT's first Pi 5 test found
  (its commits 5d529c4 and 288ddb9):
  - Memory aligned to more than 64 bytes no longer comes from Circle's
    memalign(), which asserts then and stops every core (BMNeXT's black
    screen): Amiberry's memory banks (aligned to 4K pages by
    posix_memalign()), mmap()'s 64K and thread-local storage are aligned
    inside bigger blocks, with free() wrapped to give those back
  - A crash on any core writes the log out before the Pi stops
  - The Pi 5 is asked for its full clock, as the Pi 4 is
  - Only the lines of the picture that changed are drawn and written out
  - The Pi 5's HDMI sound at the right pitch (its CTS for the firmware's
    mode)
  - FatFs fast seek for disk images (patches to Circle and circle-newlib)
  - Every 10 s the log has the Amiga's speed, the ARM's clock and
    temperature, and where the time went

- The documents and licences are BMAmiga's: the User Guide, Installation,
  ROMS.txt (Kickstarts, Amiga Forever's included), the Pi 4 and 5 notes,
  building, the simulator (macOS) and updating Amiberry rewritten for the
  Amiga, and the card's README. The card's licences have Amiberry's and
  the notices of the code it includes (amiberry.txt) and zlib's, in place
  of XRoar's; THIRD_PARTY.md lists them. The card has disks and saves
  folders, and only the Pi 4 and 5's firmware and config.txt sections.
  The web UI says Amiga and starts floppy images in DF0:

- A build needs no download of Amiberry's json.hpp: nlohmann/json 3.11.3,
  the version Amiberry pins, is kept in third_party/nlohmann-json, and
  amiberry.mk points Amiberry's build at it
- The web UI in Workbench 1.3's colours: its blue, white and orange, the
  boing ball's red, and the Amiga logo's rainbow, in place of the CoCo's
  green

- Hard drives and CD (the menu's *Hard Drives and CD*): DH0: to DH3:, each
  an HDF image (with or without a Rigid Disk Block) or a folder on the
  card, which the Amiga sees as a volume of its name and can write to;
  and a CD image (ISO, CUE, CCD, NRG, MDS, CHD) in the CD32's or CDTV's
  own drive, or on the other Amigas in uaescsi.device's unit 0, which
  Amiberry's file system mounts as CD0: with no CD file system needed on
  the Amiga. Kept in bmamiga.cfg (dh0 to dh3, cd0), and mounted by a hard
  reset as the Amiga starts. The web UI starts HDF images as DH0: and CD
  images in the CD drive
  - Amiberry's other threads (the hard drives', the file system's) run:
    a thread waiting on a mutex or condition is switched to when it is
    released, and every thread gets a turn each frame. HDF images hung at
    a white screen before
  - A floppy put in a drive from BMAmiga stays there after a hard reset
    (the reset put back the empty name disk_eject() had left)
  - Amiberry's threads that sleep (the CD32's, the CD's audio) no longer
    stop the whole Pi while they do: they wait while the emulation goes
    on. A CD32 with a disc ran at half speed
- The gamepad reaches the Amiga: the core reads its player's pad from its
  port 0, beside the mouse, not port 1, where BMAmiga gave it. On a CD32
  it is a CD32 pad, with the pad's own buttons, which CD32 games need

BMCoco's history, from which BMAmiga comes, follows.

## 0.5.5 – 2026-10-20

- Hard disk and DriveWire activity, shown at the bottom left as the floppy
  disk's is (*Show Disk Activity*): `HD0 LSN 1234` for the IDE interface
  and the NX32's and MOOH's SD card, `DW0 LSN 1234` for DriveWire (the
  built-in server or another computer's), with the light on while a
  sector is read or written, and errors in red (`error`; DriveWire's `no
  disk`, `write protected`, ...). Whichever was busy last is shown
- Hi-Res Mouse (USB) in the Joysticks page (`hjoy0`): the USB mouse
  through Tandy's Hi-Res Joystick Interface, 640 positions across for
  software that reads it. Timed for the HIRESMS reader (X 0-616, Y 0-182);
  `hires_mouse_x` and `hires_mouse_y` in `cmdline.txt` change the timing
  for other programs (OS-9's driver: `1133,5423` on both)
- Fixed: the mouse's right button is the CoCo 3's second button, as the
  User Guide says; the middle button (the wheel) was. XRoar numbers the
  buttons left, middle, right; BMCoco gave it the USB order (left, right,
  middle). Both the Mouse and the Hi-Res Mouse
- Fixed: the *Arrow keys* joystick (Joysticks page, `joy_right=kjoy0`) did
  nothing; the port it was given was left with no joystick at all. The
  arrow keys now move it and left Alt fires
- Fixed: hard disks chosen in the menu were lost at the next boot when no
  machine was set (`machine=`): BMCoco then starts the first CoCo it has
  ROMs for, which came with RS-DOS instead of the IDE interface. The
  interface (and its images) now goes back in
- Fixed: the built-in DriveWire server's virtual channels started with
  connection 0 as theirs rather than none, so a reset of the CoCo, or
  NitrOS-9 opening a channel, closed connection 0, whatever had it
  (another channel's call, or the RS-232 Pak's)

## 0.5.4 – 2026-10-19

- NitrOS-9 on the network with the built-in DriveWire server: its virtual
  channels (`/N`) reach the internet, as with DriveWire 4 or pyDriveWire.
  `tcp connect host port` (NitrOS-9's network programs) connects in the
  background and answers `OK connected` or `FAIL`; `ATDT` and the AT
  commands make a channel a Hayes modem, for terminal programs; `dw disk
  show` and `dw server show`. See "NitrOS-9 on the network" in the User
  Guide
- The RS-232 Pak, with a virtual modem: Cartridge, *RS-232 Pak (Modem)*
  (`rs232=1`) puts a 6551 at `$FF68` beside the cartridge plugged in, for
  CoCo terminal programs and NitrOS-9's `/T2`, and `ATDTbbs.example.com:23`
  calls a telnet BBS, in the background (`CONNECT`, or `NO CARRIER`), with
  `+++`, `ATH`, `ATO`, `ATZ` and the rest. XRoar has no RS-232 Pak; it is
  BMCoco's own (`src/port/rs232.c`). See "The RS-232 Pak and calling
  BBSes" in the User Guide
- Tests: the stand-in HDB-DOS ROM is no longer written through a link into
  BMCOCO_ROMS; dialling a stand-in BBS through the RS-232 Pak, +++ and ATH,
  and a call nobody answers
- Merged BMA800XL c33adfd: `build_sdcard.sh` runs the PC tests first
  (`--skip-tests` leaves them out)

## 0.5.3 – 2026-10-19

- Pi Zero, 2, 3 and 4: run the ARM at its full clock. Without an operating
  system asking for more the firmware left it at its lowest (600 MHz on a
  Pi 3 B+, not 1.4 GHz), too slow for busy CoCo 3 games such as Rick
  Dangerous, whose sound then broke up. The log shows the clock
- Pi 4 and 5: fix the sound breaking up. Their HDMI plays at 48 kHz, but
  XRoar made its sound at 44.1 kHz, so the buffer ran dry all the time and
  the sound was stretched in jumps. XRoar now makes it at the output's rate
- Video, Screen Size (100% down to 80%): makes the picture and the menu
  smaller, for a TV that cuts off the edges (overscan). The same as the
  `scale` option, now changed without editing `cmdline.txt`
- Disk activity: while a floppy disk is read or written, a red light and
  the drive, track, side and sector (`D0 T17 H0 S03`) at the bottom left
  of the picture, and in red for a few seconds an empty drive, a sector not
  found, a CRC error or a seek error, so that a program stuck loading (the
  wrong disk in the drive, a bad image) can be seen to be. Disk Drives,
  Show Disk Activity (`disk_activity=0`) turns it off
- Fix the Pi hanging when the Becker port is used with a DriveWire server
  on another computer (the built-in server was fine). Circle's C library
  holds the lock on its table of open files for as long as a socket call
  waits, so while the Becker port was connecting in the background, the
  emulator opening a file (the cartridge's ROM, at the reset that follows)
  waited for it for ever, and the network with it (no ping). The Becker
  port now uses Circle's own sockets
- Becker port with a DriveWire server on another computer: replies come
  back within the frame. Circle runs the network once a frame, so each
  request and reply took one or two frames (two of them per sector for
  HDB-DOS); while the CoCo waits for a reply the network now runs every
  half millisecond. A port that isn't a number from 1 to 65535 is reported
  in the log rather than tried. Both from a tester's patch
- From BMA800XL 1.4.1 and 1.4.2:
  - The menus can be used with GPIO joysticks: either one moves, fire
    chooses
  - `cmdline.txt` leaves `usbpowerdelay` to Circle (300 ms, 510 ms behind a
    hub) rather than 1000 ms, for a quicker start; add `usbpowerdelay=1000`
    for a keyboard that needs longer
  - `./build_sdcard.sh pi4 pi5` builds several boards at once
  - The SD card's `licenses` folder: the notices and licences of the code
    from other projects in the kernels (XRoar, Circle and newlib, BMC64's
    Apache 2.0 code, crt-pi, Mesa and Linux's V3D code, the GCC runtime);
    docs/THIRD_PARTY.md says where each is used
  - The Pi 4 and 5's display and GPU code is in `src/av/`, `src/v3d/` and
    `src/kms/`, as in BMA800XL
  - Troubleshooting: an 8BitDo M30 2.4G whose receiver is in Switch mode
- Simulator: BMCOCO_SIM_RATE=48000 plays the sound at the Pi 4 and 5's rate
- docs/SIMULATOR.md: testing and debugging BMCoco on a PC (with gdb, lldb
  or strace), XRoar's traces, and getting a log from the Pi

## 0.5.2 – 2026-10-16

- Fix the sound clicking or sounding wrong (PLAY on a CoCo 3, for one): a
  CoCo 3 (59.69 Hz) locked to a 60 Hz display, or a PAL CoCo (50.32 Hz) to
  50 Hz, runs up to 0.6% fast, more than BMCoco's corrections (one sample in
  512) could take up, so the sound buffer filled and what didn't fit was cut
  off. The sound is now squeezed or stretched smoothly, by up to 2%
- Simulator: BMCOCO_SIM_WAV saves the sound, BMCOCO_SIM_CPU shows how hard
  each screen is to emulate

## 0.5.1 – 2026-10-16

- The web UI shows the machine running ("Tandy CoCo 3 (NTSC)"), not always
  "Tandy Color Computer"; the file list has icons for CoCo files (disks,
  tapes, cartridges, programs) and offers "Plug in cartridge" for `.ccc`
  and `.rom` files
- Pi 4 and 5: a PAL CoCo or Dragon gets a 50 Hz display, and the frame
  lock, without `hdmi_mode=19` (on a Pi 5, which ignores `hdmi_mode`, it
  was shown at 60 Hz, unlocked). Rates just outside what HDMI allows (PAL's
  50.32 Hz, the CoCo 3's 59.70 Hz) now get the standard 50 or 60 Hz mode
  instead of keeping whatever mode the display had
- The web UI no longer carries BMC64's BASIC listing editor, which BMCoco
  never used
- Docs: the SD card's README lists every Pi BMCoco runs on and the newer
  menu pages; the User Guide says which machines a Pi 4 or 5 runs at their
  exact rate (NTSC CoCo 1 and 2)

## 0.5.0 – 2026-10-16

- A built-in DriveWire server for the Becker port, so NitrOS-9's DriveWire
  builds (such as NitrOS-9 EOU's `dw_becker` IDE disks) boot on the Pi with
  no other computer: DriveWire in the menu chooses its drives 0-3 (`dw0` to
  `dw3`) and the server (built in, or another computer's address, moved
  there from Network). No address, `127.0.0.1` or `localhost` mean the
  built-in one
- Fix plugging in the IDE interface (or choosing a hard disk image) taking
  minutes, or seeming to hang, and NitrOS-9 hanging at "Drivewire": the IDE
  interface has a Becker port, which connected to a DriveWire server at
  127.0.0.1, where nothing answers on the Pi, and Circle waits about 3
  minutes for that; connecting now happens in the background

## 0.4.0 – 2026-10-15

- From BMA800XL 1.2.0 to 1.4.0:
  - Raspberry Pi 5, Pi 500 and Pi 500+ (`kernel_2712.img`): BMA800XL's
    display driver, run at the CoCo's exact rate; Resolution in the Video
    menu (the Pi 5's firmware ignores `hdmi_mode`); HDMI sound; the power
    button shuts down (docs/PI5.md)
  - Raspberry Pi Zero and Zero W (`kernel.img`), on their one core
  - The Pi 4 and 5's 3D GPU draws the CRT effect and smooth scaling, at the
    full 1080 lines (`crt_gpu=0` for the ARM, as before)
  - CRT Curvature in the Video menu
  - Fix the Pi 2 and 3 starting at 1080p instead of 720p
  - Logging is off unless `enable_logging` is set (`file` to see the load
    reports); File logs to the serial port too, and the log file is
    complete after a crash, a hang or switching off

## 0.3.1 – 2026-10-15

- Missing CP-400 and MX-1600 ROMs are named by their files (cp400bas.rom,
  cp400extbas.rom, ...), not XRoar's list names: a CP-400 without
  cp400bas.rom showed only "cp400", then ran with no Color BASIC (a screen
  of garbage)

## 0.3.0 – 2026-10-15

- A Pi too slow for the frame lock (the most accurate artifact colours on a
  Pi 3, a CoCo 3 at double speed in a busy game) no longer drops to half
  speed: BMCoco paces by the clock until frames fit again, and stretches
  the sound rather than leaving gaps. The load is logged every 30 seconds
  and shown in About
- The two most accurate artifact renderers are marked as slow on a Pi 2/3
- Memory in the menu: a CoCo 3's 128K to 2M, a CoCo 1/2's 4K to 64K
- Hard disks in the menu: images for an IDE interface (HDB-DOS), plugged
  in when needed, kept for the next boot
- The Becker port (DriveWire over the network), with the server set in the
  Network page
- USB mouse, as the CoCo's mouse in a joystick port
- The Color Computer Archive's CP-400 Extended BASIC (cp400extbas.rom) is
  found as it is
- The joysticks no longer look up their settings on every read

## 0.2.0 – 2026-10-13

- From BMA800XL 1.1.0: Raspberry Pi 4 B and Pi 400 support
  - BMA800XL's own display driver, on either HDMI port, with the display
    run at an NTSC CoCo 1/2's exact refresh rate (59.92 Hz); the CoCo 3
    and PAL CoCos lock to 60 or 50 Hz, as on a Pi 3 (docs/PI4.md)
  - CRT shader and smooth scaling
  - Sound on either HDMI port or the headphone jack
  - Pi 400: Shut Down switches it off; Fn+F10 shuts down
- A keyboard's power key shuts down
- Fix the smooth setting being stuck on for the Pi 2 and 3 (from BMA800XL)
- The Pi 4 scaler and display driver tests run in CI

## 0.1.1 – 2026-10-12

- Web UI in Color Computer colours: the VDG's green text screen, its
  orange, and its eight colours as a colour bar (it had BMA800XL's Atari
  colours)

## 0.1.0 – 2026-10-12

- First version: BMCoco, forked from BMA800XL 1.0.1, with the atari800
  emulator replaced by XRoar 1.12.1
- Tandy Color Computer 1, 2 and 3 (and the Dragon and MC-10, from XRoar)
- Frames locked to the display refresh; timer pacing otherwise
- NTSC artifact colours, S-Video and RGB
- Disks (RS-DOS), tapes, cartridges and machine code programs
- USB keyboards (typed characters or the CoCo's layout), gamepads as analog
  joysticks, GPIO joysticks
- On-screen menu (F12) drawn in the CoCo 3's font
- Ethernet / Wi-Fi with BMC64's web UI
- Builds on Linux and macOS
