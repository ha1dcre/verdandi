/*
 * vcrow_skin.h
 *
 * the virtual crow window: a plate like the arc's with the two inputs on the left and the
 * four outputs on the right. an input is a knob that sets its voltage (-5 to +10 V) in a lit
 * track, with what the script made of it (stream, change, ...); an output is a jack in a
 * ring that glows with its voltage (amber up, blue down). every column has its value and a
 * scope of the last 2 s. pure cairo, like varc_skin.h (macos/tests/device_skin_test.c).
 */

#pragma once

#include <cairo.h>

#include "vcrow_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double s;      // device pixels per unit
    double ox, oy; // origin in device pixels
} vcrow_geom_t;

// window size in units (one unit per point at 1:1)
extern void vcrow_skin_units(int *w, int *h);
extern void vcrow_skin_geom(int w, int h, vcrow_geom_t *g);

// the input (0-1) whose column is under device pixel x/y, -1 if none
extern int vcrow_skin_hit(const vcrow_geom_t *g, double x, double y);

// held: bit n = input n held at the gate voltage by the mouse
extern void vcrow_skin_draw(cairo_t *cr, const vcrow_geom_t *g, int w, int h, const vcrow_view_t *v, int held);

#ifdef __cplusplus
}
#endif
