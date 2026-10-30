# Installation

## What you need

- A Raspberry Pi 4 Model B or Pi 400, or a Raspberry Pi 5, Pi 500 or
  Pi 500+ (BMAmiga needs a 64-bit Pi 4 or 5)
- A microSD card (any size; BMAmiga itself needs about 70 MB)
- An HDMI display
- Your own Kickstart ROMs, for the real thing (see below); without them
  the Amiga starts with AROS
- A USB keyboard and mouse, and if you like a USB gamepad

## Prepare the SD card

1. Download `bmamiga-v<version>.zip` from the [releases page](https://github.com/jhonnaker1/bmamiga/releases), or build it
   yourself (see [BUILDING.md](BUILDING.md)) and use `build/sdcard/`.
2. Format the SD card as FAT32.
3. Extract the zip to the root of the card, so that the kernels
   (`kernel_2712.img`, `kernel8-rpi4.img`) and `config.txt` are at the top
   level, not in a folder (with them in a folder the Pi's firmware shows
   its "not found" screen).
4. Copy your Kickstart ROMs into the `roms` folder (see below).
5. Copy your floppy images to the card. The `disks` folder is there for
   them, but any folder works.
6. Put the card in the Pi, connect the display, keyboard and mouse, and
   switch on.

There is no image to write and nothing to install on the card beyond
copying the files.

### A second partition for big disk images (optional)

A file on FAT32 can be at most 4 GB, so a hard disk image can't be bigger.
For bigger ones, the card can have a second partition formatted as exFAT,
which BMAmiga mounts as **SD2:**: the menu's file browser shows it at the
top of the card, as *SD2: (second partition)*. The first partition must
stay FAT32, with BMAmiga's files on it: the Pi starts from it. Use an MBR
partition table (not GPT).

Making the partitions erases the card. Put 4 GB or more in the first, for
BMAmiga, the ROMs, floppies and the rest, and the remainder in the second.

**macOS** (find the card's disk with `diskutil list`; here `disk4`):

```
diskutil partitionDisk disk4 MBR FAT32 BMAMIGA 4G ExFAT AMIGA R
```

**Windows**, in an administrator's command prompt (`list disk` shows the
card's number; here 2):

```
diskpart
select disk 2
clean
create partition primary size=4096
format fs=fat32 quick label=BMAMIGA
assign
create partition primary
format fs=exfat quick label=AMIGA
assign
exit
```

**Linux** (the card here is `/dev/sdX`):

```
sudo parted /dev/sdX --script mklabel msdos mkpart primary fat32 1MiB 4GiB mkpart primary 4GiB 100%
sudo mkfs.vfat -F 32 -n BMAMIGA /dev/sdX1
sudo mkfs.exfat -n AMIGA /dev/sdX2
```

Then copy BMAmiga's files to the first partition as above, and big disk
images to the second. BMAmiga's log says `Second partition mounted as SD2:
(exFAT)` when it finds it. The web UI shows only the first partition.

## Kickstart ROMs

The Amiga's Kickstart ROMs are copyrighted, so BMAmiga can't include them.
Copy your own into the `roms` folder. Amiberry, the emulator inside
BMAmiga, recognises them by their contents, so Amiga Forever's files work
as they are (`amiga-os-130.rom` and the others, with their `rom.key`), and
so do files named for their Kickstart, such as `kick13.rom` and
`kick31.rom`. [ROMS.txt](ROMS.txt) has the list for each model.

Kickstarts can be bought with [Amiga Forever](https://www.amigaforever.com),
or copied from your own Amiga.

## First boot

The Pi starts an Amiga 500. With a Kickstart 1.3 it shows the Kickstart's
"insert disk" screen; without one, AROS. Press **F12** (or **Home**, or
**Select+Start** on a gamepad) for the menu, and *Floppy Drives* to put a
disk in DF0:. *Model* chooses another Amiga.

USB keyboards, mice and gamepads can be plugged in and out while it runs.

If you get no picture, see [Troubleshooting](USER_GUIDE.md#troubleshooting)
in the User Guide.

## Networking (optional)

To manage the card's files from a browser, turn on Ethernet or Wi-Fi under
*Network* in the menu, then reboot; the web UI's address is shown at the
bottom of the screen. For Wi-Fi, *Scan for Wi-Fi Networks* lists the
networks in range and asks for the password. See
[Networking and the web UI](USER_GUIDE.md#networking-and-the-web-ui).

## Upgrading

Copy the new release's files over the old ones, except for files you have
changed: `config.txt` and `cmdline.txt` if you edited them. Your settings
(`bmamiga.cfg`, `wpa_supplicant.conf`), Kickstarts and floppy images are
not in the release zip and stay as they are.

With networking on, a new kernel (`kernel_2712.img` for a Pi 5 or 500,
`kernel8-rpi4.img` for a Pi 4 or 400) can also be uploaded in the web
UI's file manager and started with **Reboot**. Updates that change other
files (the Pi firmware in the root or in `firmware/`) need the card in a
computer.
