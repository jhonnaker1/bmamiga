# BMAmiga Patches

## Circle

The following patches are applied by `make_all.sh`, in this order, while
preparing the Circle dependency. They carry changes that are not part of the
checked-out upstream revision. `make_all.sh --skip-patches` (and
`build_sdcard.sh --skip-patches`) leave Circle as it is.

Patches are applied to the submodule revisions pinned by circle-stdlib v21
(Circle 51.1.1, tag `Step51.1.1`), the same revisions BMC64 uses.

| Patch | Description |
| --- | --- |
| `circle_bolt_keyboard_patch.diff` | Adds Logitech Bolt keyboard receiver support (from BMC64). |
| `circle_linux_bug_patch.diff` | Turns off `BUG_ON`. Circle's Linux emulation, which VCHIQ uses for the display and sound, asserts with `BUG_ON` that it only runs on core 0; BMAmiga calls it from core 1, where the emulator runs (as BMC64 does with VICE). Without it the Pi halts with `mutex.cpp(9): assertion failed`. From BMC64's `circle_patch.diff`. |
| `circle_fatfs_fastseek_patch.diff` | Turns on FatFs's fast seek (`FF_USE_FASTSEEK` in `addon/fatfs/ffconf.h`), for `newlib_fastseek_patch.diff`. It changes FatFs's `FIL`, so circle-newlib must be built with it too (`make_all.sh` builds both). From BMNeXT. |
| `circle_fatfs_utf8_patch.diff` | File names in UTF-8 on FatFs's API (`FF_LFN_UNICODE` 2 in `addon/fatfs/ffconf.h`, not code page 850): Amiberry gives and expects UTF-8 (the Amiga's Latin-1 converted), so a name with ä, é or ß was stored as two code page characters, garbled for a Mac or PC, and a name made on a Mac came back to Amiberry as no UTF-8 at all. |
| `circle_sysconfig_patch.diff` | Enables multi-core support and the USB FIQ in `sysconfig.h`. From BMC64's `circle_patch.diff`. Also saves the floating point registers on interrupts on the Pi Zero, as Circle already does on the Pi 2 and later with GCC 12 or newer; on its one core, interrupts would otherwise clobber the emulator's (BMC64's Pi Zero build does the same). |
| `circle_tcp_window_patch.diff` | Raises Circle's TCP receive window from 10 to 44 segments (64,240 bytes, the largest that fits without window scaling), so a sender can deliver more between turns of the network task. This speeds up web UI uploads. From BMC64. |
| `circle_tls_patch.diff` | Puts the thread-local `.tbss` section right after `.tdata` in Circle's linker script (`circle.ld`), before `.bss`: the linker refuses TLS sections with another between them. Circle has no thread-local variables; Amiberry has, and `src/port/circle/tls.c` gives them their block. |

## circle-newlib

Applied by `make_all.sh` after Circle's, to circle-newlib (Circle's C
library), reset first in the same way.

| Patch | Description |
| --- | --- |
| `newlib_fastseek_patch.diff` | Fast seek for files of 1 MB or more opened as they are (disk images): their cluster map is made when they are opened, so a seek back no longer follows the cluster chain from the file's start, a read from the SD card for every 128 clusters (hundreds a seek in a big image). A file in fast seek mode can't grow, so the map is let go before a write or seek past its end, and before truncating. From BMNeXT. |
| `newlib_open_files_patch.diff` | Raises circle-newlib's open files from 20 to 32 (the most its `select()` allows: `fd_set` is one 32-bit `int`, `FD_SETSIZE` 32) and open directories from 20 to 64. A CD image with a `.cue` keeps every track's file open (Battle Chess has 20: its tracks from 17 on failed to open on the Pi; about 28 now fit), and a folder drive has a file open for each one the Amiga has, and a directory for each it is reading. |
| `newlib_paths_patch.diff` | A path whose folder doesn't exist (FatFs's `FR_NO_PATH`) is `ENOENT`, as POSIX has it, in `open()`, `stat()`, `unlink()` and `truncate()`, not `ENOTDIR`. With `ENOTDIR`, C++'s `std::filesystem::create_directories()` gave up instead of making the folder, so Amiberry never made `saves/NVRAM` (the CD32's saved games) and its other folders. |
| `newlib_seek64_patch.diff` | 64-bit file positions in `lseek()` (`CGlueIO::LSeek()` and `_lseek()` took and gave an `int`, so a disk image's data past 2 GB couldn't be reached though FAT32's files go to 4 GB). A position past what FatFs can hold is `EOVERFLOW`, not wrapped round. |

Amiberry is not patched: what it needs on Circle is in `src/port/circle`.
