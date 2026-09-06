/*
 * xsunclock -- a pure-Xlib clone of the classic "xsunclock" for NX11 on EwokOS.
 *
 * A world map in equirectangular projection with the day/night terminator
 * sweeping across it, the subsolar point (where the sun stands overhead), the
 * analemma (the figure-eight the sun traces over a year at a fixed clock time),
 * labelled cities and a UTC / local-time read-out.  This is ordinary X11 code:
 * libNX11 turns every call into a Nano-X server call and the top-level window
 * becomes a real EwokOS xwin window (NANOWM=0), so the desktop window manager
 * owns the frame and the move/resize/close gestures.
 *
 * The real xsunclock ships an XPM image of the earth.  NX11 has no image files
 * and no libXpm, so a simplified 5-degree world coastline is baked in below as
 * per-row land spans and rasterised into a mask at start-up.  The solar maths
 * (declination, equation of time, hour angle, terminator) are the standard
 * formulas and are computed live from time()/gmtime(); EwokOS honours $TZ for
 * the local-time read-out (see libgloss/compat.c).
 *
 * The loop is a non-blocking poll (XCheckWindowEvent() -> GsSelect(POLL) also
 * flushes and presents the previous frame).  The map is repainted when the
 * minute rolls over (the terminator moves only ~0.25 deg/min) and the status
 * bar every second.  proc_usleep() paces the loop.
 *
 * Controls:  l = city labels, g = graticule, a = analemma, space = redraw now,
 *            Q/Esc quits, the window manager close box quits, freely resizable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ewoksys/proc.h>		/* proc_usleep */

#define TITLE		"Sun Clock (NX11)"
#define FONT_NAME	"fixed"
#define FULL_CIRCLE	23040		/* 360 * 64 */
#define POLL_US		250000		/* 250 ms */
#define TB		24		/* toolbar height */
#define SB		40		/* status height (two lines) */
#define MARGIN		8
#define GRIDX		72		/* land mask: 5-degree cells */
#define GRIDY		36
#define NSTEP		12		/* day->night colour ramp steps */
#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif
#define DEG2RAD		(M_PI / 180.0)
#define RAD2DEG		(180.0 / M_PI)

/* one inclusive run of land columns [c0,c1] on row r of the mask */
typedef struct { unsigned char r, c0, c1; } LandSpan;

/*
 * Simplified world land mask at 5-degree resolution.  Row r covers latitude
 * (90 - 5r) .. (85 - 5r); column c covers longitude (5c - 180) .. (5c - 175).
 * Each entry paints one horizontal land run; overlapping runs are fine.  The
 * continents are recognisable rather than cartographically exact -- enough for
 * the terminator to sweep over real coastlines.
 */
static const LandSpan spans[] = {
	{  1,19,22},{  1,27,31},{  1,41,42},{  1,54,58},
	{  2,17,24},{  2,26,31},{  2,41,42},{  2,52,64},{  2,68,71},
	{  3, 3,33},{  3,38,71},
	{  4, 3,23},{  4,25,32},{  4,37,71},
	{  5, 3,24},{  5,26,32},{  5,37,68},
	{  6, 2,25},{  6,27,28},{  6,34,35},{  6,38,68},
	{  7,10,25},{  7,33,66},
	{  8,11,25},{  8,35,64},
	{  9,11,22},{  9,34,63},
	{ 10,11,21},{ 10,34,35},{ 10,37,38},{ 10,40,63},
	{ 11,11,21},{ 11,34,63},
	{ 12,14,17},{ 12,19,20},{ 12,33,61},
	{ 13,15,21},{ 13,33,48},{ 13,50,59},
	{ 14,17,19},{ 14,32,42},{ 14,44,47},{ 14,50,52},{ 14,54,58},{ 14,60,61},
	{ 15,18,20},{ 15,32,44},{ 15,46,47},{ 15,50,52},{ 15,54,58},{ 15,60,61},
	{ 16,20,29},{ 16,33,45},{ 16,51,52},{ 16,54,58},{ 16,60,61},
	{ 17,20,27},{ 17,37,46},{ 17,55,61},
	{ 18,19,29},{ 18,38,44},{ 18,55,60},{ 18,62,66},
	{ 19,20,29},{ 19,38,44},{ 19,57,61},{ 19,62,66},
	{ 20,21,28},{ 20,38,43},{ 20,44,46},{ 20,58,64},
	{ 21,21,28},{ 21,38,44},{ 21,45,46},{ 21,58,65},
	{ 22,22,28},{ 22,38,43},{ 22,44,46},{ 22,58,66},
	{ 23,21,26},{ 23,38,42},{ 23,58,66},
	{ 24,21,25},{ 24,39,42},{ 24,59,66},
	{ 25,21,25},{ 25,39,41},{ 25,62,66},{ 25,70,71},
	{ 26,21,23},{ 26,65,66},{ 26,69,70},
	{ 27,21,23},{ 27,69,70},
	{ 28,21,23},
	{ 29,22,23},{ 29,30,31},
	{ 30,23,24},
	{ 31,20,55},
	{ 32, 2,71},
	{ 33, 0,71},
	{ 34, 0,71},
	{ 35, 0,71},
};
#define NSPAN	((int)(sizeof(spans) / sizeof(spans[0])))

typedef struct { const char *name; double lat, lon; } City;
static const City cities[] = {
	{ "New York",     40.7,  -74.0 }, { "Los Angeles", 34.1, -118.2 },
	{ "Mexico",       19.4,  -99.1 }, { "Rio",        -22.9,  -43.2 },
	{ "Buenos Aires",-34.6,  -58.4 }, { "London",      51.5,   -0.1 },
	{ "Moscow",       55.8,   37.6 }, { "Cairo",       30.0,   31.2 },
	{ "Lagos",         6.5,    3.4 }, { "Nairobi",     -1.3,   36.8 },
	{ "Dubai",        25.2,   55.3 }, { "Bombay",      19.1,   72.9 },
	{ "Beijing",      39.9,  116.4 }, { "Tokyo",       35.7,  139.7 },
	{ "Singapore",     1.4,  103.8 }, { "Sydney",     -33.9,  151.2 },
};
#define NCITY	((int)(sizeof(cities) / sizeof(cities[0])))

static Display *	dpy;
static int		scr;
static Window		win;
static GC		gc;
static XFontStruct *	xfont;
static Colormap		cmap;
static Atom		wm_delete;

static unsigned char	land[GRIDY][GRIDX];
static unsigned long	land_pal[NSTEP];	/* night -> day land ramp  */
static unsigned long	sea_pal[NSTEP];		/* night -> day ocean ramp */
static unsigned long	c_bg, c_text, c_dim, c_border;
static unsigned long	c_grat, c_equator, c_term, c_ana;
static unsigned long	c_sun, c_sunray, c_city, c_citytxt;

static int		win_w = 640, win_h = 420;
static int		mx, my, mw, mh;		/* map rect */
static int		show_labels = 1, show_grat = 1, show_ana = 1;
static int		running = 1, need_draw = 1;
static int		last_sec = -1, last_min = -1;
static double		g_slat, g_slon;		/* last subsolar point */

/*----------------------------------------------------------------------------*/

static unsigned long
alloc_rgb(int r, int g, int b)
{
	XColor	c;

	memset(&c, 0, sizeof(c));
	c.red   = (unsigned short)(r << 8);
	c.green = (unsigned short)(g << 8);
	c.blue  = (unsigned short)(b << 8);
	c.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(dpy, cmap, &c))
		return c.pixel;
	return BlackPixel(dpy, scr);
}

/* allocate a colour linearly blended between two RGB triples by t in [0,1] */
static unsigned long
alloc_blend(int r0, int g0, int b0, int r1, int g1, int b1, double t)
{
	int	r = r0 + (int)((r1 - r0) * t + 0.5);
	int	g = g0 + (int)((g1 - g0) * t + 0.5);
	int	b = b0 + (int)((b1 - b0) * t + 0.5);

	return alloc_rgb(r, g, b);
}

static void
text_left(int x, int baseline, const char *s, unsigned long color)
{
	XSetForeground(dpy, gc, color);
	XDrawString(dpy, win, gc, x, baseline, s, (int)strlen(s));
}

/*----------------------------------------------------------------------------*/
/* the land mask                                                                */
/*----------------------------------------------------------------------------*/

static void
rasterize_land(void)
{
	int	i, r, c;

	memset(land, 0, sizeof(land));
	for (i = 0; i < NSPAN; i++) {
		r = spans[i].r;
		if (r < 0 || r >= GRIDY)
			continue;
		for (c = spans[i].c0; c <= spans[i].c1 && c < GRIDX; c++)
			land[r][c] = 1;
	}
}

/*----------------------------------------------------------------------------*/
/* solar astronomy                                                              */
/*----------------------------------------------------------------------------*/

/* solar declination (radians) for a day-of-year (1..365), Spencer's series */
static double
solar_declination(int doy)
{
	double	b = 2.0 * M_PI * (doy - 1) / 365.0;

	return 0.006918
	     - 0.399912 * cos(b)     + 0.070257 * sin(b)
	     - 0.006758 * cos(2 * b) + 0.000907 * sin(2 * b)
	     - 0.002697 * cos(3 * b) + 0.001480 * sin(3 * b);
}

/* equation of time (minutes) for a day-of-year */
static double
equation_of_time(int doy)
{
	double	b = 2.0 * M_PI * (doy - 1) / 365.0;

	return 229.18 * (0.000075
	     + 0.001868 * cos(b)     - 0.032077 * sin(b)
	     - 0.014615 * cos(2 * b) - 0.040849 * sin(2 * b));
}

/* subsolar longitude (degrees, [-180,180]) at a UTC fractional hour */
static double
subsolar_lon(int doy, double utc_hours)
{
	double	lon = 15.0 * (12.0 - (utc_hours + equation_of_time(doy) / 60.0));

	while (lon > 180.0)  lon -= 360.0;
	while (lon < -180.0) lon += 360.0;
	return lon;
}

/* fill in the subsolar point (and the UTC hour) for a moment in time */
static void
compute_subsolar(time_t now, double *slat, double *slon, double *utc_hours)
{
	struct tm	g = *gmtime(&now);	/* copy: gmtime returns a static */
	int		doy = g.tm_yday + 1;
	double		uh = g.tm_hour + g.tm_min / 60.0 + g.tm_sec / 3600.0;

	*slat = solar_declination(doy) * RAD2DEG;
	*slon = subsolar_lon(doy, uh);
	if (utc_hours != NULL)
		*utc_hours = uh;
}

/* sun altitude (degrees) at (lat,lon) given the subsolar point; <0 is night */
static double
sun_altitude(double lat, double lon, double slat, double slon)
{
	double	h  = (lon - slon) * DEG2RAD;		/* hour angle */
	double	la = lat * DEG2RAD, d = slat * DEG2RAD;
	double	s  = sin(la) * sin(d) + cos(la) * cos(d) * cos(h);

	if (s > 1.0)  s = 1.0;
	if (s < -1.0) s = -1.0;
	return asin(s) * RAD2DEG;
}

/* latitude of the terminator (sun altitude 0) at a given longitude */
static double
terminator_lat(double lon, double slat, double slon)
{
	double	h = (lon - slon) * DEG2RAD;
	double	d = slat * DEG2RAD;
	double	sd = sin(d);

	if (fabs(sd) < 1e-6)				/* equinox: pole to pole */
		return (cos(h) >= 0.0) ? -89.9 : 89.9;
	return atan(-cos(d) * cos(h) / sd) * RAD2DEG;
}

/*----------------------------------------------------------------------------*/
/* equirectangular projection                                                   */
/*----------------------------------------------------------------------------*/

static int	lon2x(double lon) { return mx + (int)((lon + 180.0) / 360.0 * mw + 0.5); }
static int	lat2y(double lat) { return my + (int)((90.0 - lat) / 180.0 * mh + 0.5); }
static double	x2lon(int x)    { return (double)(x - mx) / (double)mw * 360.0 - 180.0; }

static void
layout(void)
{
	int	avail_w = win_w - 2 * MARGIN;
	int	avail_h = win_h - TB - SB - 2 * MARGIN;
	int	w, h;

	if (avail_w < 32) avail_w = 32;
	if (avail_h < 16) avail_h = 16;
	w = avail_w;				/* keep the 2:1 map aspect */
	h = w / 2;
	if (h > avail_h) {
		h = avail_h;
		w = h * 2;
	}
	mw = w;
	mh = h;
	mx = MARGIN + (avail_w - w) / 2;
	my = TB + MARGIN + (avail_h - h) / 2;
}

/*----------------------------------------------------------------------------*/
/* drawing                                                                      */
/*----------------------------------------------------------------------------*/

static void
draw_map(double slat, double slon)
{
	int	r, c;

	for (r = 0; r < GRIDY; r++) {
		double	lat = 87.5 - 5.0 * r;			/* cell centre */
		int	y0 = my + mh * r / GRIDY;
		int	y1 = my + mh * (r + 1) / GRIDY;
		for (c = 0; c < GRIDX; c++) {
			double	lon = c * 5.0 - 177.5;
			double	alt = sun_altitude(lat, lon, slat, slon);
			double	f = (alt + 6.0) / 12.0;		/* twilight ramp */
			int	idx;
			int	x0 = mx + mw * c / GRIDX;
			int	x1 = mx + mw * (c + 1) / GRIDX;

			if (f < 0.0) f = 0.0;
			if (f > 1.0) f = 1.0;
			idx = (int)(f * (NSTEP - 1) + 0.5);
			XSetForeground(dpy, gc,
				       land[r][c] ? land_pal[idx] : sea_pal[idx]);
			XFillRectangle(dpy, win, gc, x0, y0,
				       (unsigned)(x1 - x0), (unsigned)(y1 - y0));
		}
	}
}

static void
draw_graticule(void)
{
	int	v;

	XSetForeground(dpy, gc, c_grat);
	for (v = -180; v <= 180; v += 30) {
		int	x = lon2x((double)v);
		XDrawLine(dpy, win, gc, x, my, x, my + mh - 1);
	}
	for (v = -60; v <= 60; v += 30) {
		int	y = lat2y((double)v);
		XDrawLine(dpy, win, gc, mx, y, mx + mw - 1, y);
	}
	XSetForeground(dpy, gc, c_equator);			/* equator */
	XDrawLine(dpy, win, gc, mx, lat2y(0.0), mx + mw - 1, lat2y(0.0));
}

static void
draw_terminator(double slat, double slon)
{
	int	x, px = 0, py = 0, have = 0;

	XSetForeground(dpy, gc, c_term);
	for (x = mx; x <= mx + mw; x += 2) {
		double	tlat = terminator_lat(x2lon(x), slat, slon);
		int	y = lat2y(tlat);

		if (y < my)      y = my;
		if (y > my + mh) y = my + mh;
		if (have)
			XDrawLine(dpy, win, gc, px, py, x, y);
		px = x; py = y; have = 1;
	}
}

/* the analemma: subsolar positions at this clock time across the year */
static void
draw_analemma(double utc_hours)
{
	int	doy, px = 0, py = 0, have = 0;

	XSetForeground(dpy, gc, c_ana);
	for (doy = 1; doy <= 366; doy += 2) {
		double	slat = solar_declination(doy) * RAD2DEG;
		double	slon = subsolar_lon(doy, utc_hours);
		int	x = lon2x(slon), y = lat2y(slat);

		if (have)
			XDrawLine(dpy, win, gc, px, py, x, y);
		px = x; py = y; have = 1;
	}
}

static void
draw_cities(void)
{
	int	i;

	for (i = 0; i < NCITY; i++) {
		int	x = lon2x(cities[i].lon);
		int	y = lat2y(cities[i].lat);

		if (x < mx || x > mx + mw || y < my || y > my + mh)
			continue;
		XSetForeground(dpy, gc, c_city);
		XFillRectangle(dpy, win, gc, x - 1, y - 1, 3, 3);
		if (show_labels) {
			const char *	nm = cities[i].name;
			/* keep the label inside the map */
			int		tw = XTextWidth(xfont, nm, (int)strlen(nm));
			int		tx = x + 3;
			if (tx + tw > mx + mw)
				tx = x - 3 - tw;
			XSetForeground(dpy, gc, c_citytxt);
			XDrawString(dpy, win, gc, tx, y + 3, nm, (int)strlen(nm));
		}
	}
}

static void
draw_sun(double slat, double slon)
{
	int	x = lon2x(slon), y = lat2y(slat);
	int	r = 5, i;

	if (x < mx || x > mx + mw || y < my || y > my + mh)
		return;
	XSetForeground(dpy, gc, c_sunray);
	for (i = 0; i < 8; i++) {
		double	a = i * M_PI / 4.0;
		XDrawLine(dpy, win, gc,
			  x + (int)(r * cos(a)),       y + (int)(r * sin(a)),
			  x + (int)((r + 4) * cos(a)), y + (int)((r + 4) * sin(a)));
	}
	XSetForeground(dpy, gc, c_sun);
	XFillArc(dpy, win, gc, x - r, y - r, 2 * r, 2 * r, 0, FULL_CIRCLE);
	XSetForeground(dpy, gc, c_sunray);
	XDrawArc(dpy, win, gc, x - r, y - r, 2 * r, 2 * r, 0, FULL_CIRCLE);
}

static void
draw_toolbar(void)
{
	int	base = TB / 2 + (xfont->ascent - xfont->descent) / 2;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, TB);
	text_left(6, base, "xsunclock (NX11)", c_text);
	text_left(140, base, "l labels  g grid  a analemma  space redraw  q quit",
		  c_dim);
	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, TB - 1, win_w, TB - 1);
}

static void
draw_status(double slat, double slon)
{
	time_t		now = time(NULL);
	struct tm	g = *gmtime(&now);	/* copy before localtime() clobbers */
	struct tm	l = *localtime(&now);
	static const char *mon[12] = { "Jan","Feb","Mar","Apr","May","Jun",
				       "Jul","Aug","Sep","Oct","Nov","Dec" };
	char		buf[128];
	int		y0 = win_h - SB;
	int		lineh = xfont->ascent + xfont->descent;
	int		b1 = y0 + 3 + xfont->ascent;
	int		b2 = b1 + lineh + 3;

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, y0, win_w, SB);
	XSetForeground(dpy, gc, c_border);
	XDrawLine(dpy, win, gc, 0, y0, win_w, y0);

	snprintf(buf, sizeof(buf), "UTC %02d:%02d:%02d   Local %02d:%02d:%02d",
		 g.tm_hour, g.tm_min, g.tm_sec, l.tm_hour, l.tm_min, l.tm_sec);
	text_left(6, b1, buf, c_text);

	snprintf(buf, sizeof(buf), "%s %02d %d   Sun %4.1f%c %5.1f%c",
		 mon[l.tm_mon % 12], l.tm_mday, l.tm_year + 1900,
		 fabs(slat), (slat >= 0 ? 'N' : 'S'),
		 fabs(slon), (slon >= 0 ? 'E' : 'W'));
	text_left(6, b2, buf, c_dim);
}

static void
redraw_all(void)
{
	time_t	now = time(NULL);
	double	utc_hours = 0.0;

	compute_subsolar(now, &g_slat, &g_slon, &utc_hours);

	XSetForeground(dpy, gc, c_bg);
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

	draw_toolbar();
	draw_map(g_slat, g_slon);
	if (show_grat)
		draw_graticule();
	draw_terminator(g_slat, g_slon);
	if (show_ana)
		draw_analemma(utc_hours);
	draw_cities();
	draw_sun(g_slat, g_slon);

	XSetForeground(dpy, gc, c_border);
	XDrawRectangle(dpy, win, gc, mx - 1, my - 1,
		       (unsigned)mw + 1, (unsigned)mh + 1);

	draw_status(g_slat, g_slon);
}

static void
handle_event(XEvent *ev)
{
	switch (ev->type) {
	case Expose:
		if (ev->xexpose.count == 0)
			need_draw = 1;
		break;

	case ConfigureNotify:
		if (ev->xconfigure.width != win_w ||
		    ev->xconfigure.height != win_h) {
			win_w = ev->xconfigure.width;
			win_h = ev->xconfigure.height;
			layout();
			need_draw = 1;
		}
		break;

	case KeyPress: {
		KeySym	ks;
		char	buf[8];

		XLookupString(&ev->xkey, buf, (int)sizeof(buf), &ks, NULL);
		if (ks == XK_q || ks == XK_Q || ks == XK_Escape) {
			running = 0;
		} else if (ks == XK_l || ks == XK_L) {
			show_labels = !show_labels; need_draw = 1;
		} else if (ks == XK_g || ks == XK_G) {
			show_grat = !show_grat; need_draw = 1;
		} else if (ks == XK_a || ks == XK_A) {
			show_ana = !show_ana; need_draw = 1;
		} else if (ks == XK_space) {
			need_draw = 1;
		}
		break;
	}

	case ClientMessage:
		if ((Atom)ev->xclient.data.l[0] == wm_delete)
			running = 0;
		break;

	default:
		break;
	}
}

/*----------------------------------------------------------------------------*/

int
main(int argc, char **argv)
{
	XEvent	ev;
	int	i;

	(void)argc;
	(void)argv;

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xsunclock: cannot open display (is /dev/x running?)\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	cmap = DefaultColormap(dpy, scr);

	c_bg      = alloc_rgb(0xd8, 0xd8, 0xe0);
	c_text    = alloc_rgb(0x10, 0x10, 0x18);
	c_dim     = alloc_rgb(0x50, 0x50, 0x5a);
	c_border  = alloc_rgb(0x20, 0x20, 0x28);
	c_grat    = alloc_rgb(0x50, 0x68, 0x90);
	c_equator = alloc_rgb(0xa0, 0x80, 0x40);
	c_term    = alloc_rgb(0xff, 0xe0, 0x40);
	c_ana     = alloc_rgb(0xff, 0xff, 0xff);
	c_sun     = alloc_rgb(0xff, 0xd8, 0x20);
	c_sunray  = alloc_rgb(0xff, 0xa0, 0x00);
	c_city    = alloc_rgb(0xff, 0x40, 0x40);
	c_citytxt = alloc_rgb(0xf0, 0xf0, 0xf0);

	/* night -> day colour ramps for land and sea */
	for (i = 0; i < NSTEP; i++) {
		double	t = (double)i / (NSTEP - 1);
		land_pal[i] = alloc_blend(0x10, 0x20, 0x18, 0x3c, 0x8c, 0x48, t);
		sea_pal[i]  = alloc_blend(0x06, 0x0e, 0x26, 0x20, 0x50, 0x96, t);
	}

	xfont = XLoadQueryFont(dpy, FONT_NAME);
	if (xfont == NULL) {
		fprintf(stderr, "xsunclock: cannot load font \"%s\"\n", FONT_NAME);
		XCloseDisplay(dpy);
		return 1;
	}

	rasterize_land();
	layout();

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
			(DisplayWidth(dpy, scr) - win_w) / 2,
			(DisplayHeight(dpy, scr) - win_h) / 2,
			(unsigned)win_w, (unsigned)win_h, 0, c_border, c_bg);
	XStoreName(dpy, win, TITLE);

	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, xfont->fid);
	XSetGraphicsExposures(dpy, gc, False);

	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | KeyPressMask);

	XMapWindow(dpy, win);
	XSync(dpy, False);

	while (running) {
		time_t		now = time(NULL);
		struct tm	g = *gmtime(&now);
		int		sec = g.tm_sec, min = g.tm_min;

		while (XCheckWindowEvent(dpy, win,
				ExposureMask | StructureNotifyMask | KeyPressMask, &ev))
			handle_event(&ev);

		if (need_draw || min != last_min) {
			redraw_all();
			need_draw = 0;
		} else if (sec != last_sec) {
			draw_status(g_slat, g_slon);
		}
		last_sec = sec;
		last_min = min;
		proc_usleep(POLL_US);
	}

	XFreeGC(dpy, gc);
	XFreeFont(dpy, xfont);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
