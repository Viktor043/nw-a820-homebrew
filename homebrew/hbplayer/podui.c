/* podui.c: the podcast screens around the downloads: finding a show by name (keyboard -> Apple Podcasts directory
 * -> results with their covers), its card (follow / episodes), the list of a feed's episodes to download any of
 * them, and the OPTION menus of the Podcasts list (per show: check now, episodes, how many to keep, stop following;
 * on the top rows: the phone and the directory's country). The network side is podsync.c / podsearch.c. */
#include "hb.h"
#include "screens.h"
#include "podsearch.h"
#include "../net/rss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SEARCH_JSON "/tmp/podsearch.json"
#define FEED_XML "/tmp/podfeed.xml"
#define ART_TMP "/tmp/podart.jpg"
#define MAX_FEED_BYTES (3 << 20)	/* a feed to browse is read up to here (the newest episodes come first) */
#define BROWSE_MAX 40

static struct pod_result res[POD_RESULTS];
static int nres;

/* ---------- a transfer with a "please wait" screen; BACK cancels ---------- */

static const char *wait_title, *wait_text;
static double wait_t0, wait_drawn;
static long feed_scan; static int feed_items, feed_want;

static int wait_cb(long got, long total)
{
	if (now() - wait_drawn > 0.3) {
		wait_drawn = now();
		char sub[64];
		if (total > 0) snprintf(sub, sizeof sub, "%ld of %ld kB", got >> 10, total >> 10);
		else if (got > 0) snprintf(sub, sizeof sub, "%ld kB", got >> 10);
		else snprintf(sub, sizeof sub, "%d s", (int)(now() - wait_t0));
		message(wait_title, wait_text, sub);
	}
	if (feed_want && pod_count_items(FEED_XML, &feed_scan, &feed_items) >= feed_want) return 2;	/* enough episodes */
	if (getkey(0) == K_BACK) return 1;
	return 0;
}

static int fetch_wait(const char *title, const char *text, const char *url, const char *dest)
{
	wait_title = title; wait_text = text; wait_t0 = now(); wait_drawn = 0;
	message(title, text, "");
	return pod_fetch(url, dest, wait_cb);
}

/* a small JPEG to a px x px image (not cached: these are throwaway files) */
static int load_art(const char *art_url, int px, uint16_t *dst)
{
	char u[512];
	pod_art_url(art_url, px, u, sizeof u);
	if (!u[0] || pod_fetch(u, ART_TMP, 0)) return 0;
	int ok = cover_decode_file(ART_TMP, dst, px);
	unlink(ART_TMP);
	return ok;
}

/* ---------- results ---------- */

static uint16_t res_th[POD_RESULTS][EP_THUMB * EP_THUMB];
static signed char res_ts[POD_RESULTS];
static char res_val[POD_RESULTS][96];

static const char *res_label(int i) { return res[i].name; }
static const char *res_value(int i)
{
	char *b = res_val[i];
	if (pod_feed_find_url(res[i].feed) >= 0) snprintf(b, 96, "Following");
	else if (res[i].count > 0) snprintf(b, 96, "%s%s%d episode%s", res[i].author, res[i].author[0] ? " · " : "", res[i].count, res[i].count == 1 ? "" : "s");
	else snprintf(b, 96, "%s", res[i].author);
	return b;
}
static int res_state(int i) { return pod_feed_find_url(res[i].feed) >= 0 ? 2 : 0; }
static struct img res_thumb(int i)
{
	struct img im = { 0, 0, 0 };
	if (res_ts[i] == 1) { im.w = im.h = EP_THUMB; im.px = res_th[i]; }
	return im;
}
/* covers one at a time between keys, the visible rows first */
static int res_pending(void)
{
	for (int j = 0; j < nres; j++) {
		int i = (list_vis_top + j) % nres;
		if (res_ts[i]) continue;
		res_ts[i] = -1;
		if (res[i].art[0] && pod_net_busy() && load_art(res[i].art, EP_THUMB, res_th[i])) res_ts[i] = 1;
		return 1;
	}
	return 0;
}

/* ---------- a feed's episodes ---------- */

static struct rss_item ep[BROWSE_MAX];
static int nep, ep_fidx;
static char ep_dir[PATHLEN], ep_val[BROWSE_MAX][48];
static struct img ep_cover;

static int ep_have(int i)
{
	if (!ep_dir[0]) return 0;
	char fn[256], fsn[256], p[PATHLEN + 256];
	rss_filename(&ep[i], fn, sizeof fn);
	utf8_to_fs(fn, fsn, sizeof fsn);
	snprintf(p, sizeof p, "%s/%s", ep_dir, fsn);
	return access(p, F_OK) == 0;
}
static const char *ep_label(int i) { return ep[i].title; }
static const char *ep_value(int i)
{
	static const char *mes[12] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };
	char d[24] = "", sz[24] = "";
	int y, m, dd;
	if (sscanf(ep[i].date, "%4d-%2d-%2d", &y, &m, &dd) == 3 && m >= 1 && m <= 12) {
		time_t t = time(0); struct tm *tm = localtime(&t);	/* the year only when it isn't this one */
		if (tm && tm->tm_year + 1900 == y) snprintf(d, sizeof d, "%d %s", dd, mes[m - 1]);
		else snprintf(d, sizeof d, "%d %s %d", dd, mes[m - 1], y);
	}
	if (ep[i].bytes > 0) snprintf(sz, sizeof sz, "%ld MB", (ep[i].bytes + (1 << 19)) >> 20);
	if (ep_have(i)) snprintf(ep_val[i], 48, "%s%son the Walkman", d, d[0] ? " · " : "");
	else snprintf(ep_val[i], 48, "%s%s%s", d, d[0] && sz[0] ? " · " : "", sz);
	return ep_val[i];
}
static int ep_state(int i) { return ep_have(i) ? 2 : 0; }
static struct img ep_thumb(int i) { (void)i; return ep_cover; }

static char menu_items[10][64];
static const char *menu_label(int i) { return menu_items[i]; }
/* a short list as a question; returns the row picked, -1 = BACK */
static int ask(const char *title, int n)
{
	struct list_view v = { .title = title, .n = n, .label = menu_label, .bar_y = -1 };
	return list_run(&v);
}

static int follow(const struct pod_result *r);

/* downloads a feed (its newest episodes) and lists them; PLAY on one queues it. r: the search result when the
 * show is not followed yet (following it is offered first), else 0 with fidx */
static void episodes(const char *feed_url, const char *show, int fidx, const struct pod_result *r, struct img cover)
{
	feed_scan = 0; feed_items = 0; feed_want = BROWSE_MAX;
	int f = fetch_wait(show, "Reading the episodes...", feed_url, FEED_XML);
	feed_want = 0;
	if (f) { if (f == -1) { message(show, "Could not read the episodes.", "Try again later."); msg_wait(3); } unlink(FEED_XML); return; }
	FILE *fp = fopen(FEED_XML, "rb");
	char *xml = 0; long n = 0;
	if (fp) { fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET); if (n > 0 && n < MAX_FEED_BYTES) xml = malloc(n); if (xml && fread(xml, 1, n, fp) != (size_t)n) { free(xml); xml = 0; } fclose(fp); }
	unlink(FEED_XML);
	nep = xml ? rss_parse(xml, n, ep, BROWSE_MAX) : 0;
	free(xml);
	if (nep <= 0) { message(show, "This podcast has no episodes", "the Walkman can download."); msg_wait(3); return; }
	static uint16_t thumb[EP_THUMB * EP_THUMB];
	ep_cover = cover.w ? gfx_scaled(cover, EP_THUMB, EP_THUMB, thumb) : (struct img){ 0, 0, 0 };
	ep_fidx = fidx;
	if (fidx >= 0) pod_feed_path(fidx, ep_dir, sizeof ep_dir); else ep_dir[0] = 0;
	struct list_view v = { .title = show, .n = nep, .label = ep_label, .value = ep_value, .state = ep_state, .thumb = ep_thumb, .ep = 1, .bar_y = -1 };
	for (;;) {
		int s = list_run(&v);
		if (s < 0) return;
		if (ep_have(s)) { message(show, "That episode is already on the Walkman.", ep[s].title); msg_wait(2); continue; }
		if (ep_fidx < 0) {	/* not followed yet: the episode needs the show's folder */
			snprintf(menu_items[0], 64, "Follow and download it");
			snprintf(menu_items[1], 64, "Cancel");
			if (ask(show, 2) != 0 || !r || (ep_fidx = follow(r)) < 0) continue;
			pod_feed_path(ep_fidx, ep_dir, sizeof ep_dir);
		}
		int q = dl_add_episode(ep_fidx, &ep[s]);
		if (q == 0) { message(show, "Downloading in the background:", ep[s].title); msg_wait(2); }
		else if (q == 1) { message(show, "Already in the download queue.", ep[s].title); msg_wait(2); }
		else { message(show, "Could not start the download.", ""); msg_wait(3); }
	}
}

/* ---------- following a show ---------- */

/* adds it to feeds.txt (3 newest episodes), fetches its cover for the lists; index or -1 */
static int follow(const struct pod_result *r)
{
	char name[96];
	pod_folder_name(r->name, name, sizeof name);
	int fidx = pod_subscribe(name, r->feed, 3);
	if (fidx < 0) { message("Podcasts", "Could not save the subscription.", "Is the memory full?"); msg_wait(3); return -1; }
	char dir[PATHLEN], cj[PATHLEN + 16], part[PATHLEN + 24], u[512];
	pod_feed_path(fidx, dir, sizeof dir);
	snprintf(cj, sizeof cj, "%s/cover.jpg", dir);
	snprintf(part, sizeof part, "%s.part", cj);
	if (access(cj, F_OK) && r->art[0]) {
		pod_art_url(r->art, 300, u, sizeof u);
		if (!pod_fetch(u, part, 0)) rename(part, cj); else unlink(part);
	}
	return fidx;
}

/* the card of a search result; returns 1 when the user is done (went to the downloads) */
static int show_card(const struct pod_result *r)
{
	static uint16_t big[POD_ART * POD_ART];
	struct img cover = { 0, 0, 0 };
	message(r->name, "Loading...", "");
	if (r->art[0] && load_art(r->art, POD_ART, big)) { cover.w = cover.h = POD_ART; cover.px = big; }
	char info[96] = "";
	int sel = 0;
	for (;;) {
		int fidx = pod_feed_find_url(r->feed), n = 0;
		if (r->count > 0) snprintf(info, sizeof info, "%s%d episode%s%s%s", fidx >= 0 ? "Following · " : "", r->count, r->count == 1 ? "" : "s", fidx < 0 && r->genre[0] ? " · " : "", fidx < 0 ? r->genre : "");
		else snprintf(info, sizeof info, "%s", fidx >= 0 ? "Following" : r->genre);
		const char *items[3];
		items[n++] = fidx >= 0 ? "Get new episodes" : "Follow this podcast";
		items[n++] = "Episodes";
		if (fidx >= 0) items[n++] = "Unfollow";
		if (sel >= n) sel = n - 1;
		scr_pod_show(r->name, r->author, info, cover, items, n, sel); flip();
		idle_tick(0);
		int k = getkey(1000);
		if (k < 0) continue;
		if (k == K_UP) sel = sel > 0 ? sel - 1 : n - 1;
		else if (k == K_DOWN) sel = sel < n - 1 ? sel + 1 : 0;
		else if (k == K_BACK) return 0;
		else if (k == K_PLAY) {
			if (sel == 0) {
				if (fidx < 0) {
					if ((fidx = follow(r)) < 0) continue;
					snprintf(menu_items[0], 64, "Get the latest 3 episodes");
					snprintf(menu_items[1], 64, "Not now");
					if (ask(r->name, 2) != 0) { message(r->name, "Done: you follow it.", "\"Check for new\" gets its episodes."); msg_wait(2); continue; }
				}
				podsync_update(fidx);
				return 1;
			}
			if (sel == 1) episodes(r->feed, r->name, fidx, r, cover);
			if (sel == 2 && fidx >= 0) {
				snprintf(menu_items[0], 64, "Unfollow, keep episodes");
				snprintf(menu_items[1], 64, "Unfollow and delete episodes");
				snprintf(menu_items[2], 64, "Cancel");
				int a = ask(r->name, 3);
				if (a == 0 || a == 1) pod_unsubscribe(fidx, a == 1);
			}
		}
		else ui_volume_key(k);
	}
}

/* ---------- "Agregar podcast" ---------- */

static int search(const char *term)
{
	char url[600];
	pod_search_url(term, url, sizeof url);
	nres = 0;
	int f = fetch_wait("Find a podcast", "Searching...", url, SEARCH_JSON);
	if (f) { unlink(SEARCH_JSON); return f == -2 ? -2 : -1; }
	FILE *fp = fopen(SEARCH_JSON, "rb");
	char *js = 0; long n = 0;
	if (fp) { fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET); if (n > 0 && n < (1 << 20)) js = malloc(n); if (js && fread(js, 1, n, fp) != (size_t)n) { free(js); js = 0; } fclose(fp); }
	unlink(SEARCH_JSON);
	if (!js) return -1;
	nres = pod_search_parse(js, n, res, POD_RESULTS);
	free(js);
	lg("podcasts: search \"%s\": %d results", term, nres);
	return nres;
}

void pod_add(void)
{
	static char term[80];
	int linked = 0;
	for (;;) {
		if (!kbd_input("Find a podcast", "Podcast name", term, sizeof term)) break;
		if (!linked) { if (pod_net_up("Find a podcast")) continue; linked = 1; }
		int n = search(term);
		if (n == -2) continue;
		if (n < 0) { message("Find a podcast", "The search failed.", pod_net_busy() ? "Try again." : "The phone connection dropped."); msg_wait(3); continue; }
		if (n == 0) { message("Find a podcast", "No podcasts found for", term); msg_wait(3); continue; }
		memset(res_ts, 0, sizeof res_ts);
		char title[96]; snprintf(title, sizeof title, "\"%s\"", term);
		struct list_view v = { .title = title, .n = nres, .label = res_label, .value = res_value, .state = res_state, .thumb = res_thumb, .ep = 1, .bar_y = -1 };
		int done = 0;
		while (!done) {
			list_idle_hook = res_pending;
			int s = list_run(&v);
			list_idle_hook = 0;
			if (s < 0) break;
			done = show_card(&res[s]);
		}
		if (done) break;
	}
	if (linked) pod_net_down();
}

/* ---------- OPTION menus of the Podcasts list ---------- */

static const int keep_choices[] = { 1, 2, 3, 5, 10 };

/* OPTION on a show in the Podcasts list; 1 = the list must be read again */
int pod_show_option(const char *folder_path, const char *folder_name)
{
	int fidx = pod_feed_for_folder(folder_path);
	if (fidx < 0) {
		snprintf(menu_items[0], 64, "Sound");
		snprintf(menu_items[1], 64, "Cancel");
		if (ask(folder_name, 2) == 0 && sound_quick_menu(eng_active())) nowplaying_run();
		return 0;
	}
	for (;;) {
		snprintf(menu_items[0], 64, "Check for new episodes");
		snprintf(menu_items[1], 64, "All episodes");
		snprintf(menu_items[2], 64, "Get the latest %d", pod_feed_keep(fidx));
		snprintf(menu_items[3], 64, "Unfollow");
		snprintf(menu_items[4], 64, "Sound");
		int a = ask(folder_name, 5);
		if (a < 0) return 0;
		if (a == 0) { podsync_update(fidx); return 1; }
		if (a == 1) {
			if (pod_net_up("Podcasts")) continue;
			char dir[PATHLEN], cj[PATHLEN + 16]; static uint16_t big[POD_ART * POD_ART];
			pod_feed_path(fidx, dir, sizeof dir); snprintf(cj, sizeof cj, "%s/cover.jpg", dir);
			struct img cover = { 0, 0, 0 };
			if (cover_load(cj, big, POD_ART)) { cover.w = cover.h = POD_ART; cover.px = big; }
			char url[512], show[64];
			snprintf(url, sizeof url, "%s", pod_feed_url(fidx)); snprintf(show, sizeof show, "%s", pod_feed_name(fidx));
			episodes(url, show, fidx, 0, cover);
			pod_net_down();
			return 1;
		}
		if (a == 2) {
			for (int i = 0; i < 5; i++) snprintf(menu_items[i], 64, "%d episode%s", keep_choices[i], keep_choices[i] == 1 ? "" : "s");
			int c = ask("How many to get?", 5);
			if (c >= 0) pod_set_keep(fidx, keep_choices[c]);
			continue;
		}
		if (a == 3) {
			snprintf(menu_items[0], 64, "Unfollow, keep episodes");
			snprintf(menu_items[1], 64, "Unfollow and delete episodes");
			snprintf(menu_items[2], 64, "Cancel");
			int d = ask(folder_name, 3);
			if (d != 0 && d != 1) continue;
			struct eng_info e; eng_info(&e);
			size_t l = strlen(folder_path);
			if (d == 1 && eng_active() && e.path && !strncmp(e.path, folder_path, l) && e.path[l] == '/') eng_stop();	/* playing from it */
			if (pod_unsubscribe(fidx, d == 1)) { message("Podcasts", "Could not change the subscription.", ""); msg_wait(3); }
			return 1;
		}
		if (a == 4) { if (sound_quick_menu(eng_active())) nowplaying_run(); return 0; }
	}
}

/* OPTION on the top rows: the phone and the directory's country */
static const char *const countries[][2] = { { "US", "Estados Unidos" }, { "AR", "Argentina" }, { "MX", "Mexico" }, { "ES", "Spain" },
	{ "CL", "Chile" }, { "CO", "Colombia" }, { "UY", "Uruguay" }, { "GB", "Reino Unido" }, { "BR", "Brasil" } };
#define NCOUNTRIES (int)(sizeof countries / sizeof countries[0])

void pod_menu_option(void)
{
	for (;;) {
		const char *ph = pod_phone_name(), *cn = pod_country;
		for (int i = 0; i < NCOUNTRIES; i++) if (!strcmp(countries[i][0], pod_country)) cn = countries[i][1];
		snprintf(menu_items[0], 64, "Phone: %s", ph ? ph : "choose");
		snprintf(menu_items[1], 64, "Search in: %s", cn);
		int a = ask("Podcasts", 2);
		if (a < 0) return;
		if (a == 0) pod_choose_phone();
		if (a == 1) {
			for (int i = 0; i < NCOUNTRIES; i++) snprintf(menu_items[i], 64, "%s", countries[i][1]);
			int c = ask("Podcast directory", NCOUNTRIES);
			if (c >= 0) { snprintf(pod_country, sizeof pod_country, "%s", countries[c][0]); settings_save(); }
		}
	}
}
