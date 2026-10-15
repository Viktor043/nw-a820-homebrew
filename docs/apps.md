# Apps for the Walkman (hbapps)

Small Lua programs that run full screen on the NW-A829, next to the music. An app is one file,
`H:\Apps\<Name>.lua`, or a folder `H:\Apps\<Name>\` with a `main.lua` (and any other `.lua` files it `require`s, plus
its data files). Apps show up in **Juegos** with the value `App`; PLAY starts one. The music keeps playing, and
**BACK held 2 s always quits**, whatever the app is doing (BACK held 6 s and the player kills it).

The runtime is `homebrew/hbapps/hbapps.c`: Lua 5.4 plus the player's own drawing code, so apps get Sony's font,
title bar and lists for free. The sample apps are in `homebrew/hbapps/apps/`.

## A complete app

```lua
local n = tonumber(hb.load("count") or "0")
while true do
	hb.screen("Counter")
	hb.center(hb.W // 2, 120, tostring(n), hb.ORANGE, 3)
	hb.flip()
	local k = hb.key()
	if k == "back" then break end
	if k == "play" then n = n + 1; hb.save("count", tostring(n)) end
end
```

The model is a plain loop: draw, `hb.flip()`, wait for a key with `hb.key()`, react. Nothing reaches the screen
until `hb.flip()`.

## Screen

240 x 320 pixels, RGB565. The Sony layout has a title bar `[0, hb.TOP)` (28 px) and a bottom bar
`[hb.BOTTOM, 320)` (from 288); the content area is in between. One text line is `hb.LINE` (22) px high.

| Call | What it does |
|---|---|
| `hb.screen(title)` | Sony background, title bar with `title`, bottom bar. The usual start of a frame. |
| `hb.clear([color])` | Fill the whole screen (default black). |
| `hb.fill(x, y, w, h, [color])` / `hb.frame(x, y, w, h, [color])` | Filled / outlined rectangle. |
| `hb.text(x, y, s, [color], [scale])` -> x | UTF-8 text in Sony's font, top-left at x, y; returns the x after it. Scale 1-4. |
| `hb.textw(s, [scale])` -> w | Width in pixels. |
| `hb.center(cx, y, s, [color], [scale])` | Text centred on cx. |
| `hb.wrap(x, y, s, [color], [maxw], [lines], [lineh])` -> n | Word-wrapped text; returns the lines used. The last line ends in "..." if it doesn't all fit. |
| `hb.fit(s, i, maxw)` -> j, next | The line starting at byte i that fits in maxw px is `s:sub(i, j)`; `next` is where the next line starts. Breaks at spaces and `\n`, cuts words too long for a line at a UTF-8 boundary. Measures only that line, so it's cheap on big texts (see the Reader app). |
| `hb.flip()` | Show what was drawn. |
| `hb.message(title, line1, [line2])` | A message screen, shown at once. |
| `hb.rgb(r, g, b)` -> color | Colours are RGB565 integers. Named: `hb.BLACK WHITE GREY DARK ORANGE BLUE RED GREEN`. |
| `hb.backlight(0..100)` | Backlight level. |

## Keys

`hb.key([timeout_ms])` waits for a key and returns its name, or `nil` after `timeout_ms` (no argument: wait for
ever). Names: `"up" "down" "left" "right" "play" "back" "option" "bt" "vol+" "vol-"`. The d-pad repeats while
held. `"back"` arrives when BACK is released (a hold of 2 s quits instead). For animation, loop on `hb.key(50)`.

`hb.list(title, items, [sel], [values], [empty])` -> index or nil: a Sony-style list (the player's own), with the
right-aligned orange `values` if given. UP/DOWN move, LEFT/RIGHT page, PLAY picks (returns the 1-based index), BACK
returns `nil`. `empty` is the text shown when `items` is empty.

## Files and data

| Call | What it does |
|---|---|
| `hb.load(name)` -> string or nil | Read a value the app saved before (nil if none). |
| `hb.save(name, s)` -> true or nil, err | Save a value, atomically. Names: letters, digits, `_ - .`, no leading dot. Stored in `H:\Apps\.data\<App>\`. |
| `hb.read(path)` -> string or nil, err | Read a file on H:\ (path relative to H:\, `/` as separator, no `..`), up to 2 MB. |
| `hb.dir(path)` -> names or nil, err | Sorted names in a folder on H:\; folders end in `/`; hidden names are skipped. |
| `require "name"` | Load `name.lua` from the app's own folder (once). |
| `hb.appdir`, `hb.app` | The app's folder relative to H:\ (`"Apps"` or `"Apps/Reader"`) and its name. |

Apps can read anything on H:\ (music, books, podcasts) but write only to their own data folder.

## Everything else

- `hb.lang` is `"es"` or `"en"` (the player's language). `hb.time()` is the Unix time, `hb.clock()` a precise
  clock in seconds, `hb.sleep(ms)` waits. `os.time`, `os.date`, `os.clock` work; `print` goes to the log.
- `hb.mem()` -> used, cap: the Lua heap is capped at 6 MB. Past it, the app stops with "not enough memory".
- Not available: `io`, `package`, `debug`, `dofile`, `loadfile`, `os.execute` and the other `os` functions that
  touch files, processes or the environment.
- An error stops the app and shows `file:line: message` on screen; any key closes it.
- The CPU runs at 124 MHz while an app runs (the games get 248): redraw when something changes, not in a tight
  loop. (Speeds on the Walkman itself are still to be measured: the runtime has only run in the simulator so far.)

## Trying an app on the PC

The simulator builds from the same code (`sh homebrew/hbapps/build.sh sim` with `ZIG` set) and runs an app on a key
script, writing one frame per `hb.flip()`:

```
HB_CONTENTS=/path/to/a/fake/H HB_KEYS="down down play back" HB_SHOTS=/tmp/frames HB_LANG=en \
    homebrew/hbapps/hbapps_sim /path/to/a/fake/H/Apps/MyApp.lua
```

`none` in the script is a key timeout; the end of the script quits the app. Frames are raw RGB565 240x320
(`homebrew/tests/run_tests.py` shows how to turn them into PNG). Without Sony's `res` folder (`HB_RES`, extracted from your own
firmware) the simulator uses a fallback 8x16 font: text widths differ from the Walkman's and non-ASCII symbols show
as `?`, so leave some margin and measure with `hb.textw` instead of assuming a width.
`python homebrew/tests/run_tests.py` runs the runtime checks and every sample app this way.

## Writing apps with a coding agent

`.claude/skills/walkman-app/SKILL.md` teaches Claude Code (and any agent that reads it) the rules above: describe
the app, get a `.lua` file, try it in the simulator, copy it to `H:\Apps`.
