-- runtime_test: checks of the hbapps runtime, run in the PC simulator by run_tests.py (any error = failure)
local function eq(a, b, what) if a ~= b then error(what .. ": got " .. tostring(a) .. ", expected " .. tostring(b), 2) end end
local function fails(f, what) if pcall(f) then error(what .. " should have failed", 2) end end

-- sandbox: no files outside H:\, no processes, no environment
eq(io, nil, "io"); eq(dofile, nil, "dofile"); eq(loadfile, nil, "loadfile")
eq(os.execute, nil, "os.execute"); eq(os.remove, nil, "os.remove"); eq(os.getenv, nil, "os.getenv")
assert(os.time() > 0 and os.date("%Y") ~= nil, "os.time / os.date")
fails(function() hb.read("../etc/passwd") end, "read ..")
fails(function() hb.read("/etc/passwd") end, "read absolute")
fails(function() hb.save("../x", "y") end, "save outside the data folder")
fails(function() hb.save(".hidden", "y") end, "save hidden")

-- data round trip
assert(hb.save("t1", "hello\0world"), "save")
eq(hb.load("t1"), "hello\0world", "load")
eq(hb.load("never_saved"), nil, "load missing")

-- files on H:\ and modules next to the app
local d = hb.dir(hb.appdir)
local seen = false
for _, n in ipairs(d) do if n == "runtime_test.lua" then seen = true end end
assert(seen, "hb.dir lists the app's folder")
eq(require("helper").answer, 42, "require")
eq(require("helper").loads, 1, "require loads once")

-- hb.fit: line breaking (the fallback font is 8 px per character at scale 1)
local cw = hb.textw("a")
local s = "aaa bbb ccc"
local j, nxt = hb.fit(s, 1, cw * 7)		-- "aaa bbb" fits, " ccc" does not
eq(s:sub(1, j), "aaa bbb", "fit words"); eq(nxt, 9, "fit next skips the space")
j, nxt = hb.fit(s, nxt, cw * 7); eq(s:sub(9, j), "ccc", "fit last"); eq(nxt, #s + 1, "fit end")
j, nxt = hb.fit("ab\ncd", 1, 1000); eq(j, 2, "fit newline end"); eq(nxt, 4, "fit after newline")
j, nxt = hb.fit("\n\nx", 1, 1000); eq(j, 0, "fit empty line"); eq(nxt, 2, "fit empty line next")
j, nxt = hb.fit("abcdefghij", 1, cw * 4); eq(j, 4, "fit cuts a long word"); eq(nxt, 5, "fit long word next")
j, nxt = hb.fit("ééééé", 1, cw * 2); assert(j % 2 == 0, "fit cuts at a UTF-8 boundary")
j, nxt = hb.fit("abc", 4, 100); eq(nxt, 4, "fit at the end")

-- drawing and lists (the simulator feeds keys from HB_KEYS: "down play" picks the 2nd row)
hb.screen("Test"); hb.text(10, 40, "text"); hb.fill(0, 0, 10, 10, hb.rgb(255, 0, 0)); hb.flip()
eq(hb.list("Pick", { "one", 2, "three" }, 1, { "a", nil, "c" }), 2, "list pick")
eq(hb.key(), "back", "key")
eq(hb.list("Empty", {}), nil, "empty list returns nil on back")
local used, cap = hb.mem(); assert(used > 0 and used < cap, "mem")
print("runtime_test: ok")
