/*
 * vcrow_skin.c: see vcrow_skin.h. plate, knobs and leds follow varc_skin.c, so the crow
 * sits next to the arc and grid as one family.
 */

#include <math.h>
#include <stdio.h>

#include "skin_theme.h"
#include "vcrow_skin.h"

#define FLAT (skin_style() == SKIN_STYLE_FLAT)

#define MARGIN 18.0
#define EDGE 24.0
#define COLW 124.0 // one input or output
#define GAP 28.0   // between the inputs and the outputs
#define PLATE_H 300.0
#define LABEL_Y 58.0 // in the plate
#define CENTER_Y 128.0
#define VALUE_Y 202.0
#define MODE_Y 221.0
#define SCOPE_Y 234.0
#define SCOPE_H 46.0
#define SCOPE_W 104.0
#define TRACK_R 44.0
#define KNOB_R 31.0
#define JACK_R 19.0
#define RING_R 33.0

#define VMIN -5.0
#define VMAX 10.0
// the input knob: -5 V at half past seven, +10 V at half past four, clockwise
#define A0 (0.75 * M_PI)
#define SWEEP (1.5 * M_PI)

static double plate_w(void) {
    return 2 * EDGE + 6 * COLW + GAP;
}

void vcrow_skin_units(int *w, int *h) {
    *w = (int)ceil(plate_w() + 2 * MARGIN);
    *h = (int)ceil(PLATE_H + 2 * MARGIN);
}

void vcrow_skin_geom(int w, int h, vcrow_geom_t *g) {
    int uw, uh;
    vcrow_skin_units(&uw, &uh);
    double sx = (double)w / uw, sy = (double)h / uh;
    g->s = sx < sy ? sx : sy;
    g->ox = (w - uw * g->s) / 2;
    g->oy = (h - uh * g->s) / 2;
}

// column i (inputs 0-1, outputs 2-5): its centre in units
static double col_x(int i) {
    return MARGIN + EDGE + i * COLW + (i >= 2 ? GAP : 0) + COLW / 2;
}

int vcrow_skin_hit(const vcrow_geom_t *g, double x, double y) {
    double ux = (x - g->ox) / g->s, uy = (y - g->oy) / g->s;
    if (uy < MARGIN + LABEL_Y - 20 || uy > MARGIN + SCOPE_Y + SCOPE_H + 8) {
        return -1;
    }
    for (int n = 0; n < 2; n++) {
        if (fabs(ux - col_x(n)) <= COLW / 2) {
            return n;
        }
    }
    return -1;
}

static double angle_of(double v) {
    v = v < VMIN ? VMIN : (v > VMAX ? VMAX : v);
    return A0 + (v - VMIN) / (VMAX - VMIN) * SWEEP;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void text(cairo_t *cr, const char *s, double cx, double y, double size, int bold) {
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t te;
    cairo_text_extents(cr, s, &te);
    cairo_move_to(cr, cx - te.width / 2 - te.x_bearing, y);
    cairo_show_text(cr, s);
    cairo_new_path(cr); // show_text leaves a current point the next arc would draw from
}

static void label_colour(cairo_t *cr, int strong) {
    if (FLAT) {
        flat_rgb(cr, strong ? FLAT_TEXT : FLAT_DIM, 1);
    } else {
        cairo_set_source_rgb(cr, strong ? 0.20 : 0.36, strong ? 0.21 : 0.37, strong ? 0.22 : 0.39);
    }
}

static void draw_plate(cairo_t *cr) {
    double w = plate_w(), h = PLATE_H;
    if (FLAT) {
        rounded_rect(cr, MARGIN, MARGIN, w, h, 14);
        flat_rgb(cr, FLAT_BODY, 1);
        cairo_fill(cr);
    } else {
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
    // the name, and a line between inputs and outputs
    label_colour(cr, 1);
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 15);
    cairo_move_to(cr, MARGIN + EDGE, MARGIN + 28);
    cairo_show_text(cr, "crow");
    label_colour(cr, 0);
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    cairo_move_to(cr, MARGIN + EDGE + 40, MARGIN + 28);
    cairo_show_text(cr, "virtual");
    cairo_new_path(cr);
    double dx = MARGIN + EDGE + 2 * COLW + GAP / 2;
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, dx, MARGIN + 44);
    cairo_line_to(cr, dx, MARGIN + PLATE_H - 16);
    if (FLAT) {
        flat_rgb(cr, FLAT_LINE, 1);
    } else {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
    }
    cairo_stroke(cr);
}

// amber for positive voltages, blue for negative; l 0-1
static void glow_colour(cairo_t *cr, double v, double a) {
    if (v >= 0) {
        cairo_set_source_rgba(cr, 1.0, 0.76, 0.10, a);
    } else {
        cairo_set_source_rgba(cr, 0.25, 0.60, 1.0, a);
    }
}

static void draw_input(cairo_t *cr, int n, double v, int held) {
    double cx = col_x(n), cy = MARGIN + CENTER_Y;
    double a = angle_of(v), z = angle_of(0);
    // the track, lit from 0 V to the value
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 6);
    cairo_arc(cr, cx, cy, TRACK_R, A0, A0 + SWEEP);
    if (FLAT) {
        flat_rgb(cr, FLAT_LINE, 1);
    } else {
        cairo_set_source_rgb(cr, 0.60, 0.61, 0.63);
    }
    cairo_stroke(cr);
    if (fabs(a - z) > 1e-3) {
        if (a > z) {
            cairo_arc(cr, cx, cy, TRACK_R, z, a);
        } else {
            cairo_arc(cr, cx, cy, TRACK_R, a, z);
        }
        if (FLAT) {
            flat_rgb(cr, FLAT_ACCENT, 1);
            cairo_stroke(cr);
        } else {
            cairo_path_t *path = cairo_copy_path(cr);
            cairo_set_line_width(cr, 14);
            glow_colour(cr, v, 0.25);
            cairo_stroke(cr);
            cairo_append_path(cr, path);
            cairo_set_line_width(cr, 5);
            glow_colour(cr, v, 1);
            cairo_stroke(cr);
            cairo_path_destroy(path);
        }
    }
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    // marks at -5, 0, +5, +10
    cairo_set_line_width(cr, 1.5);
    for (int m = -5; m <= 10; m += 5) {
        double t = angle_of(m);
        cairo_move_to(cr, cx + cos(t) * (TRACK_R + 6), cy + sin(t) * (TRACK_R + 6));
        cairo_line_to(cr, cx + cos(t) * (TRACK_R + 10), cy + sin(t) * (TRACK_R + 10));
    }
    label_colour(cr, 0);
    cairo_stroke(cr);

    // the knob, with a pointer
    double ky = cy + (held ? 1.2 : 0);
    if (FLAT) {
        cairo_arc(cr, cx, ky, KNOB_R, 0, 2 * M_PI);
        flat_rgb(cr, held ? FLAT_FACE_DOWN : FLAT_FACE, 1);
        cairo_fill(cr);
        cairo_arc(cr, cx + cos(a) * (KNOB_R - 9), ky + sin(a) * (KNOB_R - 9), 3.5, 0, 2 * M_PI);
        flat_rgb(cr, FLAT_TEXT, 1);
        cairo_fill(cr);
        return;
    }
    cairo_arc(cr, cx + 2, ky + 4, KNOB_R + 1, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
    cairo_fill(cr);
    cairo_arc(cr, cx, ky, KNOB_R, 0, 2 * M_PI);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, ky - KNOB_R, 0, ky + KNOB_R);
    cairo_pattern_add_color_stop_rgb(p, 0, 0.95, 0.955, 0.96);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.56, 0.575, 0.60);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    double top = KNOB_R - 6;
    cairo_arc(cr, cx, ky, top, 0, 2 * M_PI);
    p = cairo_pattern_create_radial(cx - 8, ky - 10, 2, cx, ky, top + 4);
    cairo_pattern_add_color_stop_rgb(p, 0, held ? 0.88 : 0.985, held ? 0.885 : 0.985, held ? 0.89 : 0.99);
    cairo_pattern_add_color_stop_rgb(p, 1, held ? 0.66 : 0.75, held ? 0.67 : 0.765, held ? 0.69 : 0.785);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 3);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, cx + cos(a) * 8, ky + sin(a) * 8);
    cairo_line_to(cr, cx + cos(a) * (top - 3), ky + sin(a) * (top - 3));
    cairo_set_source_rgb(cr, 0.25, 0.26, 0.28);
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
}

static void draw_output(cairo_t *cr, int n, double v) {
    double cx = col_x(2 + n), cy = MARGIN + CENTER_Y;
    double l = fabs(v) / (v >= 0 ? VMAX : -VMIN);
    l = l > 1 ? 1 : l;
    // the ring
    cairo_set_line_width(cr, 6);
    cairo_arc(cr, cx, cy, RING_R, 0, 2 * M_PI);
    if (FLAT) {
        flat_mix(cr, FLAT_LINE, FLAT_ACCENT, l);
        cairo_stroke(cr);
    } else {
        cairo_set_source_rgb(cr, 0.60, 0.61, 0.63);
        cairo_stroke(cr);
        if (l > 0.005) {
            cairo_pattern_t *gl = cairo_pattern_create_radial(cx, cy, RING_R - 12, cx, cy, RING_R + 14);
            double r = v >= 0 ? 1.0 : 0.25, gg = v >= 0 ? 0.76 : 0.60, b = v >= 0 ? 0.10 : 1.0;
            cairo_pattern_add_color_stop_rgba(gl, 0, r, gg, b, 0);
            cairo_pattern_add_color_stop_rgba(gl, 0.5, r, gg, b, 0.55 * l);
            cairo_pattern_add_color_stop_rgba(gl, 1, r, gg, b, 0);
            cairo_set_source(cr, gl);
            cairo_arc(cr, cx, cy, RING_R + 14, 0, 2 * M_PI);
            cairo_arc_negative(cr, cx, cy, RING_R - 12, 2 * M_PI, 0);
            cairo_fill(cr);
            cairo_pattern_destroy(gl);
            cairo_set_line_width(cr, 5);
            cairo_arc(cr, cx, cy, RING_R, 0, 2 * M_PI);
            glow_colour(cr, v, 0.25 + 0.75 * l);
            cairo_stroke(cr);
        }
    }
    // the jack: a nut round a hole
    if (FLAT) {
        cairo_arc(cr, cx, cy, JACK_R, 0, 2 * M_PI);
        flat_rgb(cr, FLAT_FACE, 1);
        cairo_fill(cr);
        cairo_arc(cr, cx, cy, JACK_R * 0.45, 0, 2 * M_PI);
        flat_rgb(cr, FLAT_BG, 1);
        cairo_fill(cr);
        return;
    }
    cairo_new_path(cr);
    for (int i = 0; i < 6; i++) {
        double t = M_PI / 6 + i * M_PI / 3;
        if (i == 0) {
            cairo_move_to(cr, cx + cos(t) * JACK_R, cy + sin(t) * JACK_R);
        } else {
            cairo_line_to(cr, cx + cos(t) * JACK_R, cy + sin(t) * JACK_R);
        }
    }
    cairo_close_path(cr);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, cy - JACK_R, 0, cy + JACK_R);
    cairo_pattern_add_color_stop_rgb(p, 0, 0.96, 0.965, 0.97);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.60, 0.615, 0.64);
    cairo_set_source(cr, p);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
    cairo_stroke(cr);
    cairo_arc(cr, cx, cy, JACK_R * 0.62, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.78, 0.79, 0.81);
    cairo_fill(cr);
    cairo_arc(cr, cx, cy, JACK_R * 0.45, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.09);
    cairo_fill(cr);
}

static void draw_scope(cairo_t *cr, int col, const float *lo, const float *hi) {
    double x = col_x(col) - SCOPE_W / 2, y = MARGIN + SCOPE_Y;
    rounded_rect(cr, x, y, SCOPE_W, SCOPE_H, 5);
    if (FLAT) {
        flat_rgb(cr, FLAT_BG, 1);
    } else {
        cairo_set_source_rgb(cr, 0.13, 0.135, 0.145);
    }
    cairo_fill(cr);
    double pad = 4, h = SCOPE_H - 2 * pad, w = SCOPE_W - 2 * pad;
#define VY(v) (y + pad + h - ((v) - VMIN) / (VMAX - VMIN) * h)
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x + pad, VY(0));
    cairo_line_to(cr, x + pad + w, VY(0));
    if (FLAT) {
        flat_rgb(cr, FLAT_LINE, 1);
    } else {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
    }
    cairo_stroke(cr);
    for (int k = 0; k < VCROW_SCOPE; k++) {
        double px = x + pad + w * k / (VCROW_SCOPE - 1);
        double a = hi[k] > VMAX ? VMAX : (hi[k] < VMIN ? VMIN : hi[k]);
        double b = lo[k] > VMAX ? VMAX : (lo[k] < VMIN ? VMIN : lo[k]);
        if (k == 0) {
            cairo_move_to(cr, px, VY(a));
        } else {
            cairo_line_to(cr, px, VY(a));
        }
        if (b != a) {
            cairo_line_to(cr, px, VY(b));
        }
    }
#undef VY
    cairo_set_line_width(cr, 1.4);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (FLAT) {
        flat_rgb(cr, FLAT_ACCENT, 1);
    } else {
        cairo_set_source_rgba(cr, 1.0, 0.80, 0.30, 0.95);
    }
    cairo_stroke(cr);
}

void vcrow_skin_draw(cairo_t *cr, const vcrow_geom_t *g, int w, int h, const vcrow_view_t *v, int held) {
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
    draw_plate(cr);
    char s[48];
    for (int c = 0; c < VCROW_CHANNELS; c++) {
        int is_in = c < VCROW_INPUTS;
        int n = is_in ? c : c - VCROW_INPUTS;
        double val = is_in ? v->in[n] : v->out[n];
        snprintf(s, sizeof s, is_in ? "IN %d" : "OUT %d", n + 1);
        label_colour(cr, 0);
        text(cr, s, col_x(c), MARGIN + LABEL_Y, 12, 1);
        if (is_in) {
            draw_input(cr, n, val, (held >> n) & 1);
        } else {
            draw_output(cr, n, val);
        }
        snprintf(s, sizeof s, "%+.2f V", fabs(val) < 0.005 ? 0.0 : val);
        label_colour(cr, 1);
        text(cr, s, col_x(c), MARGIN + VALUE_Y, 15, 1);
        if (is_in) {
            label_colour(cr, 0);
            text(cr, v->mode[n], col_x(c), MARGIN + MODE_Y, 11, 0);
        }
        draw_scope(cr, c, v->scope_min[c], v->scope_max[c]);
    }
    cairo_restore(cr);
}
