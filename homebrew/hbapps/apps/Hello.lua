-- Hello: the smallest complete app, and a tour of the hb API. Copy it to H:\Apps\ and start it from Juegos.
-- Keys come back as names: "up" "down" "left" "right" "play" "back" "option" "bt" "vol+" "vol-".
-- BACK held 2 s always quits, whatever the app is doing.

local en = hb.lang == "en"
local runs = tonumber(hb.load("runs") or "0") + 1	-- small values survive between runs (H:\Apps\.data\Hello\)
hb.save("runs", tostring(runs))

local function home()
	hb.screen("Hello")			-- Sony background, title bar and bottom bar
	hb.center(hb.W // 2, 44, en and "Hello!" or "¡Hola!", hb.ORANGE, 2)
	hb.wrap(16, 96, en and ("Opened " .. runs .. (runs == 1 and " time" or " times") .. ". PLAY opens a list, BACK quits.")
		or ("Abierta " .. runs .. (runs == 1 and " vez" or " veces") .. ". PLAY abre una lista, BACK sale."), hb.WHITE, hb.W - 32, 4)
	local used, cap = hb.mem()
	hb.text(16, 250, string.format("Lua %d KB / %d KB", used // 1024, cap // 1024), hb.GREY)
	hb.flip()				-- nothing shows until flip
end

while true do
	home()
	local k = hb.key()			-- waits; hb.key(500) would return nil after 500 ms
	if k == "back" then break end
	if k == "play" then
		local fruits = { "Apple", "Banana", "Cherry", "Fig", "Grape", "Kiwi", "Lemon", "Mango", "Orange", "Pear", "Plum" }
		local i = hb.list(en and "Pick one" or "Elegí una", fruits, 1, { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11" })
		if i then hb.message("Hello", fruits[i], en and "Any key to go back" or "Cualquier tecla para volver"); hb.key() end
	end
end
