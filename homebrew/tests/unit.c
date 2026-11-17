/* unit.c: PC tests of the player's pure logic (built and run by run_tests.py with zig for Windows):
 * the podcast directory parser, folder names, FAT name mapping, the battery curve, the keyboard layout and the
 * English translation. Prints one line per failure and a summary; exit code = failures. */
#include "../hbplayer/hb.h"
#include "../hbplayer/podsearch.h"
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the player symbols the modules under test reach (none of them runs here) */
void lg(const char *fmt, ...) { (void)fmt; }
void hb_alive(void) {}
int dl_active(void) { return 0; }
int dl_percent(void) { return -1; }

static char *slurp(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
	char *b = malloc(*n + 1);
	if (fread(b, 1, *n, f) != (size_t)*n) { free(b); fclose(f); return 0; }
	b[*n] = 0; fclose(f);
	return b;
}

static void test_podsearch(const char *dir)
{
	char p[512]; long n; struct pod_result r[POD_RESULTS];
	snprintf(p, sizeof p, "%s/itunes_corridor.json", dir);
	char *j = slurp(p, &n);
	CHECK(j, "fixture %s", p);
	if (j) {
		int k = pod_search_parse(j, n, r, POD_RESULTS);
		CHECK(k == 3, "corridor results %d", k);
		CHECK(!strcmp(r[0].name, "Corridor Cast") && !strcmp(r[0].feed, "https://anchor.fm/s/31429754/podcast/rss") && r[0].count == 250, "corridor first result");
		for (long cut = 0; cut < n; cut += 7) { int c = pod_search_parse(j, cut, r, POD_RESULTS); CHECK(c >= -1 && c <= POD_RESULTS, "cut %ld -> %d", cut, c); }
		free(j);
	}
	snprintf(p, sizeof p, "%s/itunes_escapes.json", dir);
	j = slurp(p, &n);
	CHECK(j, "fixture %s", p);
	if (j) {
		int k = pod_search_parse(j, n, r, POD_RESULTS);
		CHECK(k == 1, "escapes results %d (the one without a feed is dropped)", k);
		CHECK(!strcmp(r[0].name, "Caf\xc3\xa9 \"x\" \xf0\x9f\x98\x80 a/b:c"), "escapes name [%s]", r[0].name);
		free(j);
	}
	CHECK(pod_search_parse("not json", 8, r, POD_RESULTS) == -1, "garbage");
	CHECK(pod_search_parse("{\"results\":[]}", 14, r, POD_RESULTS) == 0, "empty");
	char u[600];
	strcpy(pod_country, "AR");
	pod_search_url("Se regalan dudas \xc3\xb1 & co", u, sizeof u);
	CHECK(!strcmp(u, "https://itunes.apple.com/search?media=podcast&entity=podcast&limit=15&country=AR&term=Se+regalan+dudas+%C3%B1+%26+co"), "url %s", u);
	pod_art_url("https://h/x/mza_1.jpg/100x100bb.jpg", 46, u, sizeof u);
	CHECK(!strcmp(u, "https://h/x/mza_1.jpg/46x46bb.jpg"), "art %s", u);
	pod_art_url("https://h/other.png", 46, u, sizeof u);
	CHECK(!strcmp(u, "https://h/other.png"), "art untouched %s", u);
	char f[128];
	pod_folder_name("Elevating Enterprise: Inside the I-Corridor", f, sizeof f);
	CHECK(!strcmp(f, "Elevating Enterprise Inside the I-Corridor"), "folder [%s]", f);
	pod_folder_name("Caf\xc3\xa9 \"x\" \xf0\x9f\x98\x80 a/b:c ", f, sizeof f);
	CHECK(!strcmp(f, "Caf\xc3\xa9 x a b c"), "folder emoji [%s]", f);
	pod_folder_name("...", f, sizeof f);
	CHECK(!strcmp(f, "Podcast"), "folder dots [%s]", f);
	pod_folder_name("A very long podcast name that goes on and on and on, far beyond sixty bytes \xc3\xa1\xc3\xa9", f, sizeof f);
	CHECK(strlen(f) <= 60 && (f[strlen(f) - 1] & 0xC0) != 0xC0, "folder long [%s]", f);
}

static void test_fsname(void)
{
	char o[64];
	utf8_to_fs("R\xc3\xa9volution \xe2\x80\x9cx\xe2\x80\x9d \xe2\x80\x94 \xf0\x9f\x98\x80!", o, sizeof o);
	CHECK(!strcmp(o, "R\xe9volution 'x' - !"), "utf8_to_fs [%s]", o);
	fs_to_utf8("R\xe9volution", o, sizeof o);
	CHECK(!strcmp(o, "R\xc3\xa9volution"), "fs_to_utf8 [%s]", o);
	CHECK(fs_match("R\xe9volution", "R\xc3\xa9volution"), "fs_match latin1");
	CHECK(fs_match("a?b", "a\xe2\x80\x94" "b"), "fs_match '?'");
	CHECK(!fs_match("ab", "abc"), "fs_match length");
}

static void test_batt(void)
{
	CHECK(batt_pct(230) == 100 && batt_pct(224) == 100, "full");
	CHECK(batt_pct(177) == 0 && batt_pct(150) == 0, "empty");
	CHECK(batt_pct(200) == 18, "200 -> %d", batt_pct(200));
	int last = 0;
	for (double a = 170; a <= 230; a += 0.25) { int p = batt_pct(a); CHECK(p >= last && p >= 0 && p <= 100, "monotonic at %.2f", a); last = p; }
}

static void test_kbd(void)
{
	for (int m = 0; m < 2; m++)
		for (int u = 0; u < 2; u++)
			for (int r = 0; r < KBD_ROWS; r++) {
				struct kbd_key k[KBD_COLS];
				int n = kbd_row(m, u, r, k), col = 0;
				for (int i = 0; i < n; i++) { CHECK(k[i].col == col, "row %d key %d col", r, i); col += k[i].span; if (k[i].kind == KB_CHAR) CHECK(k[i].ch && *k[i].ch, "char"); }
				CHECK(col == KBD_COLS, "mode %d upper %d row %d covers %d columns", m, u, r, col);
			}
}

static void test_i18n(void)	/* 2026-11-17: the interface is English in the code; ui_tr passes texts through */
{
	ui_lang_set(1);
	CHECK(!strcmp(ui_tr("Settings"), "Settings"), "pass-through");
	CHECK(gfx_tr == 0, "no translation hook");
}

int main(int argc, char **argv)
{
	test_podsearch(argc > 1 ? argv[1] : "fixtures");
	test_fsname();
	test_batt();
	test_kbd();
	test_i18n();
	printf("unit: %d checks, %d failures\n", checks, fails);
	return fails;
}
