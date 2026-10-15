-- Reader: plain-text books, a page at a time, the place in each book remembered (the CrossPoint idea, on a Walkman).
-- Copy .txt files (UTF-8) to H:\Apps\Reader\. RIGHT / DOWN / PLAY next page, LEFT / UP previous, OPTION jump, BACK books.
local en = hb.lang == "en"
local X, Y, LH, LINES = 10, hb.TOP + 6, hb.LINE, 10
local WIDTH = hb.W - 2 * X

local function books()
	local out = {}
	for _, name in ipairs(hb.dir(hb.appdir) or {}) do
		if name:lower():match("%.txt$") then out[#out + 1] = name end
	end
	return out
end

local function key_for(name) return "pos_" .. name:gsub("[^%w_%-%.]", "_"):sub(1, 50) end

-- the lines of the page starting at byte `from`: { {i, j}, ... } and where the next page starts
local function layout(text, from)
	local lines, at = {}, from
	while #lines < LINES and at <= #text do
		local j, nxt = hb.fit(text, at, WIDTH)
		lines[#lines + 1] = { at, j }
		at = nxt
	end
	return lines, at
end

-- where the page before `start` begins: lay out a stretch of text before it and keep its last LINES lines
local function prev_page(text, start)
	if start <= 1 then return 1 end
	local from = math.max(1, start - 3000)
	if from > 1 then local nl = text:find("\n", from, true); from = (nl and nl < start) and nl + 1 or from end
	local starts, at = {}, from
	while at < start do
		starts[#starts + 1] = at
		local _, nxt = hb.fit(text, at, WIDTH)
		at = nxt
	end
	return starts[math.max(1, #starts - LINES + 1)] or 1
end

local function draw(title, text, lines, from)
	hb.screen(title)
	for n, l in ipairs(lines) do hb.text(X, Y + (n - 1) * LH, text:sub(l[1], l[2]), hb.WHITE) end
	local pct = #text > 0 and math.floor((from - 1) * 100 / #text) or 100
	hb.fill(X, 266, WIDTH, 2, hb.DARK)
	hb.fill(X, 266, WIDTH * pct // 100, 2, hb.ORANGE)
	hb.text(X, 268, pct .. "%", hb.GREY)
	hb.flip()
end

local function read(name)
	local text = hb.read(hb.appdir .. "/" .. name)
	if not text then hb.message(name, en and "Could not open it." or "No se pudo abrir.", ""); hb.key(3000); return end
	text = text:gsub("^\239\187\191", ""):gsub("\r\n?", "\n")	-- BOM, Windows line ends
	local title, key = name:gsub("%.[^.]*$", ""), key_for(name)
	local from = math.min(tonumber(hb.load(key) or "1") or 1, math.max(1, #text))
	while true do
		local lines, nxt = layout(text, from)
		draw(title, text, lines, from)
		hb.save(key, tostring(from))
		local k = hb.key()
		if k == "back" then return
		elseif k == "right" or k == "down" or k == "play" then if nxt <= #text then from = nxt end
		elseif k == "left" or k == "up" then from = prev_page(text, from)
		elseif k == "option" then
			local marks = { 0, 10, 25, 50, 75, 90 }
			local names = {}
			for i, p in ipairs(marks) do names[i] = p == 0 and (en and "Beginning" or "Inicio") or (p .. "%") end
			local i = hb.list(en and "Go to" or "Ir a", names)
			if i then
				local at = math.max(1, #text * marks[i] // 100)
				local nl = text:find("\n", at, true) or text:find(" ", at, true)	-- start at a paragraph (or a word)
				from = (at == 1) and 1 or (nl and math.min(nl + 1, #text) or at)
			end
		end
	end
end

local sel = 1
while true do
	local list = books()
	local names, vals = {}, {}
	for i, b in ipairs(list) do names[i] = b:gsub("%.[^.]*$", ""); vals[i] = hb.load(key_for(b)) and "·" or "" end
	sel = hb.list(en and "Reader" or "Lector", names, sel, vals,
		en and "Copy .txt books to H:\\Apps\\Reader" or "Copiá libros .txt a H:\\Apps\\Reader")
	if not sel then break end
	read(list[sel])
end
