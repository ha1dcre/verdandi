/*
 * device_skin.c: see device_skin.h
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "device_skin.h"
#include "skin_theme.h"

#define FLAT (skin_style() == SKIN_STYLE_FLAT)

typedef struct {
    double x, y;
} pt_t;

// standard norns, in layout units
static const pt_t key_pos[3] = {{109, 252}, {594, 542}, {749, 542}};
static const pt_t enc_pos[3] = {{216, 252}, {672, 420}, {827, 420}};
static const double key_r = 31;
static const double enc_r = 38; // washer; the knob itself is 31
static const double bezel_x = 77, bezel_y = 356, bezel_w = 417, bezel_h = 220;
// the OUT fader and MUTE, right of E3
static const double fader_x = 913, fader_top = 196, fader_bottom = 486;
static const double fader_cap_w = 34, fader_cap_h = 18;
static const double mute_x = 886, mute_y = 520, mute_w = 54, mute_h = 34;
static const double bezel_pad = 14;

// the button row along the top: menu pages on the left, tools on the right
typedef struct {
    double x;
    const char *label;
} button_t;
static const button_t buttons[] = {
    {77, "MIX"}, {153, "TAPE"}, {229, "HOME"}, {305, "PARAMS"}, // SKIN_PAGE1..4
    {398, "MIC"},
    {474, "GRID"},
    {550, "ARC"},
    {626, "KEYS"}, // SKIN_MIC..KEYS
    {719, "MAIDEN"},
    {795, "MANUAL"}, // SKIN_MAIDEN, MANUAL
};
#define NBUTTONS ((int)(sizeof buttons / sizeof buttons[0]))
static const double button_y = 70, button_w = 70, button_h = 40;

// the knobs are endless encoders: no pointer, only the knurl moves. one detent turns it by
// a third of the knurl spacing, so it always visibly moves the right way
#define KNURLS 60
#define KNURL_STEP (2 * M_PI / KNURLS / 3)

void skin_geom(int w, int h, skin_geom_t *g) {
    g->w = w;
    g->h = h;
    double sx = (double)w / (SKIN_W + 2 * SKIN_MARGIN);
    double sy = (double)h / (SKIN_H + 2 * SKIN_MARGIN);
    g->s = sx < sy ? sx : sy;
    g->ox = (w - SKIN_W * g->s) / 2;
    g->oy = (h - SKIN_H * g->s) / 2;
    double fit_w = (bezel_w - 2 * bezel_pad) * g->s / 128;
    double fit_h = (bezel_h - 2 * bezel_pad) * g->s / 64;
    int px = (int)floor(fit_w < fit_h ? fit_w : fit_h);
    g->oled_px = px < 1 ? 1 : px;
    g->oled_x = (int)lround(g->ox + (bezel_x + bezel_w / 2) * g->s - 64 * g->oled_px);
    g->oled_y = (int)lround(g->oy + (bezel_y + bezel_h / 2) * g->s - 32 * g->oled_px);
}

static pt_t to_units(const skin_geom_t *g, double x, double y) {
    pt_t p = {(x - g->ox) / g->s, (y - g->oy) / g->s};
    return p;
}

static double dist(pt_t a, pt_t b) {
    return hypot(a.x - b.x, a.y - b.y);
}

int skin_hit(const skin_geom_t *g, double x, double y) {
    pt_t p = to_units(g, x, y);
    for (int i = 0; i < NBUTTONS; i++) {
        if (p.x >= buttons[i].x - 3 && p.x <= buttons[i].x + button_w + 3 && p.y >= button_y - 3 &&
            p.y <= button_y + button_h + 3) {
            return SKIN_PAGE1 + i;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (dist(p, key_pos[i]) <= key_r + 6) {
            return SKIN_K1 + i;
        }
        if (dist(p, enc_pos[i]) <= enc_r + 6) {
            return SKIN_E1 + i;
        }
    }
    if (fabs(p.x - fader_x) <= fader_cap_w / 2 + 6 && p.y >= fader_top - fader_cap_h && p.y <= fader_bottom + fader_cap_h) {
        return SKIN_FADER;
    }
    if (p.x >= mute_x - 3 && p.x <= mute_x + mute_w + 3 && p.y >= mute_y - 3 && p.y <= mute_y + mute_h + 3) {
        return SKIN_MUTE;
    }
    return SKIN_NONE;
}

double skin_fader_value(const skin_geom_t *g, double y) {
    double v = (fader_bottom - (y - g->oy) / g->s) / (fader_bottom - fader_top);
    return v < 0 ? 0 : v > 1 ? 1
                             : v;
}

int skin_oled_pixel(const skin_geom_t *g, double x, double y, int *ox, int *oy) {
    int px = (int)floor((x - g->oled_x) / g->oled_px), py = (int)floor((y - g->oled_y) / g->oled_px);
    if (px < 0 || px > 127 || py < 0 || py > 63) {
        return 0;
    }
    *ox = px;
    *oy = py;
    return 1;
}

int skin_nearest_enc(const skin_geom_t *g, double x, double y) {
    pt_t p = to_units(g, x, y);
    int best = 0;
    for (int i = 1; i < 3; i++) {
        if (dist(p, enc_pos[i]) < dist(p, enc_pos[best])) {
            best = i;
        }
    }
    return best + 1;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void circle(cairo_t *cr, double x, double y, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
}

static void hex(cairo_t *cr, const char *rgb, double a) {
    unsigned v = 0;
    for (const char *c = rgb; *c; c++) {
        v = v * 16 + (unsigned)(*c <= '9' ? *c - '0' : (*c | 0x20) - 'a' + 10);
    }
    cairo_set_source_rgba(cr, ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0, a);
}

static void stop(cairo_pattern_t *p, double at, const char *rgb, double a) {
    unsigned v = 0;
    for (const char *c = rgb; *c; c++) {
        v = v * 16 + (unsigned)(*c <= '9' ? *c - '0' : (*c | 0x20) - 'a' + 10);
    }
    cairo_pattern_add_color_stop_rgba(p, at, ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0,
                                      a);
}

// small deterministic noise for the brushed finish
static uint32_t lcg(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void draw_box(cairo_t *cr, const skin_geom_t *g) {
    const double r = 22;

    // soft shadow under the box
    for (int i = 10; i >= 1; i--) {
        rounded_rect(cr, -i * 0.9, -i * 0.6 + 6, SKIN_W + i * 1.8, SKIN_H + i * 1.8, r + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.045);
        cairo_fill(cr);
    }

    rounded_rect(cr, 0, 0, SKIN_W, SKIN_H, r);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, 0, SKIN_H);
    stop(p, 0, "dfe1e4", 1);
    stop(p, 1, "c4c7cb", 1);
    cairo_set_source(cr, p);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(p);

    // brushed aluminium: one faint streak per device pixel row
    cairo_save(cr);
    cairo_clip(cr);
    cairo_identity_matrix(cr);
    uint32_t seed = 0x6e6f726e;
    int y0 = (int)floor(g->oy), y1 = (int)ceil(g->oy + SKIN_H * g->s);
    double x0 = g->ox, x1 = g->ox + SKIN_W * g->s;
    cairo_set_line_width(cr, 1);
    for (int y = y0; y < y1; y++) {
        double a = (lcg(&seed) % 1000) / 1000.0 * 0.07;
        if (lcg(&seed) & 1) {
            cairo_set_source_rgba(cr, 1, 1, 1, a);
        } else {
            cairo_set_source_rgba(cr, 0, 0, 0, a * 0.6);
        }
        cairo_move_to(cr, x0, y + 0.5);
        cairo_line_to(cr, x1, y + 0.5);
        cairo_stroke(cr);
    }
    cairo_restore(cr);

    // edges: light from above
    cairo_set_line_width(cr, 1.5);
    rounded_rect(cr, 0.75, 0.75, SKIN_W - 1.5, SKIN_H - 1.5, r - 0.75);
    p = cairo_pattern_create_linear(0, 0, 0, SKIN_H);
    stop(p, 0, "ffffff", 0.9);
    stop(p, 0.5, "ffffff", 0.15);
    stop(p, 1, "000000", 0.35);
    cairo_set_source(cr, p);
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
}

static void draw_bezel(cairo_t *cr) {
    // the screen sits in a recess: dark lip above, light lip below
    rounded_rect(cr, bezel_x - 3, bezel_y - 3, bezel_w + 6, bezel_h + 6, 9);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, bezel_y - 3, 0, bezel_y + bezel_h + 3);
    stop(p, 0, "000000", 0.45);
    stop(p, 1, "ffffff", 0.85);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);

    // the whole window black, as the oled's own black: no frame around the pixels
    rounded_rect(cr, bezel_x, bezel_y, bezel_w, bezel_h, 7);
    hex(cr, "000000", 1);
    cairo_fill(cr);
}

static void draw_label(cairo_t *cr, pt_t c, double r, const char *text) {
    cairo_save(cr);
    double d = r + 5;
    cairo_translate(cr, c.x + d * 0.70, c.y + d * 0.70);
    cairo_rotate(cr, M_PI / 4);
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 17);
    // engraved: highlight below, dark on top
    cairo_move_to(cr, 0.6, 1.4);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
    cairo_show_text(cr, text);
    cairo_move_to(cr, 0, 0);
    hex(cr, "4a4d52", 0.75);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

static void draw_washer(cairo_t *cr, pt_t c) {
    circle(cr, c.x, c.y, enc_r);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, c.y - enc_r, 0, c.y + enc_r);
    stop(p, 0, "a3a6ab", 1);
    stop(p, 1, "d3d5d8", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1);
    circle(cr, c.x, c.y, enc_r - 0.5);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
    cairo_stroke(cr);
}

//--- dark flat: the same places, drawn as plain shapes

// the control's name centred under it
static void flat_label(cairo_t *cr, pt_t c, double r, const char *text) {
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_text_extents_t te;
    cairo_text_extents(cr, text, &te);
    cairo_move_to(cr, c.x - te.width / 2 - te.x_bearing, c.y + r + 22);
    flat_rgb(cr, FLAT_DIM, 1);
    cairo_show_text(cr, text);
}

static void flat_static(cairo_t *cr, const skin_geom_t *g) {
    flat_rgb(cr, FLAT_BG, 1);
    cairo_paint(cr);
    cairo_translate(cr, g->ox, g->oy);
    cairo_scale(cr, g->s, g->s);
    rounded_rect(cr, 0, 0, SKIN_W, SKIN_H, 22);
    flat_rgb(cr, FLAT_BODY, 1);
    cairo_fill(cr);
    // the screen: black, a hairline round it (outside the part the oled redraws)
    rounded_rect(cr, bezel_x, bezel_y, bezel_w, bezel_h, 7);
    hex(cr, "000000", 1);
    cairo_fill(cr);
    rounded_rect(cr, bezel_x - 2.5, bezel_y - 2.5, bezel_w + 5, bezel_h + 5, 9);
    cairo_set_line_width(cr, 1.5);
    flat_rgb(cr, FLAT_LINE, 1);
    cairo_stroke(cr);
    static const char *names[3][2] = {{"K1", "E1"}, {"K2", "E2"}, {"K3", "E3"}};
    for (int i = 0; i < 3; i++) {
        flat_label(cr, key_pos[i], key_r, names[i][0]);
        flat_label(cr, enc_pos[i], enc_r, names[i][1]);
    }
}

// a round key: lit while held
static void flat_key(cairo_t *cr, pt_t c, int down, int hover) {
    circle(cr, c.x, c.y, 25);
    flat_rgb(cr, down ? FLAT_ACCENT : hover ? FLAT_FACE_UP
                                            : FLAT_FACE,
             1);
    cairo_fill(cr);
}

// an endless knob: a dot walks round it with the turns
#define FLAT_DOT_STEP (2 * M_PI / 64)
static void flat_knob(cairo_t *cr, pt_t c, int ticks, int hover) {
    circle(cr, c.x, c.y, 31);
    flat_rgb(cr, hover ? FLAT_FACE_UP : FLAT_FACE, 1);
    cairo_fill(cr);
    double a = -M_PI / 2 + ticks * FLAT_DOT_STEP;
    circle(cr, c.x + cos(a) * 22, c.y + sin(a) * 22, 3.4);
    flat_rgb(cr, FLAT_TEXT, 1);
    cairo_fill(cr);
}

void skin_draw_static(cairo_t *cr, const skin_geom_t *g) {
    cairo_save(cr);
    if (FLAT) {
        flat_static(cr, g);
        cairo_restore(cr);
        return;
    }
    hex(cr, "1a1a1c", 1);
    cairo_paint(cr);

    cairo_translate(cr, g->ox, g->oy);
    cairo_scale(cr, g->s, g->s);
    draw_box(cr, g);
    draw_bezel(cr);
    static const char *names[3][2] = {{"K1", "E1"}, {"K2", "E2"}, {"K3", "E3"}};
    for (int i = 0; i < 3; i++) {
        draw_washer(cr, enc_pos[i]);
        // key well
        circle(cr, key_pos[i].x, key_pos[i].y, key_r);
        cairo_pattern_t *p = cairo_pattern_create_linear(0, key_pos[i].y - key_r, 0, key_pos[i].y + key_r);
        stop(p, 0, "8e9196", 1);
        stop(p, 1, "e3e5e8", 1);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
        circle(cr, key_pos[i].x, key_pos[i].y, key_r - 4);
        hex(cr, "161618", 1);
        cairo_fill(cr);

        draw_label(cr, key_pos[i], key_r, names[i][0]);
        draw_label(cr, enc_pos[i], enc_r, names[i][1]);
    }
    cairo_restore(cr);
}

static void draw_key(cairo_t *cr, pt_t c, int down, int hover) {
    double dy = down ? 2 : 0;
    double r = down ? 23 : 25;
    // cap; pressed it sinks into the well and loses its highlight
    circle(cr, c.x, c.y + dy, r);
    cairo_pattern_t *p = down ? cairo_pattern_create_radial(c.x, c.y + dy + 6, 2, c.x, c.y + dy, r)
                              : cairo_pattern_create_radial(c.x - 7, c.y - 9, 2, c.x, c.y, r);
    stop(p, 0, down ? "2c2d30" : "5b5e63", 1);
    stop(p, 1, down ? "0e0e10" : "26272a", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    // rim light
    cairo_set_line_width(cr, 1.2);
    circle(cr, c.x, c.y + dy, r - 0.6);
    p = cairo_pattern_create_linear(0, c.y + dy - r, 0, c.y + dy + r);
    if (down) {
        stop(p, 0, "000000", 0.6);
        stop(p, 1, "ffffff", 0.10);
    } else {
        stop(p, 0, "ffffff", 0.28);
        stop(p, 0.6, "ffffff", 0);
    }
    cairo_set_source(cr, p);
    cairo_stroke(cr);
    cairo_pattern_destroy(p);
    if (hover && !down) {
        circle(cr, c.x, c.y, r);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        cairo_fill(cr);
    }
}

static void draw_knob(cairo_t *cr, pt_t c, int ticks, int hover) {
    const double r = 31, top = 25;
    double a = ticks * KNURL_STEP;

    // shadow on the washer
    circle(cr, c.x + 2, c.y + 4, r + 1);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    cairo_fill(cr);

    // knurled side
    circle(cr, c.x, c.y, r);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, c.y - r, 0, c.y + r);
    stop(p, 0, "f1f2f4", 1);
    stop(p, 1, "8f9399", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1.1);
    for (int i = 0; i < KNURLS; i++) {
        double t = a + i * (2 * M_PI / KNURLS);
        cairo_move_to(cr, c.x + cos(t) * (top + 1.5), c.y + sin(t) * (top + 1.5));
        cairo_line_to(cr, c.x + cos(t) * (r - 0.5), c.y + sin(t) * (r - 0.5));
    }
    cairo_set_source_rgba(cr, 0, 0, 0, 0.16);
    cairo_stroke(cr);

    // top face
    circle(cr, c.x, c.y, top);
    p = cairo_pattern_create_radial(c.x - 8, c.y - 10, 1, c.x, c.y, top + 4);
    stop(p, 0, "fbfbfc", 1);
    stop(p, 1, "c0c3c8", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_set_line_width(cr, 1);
    circle(cr, c.x, c.y, top);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
    cairo_stroke(cr);

    if (hover) {
        circle(cr, c.x, c.y, r);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
        cairo_fill(cr);
    }
}

// a lit (amber) or dark led
static void draw_led(cairo_t *cr, double x, double y, int on) {
    if (FLAT) {
        circle(cr, x, y, 3.6);
        flat_rgb(cr, on ? FLAT_ACCENT : FLAT_LINE, 1);
        cairo_fill(cr);
        return;
    }
    if (on) {
        cairo_pattern_t *p = cairo_pattern_create_radial(x, y, 0, x, y, 11);
        stop(p, 0, "ffb04a", 0.55);
        stop(p, 1, "ffb04a", 0);
        cairo_set_source(cr, p);
        circle(cr, x, y, 11);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
    }
    circle(cr, x, y, 3.6);
    hex(cr, on ? "ffc46b" : "3b3c40", 1);
    cairo_fill(cr);
}

// "opens elsewhere": a small arrow up and right
static void draw_out_arrow(cairo_t *cr, double x, double y) {
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x - 3.5, y + 3.5);
    cairo_line_to(cr, x + 3.5, y - 3.5);
    cairo_move_to(cr, x - 1, y - 3.5);
    cairo_line_to(cr, x + 3.5, y - 3.5);
    cairo_line_to(cr, x + 3.5, y + 1);
    if (FLAT) {
        flat_rgb(cr, FLAT_DIM, 1);
    } else {
        hex(cr, "8d9096", 1);
    }
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
}

static void draw_button(cairo_t *cr, int i, const skin_state_t *st) {
    const button_t *b = &buttons[i];
    int id = SKIN_PAGE1 + i;
    int down = st->button_down == id;
    int lit = (id <= SKIN_PAGE4 && st->page == i + 1) || (id == SKIN_GRID && st->grid_open) ||
              (id == SKIN_ARC && st->arc_open) || (id == SKIN_KEYS && st->keys_open) ||
              (id == SKIN_MIC && st->input != SKIN_INPUT_OFF);
    double x = b->x, y = button_y, dy = down ? 1.2 : 0;

    if (FLAT) {
        dy = 0;
        rounded_rect(cr, x, y, button_w, button_h, 8);
        flat_rgb(cr, down ? FLAT_FACE_DOWN : st->hover == id ? FLAT_FACE_UP
                                                             : FLAT_FACE,
                 1);
        cairo_fill(cr);
    } else {
        // well
        rounded_rect(cr, x - 2.5, y - 2.5, button_w + 5, button_h + 5, 10);
        cairo_pattern_t *p = cairo_pattern_create_linear(0, y - 2.5, 0, y + button_h + 2.5);
        stop(p, 0, "8e9196", 1);
        stop(p, 1, "e3e5e8", 1);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);

        // face
        rounded_rect(cr, x, y + dy, button_w, button_h, 8);
        p = cairo_pattern_create_linear(0, y + dy, 0, y + dy + button_h);
        stop(p, 0, down ? "232427" : "3d3f43", 1);
        stop(p, 1, down ? "17181a" : "232427", 1);
        cairo_set_source(cr, p);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
        cairo_set_line_width(cr, 1);
        rounded_rect(cr, x + 0.5, y + dy + 0.5, button_w - 1, button_h - 1, 7.5);
        p = cairo_pattern_create_linear(0, y + dy, 0, y + dy + button_h);
        stop(p, 0, "ffffff", down ? 0.06 : 0.22);
        stop(p, 0.5, "ffffff", 0);
        cairo_set_source(cr, p);
        cairo_stroke(cr);
        cairo_pattern_destroy(p);
        if (st->hover == id && !down) {
            rounded_rect(cr, x, y, button_w, button_h, 8);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_fill(cr);
        }
    }

    // led or arrow on the left, label in the rest
    double mid = y + dy + button_h / 2;
    if (id <= SKIN_KEYS) {
        draw_led(cr, x + 12, mid, lit);
    } else {
        draw_out_arrow(cr, x + 12, mid);
    }
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    double area = x + 20, area_w = button_w - 23;
    static const char *input_label[] = {"OFF", "MIC", "SYS", "APP"};
    const char *label = b->label;
    if (id == SKIN_MIC && st->input >= SKIN_INPUT_OFF && st->input <= SKIN_INPUT_APP) {
        label = input_label[st->input];
    }
    // long labels get a smaller size rather than touching the edge
    double size = 12;
    cairo_text_extents_t te;
    for (;;) {
        cairo_set_font_size(cr, size);
        cairo_text_extents(cr, label, &te);
        if (te.width <= area_w - 2 || size <= 8) {
            break;
        }
        size -= 0.5;
    }
    cairo_move_to(cr, area + (area_w - te.width) / 2 - te.x_bearing, mid - te.height / 2 - te.y_bearing);
    if (FLAT) {
        flat_rgb(cr, lit ? FLAT_TEXT : FLAT_SOFT, 1);
    } else {
        hex(cr, lit ? "ffffff" : "c3c6cb", 1);
    }
    cairo_show_text(cr, label);
}

// a message under the buttons, right of E1: a dark pill like a notification
static void draw_toast(cairo_t *cr, const char *text, int recording) {
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_text_extents_t te;
    cairo_text_extents(cr, text, &te);
    double dot = recording ? 22 : 0;
    double w = te.x_advance + 36 + dot, h = 36;
    double right = SKIN_W - 95, cy = 175;
    if (w > right - 300) {
        w = right - 300;
    }
    double x = right - w, y = cy - h / 2;
    if (FLAT) {
        rounded_rect(cr, x, y, w, h, h / 2);
        flat_rgb(cr, FLAT_FACE_DOWN, 1);
        cairo_fill(cr);
    } else {
        rounded_rect(cr, x, y + 2, w, h, h / 2);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_fill(cr);
        rounded_rect(cr, x, y, w, h, h / 2);
        hex(cr, "1d1e21", 0.92);
        cairo_fill(cr);
    }
    if (recording) {
        circle(cr, x + 22, cy, 6);
        hex(cr, "ff453a", 1);
        cairo_fill(cr);
    }
    cairo_save(cr);
    rounded_rect(cr, x + 12, y, w - 24, h, 0);
    cairo_clip(cr);
    cairo_move_to(cr, x + 18 + dot - te.x_bearing, cy - te.height / 2 - te.y_bearing);
    hex(cr, "f2f3f5", 1);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}

// MUTE, dark flat: a button like the others
static void flat_mute(cairo_t *cr, const skin_state_t *st) {
    int down = st->button_down == SKIN_MUTE;
    rounded_rect(cr, mute_x, mute_y, mute_w, mute_h, 7);
    flat_rgb(cr, down ? FLAT_FACE_DOWN : st->hover == SKIN_MUTE ? FLAT_FACE_UP
                                                                : FLAT_FACE,
             1);
    cairo_fill(cr);
    double mid = mute_y + mute_h / 2;
    draw_led(cr, mute_x + 10, mid, st->out_muted);
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    cairo_text_extents_t te;
    cairo_text_extents(cr, "MUTE", &te);
    double area = mute_x + 17, area_w = mute_w - 19;
    cairo_move_to(cr, area + (area_w - te.width) / 2 - te.x_bearing, mid - te.height / 2 - te.y_bearing);
    flat_rgb(cr, st->out_muted ? FLAT_TEXT : FLAT_SOFT, 1);
    cairo_show_text(cr, "MUTE");
}

// the OUT fader: a slot, the level lit up to the cap, the cap; MUTE under it
static void draw_fader(cairo_t *cr, const skin_state_t *st) {
    double v = st->out_level < 0 ? 0 : st->out_level > 1 ? 1
                                                         : st->out_level;
    double cy = fader_bottom - v * (fader_bottom - fader_top);
    int hover = st->hover == SKIN_FADER;

    // label, engraved like the others
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 15);
    cairo_text_extents_t te;
    cairo_text_extents(cr, "OUT", &te);
    double lx = fader_x - te.width / 2 - te.x_bearing, ly = fader_top - fader_cap_h / 2 - 12;
    if (FLAT) {
        cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        cairo_text_extents(cr, "OUT", &te);
        cairo_move_to(cr, fader_x - te.width / 2 - te.x_bearing, ly);
        flat_rgb(cr, FLAT_DIM, 1);
        cairo_show_text(cr, "OUT");
    } else {
        cairo_move_to(cr, lx + 0.6, ly + 1.4);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
        cairo_show_text(cr, "OUT");
        cairo_move_to(cr, lx, ly);
        hex(cr, "4a4d52", 0.75);
        cairo_show_text(cr, "OUT");
    }

    // scale marks
    cairo_set_line_width(cr, 1);
    for (int i = 0; i <= 8; i++) {
        double y = fader_top + i * (fader_bottom - fader_top) / 8;
        double w = i % 4 == 0 ? 9 : 5;
        cairo_move_to(cr, fader_x - 11 - w, y + 0.5);
        cairo_line_to(cr, fader_x - 11, y + 0.5);
        cairo_move_to(cr, fader_x + 11, y + 0.5);
        cairo_line_to(cr, fader_x + 11 + w, y + 0.5);
    }
    if (FLAT) {
        flat_rgb(cr, FLAT_LINE, 1);
    } else {
        hex(cr, "7d8086", 0.8);
    }
    cairo_stroke(cr);

    // slot
    rounded_rect(cr, fader_x - 4, fader_top - 3, 8, fader_bottom - fader_top + 6, 4);
    if (FLAT) {
        flat_rgb(cr, FLAT_BG, 1);
    } else {
        hex(cr, "1a1b1d", 1);
    }
    cairo_fill(cr);
    // the level, unless muted
    if (!st->out_muted && v > 0) {
        rounded_rect(cr, fader_x - 2, cy, 4, fader_bottom - cy + 1, 2);
        hex(cr, "ffb04a", 0.85);
        cairo_fill(cr);
    }

    // cap
    double x = fader_x - fader_cap_w / 2, y = cy - fader_cap_h / 2;
    if (FLAT) {
        rounded_rect(cr, x, y, fader_cap_w, fader_cap_h, 4);
        flat_rgb(cr, hover ? FLAT_FACE_DOWN : FLAT_FACE_UP, 1);
        cairo_fill(cr);
        cairo_move_to(cr, x + 6, cy + 0.5);
        cairo_line_to(cr, x + fader_cap_w - 6, cy + 0.5);
        flat_rgb(cr, FLAT_TEXT, 1);
        cairo_stroke(cr);
        flat_mute(cr, st);
        return;
    }
    rounded_rect(cr, x, y + 2, fader_cap_w, fader_cap_h, 4);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
    cairo_fill(cr);
    rounded_rect(cr, x, y, fader_cap_w, fader_cap_h, 4);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + fader_cap_h);
    stop(p, 0, "5b5e63", 1);
    stop(p, 1, "26272a", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    cairo_move_to(cr, x + 5, cy + 0.5);
    cairo_line_to(cr, x + fader_cap_w - 5, cy + 0.5);
    hex(cr, "c3c6cb", 0.9);
    cairo_stroke(cr);
    if (hover) {
        rounded_rect(cr, x, y, fader_cap_w, fader_cap_h, 4);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
        cairo_fill(cr);
    }

    // MUTE: a small button, lit while muted
    int down = st->button_down == SKIN_MUTE;
    double dy = down ? 1.2 : 0;
    rounded_rect(cr, mute_x - 2.5, mute_y - 2.5, mute_w + 5, mute_h + 5, 9);
    p = cairo_pattern_create_linear(0, mute_y - 2.5, 0, mute_y + mute_h + 2.5);
    stop(p, 0, "8e9196", 1);
    stop(p, 1, "e3e5e8", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    rounded_rect(cr, mute_x, mute_y + dy, mute_w, mute_h, 7);
    p = cairo_pattern_create_linear(0, mute_y + dy, 0, mute_y + dy + mute_h);
    stop(p, 0, down ? "232427" : "3d3f43", 1);
    stop(p, 1, down ? "17181a" : "232427", 1);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
    if (st->hover == SKIN_MUTE && !down) {
        rounded_rect(cr, mute_x, mute_y, mute_w, mute_h, 7);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        cairo_fill(cr);
    }
    double mid = mute_y + dy + mute_h / 2;
    draw_led(cr, mute_x + 10, mid, st->out_muted);
    cairo_select_font_face(cr, "Helvetica Neue", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    cairo_text_extents(cr, "MUTE", &te);
    double area = mute_x + 17, area_w = mute_w - 19;
    cairo_move_to(cr, area + (area_w - te.width) / 2 - te.x_bearing, mid - te.height / 2 - te.y_bearing);
    hex(cr, st->out_muted ? "ffffff" : "c3c6cb", 1);
    cairo_show_text(cr, "MUTE");
}

void skin_draw_controls(cairo_t *cr, const skin_geom_t *g, const skin_state_t *st) {
    cairo_save(cr);
    cairo_translate(cr, g->ox, g->oy);
    cairo_scale(cr, g->s, g->s);
    for (int i = 0; i < NBUTTONS; i++) {
        draw_button(cr, i, st);
    }
    if (st->toast != NULL && st->toast[0] != '\0') {
        draw_toast(cr, st->toast, st->recording);
    }
    for (int i = 0; i < 3; i++) {
        if (FLAT) {
            flat_key(cr, key_pos[i], st->key_down[i], st->hover == SKIN_K1 + i);
            flat_knob(cr, enc_pos[i], st->enc_ticks[i], st->hover == SKIN_E1 + i);
        } else {
            draw_key(cr, key_pos[i], st->key_down[i], st->hover == SKIN_K1 + i);
            draw_knob(cr, enc_pos[i], st->enc_ticks[i], st->hover == SKIN_E1 + i);
        }
    }
    draw_fader(cr, st);
    cairo_restore(cr);
}

void skin_draw_oled(cairo_t *cr, const skin_geom_t *g, cairo_surface_t *oled) {
    cairo_save(cr);
    // bezel interior, in device pixels, so the oled can be redrawn on its own
    cairo_save(cr);
    cairo_translate(cr, g->ox, g->oy);
    cairo_scale(cr, g->s, g->s);
    rounded_rect(cr, bezel_x, bezel_y, bezel_w, bezel_h, 7);
    cairo_restore(cr);
    cairo_clip(cr);
    hex(cr, "000000", 1);
    cairo_paint(cr);

    cairo_save(cr);
    cairo_translate(cr, g->oled_x, g->oled_y);
    cairo_scale(cr, g->oled_px, g->oled_px);
    cairo_set_source_surface(cr, oled, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, 128, 64);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_restore(cr);
}
