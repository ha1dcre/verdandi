/*
 * vgrid_skin.c: see vgrid_skin.h. proportions from a photo of a 128: key pitch 44 units,
 * keys 31 square, 26.5 from the plate edge to the first key.
 */

#include <math.h>
#include <stdio.h>

#include "skin_theme.h"
#include "vgrid_skin.h"

#define FLAT (skin_style() == SKIN_STYLE_FLAT)

const int vgrid_sizes[4][2] = {{0, 0}, {8, 8}, {16, 8}, {16, 16}};

#define PITCH 44.0
#define KEY 31.0
#define EDGE 26.5
#define MARGIN 18.0
#define BAR 50.0 // the size switch under the plate
#define SW_W 64.0
#define SW_H 26.0
#define SW_GAP 8.0

static double plate_w(int cols) {
    return 2 * EDGE + (cols - 1) * PITCH + KEY;
}

static double plate_h(int rows) {
    return 2 * EDGE + (rows - 1) * PITCH + KEY;
}

void vgrid_skin_units(int cols, int rows, int *w, int *h) {
    *w = (int)ceil(plate_w(cols) + 2 * MARGIN);
    *h = (int)ceil(plate_h(rows) + 2 * MARGIN + BAR);
}

void vgrid_skin_geom(int w, int h, int cols, int rows, vgrid_geom_t *g) {
    int uw, uh;
    vgrid_skin_units(cols, rows, &uw, &uh);
    g->cols = cols;
    g->rows = rows;
    double sx = (double)w / uw, sy = (double)h / uh;
    g->s = sx < sy ? sx : sy;
    g->ox = (w - uw * g->s) / 2;
    g->oy = (h - uh * g->s) / 2;
}

// top-left of key c,r in units
static double key_x(int c) {
    return MARGIN + EDGE + c * PITCH;
}
static double key_y(int r) {
    return MARGIN + EDGE + r * PITCH;
}

int vgrid_skin_hit(const vgrid_geom_t *g, double x, double y, int *col, int *row) {
    double ux = (x - g->ox) / g->s - MARGIN - EDGE + (PITCH - KEY) / 2;
    double uy = (y - g->oy) / g->s - MARGIN - EDGE + (PITCH - KEY) / 2;
    int c = (int)floor(ux / PITCH), r = (int)floor(uy / PITCH);
    if (ux < 0 || uy < 0 || c >= g->cols || r >= g->rows) {
        return 0;
    }
    *col = c;
    *row = r;
    return 1;
}

static double switch_x(const vgrid_geom_t *g, int i) {
    double total = 3 * SW_W + 2 * SW_GAP;
    return MARGIN + (plate_w(g->cols) - total) / 2 + (i - 1) * (SW_W + SW_GAP);
}
static double switch_y(const vgrid_geom_t *g) {
    return MARGIN + plate_h(g->rows) + (BAR - SW_H) / 2 + 4;
}

int vgrid_skin_hit_size(const vgrid_geom_t *g, double x, double y) {
    double ux = (x - g->ox) / g->s, uy = (y - g->oy) / g->s;
    double sy = switch_y(g);
    if (uy < sy - 3 || uy > sy + SW_H + 3) {
        return 0;
    }
    for (int i = 1; i <= 3; i++) {
        double sx = switch_x(g, i);
        if (ux >= sx - 3 && ux <= sx + SW_W + 3) {
            return i;
        }
    }
    return 0;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void draw_plate(cairo_t *cr, const vgrid_geom_t *g) {
    double w = plate_w(g->cols), h = plate_h(g->rows);
    if (FLAT) {
        rounded_rect(cr, MARGIN, MARGIN, w, h, 10);
        flat_rgb(cr, FLAT_BODY, 1);
        cairo_fill(cr);
        return;
    }
    // shadow on the desk
    for (int i = 8; i >= 1; i--) {
        rounded_rect(cr, MARGIN - i * 0.8, MARGIN - i * 0.5 + 4, w + i * 1.6, h + i * 1.6, 10 + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
        cairo_fill(cr);
    }
    rounded_rect(cr, MARGIN, MARGIN, w, h, 10);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, MARGIN, 0, MARGIN + h);
    cairo_pattern_add_color_stop_rgb(p, 0, 0.905, 0.910, 0.918);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.835, 0.840, 0.850);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    // edge: light from above
    cairo_set_line_width(cr, 1.2);
    rounded_rect(cr, MARGIN + 0.6, MARGIN + 0.6, w - 1.2, h - 1.2, 9.4);
    p = cairo_pattern_create_linear(0, MARGIN, 0, MARGIN + h);
    cairo_pattern_add_color_stop_rgba(p, 0, 1, 1, 1, 0.9);
    cairo_pattern_add_color_stop_rgba(p, 0.5, 1, 1, 1, 0.1);
    cairo_pattern_add_color_stop_rgba(p, 1, 0, 0, 0, 0.25);
    cairo_set_source(cr, p);
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
}

static void draw_keys(cairo_t *cr, const vgrid_geom_t *g, const uint8_t level[VGRID_MAX][VGRID_MAX], int intensity,
                      const uint8_t held[VGRID_MAX][VGRID_MAX]) {
    double scale = (intensity < 0 ? 15 : intensity) / 15.0;
    if (FLAT) {
        // a plain square per key, from dark towards amber with its level
        for (int r = 0; r < g->rows; r++) {
            for (int c = 0; c < g->cols; c++) {
                double x = key_x(c), y = key_y(r);
                double l = level[r][c] / 15.0 * scale;
                rounded_rect(cr, x, y, KEY, KEY, 4.2);
                if (l > 0) {
                    flat_mix(cr, FLAT_FACE, FLAT_ACCENT, l);
                } else {
                    flat_rgb(cr, held[r][c] & VGRID_HELD_MOUSE ? FLAT_FACE_DOWN : FLAT_FACE, 1);
                }
                cairo_fill(cr);
                if (held[r][c] & VGRID_HELD_LATCH) {
                    rounded_rect(cr, x - 3.5, y - 3.5, KEY + 7, KEY + 7, 7);
                    cairo_set_line_width(cr, 2);
                    flat_rgb(cr, FLAT_ACCENT, 1);
                    cairo_stroke(cr);
                }
            }
        }
        return;
    }
    // glows first, so neighbouring keys do not cut them off
    for (int r = 0; r < g->rows; r++) {
        for (int c = 0; c < g->cols; c++) {
            double l = level[r][c] / 15.0 * scale;
            if (l > 0) {
                double cx = key_x(c) + KEY / 2, cy = key_y(r) + KEY / 2;
                cairo_pattern_t *gl = cairo_pattern_create_radial(cx, cy, KEY * 0.3, cx, cy, KEY * 1.2);
                cairo_pattern_add_color_stop_rgba(gl, 0, 1.0, 0.76, 0.10, 0.75 * l);
                cairo_pattern_add_color_stop_rgba(gl, 1, 1.0, 0.76, 0.10, 0);
                cairo_set_source(cr, gl);
                cairo_rectangle(cr, cx - KEY * 1.25, cy - KEY * 1.25, KEY * 2.5, KEY * 2.5);
                cairo_fill(cr);
                cairo_pattern_destroy(gl);
            }
        }
    }
    for (int r = 0; r < g->rows; r++) {
        for (int c = 0; c < g->cols; c++) {
            double x = key_x(c), y = key_y(r);
            double l = level[r][c] / 15.0 * scale;
            int down = (held[r][c] & VGRID_HELD_MOUSE) != 0;
            // the cut-out in the plate: a dark seam, heavier at the top
            rounded_rect(cr, x - 1.1, y - 1.1, KEY + 2.2, KEY + 2.2, 5.0);
            cairo_pattern_t *p = cairo_pattern_create_linear(0, y - 1.1, 0, y + KEY + 1.1);
            cairo_pattern_add_color_stop_rgb(p, 0, 0.40, 0.41, 0.43);
            cairo_pattern_add_color_stop_rgb(p, 1, 0.60, 0.61, 0.63);
            cairo_set_source(cr, p);
            cairo_fill(cr);
            cairo_pattern_destroy(p);
            // the silicone key: light grey, towards a glowing yellow with its led
            double base = down ? 0.70 : 0.775;
            double rr = base + (1.00 - base) * l, gg = base + 0.005 + (0.80 - base) * l,
                   bb = base + 0.015 + (0.16 - base - 0.015) * l;
            double in = down ? 0.8 : 0;
            rounded_rect(cr, x + in, y + in, KEY - 2 * in, KEY - 2 * in, 4.2);
            p = cairo_pattern_create_linear(0, y, 0, y + KEY);
            cairo_pattern_add_color_stop_rgb(p, 0, fmin(rr + 0.03, 1), fmin(gg + 0.03, 1), fmin(bb + 0.03, 1));
            cairo_pattern_add_color_stop_rgb(p, 1, rr - 0.025, gg - 0.025, bb - 0.025);
            cairo_set_source(cr, p);
            cairo_fill(cr);
            cairo_pattern_destroy(p);
            if (l > 0) {
                // the led behind the silicone: brightest in the middle
                double cx = x + KEY / 2, cy = y + KEY / 2;
                p = cairo_pattern_create_radial(cx, cy, 0, cx, cy, KEY * 0.62);
                cairo_pattern_add_color_stop_rgba(p, 0, 1, 0.97, 0.62, 0.85 * l);
                cairo_pattern_add_color_stop_rgba(p, 1, 1, 0.92, 0.40, 0);
                cairo_set_source(cr, p);
                rounded_rect(cr, x + in, y + in, KEY - 2 * in, KEY - 2 * in, 4.2);
                cairo_fill(cr);
                cairo_pattern_destroy(p);
            }
            if (held[r][c] & VGRID_HELD_LATCH) {
                rounded_rect(cr, x - 3.5, y - 3.5, KEY + 7, KEY + 7, 7);
                cairo_set_line_width(cr, 2);
                cairo_set_source_rgba(cr, 0.95, 0.55, 0.12, 0.95);
                cairo_stroke(cr);
            }
        }
    }
}

static void draw_switch(cairo_t *cr, const vgrid_geom_t *g, int size) {
    static const char *labels[4] = {"", "64", "128", "256"};
    double y = switch_y(g);
    for (int i = 1; i <= 3; i++) {
        double x = switch_x(g, i);
        int on = i == size;
        rounded_rect(cr, x, y, SW_W, SW_H, 6);
        if (FLAT) {
            flat_rgb(cr, FLAT_FACE, 1);
            cairo_fill(cr);
            double lx = x + 13, ly = y + SW_H / 2;
            cairo_arc(cr, lx, ly, 3, 0, 2 * M_PI);
            flat_rgb(cr, on ? FLAT_ACCENT : FLAT_LINE, 1);
            cairo_fill(cr);
            cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 12);
            cairo_text_extents_t te;
            cairo_text_extents(cr, labels[i], &te);
            double area = x + 22, aw = SW_W - 26;
            cairo_move_to(cr, area + (aw - te.width) / 2 - te.x_bearing, ly - te.height / 2 - te.y_bearing);
            flat_rgb(cr, on ? FLAT_TEXT : FLAT_SOFT, 1);
            cairo_show_text(cr, labels[i]);
            continue;
        }
        cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + SW_H);
        cairo_pattern_add_color_stop_rgb(p, 0, 0.24, 0.25, 0.26);
        cairo_pattern_add_color_stop_rgb(p, 1, 0.14, 0.14, 0.15);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
        // led
        double lx = x + 13, ly = y + SW_H / 2;
        if (on) {
            p = cairo_pattern_create_radial(lx, ly, 0, lx, ly, 9);
            cairo_pattern_add_color_stop_rgba(p, 0, 1, 0.69, 0.29, 0.55);
            cairo_pattern_add_color_stop_rgba(p, 1, 1, 0.69, 0.29, 0);
            cairo_set_source(cr, p);
            cairo_arc(cr, lx, ly, 9, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(p);
        }
        cairo_arc(cr, lx, ly, 3, 0, 2 * M_PI);
        if (on) {
            cairo_set_source_rgb(cr, 1, 0.77, 0.42);
        } else {
            cairo_set_source_rgb(cr, 0.23, 0.24, 0.25);
        }
        cairo_fill(cr);
        cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, labels[i], &te);
        double area = x + 22, aw = SW_W - 26;
        cairo_move_to(cr, area + (aw - te.width) / 2 - te.x_bearing, ly - te.height / 2 - te.y_bearing);
        if (on) {
            cairo_set_source_rgb(cr, 1, 1, 1);
        } else {
            cairo_set_source_rgb(cr, 0.76, 0.78, 0.80);
        }
        cairo_show_text(cr, labels[i]);
    }
}

void vgrid_skin_draw(cairo_t *cr, const vgrid_geom_t *g, int w, int h,
                     const uint8_t level[VGRID_MAX][VGRID_MAX], int intensity,
                     const uint8_t held[VGRID_MAX][VGRID_MAX], int size) {
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
    draw_plate(cr, g);
    draw_keys(cr, g, level, intensity, held);
    draw_switch(cr, g, size);
    cairo_restore(cr);
}
