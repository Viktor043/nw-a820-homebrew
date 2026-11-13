/* game_i18n.h: the emulators' pause menus in English when the player starts them with HB_LANG=en (the player's own
 * translation is hbplayer/i18n.c; these small programs only need their menu). Include once, then call
 * game_i18n_init() at start. */
#ifndef GAME_I18N_H
#define GAME_I18N_H
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const game_tr_tab[][2] = {
	{ "Juego en pausa", "Game paused" }, { "Continuar", "Resume" }, { "Guardar estado", "Save state" }, { "Cargar estado", "Load state" },
	{ "Salir del juego", "Quit game" }, { "Pantalla: rotada", "Screen: rotated" }, { "Pantalla: normal", "Screen: normal" },
	{ "Pantalla: horizontal 1.33x", "Screen: landscape 1.33x" }, { "Pantalla: horizontal 1:1", "Screen: landscape 1:1" },
	{ "Pantalla: vertical 1:1", "Screen: portrait 1:1" }, { "Estado guardado", "State saved" }, { "No se pudo guardar", "Could not save" },
	{ "No hay estado guardado", "No saved state" }, { "Volumen: (sin sonido)", "Volume: (muted)" },
	{ "No se puede abrir la ROM", "Can't open the ROM" }, { "No se pudo cargar el juego", "Could not load the game" },
	{ "\xc2\xbfMemoria insuficiente o ROM da\xc3\xb1" "ada?", "Not enough memory or a damaged ROM?" },
};

static const char *game_tr(const char *s)
{
	static char b[96];
	for (unsigned i = 0; i < sizeof game_tr_tab / sizeof *game_tr_tab; i++) if (!strcmp(s, game_tr_tab[i][0])) return game_tr_tab[i][1];
	if (!strncmp(s, "Volumen: ", 9)) { snprintf(b, sizeof b, "Volume: %s", s + 9); return b; }	/* "Volumen: 12  < >" */
	return s;
}

static void game_i18n_init(void)
{
	gfx_tr = game_tr;	/* 2026-11-13: English only */
}

/* the pause menu in the SNRGY black look (2026-11-13): no Sony textures (their status bar and orange bar), the
 * player's font, a light rounded row for the selection. `info` (may be 0) is a grey line at the bottom. */
#define GM_PAPER 0x18C3	/* #1A1B1D */
#define GM_INK 0xE71B	/* #E4E2DC */
#define GM_GREY 0x7BF0	/* #7E7F82 */
#define GM_RULE 0x39C7	/* #3A3B3E */
static void game_menu_row(int x, int y, int w, int h, uint16_t c)
{
	for (int j = 0; j < h; j++) {
		int in = j == 0 || j == h - 1 ? 3 : j == 1 || j == h - 2 ? 1 : 0;	/* small rounded corners */
		gfx_fill(x + in, y + j, w - 2 * in, 1, c);
	}
}
static void game_menu_draw(const char *const *items, int n, int sel, const char *note, const char *info)
{
	int face = gfx_face, caps = gfx_caps;
	gfx_face = 1; gfx_caps = 1;
	gfx_fill(0, 0, W, H, GM_PAPER);
	gfx_text(12, 10, "Juego en pausa", GM_GREY, 1);
	gfx_fill(10, 38, W - 20, 1, GM_RULE);
	for (int i = 0; i < n; i++) {
		int y = 46 + i * 34;
		if (i == sel) game_menu_row(4, y, W - 8, 32, GM_INK);
		gfx_text(14, y + 6, items[i], i == sel ? GM_PAPER : GM_INK, 1);
	}
	if (note) gfx_text_center(W / 2, 262, note, GM_INK, 1);
	if (info) gfx_text_center(W / 2, 292, info, GM_GREY, 1);
	gfx_flip();
	gfx_face = face; gfx_caps = caps;
}
#endif
