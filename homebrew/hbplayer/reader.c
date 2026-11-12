/* reader.c: books for the player: EPUB (zip of XHTML; zlib's inflate, already linked for the podcasts) and plain
 * text, from /contents/Libros. A chapter at a time: its XHTML becomes paragraphs of UTF-8 text, laid out in the
 * reader face (tools/mkfont.py --name book: a serif, 21 px lines), justified, one page = the lines that fit above
 * the status bar. The SNRGY strip shows the E-PLAYER's reading lines: chapter and page / book - author / progress
 * and the reading time left. Positions and bookmarks: <statedir>/books.txt.
 *
 * Keys: DOWN next page, UP previous page, LEFT / RIGHT darker / brighter, PLAY portrait / landscape (the whole screen, no strip;
 * held sideways, the keys on the right), VOL+ / VOL- the music's volume, OPTION chapters and bookmarks, BACK the
 * list. The text size: Ajustes > Pantalla > Letra de los libros. The screen never goes dark while a book is open.
 * READER_HOST builds the parsing and layout alone (tests on the PC). */
#include "hb.h"
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "zlib.h"

#define BOOKS_DIR "/contents/Libros"
/* the page: portrait above the status strip, or landscape (PLAY) on the whole screen, 320 x 240, no strip */
int ui_book_size = 1, ui_book_land;	/* saved in pos.txt: the zoom (face 2's size) and the orientation */
#define RD_LH gfx_book_line()		/* line pitch of the reader face at the current zoom */
#define RD_X 12
#define RD_W ((ui_book_land ? H : W) - 24)
#define RD_INDENT 14			/* first line of a paragraph */
#define CHARS_PER_MIN 1100		/* reading speed for the time left (~200 words a minute) */

struct chap { char href[256]; char name[96]; long size; };
struct book { char path[PATHLEN]; char title[128], author[96]; int epub, nch; struct chap *ch; long total; char *txt; long txtlen; };

/* ---------------- zip ---------------- */

struct zent { char name[256]; int method; long csize, usize, lofs; };
static struct zent *zents;
static int nz;
static FILE *zf;

static unsigned rd16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static void zip_close(void) { if (zf) fclose(zf); zf = 0; free(zents); zents = 0; nz = 0; }

static int zip_open(const char *path)
{
	zip_close();
	zf = fopen(path, "rb");
	if (!zf) return -1;
	fseek(zf, 0, SEEK_END);
	long size = ftell(zf), tail = size < 65557 ? size : 65557;
	unsigned char *t = malloc(tail);
	if (!t) return -1;
	fseek(zf, size - tail, SEEK_SET);
	if (fread(t, 1, tail, zf) != (size_t)tail) { free(t); return -1; }
	long e = -1;
	for (long i = tail - 22; i >= 0; i--) if (rd32(t + i) == 0x06054b50) { e = i; break; }
	if (e < 0) { free(t); return -1; }
	int n = rd16(t + e + 10);
	long cdsize = rd32(t + e + 12), cdoff = rd32(t + e + 16);
	free(t);
	unsigned char *cd = malloc(cdsize + 1);
	zents = calloc(n ? n : 1, sizeof *zents);
	if (!cd || !zents) { free(cd); return -1; }
	fseek(zf, cdoff, SEEK_SET);
	if (fread(cd, 1, cdsize, zf) != (size_t)cdsize) { free(cd); return -1; }
	long o = 0;
	for (int i = 0; i < n && o + 46 <= cdsize && rd32(cd + o) == 0x02014b50; i++) {
		int nl = rd16(cd + o + 28), xl = rd16(cd + o + 30), cl = rd16(cd + o + 32);
		struct zent *z = &zents[nz++];
		z->method = rd16(cd + o + 10); z->csize = rd32(cd + o + 20); z->usize = rd32(cd + o + 24); z->lofs = rd32(cd + o + 42);
		snprintf(z->name, sizeof z->name, "%.*s", nl < 255 ? nl : 255, (const char *)cd + o + 46);
		o += 46 + nl + xl + cl;
	}
	free(cd);
	return 0;
}

static struct zent *zip_find(const char *name)
{
	for (int i = 0; i < nz; i++) if (!strcmp(zents[i].name, name)) return &zents[i];
	for (int i = 0; i < nz; i++) if (!strcasecmp(zents[i].name, name)) return &zents[i];
	return 0;
}

/* the whole entry, NUL-terminated (free it) */
static char *zip_read(const char *name, long *len)
{
	struct zent *z = zip_find(name);
	if (!z || !zf || z->usize > (16 << 20)) return 0;
	unsigned char h[30];
	fseek(zf, z->lofs, SEEK_SET);
	if (fread(h, 1, 30, zf) != 30 || rd32(h) != 0x04034b50) return 0;
	fseek(zf, z->lofs + 30 + rd16(h + 26) + rd16(h + 28), SEEK_SET);
	unsigned char *c = malloc(z->csize + 1);
	char *out = malloc(z->usize + 1);
	if (!c || !out || fread(c, 1, z->csize, zf) != (size_t)z->csize) { free(c); free(out); return 0; }
	if (z->method == 0) memcpy(out, c, z->usize);
	else if (z->method == 8) {
		z_stream s; memset(&s, 0, sizeof s);
		if (inflateInit2(&s, -15) != Z_OK) { free(c); free(out); return 0; }
		s.next_in = c; s.avail_in = z->csize; s.next_out = (unsigned char *)out; s.avail_out = z->usize;
		int r = inflate(&s, Z_FINISH);
		inflateEnd(&s);
		if (r != Z_STREAM_END && s.total_out != (unsigned long)z->usize) { free(c); free(out); return 0; }
	} else { free(c); free(out); return 0; }
	free(c);
	out[z->usize] = 0;
	if (len) *len = z->usize;
	return out;
}

/* ---------------- XML helpers ---------------- */

/* attribute `name` of the tag starting at t (up to its '>'), into out */
static int attr(const char *t, const char *name, char *out, int n)
{
	const char *e = strchr(t, '>');
	size_t l = strlen(name);
	for (const char *p = t; p && (!e || p < e); p++) {
		p = strstr(p, name);
		if (!p || (e && p >= e)) break;
		if ((p == t || isspace((unsigned char)p[-1])) && p[l] == '=' && (p[l + 1] == '"' || p[l + 1] == '\'')) {
			char q = p[l + 1]; const char *v = p + l + 2, *ve = strchr(v, q);
			if (!ve) return 0;
			int k = (int)(ve - v) < n - 1 ? (int)(ve - v) : n - 1;
			memcpy(out, v, k); out[k] = 0;
			return 1;
		}
	}
	return 0;
}

/* the text between <tag ...> and </tag> after p (tag without the namespace prefix), entities left as they are */
static int elem_text(const char *p, const char *tag, char *out, int n)
{
	char open[48]; snprintf(open, sizeof open, "%s", tag);
	for (const char *q = p; (q = strchr(q, '<')); q++) {
		const char *nm = q + 1, *colon = strchr(nm, ':'), *sp = nm + strcspn(nm, " >/");
		if (colon && colon < sp) nm = colon + 1;
		if ((size_t)(sp - nm) == strlen(open) && !strncmp(nm, open, sp - nm)) {
			const char *v = strchr(q, '>');
			if (!v) return 0;
			v++;
			const char *ve = strchr(v, '<');
			if (!ve) return 0;
			int k = (int)(ve - v) < n - 1 ? (int)(ve - v) : n - 1;
			memcpy(out, v, k); out[k] = 0;
			return 1;
		}
	}
	return 0;
}

static void put_utf8(char **o, unsigned cp)
{
	char *d = *o;
	/* what the reader faces have: typographic punctuation to its plain form */
	if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x2032) cp = '\'';
	else if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033) cp = '"';
	else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212) cp = '-';
	else if (cp == 0x2026) { memcpy(d, "...", 3); *o = d + 3; return; }
	else if (cp == 0xA0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x200A) cp = ' ';
	else if (cp == 0xAD || cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF) return;
	else if (cp >= 0x300 && cp <= 0x36F) return;	/* combining accents (decomposed text): the base letter stays */
	else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2043) cp = '-';
	else if (cp == 0x2022 || cp == 0x2023 || cp == 0x2027 || cp == 0x25CF || cp == 0x2219 || cp == 0x22C5) cp = 0xB7;
	else if (cp == 0x2039) cp = '<';
	else if (cp == 0x203A) cp = '>';
	else if (cp == 0x02BC || cp == 0x02B9) cp = '\'';
	else if (cp == 0x2122) { memcpy(d, "TM", 2); *o = d + 2; return; }
	else if (cp == 0x0153 || cp == 0x0152) { memcpy(d, cp == 0x0153 ? "oe" : "OE", 2); *o = d + 2; return; }
	else if (cp == 0xFB01 || cp == 0xFB02) { memcpy(d, cp == 0xFB01 ? "fi" : "fl", 2); *o = d + 2; return; }
	else if (cp == 0x2192) { memcpy(d, "->", 2); *o = d + 2; return; }
	if (cp < 0x80) *d++ = (char)cp;
	else if (cp < 0x800) { *d++ = (char)(0xC0 | cp >> 6); *d++ = (char)(0x80 | (cp & 63)); }
	else { *d++ = (char)(0xE0 | cp >> 12); *d++ = (char)(0x80 | ((cp >> 6) & 63)); *d++ = (char)(0x80 | (cp & 63)); }
	*o = d;
}

/* "&amp;" etc. at p (after the '&'): the code point, *len = characters used; 0 = not an entity */
static unsigned entity(const char *p, int *len)
{
	static const struct { const char *n; unsigned cp; } ents[] = {
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
		{ "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
		{ "ldquo", 0x201C }, { "rdquo", 0x201D }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "shy", 0xAD }, { "copy", 0xA9 },
	};
	const char *e = strchr(p, ';');
	if (!e || e - p > 10) return 0;
	*len = (int)(e - p) + 1;
	if (*p == '#') return p[1] == 'x' || p[1] == 'X' ? (unsigned)strtoul(p + 2, 0, 16) : (unsigned)strtoul(p + 1, 0, 10);
	for (unsigned i = 0; i < sizeof ents / sizeof *ents; i++) if ((size_t)(e - p) == strlen(ents[i].n) && !strncmp(p, ents[i].n, e - p)) return ents[i].cp;
	return 0;
}

/* XHTML -> paragraphs separated by '\n'; a heading paragraph starts with '\1'. Returns the text (free it). */
static char *html_text(const char *h, long n, long *outlen)
{
	char *out = malloc(n + 16), *o = out;
	if (!out) return 0;
	int skip = 0, space = 0, para = 0, heading = 0;
	const char *end = h + n;
	const char *body = strstr(h, "<body");
	if (body) h = body;
	while (h < end && *h) {
		if (*h == '<') {
			const char *t = h + 1;
			int close = *t == '/';
			if (close) t++;
			char name[16]; int k = 0;
			while (k < 15 && (isalnum((unsigned char)t[k]) || t[k] == ':')) { name[k] = (char)tolower((unsigned char)t[k]); k++; }
			name[k] = 0;
			const char *gt = strchr(h, '>');
			if (!strncmp(h, "<!--", 4)) { const char *c = strstr(h, "-->"); h = c ? c + 3 : end; continue; }
			if (!gt) break;
			if (!strcmp(name, "style") || !strcmp(name, "script") || !strcmp(name, "head")) skip = !close && gt[-1] != '/';
			int block = !strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "br") || !strcmp(name, "li") || !strcmp(name, "tr")
				|| !strcmp(name, "blockquote") || !strcmp(name, "section") || !strcmp(name, "h1") || !strcmp(name, "h2") || !strcmp(name, "h3")
				|| !strcmp(name, "h4") || !strcmp(name, "h5") || !strcmp(name, "h6") || !strcmp(name, "hr") || !strcmp(name, "pre") || !strcmp(name, "dd") || !strcmp(name, "dt");
			if (block) {
				if (o > out && o[-1] != '\n') { while (o > out && o[-1] == ' ') o--; if (o > out && o[-1] != '\n' && o[-1] != '\1') *o++ = '\n'; else if (o > out && o[-1] == '\1') o--; }
				para = 1; space = 0;
				heading = name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !close;
			}
			h = gt + 1;
			continue;
		}
		if (skip) { h++; continue; }
		unsigned cp = (unsigned char)*h;
		int used = 1;
		if (cp == '&') { int l; unsigned e = entity(h + 1, &l); if (e) { cp = e; used = l + 1; } }
		else if (cp >= 0xC0) {	/* UTF-8 sequence: decode, so the punctuation can be mapped */
			int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
			unsigned v = cp & (0x3F >> extra);
			for (int i = 1; i <= extra && h + i < end; i++) v = v << 6 | ((unsigned char)h[i] & 63);
			cp = v; used = extra + 1;
		}
		h += used;
		if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) { space = 1; continue; }
		if (para) { if (heading) *o++ = '\1'; para = 0; space = 0; }
		else if (space && o > out && o[-1] != '\n' && o[-1] != '\1') *o++ = ' ';
		space = 0;
		put_utf8(&o, cp);
	}
	while (o > out && (o[-1] == '\n' || o[-1] == ' ' || o[-1] == '\1')) o--;
	*o = 0;
	*outlen = o - out;
	return out;
}

/* "a/b/../c%20d.xhtml" relative to dir -> zip path */
static void resolve(const char *dir, const char *href, char *out, int n)
{
	char tmp[512];
	snprintf(tmp, sizeof tmp, "%s%s", href[0] == '/' ? "" : dir, href[0] == '/' ? href + 1 : href);
	char *h = strchr(tmp, '#'); if (h) *h = 0;
	char dec[512]; int k = 0;
	for (const char *p = tmp; *p && k < 511; p++) {
		if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) { char x[3] = { p[1], p[2], 0 }; dec[k++] = (char)strtol(x, 0, 16); p += 2; }
		else dec[k++] = *p;
	}
	dec[k] = 0;
	/* drop "x/../" */
	char *parts[64]; int np = 0;
	for (char *s = strtok(dec, "/"); s && np < 64; s = strtok(0, "/")) {
		if (!strcmp(s, "..")) { if (np) np--; }
		else if (strcmp(s, ".")) parts[np++] = s;
	}
	out[0] = 0;
	for (int i = 0; i < np; i++) { strncat(out, parts[i], n - strlen(out) - 2); if (i + 1 < np) strncat(out, "/", n - strlen(out) - 1); }
}

static void clean_inline(char *s)	/* entities in a short metadata string */
{
	long l; char *t = html_text(s, strlen(s), &l);
	if (!t) return;
	for (char *p = t; *p; p++) if (*p == '\n' || *p == '\1') *p = ' ';
	snprintf(s, 96, "%s", t);
	free(t);
}

/* ---------------- opening a book ---------------- */

static void book_free(struct book *b) { free(b->ch); free(b->txt); memset(b, 0, sizeof *b); zip_close(); }

static int open_epub(struct book *b)
{
	if (zip_open(b->path)) return -1;
	char *c = zip_read("META-INF/container.xml", 0);
	char opf[256] = "";
	const char *rf = c ? strstr(c, "<rootfile ") : 0;
	if (!rf || !attr(rf, "full-path", opf, sizeof opf)) { free(c); return -1; }
	free(c);
	long ol;
	char *o = zip_read(opf, &ol);
	if (!o) return -1;
	char dir[256]; snprintf(dir, sizeof dir, "%s", opf);
	char *sl = strrchr(dir, '/'); if (sl) sl[1] = 0; else dir[0] = 0;
	if (elem_text(o, "title", b->title, sizeof b->title)) clean_inline(b->title);
	if (elem_text(o, "creator", b->author, sizeof b->author)) clean_inline(b->author);
	/* manifest: id -> href; the spine in reading order */
	int cap = 64; b->ch = calloc(cap, sizeof *b->ch);
	char toc[256] = "", navdoc[256] = "";
	const char *spine = strstr(o, "<spine");
	if (spine) { char tid[96]; if (attr(spine, "toc", tid, sizeof tid)) {
		for (const char *it = o; (it = strstr(it, "<item ")); it++) { char id[96]; if (attr(it, "id", id, sizeof id) && !strcmp(id, tid)) { char hr[256]; attr(it, "href", hr, sizeof hr); resolve(dir, hr, toc, sizeof toc); break; } } } }
	for (const char *it = o; (it = strstr(it, "<item ")); it++) {
		char pr[96]; if (attr(it, "properties", pr, sizeof pr) && strstr(pr, "nav")) { char hr[256]; attr(it, "href", hr, sizeof hr); resolve(dir, hr, navdoc, sizeof navdoc); }
	}
	for (const char *r = spine; r && (r = strstr(r, "<itemref")); r++) {
		char idref[96]; if (!attr(r, "idref", idref, sizeof idref)) continue;
		for (const char *it = o; (it = strstr(it, "<item ")); it++) {
			char id[96]; if (!attr(it, "id", id, sizeof id) || strcmp(id, idref)) continue;
			char hr[256]; attr(it, "href", hr, sizeof hr);
			if (b->nch == cap) { cap *= 2; struct chap *nc = realloc(b->ch, cap * sizeof *nc); if (!nc) break; b->ch = nc; }
			struct chap *ch = &b->ch[b->nch];
			memset(ch, 0, sizeof *ch);
			resolve(dir, hr, ch->href, sizeof ch->href);
			struct zent *z = zip_find(ch->href);
			ch->size = z ? z->usize : 0;
			if (z) { b->total += ch->size; b->nch++; }
			break;
		}
	}
	free(o);
	/* chapter names from the table of contents: EPUB 3 nav, else the NCX */
	const char *tocf = navdoc[0] ? navdoc : toc;
	if (tocf[0]) {
		char tdir[256]; snprintf(tdir, sizeof tdir, "%s", tocf); sl = strrchr(tdir, '/'); if (sl) sl[1] = 0; else tdir[0] = 0;
		long tl; char *t = zip_read(tocf, &tl);
		for (const char *p = t; p && *p; ) {
			char href[256] = "", name[96] = "";
			if (navdoc[0]) {
				p = strstr(p, "<a "); if (!p) break;
				attr(p, "href", href, sizeof href);
				const char *g = strchr(p, '>'), *e = g ? strstr(g, "</a>") : 0;
				if (g && e) { int k = (int)(e - g - 1) < 95 ? (int)(e - g - 1) : 95; memcpy(name, g + 1, k); name[k] = 0; }
				p += 3;
			} else {
				p = strstr(p, "<navPoint"); if (!p) break;
				elem_text(p, "text", name, sizeof name);
				const char *ct = strstr(p, "<content"); if (ct) attr(ct, "src", href, sizeof href);
				p += 9;
			}
			clean_inline(name);
			char full[256]; resolve(tdir, href, full, sizeof full);
			for (int i = 0; i < b->nch; i++) if (!b->ch[i].name[0] && !strcmp(b->ch[i].href, full)) { snprintf(b->ch[i].name, sizeof b->ch[i].name, "%s", name); break; }
		}
		free(t);
	}
	return b->nch ? 0 : -1;
}

/* plain text: UTF-8 (or Latin-1, converted), paragraphs at blank lines (or one per line when the file has none),
 * chapters at "CHAPTER" / "Capítulo" lines */
static int open_txt(struct book *b)
{
	FILE *f = fopen(b->path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > (24 << 20)) { fclose(f); return -1; }
	unsigned char *raw = malloc(n + 1);
	if (!raw || fread(raw, 1, n, f) != (size_t)n) { fclose(f); free(raw); return -1; }
	fclose(f);
	raw[n] = 0;
	int utf8 = 1;
	for (long i = 0; i < n && utf8; i++) if (raw[i] >= 0x80) {
		int extra = raw[i] >= 0xF0 ? 3 : raw[i] >= 0xE0 ? 2 : raw[i] >= 0xC0 ? 1 : -1;
		if (extra < 0) utf8 = 0;
		for (int k = 1; utf8 && k <= extra; k++) if (i + k >= n || (raw[i + k] & 0xC0) != 0x80) utf8 = 0;
		if (utf8) i += extra;
	}
	long lines = 0, blanks = 0;
	for (long i = 0; i < n; i++) if (raw[i] == '\n') { lines++; long j = i + 1; while (j < n && (raw[j] == '\r' || raw[j] == ' ' || raw[j] == '\t')) j++; if (j < n && raw[j] == '\n') blanks++; }
	int per_line = blanks * 20 < lines;
	char *out = malloc(n * 2 + 16), *o = out;
	if (!out) { free(raw); return -1; }
	for (long i = 0; i < n; ) {
		unsigned cp = raw[i++];
		if (!utf8 && cp >= 0x80) { put_utf8(&o, cp); continue; }
		if (utf8 && cp >= 0xC0) {	/* decode, so curly quotes, dashes and the rest map like in EPUB text */
			int ex = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
			cp &= 0x3F >> ex;
			for (int k = 0; k < ex && i < n; k++) cp = cp << 6 | (raw[i++] & 63);
			put_utf8(&o, cp);
			continue;
		}
		if (cp == '\r') continue;
		if (cp == '\n') {
			long j = i; while (j < n && (raw[j] == '\r' || raw[j] == ' ' || raw[j] == '\t')) j++;
			int blank = j < n && raw[j] == '\n';
			if (per_line || blank) { while (o > out && o[-1] == ' ') o--; if (o > out && o[-1] != '\n') *o++ = '\n'; i = blank ? j + 1 : i; }
			else if (o > out && o[-1] != ' ' && o[-1] != '\n') *o++ = ' ';
			continue;
		}
		if ((cp == ' ' || cp == '\t') && (o == out || o[-1] == ' ' || o[-1] == '\n')) continue;
		*o++ = (char)cp;
	}
	free(raw);
	*o = 0;
	b->txt = out; b->txtlen = o - out; b->total = b->txtlen;
	/* chapters */
	int cap = 16; b->ch = calloc(cap, sizeof *b->ch);
	long start = 0;
	for (char *p = out; ; ) {
		char *e = strchr(p, '\n'); long l = e ? e - p : (long)strlen(p);
		int is = l > 0 && l < 70 && (!strncmp(p, "CHAPTER", 7) || !strncmp(p, "Chapter", 7) || !strncmp(p, "CAPÍTULO", 9) || !strncmp(p, "Capítulo", 9) || !strncmp(p, "CAPITULO", 8) || !strncmp(p, "Capitulo", 8));
		if (is || !b->nch) {
			if (b->nch == cap) { cap *= 2; struct chap *nc = realloc(b->ch, cap * sizeof *nc); if (!nc) break; b->ch = nc; }
			struct chap *c = &b->ch[b->nch++]; memset(c, 0, sizeof *c);
			c->size = p - out;	/* start offset for now */
			if (is) snprintf(c->name, sizeof c->name, "%.*s", (int)(l < 95 ? l : 95), p);
		}
		if (!e) break;
		p = e + 1;
	}
	for (int i = 0; i < b->nch; i++) {	/* size field = start; href holds nothing: sizes from the starts */
		long s0 = b->ch[i].size, s1 = i + 1 < b->nch ? b->ch[i + 1].size : b->txtlen;
		snprintf(b->ch[i].href, sizeof b->ch[i].href, "%ld", s0);
		b->ch[i].size = s1 - s0;
	}
	(void)start;
	return 0;
}

static int book_open(struct book *b, const char *path)
{
	memset(b, 0, sizeof *b);
	snprintf(b->path, sizeof b->path, "%s", path);
	const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
	snprintf(b->title, sizeof b->title, "%s", base);
	char *dot = strrchr(b->title, '.'); if (dot) *dot = 0;
	b->epub = dot && !strcasecmp(base + (dot - b->title), ".epub");
	int r = b->epub ? open_epub(b) : open_txt(b);
	if (r) book_free(b);
	return r;
}

/* the text of chapter i (free it), headings marked with '\1' */
static char *chapter_text(struct book *b, int i, long *len)
{
	if (i < 0 || i >= b->nch) return 0;
	if (!b->epub) {
		long s = atol(b->ch[i].href), n = b->ch[i].size;
		char *t = malloc(n + 2); if (!t) return 0;
		memcpy(t, b->txt + s, n); t[n] = 0;
		if (b->ch[i].name[0]) { /* the "CHAPTER ..." line becomes the heading */ memmove(t + 1, t, n + 1); t[0] = '\1'; n++; }
		*len = n;
		return t;
	}
	long hl; char *h = zip_read(b->ch[i].href, &hl);
	if (!h) return 0;
	char *t = html_text(h, hl, len);
	free(h);
	return t;
}

/* ---------------- layout ---------------- */

struct line { long off; int len; unsigned char kind, indent; };	/* kind: 0 justified, 1 paragraph end, 2 heading, 3 blank */
static struct line *lines;
static int nlines, lines_cap;

static int face_w(const char *s, int n)
{
	char buf[256];
	if (n > 255) n = 255;
	memcpy(buf, s, n); buf[n] = 0;
	return gfx_text_w(buf, 1);
}

static void add_line(long off, int len, int kind, int indent)
{
	if (nlines == lines_cap) { lines_cap = lines_cap ? lines_cap * 2 : 256; struct line *nl = realloc(lines, lines_cap * sizeof *nl); if (!nl) return; lines = nl; }
	lines[nlines++] = (struct line){ off, len, (unsigned char)kind, (unsigned char)indent };
}

static void heading_face(int on) { if (on) { gfx_face = ui_theme ? 1 : 0; gfx_caps = ui_theme != 0; } else { gfx_face = 2; gfx_caps = 0; } }

/* the chapter's lines at the reader's width */
static void layout(const char *t, long n)
{
	nlines = 0;
	int spw, saved_face = gfx_face, saved_caps = gfx_caps;
	gfx_book_size = ui_book_size;
	gfx_face = 2; gfx_caps = 0;
	spw = gfx_text_w(" ", 1);
	long p = 0;
	int first_after_heading = 1;
	while (p < n) {
		long e = p; while (e < n && t[e] != '\n') e++;
		int heading = t[p] == '\1';
		long s = p + heading;
		heading_face(heading);
		int indent = heading || first_after_heading ? 0 : RD_INDENT, x = indent;
		long ls = s;
		for (long w = s; w < e; ) {
			long we = w; while (we < e && t[we] != ' ') we++;
			int ww = face_w(t + w, (int)(we - w));
			if (ww > RD_W) {	/* a word wider than the line: cut it */
				long k = w; int acc = 0;
				while (k < we) { int cl = 1; while (k + cl < we && ((unsigned char)t[k + cl] & 0xC0) == 0x80) cl++; int g = face_w(t + k, cl); if (x + acc + g > RD_W && k > w) break; acc += g; k += cl; }
				if (ls < w) { add_line(ls, (int)(w - 1 - ls), heading ? 2 : 0, indent); indent = 0; }
				add_line(w, (int)(k - w), heading ? 2 : 0, ls < w ? 0 : indent); indent = 0;
				x = 0; ls = w = k;
				continue;
			}
			if (x + (w > ls ? spw : 0) + ww > RD_W && w > ls) {
				add_line(ls, (int)(w - 1 - ls), heading ? 2 : 0, indent);
				indent = 0; x = 0; ls = w;
			}
			x += (w > ls ? spw : 0) + ww;
			w = we < e ? we + 1 : we;
		}
		if (ls < e || e == s) add_line(ls, (int)(e - ls), heading ? 2 : 1, indent);
		if (heading) add_line(e, 0, 3, 0);	/* a blank line under a heading */
		first_after_heading = heading;
		heading_face(0);
		p = e + 1;
	}
	gfx_face = saved_face; gfx_caps = saved_caps;	/* the rest of the UI keeps its own face */
}

#ifndef READER_SHOT	/* the screenshot build (shots.c) needs only the layout and the page */
/* ---------------- positions and bookmarks ---------------- */

struct mark { char path[PATHLEN]; int ch; long off; int pct; int bm; };
static struct mark *marks;
static int nmarks;

static void marks_load(void)
{
	free(marks); marks = 0; nmarks = 0;
	char fn[PATHLEN + 16]; snprintf(fn, sizeof fn, "%s/books.txt", statedir);
	FILE *f = fopen(fn, "r");
	if (!f) return;
	char l[PATHLEN + 64];
	while (fgets(l, sizeof l, f)) {
		struct mark m; memset(&m, 0, sizeof m);
		char kind[8]; int pos;
		if (sscanf(l, "%7s %d %ld %d %n", kind, &m.ch, &m.off, &m.pct, &pos) < 4) continue;
		l[strcspn(l, "\r\n")] = 0;
		snprintf(m.path, sizeof m.path, "%s", l + pos);
		m.bm = !strcmp(kind, "bm");
		struct mark *nm = realloc(marks, (nmarks + 1) * sizeof *nm);
		if (!nm) break;
		marks = nm; marks[nmarks++] = m;
	}
	fclose(f);
}

static void marks_save(void)
{
	char fn[PATHLEN + 16], tmp[PATHLEN + 32]; snprintf(fn, sizeof fn, "%s/books.txt", statedir);
	FILE *f = safe_open(fn, tmp, sizeof tmp);
	if (!f) return;
	for (int i = 0; i < nmarks; i++) fprintf(f, "%s %d %ld %d %s\n", marks[i].bm ? "bm" : "pos", marks[i].ch, marks[i].off, marks[i].pct, marks[i].path);
	safe_close(f, tmp, fn);
}

static struct mark *pos_of(const char *path) { for (int i = 0; i < nmarks; i++) if (!marks[i].bm && !strcmp(marks[i].path, path)) return &marks[i]; return 0; }

static void pos_put(const char *path, int ch, long off, int pct)
{
	struct mark *m = pos_of(path);
	if (!m) { struct mark *nm = realloc(marks, (nmarks + 1) * sizeof *nm); if (!nm) return; marks = nm; m = &marks[nmarks++]; memset(m, 0, sizeof *m); snprintf(m->path, sizeof m->path, "%s", path); }
	m->ch = ch; m->off = off; m->pct = pct;
	marks_save();
}

#endif

#ifndef READER_HOST
/* ---------------- the reader ---------------- */

static struct book bk;
static char *ctext;
static long clen;
static int cur_ch = -1;

static int load_chapter(int i)
{
	free(ctext); ctext = 0; clen = 0; cur_ch = -1;
	ctext = chapter_text(&bk, i, &clen);
	if (!ctext) { ctext = strdup(""); clen = 0; }
	cur_ch = i;
	layout(ctext, clen);
	if (!nlines) add_line(0, 0, 1, 0);
	return 0;
}

static int rd_y0(void) { return ui_book_land ? 10 : ui_theme ? 8 : TITLE_H + 6; }
static int rd_lines(void) { return ((ui_book_land ? W : BOTTOM_Y) - rd_y0() - 4) / RD_LH; }
static int npages(void) { int k = rd_lines(); return (nlines + k - 1) / k; }
static int page_of(long off) { int l = 0; while (l + 1 < nlines && lines[l + 1].off <= off) l++; return l / rd_lines(); }

static int book_pct(int page)
{
	if (bk.total <= 0) return 0;
	long before = 0; for (int i = 0; i < cur_ch; i++) before += bk.ch[i].size;
	double in = npages() ? (double)page / npages() : 0;
	int p = (int)((before + in * bk.ch[cur_ch].size) * 100 / bk.total);
	return p < 0 ? 0 : p > 100 ? 100 : p;
}

static void draw_page(int page)
{
	static char l1[48], l2[200], left[24];
	int k = rd_lines(), y0 = rd_y0();
	if (ui_book_land) gfx_fill(0, 0, W, H, scr_paper());	/* the whole screen: no title, no strip */
	else scr_page(bk.title);
	int saved_face = gfx_face, saved_caps = gfx_caps;
	gfx_book_size = ui_book_size;
	gfx_landscape = ui_book_land;
	for (int r = 0; r < k; r++) {
		int i = page * k + r;
		if (i >= nlines) break;
		struct line *L = &lines[i];
		int y = y0 + r * RD_LH;
		if (L->kind == 3 || !L->len) continue;
		const char *s = ctext + L->off;
		if (L->kind == 2) {	/* heading: centred, in the UI face */
			heading_face(1);
			char b[256]; int n = L->len < 255 ? L->len : 255; memcpy(b, s, n); b[n] = 0;
			gfx_text_center((ui_book_land ? H : W) / 2, y - 1, b, C_WHITE, 1);
			continue;
		}
		heading_face(0);
		int x = RD_X + L->indent, words = 0, natural = 0, spw = gfx_text_w(" ", 1);
		for (int j = 0; j < L->len; ) { int e = j; while (e < L->len && s[e] != ' ') e++; natural += face_w(s + j, e - j); words++; j = e + 1; }
		int gaps = words - 1, extra = RD_W - L->indent - natural - gaps * spw;
		int justify = L->kind == 0 && gaps > 0 && extra > 0 && extra < gaps * 14;
		for (int j = 0, g = 0; j < L->len; g++) {
			int e = j; while (e < L->len && s[e] != ' ') e++;
			char b[256]; int n = e - j < 255 ? e - j : 255; memcpy(b, s + j, n); b[n] = 0;
			x = gfx_text(x, y, b, C_WHITE, 1);
			x += spw + (justify ? extra / gaps + (g < extra % gaps) : 0);
			j = e + 1;
		}
	}
	gfx_face = saved_face; gfx_caps = saved_caps;
	gfx_landscape = 0;
	if (ui_book_land) return;
	/* the strip: CH 3 P 12/40 / book - author / 37% ---- 4H 12M */
	int pct = book_pct(page);
	long before = 0; for (int i = 0; i < cur_ch; i++) before += bk.ch[i].size;
	long rest = bk.total - before - (long)(npages() ? (double)page / npages() * bk.ch[cur_ch].size : 0);
	int mins = (int)(rest * (bk.epub ? 0.6 : 1.0) / CHARS_PER_MIN);
	snprintf(l1, sizeof l1, ui_lang ? "CH%d P%d/%d" : "CAP%d P%d/%d", cur_ch + 1, page + 1, npages());	/* compact: the strip has ~10 letters before the battery */
	snprintf(l2, sizeof l2, "%s%s%s", bk.title, bk.author[0] ? " - " : "", bk.author);
	if (mins >= 60) snprintf(left, sizeof left, "%dH %02dM", mins / 60, mins % 60); else snprintf(left, sizeof left, "%dM", mins);
	ui_st.rd_on = 1; ui_st.rd_l1 = l1; ui_st.rd_l2 = l2; ui_st.rd_left = left; ui_st.rd_pct = pct;
	scr_page_status(l1);
}

#ifndef READER_SHOT
static struct book *lv_book;
static const char *chap_label(int i)
{
	static char b[128];
	if (lv_book->ch[i].name[0]) return lv_book->ch[i].name;
	snprintf(b, sizeof b, "Capítulo %d", i + 1);
	return b;
}

static int bm_idx[256], nbm;
static const char *bm_label(int i)
{
	static char b[160];
	struct mark *m = &marks[bm_idx[i]];
	const char *cn = m->ch < bk.nch && bk.ch[m->ch].name[0] ? bk.ch[m->ch].name : "";
	snprintf(b, sizeof b, "%d%% %s", m->pct, cn);
	return b;
}
static int bm_delete(int i)
{
	int k = bm_idx[i];
	memmove(&marks[k], &marks[k + 1], (nmarks - k - 1) * sizeof *marks); nmarks--;
	marks_save();
	return 1;
}

static const char *reader_opt_label(int i) { static const char *t[3] = { "Capítulos", "Añadir marcador", "Marcadores" }; return i >= 0 && i < 3 ? t[i] : ""; }

/* OPTION: chapters / add a bookmark / bookmarks; returns 1 if the position moved (*ch, *off) */
static int reader_menu(int *ch, long *off, int page)
{
	struct list_view mv = { "Opciones", 3, 0, 0, reader_opt_label, 0, 0, LI_NONE, 0, 0 };
	int s = list_run(&mv);
	if (s == 0) {	/* the chapters, the current one selected */
		lv_book = &bk;
		struct list_view cv = { bk.title, bk.nch, cur_ch, 0, chap_label, 0, 0, LI_FOLDER, 0, 0 };
		if (cv.sel >= LIST_ROWS) cv.top = cv.sel - LIST_ROWS / 2;
		int c = list_run(&cv);
		if (c >= 0) { *ch = c; *off = 0; return 1; }
	} else if (s == 1) {	/* a bookmark at the top of this page */
		struct mark *nm = realloc(marks, (nmarks + 1) * sizeof *nm);
		if (!nm) return 0;
		marks = nm;
		struct mark *m = &marks[nmarks++]; memset(m, 0, sizeof *m);
		snprintf(m->path, sizeof m->path, "%s", bk.path);
		int l = page * rd_lines();
		m->ch = cur_ch; m->off = l < nlines ? lines[l].off : 0; m->pct = book_pct(page); m->bm = 1;
		marks_save();
		eng_note("Marcador guardado");
	} else if (s == 2) {	/* this book's bookmarks; OPTION on one deletes it */
		nbm = 0;
		for (int i = 0; i < nmarks && nbm < 256; i++) if (marks[i].bm && !strcmp(marks[i].path, bk.path)) bm_idx[nbm++] = i;
		if (!nbm) { eng_note("Sin marcadores"); return 0; }
		struct list_view bv = { "Marcadores", nbm, 0, 0, bm_label, 0, 0, LI_NONE, 0, 0 };
		bv.option = bm_delete;
		int c = list_run(&bv);
		if (c >= 0 && c < nbm) { struct mark *m = &marks[bm_idx[c]]; *ch = m->ch; *off = m->off; return 1; }
	}
	return 0;
}

static void reader_run(const char *path)
{
	scr_message("Libros", "Abriendo...", "");
	flip();
	if (book_open(&bk, path)) { eng_note("No se pudo abrir el libro"); return; }
	marks_load();
	struct mark *m = pos_of(path);
	int ch = m && m->ch < bk.nch ? m->ch : 0;
	long off = m ? m->off : 0;
	load_chapter(ch);
	int page = page_of(off), redraw = 1, turns = 0, saved_rot = gfx_rot;
	ui_keep_awake = 1;	/* the screen stays on while a book is open */
	if (ui_book_land) { ui_fullscreen = 1; gfx_rot = 0; gfx_invalidate(); }
	int redraw_strip = 0;
	for (;;) {
		if (popup_expired() && redraw_strip) { redraw_strip = 0; redraw = 1; }	/* the volume shown in the strip: the reading lines again */
		if (redraw && backlight_on) { draw_page(page); flip(); redraw = 0; }
		idle_tick(0);
		int k = getkey(1000);
		if (k == K_REDRAW) { redraw = 1; continue; }
		if (k < 0) continue;
		int np = npages(), moved = 0;
		if (k == K_VOLUP || k == K_VOLDN) { volume_key(k); if (!ui_book_land) redraw_strip = 1; continue; }	/* the music's volume */
		if (k == K_PLAY) {	/* portrait / landscape, at the same place */
			long here = page * rd_lines() < nlines ? lines[page * rd_lines()].off : 0;
			{
				ui_book_land = !ui_book_land;
				ui_fullscreen = ui_book_land;
				gfx_rot = ui_book_land ? 0 : saved_rot;
				gfx_invalidate();
			}
			layout(ctext, clen);
			if (!nlines) add_line(0, 0, 1, 0);
			page = page_of(here);
			settings_save();
			redraw = 1;
			continue;
		}
		if (k == K_LEFT || k == K_RIGHT) {	/* the backlight, in its 5 real steps */
			int b = ui_brightness + (k == K_RIGHT ? 20 : -20);
			if (b < 20) b = 20;
			if (b > 100) b = 100;
			if (b != ui_brightness) { ui_brightness = b; gfx_backlight(b); settings_save(); }
			if (!ui_book_land) { char n[32]; snprintf(n, sizeof n, "Brillo %d%%", ui_brightness); eng_note(n); redraw_strip = 1; }
			continue;
		}
		if (k == K_DOWN) {
			if (page + 1 < np) page++;
			else if (cur_ch + 1 < bk.nch) { load_chapter(cur_ch + 1); page = 0; }
			moved = 1;
		} else if (k == K_UP) {
			if (page > 0) page--;
			else if (cur_ch > 0) { load_chapter(cur_ch - 1); page = npages() - 1; }
			moved = 1;
		} else if (k == K_OPTION) {
			int nch = cur_ch; long noff = 0;
			if (reader_menu(&nch, &noff, page)) { load_chapter(nch); page = page_of(noff); moved = 1; }
			redraw = 1;
		} else if (k == K_BACK) break;
		if (moved) { redraw = 1; if (++turns % 5 == 0) pos_put(bk.path, cur_ch, lines[page * rd_lines() < nlines ? page * rd_lines() : 0].off, book_pct(page)); }
	}
	pos_put(bk.path, cur_ch, nlines ? lines[page * rd_lines() < nlines ? page * rd_lines() : 0].off : 0, book_pct(page));
	ui_st.rd_on = 0;
	ui_keep_awake = 0;
	if (ui_fullscreen) { ui_fullscreen = 0; gfx_rot = saved_rot; gfx_invalidate(); }
	free(ctext); ctext = 0; free(lines); lines = 0; nlines = lines_cap = 0;
	book_free(&bk);
}

/* ---------------- the list of books ---------------- */

static char (*bpaths)[PATHLEN];
static int nbooks;
static void scan(const char *dir, int depth)
{
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char p[PATHLEN]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(p, &st)) continue;
		if (S_ISDIR(st.st_mode)) { if (depth < 2) scan(p, depth + 1); continue; }
		const char *dot = strrchr(e->d_name, '.');
		if (!dot || (strcasecmp(dot, ".epub") && strcasecmp(dot, ".txt"))) continue;
		char (*np)[PATHLEN] = realloc(bpaths, (nbooks + 1) * sizeof *np);
		if (!np) break;
		bpaths = np; snprintf(bpaths[nbooks++], PATHLEN, "%s", p);
	}
	closedir(d);
}
static int cmp_path(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static const char *book_label(int i)
{
	static char b[160];
	const char *s = strrchr(bpaths[i], '/'); s = s ? s + 1 : bpaths[i];
	snprintf(b, sizeof b, "%s", s);
	char *dot = strrchr(b, '.'); if (dot) *dot = 0;
	for (char *p = b; *p; p++) if (*p == '_') *p = ' ';
	return b;
}
static const char *book_value(int i)
{
	static char b[16];
	struct mark *m = pos_of(bpaths[i]);
	if (!m) return "";
	snprintf(b, sizeof b, "%d%%", m->pct);
	return b;
}

void books_menu(void)
{
	mkdir(BOOKS_DIR, 0755);
	nbooks = 0;
	scan(BOOKS_DIR, 0);
	qsort(bpaths, nbooks, PATHLEN, cmp_path);
	marks_load();
	struct list_view v = { "Libros", nbooks, 0, 0, book_label, book_value, 0, LI_NONE, "No hay libros. Copiá archivos EPUB o TXT a la carpeta Libros.", 0 };
	for (;;) {
		v.n = nbooks;
		int s = list_run(&v);
		if (s < 0 || s >= nbooks) break;
		v.sel = s;
		reader_run(bpaths[s]);
		marks_load();
	}
	free(bpaths); bpaths = 0; nbooks = 0;
}
#else
/* screenshots: one page of `text` (paragraphs separated by '\n', '\1' starts a heading) as chapter 1 of 1 */
void reader_shot(const char *title, const char *author, const char *text)
{
	memset(&bk, 0, sizeof bk);
	snprintf(bk.title, sizeof bk.title, "%s", title); snprintf(bk.author, sizeof bk.author, "%s", author);
	static struct chap one; one.size = (long)strlen(text) * 40; bk.ch = &one; bk.nch = 1; bk.total = one.size;
	free(ctext); ctext = strdup(text); clen = (long)strlen(text); cur_ch = 0;
	layout(ctext, clen);
	draw_page(0);
	ui_st.rd_on = 0;
}
#endif
#else
/* ---------------- host test: reader_test BOOK -> chapters, a sample of text, pages ---------------- */
int main(int argc, char **argv)
{
	if (argc < 2) return 1;
	if (argc > 2) gfx_load_res(argv[2]);
	struct book b;
	if (book_open(&b, argv[1])) { printf("open failed\n"); return 1; }
	printf("title: %s\nauthor: %s\nchapters: %d, %ld bytes\n", b.title, b.author, b.nch, b.total);
	for (int i = 0; i < b.nch && i < 12; i++) printf("  %2d %-40s %6ld %s\n", i + 1, b.ch[i].name, b.ch[i].size, b.ch[i].href);
	int show = argc > 3 ? atoi(argv[3]) : 3;
	long n; char *t = chapter_text(&b, show, &n);
	if (t) {
		layout(t, n);
		printf("chapter %d: %ld bytes of text, %d lines\n", show + 1, n, nlines);
		for (int i = 0; i < nlines && i < 14; i++) printf("  [%d%s] %.*s\n", lines[i].kind, lines[i].indent ? ">" : " ", lines[i].len, t + lines[i].off);
		free(t);
	}
	book_free(&b);
	return 0;
}
#endif
