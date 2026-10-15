---
name: walkman-app
description: Write or fix a Lua app for the Sony NW-A829 homebrew (hbapps): anything that should run on the Walkman's screen from H:\Apps, such as a tool, a reader, a timer or a small game. Use it whenever the user asks for a Walkman app, an hbapps app or a .lua file for H:\Apps.
---

# Walkman apps (hbapps)

An app is one Lua 5.4 file, `H:\Apps\<Name>.lua`, or a folder `H:\Apps\<Name>\main.lua` with more modules and data
files. Read `docs/apps.md` for the whole API before writing anything, and copy the shape of the sample apps in
`homebrew/hbapps/apps/` (Hello.lua is the template, Reader/main.lua the reference for text).

## Rules that are easy to get wrong

- Screen 240x320. Sony layout: `hb.screen(title)` draws the title bar (0-28) and bottom bar (288-320); content goes
  between `hb.TOP` and `hb.BOTTOM`, `hb.LINE` (22 px) per text line, about 10 lines.
- Nothing shows until `hb.flip()`. Draw a whole frame, then flip.
- The main loop is draw -> flip -> `local k = hb.key([timeout_ms])` -> react. `"back"` must leave the current
  screen (or the app at the top level). Never busy-wait: use `hb.key(ms)` for timing.
- Keys: `"up" "down" "left" "right" "play" "back" "option" "bt" "vol+" "vol-"`. There is no keyboard: prefer lists
  (`hb.list`), presets and the d-pad over text entry.
- Persist small state with `hb.save(name, string)` / `hb.load(name)` (strings only: serialise numbers and tables
  yourself). Read files with `hb.read(path)` and `hb.dir(path)`, paths relative to H:\ with `/`.
- No `io`, `require` of standard modules, `os.execute`, `dofile`, network or threads. `require "x"` only loads x.lua
  from the app's own folder.
- Support both languages when the app has interface text: `local en = hb.lang == "en"`.
- 6 MB Lua heap; files up to 2 MB through `hb.read`. CPU is a 124 MHz ARM9: no per-pixel loops over the whole screen
  every frame.

## Check it before handing it over

Build the simulator once (`ZIG=/path/to/zig sh homebrew/hbapps/build.sh sim`), put the app in a fake drive and run
it on a key script that visits every screen:

```
mkdir -p /tmp/H/Apps && cp MyApp.lua /tmp/H/Apps/
HB_CONTENTS=/tmp/H HB_KEYS="down play none back back" HB_SHOTS=/tmp/frames HB_LANG=en \
    homebrew/hbapps/hbapps_sim /tmp/H/Apps/MyApp.lua; echo $?
```

Exit code 0 means no Lua error (an error prints `file:line: message`). Convert a few frames to PNG and look at them
(the code in `homebrew/tests/run_tests.py` does the RGB565 conversion). Without Sony's res folder the simulator uses a fallback
font whose widths differ from the Walkman's: measure with `hb.textw` / `hb.fit` rather than assuming widths.

Tell the user to copy the file (or folder) to `H:\Apps\` and start it from Juegos; BACK held 2 s quits.
