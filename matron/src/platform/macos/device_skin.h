/*
 * device_skin.h
 *
 * the norns window drawn as the device: the aluminium box seen from the top, the oled
 * behind its bezel, K1-K3 and E1-E3 where they sit on a standard norns. pure cairo, no
 * SDL, so the drawing can be rendered to png and checked without a window
 * (macos/tests/device_skin_test.c).
 *
 * layout units: the box is SKIN_W x SKIN_H units, positions taken from the monome
 * "standard norns" legend. everything is drawn in device pixels through skin_geom_t.
 */

#pragma once

#include <cairo.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKIN_W 960
#define SKIN_H 630
#define SKIN_MARGIN 16

// controls: 1-3 = K1-K3, 4-6 = E1-E3, then the button row along the top (not on the
// device): the menu pages MIX TAPE HOME PARAMS, MIC (input on/off), the virtual device
// windows GRID ARC KEYS CROW, then MAIDEN, MANUAL
enum {
    SKIN_NONE = 0,
    SKIN_K1 = 1,
    SKIN_K3 = 3,
    SKIN_E1 = 4,
    SKIN_E3 = 6,
    SKIN_PAGE1 = 7,
    SKIN_PAGE4 = 10,
    SKIN_MIC = 11,
    SKIN_GRID = 12,
    SKIN_ARC = 13,
    SKIN_KEYS = 14,
    SKIN_CROW = 15,
    SKIN_MAIDEN = 16,
    SKIN_MANUAL = 17,
    SKIN_FADER = 18, // the OUT fader right of E3 (not on the device): norns' output level
    SKIN_MUTE = 19,  // under it: output off and back
};

// the MIC button: what reaches norns' input. lit unless OFF
enum {
    SKIN_INPUT_OFF = 0,
    SKIN_INPUT_MIC = 1,    // the Mac's microphone or the interface
    SKIN_INPUT_SYSTEM = 2, // every other app's sound
    SKIN_INPUT_APP = 3,    // one app's sound
};

typedef struct {
    int key_down[3];   // K1-K3 held
    int enc_ticks[3];  // E1-E3 turned so far, sets the knob angle
    int hover;         // control under the mouse, SKIN_NONE if none
    int page;          // menu page shown (1-4: MIX TAPE HOME PARAMS), 0 = script
    int grid_open;     // virtual grid window open
    int arc_open;      // virtual arc window open
    int keys_open;     // virtual keyboard window open
    int crow_open;     // virtual crow window open
    int input;         // SKIN_INPUT_*: what norns' input hears (the MIC button)
    int button_down;   // button held by the mouse, SKIN_NONE if none
    int recording;     // a gif of the screen is being recorded
    const char *toast; // a short message on the box (import, screenshot), NULL for none
    double out_level;  // the OUT fader, 0-1 (the output level's place on its scale)
    int out_muted;     // MUTE is on
} skin_state_t;

typedef struct {
    int w, h;                    // canvas in device pixels
    double s;                    // device pixels per layout unit
    double ox, oy;               // box origin in device pixels
    int oled_x, oled_y, oled_px; // oled position and pixel size (integer: crisp pixels)
} skin_geom_t;

// fit the box (plus margin) into a w x h canvas
extern void skin_geom(int w, int h, skin_geom_t *g);

// control under device pixel x/y, SKIN_NONE if none
extern int skin_hit(const skin_geom_t *g, double x, double y);

// centre of the encoder nearest to device pixel x/y: 1-3
extern int skin_nearest_enc(const skin_geom_t *g, double x, double y);

// the OUT fader's value (0-1) for the pointer at device pixel height y
extern double skin_fader_value(const skin_geom_t *g, double y);

// the oled pixel (0-127, 0-63) under device pixel x/y; false if it is not on the oled
extern int skin_oled_pixel(const skin_geom_t *g, double x, double y, int *ox, int *oy);

// background, box, bezel and labels: everything that only changes with the size
extern void skin_draw_static(cairo_t *cr, const skin_geom_t *g);

// keys and knobs
extern void skin_draw_controls(cairo_t *cr, const skin_geom_t *g, const skin_state_t *st);

// the oled frame: oled is a 128x64 image surface
extern void skin_draw_oled(cairo_t *cr, const skin_geom_t *g, cairo_surface_t *oled);

#ifdef __cplusplus
}
#endif
