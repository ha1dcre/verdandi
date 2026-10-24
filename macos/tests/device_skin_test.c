/*
 * device_skin_test.c: hit testing and png renders for platform/macos/device_skin.c
 *
 * cc -std=c11 -Wall -Wextra -I matron/src/platform/macos $(pkg-config --cflags --libs cairo) \
 *    macos/tests/device_skin_test.c matron/src/platform/macos/device_skin.c \
 *    matron/src/platform/macos/vgrid_skin.c matron/src/platform/macos/varc_skin.c \
 *    matron/src/platform/macos/vkeys_skin.c matron/src/platform/macos/skin_theme.c \
 *    -o /tmp/dst && /tmp/dst [out_dir [flat]]
 *
 * with out_dir it also writes skin_<w>x<h>.png renders (and the grid, arc and keyboard
 * windows) to look at; with flat, in the dark flat style.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "device_skin.h"
#include "skin_theme.h"
#include "varc_skin.h"
#include "vgrid_skin.h"
#include "vkeys_skin.h"

static int failures = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            failures++;                                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

// layout units -> device pixels
static double px(const skin_geom_t *g, double u) {
    return g->ox + u * g->s;
}
static double py(const skin_geom_t *g, double u) {
    return g->oy + u * g->s;
}

static void test_geom(int w, int h) {
    skin_geom_t g;
    skin_geom(w, h, &g);
    // the box fits, centred
    CHECK(g.ox >= 0 && g.oy >= 0, "%dx%d: box origin %.1f,%.1f", w, h, g.ox, g.oy);
    CHECK(g.ox + SKIN_W * g.s <= w + 0.01 && g.oy + SKIN_H * g.s <= h + 0.01, "%dx%d: box overflows", w, h);
    // the oled is an integer scale inside the bezel (77,356 417x220)
    CHECK(g.oled_px >= 1, "%dx%d: oled px %d", w, h, g.oled_px);
    CHECK(g.oled_x >= px(&g, 77) && g.oled_x + 128 * g.oled_px <= px(&g, 77 + 417) + 0.5, "%dx%d: oled x outside bezel",
          w, h);
    CHECK(g.oled_y >= py(&g, 356) && g.oled_y + 64 * g.oled_px <= py(&g, 356 + 220) + 0.5, "%dx%d: oled y outside bezel",
          w, h);
    // the oled keeps a margin to the bezel edge (the text is not pressed against the frame)
    CHECK(g.oled_x - px(&g, 77) >= 8 * g.s, "%dx%d: no left margin", w, h);

    // controls at their centres
    static const double k[3][2] = {{109, 252}, {594, 542}, {749, 542}};
    static const double e[3][2] = {{216, 252}, {672, 420}, {827, 420}};
    for (int i = 0; i < 3; i++) {
        int hk = skin_hit(&g, px(&g, k[i][0]), py(&g, k[i][1]));
        CHECK(hk == SKIN_K1 + i, "%dx%d: K%d centre hits %d", w, h, i + 1, hk);
        int he = skin_hit(&g, px(&g, e[i][0]), py(&g, e[i][1]));
        CHECK(he == SKIN_E1 + i, "%dx%d: E%d centre hits %d", w, h, i + 1, he);
        // edge of the knob still turns it
        he = skin_hit(&g, px(&g, e[i][0] + 36), py(&g, e[i][1]));
        CHECK(he == SKIN_E1 + i, "%dx%d: E%d rim hits %d", w, h, i + 1, he);
        CHECK(skin_nearest_enc(&g, px(&g, e[i][0] + 3), py(&g, e[i][1] - 50)) == i + 1, "%dx%d: nearest E%d", w, h,
              i + 1);
    }
    // the screen, the empty top and the outside are no control
    CHECK(skin_hit(&g, px(&g, 280), py(&g, 460)) == SKIN_NONE, "%dx%d: screen hits a control", w, h);

    // the OUT fader right of E3 (x 913, 196 top = 1, 486 bottom = 0) and MUTE under it
    CHECK(skin_hit(&g, px(&g, 913), py(&g, 340)) == SKIN_FADER, "%dx%d: fader middle", w, h);
    CHECK(skin_hit(&g, px(&g, 913), py(&g, 190)) == SKIN_FADER && skin_hit(&g, px(&g, 913), py(&g, 495)) == SKIN_FADER,
          "%dx%d: fader ends (the cap sticks out)", w, h);
    CHECK(skin_hit(&g, px(&g, 913), py(&g, 537)) == SKIN_MUTE, "%dx%d: mute", w, h);
    CHECK(skin_hit(&g, px(&g, 870), py(&g, 420)) == SKIN_E3, "%dx%d: E3's rim is still E3", w, h);
    CHECK(fabs(skin_fader_value(&g, py(&g, 196)) - 1) < 1e-6 && fabs(skin_fader_value(&g, py(&g, 486))) < 1e-6,
          "%dx%d: fader top 1, bottom 0", w, h);
    CHECK(fabs(skin_fader_value(&g, py(&g, 341)) - 0.5) < 1e-6, "%dx%d: fader middle 0.5", w, h);
    CHECK(skin_fader_value(&g, py(&g, 100)) == 1 && skin_fader_value(&g, py(&g, 600)) == 0, "%dx%d: fader clamps", w, h);

    // oled pixels under the mouse
    int ox = -1, oy = -1;
    CHECK(skin_oled_pixel(&g, g.oled_x + 0.5, g.oled_y + 0.5, &ox, &oy) && ox == 0 && oy == 0, "%dx%d: oled 0,0", w, h);
    CHECK(skin_oled_pixel(&g, g.oled_x + 127.5 * g.oled_px, g.oled_y + 63.5 * g.oled_px, &ox, &oy) && ox == 127 &&
              oy == 63,
          "%dx%d: oled 127,63 (%d,%d)", w, h, ox, oy);
    CHECK(skin_oled_pixel(&g, g.oled_x + 30.5 * g.oled_px, g.oled_y + 10.5 * g.oled_px, &ox, &oy) && ox == 30 &&
              oy == 10,
          "%dx%d: oled 30,10", w, h);
    CHECK(!skin_oled_pixel(&g, g.oled_x - 1, g.oled_y + 5, &ox, &oy) &&
              !skin_oled_pixel(&g, g.oled_x + 5, g.oled_y + 64 * g.oled_px, &ox, &oy),
          "%dx%d: off the oled", w, h);
    CHECK(skin_hit(&g, px(&g, 500), py(&g, 170)) == SKIN_NONE, "%dx%d: top hits a control", w, h);
    CHECK(skin_hit(&g, 1, 1) == SKIN_NONE, "%dx%d: corner hits a control", w, h);
    // the button row: pages, mic, grid, arc, keys, maiden, manual, left to right
    static const double bx[10] = {77, 153, 229, 305, 398, 474, 550, 626, 719, 795};
    for (int i = 0; i < 10; i++) {
        int hb = skin_hit(&g, px(&g, bx[i] + 35), py(&g, 90));
        CHECK(hb == SKIN_PAGE1 + i, "%dx%d: button %d hits %d", w, h, i, hb);
    }
    CHECK(skin_hit(&g, px(&g, 626 + 35), py(&g, 90)) == SKIN_KEYS, "%dx%d: KEYS", w, h);
    CHECK(skin_hit(&g, px(&g, 795 + 35), py(&g, 90)) == SKIN_MANUAL, "%dx%d: MANUAL is last", w, h);
    CHECK(skin_hit(&g, px(&g, 386), py(&g, 90)) == SKIN_NONE, "%dx%d: gap between button groups", w, h);
    CHECK(skin_hit(&g, px(&g, 707), py(&g, 90)) == SKIN_NONE, "%dx%d: gap before maiden", w, h);
    // nothing reaches past the last knob (the box edge)
    CHECK(795 + 70 <= 865 + 0.01, "buttons end at E3's edge");
    CHECK(skin_hit(&g, px(&g, 125), py(&g, 160)) == SKIN_NONE, "%dx%d: below the buttons", w, h);
    // K2 and E2 do not overlap: between them is nothing
    CHECK(skin_hit(&g, px(&g, 633), py(&g, 481)) == SKIN_NONE, "%dx%d: K2/E2 overlap", w, h);
}

// a fake menu frame, like the HOME page
static cairo_surface_t *fake_oled(void) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 128, 64);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const char *lines[] = {"HOME", "", "SELECT >", "SYSTEM >", "SLEEP >"};
    for (int i = 0; i < 5; i++) {
        double l = i == 2 ? 1 : 0.3;
        cairo_set_source_rgb(cr, l, l, l);
        cairo_move_to(cr, 0, 10 * (i + 1));
        cairo_set_font_size(cr, 8);
        cairo_show_text(cr, lines[i]);
    }
    cairo_destroy(cr);
    return s;
}

static void render(const char *dir, int w, int h, const skin_state_t *st, const char *tag) {
    skin_geom_t g;
    skin_geom(w, h, &g);
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(out);
    cairo_surface_t *oled = fake_oled();
    skin_draw_static(cr, &g);
    skin_draw_controls(cr, &g, st);
    skin_draw_oled(cr, &g, oled);
    char path[512];
    snprintf(path, sizeof path, "%s/skin_%dx%d%s.png", dir, w, h, tag);
    CHECK(cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS, "write %s", path);
    cairo_surface_destroy(oled);
    cairo_destroy(cr);
    cairo_surface_destroy(out);
}

static void test_vgrid(const char *dir, int size) {
    int cols = vgrid_sizes[size][0], rows = vgrid_sizes[size][1], uw, uh, c, r;
    vgrid_skin_units(cols, rows, &uw, &uh);
    int w = uw * 2, h = uh * 2;
    vgrid_geom_t g;
    vgrid_skin_geom(w, h, cols, rows, &g);
    // key centres (pitch 44, key 31, 18 + 26.5 to the first key) hit their key
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            double px = g.ox + (44.5 + x * 44 + 15.5) * g.s, py = g.oy + (44.5 + y * 44 + 15.5) * g.s;
            CHECK(vgrid_skin_hit(&g, px, py, &c, &r) && c == x && r == y, "%dx%d: key %d,%d hits %d,%d", cols, rows,
                  x, y, c, r);
        }
    }
    CHECK(!vgrid_skin_hit(&g, 2, 2, &c, &r), "%dx%d: corner hits a key", cols, rows);
    CHECK(!vgrid_skin_hit(&g, w - 2, h / 2.0, &c, &r), "%dx%d: right edge hits a key", cols, rows);
    // the size switch sits under the plate, in the middle, and no key is under it
    int found[4] = {0};
    for (double x = 0; x < w; x += 2) {
        int s = vgrid_skin_hit_size(&g, x, g.oy + (uh - 50 + 4 + 13 + 5) * g.s);
        found[s] = 1;
    }
    CHECK(found[1] && found[2] && found[3], "%dx%d: size switch has 64 128 256", cols, rows);
    CHECK(vgrid_skin_hit_size(&g, w / 2.0, g.oy + 60 * g.s) == 0, "%dx%d: switch on the keys", cols, rows);
    if (dir == NULL) {
        return;
    }
    uint8_t level[VGRID_MAX][VGRID_MAX] = {{0}}, held[VGRID_MAX][VGRID_MAX] = {{0}};
    for (int x = 0; x < cols; x++) {
        level[x % rows][x] = 15;
        level[rows - 1][x] = (uint8_t)(x * 15 / (cols - 1));
    }
    level[3][5] = 4;
    held[2][6] = VGRID_HELD_MOUSE;
    held[5][7] = VGRID_HELD_LATCH;
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(out);
    vgrid_skin_draw(cr, &g, w, h, level, 15, held, size);
    char path[512];
    snprintf(path, sizeof path, "%s/vgrid_%d.png", dir, cols * rows);
    CHECK(cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS, "write %s", path);
    cairo_destroy(cr);
    cairo_surface_destroy(out);
}

static void test_varc(const char *dir) {
    int uw, uh;
    varc_skin_units(4, &uw, &uh);
    int w = uw * 2, h = uh * 2;
    varc_geom_t g;
    varc_skin_geom(w, h, 4, &g);
    for (int n = 0; n < 4; n++) {
        double cx, cy;
        varc_skin_center(&g, n, &cx, &cy);
        CHECK(varc_skin_hit(&g, cx, cy) == n, "arc: centre of ring %d", n);
        CHECK(varc_skin_hit(&g, cx + 80 * g.s, cy) == n, "arc: rim of ring %d", n);
        CHECK(cx > 0 && cx < w && cy > 0 && cy < h, "arc: ring %d inside", n);
    }
    CHECK(varc_skin_hit(&g, 2, 2) == -1, "arc: corner hits a ring");
    if (dir == NULL) {
        return;
    }
    uint8_t ring[VARC_RINGS][VARC_LEDS] = {{0}};
    for (int x = 0; x < 64; x++) {
        ring[0][x] = x < 24 ? 15 : 0;
        ring[1][x] = (uint8_t)(x / 4);
        ring[3][x] = x >= 40 ? 6 : 0;
    }
    ring[2][10] = 15;
    double angle[4] = {0, 0.1, 0.2, 0.3};
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(out);
    varc_skin_draw(cr, &g, w, h, ring, 15, angle, 2);
    char path[512];
    snprintf(path, sizeof path, "%s/varc.png", dir);
    CHECK(cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS, "write %s", path);
    cairo_destroy(cr);
    cairo_surface_destroy(out);
}

static void test_vkeys(const char *dir) {
    int uw, uh, key = -1, vel = 0;
    vkeys_skin_units(&uw, &uh);
    int w = uw * 2, h = uh * 2;
    vkeys_geom_t g;
    vkeys_skin_geom(w, h, &g);
    // layout units: keys start at 32,86; white keys 44 x 190, black 27 x 118
    double kx = g.ox + 32 * g.s, ky = g.oy + 86 * g.s;
    // the front of each white key: C D E F G A B C ...
    static const int whites[15] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
    for (int i = 0; i < 15; i++) {
        int c = vkeys_skin_hit(&g, kx + (i * 44 + 22) * g.s, ky + 170 * g.s, &key, &vel);
        CHECK(c == VK_KEY && key == whites[i], "keys: white %d hits %d (key %d)", i, c, key);
        CHECK(vel > 100, "keys: front of a key is loud (%d)", vel);
    }
    // black keys over the white ones: C# between C and D, at the top
    int c = vkeys_skin_hit(&g, kx + 44 * g.s, ky + 20 * g.s, &key, &vel);
    CHECK(c == VK_KEY && key == 1, "keys: C# hits %d (key %d)", c, key);
    CHECK(vel < 60, "keys: back of a key is soft (%d)", vel);
    c = vkeys_skin_hit(&g, kx + (7 * 44 + 4 * 44) * g.s, ky + 60 * g.s, &key, &vel);
    CHECK(c == VK_KEY && key == 18, "keys: upper F# hits %d (key %d)", c, key);
    // E and F have no black key between them
    c = vkeys_skin_hit(&g, kx + 3 * 44 * g.s, ky + 40 * g.s, &key, &vel);
    CHECK(c == VK_KEY && (key == 4 || key == 5), "keys: between E and F hits %d (key %d)", c, key);
    // the strip
    int found[VK_KEY + 1] = {0};
    for (double x = 0; x < w; x += 2) {
        int hit = vkeys_skin_hit(&g, x, g.oy + (32 + 17) * g.s, &key, &vel);
        found[hit] = 1;
    }
    CHECK(found[VK_OCT_DOWN] && found[VK_OCT_UP] && found[VK_VEL_DOWN] && found[VK_VEL_UP] && found[VK_LATCH],
          "keys: strip has octave, velocity and latch");
    CHECK(!found[VK_KEY], "keys: strip hits a key");
    CHECK(vkeys_skin_hit(&g, 2, 2, &key, &vel) == VK_NONE, "keys: corner");

    // notes and the computer keyboard
    vkeys_state_t st;
    memset(&st, 0, sizeof st);
    st.octave = 3;
    CHECK(vkeys_note(&st, 0) == 48 && vkeys_note(&st, 12) == 60, "keys: octave 3 is C3 (48), middle C 60");
    int ctl;
    CHECK(vkeys_typed(4, &ctl) == 0 && ctl == VK_NONE, "keys: A plays C");
    CHECK(vkeys_typed(26, &ctl) == 1, "keys: W plays C#");
    CHECK(vkeys_typed(14, &ctl) == 12, "keys: K plays the upper C");
    CHECK(vkeys_typed(52, &ctl) == 17, "keys: ' plays the upper F");
    CHECK(vkeys_typed(29, &ctl) == -1 && ctl == VK_OCT_DOWN, "keys: Z is octave down");
    CHECK(vkeys_typed(27, &ctl) == -1 && ctl == VK_OCT_UP, "keys: X is octave up");
    CHECK(vkeys_typed(6, &ctl) == -1 && ctl == VK_VEL_DOWN, "keys: C is velocity down");
    CHECK(vkeys_typed(25, &ctl) == -1 && ctl == VK_VEL_UP, "keys: V is velocity up");
    CHECK(vkeys_typed(20, &ctl) == -1 && ctl == VK_NONE, "keys: Q plays nothing");
    for (int k = 0; k < 18; k++) {
        CHECK(vkeys_typed(vkeys_typed_usage(k), &ctl) == k, "keys: usage round trip %d", k);
    }
    CHECK(vkeys_typed_usage(20) == -1, "keys: no computer key above F");
    if (dir == NULL) {
        return;
    }
    st.velocity = 100;
    st.latch = 1;
    st.held[0] = VKEYS_HELD_MOUSE;
    st.held[4] = VKEYS_HELD_LATCH;
    st.held[13] = VKEYS_HELD_TYPED;
    static const char *labels[18] = {"A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J", "K", "O", "L", "P", ";", "'"};
    for (int k = 0; k < 18; k++) {
        st.typed_label[k] = labels[k];
    }
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(out);
    vkeys_skin_draw(cr, &g, w, h, &st);
    char path[512];
    snprintf(path, sizeof path, "%s/vkeys.png", dir);
    CHECK(cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS, "write %s", path);
    cairo_destroy(cr);
    cairo_surface_destroy(out);
}

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[2], "flat") == 0) {
        skin_set_style(SKIN_STYLE_FLAT);
    }
    test_varc(argc > 1 ? argv[1] : NULL);
    test_vkeys(argc > 1 ? argv[1] : NULL);
    for (int size = 1; size <= 3; size++) {
        test_vgrid(argc > 1 ? argv[1] : NULL, size);
    }
    int sizes[][2] = {{1984, 1324}, {992, 662}, {496, 331}, {2400, 900}, {700, 1200}};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        test_geom(sizes[i][0], sizes[i][1]);
    }
    if (argc > 1) {
        skin_state_t idle = {.hover = SKIN_NONE, .input = SKIN_INPUT_MIC, .button_down = SKIN_NONE, .out_level = 0.6};
        skin_state_t used = {.key_down = {0, 1, 0},
                             .enc_ticks = {0, 3, -5},
                             .hover = SKIN_E1,
                             .page = 4,
                             .grid_open = 1,
                             .keys_open = 1,
                             .button_down = SKIN_MAIDEN,
                             .recording = 1,
                             .out_level = 0.8,
                             .toast = "REC  GIF · \u21e7\u2318G stops"};
        skin_state_t toast = {.hover = SKIN_NONE,
                              .input = SKIN_INPUT_SYSTEM,
                              .arc_open = 1,
                              .toast = "3 files \u2192 dust/audio",
                              .out_level = 0.3,
                              .out_muted = 1};
        render(argv[1], 1984, 1324, &idle, "");
        render(argv[1], 1984, 1324, &used, "_used");
        render(argv[1], 992, 662, &idle, "");
        render(argv[1], 992, 662, &toast, "_toast");
    }
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
