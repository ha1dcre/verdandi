/*
 * skin_theme.h
 *
 * which look the windows are drawn in: the device (aluminium, as the hardware) or dark flat
 * (near black, no gradients, bevels or glow, one amber accent: easier on the eyes in a dark
 * room). same layout and hit areas in both, only the drawing differs. the main window, grid,
 * arc and keyboard all follow it.
 */

#pragma once

#include <cairo.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SKIN_STYLE_DEVICE = 0,
    SKIN_STYLE_FLAT = 1,
};

extern int skin_style(void);
extern void skin_set_style(int style);

// the dark flat palette, 0xrrggbb
#define FLAT_BG 0x0c0c0d      // around the box
#define FLAT_BODY 0x151517    // the box, plate, keyboard body
#define FLAT_FACE 0x212124    // keys, knobs, buttons
#define FLAT_FACE_UP 0x2a2a2e // the same under the mouse
#define FLAT_FACE_DOWN 0x323237
#define FLAT_LINE 0x2e2e33   // outlines, scale marks, dark leds
#define FLAT_TEXT 0xd4d6d9   // lit labels, values
#define FLAT_SOFT 0x9a9da3   // unlit button labels
#define FLAT_DIM 0x6e7177    // labels
#define FLAT_ACCENT 0xffb04a // lit: leds, level, held keys

extern void flat_rgb(cairo_t *cr, unsigned rgb, double a);

// the colour l (0-1) of the way from rgb a to rgb b
extern void flat_mix(cairo_t *cr, unsigned a, unsigned b, double l);

#ifdef __cplusplus
}
#endif
