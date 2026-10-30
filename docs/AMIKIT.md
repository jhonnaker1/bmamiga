# AmiKit on BMAmiga

The goal: boot [AmiKit](https://www.amikit.amiga.sk) (AmiKit X, for macOS,
which comes with its own Amiberry) on BMAmiga. This page is the plan, and
how far it has got.

## What AmiKit's Amiberry runs

From its `Contents/Resources/Configurations/default.uae` (AmiKit X13):

| What | AmiKit's setting | On BMAmiga |
|------|------------------|------------|
| Kickstart | 3.1 (A1200), `RabbitHole/RequiredFiles/kick.rom` with `rom.key` | Amiga Setup, Kickstart (or model A1200) |
| CPU | 68020 with a 68882, as fast as possible, not cycle-exact, 32-bit addressing, JIT (`cachesize=16384`) | Amiga Setup: CPU 68020, CPU Speed Fastest; `bmamiga.uae`: `fpu_model=68882`, `cpu_24bit_addressing=false`. No JIT (below) |
| Chipset | AGA | model A1200 |
| Memory | 8 MB chip (`chipmem_size=16`), 512 MB Zorro III (`z3mem_size=512` at `0x40000000`) | `bmamiga.uae`; room for it since 0.2.0 (up to 2 GB in all) |
| Picture | Picasso96: `uaegfx`, Zorro III, 64 MB (`gfxcard_type=ZorroIII`, `gfxcard_size=64`), Workbench at 1920x1080 | `bmamiga.uae`; untested (below) |
| Drives | folders: `AK0:AmiKit` (boots, priority 0), `AK1:RabbitHole` (-128); `HOST:` and `APP:` (the Mac's) | `bmamiga.uae` `filesystem2=` lines (since 0.2.0), or DH0:/DH1: in the menu; `HOST:`/`APP:` aren't needed (nothing in `S:` uses them) |
| Network | `bsdsocket_emu=true` (no TCP/IP stack on the Amiga) | Amiga Setup, Network, bsdsocket.library (since 0.2.0) |
| Sound | 48 kHz, sinc, `sound_filter=emulated`, `sound_filter_type=enhanced` | as it is |
| Mac only | `clipboard_sharing`, `magic_mouse`, `native_code`, the RabbitHole icons that start Mac apps | off: there is no host to share with |

AmiKit's scripts use the volume names (`AmiKit:`, `RabbitHole:`), not the
device names, so DH0:/DH1: folders named `AmiKit` and `RabbitHole` would
do too.

## Done (0.2.0)

- The memory: Amiberry's reservation from all of the Pi's memory, up to
  2 GB (`src/port/circle/posix.c`, `host_large_alloc()` in
  `src/kernel.cpp`).
- `bmamiga.uae` drive lines, after the menu's DH0: to DH3:, at every hard
  reset (`src/port/bridge.cpp`); the log names each drive.
- bsdsocket.library: Amiberry's `osdep/bsdsocket_host.cpp` on bmnet
  (`src/port/bmnet.c`), with DNS through the Pi's DNS server. Tested by
  `tools/hostsim/bmnet_test.c` (pipes, peeking, waiting, names) and the
  simulator builds; not yet with an Amiga program on it.

## Next

1. **Boot AmiKit in the PC simulator**, from the AmiKit installed on the
   Mac (`/Applications/AmiKit.app/Contents/Resources/AmiKit` and
   `RabbitHole`), with a `bmamiga.uae` made from the table above. This
   shows whether Picasso96's screen, the folders and the memory work,
   without an SD card. Things to look at: the RTG picture (switching to
   and from the Amiga's own screen; converting 1920x1080 each frame, which
   wants BMNeXT's "only the lines that changed"), and bsdsocket.library
   with AmiKit's own programs.
2. **Measure the speed on a Pi 5**: a 68020 without the JIT. AmiKit is
   made for a fast JIT; the menu's speed line and the log's every 10 s
   say how close it gets.
3. **Copying AmiKit to the card**: 2.9 GB, 68,666 files (the `.uaem` files
   keep the Amiga's protection bits and comments), with
   `tools/copy_amiga_folder.py` (0.2.3), which gives the five names FAT32
   can't hold (`Libs/xad/R.A.W.`, `Demos/Relic/Relic ` and three more,
   ending in a dot or a space) Amiberry's `%xx` names, and the 186 with
   accents macOS keeps decomposed the form Amiberry needs: 228 names in
   all. A folder that size on the Pi wants a speed test.
4. **The JIT**, only if 2. says so: Amiberry's ARM64 JIT reserves 4 GB of
   address space for the Amiga's memory (`LIBRETRO_NO_JIT` leaves it out
   now). On a Pi 5 with 8 GB that could be possible; a project of its own.
5. An `amikit.uae` example on the card, and a part of the User Guide on
   copying AmiKit over.
