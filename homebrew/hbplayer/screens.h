/* screens.h: pure drawing of every screen of the homebrew player, in the style of the Sony NW-A820 UI
 * (silver title bar on top, dark blue-grey background, status bar with play state / now playing / battery at the bottom).
 * No input, no I/O: the same functions render on the Walkman and in the PC screenshot tool (shots.c). */
#ifndef SCREENS_H
#define SCREENS_H
#include "../common/gfx.h"

/* look (Ajustes > Pantalla > Estilo): 0 Sony, 1 SNRGY plata, 2 SNRGY negro. The four theme colours are variables:
 * "white" = text, "black" = text on the selection, grey = secondary, "orange" = accent */
#define THEME_COUNT 3
extern int ui_theme;
extern const char *const theme_names[THEME_COUNT];
extern uint16_t ui_pal[4];
void ui_theme_set(int t);	/* palette, font, layout rotation; the next frame is drawn whole */
#define C_WHITE ui_pal[0]
#define C_BLACK ui_pal[1]
#define C_GREY ui_pal[2]
#define C_DARK RGB(60, 60, 64)
#define C_ORANGE ui_pal[3]
#define C_BLUE RGB(80, 140, 255)
#define C_RED RGB(255, 90, 90)
#define C_GREEN RGB(120, 230, 120)

/* layout: title bar [0, TITLE_H), content, bottom status bar [BOTTOM_Y, H) */
#define TITLE_H 28
#define BOTTOM_Y (ui_theme ? 264 : 288)	/* SNRGY: the 3-row status strip is 56 px (gfx_rot) */

/* playing: 0 idle, 1 playing, 2 paused. np_title / np_artist: what the bottom bar shows (0 = nothing) */
struct ui_status { int batt, charging, bt, hold, playing; const char *np_title, *np_artist; int np_idx, np_n, np_sec, np_total; };
extern struct ui_status ui_st;

enum { LI_NONE, LI_FOLDER, LI_TRACK, LI_GAME, LI_BT, LI_SETTING };

struct list_view {
	const char *title;
	int n, sel, top;
	const char *(*label)(int);
	const char *(*value)(int);	/* optional right-aligned value (orange) */
	int (*mark)(int);		/* optional "resume here" marker */
	int icon;			/* LI_* */
	const char *empty;		/* text when n == 0 */
	int scroll;			/* marquee offset in pixels for the selected row */
	struct img (*thumb)(int);	/* optional 28x28 thumbnail per row (w = 0: none yet) */
	int bar_y;			/* -1 normally; during the selection slide: pixel y of the bar */
	int (*option)(int);		/* optional OPTION-key action on the selected row; returning 1 ends list_run with -2 (reload) */
	int big;			/* big rows: 4 of 64 px with a 60 px cover and the value as a second line (Ajustes > Pantalla) */
	int grid;			/* mosaic: 3x3 covers of 72 px; the selected name goes up to the title bar, the value under the grid */
	int ep;				/* podcast episodes: 5 rows of 51 px, 46 px cover, value as a second line, state + progress */
	int (*state)(int);		/* ep rows: 0 normal, 1 new (orange dot on the cover), 2 already heard (dimmed) */
	int (*progress)(int);		/* ep rows: % heard of an episode started, -1 = none */
};
#define LIST_ROWS (ui_theme ? 8 : 9)
#define LIST_Y0 32		/* y of the first row */
#define LIST_ROW_H 28		/* row pitch (= height of the selection bar) */
#define BIG_ROWS (ui_theme ? 3 : 4)
#define BIG_ROW_H 64
#define BIG_THUMB 60
#define GRID_TH 72		/* mosaic cover size, 3 per row, 3 rows */
#define GRID_N 9
#define EP_ROWS (ui_theme ? 4 : 5)
#define EP_ROW_H 51
#define EP_THUMB 46
static inline int lv_rows(const struct list_view *v) { return v->grid ? GRID_N : v->ep ? EP_ROWS : v->big ? BIG_ROWS : LIST_ROWS; }
static inline int lv_rowh(const struct list_view *v) { return v->ep ? EP_ROW_H : v->big ? BIG_ROW_H : LIST_ROW_H; }
extern int ui_big_rows;		/* setting: lists with covers use big rows */
#define WALL_COUNT 6
extern int ui_wall;
extern const char *const wall_names[WALL_COUNT];
void scr_wall_changed(void);
extern int ui_album_grid;	/* setting: album lists as a mosaic of covers (OPTION in the album list) */
extern int ui_pod_new;		/* home: episodes found by the last search and still to download (badge on Podcasts) */

#define PLAYER_ART 112		/* cover size (px) the player screen shows */
struct player_view {
	const char *book, *track, *sub;
	int idx, ntr, sec, total, paused;
	int vol, volmax, sleep_left;	/* sleep_left in minutes, 0 = off */
	const char *out;		/* "Auriculares" / "Bluetooth" */
	struct img art;			/* cover, PLAYER_ART square (w = 0 -> placeholder) */
	int speed;			/* x100; shown when != 100 */
	int show_lrc;			/* lyrics view instead of the cover */
	const char *lrc_prev, *lrc_cur, *lrc_next;
	const char *genre;		/* music: genre line (0/"" = none) */
	const char *chap;		/* podcast chapters line ("2/5 · Title"), 0 = none */
	const char *year;		/* music: the album's year, on the line under the genre (0/"" = none) */
	int meta;			/* podcasts / audiobooks: show, publication date, chapter or length (like the music lines) */
	const char *date;		/* publication date ("25 jun 2026", "" = unknown) */
	int shuffle, repeat;		/* SNRGY: the play mode shown under the cover (eng_shuffle, eng_repeat) */
	int browsing, cursor;		/* SNRGY: the queue under the cover is being browsed; the highlighted row */
	const char *(*qtitle)(int);	/* SNRGY: title of queue position i (0..ntr-1) */
};

struct focus_view { int left, total, paused; const char *l1, *l2; };

void scr_status(const char *title);	/* title bar + bottom status bar (content area untouched) */
void scr_screen(const char *title);	/* background + both bars: an empty screen to draw on */
#define HOME_SLOTS 9
extern const char *const home_labels[HOME_SLOTS];	/* "" = empty slot */
/* cell rectangle of slot i (80x72): the zoom animation redraws + flips just these */
#define HOME_CELL_X(i) (((i) % 3) * 80)
#define HOME_CELL_Y(i) (TITLE_H + ((i) / 3) * 72)
#define HOME_CELL_W 80
#define HOME_CELL_H 72
void scr_warm(void (*touch)(const void *p, long n));	/* every texture the screens use (warm.c) */
void scr_home(int sel, const int *frame, const char *nowline);	/* frame[i]: 0 fully selected .. 8 normal */
void scr_home_icon(int i, int frame);	/* redraw one cell (animation) */
void scr_toast(const char *msg);
void scr_list(const struct list_view *v);
int scr_list_overflow(const struct list_view *v);	/* pixels the selected label exceeds its row by (0 = fits) */
void scr_sony_eq(const char *const *names, int n, int sel, int top, int active, int cb, const int *bands, int edit, int grey);	/* Sony EQ screen */
void scr_player(const struct player_view *v, int full);
void scr_strip(void);		/* SNRGY: redraw the status strip (clock, progress) in the back buffer's status band */
extern int ui_home_tab;		/* SNRGY home: 0 the list has the focus, 1 the HOME tab, 2 the RESUME tab */
extern const int home_order[];	/* SNRGY home: the slots in list order (no "Reproduciendo": the RESUME tab is that) */
extern const int home_order_n;
#define PLAYER_ART_BIG 236	/* SNRGY player: the cover across the width */
void scr_volume(int vol, int volmax);
void scr_hold(void);
void scr_message(const char *title, const char *l1, const char *l2);
void scr_progress(const char *title, int step, int nsteps, const char *text, int secs, int spin, const char *hint);
void scr_splash(const char *line);
void scr_poweroff(void);
void scr_focus(const struct focus_view *v);
void scr_ask(void);

/* podcast downloads (podsync.c): one row per episode; st 0 queued, 1 downloading, 2 done, 3 failed, 4 cancelled */
struct dl_row { const char *title, *feed; long got, total; int st; struct img cover; };	/* cover: the feed's, 40 px (w 0 = none) */
int scr_downloads(const char *header, const char *sub, int active, int reading, const struct dl_row *rows, int n, int top, double rate);	/* returns the top actually used (clamped) */
#define DL_ROWS 3	/* episodes visible at once */

/* on-screen keyboard (kbd.c runs it): 4 rows of 7 characters, then [ABC][123][espacio] and [Borrar][OK]. mode 0
 * letters, 1 digits / accents / symbols; upper: capitals. A key is (row, index in the row). */
#define KBD_COLS 7
#define KBD_ROWS 6
enum { KB_CHAR, KB_SHIFT, KB_MODE, KB_SPACE, KB_DEL, KB_OK };
struct kbd_key { int kind, col, span; const char *ch; };
int kbd_row(int mode, int upper, int row, struct kbd_key *keys);	/* the keys of a row: how many */
void scr_keyboard(const char *title, const char *text, const char *placeholder, const char *ok_label, int mode, int upper, int row, int key);

/* a podcast before subscribing (search result) or one of ours: cover, name, author, a line of facts, actions */
#define POD_ART 88
void scr_pod_show(const char *name, const char *author, const char *info, struct img cover, const char *const *items, int n, int sel);
#endif
