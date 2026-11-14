# AmiKit on BMAmiga

[AmiKit](https://www.amikit.amiga.sk) (AmiKit X, which comes with its own
Amiberry for macOS, Windows and Linux) runs on BMAmiga since 0.6.0: its
1920x1080 Workbench, Directory Opus, the dock, and its programs on the
network (its weather, for one). Tried on a Pi 5 and a Pi 4 (slower, but it
runs).

## Copying it to the card

AmiKit is two folders, `AmiKit` and `RabbitHole` (on a Mac, in
`AmiKit.app/Contents/Resources`): 2.9 GB and nearly 69,000 files. Copy them
to the card's `disks` folder with `tools/copy_amiga_folder.py`, which
keeps the `.uaem` files (the Amiga's protection bits and comments) and
renames the few files FAT32 can't hold the way Amiberry expects:

```
python3 tools/copy_amiga_folder.py /Applications/AmiKit.app/Contents/Resources/AmiKit /Volumes/BMCARD/disks/AmiKit
python3 tools/copy_amiga_folder.py /Applications/AmiKit.app/Contents/Resources/RabbitHole /Volumes/BMCARD/disks/RabbitHole
```

AmiKit needs a Kickstart 3.1 (A1200) in the card's `roms` folder.

## Settings

All of these are in the menu (F1); nothing is needed in `bmamiga.uae`.

| Where | Setting |
|-------|---------|
| Model (reboot) | Amiga 1200, 2 MB |
| Amiga Setup | Kickstart 3.1 (A1200); CPU 68020, FPU 68882, JIT on |
| Amiga Setup | Chip RAM 8M, Zorro III RAM 512 MB, RTG Card 64 MB |
| Amiga Setup | Network: bsdsocket.library (reboot) |
| Hard Drives and CD | DH0: `disks/AmiKit`, DH1: `disks/RabbitHole` |
| Network | Wi-Fi or Ethernet, and *Wait for Network at Start* |

In `bmamiga.cfg` that is:

```
model=A1200OG
kickstart=/roms/amiga-os-310-a1200.rom
cpu=68020
fpu=68882
jit=on
chip_ram=8192
z3_ram=512
rtg=64
amiga_network=bsdsocket
network=wifi
wait_for_network=1
dh0=/disks/AmiKit
dh1=/disks/RabbitHole
```

AmiKit's scripts use the volume names (`AmiKit:`, `RabbitHole:`), which
are the folders' names, so DH0: and DH1: do.

AmiKit's tools (its `env-handler` for `ENV:`, Directory Opus) need
Amiberry's native code interface, which BMAmiga turns on; leave
`native_code` out of `bmamiga.uae`.

Without *Wait for Network at Start*, AmiKit starts before the Pi's Wi-Fi
has connected, and its weather and menu bar say NO NET until their next
check.

## The screen

AmiKit's Workbench is a 1920x1080 RTG (Picasso96) screen. On a 1920x1080
display it is shown pixel for pixel. A Pi 4 runs its display at 1280x720,
and the picture is shrunk to fit; *Smooth Scaling* in the Video menu makes
the text easier to read, or set AmiKit's screen mode (Prefs/ScreenMode) to
1280x720.

## What doesn't work

- `ping`: bsdsocket.library on BMAmiga has no ICMP.
- AmiKit's update check (AmiKit Setup, Update) can say "Network error
  while checking": it gives up after 6 s, and its HTTPS connection takes
  about 5 s on BMAmiga (the Amiga's network runs at the emulator's frame
  rate).
- The RabbitHole icons that start Mac, Windows or Linux programs, and
  sharing the clipboard with the host: there is no host.
