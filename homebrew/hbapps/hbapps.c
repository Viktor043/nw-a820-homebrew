/* hbapps.c: the app runtime. Runs one Lua app full screen: H:\Apps\<Name>.lua or H:\Apps\<Name>\main.lua.
 * hbplayer starts it from the Juegos list like a game: hbplayer keeps the music going, feeds the watchdog and kills us
 * if BACK is held 6 s; we draw with the player's own gfx / screens code (Sony font, title bar, lists) and read the keys
 * from /dev/input/event0 ourselves. BACK held 2 s quits the app, as in the games.
 *
 * Apps get one table, `hb` (see docs/apps.md), plus the safe parts of Lua: string, table, math, utf8, coroutine and a
 * restricted os (time, date, clock, difftime). No io, no package, no debug, no dofile / loadfile. Reads are limited to
 * the content drive (H:\), writes to the app's own data folder H:\Apps\.data\<Name>\, and the Lua heap to MEM_CAP so
 * a runaway app gets "not enough memory" instead of taking the Walkman down.
 *
 * usage: hbapps APP.lua      env: HB_LANG es|en
 * PC build (-DHB_PC, see build_sim.sh) = the simulator: HB_KEYS="down down play back" is the key script ("none" =
 * a key timeout; the end of the script quits), HB_SHOTS=dir gets one RGB565 frame per flip, HB_CONTENTS replaces
 * /contents (default "."), HB_RES is Sony's res folder if you have it (fallback font otherwise). */
#include "../common/gfx.h"
#include "../common/keys.h"
#include "../hbplayer/screens.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int ui_big_rows, ui_album_grid;	/* screens.c reads these; the player defines them in hbplayer.c */
int dl_active(void) { return 0; }	/* the bottom bar's podcast-download indicator: downloads belong to the player */
int dl_percent(void) { return -1; }

#define MEM_CAP (6 << 20)	/* Lua heap: ~14 MB are free without Sony's UI, the player and its buffers take part */
#define READ_CAP (2 << 20)	/* biggest file hb.read returns */
#define MAXLIST 2000		/* rows of hb.list */
#define RESDIR_DEV "/usr/local/share/gemini/res"

static char root[400] = "/contents";	/* the content drive (H:\) */
static char datadir[600];		/* <root>/Apps/.data/<app> */
static char appdir[400];		/* the app's folder relative to root: "Apps" or "Apps/Reader" */
static char appname[96];
static int lang_en;
static int quit_req;			/* BACK held 2 s (or the end of the simulator's key script) */

static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

/* ---------- memory: a capped allocator ---------- */

static size_t mem_used;
static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	(void)ud;
	if (!ptr) osize = 0;	/* Lua passes the object type in osize when ptr is NULL */
	if (nsize == 0) { free(ptr); mem_used -= osize; return 0; }
	if (nsize > osize && mem_used + (nsize - osize) > MEM_CAP) return 0;
	void *p = realloc(ptr, nsize);
	if (p) mem_used = mem_used - osize + nsize;
	return p;
}

/* ---------- text: app strings are drawn as given (no Spanish -> English table) ---------- */

#define RAW(stmt) do { const char *(*t_)(const char *) = gfx_tr; gfx_tr = 0; stmt; gfx_tr = t_; } while (0)
static const char *T(const char *es, const char *en) { return lang_en ? en : es; }

/* ---------- keys ---------- */

#define QN 32
static int q[QN], qh, qt;
static void push_key(int k) { if ((qt + 1) % QN != qh) { q[qt] = k; qt = (qt + 1) % QN; } }
static int pop_key(void) { if (qh == qt) return -1; int k = q[qh]; qh = (qh + 1) % QN; return k; }

static const char *key_name(int c)
{
	switch (c) {
	case K_UP: return "up"; case K_DOWN: return "down"; case K_LEFT: return "left"; case K_RIGHT: return "right";
	case K_PLAY: return "play"; case K_BACK: return "back"; case K_OPTION: return "option"; case K_BT: return "bt";
	case K_VOLUP: return "vol+"; case K_VOLDN: return "vol-";
	}
	return 0;
}

#ifndef HB_PC
static int keyfd = -1;
struct kev { long sec, usec; unsigned short type, code; int value; };	/* struct input_event on the device */
static double back_t, rep_t0, rep_last;
static int rep_code = -1;

/* read what is there: presses go to the queue, the d-pad repeats when held, BACK is sent on release (< 2 s) and quits
 * when held 2 s. Values are inverted on this Walkman: 0 = pressed, 1 = released; HOLD adds 0x80 to the codes. */
static void poll_input(void)
{
	struct kev e;
	while (keyfd >= 0 && read(keyfd, &e, sizeof e) == sizeof e) {
		if (e.type != 1 || (e.value != 0 && e.value != 1)) continue;
		int c = e.code & 0x7f, pressed = KEY_PRESSED(e.value);
		if (c == K_HOLD_B) continue;	/* the HOLD switch itself (143 / 15) */
		if (c == K_BACK) {
			if (pressed) back_t = now();
			else if (back_t) { back_t = 0; push_key(K_BACK); }
			continue;
		}
		if (pressed) {
			push_key(c);
			if (c >= K_RIGHT && c <= K_DOWN) { rep_code = c; rep_t0 = rep_last = now(); }
		} else if (c == rep_code) rep_code = -1;
	}
	double t = now();
	if (back_t && t - back_t >= 2.0) { back_t = 0; quit_req = 1; }
	if (rep_code >= 0 && t - rep_t0 > 0.45 && t - rep_last > 0.09) { rep_last = t; push_key(rep_code); }
}
#else
static const char *script;	/* HB_KEYS */
static int poll_input(void) { return 0; }
/* the next key of the script: a key code, -1 = "none" (a timeout), -2 = end of the script */
static int sim_next(void)
{
	static const char *names[] = { "up", "down", "left", "right", "play", "back", "option", "bt", "vol+", "vol-", 0 };
	static const int codes[] = { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PLAY, K_BACK, K_OPTION, K_BT, K_VOLUP, K_VOLDN };
	while (script && *script == ' ') script++;
	if (!script || !*script) return -2;
	const char *e = script; while (*e && *e != ' ') e++;
	int n = (int)(e - script), k = -1;
	for (int i = 0; names[i]; i++) if ((int)strlen(names[i]) == n && !strncmp(script, names[i], n)) k = codes[i];
	script = e;
	return k;
}
#endif

static int quit_mark;	/* its address is the error object that unwinds the app on quit */
static void raise_quit(lua_State *L) { lua_pushlightuserdata(L, &quit_mark); lua_error(L); }
static void check_quit(lua_State *L) { poll_input(); if (quit_req) raise_quit(L); }

/* every 20000 VM instructions: an app stuck in a loop still quits with BACK held 2 s */
static void hook(lua_State *L, lua_Debug *ar) { (void)ar; check_quit(L); }

/* wait for a key: its code, or -1 after timeout_ms (< 0: wait for ever) */
static int wait_key(lua_State *L, int timeout_ms)
{
#ifdef HB_PC
	(void)timeout_ms;
	int k = sim_next();
	if (k == -2) { quit_req = 1; raise_quit(L); }
	return k;
#else
	double end = now() + timeout_ms / 1000.0;
	for (;;) {
		check_quit(L);
		int k = pop_key();
		if (k >= 0) return k;
		if (timeout_ms >= 0 && now() >= end) return -1;
		usleep(10000);
	}
#endif
}

/* ---------- screen ---------- */

#ifdef HB_PC
static const char *shots;
static void sim_shot(void)
{
	static int n;
	if (!shots) return;
	char fn[600]; snprintf(fn, sizeof fn, "%s/shot_%03d.rgb565", shots, n++);
	FILE *f = fopen(fn, "wb");
	if (f) { fwrite(fb, 2, W * H, f); fclose(f); }
}
#endif
static void flip(void)
{
	gfx_flip();
#ifdef HB_PC
	sim_shot();
#endif
}

/* ---------- paths: reads anywhere on H:\, writes only in the app's data folder ---------- */

static int rel_ok(const char *rel)
{
	if (!rel || rel[0] == '/' || strchr(rel, '\\')) return 0;
	for (const char *p = rel; *p; ) {
		const char *s = strchr(p, '/');
		size_t n = s ? (size_t)(s - p) : strlen(p);
		if (n == 2 && p[0] == '.' && p[1] == '.') return 0;
		p += n; if (*p == '/') p++;
	}
	return 1;
}
static const char *full_path(lua_State *L, int arg, char *out, size_t max)
{
	const char *rel = luaL_optstring(L, arg, "");
	if (!rel_ok(rel)) luaL_error(L, "bad path '%s' (relative to H:\\, no '..')", rel);
	if (*rel) snprintf(out, max, "%s/%s", root, rel); else snprintf(out, max, "%s", root);
	return rel;
}
static const char *data_path(lua_State *L, int arg, char *out, size_t max)
{
	const char *k = luaL_checkstring(L, arg);
	size_t n = strlen(k);
	int ok = n > 0 && n < 64 && k[0] != '.';
	for (size_t i = 0; ok && i < n; i++) ok = isalnum((unsigned char)k[i]) || k[i] == '_' || k[i] == '-' || k[i] == '.';
	if (!ok) luaL_error(L, "bad data name '%s' (letters, digits, _ - .)", k);
	snprintf(out, max, "%s/%s", datadir, k);
	return k;
}
static void mkdirs(const char *path)
{
	char p[700]; snprintf(p, sizeof p, "%s", path);
	for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}
static int read_file(lua_State *L, const char *fn)
{
	FILE *f = fopen(fn, "rb");
	if (!f) { lua_pushnil(L); lua_pushstring(L, strerror(errno)); return 2; }
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n < 0 || n > READ_CAP) { fclose(f); lua_pushnil(L); lua_pushstring(L, "file too big"); return 2; }
	luaL_Buffer b;
	char *p = luaL_buffinitsize(L, &b, (size_t)n);
	size_t got = fread(p, 1, (size_t)n, f);
	fclose(f);
	luaL_pushresultsize(&b, got);
	return 1;
}

/* ---------- the hb table ---------- */

static int l_rgb(lua_State *L)
{
	int r = (int)luaL_checkinteger(L, 1), g = (int)luaL_checkinteger(L, 2), b = (int)luaL_checkinteger(L, 3);
	lua_pushinteger(L, RGB(r & 255, g & 255, b & 255));
	return 1;
}
static uint16_t color(lua_State *L, int arg, uint16_t def) { return (uint16_t)luaL_optinteger(L, arg, def); }
static int l_clear(lua_State *L) { gfx_fill(0, 0, W, H, color(L, 1, C_BLACK)); return 0; }
static int l_fill(lua_State *L)
{
	gfx_fill((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4), color(L, 5, C_WHITE));
	return 0;
}
static int l_frame(lua_State *L)
{
	gfx_frame((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4), color(L, 5, C_WHITE));
	return 0;
}
static int l_text(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2), r;
	const char *s = luaL_checkstring(L, 3);
	uint16_t c = color(L, 4, C_WHITE);
	int scale = (int)luaL_optinteger(L, 5, 1);
	RAW(r = gfx_text(x, y, s, c, scale < 1 ? 1 : scale > 4 ? 4 : scale));
	lua_pushinteger(L, r);
	return 1;
}
static int l_textw(lua_State *L)
{
	const char *s = luaL_checkstring(L, 1);
	int scale = (int)luaL_optinteger(L, 2, 1), r;
	RAW(r = gfx_text_w(s, scale < 1 ? 1 : scale > 4 ? 4 : scale));
	lua_pushinteger(L, r);
	return 1;
}
static int l_center(lua_State *L)
{
	int cx = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
	const char *s = luaL_checkstring(L, 3);
	uint16_t c = color(L, 4, C_WHITE);
	int scale = (int)luaL_optinteger(L, 5, 1);
	RAW(gfx_text_center(cx, y, s, c, scale < 1 ? 1 : scale > 4 ? 4 : scale));
	return 0;
}
static int l_wrap(lua_State *L)
{
	int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2), r;
	const char *s = luaL_checkstring(L, 3);
	uint16_t c = color(L, 4, C_WHITE);
	int maxw = (int)luaL_optinteger(L, 5, W - x - 8), lines = (int)luaL_optinteger(L, 6, 10), lh = (int)luaL_optinteger(L, 7, LINE_H);
	RAW(r = gfx_text_wrap(x, y, s, c, maxw, lines, lh));
	lua_pushinteger(L, r);
	return 1;
}
static int measure(const char *s, size_t len)
{
	char buf[512]; int w;
	if (len >= sizeof buf) len = sizeof buf - 1;
	memcpy(buf, s, len); buf[len] = 0;
	RAW(w = gfx_text_w(buf, 1));
	return w;
}
/* hb.fit(s, i, maxw) -> j, next: s:sub(i, j) is the longest line from byte i that fits in maxw pixels, broken at a
 * space (or inside a word too long for a line, at a UTF-8 boundary); next is where the following line starts.
 * A "\n" always ends the line. The building block of a text reader: it never measures more than one line. */
static int l_fit(lua_State *L)
{
	size_t n; const char *s = luaL_checklstring(L, 1, &n);
	lua_Integer i1 = luaL_optinteger(L, 2, 1);
	int maxw = (int)luaL_optinteger(L, 3, W - 16);
	size_t i = i1 < 1 ? 0 : (size_t)i1 - 1;
	if (i >= n) { lua_pushinteger(L, (lua_Integer)n); lua_pushinteger(L, (lua_Integer)n + 1); return 2; }
	size_t best = i, j = i, end, next;
	while (j < n && s[j] != '\n') {
		size_t k = j;
		while (k < n && s[k] == ' ') k++;
		while (k < n && s[k] != ' ' && s[k] != '\n') k++;
		if (measure(s + i, k - i) > maxw) break;
		best = j = k;
	}
	if (best > i || (j < n && s[j] == '\n')) {
		end = best; next = best;
		if (next < n && s[next] == '\n') next++;
		else { while (next < n && s[next] == ' ') next++; }
	} else {	/* one word wider than the line: cut it */
		size_t k = i, last = i;
		while (k < n && s[k] != '\n') {
			size_t c = k + 1;
			while (c < n && (s[c] & 0xC0) == 0x80) c++;
			if (measure(s + i, c - i) > maxw) break;
			last = k = c;
		}
		if (last == i) { last = i + 1; while (last < n && (s[last] & 0xC0) == 0x80) last++; }
		end = next = last;
	}
	lua_pushinteger(L, (lua_Integer)end);	/* 0-based exclusive end = 1-based inclusive end */
	lua_pushinteger(L, (lua_Integer)next + 1);
	return 2;
}
static int l_screen(lua_State *L) { const char *t = luaL_optstring(L, 1, ""); RAW(scr_screen(t)); return 0; }
static int l_flip(lua_State *L) { (void)L; flip(); return 0; }
static int l_message(lua_State *L)
{
	const char *t = luaL_checkstring(L, 1), *a = luaL_optstring(L, 2, ""), *b = luaL_optstring(L, 3, "");
	RAW(scr_message(t, a, b));
	flip();
	return 0;
}
static int l_key(lua_State *L)
{
	int k = wait_key(L, (int)luaL_optinteger(L, 1, -1));
	const char *nm = k >= 0 ? key_name(k) : 0;
	if (nm) lua_pushstring(L, nm); else lua_pushnil(L);
	return 1;
}

/* hb.list(title, items [, sel [, values [, empty]]]) -> index or nil (BACK): a Sony-style list, like the player's */
static const char *ls_items[MAXLIST], *ls_vals[MAXLIST];
static const char *ls_label(int i) { return ls_items[i]; }
static const char *ls_value(int i) { return ls_vals[i]; }
static int l_list(lua_State *L)
{
	const char *title = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	int n = (int)luaL_len(L, 2), has_v = lua_istable(L, 4);
	if (n > MAXLIST) n = MAXLIST;
	/* the rows as strings, kept alive in a private table on the stack while the list runs (the app's tables are
	 * left as they are) */
	lua_settop(L, 5);
	lua_createtable(L, 2 * n, 0);
	int keep = lua_gettop(L);
	for (int i = 0; i < n; i++) {
		lua_geti(L, 2, i + 1);
		ls_items[i] = luaL_tolstring(L, -1, 0);
		lua_rawseti(L, keep, 2 * i + 1);
		lua_pop(L, 1);
		ls_vals[i] = 0;
		if (has_v) {
			lua_geti(L, 4, i + 1);
			if (!lua_isnil(L, -1)) { ls_vals[i] = luaL_tolstring(L, -1, 0); lua_rawseti(L, keep, 2 * i + 2); }
			lua_pop(L, 1);
		}
	}
	struct list_view v; memset(&v, 0, sizeof v);
	v.title = title; v.n = n; v.label = ls_label; v.value = has_v ? ls_value : 0; v.icon = LI_NONE; v.bar_y = -1;
	v.empty = luaL_optstring(L, 5, T("(vacío)", "(empty)"));
	v.sel = (int)luaL_optinteger(L, 3, 1) - 1;
	if (v.sel < 0 || v.sel >= n) v.sel = 0;
	for (;;) {
		if (n) {
			if (v.sel < v.top) v.top = v.sel;
			if (v.sel >= v.top + LIST_ROWS) v.top = v.sel - LIST_ROWS + 1;
		}
		RAW(scr_list(&v));
		flip();
		int k = wait_key(L, -1);
		if (k == K_BACK) { lua_pushnil(L); return 1; }
		if (!n) continue;
		if (k == K_UP) v.sel = (v.sel + n - 1) % n;
		else if (k == K_DOWN) v.sel = (v.sel + 1) % n;
		else if (k == K_LEFT) { v.sel -= LIST_ROWS; if (v.sel < 0) v.sel = 0; }
		else if (k == K_RIGHT) { v.sel += LIST_ROWS; if (v.sel >= n) v.sel = n - 1; }
		else if (k == K_PLAY) { lua_pushinteger(L, v.sel + 1); return 1; }
	}
}

static int l_read(lua_State *L) { char fn[700]; full_path(L, 1, fn, sizeof fn); return read_file(L, fn); }

static int cmp_names(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }
/* hb.dir(path) -> sorted names; folders end with "/". Hidden names (".x") are skipped. */
static int l_dir(lua_State *L)
{
	char fn[700]; full_path(L, 1, fn, sizeof fn);
	DIR *d = opendir(fn);
	if (!d) { lua_pushnil(L); lua_pushstring(L, strerror(errno)); return 2; }
	char *names[1000]; int n = 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < 1000) {
		if (e->d_name[0] == '.') continue;
		char p[1000]; snprintf(p, sizeof p, "%s/%s", fn, e->d_name);
		struct stat st; int isdir = !stat(p, &st) && S_ISDIR(st.st_mode);
		size_t len = strlen(e->d_name);
		names[n] = malloc(len + 2);
		if (!names[n]) break;
		memcpy(names[n], e->d_name, len); names[n][len] = isdir ? '/' : 0; names[n][len + 1] = 0;
		n++;
	}
	closedir(d);
	qsort(names, n, sizeof names[0], cmp_names);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) { lua_pushstring(L, names[i]); lua_rawseti(L, -2, i + 1); free(names[i]); }
	return 1;
}
static int l_load(lua_State *L)
{
	char fn[700]; data_path(L, 1, fn, sizeof fn);
	int r = read_file(L, fn);
	if (r == 2) { lua_pop(L, 1); return 1; }	/* nothing saved yet: just nil */
	return r;
}
static int l_save(lua_State *L)
{
	char fn[700], tmp[720]; data_path(L, 1, fn, sizeof fn);
	size_t n; const char *s = luaL_checklstring(L, 2, &n);
	snprintf(tmp, sizeof tmp, "%s.tmp", fn);
	FILE *f = fopen(tmp, "wb");
	int ok = f && fwrite(s, 1, n, f) == n;
	if (f && fclose(f)) ok = 0;
	if (ok) { remove(fn); ok = !rename(tmp, fn); }	/* FAT: rename does not replace */
	if (!ok) { remove(tmp); lua_pushnil(L); lua_pushstring(L, strerror(errno)); return 2; }
	lua_pushboolean(L, 1);
	return 1;
}
static int l_time(lua_State *L) { lua_pushinteger(L, (lua_Integer)time(0)); return 1; }
static int l_clock(lua_State *L) { lua_pushnumber(L, now()); return 1; }
static int l_sleep(lua_State *L)
{
	int ms = (int)luaL_checkinteger(L, 1);
#ifndef HB_PC
	double end = now() + ms / 1000.0;
	while (now() < end) { check_quit(L); usleep(10000); }
#else
	(void)ms;
#endif
	return 0;
}
static int l_backlight(lua_State *L) { gfx_backlight((int)luaL_checkinteger(L, 1)); return 0; }
static int l_mem(lua_State *L) { lua_pushinteger(L, (lua_Integer)mem_used); lua_pushinteger(L, MEM_CAP); return 2; }

/* require "name": name.lua from the app's own folder (multi-file apps); loaded once */
static int l_require(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	for (const char *p = name; *p; p++) if (!isalnum((unsigned char)*p) && *p != '_') luaL_error(L, "bad module name '%s'", name);
	lua_getfield(L, LUA_REGISTRYINDEX, "hb_loaded");
	if (lua_getfield(L, -1, name) != LUA_TNIL) return 1;
	lua_pop(L, 1);
	char fn[900]; snprintf(fn, sizeof fn, "%s/%s/%s.lua", root, appdir, name);
	if (read_file(L, fn) != 1) luaL_error(L, "module '%s' not found next to the app", name);
	size_t n; const char *src = lua_tolstring(L, -1, &n);
	char chunk[160]; snprintf(chunk, sizeof chunk, "@%s.lua", name);
	if (luaL_loadbuffer(L, src, n, chunk)) lua_error(L);
	lua_call(L, 0, 1);
	if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
	lua_pushvalue(L, -1); lua_setfield(L, -4, name);
	return 1;
}

static const luaL_Reg hb_fns[] = {
	{ "rgb", l_rgb }, { "clear", l_clear }, { "fill", l_fill }, { "frame", l_frame }, { "text", l_text },
	{ "textw", l_textw }, { "center", l_center }, { "wrap", l_wrap }, { "fit", l_fit }, { "screen", l_screen },
	{ "flip", l_flip }, { "message", l_message }, { "key", l_key }, { "list", l_list }, { "read", l_read },
	{ "dir", l_dir }, { "load", l_load }, { "save", l_save }, { "time", l_time }, { "clock", l_clock },
	{ "sleep", l_sleep }, { "backlight", l_backlight }, { "mem", l_mem }, { 0, 0 }
};

static void set_int(lua_State *L, const char *k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); }
static void set_str(lua_State *L, const char *k, const char *v) { lua_pushstring(L, v); lua_setfield(L, -2, k); }

static void open_libs(lua_State *L)
{
	static const luaL_Reg libs[] = {
		{ LUA_GNAME, luaopen_base }, { LUA_COLIBNAME, luaopen_coroutine }, { LUA_TABLIBNAME, luaopen_table },
		{ LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 },
		{ LUA_OSLIBNAME, luaopen_os }, { 0, 0 }
	};
	for (const luaL_Reg *l = libs; l->func; l++) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
	/* no files outside the sandbox, no processes, no environment */
	static const char *base_off[] = { "dofile", "loadfile", 0 }, *os_off[] = { "execute", "exit", "getenv", "remove", "rename", "tmpname", "setlocale", 0 };
	for (int i = 0; base_off[i]; i++) { lua_pushnil(L); lua_setglobal(L, base_off[i]); }
	lua_getglobal(L, "os");
	for (int i = 0; os_off[i]; i++) { lua_pushnil(L); lua_setfield(L, -2, os_off[i]); }
	lua_pop(L, 1);
	lua_newtable(L); lua_setfield(L, LUA_REGISTRYINDEX, "hb_loaded");
	lua_register(L, "require", l_require);

	luaL_newlib(L, hb_fns);
	set_int(L, "W", W); set_int(L, "H", H); set_int(L, "TOP", TITLE_H); set_int(L, "BOTTOM", BOTTOM_Y); set_int(L, "LINE", LINE_H);
	set_int(L, "BLACK", C_BLACK); set_int(L, "WHITE", C_WHITE); set_int(L, "GREY", C_GREY); set_int(L, "DARK", C_DARK);
	set_int(L, "ORANGE", C_ORANGE); set_int(L, "BLUE", C_BLUE); set_int(L, "RED", C_RED); set_int(L, "GREEN", C_GREEN);
	set_str(L, "lang", lang_en ? "en" : "es"); set_str(L, "app", appname); set_str(L, "appdir", appdir);
	set_str(L, "version", "1");
	lua_setglobal(L, "hb");
}

static int traceback(lua_State *L)
{
	if (lua_touserdata(L, 1) == &quit_mark) return 1;
	const char *m = lua_tostring(L, 1);
	if (!m) m = luaL_tolstring(L, 1, 0);
	luaL_traceback(L, L, m, 1);
	return 1;
}

/* an error: on screen (the first lines matter: file:line: message) and in the log; any key or 60 s closes it */
static void show_error(lua_State *L, const char *msg)
{
	fprintf(stderr, "hbapps: %s: %s\n", appname, msg);
	char first[600]; snprintf(first, sizeof first, "%s", msg);
	char *tb = strstr(first, "\nstack traceback:"); if (tb) *tb = 0;
	RAW(scr_screen(T("Error en la app", "App error")));
	RAW(gfx_text_clip(12, 40, appname, C_ORANGE, W - 24));
	RAW(gfx_text_wrap(12, 70, first, C_WHITE, W - 24, 9, 22));
	RAW(gfx_text_wrap(12, 262, T("Cualquier tecla: salir", "Any key: quit"), C_GREY, W - 24, 1, 22));
	flip();
	quit_req = 0;
#ifndef HB_PC
	double t0 = now();
	while (now() - t0 < 60) { poll_input(); if (quit_req || pop_key() >= 0) break; usleep(20000); }
#endif
	(void)L;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: hbapps APP.lua\n"); return 2; }
	const char *lg = getenv("HB_LANG");
	lang_en = lg && !strcmp(lg, "en");
	const char *resdir = RESDIR_DEV;
#ifdef HB_PC
	if (getenv("HB_CONTENTS")) snprintf(root, sizeof root, "%s", getenv("HB_CONTENTS")); else snprintf(root, sizeof root, ".");
	script = getenv("HB_KEYS"); shots = getenv("HB_SHOTS");
	resdir = getenv("HB_RES") ? getenv("HB_RES") : "res";
#else
	keyfd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
#endif
	if (gfx_open()) { fprintf(stderr, "hbapps: no frame buffer\n"); return 3; }
	gfx_load_res(resdir);

	/* names: /contents/Apps/Clock.lua -> app "Clock", folder "Apps"; /contents/Apps/Reader/main.lua -> "Reader" */
	const char *path = argv[1];
	size_t rl = strlen(root);
	const char *rel = !strncmp(path, root, rl) && path[rl] == '/' ? path + rl + 1 : path;
	snprintf(appdir, sizeof appdir, "%s", rel);
	char *slash = strrchr(appdir, '/');
	const char *file = slash ? slash + 1 : appdir;
	if (!strcasecmp(file, "main.lua") && slash) {
		*slash = 0;
		const char *s2 = strrchr(appdir, '/');
		snprintf(appname, sizeof appname, "%s", s2 ? s2 + 1 : appdir);
	} else {
		snprintf(appname, sizeof appname, "%s", file);
		char *dot = strrchr(appname, '.'); if (dot && dot > appname) *dot = 0;
		if (slash) *slash = 0; else appdir[0] = 0;
	}
	snprintf(datadir, sizeof datadir, "%s/Apps/.data/%s", root, appname);
	mkdirs(datadir);

	lua_State *L = lua_newstate(l_alloc, 0);
	if (!L) return 4;
	open_libs(L);
	lua_sethook(L, hook, LUA_MASKCOUNT, 20000);
	int rc = 0;
	lua_pushcfunction(L, traceback);
	if (read_file(L, path) != 1) {
		char m[300]; snprintf(m, sizeof m, "%s: %s", path, lua_tostring(L, -1));
		show_error(L, m); rc = 1;
	} else {
		size_t n; const char *src = lua_tolstring(L, -1, &n);
		char chunk[160]; snprintf(chunk, sizeof chunk, "@%s", appname);
		if (luaL_loadbuffer(L, src, n, chunk)) { show_error(L, lua_tostring(L, -1)); rc = 1; }
		else if (lua_pcall(L, 0, 0, -3)) {
			if (lua_touserdata(L, -1) != &quit_mark) { show_error(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "error"); rc = 1; }
		}
	}
	lua_close(L);
	return rc;
}
