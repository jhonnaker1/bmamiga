BMAmiga - bare metal Amiga emulator for the Raspberry Pi
========================================================

Copy everything here to the root of a FAT32 SD card and boot a Raspberry
Pi 5, 500, 4 or 400 from it, with a USB keyboard and mouse connected.

First copy your Kickstart ROMs into the roms folder (see roms/ROMS.txt):
Amiga Forever's amiga-os-*.rom files with their rom.key, or kick13.rom,
kick31.rom and so on. They are copyrighted, so they are not included.
Without them the Amiga starts with AROS, the free Kickstart replacement.

It starts as an Amiga 500. Press F12 for the menu: floppy drives DF0: to
DF3:, the model (Amiga 500, 500+, 600, 1200, 4000, CD32, CDTV), warp,
resets, video, sound, network.

  F12               Menu                F11      Warp (as fast as it can)
  Windows keys      Left, right Amiga   Page Down  Help
  (or Insert, Home)

The mouse is the Amiga's mouse (port 1). A gamepad or GPIO joystick is
the joystick (port 2): stick or D-pad to move, A to fire, Home (or
Select+Start) for the menu.

Put your floppy images (.adf, .adz, .dms, .ipf, .zip) in the disks
folder (or anywhere). Amiberry keeps what it saves in the saves folder.

Video locks to the display at the Amiga's own rate: 50 Hz for a PAL
Amiga.

Networking (for the web UI): choose Ethernet or Wi-Fi under Network in
the menu (or add network=ethernet or network=wifi to cmdline.txt), then
open the address shown on screen in a browser to manage the files on
this card and start floppy images.

Licences: BMAmiga is under the GPL version 3 (LICENSE.txt); the licenses
folder has the notices for other projects' code it includes, and the
firmware folder the Raspberry Pi and Wi-Fi firmware's licences.

Options are in cmdline.txt; see docs/USER_GUIDE.md in BMAmiga's source for
all of them.
