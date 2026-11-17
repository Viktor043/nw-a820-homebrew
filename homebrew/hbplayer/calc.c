/* calc.c: Tools > Calculator (2026-11-17). A pocket calculator on the d-pad: the keys are a 4 x 5 grid, the
 * arrows move the cursor (wrapping), PLAY presses the key, BACK leaves. Music keeps playing (poll_key paces the
 * engine). Doubles in software float: plenty fast for a few key presses. Drawn by scr_calc (screens.c). */
#include "hb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void fmt(double v, char *s, size_t n)
{
	if (v == 0) v = 0;	/* no "-0" */
	snprintf(s, n, "%.10g", v);
	if (strlen(s) > 12) snprintf(s, n, "%.5e", v);
}

static double apply(double a, char op, double b, int *err)
{
	switch (op) {
	case '+': return a + b;
	case '-': return a - b;
	case 'x': return a * b;
	case '/': if (b == 0) { *err = 1; return 0; } return a / b;
	}
	return b;
}

void calc_run(void)
{
	char ent[32] = "0", expr[48] = "";
	double acc = 0;
	char op = 0;
	int fresh = 1, err = 0, sel = 16, redraw = 1;	/* the cursor starts on 0 */
	for (;;) {
		if (redraw && backlight_on && !popup_active()) {
			struct calc_view cv = { err ? "Error" : ent, expr, sel };
			scr_calc(&cv); flip(); redraw = 0;
		}
		idle_tick(0);
		int k = poll_key(500);
		if (popup_expired()) redraw = 1;
		if (k == K_REDRAW) { redraw = 1; continue; }
		if (k < 0) continue;
		redraw = 1;
		if (k == K_BACK) return;
		if (k == K_UP) { sel -= CALC_COLS; if (sel < 0) sel += CALC_ROWS * CALC_COLS; continue; }
		if (k == K_DOWN) { sel = (sel + CALC_COLS) % (CALC_ROWS * CALC_COLS); continue; }
		if (k == K_LEFT) { sel = sel % CALC_COLS ? sel - 1 : sel + CALC_COLS - 1; continue; }
		if (k == K_RIGHT) { sel = sel % CALC_COLS < CALC_COLS - 1 ? sel + 1 : sel - CALC_COLS + 1; continue; }
		if (k != K_PLAY) { volume_key(k); continue; }
		const char *key = calc_keys[sel];
		if (err) { err = 0; strcpy(ent, "0"); acc = 0; op = 0; fresh = 1; expr[0] = 0; if (key[0] == 'C') continue; }
		double val = strtod(ent, 0);
		if (key[0] >= '0' && key[0] <= '9') {
			if (fresh) { strcpy(ent, "0"); fresh = 0; }
			size_t digits = 0; for (const char *p = ent; *p; p++) digits += *p >= '0' && *p <= '9';
			if (digits >= 12) continue;
			if (!strcmp(ent, "0")) ent[0] = 0; else if (!strcmp(ent, "-0")) ent[1] = 0;
			strcat(ent, key);
		} else if (!strcmp(key, ".")) {
			if (fresh) { strcpy(ent, "0"); fresh = 0; }
			if (!strchr(ent, '.')) strcat(ent, ".");
		} else if (!strcmp(key, "DEL")) {
			if (fresh) continue;
			size_t l = strlen(ent);
			if (l) ent[--l] = 0;
			if (!l || !strcmp(ent, "-")) strcpy(ent, "0");
		} else if (!strcmp(key, "C")) {
			strcpy(ent, "0"); acc = 0; op = 0; fresh = 1; expr[0] = 0;
		} else if (!strcmp(key, "+/-")) {
			if (ent[0] == '-') memmove(ent, ent + 1, strlen(ent));
			else if (strcmp(ent, "0") && strlen(ent) < sizeof ent - 1) { memmove(ent + 1, ent, strlen(ent) + 1); ent[0] = '-'; }
		} else if (!strcmp(key, "%")) {	/* 200 + 10 % = 220; 50 x 10 % = 5 */
			fmt(op == '+' || op == '-' ? acc * val / 100 : val / 100, ent, sizeof ent); fresh = 1;
		} else if (!strcmp(key, "=")) {
			if (!op) continue;
			char a[24]; fmt(acc, a, sizeof a);
			snprintf(expr, sizeof expr, "%s %c %s =", a, op, ent);
			acc = apply(acc, op, val, &err);
			fmt(acc, ent, sizeof ent); op = 0; fresh = 1;
		} else {	/* + - x / */
			if (op && !fresh) acc = apply(acc, op, val, &err);
			else if (!op) acc = val;
			op = key[0];
			char a[24]; fmt(acc, a, sizeof a);
			snprintf(expr, sizeof expr, "%s %c", a, op);
			fmt(acc, ent, sizeof ent); fresh = 1;
		}
	}
}
