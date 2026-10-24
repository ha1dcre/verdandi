/*
 * vkeys_skin.c: see vkeys_skin.h
 */

#include <math.h>
#include <stdio.h>

#include "skin_theme.h"
#include "vkeys_skin.h"

#define FLAT (skin_style() == SKIN_STYLE_FLAT)

#define MARGIN 18.0
#define BODY_PAD 14.0
#define STRIP_H 54.0
#define WHITE_W 44.0
#define WHITE_H 190.0
#define BLACK_W 27.0
#define BLACK_H 118.0
#define WHITES 15 // C D E F G A B x2, and the top C
#define CTRL_W 46.0
#define CTRL_H 30.0
#define LATCH_W 92.0
#define LABEL_W 78.0

// semitone -> white key index within the octave, or -1 for a black key
static const int white_of[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
// black key -> the white key it sits after
static const int black_after[12] = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1};

static double body_w(void) {
    return WHITES * WHITE_W + 2 * BODY_PAD;
}
static double body_h(void) {
    return STRIP_H + WHITE_H + 2 * BODY_PAD;
}

void vkeys_skin_units(int *w, int *h) {
    *w = (int)ceil(body_w() + 2 * MARGIN);
    *h = (int)ceil(body_h() + 2 * MARGIN);
}

void vkeys_skin_geom(int w, int h, vkeys_geom_t *g) {
    int uw, uh;
    vkeys_skin_units(&uw, &uh);
    double sx = (double)w / uw, sy = (double)h / uh;
    g->s = sx < sy ? sx : sy;
    g->ox = (w - uw * g->s) / 2;
    g->oy = (h - uh * g->s) / 2;
}

static double keys_x(void) {
    return MARGIN + BODY_PAD;
}
static double keys_y(void) {
    return MARGIN + BODY_PAD + STRIP_H;
}

static int is_black(int key) {
    return white_of[key % 12] < 0;
}

// left edge of key i, in units
static double key_left(int key) {
    int oct = key / 12, semi = key % 12;
    if (is_black(key)) {
        return keys_x() + (oct * 7 + black_after[semi] + 1) * WHITE_W - BLACK_W / 2;
    }
    return keys_x() + (oct * 7 + white_of[semi]) * WHITE_W;
}

// strip: [-] OCT n [+]   [-] VEL n [+]   ...   [LATCH]
typedef struct {
    int id;
    double x, w;
} ctrl_t;

static int controls(ctrl_t *c) {
    double x0 = keys_x();
    int n = 0;
    c[n++] = (ctrl_t){VK_OCT_DOWN, x0, CTRL_W};
    c[n++] = (ctrl_t){VK_OCT_UP, x0 + CTRL_W + LABEL_W, CTRL_W};
    double v = x0 + 2 * CTRL_W + LABEL_W + 30;
    c[n++] = (ctrl_t){VK_VEL_DOWN, v, CTRL_W};
    c[n++] = (ctrl_t){VK_VEL_UP, v + CTRL_W + LABEL_W, CTRL_W};
    c[n++] = (ctrl_t){VK_LATCH, keys_x() + WHITES * WHITE_W - LATCH_W, LATCH_W};
    return n;
}

static double ctrl_y(void) {
    return MARGIN + BODY_PAD + (STRIP_H - 10 - CTRL_H) / 2;
}

int vkeys_skin_hit(const vkeys_geom_t *g, double x, double y, int *key, int *velocity) {
    double ux = (x - g->ox) / g->s, uy = (y - g->oy) / g->s;
    ctrl_t c[8];
    int n = controls(c);
    double cy = ctrl_y();
    if (uy >= cy - 3 && uy <= cy + CTRL_H + 3) {
        for (int i = 0; i < n; i++) {
            if (ux >= c[i].x - 3 && ux <= c[i].x + c[i].w + 3) {
                return c[i].id;
            }
        }
    }
    double ky = uy - keys_y();
    if (ky < 0 || ky > WHITE_H || ux < keys_x() || ux >= keys_x() + WHITES * WHITE_W) {
        return VK_NONE;
    }
    // black keys lie on top
    if (ky <= BLACK_H) {
        for (int k = 0; k < VKEYS_N; k++) {
            if (is_black(k) && ux >= key_left(k) && ux < key_left(k) + BLACK_W) {
                *key = k;
                *velocity = 30 + (int)lround(97 * ky / BLACK_H);
                return VK_KEY;
            }
        }
    }
    for (int k = 0; k < VKEYS_N; k++) {
        if (!is_black(k) && ux >= key_left(k) && ux < key_left(k) + WHITE_W) {
            *key = k;
            *velocity = 30 + (int)lround(97 * ky / WHITE_H);
            return VK_KEY;
        }
    }
    return VK_NONE;
}

int vkeys_note(const vkeys_state_t *st, int key) {
    return 12 * (st->octave + 1) + key;
}

// A W S E D F T G Y H U J K O L P ; '  (usages: letters a=4.., ;=51 '=52)
static const int typed_usage[18] = {4, 26, 22, 8, 7, 9, 23, 10, 28, 11, 24, 13, 14, 18, 15, 19, 51, 52};

int vkeys_typed(int usage, int *control) {
    *control = VK_NONE;
    switch (usage) {
    case 29: // z
        *control = VK_OCT_DOWN;
        return -1;
    case 27: // x
        *control = VK_OCT_UP;
        return -1;
    case 6: // c
        *control = VK_VEL_DOWN;
        return -1;
    case 25: // v
        *control = VK_VEL_UP;
        return -1;
    default:
        break;
    }
    for (int i = 0; i < 18; i++) {
        if (typed_usage[i] == usage) {
            return i;
        }
    }
    return -1;
}

int vkeys_typed_usage(int key) {
    return key >= 0 && key < 18 ? typed_usage[key] : -1;
}

//--- drawing

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// only the bottom corners rounded
static void key_shape(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + w, y);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_close_path(cr);
}

static void draw_body(cairo_t *cr) {
    double w = body_w(), h = body_h();
    if (FLAT) {
        rounded_rect(cr, MARGIN, MARGIN, w, h, 14);
        flat_rgb(cr, FLAT_BODY, 1);
        cairo_fill(cr);
        rounded_rect(cr, keys_x() - 3, keys_y() - 3, WHITES * WHITE_W + 6, WHITE_H + 6, 5);
        flat_rgb(cr, FLAT_BG, 1);
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
    cairo_pattern_add_color_stop_rgb(p, 0, 0.875, 0.882, 0.894);
    cairo_pattern_add_color_stop_rgb(p, 1, 0.769, 0.780, 0.796);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1.2);
    rounded_rect(cr, MARGIN + 0.6, MARGIN + 0.6, w - 1.2, h - 1.2, 13.4);
    p = cairo_pattern_create_linear(0, MARGIN, 0, MARGIN + h);
    cairo_pattern_add_color_stop_rgba(p, 0, 1, 1, 1, 0.9);
    cairo_pattern_add_color_stop_rgba(p, 0.5, 1, 1, 1, 0.1);
    cairo_pattern_add_color_stop_rgba(p, 1, 0, 0, 0, 0.3);
    cairo_set_source(cr, p);
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
    // the slot the keys sit in
    rounded_rect(cr, keys_x() - 3, keys_y() - 3, WHITES * WHITE_W + 6, WHITE_H + 6, 5);
    cairo_set_source_rgb(cr, 0.09, 0.09, 0.10);
    cairo_fill(cr);
}

static void text_in(cairo_t *cr, const char *s, double x, double w, double mid, double size, double r, double gr,
                    double b) {
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t te;
    cairo_text_extents(cr, s, &te);
    cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, mid - te.height / 2 - te.y_bearing);
    cairo_set_source_rgb(cr, r, gr, b);
    cairo_show_text(cr, s);
}

static void draw_control(cairo_t *cr, const ctrl_t *c, const vkeys_state_t *st) {
    double y = ctrl_y();
    int down = st->button_down == c->id;
    double dy = down ? 1.2 : 0;
    if (FLAT) {
        dy = 0;
        rounded_rect(cr, c->x, y, c->w, CTRL_H, 7);
        flat_rgb(cr, down ? FLAT_FACE_DOWN : st->hover == c->id ? FLAT_FACE_UP
                                                                : FLAT_FACE,
                 1);
        cairo_fill(cr);
    } else {
        rounded_rect(cr, c->x - 2.5, y - 2.5, c->w + 5, CTRL_H + 5, 9);
        cairo_pattern_t *p = cairo_pattern_create_linear(0, y - 2.5, 0, y + CTRL_H + 2.5);
        cairo_pattern_add_color_stop_rgb(p, 0, 0.557, 0.569, 0.588);
        cairo_pattern_add_color_stop_rgb(p, 1, 0.890, 0.898, 0.910);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
        rounded_rect(cr, c->x, y + dy, c->w, CTRL_H, 7);
        p = cairo_pattern_create_linear(0, y + dy, 0, y + dy + CTRL_H);
        cairo_pattern_add_color_stop_rgb(p, 0, down ? 0.137 : 0.239, down ? 0.141 : 0.247, down ? 0.153 : 0.263);
        cairo_pattern_add_color_stop_rgb(p, 1, down ? 0.090 : 0.137, down ? 0.094 : 0.141, down ? 0.102 : 0.153);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
        if (st->hover == c->id && !down) {
            rounded_rect(cr, c->x, y, c->w, CTRL_H, 7);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_fill(cr);
        }
    }
    double mid = y + dy + CTRL_H / 2;
    if (c->id == VK_LATCH) {
        double lx = c->x + 15;
        if (st->latch && !FLAT) {
            cairo_pattern_t *gl = cairo_pattern_create_radial(lx, mid, 0, lx, mid, 10);
            cairo_pattern_add_color_stop_rgba(gl, 0, 1, 0.69, 0.29, 0.55);
            cairo_pattern_add_color_stop_rgba(gl, 1, 1, 0.69, 0.29, 0);
            cairo_set_source(cr, gl);
            cairo_arc(cr, lx, mid, 10, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(gl);
        }
        cairo_arc(cr, lx, mid, 3.4, 0, 2 * M_PI);
        if (FLAT) {
            flat_rgb(cr, st->latch ? FLAT_ACCENT : FLAT_LINE, 1);
        } else if (st->latch) {
            cairo_set_source_rgb(cr, 1, 0.77, 0.42);
        } else {
            cairo_set_source_rgb(cr, 0.23, 0.24, 0.25);
        }
        cairo_fill(cr);
        double l = FLAT ? (st->latch ? 0.83 : 0.60) : st->latch ? 1
                                                                : 0.765;
        text_in(cr, "LATCH", c->x + 24, c->w - 28, mid, 12, l, l, l);
        return;
    }
    // minus / plus
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    double cx = c->x + c->w / 2;
    cairo_move_to(cr, cx - 6, mid);
    cairo_line_to(cr, cx + 6, mid);
    if (c->id == VK_OCT_UP || c->id == VK_VEL_UP) {
        cairo_move_to(cr, cx, mid - 6);
        cairo_line_to(cr, cx, mid + 6);
    }
    if (FLAT) {
        flat_rgb(cr, FLAT_SOFT, 1);
    } else {
        cairo_set_source_rgb(cr, 0.765, 0.776, 0.796);
    }
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
}

// engraved label between two controls
static void draw_value(cairo_t *cr, const char *top, const char *value, double x) {
    double y = ctrl_y();
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    cairo_text_extents_t te;
    cairo_text_extents(cr, top, &te);
    cairo_move_to(cr, x + (LABEL_W - te.width) / 2 - te.x_bearing, y + 9);
    if (FLAT) {
        flat_rgb(cr, FLAT_DIM, 1);
        cairo_show_text(cr, top);
        text_in(cr, value, x, LABEL_W, y + CTRL_H / 2 + 6, 15, 0.83, 0.84, 0.85);
        return;
    }
    cairo_set_source_rgba(cr, 0.29, 0.30, 0.32, 0.8);
    cairo_show_text(cr, top);
    text_in(cr, value, x, LABEL_W, y + CTRL_H / 2 + 6, 15, 0.20, 0.21, 0.23);
}

static void note_name(int note, char *buf, size_t n) {
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    snprintf(buf, n, "%s%d", names[note % 12], note / 12 - 1);
}

static void draw_keys(cairo_t *cr, const vkeys_state_t *st) {
    double y = keys_y();
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < VKEYS_N; k++) {
            int black = is_black(k);
            if (black != pass) {
                continue;
            }
            double x = key_left(k), w = black ? BLACK_W : WHITE_W, h = black ? BLACK_H : WHITE_H;
            int on = st->held[k] != 0;
            int hover = st->hover == VK_KEY + k && !on;
            if (FLAT) {
                // plain keys: grey and near black, amber while held
                if (black) {
                    key_shape(cr, x, y, w, h, 3);
                    flat_rgb(cr, on ? FLAT_ACCENT : hover ? FLAT_FACE
                                                          : 0x0f0f11,
                             1);
                } else {
                    key_shape(cr, x + 0.8, y, w - 1.6, h - 1, 4);
                    flat_rgb(cr, on ? FLAT_ACCENT : hover ? 0x3a3a40
                                                          : 0x2e2e33,
                             1);
                }
                cairo_fill(cr);
                if (!black && k % 12 == 0) {
                    char name[8];
                    note_name(vkeys_note(st, k), name, sizeof name);
                    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, 11);
                    cairo_text_extents_t te;
                    cairo_text_extents(cr, name, &te);
                    cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, y + h - 12);
                    flat_rgb(cr, on ? FLAT_BODY : FLAT_SOFT, 1);
                    cairo_show_text(cr, name);
                }
                const char *label = st->typed_label[k];
                if (label != NULL) {
                    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                    cairo_set_font_size(cr, 10);
                    cairo_text_extents_t te;
                    cairo_text_extents(cr, label, &te);
                    cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, black ? y + h - 10 : y + h - 30);
                    flat_rgb(cr, on ? FLAT_BODY : FLAT_DIM, 1);
                    cairo_show_text(cr, label);
                }
                continue;
            }
            if (!black) {
                key_shape(cr, x + 0.8, y, w - 1.6, h - 1, 4);
                cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + h);
                if (on) {
                    cairo_pattern_add_color_stop_rgb(p, 0, 1.0, 0.86, 0.55);
                    cairo_pattern_add_color_stop_rgb(p, 1, 1.0, 0.74, 0.30);
                } else {
                    cairo_pattern_add_color_stop_rgb(p, 0, 0.93, 0.93, 0.92);
                    cairo_pattern_add_color_stop_rgb(p, 0.85, 0.985, 0.985, 0.98);
                    cairo_pattern_add_color_stop_rgb(p, 1, 0.86, 0.86, 0.85);
                }
                cairo_set_source(cr, p);
                cairo_fill(cr);
                cairo_pattern_destroy(p);
                if (st->hover == VK_KEY + k && !on) {
                    key_shape(cr, x + 0.8, y, w - 1.6, h - 1, 4);
                    cairo_set_source_rgba(cr, 0, 0, 0, 0.04);
                    cairo_fill(cr);
                }
                if (k % 12 == 0) {
                    char name[8];
                    note_name(vkeys_note(st, k), name, sizeof name);
                    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                    cairo_set_font_size(cr, 11);
                    cairo_text_extents_t te;
                    cairo_text_extents(cr, name, &te);
                    cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, y + h - 12);
                    cairo_set_source_rgba(cr, 0.35, 0.36, 0.38, 0.9);
                    cairo_show_text(cr, name);
                }
            } else {
                key_shape(cr, x, y, w, h, 3);
                cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + h);
                if (on) {
                    cairo_pattern_add_color_stop_rgb(p, 0, 0.55, 0.36, 0.10);
                    cairo_pattern_add_color_stop_rgb(p, 1, 0.85, 0.55, 0.16);
                } else {
                    cairo_pattern_add_color_stop_rgb(p, 0, 0.10, 0.10, 0.11);
                    cairo_pattern_add_color_stop_rgb(p, 0.9, 0.20, 0.20, 0.22);
                    cairo_pattern_add_color_stop_rgb(p, 1, 0.08, 0.08, 0.09);
                }
                cairo_set_source(cr, p);
                cairo_fill(cr);
                cairo_pattern_destroy(p);
                // glossy top
                key_shape(cr, x + 3, y, w - 6, h - 12, 2);
                cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.10 : 0.07);
                cairo_fill(cr);
                if (st->hover == VK_KEY + k && !on) {
                    key_shape(cr, x, y, w, h, 3);
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
                    cairo_fill(cr);
                }
            }
            const char *label = st->typed_label[k];
            if (label != NULL) {
                cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
                cairo_set_font_size(cr, 10);
                cairo_text_extents_t te;
                cairo_text_extents(cr, label, &te);
                double ty = black ? y + h - 10 : y + h - 30;
                cairo_move_to(cr, x + (w - te.width) / 2 - te.x_bearing, ty);
                if (black) {
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
                } else {
                    cairo_set_source_rgba(cr, 0.35, 0.36, 0.38, 0.55);
                }
                cairo_show_text(cr, label);
            }
        }
    }
    if (FLAT) {
        return;
    }
    // shade under the strip edge
    cairo_rectangle(cr, keys_x(), y, WHITES * WHITE_W, 5);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + 5);
    cairo_pattern_add_color_stop_rgba(p, 0, 0, 0, 0, 0.35);
    cairo_pattern_add_color_stop_rgba(p, 1, 0, 0, 0, 0);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

void vkeys_skin_draw(cairo_t *cr, const vkeys_geom_t *g, int w, int h, const vkeys_state_t *st) {
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
    draw_body(cr);
    ctrl_t c[8];
    int n = controls(c);
    for (int i = 0; i < n; i++) {
        draw_control(cr, &c[i], st);
    }
    char buf[16];
    note_name(vkeys_note(st, 0), buf, sizeof buf);
    draw_value(cr, "OCTAVE", buf, c[0].x + CTRL_W);
    snprintf(buf, sizeof buf, "%d", st->velocity);
    draw_value(cr, "VELOCITY", buf, c[2].x + CTRL_W);
    draw_keys(cr, st);
    cairo_restore(cr);
}
