# Setting up the Pi Zero 2 W

> **Not verified on hardware.** These steps are written from the documented behaviour of Raspberry Pi OS,
> SDL2 and KMS/DRM; nobody has run them on the actual Pi + touch frame yet. Treat each step as something to
> confirm, and fix this page when reality differs.

## 1. OS

Flash **Raspberry Pi OS Lite, 64-bit** (Raspberry Pi Imager; set hostname, user, 2.4 GHz Wi-Fi, enable SSH).
64-bit gives the compiler NEON by default; 32-bit also builds. Then:

```sh
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y build-essential pkg-config git libsdl2-dev
sudo usermod -aG video,render,input,audio "$USER"     # DRM, input devices, ALSA; log out and in again
```
`libsdl2-dev` pulls in the KMS/DRM, GBM and GLES pieces SDL needs. No desktop, X11 or Wayland is installed or wanted.

## 2. Video (KMS/DRM)

`/boot/firmware/config.txt` should contain `dtoverlay=vc4-kms-v3d` (the default on current images). The old
`hdmi_group/hdmi_mode` options do not apply under KMS. The Zero 2 W has one mini-HDMI port; the Pi outputs up to
1080p60, so the frame's monitor should run 1920x1080. If the display picks a wrong mode, append to the single line in
`/boot/firmware/cmdline.txt` (check the connector name with `ls /sys/class/drm/`, usually `HDMI-A-1`):

```
video=HDMI-A-1:1920x1080@60
```
Check what the game sees: `build/games/<id>/<id> --info` (video driver must say `kmsdrm`).

## 3. Audio over HDMI

With KMS the HDMI audio shows up as an ALSA card (look at `aplay -l`, expect `vc4hdmi`). SDL2 uses ALSA's `default`
device. If HDMI is not card 0 (a USB sound card may be), set it in `/etc/asound.conf`:

```
defaults.pcm.card 1
defaults.ctl.card 1
```
Test with `speaker-test -c 2 -t sine -f 440 -l 1`. Volume is set on the monitor/TV; the SDK already limits level.

## 4. The touch frame

Plug the frame's USB cable into the Zero's **data** port (the one labelled USB, not PWR) with an OTG adapter or a
powered hub. Verify: `dmesg | tail`, `ls /dev/input/by-id`, `sudo apt install evtest` then `evtest` (touching should
print `ABS_MT_POSITION_X/Y` events), and `build/games/<id>/<id> --info` should list a touch device. If it says none
are found, check the cable and group membership (`input`).

If SDL itself reports no touch device, the runtime automatically tries reading `/dev/input/event*` directly
(multitouch protocol B from `hid-multitouch`, or a plain single-touch digitiser) and logs
`evdev fallback: reading touch device ...`; it switches off again if SDL later finds one. `--info` shows both views.
Set `PG_NO_EVDEV=1` to disable the fallback. Both need read access to `/dev/input` (group `input`).

Calibrate once (the frame sits on top of the monitor and is rarely perfectly aligned):

```sh
build/games/ball-pit/ball-pit --calibrate        # touch the four circles; saved to ~/.config/pi-games/touch.cal
```
Delete that file to go back to the raw mapping. Calibration is a 2x3 affine fit, so it also fixes mirrored/rotated axes.

## 5. Running

From an SSH session (no desktop must own the display):

```sh
git clone <this repo> ~/pi-games && cd ~/pi-games
tools/pg subscribe @stable        # or individual ids
tools/pg sync
tools/pg run ball-pit             # hold both top corners for 2 s to leave
```
Run with `--fps` once to see CPU ms per frame; the budget is ~12 ms of `update + 3D` per 16.7 ms frame.
Keep the Zero cool (heatsink, airflow): `vcgencmd measure_temp`, `vcgencmd get_throttled`.

Starting a launcher at boot (a systemd service with `SupplementaryGroups=video render input audio` and
`Restart=on-failure`) is outside what this repository verified; get it working over SSH first.
