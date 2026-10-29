# Amiberry on Circle

Amiberry's libretro core expects a desktop system: threads, memory
mapping, sockets, processes, `std::filesystem`. On the Pi, BMAmiga runs it
on Circle with circle-stdlib's newlib and the Arm toolchain's libstdc++,
which have the C library and `std::filesystem` (over FatFs) but no threads.
This folder is what Amiberry needs beyond those, so that it builds
unmodified. `circle.mk` builds it all; `make -C tools/picore` builds it on
its own and checks that nothing is missing.

## Threads (`threads.c`)

POSIX threads, cooperative, on Amiberry's libco: a thread runs until it
waits (a mutex someone else holds, a condition, a join, a sleep), then the
next that can go on runs, all on the emulator's core. Under libretro
Amiberry starts one thread, "denise", the drawing thread, which only waits:
libretro builds draw in the emulation (`multithread_enabled` is 0).
Mutexes and conditions are found by their address, so the same code runs
in the PC simulator, which uses it too. `std::mutex` and
`std::condition_variable`, which libstdc++ has only with threads, are
defined over it in `include/bma_circle.h`.

`std::thread` can't be given a way to start (libstdc++ defines the class
without one), so the two files that start one are replaced:
`osdsync.cpp` (CHD's work queues, run at once) and `zz9000_media.cpp` (the
ZZ9000 card's FFmpeg player, which fails as Amiberry's does without
FFmpeg).

## Thread-local storage (`tls.c`)

Amiberry has `thread_local` variables. Circle places their sections but
sets no thread pointer; `bma_tls_init()`, called on the emulator's core
before Amiberry starts, gives it one block.

## Memory and the rest (`posix.c`)

- `mmap()` of anonymous memory is Circle's heap (no virtual memory).
  Amiberry reserves 4 GB for the Amiga's memory on 64-bit hosts (for its
  JIT), and tries smaller sizes until one is given: reservations over
  320 MB fail. The JIT is left out (`LIBRETRO_NO_JIT`).
- `iconv()` between UTF-8 and ISO-8859-1 (the Amiga's file names),
  `timegm()`, `pread()`, `pwrite()`, `access()`, and `usleep()` and
  `sleep()` for libstdc++'s `std::this_thread::sleep_for()`.
- What there is none of fails: processes, shared libraries, shared
  memory, terminals, `ioctl()`; `sigaction()` does nothing.

## Left out

- The Amiga's networking (`bsdsocket.library`): `nosockets.cpp` in place
  of `bsdsocket_host.cpp`; Amiberry then doesn't offer the library.
- Crash handlers (`nosignals.cpp` for `sigsegv_handler.cpp`): no signals.
- MAME's special file names, sockets and terminals (`chd_nofiles.cpp`).

## Headers (`include/`)

Missing ones (`sys/mman.h`, `dlfcn.h`, `sys/termios.h`, `pty.h`,
`sys/uio.h`, `sys/un.h`, `sys/ucontext.h`, `sys/ioctl.h`, `sys/mount.h`),
and newlib's with what they lack (`pthread.h` declares its functions,
`sys/signal.h` POSIX's `struct sigaction`, `sys/dirent.h` without FatFs's
Windows style types, `arpa/inet.h` and `netinet/in.h`). `bma_circle.h`,
included before each of Amiberry's files: large file names, endian
functions, `CLOCK_MONOTONIC`, `tm_gmtoff`, and the C++ classes above.
