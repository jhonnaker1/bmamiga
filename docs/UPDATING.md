# Updating Amiberry and Circle

## Amiberry

Amiberry is a git submodule in `third_party/amiberry`, from
`https://github.com/BlitterStudio/amiberry`, with libco as its own
submodule. It is used unmodified: BMAmiga builds its libretro core with
Amiberry's own `libretro/Makefile`, and everything specific to BMAmiga is
in `src/port/`:

| File | What it holds |
| --- | --- |
| `src/port/amiberry.mk` | Builds the core's objects into a static library (for the PC simulator and, through `circle.mk`, the Pi), leaving out what `BMA_EXCLUDE` names and adding `BMA_EXTRA` |
| `src/port/platform.c` | The libretro frontend: Amiberry's core options (`amiberry_*`), video, sound, input |
| `src/port/bridge.cpp` | BMAmiga's direct calls into Amiberry's own functions (disks, resets, warp) |
| `src/port/keys.c` | USB keys to libretro's keys |
| `src/port/circle/circle.mk` | The Pi build: compiler flags, and the Amiberry files replaced on Circle (`CIRCLE_AMIBERRY_EXCLUDE`) by BMAmiga's (`CIRCLE_AMIBERRY_EXTRA`) |
| `src/port/circle/` | Threads, POSIX functions, thread-local storage and missing headers for Circle (see its [README](../src/port/circle/README.md)) |

To move to a newer Amiberry:

1. Check out the new release or commit in the submodule:

   ```
   cd third_party/amiberry
   git fetch
   git checkout <tag or commit>
   git submodule update --init --recursive
   cd ../..
   ```

2. Build and test on the Mac:

   ```
   tools/hostsim/run_tests.sh
   ```

   Look for:
   - Files added to or removed from the core's `SOURCES_*` lists in
     `libretro/Makefile` (`amiberry.mk` reads them).
   - Changed core option names or values in `libretro/libretro.cpp`
     (`platform.c` answers `amiberry_model`, `amiberry_video_standard` and
     others by name) and Kickstart names (in `docs/ROMS.txt`).
   - Changes to the functions `bridge.cpp` calls.
   - A new version of `json.hpp` (`NLOHMANN_JSON_VERSION` in
     `libretro/Makefile`): replace `third_party/nlohmann-json/nlohmann/json.hpp`
     with that release's, and its version in that folder's README,
     `THIRD_PARTY.md` and the card's licences.

3. Check that the core still builds and links for the Pi, needing nothing
   Circle lacks:

   ```
   make -C tools/picore
   ```

   (circle-stdlib must be built for the Pi 4 or 5 first.) Anything listed
   as missing needs a stand-in in `src/port/circle/` (`posix.c`, or a
   header in `include/`), or, for a file that needs threads, sockets or
   signals Circle hasn't, a replacement named in `circle.mk`. libstdc++
   pulls in more than the check sees (it found `usleep()` and `sleep()`
   only at the kernel's link), so build the kernels too:

   ```
   ./build_sdcard.sh pi4 pi5
   ```

   It stops if a kernel has grown past `KERNEL_MAX_SIZE` (`make_all.sh`).

4. Check `sdcard/licenses/amiberry.txt` and `docs/THIRD_PARTY.md` against
   the code the core now includes (the folders under `build/pi4/amiberry`).

5. Commit the submodule and any port changes together:

   ```
   git add third_party/amiberry src/port
   git commit -m "Update Amiberry to <version>"
   ```

zlib (`third_party/zlib`) is a plain submodule, built by `circle.mk` for
the Pi; the simulator uses the Mac's.

## circle-stdlib (Circle and newlib)

circle-stdlib is a submodule in `third_party/circle-stdlib`, pinned to the
same revision BMC64 uses. `make_all.sh` applies the patches in
`src/patches/` before building, to Circle (`circle_*.diff`) and to
circle-newlib (`newlib_*.diff`); [`src/patches/README.md`](../src/patches/README.md)
says what each does. Two matter most to BMAmiga:

- `circle_tls_patch.diff` puts `.tbss` next to `.tdata` in Circle's linker
  script, which the linker needs for Amiberry's thread-local variables
- `circle_fatfs_fastseek_patch.diff` and `newlib_fastseek_patch.diff` give
  disk images fast seeks

To update, move the submodule to a new circle-stdlib release, run
`git submodule update --init --recursive`, then `./build_sdcard.sh`. If a
patch no longer applies, update it from the matching BMC64 patch or by
hand. Check too that Circle's `memalign()` still can't align past a cache
line, and its `sysconf()` page size: `src/port/circle/posix.c` works round
both for Amiberry.
