/* led.c: the NW-A91x's notification light (2026-11-17; the A82x has none). Driver icx1101_led, loaded by Sony's
 * /etc/init.d/icx1087driver before our hook runs. The interface comes from Sony's own /usr/local/test/test_led
 * (run in Unicorn, every pattern's ioctl captured):
 *   ioctl 0x40046c02 (LED pattern): { 0, priority 0..4, repeat (-1 = forever), steps, *step } with
 *     step = { ms, brightness 0..100, abortok }. Sony's "hotaru" (firefly) = 103 steps of 30 ms ramping up and down.
 *   ioctl 0x6c03 LED on, 0x6c04 LED off, 0x6c00 reset (cancels patterns), 0xc0046c01 read the state.
 * What it shows: charging = a slow breathing, charged and still plugged = steady, battery at 10 % or less = a short
 * blip every 4 s; the alarm blinks while it rings and the end of a Focus session blinks a few seconds.
 * Settings > Display > Notification light turns it all off. */
#include "hb.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LED_DEV "/dev/icx1101_led"
#define LED_PATTERN 0x40046c02
#define LED_ON 0x6c03
#define LED_OFF 0x6c04
#define LED_RESET 0x6c00
enum { PRI_MIN, PRI_LOW, PRI_NORMAL, PRI_HIGH, PRI_MAX };

struct led_step { int ms, bright, abortok; };
struct led_prm { int zero, prio, repeat, n; const struct led_step *step; };

int ui_led = 1;	/* setting: the light on (A91x) */
static int fd = -2, shown = -1, alert, logged;
static double alert_until;

static struct led_step breathe[81], steady[1] = { { 1000, 100, 1 } }, blip[2] = { { 120, 100, 1 }, { 3880, 0, 1 } },
	fast[2] = { { 125, 100, 1 }, { 375, 0, 1 } }, blink[2] = { { 250, 100, 1 }, { 250, 0, 1 } };

static int dev(void)
{
	if (fd == -2) {
		fd = hw_a91x ? open(LED_DEV, O_RDWR) : -1;
		if (hw_a91x && fd < 0) lg("led: cannot open %s", LED_DEV);
		for (int i = 0; i < 40; i++) {	/* 0 -> 100 -> 0 in 3.2 s, then 0.4 s dark */
			breathe[i].ms = breathe[79 - i].ms = 40;
			breathe[i].bright = breathe[79 - i].bright = (i + 1) * (i + 1) * 100 / 1600;	/* squared: looks even to the eye */
			breathe[i].abortok = breathe[79 - i].abortok = 1;
		}
		breathe[80].ms = 400; breathe[80].bright = 0; breathe[80].abortok = 1;
	}
	return fd;
}

static void run(int what)
{
	if (what == shown || dev() < 0) return;
	shown = what;
	int r0 = ioctl(fd, LED_RESET), r1 = ioctl(fd, LED_OFF), r2 = 0;
	struct led_prm p = { 0, PRI_NORMAL, -1, 0, 0 };
	switch (what) {
	case LED_CHARGING: p.prio = PRI_LOW; p.n = 81; p.step = breathe; break;
	case LED_FULL: p.prio = PRI_LOW; p.n = 1; p.step = steady; break;
	case LED_LOW: p.prio = PRI_LOW; p.n = 2; p.step = blip; break;
	case LED_ALARM: p.prio = PRI_HIGH; p.n = 2; p.step = fast; break;
	case LED_FOCUS: p.prio = PRI_HIGH; p.n = 2; p.step = blink; p.repeat = 16; break;
	}
	if (p.n) r2 = ioctl(fd, LED_PATTERN, &p);
	if (logged < 12) { logged++; lg("led: %d (reset %d, off %d, pattern %d)", what, r0, r1, r2); }
}

/* an event pattern on top of the battery one: secs > 0 for that long, 0 until led_alert(LED_NONE) */
void led_alert(int what, double secs)
{
	alert = ui_led ? what : LED_NONE;
	alert_until = secs > 0 ? now() + secs : 0;
	led_tick();
}

/* from idle_tick: the battery pattern unless an event pattern is on */
void led_tick(void)
{
	if (!hw_a91x) return;
	if (alert && alert_until && now() > alert_until) alert = LED_NONE;
	int what = LED_NONE;
	if (!ui_led) what = LED_NONE;
	else if (alert) what = alert;
	else if (ui_st.charging) what = ui_st.batt >= 100 ? LED_FULL : LED_CHARGING;
	else if (ui_st.batt > 0 && ui_st.batt <= 10) what = LED_LOW;
	run(what);
}

void led_stop(void) { run(LED_NONE); }
