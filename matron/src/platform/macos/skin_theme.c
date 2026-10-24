/*
 * skin_theme.c: see skin_theme.h
 */

#include "skin_theme.h"

static int style = SKIN_STYLE_DEVICE;

int skin_style(void) {
    return style;
}

void skin_set_style(int s) {
    style = s == SKIN_STYLE_FLAT ? SKIN_STYLE_FLAT : SKIN_STYLE_DEVICE;
}

static double ch(unsigned rgb, int shift) {
    return ((rgb >> shift) & 0xff) / 255.0;
}

void flat_rgb(cairo_t *cr, unsigned rgb, double a) {
    cairo_set_source_rgba(cr, ch(rgb, 16), ch(rgb, 8), ch(rgb, 0), a);
}

void flat_mix(cairo_t *cr, unsigned a, unsigned b, double l) {
    l = l < 0 ? 0 : l > 1 ? 1
                          : l;
    cairo_set_source_rgb(cr, ch(a, 16) + (ch(b, 16) - ch(a, 16)) * l, ch(a, 8) + (ch(b, 8) - ch(a, 8)) * l,
                         ch(a, 0) + (ch(b, 0) - ch(a, 0)) * l);
}
