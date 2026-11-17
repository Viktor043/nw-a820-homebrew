/* game_i18n.h: the emulators' pause menu look (the texts are written in English). Include once, then call
 * game_i18n_init() at start. */
#ifndef GAME_I18N_H
#define GAME_I18N_H
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void game_i18n_init(void)
{
	gfx_tr = 0;	/* the menus are written in English (2026-11-17) */
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
	gfx_text(12, 10, "Game paused", GM_GREY, 1);
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
