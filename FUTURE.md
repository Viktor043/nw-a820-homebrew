# Future projects and ideas

Ideas researched or discussed but not built yet, with what we know so far. Companion to [LEARNINGS.md](LEARNINGS.md)
(what was built and learned). Status as of 2026-11-13.

## 1. USB audio: the Walkman as a USB sound card for a computer (A82x first)

Goal: the Mac (or any computer) plays audio over USB into the Walkman's headphone output, like the "USB DAC" mode of
modern Walkmans.

**What the feasibility check found**
- **Sony publishes the kernel source.** The NW-A826/A828/A829 share a package with the NWZ-S715 series:
  [oss.sony.net → NWZ-S715](https://oss.sony.net/Products/Linux/Audio/NWZ-S715.html), file
  `linux-kernel-2.6.10.local.tgz` (~49 MB). It contains Sony's USB device controller driver
  (`drivers/usb/gadget/s1r72v17_usbc.c` + the shared `s1r72xxx_usbc.c`), Sony's storage/MTP gadgets (`fc_nwm.c`,
  `nwm_msc.c`, `nwm_mtp.c`) and the stock gadgets, including **gadgetfs** (`inode.c`: a USB device written as a program).
- **Standard interface.** The controller driver (license GPL) exports `usb_gadget_register_driver`; Sony's gadgets sit
  on the standard Linux gadget API.
- **Modules can be loaded.** No symbol versioning (`__kcrctab`/`__versions` absent): only the vermagic string must
  match, `2.6.10_dev-mp201 preempt ARMv5 gcc-3.4`.
- **The chip supports isochronous transfers** (the Epson
  [S1R72V17 technical manual](https://www.epson.jp/prod/semicon/products/interface/pdf/s1r72v17_tm.pdf) has isochronous
  IN/OUT sections), **but Sony's driver has no isochronous code at all** (bulk / interrupt / ep0 only, ep0..ep5).
- **No source for the A91x** (A916/A918/A919) on Sony's portal; the A919 would have to borrow the A820 source.
- **Low risk to the Sony side:** Sony's UI loads its own driver from the read-only rootfs; ours would only be loaded by
  the player.

**Two ways**
| | A. True USB sound card (UAC1) | B. Bulk audio stream + Mac helper app |
|---|---|---|
| Result | The Walkman appears in the Mac's Sound settings, no extra software | A small Mac app captures system audio (via [BlackHole](https://github.com/ExistentialAudio/BlackHole)) and streams PCM over USB |
| Kernel work | Add isochronous support to Sony's controller driver (Epson manual), plus a USB Audio Class gadget (none in 2.6.10) | Only build `gadgetfs.ko`; the device side is a userspace program (Zig) on bulk endpoints |
| Effort / risk | ~1–2 weeks, on-device debugging, depends on Epson's register details | A few days, low risk, a little latency (fine for music) |

**Recommendation:** B first (proves the module build, the USB link and playback), A later on the same foundation.

**Open decision:** building 2.6.10 modules needs an old ARM GCC (3.4 / 4.x, OABI). Zig and clang can't build this
kernel and there is no prebuilt one for an Intel Mac, so realistically a small Linux container (Docker) used only to
compile modules.

**Already works instead:** the A829/A828's Bluetooth receiver mode (Settings > Bluetooth): a phone or Mac streams to the
Walkman's headphone output.

**Not possible:** the Walkman *playing into* an external USB DAC (needs USB host mode, host and USB-audio drivers, and
5 V out; the hardware is device-only).

## 2. USB-C instead of the WM-PORT (A829 and A919)

Replace the 22-pin WM-PORT with a USB-C 2.0 receptacle. Only VBUS, D+, D−, GND are needed; line out, docks and remotes
are lost; charging, the drive, the installers and flashing keep working.

- **Part:** a USB-C 2.0 receptacle breakout **with 5.1 kΩ on CC1 and CC2** (without them C-to-C chargers give no power).
- **Map the pins first, don't guess:** strip the USB-A end of the original Sony cable and beep VBUS (red), D− (white),
  D+ (green), GND (black) to the WM-PORT pins with a multimeter. A wrong VBUS can damage the board.
- Open, photograph both sides, look for VBUS/D+/D− test points near the connector (easier than the connector pads).
- Check the fit (USB-C ≈ 9 × 3.2 mm; the WM-PORT opening is wider): a 3D-printed bezel fills the gap (Bambu P1S).
- Remove the WM-PORT with hot air (Kapton on neighbours) or tap its pads; D+/D− short, equal length, twisted
  (480 Mb/s); VBUS/GND slightly thicker.
- Test before closing: no VBUS–GND short; charges on C-to-C and A-to-C; mounts on the Mac; `install_mac*.py --check`.
- Reversible alternative: a WM-PORT-to-USB-C adapter or pigtail.

## 3. LoRa mesh inside an A919 (the "great body and antenna" mod)

The A919's TV and FM hardware is receive-only (`dtv.ko` only DMAs a decoded transport stream; the "Sanyo FM Tuner"
driver sets a frequency and reads signal strength; ISDB-T isn't broadcast in Europe anyway), so a mesh needs a radio.

- **Radio:** XIAO ESP32S3 + Wio-SX1262 running [Meshtastic](https://meshtastic.org) (EU_868, licence-free, duty cycle
  enforced), talking to the Walkman over a UART (Meshtastic's serial module, text mode first).
- **Walkman:** a Mesh app in the SNRGY style (message list, the existing on-screen keyboard, node/signal info in the
  strip).
- **UART:** the A82x drives its Bluetooth chip over `/dev/tts/0` through `icx_uart_sw` (with a BT_DET line); the A919
  has no Bluetooth, so its board *may* have that UART on an empty footprint. Unconfirmed until opened. Plan: a probe
  tool that sends a known pattern out of each serial device at boot, a logic analyzer or USB-serial adapter to find
  the pads, measure the idle TX level (3.3 V direct; 1.8 V needs a level shifter).
- **Antenna:** the 1seg antenna is tuned for 470–770 MHz, near 868 MHz. Find what it is (earphone cord, whip, internal
  flex) and how it feeds the tuner (coax, pogo pin, trace). **Disconnect it from the tuner** (the SX1262's +22 dBm can
  destroy its input), add a u.FL pigtail, measure with a NanoVNA and match (2–3 parts) or trim.
- **Power and space:** XIAO + Wio-SX1262 stack ≈ 21 × 17.5 × 8 mm (thinner: a bare E22-900M22S, 14 × 20 × 2.5 mm);
  30–100 mA in RX, so a power switch or a MOSFET the Walkman controls.
- **Order:** bench (Meshtastic on the kit, phone test, the Mesh app against the kit over USB on the Mac) → teardown
  photos → UART probing → RF → integration → range test.
- **Shopping list:** fine iron, flux, 0.1 mm enamelled wire, u.FL pigtail, slide switch, USB logic analyzer or 3.3 V
  USB-serial adapter, NanoVNA; maybe a level shifter and a bare SX1262 module.

## 4. Bluetooth: AirPods on the A829

Pairing failed. Likely causes, in order: AirPods not in pairing mode (case open, hold the back button until white);
the A829's 2007 Bluetooth 2.0 chip pairs with a legacy PIN and AirPods may refuse it; the AirPods hopping back to an
iPhone. Next: retry with the phone's Bluetooth off, then read the `bt:` lines in `HB/out/player_log.txt` (found the
device? pairing refused? audio failed to start?). Older/simpler Bluetooth headphones are the reliable fallback.

## 5. Player and UI ideas

- **PDF:** too heavy to render on the Walkman; a Mac-side converter (text/reflow into the reader, page images for scans).
- **Mac sync tool:** copy music/books/podcasts, strip `._` files, pre-size covers to 236 px, convert hi-res FLAC.
- **SNRGY EQ screen** (dot-matrix bars), AutoEQ headphone presets, a battery-cost hint.
- **SNRGY games screen** with cover art; **Books in the Sony theme's grid**.
- **Marquee and refresh:** the strip already scrolls; measure the every-second refresh's battery cost.
- **Open-licence fonts** (instead of DIN Alternate / Georgia) for anything shared beyond prototypes.

## 6. Verification and calibration still to do

- A919: the DSP direct mode (look for `direct` in `HB/out/player_log.txt` after playing an MP3), the texture remap on
  the real screen, the battery curve (log a full discharge; the player logs every 5 min).
- A828: charge it; it fell back to Sony's UI after a start at 1–5 % battery.
- Upstream: PR #3 (Mac installer + A91x) is open; the SNRGY work lives on the `snrgy` branch.
