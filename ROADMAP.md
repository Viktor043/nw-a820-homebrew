# Fork roadmap

Six workstreams on top of upstream's NW-A820 homebrew. Facts below come from `docs/firmware-research.es.md` and
`research/dsp/FINDINGS.md` unless marked as open questions. Keep fork-only work on branches so fixes can go upstream
as small pull requests (the host-test change and the app runtime are self-contained on purpose).

Status: **done** (in this fork), **next**, **research** (nothing to build until a question is answered).

## 1. Apps (CrossPoint / Snail OS ideas, as Lua apps) — done v1, next v2

Done:
- `hbapps`: Lua 5.4 runtime that hbplayer starts from Juegos like a game (music keeps playing, BACK 2 s quits).
  Sony-style screens and lists, sandboxed files, per-app data, 6 MB heap cap. See `docs/apps.md`.
- Sample apps: Hello (template), Stopwatch, Reader (plain-text books with saved place, the CrossPoint core).
- PC simulator and tests; a coding-agent skill (`.claude/skills/walkman-app/`), the Snail OS "describe an app, get a
  Lua file" idea.
- Host tests now run natively on Linux / macOS (`HB_PC`, `HB_TARGET`).

Next:
- **First run on the A829**: copy `build/HB/bin/hbapps` + `hbplayer` and `build/Apps`, check keys, font widths,
  page-turn speed at 124 MHz, the error screen and BACK 2 s / 6 s.
- **Network for apps** (weather, calendar, RSS, Meshtastic later): the phone link belongs to hbplayer
  (`pod_net_up` / `pod_fetch` over Bluetooth tethering, lwIP + Mbed TLS). Proposed: `hb.fetch(url)` writes a request
  to a pipe that hbplayer's game loop already services while the app runs; hbplayer fetches into the app's data folder
  and answers. Needs a non-modal variant of `pod_net_up` (the current one draws its own screen).
- EPUB in the Reader (unzip with the zlib already in the tree, strip XHTML to text; CrossPoint caches the layout to
  storage to save RAM, same idea here), dictionary lookup, reading stats.
- Event-style apps (`function on_key(k)`), a timer callback, and showing the now-playing line in the bottom bar.

## 2. Meshtastic + Bluetooth on the A919 — research, then build

Step 0, the day a unit arrives: run upstream's installer with the A919 connected and press **Save report** (read-only).
Open questions it answers: same NEC MP201 platform? same Linux 2.6.10 and update format? does the hook apply?

Known from the A829 that shapes the A919 plan:
- The A829's Bluetooth chip (CSR BlueCore) talks BCSP over UART `/dev/tts/0` at 2,592,000 baud, and that UART is
  shared with the serial console (`/dev/icx_uart_sw` switches it). The A919 has no Bluetooth, so that UART is the
  natural port for a co-processor: an nRF52840 + SX1262 Meshtastic node speaking Meshtastic's serial client API
  (protobuf frames behind the 0x94 0xC3 header, which resyncs after stray console output).
- Classic A2DP to headphones needs a second chip (ESP32 class) fed with I2S from the codec path; the A829 does A2DP
  in the kernel (`/dev/sound/adsp`, SBC inside `hci_bchs.ko`), the A919 has nothing to reuse.
- Open: where the UART and I2S are on the A919 board, and what bus the 1seg tuner uses (if SDIO, it is the best
  candidate for item 4).

## 3. USB-C on both — next (hardware)

- USB is device-only, from an Epson S1R72V17 controller: no USB host on the SoC side, so USB-C is for charging and
  the drive, not for accessories.
- Wire VBUS, D+, D-, GND from the WM-port pads to a mid-mount USB-C receptacle; 5.1 kOhm from CC1 and CC2 to ground
  so C-to-C chargers give 5 V. Sony's charging circuit and the homebrew's USB drive mode are unchanged.
- To do: WM-port pinout for this series (measure on a donor), a printed insert for the wider WM-port opening, and
  checking that nothing in the boot path watches the WM-port's accessory lines.

## 4. microSD on both — research

- Hard facts: no USB host (so no card reader), and the stock kernel's feature list has no MMC/SD support. The kernel
  is 2.6.10, older than Linux's generic SPI layer, so there is no ready mmc-over-SPI driver to build either.
- Options, cheapest first:
  1. A919: the co-processor on the UART reads the card and serves files to the player. 2,592,000 baud is about
     250 KB/s, enough for MP3/AAC and most 16-bit FLAC; not for USB mass storage.
  2. User-space SD over bit-banged SPI through `/dev/mem` GPIOs plus FatFs in the player. Needs four free GPIOs
     (unknown without a schematic; the removed tuner may free some on the A919).
  3. A kernel module for the SoC's own SD controller, if the MP201 has one and it is pinned out. Check Sony's GPL
     source for the A808 (the reference upstream used) for MMC drivers or platform devices.
- The card would only be visible to the homebrew player, not to Sony's UI or SonicStage.

## 5. Jog dial (Sony Ericsson P900 style) — next (hardware)

- Buttons reach Linux as `/dev/input/event0` (codes 0-4, 16-20 in `homebrew/common/keys.h`), but the notes also show a
  raw ADC (`/dev/mp200_ad`, channel 3, "<= 29 = Play"): some buttons are a resistor ladder, not separate lines.
  Measure which before wiring anything.
- Plan: a tiny MCU reads the encoder and "presses" UP / DOWN / PLAY through open-drain outputs (or switched resistors
  on a ladder), so the dial works in Sony's UI too, with no firmware change. A dedicated key code for the player
  (`"jog+"`/`"jog-"` in hbapps) is a later firmware step.
- Mechanical: side-wall depth on both bodies; a donor P900 dial or a modern side-mount micro encoder.

## 6. Display — research, last

- The LCD is 240x320 16 bpp on the SoC's LCD controller (frame buffer at 0x33700000, refreshed ~30 times a second
  from SDRAM). Any other panel needs a bridge from that LCD bus.
- 60 Hz e-paper does not exist at this size. If the goal is sunlight-readable and low power, the candidate is a
  reflective memory-in-pixel LCD (Sharp memory LCD class, 400x240 mono, tens of fps), driven over SPI by a bridge MCU
  or small FPGA that reads the LCD bus and dithers to 1 bit. Only the homebrew would be designed for it; Sony's UI
  would be legible at best.

## Conventions

- Upstream's style: comments say why, Spanish strings in the code with English in `i18n.c` (the tests fail on a
  missing translation), every device binary links only against libc, libm, libdl, libpthread (`NEEDED OK`).
- `python homebrew/tests/run_tests.py` before every push (needs `ZIG` = zig 0.15.2 and Pillow).
