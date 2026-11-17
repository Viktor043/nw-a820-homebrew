/* ctxopt.c: OPTION inside a menu offers what fits that menu (2026-11-17), instead of the sound settings everywhere.
 *   Music lists      Now playing · Shuffle · Repeat · Sound
 *   Albums           the same + list / mosaic view
 *   Audiobooks       Now playing · Speed · Sleep timer · Big covers · Voice
 *   Books            Text size · Orientation · Start this book over
 *   Games            Now playing · Controls (of the selected game)
 *   Podcasts         their own show menu (podui.c), unchanged
 *   Focus, Settings, Bluetooth: settings screens already, OPTION does nothing there.
 * opt_rows() builds the rows (the screenshots use it too); opt_run() shows them and acts. CTX_SHOT: rows only. */
#include "hb.h"
#include <stdio.h>
#include <string.h>

int opt_rows(int kind, const struct opt_state *st, char lab[][48], int *act)
{
	int n = 0;
#define ROW(a, ...) do { snprintf(lab[n], 48, __VA_ARGS__); act[n++] = (a); } while (0)
	if (st->np && kind != OPT_BOOKS) ROW(OA_NP, "Reproduciendo");
	switch (kind) {
	case OPT_MUSIC: case OPT_ALBUMS:
		ROW(OA_SHUFFLE, "Aleatorio: %s", st->shuffle ? "sí" : "no");
		ROW(OA_REPEAT, "Repetir: %s", st->repeat == 1 ? "todo" : st->repeat == 2 ? "una" : "no");
		if (kind == OPT_ALBUMS) ROW(OA_VIEW, "%s", st->grid ? "Ver como lista" : "Ver como mosaico");
		ROW(OA_SOUND, "Sonido");
		break;
	case OPT_AUDIOBOOKS:
		ROW(OA_SPEED, "Velocidad: %d.%02dx", st->speed / 100, st->speed % 100);
		if (st->sleep > 0) ROW(OA_SLEEP, "Temporizador: %d min", st->sleep); else ROW(OA_SLEEP, "Temporizador: no");
		ROW(OA_COVERS, "Portadas grandes: %s", st->big ? "sí" : "no");
		ROW(OA_VOICE, "Voz");
		break;
	case OPT_BOOKS: {
		static const char *const sz[4] = { "pequeña", "mediana", "grande", "muy grande" };
		ROW(OA_BOOKSIZE, "Letra: %s", sz[st->book_size & 3]);
		ROW(OA_BOOKLAND, "Orientación: %s", st->book_land ? "horizontal" : "vertical");
		ROW(OA_RESTART, "Empezar este libro de nuevo");
		break; }
	case OPT_GAMES:
		ROW(OA_CONTROLS, "Controles");
		break;
	}
#undef ROW
	return n;
}

#ifndef CTX_SHOT
static char rows[8][48];
static int acts[8];
static const char *row_label(int i) { return rows[i]; }

static void state_now(struct opt_state *st)
{
	memset(st, 0, sizeof *st);
	st->np = eng_active();
	st->shuffle = eng_shuffle; st->repeat = eng_repeat; st->grid = ui_album_grid; st->big = ui_big_rows;
	st->speed = eng_speed; st->sleep = eng_sleep_left(); st->book_size = ui_book_size; st->book_land = ui_book_land;
}

/* the options of `kind` for row `sel` of its list; 1 = the list must be set up again (list_run returns -2) */
int opt_run(int kind, int sel)
{
	struct list_view v = { "Opciones", 0, 0, 0, row_label, 0, 0, LI_NONE, 0, 0 };
	for (;;) {
		struct opt_state st; state_now(&st);
		v.n = opt_rows(kind, &st, rows, acts);
		if (v.sel >= v.n) v.sel = 0;
		int s = list_run(&v);
		if (s < 0) return 0;
		switch (acts[s]) {
		case OA_NP: nowplaying_run(); return 0;
		case OA_SHUFFLE: eng_shuffle = !eng_shuffle; settings_save(); break;
		case OA_REPEAT: eng_repeat = (eng_repeat + 1) % 3; settings_save(); break;
		case OA_VIEW: ui_album_grid = !ui_album_grid; settings_save(); return 1;
		case OA_SOUND: sound_quick_menu(0); break;
		case OA_SPEED: { static const int sp[] = { 100, 125, 150, 175, 200, 75 }; int i = 0; while (i < 6 && sp[i] != eng_speed) i++; eng_speed = sp[(i + 1) % 6]; settings_save(); break; }
		case OA_SLEEP: { int m = eng_sleep_min; eng_set_sleep(m >= 60 ? 0 : m + 15); break; }
		case OA_COVERS: ui_big_rows = !ui_big_rows; settings_save(); return 1;
		case OA_VOICE: voice_quick_menu(); break;
		case OA_BOOKSIZE: ui_book_size = (ui_book_size + 1) % 4; settings_save(); break;
		case OA_BOOKLAND: ui_book_land = !ui_book_land; settings_save(); break;
		case OA_RESTART: reader_restart(sel); return 1;
		case OA_CONTROLS: game_controls(sel); break;
		}
	}
}
int opt_music(int i) { return opt_run(OPT_MUSIC, i); }
int opt_albums(int i) { return opt_run(OPT_ALBUMS, i); }
int opt_audiobooks(int i) { return opt_run(OPT_AUDIOBOOKS, i); }
int opt_books(int i) { return opt_run(OPT_BOOKS, i); }
int opt_games(int i) { return opt_run(OPT_GAMES, i); }
#endif
