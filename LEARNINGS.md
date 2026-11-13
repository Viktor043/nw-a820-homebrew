# Learnings: Mac install, NW-A91x port, SNRGY UI, reader

What we found and built on top of the NW-A820 homebrew, from a Mac, across four Walkmans (NW-A829, NW-A828 and
two NW-A919s). Written for whoever picks this up next. All code is on the `snrgy` branch; the Mac installer and the
A91x support were also offered upstream as PR #3 (`mac-installer` branch).

## 1. Hardware and firmware

- **Two families, one platform.** The NW-A82x (2007) and the NW-A91x (2008, the 1seg TV models: A916/A918/A919) both
  run Linux 2.6.10 on the NEC MP200/MP201 (ARM926EJ-S), with the same libc and the same `/etc/init.d/gemini` start
  script. The A91x has **no Bluetooth**, a different kernel/DSP build and a different texture pack.
- **USB IDs.** A829/A828: `054c:035d`. A919: `054c:032a`. Both use Sony's `dbmn` SCSI update protocol
  (`0xFC ... 'dbmn'`: DEVINFO and upgrade).
- **Update file names differ.** A82x: `NW_WM_FW.UPG`. A91x: `NW_A91X.UPG` (that is what the A91x updater writes and
  what the device looks for).
- **UPG format.** md5(16) + DES-ECB header (8-byte signature, file count) + 4 files: a script, the kernel, a loader
  part, and the rootfs as a gzip'd tar. Repacking an unpacked file gives the identical bytes.
- **A91x key.** Not in Rockbox's database. Key `b8442b44`, signature `5dfeff3c`, found with a known-plaintext search:
  the header's second block is `nfiles, 0`, and DES ignores the low bit of each key byte, so the 16 hex characters
  fall into 9 classes: 9^8 ≈ 43 M candidates, under 2 minutes on 8 cores (CommonCrypto). The naive 16^8 search
  would have taken ~1.5 h.
- **The hook firmware** changes exactly two files: `/etc/init.d/gemini` (runs `/contents/HB/boot.sh` if present)
  and `/etc/firmversion` (sub-revision 41). A82x: 1.02-41; A91x: built from Sony's 1.03, reports **1.03-41**.
  Kernel, bootloader and Sony's UI are untouched; the official firmware flashes back at any time.
- **Brightness has 5 real steps.** `gfx_backlight()` accepts 0..100, but the LCD driver maps it to 5 levels. 5 % steps
  did nothing; 20 % steps are the truth.
- **Volume headroom.** The codec volume is 0..100 with 76 = 0 dB (0.65 dB per unit). Player volume 30 = 76, so
  there is ~15 dB above it (digital gain, can clip on loud masters). Used for "Volumen máximo" +3/+6/+9 dB.
- **Battery.** The player's percentage curve is the A82x's; on the A919 it reads oddly. A unit at 1–5 % can shut
  off mid-start, which the boot script then treats as a failed start (see §6).

## 2. Installing from a Mac

- **No `expand.exe`:** macOS `tar` (libarchive) reads Sony's CAB. Note: the A91x CAB names its entries with a
  leading `/`, so extract everything, not one named member.
- **No SCSI pass-through:** the same CDBs go over USB Bulk-Only Transport with libusb (`pyusb` + `libusb-package`).
  macOS only lets root detach a disk from its driver, so the installers run with `sudo`; the volume is unmounted
  around each command and remounted after.
- **Same MD5s as Windows:** Python's gzip writes OS byte 255 on macOS and 10 on Windows; `install_mac.py` patches
  that byte so the hook matches the tested MD5.
- Scripts: `firmware/install_mac.py` (A82x) and `firmware/install_mac_a910.py` (A91x: `--check`, `--firmware`
  for Sony's official 1.03, default = hook + diagnostic boot.sh, `--full --hb build/HB` = the homebrew,
  `--uninstall`).
- **The installer leaves the UPG on the drive** if its wait is interrupted (`NW_A91X.UPG`, 13 MB). Harmless; delete it.
- macOS creates `._*` files on the FAT drive: delete them (`dot_clean -m`, `find -name '._*' -delete`) or the
  player may list them.
- Updating the homebrew never needs a reflash: copy `build/HB/bin/*` and the boot scripts to `WALKMAN/HB`.

## 3. Building on a Mac

- **Zig 0.15.2** (`zig-x86_64-macos-0.15.2`). Downloaded binaries carry `com.apple.quarantine` and are unsigned, so
  macOS kills them (exit 137). Verify the SHA-256 against `ziglang.org/download/index.json`, then
  `xattr -dr com.apple.quarantine` on that folder only.
- The build scripts call `python`; this Mac only has `python3`. Put the project venv first on `PATH`:
  `PATH=~/.walkman-hb/venv/bin:$PATH ZIG=.../zig sh build_all.sh`.
- `homebrew/tests/run_tests.py` builds the host tests natively on macOS (`HB_PC`, the `host-tests-linux-macos`
  commit) and renders every screen; `shots.c` takes the theme as a 5th argument. Last run: 5/5, 65 screens per
  language. python.org's Python has no CA store: use `curl` for HTTPS, not `urllib`.

## 4. The A91x port

- **Textures.** The A91x `dui_tex.dpk` has the same pictures in another order (28 groups, some nested a level deeper).
  555 of 603 A82x ids were mapped by pixel match into `common/tex_a91x.h` (positions only); gfx detects the A91x by
  its group count. Missing on the A91x: Bluetooth icons, the big home art, group 28.
- **DSP.** The A91x has only the direct OMX config (`omx_nw_walkman_audio.cfg`), no `_ftb` one. `dspdec` used to die
  at start ("helper did not start") and everything decoded on the ARM. It now starts idle and only brings up the
  direct core for direct sessions. **Not verified on the device yet**: look for `direct` in `HB/out/player_log.txt`.
- **Diagnostics first.** Stage 1 of `install_mac_a910.py` installs a boot.sh that only writes `HB/out/diag.txt`
  (kernel, fb, input, devices) and boots Sony's UI: a safe way to check an untested model before running the player.

## 5. The SNRGY looks (Settings > Display > Theme: Sony / SNRGY silver / SNRGY black)

- **Art direction** from the SNRGY e-reader project: paper `#E9E6DF`, ink `#1E1E1E` (or reversed), squared type,
  inverted rounded selection, rules, chevrons, capsule scrollbar, and the E-PLAYER's dot-matrix status display.
- **The rotation trick.** Every screen still draws Sony's layout (title on top, status bar at the bottom). With a SNRGY
  theme, the copy to the LCD rotates the frame by `gfx_rot` rows, so the status band, drawn as the dot-matrix strip,
  appears on top. No screen needed relayout; partial flips, slides and screenshots (`gfx_composed`) honour it.
- **Theme colours are variables** (`ui_pal`), so all old code follows the theme.
- **The strip** is 42 px: three rows of hand-drawn 5×5 dot glyphs (2 dots apart, 7-dot advance) — status (play state,
  counter, BT/Hold, battery, 24 h clock) / song or book (scrolls when long) / elapsed–line–remaining. Refreshed once a
  second from `idle_tick` (or every 5 s in "calm" mode), the marquee ticked from the key wait loop.
- **Always-on display:** after the timeout everything goes black but the strip, at the lowest backlight step.
- **Home:** RESUME as the header (UP from the first row), then Music, Audiobooks, Books, Podcasts, Games, Focus,
  Power, Settings. Bluetooth lives in Settings. Sony's 3×3 grid is unchanged in the Sony theme.
- **Now playing:** the cover at 236 px, counter + play mode under it, the queue below; UP/DOWN browse it while it
  plays (UP above the first song returns to the cover), PLAY jumps, BACK or 8 s idle returns.
- **Fonts are not in the repo.** `tools/mkfont.py` turns a TTF into 4-bit bitmap faces (`ui_font.h`; `book_font.h`
  with several sizes). Prototype builds used DIN Alternate Bold and Georgia (Apple fonts: fine locally, not to
  redistribute). Characters a face lacks fall back to Sony's font. Mockups and renders: `reference/snrgy-ui/`.
- **Leaving a face set is a bug.** Any code that switches `gfx_face`/`gfx_caps` to measure text must restore them:
  the reader once left the book face on and every menu turned to Georgia.

## 6. Boot, switching and power

- `boot_dual.sh` boots whatever `/var/hb/mode` says (`player`/`sony`), written by the player when it starts and when
  it hands over. Power-off on either side comes back to the same side.
- **Player → Sony:** Settings > Switch to Sony's Walkman (asks once). The old "HOME held 3 s" exit was removed: it
  fired by accident, in games too.
- **Sony → player:** HOME + VOL− held together 2 s (hbwatch; HOLD blocks it). VOL− is the safe partner: holding OPTION
  powers the A82x off, VOL+ would blast the volume. **hbwatch is copied to RAM at boot**, so a new hbwatch only applies
  after a restart.
- **Safety net:** `/var/hb/starting` is set before each player start and cleared after a healthy minute; if it is
  still there at boot (hang, watchdog, or a flat battery), the unit boots Sony's UI and makes it the default.
- **One-shot scripts:** any `HB/once/*.sh` runs once at the next boot (renamed `.done`, output in `.log`). Useful to
  set `/var/hb/boot_player` from the Mac when a unit is stuck in Sony's UI. Needs the hook firmware.
- **Power off** now saves, unmounts, darkens the screen and keeps asking for `RB_POWER_OFF` (with `poweroff -f` and
  `halt -f -p` as fallbacks) instead of falling back into the UI. With a charger connected the hardware may still
  restart.
- **A black screen is usually sleep.** With nothing playing, the player suspends after a few idle minutes; any key
  wakes it. Plugged into a computer, it shows only the USB splash (the drive is handed to the computer).
- **Logs** after every boot: `HB/out/boot_events.txt`, `player_log.txt`, `hbwatch_log.txt`, `usb_log.txt`,
  `var_hb.txt`. An empty `HB/out` means the hook firmware has never run on that unit.

## 7. The reader (Books)

- EPUB via the zlib already linked for podcasts: zip central directory → `container.xml` → OPF → spine; chapter names
  from the EPUB 3 nav or the NCX; XHTML → paragraphs and headings. TXT: UTF-8 or Latin-1, paragraphs at blank lines,
  chapters at "CHAPTER"/"Capítulo" lines. `reader_test` (`-DREADER_HOST`) checks parsing on the Mac.
- **Map typographic characters** (curly quotes, dashes, bullets, ™, œ, ligatures, invisible spaces) to what the faces
  have, in both EPUB and TXT: otherwise they show as "?". A scan of the test books now finds nothing above Latin-1.
- Keys: UP/DOWN pages, LEFT/RIGHT brightness, PLAY portrait ↔ full-screen landscape (text drawn turned 90°, strip
  hidden), VOL keys stay volume, OPTION chapters/bookmarks. Text size: Settings > Display > Book text size. The screen
  never sleeps while a book is open. Positions and bookmarks: `/var/hb/books.txt`.
- Test content: public-domain books (Project Gutenberg) and freely licensed games (Libbet, µCity Advance, Freedoom).

## 8. Open items

- Verify the A919's direct DSP mode and the A91x texture remap on the device.
- Calibrate the A919 battery curve (log a full discharge; the player logs every 5 min).
- The Mac sync tool (covers pre-sized to 236 px, `._` cleanup, FLAC conversion), a SNRGY EQ screen, PDF via a Mac-side
  converter, Books in the Sony theme's grid.
- Open-licence fonts for anything shared beyond prototypes.

Future projects and ideas (USB audio, USB-C, LoRa mesh, AirPods, UI ideas): see [FUTURE.md](FUTURE.md).
