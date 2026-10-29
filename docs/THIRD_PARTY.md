# Third-Party Software

BMAmiga incorporates software and other materials from third-party projects. Third-party components remain subject to their respective copyright and license terms.

The original copyright, license, and attribution notices supplied with these components are retained with their source where applicable. The notices and licences for code BMAmiga adapts from other projects are in [`sdcard/licenses/`](../sdcard/licenses/), which becomes the `licenses` folder of the SD card.

## Amiberry

BMAmiga incorporates [Amiberry](https://github.com/BlitterStudio/amiberry), the Amiga emulator by Dimitris Panokostas and contributors, version 8.4.1, under the GNU General Public License version 3. Amiberry is built on [WinUAE](https://www.winuae.net) by Toni Wilen and UAE by Bernd Schmidt and others, whose parts are under the GNU General Public License version 2 or later. BMAmiga builds Amiberry's libretro core, unmodified, into its kernels; what it needs on Circle is in [`src/port/circle/`](../src/port/circle/).

See `third_party/amiberry/` (a submodule of `https://github.com/BlitterStudio/amiberry`) for the source and its accompanying copyright and license information.

Amiberry includes code from other projects, which is in the kernels too: 7-Zip's LZMA SDK (public domain), xDMS (public domain), MAME's CHD and TMS34010 code (BSD 3-clause), Slirp (BSD), QEMU's VGA emulation (MIT), SoftFloat (the SoftFloat-2a licence, BSD and GPL 2 or later), Altirra's 8048 core (GPL 2 or later), kjmp2 and unzip (zlib-style licences), FloppyBridge (MPL 2.0 or GPL 2 or later; used under the GPL), stb_image (MIT or public domain), JSON for Modern C++ (MIT; Amiberry's build fetches it), libretro-common (MIT) and libco (ISC). Their notices are in [`sdcard/licenses/amiberry.txt`](../sdcard/licenses/amiberry.txt), with Amiberry's own; the GNU General Public License version 3 is [`LICENSE`](../LICENSE), version 2 [`sdcard/licenses/GPL-2.0.txt`](../sdcard/licenses/GPL-2.0.txt).

## zlib

BMAmiga includes [zlib](https://zlib.net) 1.3.1 by Jean-loup Gailly and Mark Adler (a submodule, `third_party/zlib`), which Amiberry uses for compressed images, under the zlib license. See [`sdcard/licenses/zlib.txt`](../sdcard/licenses/zlib.txt).

## BMCoco

BMAmiga is based on [BMCoco](https://github.com/jhonnaker1/bmcoco), the bare metal Tandy Color Computer emulator for the Raspberry Pi, under the GNU General Public License version 3: its kernel, display, sound, network, menu and web UI code, merged up to BMCoco 0.5.8. Some fixes come from [BMNeXT](https://github.com/jhonnaker1/bmnext), built on the same code.

## BMA800XL

BMCoco, and so BMAmiga, is a fork of [BMA800XL](https://github.com/aminch/bma800xl), the bare metal Atari 800XL emulator for the Raspberry Pi, under the GNU General Public License version 3. Its kernel, display, sound, network and web UI code is used here, including its Raspberry Pi 4 and Pi 5 display, sound and GPU drivers, merged up to BMA800XL 1.4.3 and its later upload speed-up.

## circle-stdlib

BMAmiga uses the [circle-stdlib project](https://github.com/smuehlst/circle-stdlib), v21, which includes [Circle](https://github.com/rsta2/circle) by Rene Stange and the newlib C library.

See [`third_party/circle-stdlib/`](https://github.com/smuehlst/circle-stdlib/tree/v21) for the version used by BMAmiga and its accompanying copyright and license information. newlib's notices are in [`sdcard/licenses/newlib.txt`](../sdcard/licenses/newlib.txt) and [`sdcard/licenses/libgloss.txt`](../sdcard/licenses/libgloss.txt).

The Raspberry Pi 4 and Pi 5 HDMI sound ([`src/av/hdmisound.cpp`](../src/av/hdmisound.cpp)) is adapted from Circle's HDMI sound driver, under Circle's GNU General Public License version 3 or later.

## GCC Runtime

BMAmiga links runtime code from the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 15.2.rel1 (GCC), under the GNU General Public License version 3 with the GCC Runtime Library Exception 3.1. The toolchain's GCC licence section is in [`sdcard/licenses/GCC-Runtime.txt`](../sdcard/licenses/GCC-Runtime.txt).

## BMC64

BMAmiga incorporates source code from the [BMC64 project](https://github.com/randyrossi/bmc64) by Randy Rossi and contributors.

See [BMC64](https://github.com/randyrossi/bmc64) for its source and its accompanying copyright and license information.

BMC64-derived files retain their original license and attribution notices: the Apache License 2.0 (see [the included license](../sdcard/licenses/Apache-2.0.txt)) or BMC64's GNU General Public License version 3. Modifications are identified in their headers.

## Raspberry Pi Firmware

BMAmiga includes the Raspberry Pi 4's boot firmware (`start4.elf` and `fixup4.dat`; the Pi 5's is in its EEPROM) and the Raspberry Pi 4 and Pi 5 device trees from the [Raspberry Pi firmware repository](https://github.com/raspberrypi/firmware).

Their licences are next to them, in [`sdcard/firmware/`](../sdcard/firmware/) and on the SD card: `LICENCE.broadcom` for the firmware and `COPYING.linux` for the device trees. See the [Raspberry Pi firmware repository](https://github.com/raspberrypi/firmware/tree/master/boot) for more.

The Raspberry Pi 4 and 5 firmware, device trees and overlay are the ones Circle
51.1.1 uses (see its `boot/Makefile`). The device trees and overlay are
unmodified builds of the device tree sources in the
[Raspberry Pi kernel](https://github.com/raspberrypi/linux) (Linux 6.12).

## crt-pi

BMAmiga uses the crt-pi CRT shader by davej, as included in BMC64, under the GNU General Public License version 2 or later: on the ARM, and compiled for the Raspberry Pi 4 and Pi 5's 3D GPU from [`tools/v3dshader/crt.frag`](../tools/v3dshader/crt.frag). See [`sdcard/licenses/crt-pi.txt`](../sdcard/licenses/crt-pi.txt) and [`sdcard/licenses/GPL-2.0.txt`](../sdcard/licenses/GPL-2.0.txt).

## Mesa

BMAmiga uses the [Mesa 3D graphics library](https://mesa3d.org/)'s Broadcom V3D drivers in three ways. The adapted files, with their copyright notices, and Mesa's MIT licence are in [`sdcard/licenses/Mesa-MIT.txt`](../sdcard/licenses/Mesa-MIT.txt).

- **Adapted into the Raspberry Pi 4 and Pi 5 kernels** ([`src/v3d/`](../src/v3d/)): control list packet layouts and packing rules, the rendering job, texture and tile layouts, and the GPU's device facts. The V3D 4.2 (Pi 4) material is from Mesa 26.2.4, the V3D 7.1 (Pi 5) material from Mesa 25.2.8.
- **Adapted into the shader compiler tool** ([`tools/v3dshader/`](../tools/v3dshader/)): the shader preparation that Mesa's drivers do before compiling. The tool is built on a PC against Mesa 25.2.8 and is not part of the SD card.
- **As the shader compiler:** the compiled shaders in `src/v3d/` (`shader_*.h`, `shader71_*.h`) are output of Mesa 25.2.8's V3D compiler. Compiling with Mesa doesn't make them Mesa code: the crt-pi shaders are under crt-pi's licence (above), the others are BMA800XL's.

## Linux

For the 3D GPU of the Raspberry Pi 4 and Pi 5, BMAmiga adapts parts of the Linux kernel's V3D driver and BCM2835 power domain driver ([`src/v3d/`](../src/v3d/)), from the [Raspberry Pi kernel](https://github.com/raspberrypi/linux/tree/rpi-6.18.y) (`rpi-6.18.y`), under the GNU General Public License version 2 or later. See [`sdcard/licenses/Linux-V3D.txt`](../sdcard/licenses/Linux-V3D.txt) and [`sdcard/licenses/GPL-2.0.txt`](../sdcard/licenses/GPL-2.0.txt).

The Raspberry Pi 4 and Pi 5 display implementation ([`src/kms/pi4/`](../src/kms/pi4/), [`src/kms/pi5/`](../src/kms/pi5/) and the HDMI sound in [`src/av/`](../src/av/)) was independently written using hardware information documented in the Linux vc4 driver (`rpi-6.18.y`). No code from that driver was copied or incorporated.

## Circle ARM stub

For the Raspberry Pi 4, BMAmiga includes `armstub8-rpi4.bin`, built from [Circle's `boot/armstub`](https://github.com/rsta2/circle/tree/master/boot/armstub) source, under Circle's [GNU General Public License version 3](https://github.com/rsta2/circle/blob/master/COPYING).

## Wi-Fi Firmware

BMAmiga includes the Wi-Fi firmware for the Raspberry Pi's wireless chip from the [RPi-Distro firmware-nonfree repository](https://github.com/RPi-Distro/firmware-nonfree), downloaded when BMAmiga is built.

See the `firmware/` folder on the SD card for the Broadcom, Cypress and Synaptics licenses that accompany it.

The Pi 5's files (`brcmfmac43455-sdio.raspberrypi,5-model-b.*`) are copies of the `brcmfmac43455-sdio.*` files under the name Circle looks for on the Pi 5, under the same Cypress license.

## Other Third-Party Components

Additional third-party components and supporting materials may be present in the BMAmiga source tree. Where such material is accompanied by its own copyright, license, or attribution notices, those notices and terms continue to apply.
