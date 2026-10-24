/*
 * varc_skin.c: see varc_skin.h. the leds and plate match the grid (vgrid_skin.c), so
 * both look like one family on the desk.
 */

#include <math.h>

#include "skin_theme.h"
#include "varc_skin.h"

#define FLAT (skin_style() == SKIN_STYLE_FLAT)

#define MARGIN 18.0
#define EDGE 30.0
#define PITCH 190.0  // ring centre to ring centre
#define RING_R 72.0  // led circle
#define OUTER_R 82.0 // ring plus its glow
#define LED_R 3.3
#define KNOB_R 55.0
#define KNURLS 72

static double plate_w(int rings) {
    return 2 * EDGE + 2 * OUTER_R + (rings - 1) * PITCH;
}

static double plate_h(void) {
    return 2 * EDGE + 2 * OUTER_R;
}

void varc_skin_units(int rings, int *w, int *h) {
    *w = (int)ceil(plate_w(rings) + 2 * MARGIN);
    *h = (int)ceil(plate_h() + 2 * MARGIN);
}

void varc_skin_geom(int w, int h, int rings, varc_geom_t *g) {
    int uw, uh;
    varc_skin_units(rings, &uw, &uh);
    g->rings = rings;
    double sx = (double)w / uw, sy = (double)h / uh;
    g->s = sx < sy ? sx : sy;
    g->ox = (w - uw * g->s) / 2;
    g->oy = (h - uh * g->s) / 2;
}

// ring centre in units
static double ring_x(int n) {
    return MARGIN + EDGE + OUTER_R + n * PITCH;
}
static double ring_y(void) {
    return MARGIN + EDGE + OUTER_R;
}

int varc_skin_hit(const varc_geom_t *g, double x, double y) {
    double ux = (x - g->ox) / g->s, uy = (y - g->oy) / g->s;
    for (int n = 0; n < g->rings; n++) {
        if (hypot(ux - ring_x(n), uy - ring_y()) <= OUTER_R + 8) {
            return n;
        }
    }
    return -1;
}

void varc_skin_center(const varc_geom_t *g, int n, double *x, double *y) {
    *x = g->ox + ring_x(n) * g->s;
    *y = g->oy + ring_y() * g->s;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void draw_plate(cairo_t *cr, int rings) {
    double w = plate_w(rings), h = plate_h();
    if (FLAT) {
        rounded_rect(cr, MARGIN, MARGIN, w, h, 14);
        flat_rgb(cr, FLAT_BODY, 1);
        cairo_fill(cr);
        return;
    }
    for (int i = 8; i >= 1; i--) {
        rounded_rect(cr, MARGIN - i * 0.8, MARGIN - i * 0.5 + 4, w + i * 1.6, h + i * 1.6, 14 + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
        cairo_fill(cr);
    }
    rounded_rect(cr, MARGIN, MARGIN, w, h, 14);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, MARGIN, 0, MARGIN + h);
    cairo_pattern_add_color_stop_rgb(p, 0, 0.905, 0.910, 0.918);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.835, 0.840, 0.850);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1.2);
    rounded_rect(cr, MARGIN + 0.6, MARGIN + 0.6, w - 1.2, h - 1.2, 13.4);
    p = cairo_pattern_create_linear(0, MARGIN, 0, MARGIN + h);
    cairo_pattern_add_color_stop_rgba(p, 0, 1, 1, 1, 0.9);
    cairo_pattern_add_color_stop_rgba(p, 0.5, 1, 1, 1, 0.1);
    cairo_pattern_add_color_stop_rgba(p, 1, 0, 0, 0, 0.25);
    cairo_set_source(cr, p);
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
}

static void draw_ring(cairo_t *cr, int n, const uint8_t *leds, double scale) {
    double cx = ring_x(n), cy = ring_y();
    if (FLAT) {
        for (int x = 0; x < VARC_LEDS; x++) {
            double t = -M_PI / 2 + x * 2 * M_PI / VARC_LEDS;
            cairo_arc(cr, cx + cos(t) * RING_R, cy + sin(t) * RING_R, LED_R, 0, 2 * M_PI);
            flat_mix(cr, FLAT_LINE, FLAT_ACCENT, leds[x] / 15.0 * scale);
            cairo_fill(cr);
        }
        return;
    }
    // glows first, so the holes next to a lit led do not cut them off
    for (int x = 0; x < VARC_LEDS; x++) {
        double l = leds[x] / 15.0 * scale;
        if (l <= 0) {
            continue;
        }
        double t = -M_PI / 2 + x * 2 * M_PI / VARC_LEDS;
        double lx = cx + cos(t) * RING_R, ly = cy + sin(t) * RING_R;
        cairo_pattern_t *gl = cairo_pattern_create_radial(lx, ly, LED_R * 0.5, lx, ly, LED_R * 4.2);
        cairo_pattern_add_color_stop_rgba(gl, 0, 1.0, 0.76, 0.10, 0.8 * l);
        cairo_pattern_add_color_stop_rgba(gl, 1, 1.0, 0.76, 0.10, 0);
        cairo_set_source(cr, gl);
        cairo_arc(cr, lx, ly, LED_R * 4.2, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(gl);
    }
    for (int x = 0; x < VARC_LEDS; x++) {
        double l = leds[x] / 15.0 * scale;
        double t = -M_PI / 2 + x * 2 * M_PI / VARC_LEDS;
        double lx = cx + cos(t) * RING_R, ly = cy + sin(t) * RING_R;
        // the hole in the plate, dark grey, towards a glowing yellow with its led
        cairo_arc(cr, lx, ly, LED_R, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 0.46 + (1.00 - 0.46) * l, 0.47 + (0.86 - 0.47) * l, 0.49 + (0.35 - 0.49) * l);
        cairo_fill(cr);
        if (l > 0) {
            cairo_arc(cr, lx, ly, LED_R * 0.55, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1, 0.98, 0.80, 0.9 * l);
            cairo_fill(cr);
        }
    }
}

static void draw_knob(cairo_t *cr, int n, double turns, int down) {
    double cx = ring_x(n), cy = ring_y() + (down ? 1.2 : 0);
    double r = KNOB_R, top = KNOB_R - 8;
    if (FLAT) {
        // a plain disc, a dot turning with it
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        flat_rgb(cr, down ? FLAT_FACE_DOWN : FLAT_FACE, 1);
        cairo_fill(cr);
        double t = -M_PI / 2 + turns * 2 * M_PI;
        cairo_arc(cr, cx + cos(t) * (r - 12), cy + sin(t) * (r - 12), 4, 0, 2 * M_PI);
        flat_rgb(cr, FLAT_TEXT, 1);
        cairo_fill(cr);
        return;
    }
    // shadow on the plate
    cairo_arc(cr, cx + 3, cy + 6, r + 1, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
    cairo_fill(cr);
    // machined side
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, cy - r, 0, cy + r);
    cairo_pattern_add_color_stop_rgb(p, 0, 0.95, 0.955, 0.96);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.56, 0.575, 0.60);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1.1);
    double a = turns * 2 * M_PI;
    for (int i = 0; i < KNURLS; i++) {
        double t = a + i * 2 * M_PI / KNURLS;
        cairo_move_to(cr, cx + cos(t) * (top + 1.5), cy + sin(t) * (top + 1.5));
        cairo_line_to(cr, cx + cos(t) * (r - 0.5), cy + sin(t) * (r - 0.5));
    }
    cairo_set_source_rgba(cr, 0, 0, 0, 0.13);
    cairo_stroke(cr);
    // top face, brushed in circles
    cairo_arc(cr, cx, cy, top, 0, 2 * M_PI);
    p = cairo_pattern_create_radial(cx - 14, cy - 18, 2, cx, cy, top + 6);
    cairo_pattern_add_color_stop_rgb(p, 0, down ? 0.88 : 0.985, down ? 0.885 : 0.985, down ? 0.89 : 0.99);
    cairo_pattern_add_color_stop_rgb(p, 1, down ? 0.66 : 0.75, down ? 0.67 : 0.765, down ? 0.69 : 0.785);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 0.6);
    for (double rr = 6; rr < top - 2; rr += 3.2) {
        cairo_arc(cr, cx, cy, rr, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        cairo_stroke(cr);
    }
    cairo_set_line_width(cr, 1);
    cairo_arc(cr, cx, cy, top, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    cairo_stroke(cr);
}

void varc_skin_draw(cairo_t *cr, const varc_geom_t *g, int w, int h,
                    const uint8_t ring[VARC_RINGS][VARC_LEDS], int intensity, const double angle[VARC_RINGS],
                    int pressed) {
    cairo_save(cr);
    if (FLAT) {
        flat_rgb(cr, FLAT_BG, 1);
    } else {
        cairo_set_source_rgb(cr, 0.10, 0.10, 0.11);
    }
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_translate(cr, g->ox, g->oy);
    cairo_scale(cr, g->s, g->s);
    draw_plate(cr, g->rings);
    double scale = (intensity < 0 ? 15 : intensity) / 15.0;
    for (int n = 0; n < g->rings; n++) {
        draw_ring(cr, n, ring[n], scale);
    }
    for (int n = 0; n < g->rings; n++) {
        draw_knob(cr, n, angle[n], (pressed >> n) & 1);
    }
    cairo_restore(cr);
}
