/* settings.c: the "Settings" screen (Sound, alarm, brightness, screen timeout) and the unified Sound menu. */
#include "hb.h"
#include <stdio.h>
#include <sys/statvfs.h>
#include <string.h>
#include <unistd.h>

static char items[16][48], values[16][24];
static const char *it_label(int i) { return items[i]; }
static const char *it_value(int i) { return values[i]; }
static const char *band_names[5] = { "60 Hz", "250 Hz", "1 kHz", "4 kHz", "12 kHz" };
static const char *bass_names[3] = { "off", "+3 dB", "+6 dB" };

static int preset_index(void)
{
	for (int p = 0; p < eq_npresets; p++) if (!memcmp(eq_presets[p].band, eq.band, sizeof eq.band)) return p;
	return -1;
}

static void changed(void)
{
	eq_apply(44100);	/* recomputed for the real rate on the next buffer */
	eq_save(statedir);
}

/* ---------- Sound: one menu for everything audio (Settings > Sound and OPTION from any list / the player) ----------
 *   Ahora: ...              which path the audio takes (Max battery or processed); PLAY explains why
 *   Equalizer          >  on/off, preset, 5 bands, bass boost, stereo widening
 *   Headphones          >  crossfeed, loudness at low volume, AutoEq profile
 *   Voz (podcasts y libros) >  speed, silence trimming, voice boost
 *   Even volume          ReplayGain off / track / album
 *   Sony effects      >  VPT, Sony EQ, normalizer, DSEE (on the DSP)
 *   Max battery          direct DSP playback
 *   Usar el DSP de Sony     DSP decoding at all */

/* Settings > Sound > Equalizer: Sony's own (the DSP's: works with Max battery), like the Sony UI:
 * preset, Clear Bass 0..+3, 400 Hz .. 16 kHz -3..+3 (2 dB a step). Touching a value with a preset on switches to
 * Personalizado starting from that preset. Our 5-band EQ is "Equalizer avanzado" (the ARM does it). */
static void eq_adv_menu(void);

/* the Sony-style EQ screen: UP/DOWN walk the presets (the panel previews each curve), PLAY chooses (heard at once);
 * PLAY on Personalizado enters editing: LEFT/RIGHT pick Clear Bass or a band (orange knob), UP/DOWN change it
 * (heard at once, starting from the preset in use), PLAY/BACK leave editing. Last row: our "EQ avanzado". */
static void eq_menu(void)
{
	static const char *names[7] = { "Effects off", "Heavy", "Pop", "Jazz", "Unique", "Custom", "Advanced EQ (processor)" };
	int n = 7, sel = sony_eq < 0 || sony_eq > 5 ? 0 : sony_eq, top = 0, edit = -1, redraw = 1;
	for (;;) {
		if (sel < top) top = sel;
		if (sel >= top + 5) top = sel - 4;
		if (redraw && backlight_on) {
			int cb = sony_cb, bd[5];
			for (int i = 0; i < 5; i++) bd[i] = sony_band[i];
			int show = edit >= 0 ? 5 : sel;	/* the curve of the highlighted preset */
			if (show >= 1 && show <= 4) sony_eq_preset_values(show, &cb, bd);
			if (show == 0 || show == 6) { cb = 0; for (int i = 0; i < 5; i++) bd[i] = 0; }
			scr_sony_eq(names, n, sel, top, sony_eq, cb, bd, edit, show == 0 || show == 6);
			gfx_flip();
			redraw = 0;
		}
		idle_tick(0);
		int k = getkey(1000);	/* getkey keeps the audio fed */
		if (k == K_REDRAW) { redraw = 1; continue; }
		if (k < 0) continue;
		redraw = 1;
		if (ui_volume_key(k)) continue;
		if (edit >= 0) {	/* editing Personalizado */
			if (k == K_LEFT) edit = edit > 0 ? edit - 1 : 5;
			else if (k == K_RIGHT) edit = edit < 5 ? edit + 1 : 0;
			else if (k == K_UP || k == K_DOWN) {
				int d = k == K_UP ? 1 : -1;
				if (edit == 0) { sony_cb += d; if (sony_cb < 0) sony_cb = 0; if (sony_cb > 3) sony_cb = 3; }
				else { int *b = &sony_band[edit - 1]; *b += d; if (*b < -3) *b = -3; if (*b > 3) *b = 3; }
				sony_eq = 5;
				dsp_fx_update();
			}
			else if (k == K_PLAY || k == K_BACK) { edit = -1; changed(); }
			continue;
		}
		if (k == K_UP) sel = sel > 0 ? sel - 1 : n - 1;
		else if (k == K_DOWN) sel = sel < n - 1 ? sel + 1 : 0;
		else if (k == K_BACK) return;
		else if (k == K_PLAY) {
			if (sel == 6) { eq_adv_menu(); continue; }
			if (sel == 5 && sony_eq >= 1 && sony_eq <= 4) sony_eq_to_custom();	/* edit starting from the preset in use */
			sony_eq = sel;
			dsp_fx_update();
			changed();
			if (sel == 5) edit = 0;
		}
	}
}

/* our own 5-band EQ, bass boost and stereo widening, done by the ARM (Max battery off while it's on) */
static void eq_adv_menu(void)
{
	struct list_view v = { "Advanced equalizer", 9, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		int p = preset_index();
		snprintf(items[0], 48, "Equalizer"); snprintf(values[0], 24, "%s", eq.on ? "on" : "off");
		snprintf(items[1], 48, "Preset"); snprintf(values[1], 24, "%s", p >= 0 ? eq_presets[p].name : "custom");
		for (int i = 0; i < 5; i++) { snprintf(items[2 + i], 48, "  %s", band_names[i]); snprintf(values[2 + i], 24, "%+d dB", eq.band[i]); }
		snprintf(items[7], 48, "Bass boost"); snprintf(values[7], 24, "%s", bass_names[eq.bass]);
		snprintf(items[8], 48, "Wide stereo"); snprintf(values[8], 24, "%s", eq.wide ? "on" : "off");
		int s = list_run(&v);
		if (s < 0) return;
		if (s == 0) eq.on = !eq.on;
		else if (s == 1) { int n = (p + 1) % eq_npresets; memcpy(eq.band, eq_presets[n].band, sizeof eq.band); eq.on = 1; }
		else if (s >= 2 && s <= 6) { int *b = &eq.band[s - 2]; *b = *b >= 6 ? -6 : *b + 1; eq.on = 1; }
		else if (s == 7) eq.bass = (eq.bass + 1) % 3;
		else if (s == 8) eq.wide = !eq.wide;
		changed();
	}
}

void voice_quick_menu(void);
static void voice_menu(void)
{
	struct list_view v = { "Voice and podcasts", 4, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		snprintf(items[0], 48, "Speed"); snprintf(values[0], 24, "%d.%02dx", eng_speed / 100, eng_speed % 100);
		snprintf(items[1], 48, "Trim silences"); snprintf(values[1], 24, "%s", voice_trim_on ? "on" : "off");
		snprintf(items[2], 48, "Voice boost"); snprintf(values[2], 24, "%s", voice_boost_on ? "on" : "off");
		snprintf(items[3], 48, "Time saved (this session)"); snprintf(values[3], 24, "%d min", (int)(voice_saved_sec() / 60));
		int s = list_run(&v);
		if (s < 0) return;
		if (s == 0) { static const int sp[] = { 100, 125, 150, 175, 200, 75 }; int i = 0; while (i < 6 && sp[i] != eng_speed) i++; eng_speed = sp[(i + 1) % 6]; settings_save(); }
		else if (s == 1) voice_trim_on = !voice_trim_on;
		else if (s == 2) voice_boost_on = !voice_boost_on;
		else continue;
		changed();
	}
}

static void headphones_menu(void)
{
	struct list_view v = { "Headphones", 3, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		snprintf(items[0], 48, "Crossfeed"); snprintf(values[0], 24, "%s", hp_xfeed ? "on" : "off");
		snprintf(items[1], 48, "Bass at low volume"); snprintf(values[1], 24, "%s", hp_loud ? "on" : "off");
		snprintf(items[2], 48, "Headphone profile"); snprintf(values[2], 24, "%s", hp_autoeq ? "on" : "off");
		int s = list_run(&v);
		if (s < 0) return;
		if (s == 0) hp_xfeed = !hp_xfeed;
		else if (s == 1) hp_loud = !hp_loud;
		else if (s == 2) {
			if (!hp_autoeq) autoeq_reload();	/* a profile copied since the last try */
			if (!hp_autoeq && !autoeq_load()) { message("Headphone profile", "Copy your headphones' AutoEq profile to H:\\HB\\auriculares.txt", "(ParametricEQ.txt from autoeq.app)"); msg_wait(5); continue; }
			hp_autoeq = !hp_autoeq;
		}
		changed();
	}
}

static void sony_fx_menu(void)
{
	static const char *vpt_names[7] = { "off", "Mode 1", "Mode 2", "Mode 3", "Mode 4", "Mode 5", "Mode 6" };
	static const char *seq_names[5] = { "off", "Heavy", "Pop", "Jazz", "Unique" };
	struct list_view v = { "Sony effects", 3, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	sony_cs = 0;	/* Clear Stereo: no audible effect on this unit (tested 2026-09-29), no longer offered */
	for (;;) {
		snprintf(items[0], 48, "VPT (surround)"); snprintf(values[0], 24, "%s", vpt_names[sony_vpt + 1]);
		snprintf(items[1], 48, "Normalizer"); snprintf(values[1], 24, "%s", sony_dn ? "on" : "off");
		snprintf(items[2], 48, "DSEE (MP3 enhancer)"); snprintf(values[2], 24, "%s", sony_dsee ? "on" : "off");
		int s = list_run(&v);
		if (s < 0) return;
		if (s == 0) sony_vpt = sony_vpt >= 5 ? -1 : sony_vpt + 1;
		else if (s == 1) sony_dn = !sony_dn;
		else if (s == 2) sony_dsee = !sony_dsee;
		dsp_fx_update();
		changed();
	}
}

int quick_open;	/* while the sound menu (or a submenu) is on screen, OPTION does nothing: no menus stacked on menus */

/* with_np: first row "Now playing" (OPTION from a list while something plays); returns 1 when it's chosen */
static int sound_menu(int with_np)
{
	if (quick_open) return 0;
	quick_open = 1;
	int r = 0;
	struct list_view v = { "Sound", 0, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		struct eng_info e; eng_info(&e);
		int book = eng_active() ? e.mode == ENG_BOOK : 0;
		const char *why = eng_not_direct(book);
		int n = 0, act[12];
		if (with_np) { snprintf(items[n], 48, "Now playing"); values[n][0] = 0; act[n++] = 0; }
		snprintf(items[n], 48, "Now"); snprintf(values[n], 24, "%s", why ? "processed" : "max. battery"); act[n++] = 1;
		int p = preset_index();
		{ static const char *pn[6] = { "off", "Heavy", "Pop", "Jazz", "Unique", "Custom" };
		  snprintf(items[n], 48, "Equalizer"); snprintf(values[n], 24, "%s", pn[sony_eq < 0 || sony_eq > 5 ? 0 : sony_eq]); act[n++] = 2; (void)p; }
		snprintf(items[n], 48, "Headphones"); snprintf(values[n], 24, "%s", hp_xfeed || hp_loud || hp_autoeq ? "effects" : "off"); act[n++] = 3;
		snprintf(items[n], 48, "Voice"); snprintf(values[n], 24, "%d.%02dx%s", eng_speed / 100, eng_speed % 100, voice_trim_on || voice_boost_on ? "+" : ""); act[n++] = 4;
		{ static const char *vb[4] = { "normal", "+3 dB", "+6 dB", "+9 dB" };
		  snprintf(items[n], 48, "Maximum volume"); snprintf(values[n], 24, "%s", vb[vol_boost & 3]); act[n++] = 9; }
		snprintf(items[n], 48, "Even volume"); snprintf(values[n], 24, "%s", replaygain_mode == 0 ? "off" : replaygain_mode == 1 ? "per track" : "per album"); act[n++] = 5;
		snprintf(items[n], 48, "Sony effects"); snprintf(values[n], 24, "%s", sony_vpt >= 0 || sony_dn || sony_dsee ? "on" : "off"); act[n++] = 6;
		snprintf(items[n], 48, "Max. battery"); snprintf(values[n], 24, "%s", dsp_direct ? "on" : "off"); act[n++] = 7;
		snprintf(items[n], 48, "Sony DSP"); snprintf(values[n], 24, "%s", dsp_enabled ? "on" : "off"); act[n++] = 8;
		v.n = n;
		int s = list_run(&v);
		if (s < 0) break;
		switch (act[s]) {
		case 0: r = 1; break;
		case 1:	/* explain the path */
			if (why) { message("Now: processed", "Not using Max. battery because:", why); msg_wait(6); }
			else { message("Now: max. battery", "Sony's DSP decodes and plays", "by itself: the processor barely works."); msg_wait(5); }
			break;
		case 2: eq_menu(); break;
		case 3: headphones_menu(); break;
		case 4: voice_menu(); break;
		case 5: replaygain_mode = (replaygain_mode + 1) % 3; settings_save(); break;
		case 6: sony_fx_menu(); break;
		case 7: dsp_direct = !dsp_direct; changed(); break;	/* from the next track */
		case 8: dsp_enabled = !dsp_enabled; if (!dsp_enabled) dsp_stop_helper(); changed(); break;	/* from the next track */
		case 9:	/* louder headphones: at once, with a warning the first time it goes past normal */
			vol_boost = (vol_boost + 1) % 4;
			if (vol_boost == 1) { message("Maximum volume", "Louder headphones. Mind your hearing.", "+6 and +9 dB can distort loud recordings."); msg_wait(5); }
			aout_set_volume(); settings_save(); break;
		}
		if (r) break;
	}
	quick_open = 0;
	return r;
}

int sound_quick_menu(int with_np) { return sound_menu(with_np); }
void voice_quick_menu(void) { voice_menu(); }

#ifndef HB_VERSION
#define HB_VERSION "dev"	/* build.sh sets the version and the build date */
#endif

/* free space of a mount as "13,2 GB" / "3,1 MB" */
static void free_space(const char *path, char *out, size_t max)
{
	struct statvfs v;
	if (statvfs(path, &v)) { snprintf(out, max, "?"); return; }
	double b = (double)v.f_bavail * v.f_frsize;
	if (b >= 1e9) snprintf(out, max, "%.1f GB", b / 1e9); else snprintf(out, max, "%.1f MB", b / 1e6);
	for (char *c = out; *c; c++) if (*c == '.') *c = ',';
}

/* every line fits the 240 px screen: the Sony font is wide (about 9 characters per 100 px) */
static void about_screen(void)
{
	char ver[64], date[24] = "", c[24], v[24], fr[48], st[48];
	snprintf(ver, sizeof ver, "%s", HB_VERSION);	/* "1.1 (2026-10-22)": version and build date on two lines */
	char *p = strstr(ver, " (");
	if (p) { snprintf(date, sizeof date, "%s", p + 2); *p = 0; if ((p = strchr(date, ')'))) *p = 0; }
	char vl[80]; snprintf(vl, sizeof vl, "Version %s", ver);
	free_space("/contents", c, sizeof c); free_space(statedir, v, sizeof v);
	snprintf(fr, sizeof fr, "Free: %s", c); snprintf(st, sizeof st, "Settings: %s", v);
	scr_screen("About");
	gfx_text_center(W / 2, 50, gfx_anim_intl() ? "NWZ-A820 homebrew" : "NW-A820 homebrew", C_WHITE, 1);
	gfx_text_center(W / 2, 80, vl, C_WHITE, 1);
	if (*date) gfx_text_center(W / 2, 102, date, C_GREY, 1);
	gfx_text_center(W / 2, 130, fr, C_GREY, 1);
	gfx_text_center(W / 2, 152, st, C_GREY, 1);
	gfx_text_center(W / 2, 180, "License: GPL-2.0", C_GREY, 1);
	gfx_text_center(W / 2, 202, "libmad · FAAD2 · lwIP", C_DARK, 1);
	gfx_text_center(W / 2, 222, "gnuboy · Sonic", C_DARK, 1);
	gfx_text_center(W / 2, 242, "Mbed TLS · TJpgDec", C_DARK, 1);
	gfx_text_center(W / 2, 266, "HOME 3 s: to Sony", C_ORANGE, 1);
	gfx_flip();
	while (getkey(500) != K_BACK) idle_tick(0);
}

/* experimental: the Sony UI at 124 MHz instead of 248 (it never changes the clock itself; its decoding runs on the
 * DSP). The boot script applies it when the Sony UI starts; a flag file on the FAT side, so deleting it on the PC
 * undoes it */
#define SONY124 "/contents/HB/sony124"
static void sony_clock_menu(void)
{
	if (!access(SONY124, F_OK)) { unlink(SONY124); sync(); return; }
	scr_message("Sony Walkman: power saving", "Sony's Walkman will run at half clock: less battery, slower menus.", "Experimental. Applies when you go back to Sony.");
	gfx_flip(); msg_wait(5);
	FILE *f = fopen(SONY124, "w");
	if (f) { fputs("124\n", f); fclose(f); sync(); }
}

/* everything the player remembers on /var, after a confirmation; the music and the games' saves stay */
static void reset_menu(void)
{
	static const char *const opts[2] = { "Yes, reset", "Cancel" };
	snprintf(items[0], 48, "%s", opts[0]); snprintf(items[1], 48, "%s", opts[1]);
	values[0][0] = values[1][0] = 0;
	scr_message("Reset everything", "Clears positions, settings, Bluetooth and the boot mode. Your music stays.", "");
	gfx_flip(); msg_wait(4);
	struct list_view v = { "Reset everything", 2, 1, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	if (list_run(&v) == 0) factory_reset();
}

/* Settings > Display */
static void screen_menu(void)
{
	struct list_view v = { "Display", 9, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		snprintf(items[0], 48, "Brightness"); snprintf(values[0], 24, "%d%%", ui_brightness);
		snprintf(items[1], 48, "Screen off after"); snprintf(values[1], 24, "%d s", ui_dim_secs);
		snprintf(items[2], 48, "Big rows with covers"); snprintf(values[2], 24, "%s", ui_big_rows ? "on" : "off");
		snprintf(items[3], 48, "Background"); snprintf(values[3], 24, "%s", wall_names[ui_wall]);
		snprintf(items[4], 48, "Theme"); snprintf(values[4], 24, "%s", theme_names[ui_theme]);
		snprintf(items[5], 48, "Always-on display"); snprintf(values[5], 24, "%s", ui_aod ? "on" : "off");
		snprintf(items[6], 48, "Always-on brightness"); snprintf(values[6], 24, "%d%%", ui_aod_level);
		snprintf(items[7], 48, "Status strip"); snprintf(values[7], 24, "%s", ui_strip_calm ? "calm" : "live");
		{ static const char *const sz[4] = { "small", "medium", "large", "extra large" };
		  snprintf(items[8], 48, "Book text size"); snprintf(values[8], 24, "%s", sz[ui_book_size < 0 ? 0 : ui_book_size > 3 ? 3 : ui_book_size]); }
		int s = list_run(&v);
		if (s < 0) return;
		if (s == 0) { ui_brightness = ui_brightness >= 100 ? 20 : ui_brightness + 20; gfx_backlight(ui_brightness); }	/* the LCD has 5 real levels */
		else if (s == 1) ui_dim_secs = ui_dim_secs >= 120 ? 15 : ui_dim_secs * 2;
		else if (s == 2) ui_big_rows = !ui_big_rows;	/* álbumes, podcasts y audiolibros: from the next time the list opens */
		else if (s == 3) { ui_wall = (ui_wall + 1) % WALL_COUNT; scr_wall_changed(); }
		else if (s == 4) { ui_theme_set(ui_theme == 1 ? 2 : 1); scr_wall_changed(); }	/* SNRGY plata <-> SNRGY negro */
		else if (s == 5) ui_aod = !ui_aod;	/* the strip stays on when the screen times out (SNRGY looks) */
		else if (s == 6) ui_aod_level = ui_aod_level >= 100 ? 20 : ui_aod_level + 20;
		else if (s == 7) ui_strip_calm = !ui_strip_calm;
		else if (s == 8) ui_book_size = (ui_book_size + 1) % (gfx_book_sizes() < 4 ? gfx_book_sizes() : 4);	/* the reader's text: 13, 15, 18, 22 px */	/* tranquila: refreshed every 5 s, long titles don't scroll */
		settings_save();
	}
}

/* Settings > Switch to Sony's Walkman: asks once (a list row is easy to press by accident), then leaves; the
 * Walkman stays in Sony's firmware, power-offs included, until HOME + VOL- are held 2 s there */
static void switch_menu(void)
{
	snprintf(items[0], 48, "Switch now"); snprintf(items[1], 48, "Cancel");
	values[0][0] = values[1][0] = 0;
	scr_message("Switch to Sony's Walkman", "The Walkman starts in Sony's firmware until you come back.", "To come back: hold HOME and VOL- together for 2 s.");
	gfx_flip(); msg_wait(3);
	struct list_view v = { "Switch to Sony's Walkman", 2, 1, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	if (list_run(&v) == 0) switch_to_sony();
}

void settings_menu(void)
{
	struct list_view v = { "Settings", 11, 0, 0, it_label, it_value, 0, LI_NONE, 0, 0 };
	for (;;) {
		int id[11], n = 0;	/* row -> the rows below (the NW-A91x has no Bluetooth row) */
		{ static const char *pn[6] = { "EQ off", "Heavy", "Pop", "Jazz", "Unique", "Custom" };	/* the EQ: the path ("max. battery") is inside */
		  snprintf(items[0], 48, "Sound"); snprintf(values[0], 24, "%s", pn[sony_eq < 0 || sony_eq > 5 ? 0 : sony_eq]); }
		snprintf(items[1], 48, "Display"); values[1][0] = 0;
		snprintf(items[2], 48, "Bluetooth"); snprintf(values[2], 24, "%s", bt_connected() ? "connected" : "");
		snprintf(items[3], 48, "Alarm"); snprintf(values[3], 24, alarm_cfg.on ? "%02d:%02d" : "off", alarm_cfg.hour, alarm_cfg.min);
		{ time_t t = time(0); struct tm *tm = localtime(&t); snprintf(items[4], 48, "Date and time"); if (tm) snprintf(values[4], 24, "%02d:%02d", tm->tm_hour, tm->tm_min); else values[4][0] = 0; }
		snprintf(items[5], 48, "Beep"); snprintf(values[5], 24, "%s", ui_beep ? "on" : "off");
		snprintf(items[6], 48, "Sony Walkman: power saving"); snprintf(values[6], 24, "%s", access(SONY124, F_OK) ? "off" : "on");
		snprintf(items[7], 48, "Switch to Sony's Walkman"); values[7][0] = 0;
		snprintf(items[8], 48, "Power"); values[8][0] = 0;	/* 2026-11-13: moved here from the SNRGY home list */
		snprintf(items[9], 48, "Reset everything"); values[9][0] = 0;
		snprintf(items[10], 48, "About"); values[10][0] = 0;	/* Idioma removed 2026-11-13: English only */
		for (int r = 0; r < 11; r++) if (!(hw_a91x && r == 2)) { if (n != r) { memcpy(items[n], items[r], sizeof items[0]); memcpy(values[n], values[r], sizeof values[0]); } id[n++] = r; }
		v.n = n;
		int s = list_run(&v);
		if (s < 0) return;
		s = id[s];
		if (s == 0) sound_menu(0);
		else if (s == 1) screen_menu();
		else if (s == 2) bt_menu();	/* moved here from the SNRGY home list (Sony's grid keeps its icon) */
		else if (s == 3) alarm_menu();
		else if (s == 4) clock_menu();
		else if (s == 5) { ui_beep = !ui_beep; settings_save(); beep(BEEP_ONE); }	/* turning it on beeps once, like Sony */
		else if (s == 6) sony_clock_menu();
		else if (s == 7) switch_menu();
		else if (s == 8) power_menu();
		else if (s == 9) reset_menu();
		else if (s == 10) about_screen();
	}
}
