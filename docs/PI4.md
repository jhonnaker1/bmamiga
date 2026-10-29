# Raspberry Pi 4 B and Pi 400

BMAmiga runs on the Pi 4 B and Pi 400 as a 64-bit kernel,
`kernel8-rpi4.img`, with Circle's `armstub8-rpi4.bin`.

Pi 4 support comes from [BMA800XL](https://github.com/aminch/bma800xl),
through BMCoco: the display, sound and GPU code (`src/av/`, `src/kms/`,
`src/v3d/`). BMA800XL's picture is 8-bit with a palette; BMCoco's and
BMAmiga's is RGB565 (Amiberry gives 16-bit pixels), so their copy of the
display code takes RGB565 instead. The 68000 is emulated without a JIT
(see [the Circle layer](../src/port/circle/README.md)), so an Amiga 500 is
the lightest model for the Pi and an Amiga 4000/040 the heaviest.

## Picture

BMA800XL has its own driver for the Pi 4's display hardware
(`src/kms/pi4/`). It takes over the HDMI port in use (HDMI0 if both are),
shows the Amiga's picture through the HVS, the Pi's hardware scaler, and can
run the display at exactly the Amiga's frame rate. It is chosen with
`video_driver` in `cmdline.txt`:

| `video_driver` | What it does |
|---|---|
| `kms` (default) | As `hvs`, and also sets the video mode so the display runs at the Amiga's rate: about 50 Hz for a PAL Amiga, 60 Hz for an NTSC one, switching when the model does. This works for 720p and 1080p when the display lists the 50 Hz (or 60 Hz) mode, and when the Amiga's rate is within the 0.5 % HDMI allows; otherwise the display gets the standard mode and the frames are shown as they come. |
| `hvs` | Keeps the mode set in `config.txt`. Use it for a TV that goes blank for a while when the refresh rate changes. |
| `firmware` | The firmware's framebuffer, with the picture scaled by the ARM. Use it if the others give trouble. |

If something isn't as expected (an unknown mode, a step that doesn't
complete), the driver falls back to the firmware's mode or framebuffer.
The log (serial, or `bmamiga.log`) shows what happened, in lines like these
(the numbers depend on the display and the Amiga):

```
kms: HDMI0, HVS channel 1, 1280x720
kms: EDID (HDMI0): 2 block(s), HDMI, 9 video modes
kms: Video mode VIC 19, 1280x720 at 74250000 Hz pixel clock (50.000 Hz)
BMAmiga: display refresh rate 50.000 Hz
```

The driver was written by examining the Linux `vc4` driver and the BCM2711
device tree, documenting the hardware's behaviour, and writing BMA800XL's
own code from that.

A bare metal kernel can't use the Pi 4's OpenGL ES, so BMAmiga drives the
Pi 4's 3D GPU itself (`src/v3d/`, from BMA800XL 1.2.0) to draw the
smooth and CRT pictures (`smooth`, `crt_shader`, `crt_curvature`):
crt-pi, with BMC64's settings, at up to 1080 lines and
scaled up to fill larger screens. If the GPU doesn't start or can't draw a
picture, ARM cores 1 to 3 draw it instead (`src/av/scaler.cpp`; up to 1080
lines, 720 with curvature and smooth both on); `crt_gpu=0` always uses the
ARM. The log's `v3d:` lines and *Drawn by* line say which. `crt_gpu_test=1`
also compares the GPU's pictures with the ARM's at start up.

The CRT shader looks best at 1080p: `hdmi_mode=31` in `config.txt` (1080p
at 50 Hz, for a PAL Amiga; `hdmi_mode=16` for 60 Hz). Sharp pixels are
shown at whole multiples of the Amiga's size, so some resolutions leave
black bars around the picture.

## Sound

`audio_out` chooses where the sound goes:

- `auto` (the default): HDMI, on the port the display is plugged into
  (HDMI0 if both are); the headphone jack if the display takes no sound.
- `hdmi`.
- `headphones`: the Pi 4 B's 3.5 mm jack. The Pi 400 has none.

## Shut Down

*Shut Down* (menu, web UI, or a keyboard's power key, which is Fn+F10 on
the Pi 400) halts the Pi the way Linux powers it off. The Pi 400 switches
itself off; Fn+F10 turns it back on. The Pi 4 B halts; unplug it, or
connect GPIO3 to ground, to start it again.
