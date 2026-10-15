-- Stopwatch with laps. PLAY start / stop, OPTION lap (running) or reset (stopped), BACK quit.
local en = hb.lang == "en"
local t0, acc, running, laps = 0, 0, false, {}

local function elapsed() return acc + (running and (hb.clock() - t0) or 0) end
local function fmt(s)
	local m = math.floor(s / 60)
	return string.format("%02d:%04.1f", m, s - m * 60)
end

local function draw()
	hb.screen(en and "Stopwatch" or "Cronómetro")
	local s = fmt(elapsed())
	hb.center(hb.W // 2, 60, s, running and hb.ORANGE or hb.WHITE, 3)
	hb.center(hb.W // 2, 136, running and (en and "PLAY stop · OPTION lap" or "PLAY parar · OPTION vuelta")
		or (en and "PLAY start · OPTION reset" or "PLAY empezar · OPTION reiniciar"), hb.GREY)
	for i = #laps, math.max(1, #laps - 4), -1 do
		local y = 170 + (#laps - i) * 22
		hb.text(24, y, string.format("%2d", i), hb.GREY)
		hb.text(70, y, fmt(laps[i]), hb.WHITE)
	end
	hb.flip()
end

while true do
	draw()
	local k = hb.key(running and 100 or -1)	-- redraw 10 times a second while running, otherwise just wait
	if k == "back" then break
	elseif k == "play" then
		if running then acc = elapsed(); running = false else t0 = hb.clock(); running = true end
	elseif k == "option" then
		if running then laps[#laps + 1] = elapsed() else acc, laps = 0, {} end
	end
end
