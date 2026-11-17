/* i18n.c: the interface is written in English in the code (2026-11-17: the Spanish source strings and their
 * translation table are gone). ui_tr stays as the hook gfx_tr can point at, and passes texts through. */
#include "hb.h"
#include <string.h>

int ui_lang = 1;	/* English */

const char *ui_tr(const char *s) { return s; }

void ui_lang_set(int lang)
{
	(void)lang; ui_lang = 1;
	gfx_tr = 0;
}
