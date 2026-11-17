/* shots.c: renders every screen of the player on the PC (same drawing code as the Walkman) into raw
 * RGB565 files, converted to PNG by shots.py / tests/run_tests.py. usage: shots RESDIR OUTDIR [MP3 with cover] [es|en]
 * In English every text drawn is checked: Spanish left untranslated goes to OUTDIR/untranslated.txt. */
#include "hb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* English check: what ui_tr leaves as it was and still looks Spanish (accents, common words), minus the sample
 * content (song and book titles are not the interface) */
static const char *content[] = { "The Lazy Bee", "Two Cubs", "The Flamingo Socks", "The Giant Tortoise",
	"Crossing the River", "The Bald Parrot", "The Caiman War", "The Blind Doe", "Stories of the Forest", "M. Ostberg",
	"Short Stories", "All songs", "Screen: rotated", "Quit game", "Save state", "Load state", "Resume", "Game paused", 0 };
static char left[200][96]; static int nleft;
static int looks_spanish(const char *s)
{
	static const char *w[] = { " de ", " del ", " con ", " para ", " los ", " las ", " el ", " y ", "Buscar", "Volver", "episodio",
		"canci", "Ajustes", "pausa", "salir", "terminar", "falta", "desconect", "sonando", "guardar", "cambiar", "encendido", "Apagar", "Sonido",
		"Pantalla", "Despertador", "Velocidad", "Temporizador", "Repetir", "Aleatorio", "Conectar", "Bajar", "Borrar", "Cancelar", "Seguir",
		"Ver ", "nuevo", "quedan", "escuchado", "Leyendo", "Abriendo", "Juegos", 0 };	/* Spanish markers only */
	if (strlen(s) <= 2) return 0;	/* a key of the keyboard: one letter */
	for (int i = 0; content[i]; i++) if (strstr(s, content[i])) return 0;
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) if (*c == 0xC3 && strchr("\xa1\xa9\xad\xb3\xba\xb1\x81\x89\x8d\x93\x9a\x91", c[1])) return 1;
	if (strstr(s, "\xc2\xbf") || strstr(s, "\xc2\xa1")) return 1;
	for (int i = 0; w[i]; i++) if (strstr(s, w[i])) return 1;
	return 0;
}
static const char *check_tr(const char *s)
{
	const char *t = ui_tr(s);
	if (t == s && looks_spanish(s)) {
		int seen = 0;
		for (int i = 0; i < nleft; i++) if (!strcmp(left[i], s)) seen = 1;
		if (!seen && nleft < 200) snprintf(left[nleft++], sizeof left[0], "%s", s);
	}
	return t;
}

void lg(const char *fmt, ...) { (void)fmt; }
void hb_alive(void) {}
static int fake_dl = 0, fake_pct = 45;
int dl_active(void) { return fake_dl; }
int dl_percent(void) { return fake_pct; }

static const char *outdir;
static const char *song_at(int i);
static void save(const char *name)
{
	char p[512]; snprintf(p, sizeof p, "%s/%s.rgb565", outdir, name);
	FILE *f = fopen(p, "wb"); if (!f) return;
	static uint16_t out[W * H];
	gfx_composed(out);	/* as the LCD shows it (the SNRGY looks move the status strip to the top) */
	fwrite(out, 2, W * H, f); fclose(f);
}

static const char *tracks[] = { "01 - The Lazy Bee", "02 - Two Cubs of the Forest and Two Cubs of the Town",
	"03 - The Flamingo Socks", "04 - The Giant Tortoise", "05 - Crossing the River", "06 - The Bald Parrot",
	"07 - The Caiman War", "08 - The Blind Doe" };
static const char *music_root[] = { "Shuffle all", "Songs", "Artists", "Albums" };
static const char *artists[] = { "The Lanterns", "Paper Satellites", "June Harbor", "Blue Meridian" };
static const char *songs[] = { "Northern Lights", "Glass Rivers", "Ghost of the Harbor", "Slow Weather", "Paper Moon", "Static Bloom", "Long Way Home" };
static const char *opts[] = { "Sleep timer: 30 min", "Shuffle: on", "Repeat: all" };
static const char *games[] = { "Libbet and the Magic Floor", "Tobu Tobu Girl", "uCity" };
static const char *set_l[] = { "Sound", "Display", "Bluetooth", "Date and time", "Beep", "Sony Walkman: power saving", "Switch to Sony's Walkman", "Power", "Reset everything", "About" };
static const char *set_v[] = { "EQ off", "", "", "21:14", "off", "off", "", "", "", "" };
static const char *disp_l[] = { "Brightness", "Screen off after", "Big rows with covers", "Theme", "Always-on display", "Always-on brightness", "Status strip", "Book text size" };
static const char *disp_v[] = { "80%", "30 s", "off", "SNRGY silver", "on", "20%", "live", "medium" };
static const char *books_l[] = { "Pride and Prejudice - Jane Austen", "Moby Dick - Herman Melville", "The Time Machine - H. G. Wells", "Dracula - Bram Stoker", "Middlemarch - George Eliot", "The Odyssey - Homer", "Walden - Henry David Thoreau", "Emma - Jane Austen", "Persuasion - Jane Austen" };
static const char *books_v[] = { "37%", "", "100%", "12%", "", "", "4%", "", "" };
static char optl[8][48];
static int opta[8];
static const char *optl_label(int i) { return optl[i]; }
static const char *pw_l[] = { "Power off", "Cancel" };
static const char *games2[] = { "Libbet and the Magic Floor", "Tobu Tobu Girl", "sir-ababol-remastered", "twindragons", "freedoom1" };
static const char *games2_v[] = { "GB", "GB", "NES", "NES", "Doom" };
static const char *snd2_l[] = { "Now playing", "Now", "Equalizer", "Headphones", "Voice", "Even volume", "Sony effects", "Max. battery", "Sony DSP" };
static const char *snd2_v[] = { "", "max. battery", "Custom", "effects", "1.25x+", "off", "off", "on", "on" };
static const char *seq_l[] = { "Preset", "Clear Bass", "  400 Hz", "  1 kHz", "  2,5 kHz", "  6,3 kHz", "  16 kHz", "Advanced EQ" };
static const char *seq_v[] = { "Custom", "+2", "+1", "0", "-1", "+1", "+2", "off" };
static const char *snd_l[] = { "Equalizer", "Preset", "  60 Hz", "  250 Hz", "  1 kHz", "  4 kHz", "  12 kHz", "Bass boost", "Wide stereo" };
static const char *snd_v[] = { "on", "Rock", "+4 dB", "+2 dB", "-1 dB", "+2 dB", "+4 dB", "+3 dB", "on" };

static const char *bt_l[] = { "Connect: Speaker", "Find a new device", "Turn Bluetooth off", "Receiver mode (from the phone)" };
static const char *al_l[] = { "Alarm", "Time", "Ring with" };
static const char *al_v[] = { "on", "07:30", "last played" };
static const char *bo_l[] = { "Sleep timer: 30 min", "Speed: 1.50x" };
static const char *albums[] = { "Lowlands (Deluxe Edition)", "Blue Hour", "Harbor Lights", "Paper Kings", "The Long Road" };
static uint16_t thumb28[28 * 28];
static const uint16_t *bigpx;
static struct img bigthumb(int i) { struct img im = { 60, 60, bigpx + (i % 4) * 0 }; if (i == 3) im.w = 0; return im; }
static struct img album_thumb(int i) { struct img im = { 28, 28, thumb28 }; if (i > 2) im.w = 0; return im; }
static const char **cur;
static const char *lab(int i) { return cur[i]; }
static int mark2(int i) { return i == 2; }
static const char **curv;
static const char *val(int i) { return curv[i]; }

static const uint16_t *gthumb_px, *ethumb_px;
static struct img gthumb(int i) { struct img im = { GRID_TH, GRID_TH, gthumb_px + (size_t)(i % 9) * GRID_TH * GRID_TH }; if (i == 4) im.w = 0; return im; }
static struct img ethumb(int i) { (void)i; struct img im = { EP_THUMB, EP_THUMB, ethumb_px }; return im; }
static int epstate(int i) { return i < 2 ? 1 : i >= 3 ? 2 : 0; }
static int epprog(int i) { return i == 2 ? 62 : -1; }
static const char *song_at(int i) { return i >= 0 && i < 7 ? songs[i] : ""; }
int main(int argc, char **argv)
{
	if (argc < 3) return 1;
	outdir = argv[2];
	gfx_open();
	printf("res %d\n", gfx_load_res(argv[1]));
	if (argc > 4 && !strcmp(argv[4], "en")) { ui_lang_set(1); gfx_tr = check_tr; }
	if (argc > 5) ui_theme_set(atoi(argv[5]));	/* look: 0 Sony, 1 SNRGY silver, 2 SNRGY black */
	ui_st.batt = 72;

	int fr[HOME_SLOTS];
	for (int i = 0; i < HOME_SLOTS; i++) fr[i] = 8;
	ui_pod_new = 3;
	fr[4] = 0; scr_home(4, fr, 0); save("01_home_musica");
	ui_home_tab = 2; scr_home(4, fr, 0); save("01b_home_reanudar"); ui_home_tab = 0;
	ui_st.playing = 1; ui_st.np_title = "Northern Lights"; ui_st.np_artist = "The Lanterns";
	fr[4] = 8; fr[1] = 0; scr_home(1, fr, "Northern Lights"); save("02_home_audiolibros_sonando");
	fr[1] = 4; fr[2] = 4; scr_home(2, fr, "Northern Lights"); save("03_home_animacion_mitad");
	fr[1] = 8; fr[2] = 8; fr[6] = 0; scr_home(6, fr, "Northern Lights"); save("04_home_bluetooth");
	fr[6] = 8; fr[8] = 0; scr_home(8, fr, "Northern Lights"); save("04b_home_ajustes");
	ui_st.np_artist = 0;
	struct list_view st = { "Settings", 10, 0, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = set_l; curv = set_v; scr_list(&st); save("04c_ajustes");
	{ struct list_view dv = { "Display", 8, 3, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 }; cur = disp_l; curv = disp_v; scr_list(&dv); save("04j_pantalla"); }
	{ struct list_view pw = { "Power off?", 2, 1, 0, lab, 0, 0, LI_NONE, 0, 0, 0, -1 }; cur = pw_l; scr_list(&pw); save("04k_apagar"); }
	/* OPTION inside each menu (ctxopt.c builds the same rows as on the device) */
	{ static const char *const kname[] = { "", "30_opciones_musica", "31_opciones_albumes", "32_opciones_audiolibros", "33_opciones_libros", "34_opciones_juegos" };
	  struct opt_state os = { 1, 0, 1, 0, 0, 125, 30, 1, 0 };
	  for (int k = OPT_MUSIC; k <= OPT_GAMES; k++) {
		struct list_view ov = { "Options", 0, 0, 0, optl_label, 0, 0, LI_NONE, 0, 0, 0, -1 };
		ov.n = opt_rows(k, &os, optl, opta);
		scr_list(&ov); save(kname[k]);
	  } }
	{ struct list_view bl = { "Books", 9, 0, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 }; cur = books_l; curv = books_v; scr_list(&bl); save("21a_libros"); }
	struct list_view sn = { "Sound", 9, 1, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = snd_l; curv = snd_v; scr_list(&sn); save("04d_sonido");
	struct list_view sq = { "Sound", 9, 1, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = snd2_l; curv = snd2_v; scr_list(&sq); save("04e_sonido_unificado");
	struct list_view se = { "Equalizer", 8, 1, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = seq_l; curv = seq_v; scr_list(&se); save("04f_ecualizador_sony");
	{	/* big rows (Settings > Display > Big rows with covers) */
		static uint16_t big[4][60 * 60];
		for (int k = 0; k < 4; k++) for (int y = 0; y < 60; y++) for (int x = 0; x < 60; x++)
			big[k][y * 60 + x] = RGB(40 + k * 50 + x, 60 + y * 2, 180 - k * 40);
		static const char *al[] = { "Lowlands (Deluxe Edition)", "Blue Hour", "Harbor Lights", "Paper Kings", "The Long Road" };
		static const char *av[] = { "14 songs", "13 songs", "12 songs", "10 songs", "13 songs" };
		cur = al; curv = av;
		struct list_view bl = { "Albums", 5, 1, 0, lab, val, 0, LI_NONE, 0, 0, bigthumb, -1 };
		bl.big = 1; bigpx = big[0];
		scr_list(&bl); save("06c_albumes_grandes");
	}
	{
		static const char *nm[7] = { "Effects off", "Heavy", "Pop", "Jazz", "Unique", "Custom", "Advanced EQ (processor)" };
		int z[5] = { 0, 0, 0, 0, 0 }, heavy[5] = { 1, -1, 0, 1, 1 }, cus[5] = { 1, 0, -1, 1, 2 };
		scr_sony_eq(nm, 7, 0, 0, 0, 0, z, -1, 1); save("04g_eq_sony_off");
		scr_sony_eq(nm, 7, 1, 0, 0, 1, heavy, -1, 0); save("04h_eq_sony_heavy");
		scr_sony_eq(nm, 7, 5, 1, 5, 2, cus, 3, 0); save("04i_eq_sony_editando");
	}

	struct list_view v = { "Music", 4, 2, 0, lab, 0, 0, LI_FOLDER, 0, 0, 0, -1 };
	cur = music_root; scr_list(&v); save("05_musica");
	v.title = "Artists"; v.n = 4; v.sel = 0; cur = artists; scr_list(&v); save("06_artistas");
	v.title = "Lowlands (Deluxe Edition)"; v.n = 7; v.sel = 2; cur = songs; v.icon = LI_TRACK; scr_list(&v); save("07_canciones");
	{ static const char *many[] = { "After the Storm", "All the Way Down", "Amber Skies", "Before the Tide", "Blue Hour", "Borrowed Time", "Brand New Morning", "Bright Static", "Broken Compass", "Burning Slow", "Calm Water", "Coming Home" };
	  struct list_view m = { "All songs", 12, 3, 0, lab, 0, 0, LI_TRACK, 0, 40, 0, -1 }; cur = many; scr_list(&m); save("07b_todas_marquee"); }

	static uint16_t art[PLAYER_ART * PLAYER_ART];
	struct player_view p = { "Songs", "Ghost of the Harbor", "The Lanterns - Lowlands (Deluxe Edition)", 2, 14, 71, 233, 0, 14, 20, 0, "", { 0, 0, 0 } };
	p.genre = "Alternative"; p.year = "2010";
	if (argc > 3 && argv[3][0] && cover_load(argv[3], art, PLAYER_ART)) { p.art.w = p.art.h = PLAYER_ART; p.art.px = art; printf("cover ok\n"); }
	p.qtitle = song_at; p.ntr = 7;	/* the queue under the cover (SNRGY) */
	ui_st.np_title = "Ghost of the Harbor"; ui_st.np_artist = "The Lanterns"; ui_st.np_idx = 2; ui_st.np_n = 7; ui_st.np_sec = 71; ui_st.np_total = 233;
	static uint16_t big_art[PLAYER_ART_BIG * PLAYER_ART_BIG];	/* SNRGY: an invented gradient cover at the big size */
	if (ui_theme) { for (int y = 0; y < PLAYER_ART_BIG; y++) for (int x = 0; x < PLAYER_ART_BIG; x++) { int d = (x - 150) * (x - 150) + (y - 90) * (y - 90); int t = d > 40000 ? 0 : 255 - d * 255 / 40000; big_art[y * PLAYER_ART_BIG + x] = RGB(40 + t * 3 / 4, 55 + t / 2, 72 + t / 6); }
	  p.art.w = p.art.h = PLAYER_ART_BIG; p.art.px = big_art; }
	scr_player(&p, 1); save("08_reproduciendo_musica");
	{ struct img keep = p.art; p.art.w = 0; scr_player(&p, 1); save("08g_no_cover"); p.art = keep; }
	p.browsing = 1; p.cursor = 4; p.shuffle = 0; p.repeat = 1;
	scr_player(&p, 1); save("08c_reproduciendo_lista");
	p.browsing = 0; p.repeat = 0;
	{	/* the reader: an original sample page (reader.c, READER_SHOT) */
		extern void reader_shot(const char *title, const char *author, const char *text);
		reader_shot("The Harbour Clock", "M. Ostberg",
			"\1Chapter One\n"
			"The clock above the harbour office had run four minutes slow for as long as anyone in the town could remember, and nobody had ever thought to correct it. The ferries kept their own time, the fishermen kept the tide's, and the children kept whatever time the bells at the school decided to keep.\n"
			"Ines noticed it on the first morning, standing on the quay with her suitcase between her feet. She checked her watch twice, then a third time, and finally decided that the town was simply living a little behind the rest of the world.\n"
			"It suited her. She had come here to be a little behind.");
		save("21_libro");
		static const char *sample = "\1Chapter One\nThe clock above the harbour office had run four minutes slow for as long as anyone in the town could remember, and nobody had ever thought to correct it. The ferries kept their own time, the fishermen kept the tide's, and the children kept whatever time the bells at the school decided to keep.";
		ui_book_size = 3; reader_shot("The Harbour Clock", "M. Ostberg", sample); save("21c_libro_grande");
		ui_book_size = 1; ui_book_land = 1; { int r = gfx_rot; gfx_rot = 0; reader_shot("The Harbour Clock", "M. Ostberg", sample); save("21b_libro_horizontal"); gfx_rot = r; } ui_book_land = 0;
	}
	if (ui_theme) {	/* the strip: a long title scrolling, and the always-on display */
		const char *ot = ui_st.np_title; ui_st.np_title = "A Very Long Song Title That Has To Scroll";
		scr_player(&p, 1); scr_strip_marquee(0); scr_strip_marquee(4.5); scr_strip(); save("08e_titulo_largo");
		ui_aod_now = 1; scr_aod(); save("08f_siempre_activa"); ui_aod_now = 0;
		ui_st.np_title = ot; scr_strip_marquee(10);
	}
	p.paused = 1; p.sleep_left = 30; ui_st.bt = 1; p.out = "BT"; ui_st.playing = 2;
	scr_player(&p, 1); save("09_pausa_timer_bt");
	p.paused = 0; ui_st.playing = 1; scr_player(&p, 1); scr_volume(6, 30); save("10_volumen");
	struct list_view o = { "Options", 3, 0, 0, lab, 0, 0, LI_NONE, 0, 0, 0, -1 };
	cur = opts; scr_list(&o); save("11_opciones");

	struct player_view b = { "M. Ostberg - Stories of the Forest", "03 - The Flamingo Socks", "M. Ostberg - Stories of the Forest", 2, 8, 191, 647, 0, 14, 20, 0, "", { 0, 0, 0 } };
	ui_st.bt = 0; scr_player(&b, 1); save("12_audiolibro");
	struct list_view t = { "M. Ostberg - Stories of the Forest", 8, 2, 0, lab, 0, mark2, LI_TRACK, 0, 0, 0, -1 };
	cur = tracks; scr_list(&t); save("13_capitulos");
	scr_player(&b, 1); scr_toast("Bluetooth disconnected"); save("14_aviso");

	/* new menus */
	struct list_view bl = { "BT: Speaker", 4, 3, 0, lab, 0, 0, LI_NONE, 0, 0, 0, -1 };
	cur = bt_l; scr_list(&bl); save("14b_bluetooth_receptor");
	scr_message("Bluetooth receiver", "On the phone, find the Walkman (NW-A829) and connect. PIN: 0000", "Waiting for a connection...");
	gfx_text_center(W / 2, 270, "173 s", C_GREY, 1); save("14c_receptor_esperando");
	scr_message("Bluetooth receiver", "Playing from the phone", "");
	gfx_text_center(W / 2, 270, "Vol 70%   BACK: stop", C_GREY, 1); save("14d_receptor_sonando");
	struct list_view al = { "Alarm", 3, 1, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = al_l; curv = al_v; scr_list(&al); save("20b_despertador");
	scr_screen("Alarm");
	gfx_text_center(W / 2, 120, "07:30", C_WHITE, 2); gfx_fill(78, 160, 34, 3, C_ORANGE);
	gfx_text_center(W / 2, 200, "up/down: change", C_GREY, 1); gfx_text_center(W / 2, 222, "left/right: h / min", C_GREY, 1); gfx_text_center(W / 2, 244, "PLAY: save", C_GREY, 1);
	save("20c_despertador_hora");
	scr_message("Alarm", "05 - Crossing the River", "any key: continue");
	gfx_text_center(W / 2, 60, "07:30", C_ORANGE, 2); save("20d_despertador_sonando");
	struct list_view bo = { "Options", 2, 1, 0, lab, 0, 0, LI_NONE, 0, 0, 0, -1 };
	cur = bo_l; scr_list(&bo); save("11b_opciones_audiolibro");
	if (art[0] || art[1]) { for (int y = 0; y < 28; y++) for (int x = 0; x < 28; x++) thumb28[y * 28 + x] = art[(y * PLAYER_ART / 28) * PLAYER_ART + x * PLAYER_ART / 28]; }
	struct list_view ab = { "Albums", 5, 0, 0, lab, 0, 0, LI_NONE, 0, 0, album_thumb, -1 };
	cur = albums; scr_list(&ab); save("06b_albumes_miniaturas");
	struct list_view g = { "Games", 5, 4, 0, lab, val, 0, LI_NONE, 0, 0, 0, -1 };
	cur = games2; curv = games2_v; scr_list(&g); save("15_juegos");
	/* lyrics view */
	struct player_view lv = { "Songs", "Ghost of the Harbor", "The Lanterns - Lowlands", 2, 14, 71, 233, 0, 14, 20, 0, "", { 0, 0, 0 }, 100, 1, "lights across the water", "carry me home tonight", "down the long road, the long road" };
	scr_player(&lv, 1); save("08b_letras");
	struct player_view bv = { "M. Ostberg - Stories of the Forest", "03 - The Flamingo Socks", "M. Ostberg - Stories of the Forest", 2, 8, 191, 647, 0, 14, 20, 0, "", { 0, 0, 0 }, 150, 0, 0, 0, 0 };
	scr_player(&bv, 1); save("12b_audiolibro_1.5x");
	struct player_view pc = { "Podcasts", "EP#247 | Why Old Movies Still Look Great", "The Weekly Signal", 0, 3, 1260, 3700, 1, 14, 20, 15, "BT", { 0, 0, 0 }, 100, 0, 0, 0, 0, 0, "2/5 · Sponsor break" };
	pc.meta = 1; pc.date = "12 jun 2026";
	ui_st.playing = 2; scr_player(&pc, 1); save("12c_podcast_capitulos"); ui_st.playing = 1;
	pc.chap = 0; pc.sleep_left = 0; scr_player(&pc, 1); save("12d_podcast_sin_capitulos");
	/* in-game menu (drawn by gnuboy's backend with the same font/textures) */
	{
		static const char *it[] = { "Resume", "Save state", "Load state", "Screen: rotated", "Quit game" };
		struct img bar = gfx_tex(14, 4), status = gfx_tex(16, 0);
		gfx_fill(0, 0, W, H, 0);
		if (status.w) gfx_blit(status, 0, 0);
		gfx_text(6, 4, "Game paused", 0xFFFF, 1);
		for (int i = 0; i < 5; i++) { int y = 40 + i * 34; if (i == 3) gfx_blit(bar, 0, y); gfx_text(10, y + 3, it[i], i == 3 ? 0 : 0xFFFF, 1); }
		save("16_menu_del_juego");
	}
		scr_downloads("Reading Podnews (3/3)", "48 KB · 41 KB/s · 2 episodes..", 1, 1, 0, 0, 0, 0);
	save("29b_leyendo_feed");
	scr_progress("New episodes", 2, 4, "Connecting to the phone", 12, 1, "Bluetooth tethering must be on on the phone");
	save("29_buscar_progreso");
	/* podcast downloads */
	{
		static const char *pod_l[] = { "Check for new", "The Weekly Signal", "History Hour" };
		static const char *pod_v[] = { "1/3 · 45%", "", "" };
		fake_dl = 1; ui_st.playing = 1;
		struct list_view pv = { "Podcasts", 3, 0, 0, lab, val, 0, LI_FOLDER, 0, 0, 0, -1 };
		cur = pod_l; curv = pod_v; scr_list(&pv); save("30_podcasts_descargando");
		struct dl_row rows[] = {
			{ "EP#247 | Why Old Movies Still Look Great", "The Weekly Signal", 25400000, 55714376, 1 },
			{ "The Bronze Age Collapse (1177 BC)", "History Hour", 0, 80074477, 0 },
			{ "The Manhattan Project (1942)", "History Hour", 0, 71200000, 0 },
		};
		scr_downloads("Downloading 1 of 3", "", 1, 0, rows, 3, 0, 58);
		save("31_descargas");
		rows[0].st = 2; rows[0].got = rows[0].total; rows[1].st = 1; rows[1].got = 12000000; fake_pct = 15;
		scr_downloads("Downloading 2 of 3", "", 1, 0, rows, 3, 0, 61);
		save("32_descargas_2");
		rows[1].st = 2; rows[1].got = rows[1].total; rows[2].st = 3; rows[2].got = 3000000; fake_dl = 0;
		scr_downloads("2 new episodes", "", 0, 0, rows, 3, 0, 0);
		save("33_descargas_fin");
		struct dl_row many[6];
		for (int i = 0; i < 6; i++) { many[i] = rows[i % 3]; many[i].st = i < 2 ? 2 : i == 2 ? 1 : 0; many[i].got = i < 2 ? many[i].total : i == 2 ? 30000000 : 0; }
		scr_downloads("Downloading 3 of 6", "131 kB/s · 22 min", 1, 0, many, 6, 1, 131);
		save("34_descargas_scroll");
	}
	{ struct focus_view fv = { 1122, 1500, 0, "brown noise", "bell every 5 min" }; fake_dl = 0; scr_focus(&fv); save("17_modo_foco"); }
	{ struct calc_view cv = { "1234.5", "1200 + 34.5 =", 19 }; scr_calc(&cv); save("18_calculator"); }
	scr_info("Always-on display", "When the screen times out, the status strip at the top stays lit: time, battery and what is playing."); save("19_info");
	{	/* mosaic of albums and episode rows */
		static uint16_t g[9][GRID_TH * GRID_TH], e[EP_THUMB * EP_THUMB];
		for (int k = 0; k < 9; k++) for (int y = 0; y < GRID_TH; y++) for (int x = 0; x < GRID_TH; x++) g[k][y * GRID_TH + x] = RGB(30 + k * 25, 60 + y, 200 - k * 20);
		for (int y = 0; y < EP_THUMB; y++) for (int x = 0; x < EP_THUMB; x++) e[y * EP_THUMB + x] = RGB(40 + x * 3, 30, 90 + y * 2);
		gthumb_px = g[0]; ethumb_px = e;
		static const char *al[] = { "Blue Hour", "Lowlands", "Harbor Lights", "Paper Kings", "The Long Road", "Open Sky", "First Light", "Live in Lisbon", "Live at the Hall", "Extra" };
		static const char *av[] = { "13 songs", "14 songs", "12 songs", "10 songs", "13 songs", "12 songs", "11 songs", "20 songs", "18 songs", "5 songs" };
		cur = al; curv = av;
		struct list_view gv = { "Albums", 10, 1, 0, lab, val, 0, LI_NONE, 0, 0, gthumb, -1 };
		gv.grid = 1; scr_list(&gv); save("06d_albumes_mosaico");
		static const char *et[] = { "EP#249 How a Film Score Is Recorded", "EP#248 Our First Week in the New Studio", "EP#247 Why Old Movies Still Look Great", "EP#246 Building a Set from Scratch", "EP#245 The Sound of Space", "EP#244 Old" };
		static const char *ev[] = { "25 Jun · 1 h 12 min", "12 Jun · 58 min", "22 min left", "played", "played", "played" };
		cur = et; curv = ev;
		struct list_view ep = { "The Weekly Signal", 6, 1, 0, lab, val, mark2, LI_TRACK, 0, 0, ethumb, -1 };
		ep.ep = 1; ep.state = epstate; ep.progress = epprog; scr_list(&ep); save("13b_episodios_portadas");
		static const char *sl[] = { "Check for new", "The Weekly Signal", "History Hour" };
		static const char *sv[] = { "from the phone", "2 new of 5", "7 episodes" };
		cur = sl; curv = sv;
		struct list_view sh = { "Podcasts", 3, 1, 0, lab, val, 0, LI_FOLDER, 0, 0, ethumb, -1 };
		sh.ep = 1; scr_list(&sh); save("13c_podcasts_programas");
	}
	{	/* podcasts: keyboard, search results, a show's card, its episodes; settings; first run */
		scr_keyboard("Find a podcast", "", "Podcast name", "Search", 0, 0, 0, 0); save("40_teclado");
		scr_keyboard("Find a podcast", "Se regalan dudas", "Podcast name", "Search", 1, 0, 2, 0); save("40b_teclado_123");
		static const char *rl[] = { "The Weekly Signal", "Historias breves", "History Hour" };
		static const char *rv[] = { "Signal Media \xc2\xb7 250 episodes", "Following", "A. Rivera \xc2\xb7 1 episode" };
		cur = rl; curv = rv;
		struct list_view r = { .title = "\"signal\"", .n = 3, .label = lab, .value = val, .thumb = ethumb, .ep = 1, .bar_y = -1 };
		scr_list(&r); save("41_resultados");
		const char *it[] = { "Follow this podcast", "Episodes" };
		struct img none = { 0, 0, 0 };
		scr_pod_show("The Weekly Signal", "Signal Media", "250 episodes \xc2\xb7 TV & Film", none, it, 2, 0); save("42_ficha");
		const char *it2[] = { "Get new episodes", "Episodes", "Unfollow" };
		scr_pod_show("The Weekly Signal", "Signal Media", "Following \xc2\xb7 250 episodes", none, it2, 3, 2); save("42b_ficha_seguido");
		static const char *el[] = { "EP#249 How a Film Score Is Recorded", "EP#248 Our First Week in the New Studio" };
		static const char *ev2[] = { "25 Jun \xc2\xb7 58 MB", "12 Jun \xc2\xb7 on the Walkman" };
		cur = el; curv = ev2;
		struct list_view e2 = { .title = "The Weekly Signal", .n = 2, .label = lab, .value = val, .thumb = ethumb, .ep = 1, .bar_y = -1 };
		scr_list(&e2); save("43_episodios_feed");
		static const char *po[] = { "Check for new episodes", "All episodes", "Download the latest 3", "Unfollow", "Sound" };
		cur = po;
		struct list_view o = { .title = "The Weekly Signal", .n = 5, .label = lab, .bar_y = -1 };
		scr_list(&o); save("44_opciones_podcast");
		static const char *pm[] = { "Phone: Pixel 7", "Search in: Argentina" };
		cur = pm;
		struct list_view om = { .title = "Podcasts", .n = 2, .label = lab, .bar_y = -1 };
		scr_list(&om); save("45_opciones_podcasts");
		static const char *aj[] = { "Sound", "Display", "Date and time", "Beep", "Sony Walkman: power saving", "Switch to Sony's Walkman", "Power", "Reset everything", "About" };
		static const char *ajv[] = { "max. battery", "", "21:14", "off", "off", "", "", "", "" };
		cur = aj; curv = ajv;
		struct list_view a = { .title = "Settings", .n = 9, .label = lab, .value = val, .bar_y = -1 };
		scr_list(&a); save("46_ajustes");
		scr_message("Welcome", "Settings > Switch to Sony's Walkman: to Sony's firmware.", "In Sony's Walkman, hold HOME and VOL- together for 2 s to come back.");
		gfx_text_wrap(14, 220, "OPTION: more choices on almost every screen.", C_GREY, W - 28, 2, 22);
		save("47_bienvenida");
		scr_message("Reset everything", "Clears positions, settings, Bluetooth and the boot mode. Your music stays.", ""); save("48_restablecer");
	}
	if (gfx_tr == check_tr) {
		char p[512]; snprintf(p, sizeof p, "%s/untranslated.txt", outdir);
		FILE *f = fopen(p, "w");
		for (int i = 0; f && i < nleft; i++) fprintf(f, "%s\n", left[i]);
		if (f) fclose(f);
		printf("untranslated: %d\n", nleft);
	}
	return 0;
}
