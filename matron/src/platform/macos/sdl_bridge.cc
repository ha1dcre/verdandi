/*
 * sdl_bridge.cc
 *
 * main-thread side of the macOS screen/input path (see sdl_bridge.h).
 *
 * the window shows the device (device_skin.h): the oled behind its bezel and K1-K3 /
 * E1-E3 where they sit on a standard norns. click a key to press it (held while the
 * button is held); drag a knob up/right or down/left, or use the wheel over it, to turn
 * it. the wheel elsewhere turns the nearest knob. keys and knobs show what norns
 * receives, whatever sent it (mouse, keyboard, midi mapping): matron reports every
 * key/enc event back through sdl_bridge_show_control().
 *
 * the button row above the device opens the menu pages, maiden, the manual and three
 * virtual devices, each in a window of its own:
 *  - GRID: libmonome sees a usb grid (vgrid_device.h), 64, 128 or 256 (the switch under
 *    it; the choice is kept in ~/.vgrid-size). click a key to press it, drag across keys to
 *    play them in turn, right-click (or ctrl-click) to latch a key down, for chords.
 *  - ARC: libmonome sees an arc with 4 rings. drag round a knob (or use the wheel over it)
 *    to turn it, right-click to push it.
 *  - KEYS: a two octave keyboard that norns sees as the midi device "Verdandi keys" (other
 *    apps see it too). click a key, higher up for softer; with the window in front the
 *    computer keyboard plays it (vkeys_skin.h).
 * the open windows are laid out round the norns window, as large as the screen allows;
 * when the last one closes, the norns window goes back.
 *
 * files dropped on any window go into dust (dust_import.h): audio is linked, or copied with
 * option held; scripts are copied. the menu bar (mac_menu.h)
 * imports, shows dust and the tape folder, and saves the screen as a png or records it
 * as a gif, to the desktop.
 *
 * keyboard: plain keys go to norns as the usb keyboard, where lua/core/keyboard.lua turns
 * 1 2 3 / QW AS ZX into K1-3 / E1-3 when nothing else wants them. window_keys.h adds the
 * overrides for scripts that use the keyboard: Option + those keys, and Option+Esc to force
 * them without Option. the computer keyboard is not mapped to keys/encs: while the
 * window has focus it is the norns usb keyboard (virtual hid device, linux key codes), as
 * on hardware.
 */

#include <cairo.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <removefile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <deque>
#include <string>
#include <vector>

#include "event_custom.h"
#include "event_types.h"
#include "events.h"

#include "app_audio.h"
#include "device.h"
#include "device_list.h"
#include "device_monitor_macos.h"
#include "device_runloop.h"
#include "device_skin.h"
#include "dust_import.h"
#include "gif_writer.h"
#include "mac_menu.h"
#include "mac_update.h"
#include "open_url.h"
#include "product.h"
#include "sdl_bridge.h"
#include "varc_skin.h"
#include "vgrid_device.h"
#include "vgrid_skin.h"
#include "vkeys_skin.h"
#include "window_keys.h"

typedef enum {
    BRIDGE_CREATE_WINDOW,
    BRIDGE_PRESENT,
    BRIDGE_DESTROY_WINDOW,
} bridge_op_t;

typedef struct {
    bridge_op_t op;
    const char *title;
    int w, h;
    SDL_Window *window;
    SDL_Surface *src;
    dispatch_semaphore_t done;
} bridge_req_t;

static Uint32 bridge_event_type = (Uint32)-1;
static Uint32 show_event_type = (Uint32)-1;
static Uint32 device_event_type = (Uint32)-1;
static Uint32 toast_event_type = (Uint32)-1;
static pthread_t main_thread;

// device_event_type codes
enum {
    DEV_GRID_LEDS = 0,
    DEV_TOGGLE = 1,
    DEV_ARC_LEDS = 2,
    DEV_APP_INPUT = 3,   // data1 target, data2 label (strdup, "" for none)
    DEV_INPUT_CHECK = 4, // data1 = which select it checks
    DEV_CHOOSE = 5,      // data1 folder (strdup)
    DEV_CHOOSE_CLOSE = 6,
};

//--- drawing (main thread only)

static SDL_Window *the_window = NULL;
static skin_state_t skin = {};
static skin_geom_t geom = {};
// the last oled frame, kept so the window can be redrawn without matron
static cairo_surface_t *oled = NULL;
static int have_frame = 0;
// the device without the oled, at the current size
static cairo_surface_t *static_layer = NULL;
// what goes to the window
static cairo_surface_t *canvas = NULL;
static SDL_Surface *canvas_sdl = NULL;

static void free_layers(void) {
    if (canvas_sdl != NULL) {
        SDL_FreeSurface(canvas_sdl);
        canvas_sdl = NULL;
    }
    if (canvas != NULL) {
        cairo_surface_destroy(canvas);
        canvas = NULL;
    }
    if (static_layer != NULL) {
        cairo_surface_destroy(static_layer);
        static_layer = NULL;
    }
}

// (re)build the layers for a w x h pixel window surface; false if out of memory
static bool ensure_layers(int w, int h) {
    if (canvas != NULL && geom.w == w && geom.h == h) {
        return true;
    }
    free_layers();
    skin_geom(w, h, &geom);
    static_layer = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    canvas = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(static_layer) != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status(canvas) != CAIRO_STATUS_SUCCESS) {
        free_layers();
        return false;
    }
    cairo_t *cr = cairo_create(static_layer);
    skin_draw_static(cr, &geom);
    cairo_destroy(cr);
    // cairo RGB24 is native-endian xRGB, which is SDL's RGB888
    cairo_surface_flush(canvas);
    canvas_sdl = SDL_CreateRGBSurfaceWithFormatFrom(cairo_image_surface_get_data(canvas), w, h, 32,
                                                    cairo_image_surface_get_stride(canvas), SDL_PIXELFORMAT_RGB888);
    if (canvas_sdl == NULL) {
        free_layers();
        return false;
    }
    SDL_SetSurfaceBlendMode(canvas_sdl, SDL_BLENDMODE_NONE);
    return true;
}

static void draw_oled(cairo_t *cr) {
    if (have_frame) {
        skin_draw_oled(cr, &geom, oled);
    }
}

// full: the whole device; otherwise only the oled
static void redraw(bool full) {
    if (the_window == NULL) {
        return;
    }
    SDL_Surface *dst = SDL_GetWindowSurface(the_window);
    if (dst == NULL || !ensure_layers(dst->w, dst->h)) {
        return;
    }
    cairo_t *cr = cairo_create(canvas);
    SDL_Rect r;
    if (full) {
        cairo_set_source_surface(cr, static_layer, 0, 0);
        cairo_paint(cr);
        skin_draw_controls(cr, &geom, &skin);
        draw_oled(cr);
        r = {0, 0, dst->w, dst->h};
    } else {
        draw_oled(cr);
        // the bezel interior, generously
        int pad = (int)(20 * geom.s);
        r = {geom.oled_x - pad, geom.oled_y - pad, 128 * geom.oled_px + 2 * pad, 64 * geom.oled_px + 2 * pad};
    }
    cairo_destroy(cr);
    cairo_surface_flush(canvas);
    SDL_Rect clip = r;
    SDL_BlitSurface(canvas_sdl, &clip, dst, &r);
    SDL_UpdateWindowSurfaceRects(the_window, &r, 1);
}

static void gif_add_frame(void);

static void present(SDL_Window *window, SDL_Surface *src) {
    // a frame for a window that is gone (quit) is dropped
    if (src == NULL || window == NULL || window != the_window) {
        return;
    }
    if (oled == NULL) {
        oled = cairo_image_surface_create(CAIRO_FORMAT_RGB24, src->w, src->h);
    }
    cairo_surface_flush(oled);
    SDL_ConvertPixels(src->w, src->h, src->format->format, src->pixels, src->pitch, SDL_PIXELFORMAT_RGB888,
                      cairo_image_surface_get_data(oled), cairo_image_surface_get_stride(oled));
    cairo_surface_mark_dirty(oled);
    bool first = !have_frame;
    have_frame = 1;
    redraw(first || canvas == NULL);
    gif_add_frame();
}

// dock icon from the png in NORNS_ICON (set by macos/norns.sh)
static void set_icon(SDL_Window *window) {
    const char *path = getenv("NORNS_ICON");
    if (path == NULL) {
        return;
    }
    cairo_surface_t *img = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
        // cairo ARGB32 is native-endian ARGB, which is SDL's ARGB8888
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(
            cairo_image_surface_get_data(img), cairo_image_surface_get_width(img), cairo_image_surface_get_height(img),
            32, cairo_image_surface_get_stride(img), SDL_PIXELFORMAT_ARGB8888);
        if (s != NULL) {
            SDL_SetWindowIcon(window, s);
            SDL_FreeSurface(s);
        }
    }
    cairo_surface_destroy(img);
}

static void fit_main_window(SDL_Window *w);

static void bridge_do(bridge_req_t *req) {
    switch (req->op) {
    case BRIDGE_CREATE_WINDOW:
        // the window is the device: one layout unit per point (sdl.cc's 512x256 is for the bare oled)
        (void)req->w;
        (void)req->h;
        req->window =
            SDL_CreateWindow(req->title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SKIN_W + 2 * SKIN_MARGIN,
                             SKIN_H + 2 * SKIN_MARGIN, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
        if (req->window == NULL) {
            fprintf(stderr, "sdl_bridge: SDL_CreateWindow failed: %s\n", SDL_GetError());
        } else {
            SDL_SetWindowMinimumSize(req->window, (SKIN_W + 2 * SKIN_MARGIN) / 3, (SKIN_H + 2 * SKIN_MARGIN) / 3);
            fit_main_window(req->window);
            set_icon(req->window);
            the_window = req->window;
            // drops from apps other than the Finder
            mac_drop_adopt_windows();
        }
        break;
    case BRIDGE_PRESENT:
        present(req->window, req->src);
        break;
    case BRIDGE_DESTROY_WINDOW:
        if (the_window == req->window) {
            the_window = NULL;
            free_layers();
        }
        SDL_DestroyWindow(req->window);
        break;
    }
}

static void bridge_call(bridge_req_t *req) {
    if (pthread_equal(pthread_self(), main_thread)) {
        bridge_do(req);
        return;
    }
    req->done = dispatch_semaphore_create(0);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = bridge_event_type;
    ev.user.data1 = req;
    if (SDL_PushEvent(&ev) < 1) {
        fprintf(stderr, "sdl_bridge: SDL_PushEvent failed: %s\n", SDL_GetError());
    } else {
        dispatch_semaphore_wait(req->done, DISPATCH_TIME_FOREVER);
    }
    dispatch_release(req->done);
}

//--- toasts: a short message on the box (main thread)

static std::string toast_text;
static unsigned toast_gen = 0;
static bool recording = false;

static Uint32 toast_timeout(Uint32 interval, void *gen) {
    (void)interval;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = toast_event_type;
    ev.user.code = 1; // clear, if still the same message
    ev.user.data1 = gen;
    SDL_PushEvent(&ev);
    return 0;
}

// a message for a few seconds; while a gif is recorded the recording note stays instead
static void show_toast(const std::string &text, Uint32 ms = 3500) {
    toast_text = text;
    toast_gen++;
    skin.toast = toast_text.c_str();
    redraw(true);
    if (ms > 0) {
        SDL_AddTimer(ms, toast_timeout, (void *)(uintptr_t)toast_gen);
    }
}

static void recording_toast(void) {
    show_toast("REC  GIF  \xc2\xb7  shift-cmd-G stops", 0);
}

static void toast_expired(unsigned gen) {
    if (gen != toast_gen) {
        return;
    }
    if (recording) {
        recording_toast();
        return;
    }
    toast_text.clear();
    skin.toast = NULL;
    redraw(true);
}

// any thread: a message, through the event queue
static void post_toast(const std::string &text) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = toast_event_type;
    ev.user.code = 0;
    ev.user.data1 = strdup(text.c_str());
    if (SDL_PushEvent(&ev) < 1) {
        free(ev.user.data1);
    }
}

// mac_menu.m's messages (any thread)
static void note_toast(const char *text) {
    post_toast(text);
}

//--- calls into lua: _norns.<fn>(args...) on matron's thread (any thread)

struct lua_call {
    std::string fn;
    std::vector<std::string> args;
};

static void lua_call_weave(lua_State *l, void *value, void *context) {
    (void)context;
    lua_call *c = (lua_call *)value;
    lua_getglobal(l, "_norns");
    if (lua_istable(l, -1)) {
        lua_getfield(l, -1, c->fn.c_str());
        if (lua_isfunction(l, -1)) {
            for (const std::string &a : c->args) {
                lua_pushstring(l, a.c_str());
            }
            if (lua_pcall(l, (int)c->args.size(), 0, 0) != LUA_OK) {
                fprintf(stderr, "sdl_bridge: _norns.%s: %s\n", c->fn.c_str(), lua_tostring(l, -1));
                lua_pop(l, 1);
            }
        } else {
            lua_pop(l, 1);
        }
    }
    lua_pop(l, 1);
}

static void lua_call_free(void *value, void *context) {
    (void)context;
    delete (lua_call *)value;
}

static struct event_custom_ops lua_call_ops = {"macos lua call", lua_call_weave, lua_call_free};

static void call_lua(const char *fn, std::vector<std::string> args) {
    event_post(event_custom_new(&lua_call_ops, new lua_call{fn, std::move(args)}, NULL));
}

// Devices > Audio Input: lua decides (lua/core/menu.lua)
static void input_chosen(const char *mode, const char *bundle, const char *name) {
    call_lua("input_chosen", {mode != NULL ? mode : "mic", bundle != NULL ? bundle : "", name != NULL ? name : ""});
}

//--- extra windows: grid, arc, keys

typedef struct {
    SDL_Window *window;
    int uw, uh; // size at 1:1, in points
    cairo_surface_t *canvas;
    SDL_Surface *canvas_sdl;
    int w, h; // canvas in pixels
} pane_t;

static void pane_free_canvas(pane_t *p) {
    if (p->canvas_sdl != NULL) {
        SDL_FreeSurface(p->canvas_sdl);
        p->canvas_sdl = NULL;
    }
    if (p->canvas != NULL) {
        cairo_surface_destroy(p->canvas);
        p->canvas = NULL;
    }
    p->w = p->h = 0;
}

// a canvas the size of the window surface; *resized when it is new. NULL if none
static cairo_t *pane_begin(pane_t *p, bool *resized) {
    *resized = false;
    if (p->window == NULL) {
        return NULL;
    }
    SDL_Surface *dst = SDL_GetWindowSurface(p->window);
    if (dst == NULL) {
        return NULL;
    }
    if (p->canvas == NULL || p->w != dst->w || p->h != dst->h) {
        pane_free_canvas(p);
        p->canvas = cairo_image_surface_create(CAIRO_FORMAT_RGB24, dst->w, dst->h);
        p->canvas_sdl = SDL_CreateRGBSurfaceWithFormatFrom(cairo_image_surface_get_data(p->canvas), dst->w, dst->h, 32,
                                                           cairo_image_surface_get_stride(p->canvas),
                                                           SDL_PIXELFORMAT_RGB888);
        if (p->canvas_sdl == NULL) {
            pane_free_canvas(p);
            return NULL;
        }
        SDL_SetSurfaceBlendMode(p->canvas_sdl, SDL_BLENDMODE_NONE);
        p->w = dst->w;
        p->h = dst->h;
        *resized = true;
    }
    return cairo_create(p->canvas);
}

static void pane_end(pane_t *p, cairo_t *cr) {
    cairo_destroy(cr);
    cairo_surface_flush(p->canvas);
    SDL_Surface *dst = SDL_GetWindowSurface(p->window);
    if (dst != NULL) {
        SDL_BlitSurface(p->canvas_sdl, NULL, dst, NULL);
        SDL_UpdateWindowSurface(p->window);
    }
}

static SDL_Window *pane_open(pane_t *p, const char *title, int uw, int uh) {
    p->uw = uw;
    p->uh = uh;
    p->window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, uw, uh,
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (p->window != NULL) {
        SDL_SetWindowMinimumSize(p->window, uw / 3, uh / 3);
        set_icon(p->window);
        mac_drop_adopt_windows();
    }
    return p->window;
}

static void pane_close(pane_t *p) {
    if (p->window != NULL) {
        SDL_DestroyWindow(p->window);
        p->window = NULL;
    }
    pane_free_canvas(p);
}

static bool is_pane(const pane_t *p, Uint32 window_id) {
    return p->window != NULL && window_id == SDL_GetWindowID(p->window);
}

// window points -> canvas pixels
static void pane_pixels(const pane_t *p, int x, int y, double *px, double *py) {
    int ww = 1, wh = 1;
    SDL_GetWindowSize(p->window, &ww, &wh);
    double k = p->w > 0 && ww > 0 ? (double)p->w / ww : 1;
    *px = x * k;
    *py = y * k;
}

static pane_t grid_pane = {};
static pane_t arc_pane = {};
static pane_t keys_pane = {};

//--- window layout

#define MAIN_W (SKIN_W + 2 * SKIN_MARGIN)
#define MAIN_H (SKIN_H + 2 * SKIN_MARGIN)
#define LAYOUT_GAP 12
#define LAYOUT_PAD 8

static SDL_Rect main_before_extras;
static bool main_saved = false;

// the free screen area (no menu bar, no dock) of the display the norns window is on
static bool usable_bounds(SDL_Rect *u) {
    int d = the_window != NULL ? SDL_GetWindowDisplayIndex(the_window) : 0;
    return SDL_GetDisplayUsableBounds(d < 0 ? 0 : d, u) == 0 && u->w > 0 && u->h > 0;
}

// window positions and sizes are the content; the title bar sits above it
static int title_height(SDL_Window *w) {
    int top = 0, left, bottom, right;
    if (w == NULL || SDL_GetWindowBordersSize(w, &top, &left, &bottom, &right) != 0 || top <= 0) {
        top = 28;
    }
    return top;
}

// a first window that does not fit the screen opens smaller
static void fit_main_window(SDL_Window *w) {
    SDL_Rect u;
    int d = SDL_GetWindowDisplayIndex(w);
    if (SDL_GetDisplayUsableBounds(d < 0 ? 0 : d, &u) != 0) {
        return;
    }
    double k = fmin(1.0, fmin((double)(u.w - 2 * LAYOUT_PAD) / MAIN_W,
                              (double)(u.h - 2 * LAYOUT_PAD - title_height(w)) / MAIN_H));
    if (k < 1.0) {
        SDL_SetWindowSize(w, (int)(MAIN_W * k), (int)(MAIN_H * k));
        SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

static void restore_main_window(void) {
    if (main_saved && the_window != NULL) {
        SDL_SetWindowSize(the_window, main_before_extras.w, main_before_extras.h);
        SDL_SetWindowPosition(the_window, main_before_extras.x, main_before_extras.y);
    }
    main_saved = false;
}

// norns and the open device windows, all at one scale, centred: the extras in a row below
// norns, all in one row, the extras in a column right of norns, or everything stacked,
// whichever leaves them largest. the norns window is remembered for when they close
static void arrange_windows(void) {
    pane_t *open[3];
    int n = 0;
    for (pane_t *p : {&grid_pane, &keys_pane, &arc_pane}) {
        if (p->window != NULL) {
            open[n++] = p;
        }
    }
    SDL_Rect u;
    if (the_window == NULL || !usable_bounds(&u)) {
        return;
    }
    if (n == 0) {
        restore_main_window();
        return;
    }
    if (!main_saved) {
        SDL_GetWindowPosition(the_window, &main_before_extras.x, &main_before_extras.y);
        SDL_GetWindowSize(the_window, &main_before_extras.w, &main_before_extras.h);
        main_saved = true;
    }
    int t = title_height(the_window);
    double aw = u.w - 2 * LAYOUT_PAD, ah = u.h - 2 * LAYOUT_PAD;
    int sum_w = 0, sum_h = 0, max_w = 0, max_h = 0;
    for (int i = 0; i < n; i++) {
        sum_w += open[i]->uw;
        sum_h += open[i]->uh;
        max_w = open[i]->uw > max_w ? open[i]->uw : max_w;
        max_h = open[i]->uh > max_h ? open[i]->uh : max_h;
    }
    // per layout: content width/height at 1:1 and the fixed gaps and title bars
    enum { BELOW,
           ROW,
           COLUMN,
           STACK };
    double k[4];
    k[BELOW] = fmin((aw - (n - 1) * LAYOUT_GAP) / fmax(MAIN_W, sum_w), (ah - 2 * t - LAYOUT_GAP) / (MAIN_H + max_h));
    k[ROW] = fmin((aw - n * LAYOUT_GAP) / (MAIN_W + sum_w), (ah - t) / fmax(MAIN_H, max_h));
    k[COLUMN] = fmin((aw - LAYOUT_GAP) / (MAIN_W + max_w),
                     fmin((ah - t) / MAIN_H, (ah - n * t - (n - 1) * LAYOUT_GAP) / sum_h));
    k[STACK] = fmin(aw / fmax(MAIN_W, max_w), (ah - (n + 1) * t - n * LAYOUT_GAP) / (MAIN_H + sum_h));
    int best = BELOW;
    for (int i = 1; i < 4; i++) {
        if (k[i] > k[best] + 0.01) {
            best = i;
        }
    }
    double s = fmin(1.0, k[best]);
    int mw = (int)(MAIN_W * s), mh = (int)(MAIN_H * s);
    int ew[3], eh[3];
    for (int i = 0; i < n; i++) {
        ew[i] = (int)(open[i]->uw * s);
        eh[i] = (int)(open[i]->uh * s);
    }
    int mx, my, ex[3], ey[3];
    if (best == BELOW || best == ROW) {
        int row_w = (n - 1) * LAYOUT_GAP, row_h = 0;
        for (int i = 0; i < n; i++) {
            row_w += ew[i];
            row_h = eh[i] > row_h ? eh[i] : row_h;
        }
        if (best == BELOW) {
            my = u.y + t + (u.h - (t + mh + LAYOUT_GAP + t + row_h)) / 2;
            mx = u.x + (u.w - mw) / 2;
            int x = u.x + (u.w - row_w) / 2;
            for (int i = 0; i < n; i++) {
                ex[i] = x;
                ey[i] = my + mh + LAYOUT_GAP + t;
                x += ew[i] + LAYOUT_GAP;
            }
        } else {
            int x = u.x + (u.w - (mw + LAYOUT_GAP + row_w)) / 2;
            mx = x;
            my = u.y + t + (u.h - t - mh) / 2;
            x += mw + LAYOUT_GAP;
            for (int i = 0; i < n; i++) {
                ex[i] = x;
                ey[i] = u.y + t + (u.h - t - eh[i]) / 2;
                x += ew[i] + LAYOUT_GAP;
            }
        }
    } else if (best == COLUMN) {
        int col_w = 0, col_h = (n - 1) * LAYOUT_GAP + (n - 1) * t;
        for (int i = 0; i < n; i++) {
            col_w = ew[i] > col_w ? ew[i] : col_w;
            col_h += eh[i];
        }
        mx = u.x + (u.w - (mw + LAYOUT_GAP + col_w)) / 2;
        my = u.y + t + (u.h - t - mh) / 2;
        int y = u.y + t + (u.h - t - col_h) / 2;
        for (int i = 0; i < n; i++) {
            ex[i] = mx + mw + LAYOUT_GAP;
            ey[i] = y;
            y += eh[i] + LAYOUT_GAP + t;
        }
    } else {
        int total = t + mh;
        for (int i = 0; i < n; i++) {
            total += LAYOUT_GAP + t + eh[i];
        }
        int y = u.y + (u.h - total) / 2 + t;
        mx = u.x + (u.w - mw) / 2;
        my = y;
        y += mh + LAYOUT_GAP + t;
        for (int i = 0; i < n; i++) {
            ex[i] = u.x + (u.w - ew[i]) / 2;
            ey[i] = y;
            y += eh[i] + LAYOUT_GAP + t;
        }
    }
    SDL_SetWindowSize(the_window, mw, mh);
    SDL_SetWindowPosition(the_window, mx, my);
    SDL_RaiseWindow(the_window);
    for (int i = 0; i < n; i++) {
        SDL_SetWindowSize(open[i]->window, ew[i], eh[i]);
        SDL_SetWindowPosition(open[i]->window, ex[i], ey[i]);
        SDL_RaiseWindow(open[i]->window);
    }
}

//--- virtual grid window (main thread, except grid_changed)

static vgrid_device_t *vgrid = NULL;
static vgrid_geom_t grid_geom = {};
static uint8_t grid_held[VGRID_MAX][VGRID_MAX];
static int grid_size = 0;                        // 1-3: 64, 128, 256 (vgrid_sizes); 0 = not read yet
static int grid_mouse_c = -1, grid_mouse_r = -1; // key held by the left button
static int grid_pending = 0;

// device thread: leds changed; one redraw request at a time
static void grid_changed(void *ctx) {
    (void)ctx;
    if (__atomic_exchange_n(&grid_pending, 1, __ATOMIC_ACQ_REL) == 0) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = device_event_type;
        ev.user.code = DEV_GRID_LEDS;
        SDL_PushEvent(&ev);
    }
}

static void grid_redraw(void) {
    __atomic_store_n(&grid_pending, 0, __ATOMIC_RELEASE);
    if (vgrid == NULL) {
        return;
    }
    bool resized;
    cairo_t *cr = pane_begin(&grid_pane, &resized);
    if (cr == NULL) {
        return;
    }
    int cols = vgrid_sizes[grid_size][0], rows = vgrid_sizes[grid_size][1];
    if (resized || grid_geom.cols != cols || grid_geom.rows != rows) {
        vgrid_skin_geom(grid_pane.w, grid_pane.h, cols, rows, &grid_geom);
    }
    uint8_t level[VGRID_MAX][VGRID_MAX];
    int intensity;
    vgrid_levels(vgrid, level, &intensity);
    vgrid_skin_draw(cr, &grid_geom, grid_pane.w, grid_pane.h, level, intensity, grid_held, grid_size);
    pane_end(&grid_pane, cr);
}

// the size choice, in the norns home (norns.sh sets HOME)
static void grid_size_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.vgrid-size", home ? home : ".");
}

static void grid_load_size(void) {
    grid_size = 2;
    char path[1024];
    grid_size_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        int keys = 0;
        if (fscanf(f, "%d", &keys) == 1) {
            grid_size = keys == 64 ? 1 : (keys == 256 ? 3 : 2);
        }
        fclose(f);
    }
}

static void grid_save_size(void) {
    static const int keys[4] = {0, 64, 128, 256};
    char path[1024];
    grid_size_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", keys[grid_size]);
        fclose(f);
    }
}

static void toggle_grid(void);

// a new size: the grid is unplugged and plugged in again at that size, where it was
static void grid_set_size(int size) {
    if (size == grid_size || size < 1 || size > 3) {
        return;
    }
    grid_size = size;
    grid_save_size();
    if (vgrid != NULL) {
        toggle_grid();
        toggle_grid();
    }
}

// a monome device for matron; libmonome has it open when this returns, so its link can go
static void plug_in(vgrid_device_t *g, const char *name) {
    const char *path = vgrid_path(g);
    device_runloop_sync(^{
      dev_list_add(DEV_TYPE_MONOME, path, strdup(name), NULL);
    });
    vgrid_unlink(g);
}

// matron closes its side first (dev_delete -> monome_close), then the pty goes
static void unplug(vgrid_device_t *g) {
    const char *path = vgrid_path(g);
    device_runloop_sync(^{
      dev_list_remove(DEV_TYPE_MONOME, path);
    });
    vgrid_close(g);
}

static void toggle_grid(void) {
    if (vgrid != NULL) {
        unplug(vgrid);
        vgrid = NULL;
        pane_close(&grid_pane);
        memset(grid_held, 0, sizeof grid_held);
        grid_mouse_c = grid_mouse_r = -1;
        skin.grid_open = 0;
        arrange_windows();
        redraw(true);
        return;
    }
    if (grid_size == 0) {
        grid_load_size();
    }
    int cols = vgrid_sizes[grid_size][0], rows = vgrid_sizes[grid_size][1];
    vgrid = vgrid_open("virtual grid", cols, rows, grid_changed, NULL);
    if (vgrid == NULL) {
        fprintf(stderr, "sdl_bridge: virtual grid failed\n");
        return;
    }
    // one unit per point; arrange_windows fits the windows on the screen
    int uw, uh;
    vgrid_skin_units(cols, rows, &uw, &uh);
    if (pane_open(&grid_pane, "grid", uw, uh) != NULL) {
        arrange_windows();
    }
    plug_in(vgrid, "virtual grid");
    skin.grid_open = 1;
    redraw(true);
    grid_redraw();
}

static bool grid_key_at(int x, int y, int *c, int *r) {
    double px, py;
    pane_pixels(&grid_pane, x, y, &px, &py);
    return vgrid_skin_hit(&grid_geom, px, py, c, r);
}

static void grid_press(int c, int r) {
    if (grid_held[r][c] == 0) {
        vgrid_key(vgrid, c, r, 1);
    }
}

// the left button lets go of c,r: up unless it is latched
static void grid_let_go(int c, int r) {
    grid_held[r][c] &= ~VGRID_HELD_MOUSE;
    if (grid_held[r][c] == 0) {
        vgrid_key(vgrid, c, r, 0);
    }
}

static void grid_button(const SDL_MouseButtonEvent *b) {
    if (b->button == SDL_BUTTON_LEFT && b->type == SDL_MOUSEBUTTONDOWN && grid_mouse_c < 0) {
        double px, py;
        pane_pixels(&grid_pane, b->x, b->y, &px, &py);
        int size = vgrid_skin_hit_size(&grid_geom, px, py);
        if (size != 0) {
            grid_set_size(size);
            return;
        }
    }
    int c, r;
    bool hit = grid_key_at(b->x, b->y, &c, &r);
    bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    if (b->button == SDL_BUTTON_LEFT && !ctrl) {
        if (b->type == SDL_MOUSEBUTTONDOWN) {
            if (!hit || grid_mouse_c >= 0) {
                return;
            }
            // a latched key taken by the left button is let go on release
            grid_press(c, r);
            grid_held[r][c] = VGRID_HELD_MOUSE;
            grid_mouse_c = c;
            grid_mouse_r = r;
        } else if (grid_mouse_c >= 0) {
            grid_let_go(grid_mouse_c, grid_mouse_r);
            grid_mouse_c = grid_mouse_r = -1;
        }
    } else if ((b->button == SDL_BUTTON_RIGHT || ctrl) && b->type == SDL_MOUSEBUTTONDOWN && hit) {
        if (grid_held[r][c] & VGRID_HELD_LATCH) {
            grid_held[r][c] &= ~VGRID_HELD_LATCH;
            if (grid_held[r][c] == 0) {
                vgrid_key(vgrid, c, r, 0);
            }
        } else {
            grid_press(c, r);
            grid_held[r][c] |= VGRID_HELD_LATCH;
        }
    }
    grid_redraw();
}

static void grid_motion(const SDL_MouseMotionEvent *m) {
    int c, r;
    if (grid_mouse_c < 0 || !grid_key_at(m->x, m->y, &c, &r) || (c == grid_mouse_c && r == grid_mouse_r)) {
        return;
    }
    // dragging across keys plays them one after the other
    grid_let_go(grid_mouse_c, grid_mouse_r);
    grid_press(c, r);
    grid_held[r][c] |= VGRID_HELD_MOUSE;
    grid_mouse_c = c;
    grid_mouse_r = r;
    grid_redraw();
}

//--- virtual arc window (main thread, except arc_changed)

#define ARC_RINGS 4
#define ARC_NAME "monome arc virtual"
// encoder ticks per turn, as on the hardware
#define ARC_TICKS_PER_TURN 1024.0
// a turn of the mouse round a knob on the screen is a quarter turn of the knob: a small
// knob under a mouse would otherwise be far touchier than the hardware under a hand
#define ARC_DRAG_GEAR 0.25
// ticks a wheel notch (a trackpad scrolls in fractions of one)
#define ARC_WHEEL_TICKS 2

static vgrid_device_t *varc = NULL;
static varc_geom_t arc_geom = {};
static double arc_turns[ARC_RINGS]; // how far each knob turned, for its knurl
static int arc_pushed = 0;          // bit n: push button n held by the right button
static int arc_drag = -1;           // knob turned by the left button
static double arc_last = 0, arc_acc[ARC_RINGS];
static int arc_pending = 0;

static void arc_changed(void *ctx) {
    (void)ctx;
    if (__atomic_exchange_n(&arc_pending, 1, __ATOMIC_ACQ_REL) == 0) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = device_event_type;
        ev.user.code = DEV_ARC_LEDS;
        SDL_PushEvent(&ev);
    }
}

static void arc_redraw(void) {
    __atomic_store_n(&arc_pending, 0, __ATOMIC_RELEASE);
    if (varc == NULL) {
        return;
    }
    bool resized;
    cairo_t *cr = pane_begin(&arc_pane, &resized);
    if (cr == NULL) {
        return;
    }
    if (resized) {
        varc_skin_geom(arc_pane.w, arc_pane.h, ARC_RINGS, &arc_geom);
    }
    uint8_t ring[VARC_RINGS][VARC_LEDS];
    int intensity;
    vgrid_rings(varc, ring, &intensity);
    varc_skin_draw(cr, &arc_geom, arc_pane.w, arc_pane.h, ring, intensity, arc_turns, arc_pushed);
    pane_end(&arc_pane, cr);
}

static void toggle_arc(void) {
    if (varc != NULL) {
        for (int n = 0; n < ARC_RINGS; n++) {
            if (arc_pushed & (1 << n)) {
                vgrid_enc_key(varc, n, 0);
            }
        }
        unplug(varc);
        varc = NULL;
        pane_close(&arc_pane);
        arc_pushed = 0;
        arc_drag = -1;
        skin.arc_open = 0;
        arrange_windows();
        redraw(true);
        return;
    }
    // lua/core/startup.lua tells an arc from a grid by its name: "monome arc..."
    varc = vgrid_open_arc(ARC_NAME, ARC_RINGS, arc_changed, NULL);
    if (varc == NULL) {
        fprintf(stderr, "sdl_bridge: virtual arc failed\n");
        return;
    }
    int uw, uh;
    varc_skin_units(ARC_RINGS, &uw, &uh);
    if (pane_open(&arc_pane, "arc", uw, uh) != NULL) {
        arrange_windows();
    }
    plug_in(varc, ARC_NAME);
    skin.arc_open = 1;
    redraw(true);
    arc_redraw();
}

// shift: 4x finer, command: 4x faster (as on the norns knobs)
static double turn_factor(void) {
    SDL_Keymod mod = SDL_GetModState();
    if (mod & KMOD_SHIFT) {
        return 0.25;
    }
    if (mod & KMOD_GUI) {
        return 4;
    }
    return 1;
}

static void arc_turn(int n, double ticks) {
    arc_acc[n] += ticks;
    int t = (int)arc_acc[n];
    if (t != 0) {
        arc_acc[n] -= t;
        vgrid_enc(varc, n, t);
        arc_turns[n] += t / ARC_TICKS_PER_TURN;
        arc_redraw();
    }
}

static double arc_angle_at(int n, int x, int y, double *dist) {
    double px, py, cx, cy;
    pane_pixels(&arc_pane, x, y, &px, &py);
    varc_skin_center(&arc_geom, n, &cx, &cy);
    *dist = hypot(px - cx, py - cy) / (arc_geom.s > 0 ? arc_geom.s : 1);
    return atan2(py - cy, px - cx);
}

static void arc_button(const SDL_MouseButtonEvent *b) {
    double px, py;
    pane_pixels(&arc_pane, b->x, b->y, &px, &py);
    int n = varc_skin_hit(&arc_geom, px, py);
    bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    if (b->button == SDL_BUTTON_LEFT && !ctrl) {
        if (b->type == SDL_MOUSEBUTTONDOWN && n >= 0 && arc_drag < 0) {
            double d;
            arc_drag = n;
            arc_last = arc_angle_at(n, b->x, b->y, &d);
            SDL_CaptureMouse(SDL_TRUE);
        } else if (b->type == SDL_MOUSEBUTTONUP && arc_drag >= 0) {
            arc_drag = -1;
            SDL_CaptureMouse(SDL_FALSE);
        }
        return;
    }
    // right button (or ctrl-click): the push button, held while the mouse button is
    if (b->type == SDL_MOUSEBUTTONDOWN && n >= 0 && !(arc_pushed & (1 << n))) {
        arc_pushed |= 1 << n;
        vgrid_enc_key(varc, n, 1);
        arc_redraw();
    } else if (b->type == SDL_MOUSEBUTTONUP && arc_pushed) {
        for (int i = 0; i < ARC_RINGS; i++) {
            if (arc_pushed & (1 << i)) {
                vgrid_enc_key(varc, i, 0);
            }
        }
        arc_pushed = 0;
        arc_redraw();
    }
}

static void arc_motion(const SDL_MouseMotionEvent *m) {
    if (arc_drag < 0) {
        return;
    }
    // round the knob: clockwise is up. near the centre the angle jumps, so it waits there
    double d, a = arc_angle_at(arc_drag, m->x, m->y, &d);
    if (d < 8) {
        return;
    }
    double da = a - arc_last;
    while (da > M_PI) {
        da -= 2 * M_PI;
    }
    while (da < -M_PI) {
        da += 2 * M_PI;
    }
    arc_last = a;
    arc_turn(arc_drag, da / (2 * M_PI) * ARC_TICKS_PER_TURN * ARC_DRAG_GEAR * turn_factor());
}

static void arc_wheel(const SDL_MouseWheelEvent *w) {
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    double px, py;
    pane_pixels(&arc_pane, mx, my, &px, &py);
    int n = varc_skin_hit(&arc_geom, px, py);
    if (n < 0) {
        return;
    }
    double dy = w->preciseY != 0 ? w->preciseY : w->y;
    if (w->direction == SDL_MOUSEWHEEL_FLIPPED) {
        dy = -dy;
    }
    arc_turn(n, dy * ARC_WHEEL_TICKS * turn_factor());
}

//--- virtual keyboard window: a CoreMIDI source matron picks up like any app's port

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#define NORNS_KEYS_UID 0x6e726e6b // 'nrnk', so other apps remember the port

static vkeys_state_t vk = {};
static vkeys_geom_t keys_geom = {};
static MIDIEndpointRef keys_src = 0;
static int keys_mouse = -1;        // key held by the left button
static int keys_sounding[VKEYS_N]; // note sent for each key, -1 if silent
static char keys_labels[VKEYS_N][16];

static void keys_send(uint8_t status, uint8_t d1, uint8_t d2) {
    if (keys_src == 0) {
        return;
    }
    Byte buf[64];
    MIDIPacketList *list = (MIDIPacketList *)buf;
    MIDIPacket *p = MIDIPacketListInit(list);
    const Byte msg[3] = {status, d1, d2};
    if (MIDIPacketListAdd(list, sizeof buf, p, 0, 3, msg) != NULL) {
        MIDIReceived(keys_src, list);
    }
}

static void keys_redraw(void) {
    bool resized;
    cairo_t *cr = pane_begin(&keys_pane, &resized);
    if (cr == NULL) {
        return;
    }
    if (resized) {
        vkeys_skin_geom(keys_pane.w, keys_pane.h, &keys_geom);
    }
    vkeys_skin_draw(cr, &keys_geom, keys_pane.w, keys_pane.h, &vk);
    pane_end(&keys_pane, cr);
}

// key k gets a hold of kind bit: the note starts with the first hold
static void keys_hold(int k, int bit, int velocity) {
    if (vk.held[k] == 0) {
        int note = vkeys_note(&vk, k);
        if (note >= 0 && note <= 127) {
            keys_send(0x90, (uint8_t)note, (uint8_t)(velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity)));
            keys_sounding[k] = note;
        }
    }
    vk.held[k] |= (uint8_t)bit;
}

// and loses it: the note ends with the last
static void keys_release(int k, int bits) {
    if (vk.held[k] == 0) {
        return;
    }
    vk.held[k] &= (uint8_t)~bits;
    if (vk.held[k] == 0 && keys_sounding[k] >= 0) {
        keys_send(0x80, (uint8_t)keys_sounding[k], 0);
        keys_sounding[k] = -1;
    }
}

static void keys_release_all(int bits) {
    for (int k = 0; k < VKEYS_N; k++) {
        keys_release(k, bits);
    }
}

static void keys_set_labels(void) {
    for (int k = 0; k < VKEYS_N; k++) {
        int usage = vkeys_typed_usage(k);
        vk.typed_label[k] = NULL;
        if (usage < 0) {
            continue;
        }
        // the letter printed on this key of the user's keyboard layout
        const char *name = SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)usage));
        if (name != NULL && name[0] != '\0' && strlen(name) < sizeof keys_labels[k]) {
            snprintf(keys_labels[k], sizeof keys_labels[k], "%s", name);
            vk.typed_label[k] = keys_labels[k];
        }
    }
}

static void keys_control(int c) {
    switch (c) {
    case VK_OCT_DOWN:
    case VK_OCT_UP: {
        int o = vk.octave + (c == VK_OCT_UP ? 1 : -1);
        if (o >= VKEYS_OCT_MIN && o <= VKEYS_OCT_MAX) {
            // the sounding notes end, as they would on a controller with its octave changed
            keys_release_all(VKEYS_HELD_MOUSE | VKEYS_HELD_LATCH | VKEYS_HELD_TYPED);
            keys_mouse = -1;
            vk.octave = o;
        }
        break;
    }
    case VK_VEL_DOWN:
        vk.velocity = vk.velocity > 20 ? vk.velocity - 10 : 10;
        break;
    case VK_VEL_UP:
        vk.velocity = vk.velocity < 117 ? vk.velocity + 10 : 127;
        break;
    case VK_LATCH:
        vk.latch = !vk.latch;
        if (!vk.latch) {
            keys_release_all(VKEYS_HELD_LATCH);
        }
        break;
    default:
        break;
    }
    keys_redraw();
}

static void toggle_keys(void) {
    if (keys_src != 0) {
        keys_release_all(VKEYS_HELD_MOUSE | VKEYS_HELD_LATCH | VKEYS_HELD_TYPED);
        MIDIEndpointDispose(keys_src);
        keys_src = 0;
        pane_close(&keys_pane);
        keys_mouse = -1;
        skin.keys_open = 0;
        arrange_windows();
        redraw(true);
        return;
    }
    MIDIClientRef client = coremidi_client();
    if (client == 0 || MIDISourceCreate(client, CFSTR(MACOS_PRODUCT " keys"), &keys_src) != noErr) {
        fprintf(stderr, "sdl_bridge: virtual keyboard failed\n");
        keys_src = 0;
        return;
    }
    MIDIObjectSetIntegerProperty(keys_src, kMIDIPropertyUniqueID, NORNS_KEYS_UID);
    if (vk.velocity == 0) {
        vk.octave = 3;
        vk.velocity = 100;
    }
    memset(vk.held, 0, sizeof vk.held);
    for (int k = 0; k < VKEYS_N; k++) {
        keys_sounding[k] = -1;
    }
    vk.hover = vk.button_down = VK_NONE;
    keys_set_labels();
    int uw, uh;
    vkeys_skin_units(&uw, &uh);
    if (pane_open(&keys_pane, "keys", uw, uh) != NULL) {
        arrange_windows();
    }
    skin.keys_open = 1;
    redraw(true);
    keys_redraw();
}

#pragma clang diagnostic pop

static int keys_hit(int x, int y, int *key, int *velocity) {
    double px, py;
    pane_pixels(&keys_pane, x, y, &px, &py);
    return vkeys_skin_hit(&keys_geom, px, py, key, velocity);
}

static void keys_button(const SDL_MouseButtonEvent *b) {
    int key = -1, vel = 0;
    int c = keys_hit(b->x, b->y, &key, &vel);
    bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    bool right = b->button == SDL_BUTTON_RIGHT || (b->button == SDL_BUTTON_LEFT && ctrl);
    if (b->type == SDL_MOUSEBUTTONDOWN) {
        if (c != VK_KEY) {
            if (c != VK_NONE && b->button == SDL_BUTTON_LEFT) {
                vk.button_down = c;
                keys_redraw();
            }
            return;
        }
        if (right || vk.latch) {
            // latch: a click holds the note, the next click lets it go
            if (vk.held[key] & VKEYS_HELD_LATCH) {
                keys_release(key, VKEYS_HELD_LATCH);
            } else {
                keys_hold(key, VKEYS_HELD_LATCH, vel);
            }
        } else if (b->button == SDL_BUTTON_LEFT && keys_mouse < 0) {
            keys_hold(key, VKEYS_HELD_MOUSE, vel);
            keys_mouse = key;
        }
        keys_redraw();
        return;
    }
    if (b->button != SDL_BUTTON_LEFT) {
        return;
    }
    if (vk.button_down != VK_NONE) {
        int down = vk.button_down;
        vk.button_down = VK_NONE;
        if (c == down) {
            keys_control(c);
        } else {
            keys_redraw();
        }
    }
    if (keys_mouse >= 0) {
        keys_release(keys_mouse, VKEYS_HELD_MOUSE);
        keys_mouse = -1;
        keys_redraw();
    }
}

static void keys_motion(const SDL_MouseMotionEvent *m) {
    int key = -1, vel = 0;
    int c = keys_hit(m->x, m->y, &key, &vel);
    if (keys_mouse >= 0) {
        // dragging across keys plays them one after the other
        if (c == VK_KEY && key != keys_mouse) {
            keys_release(keys_mouse, VKEYS_HELD_MOUSE);
            keys_hold(key, VKEYS_HELD_MOUSE, vel);
            keys_mouse = key;
            keys_redraw();
        }
        return;
    }
    int hover = c == VK_KEY ? VK_KEY + key : c;
    if (hover != vk.hover) {
        vk.hover = hover;
        keys_redraw();
    }
}

// the computer keyboard while the keys window is in front; command keys go to the menus
static void keys_typed(const SDL_KeyboardEvent *k) {
    if (k->repeat || (k->keysym.mod & KMOD_GUI)) {
        return;
    }
    int control;
    int key = vkeys_typed((int)k->keysym.scancode, &control);
    if (k->type == SDL_KEYDOWN && control != VK_NONE) {
        keys_control(control);
        return;
    }
    if (key < 0) {
        return;
    }
    if (k->type == SDL_KEYDOWN) {
        keys_hold(key, VKEYS_HELD_TYPED, vk.velocity);
    } else {
        keys_release(key, VKEYS_HELD_TYPED);
    }
    keys_redraw();
}

//--- files in: drops and the import panel (a worker thread links and copies)

struct import_item {
    std::string path;
    int mode;    // MAC_IMPORT_LINK, _COPY (audio into dust/audio), _TAKE (copy, then remove path)
    bool chosen; // a script's file from the open panel: back to lua's fileselect
};

static std::deque<import_item> import_queue;
static pthread_mutex_t import_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t import_cond = PTHREAD_COND_INITIALIZER;
static bool import_running = false;

static std::string dust_dir(void) {
    const char *home = getenv("HOME");
    return std::string(home != NULL ? home : ".") + "/dust";
}

// the user's own home (HOME is the norns home here)
static std::string user_home(void) {
    struct passwd *pw = getpwuid(getuid());
    const char *home = pw != NULL && pw->pw_dir != NULL ? pw->pw_dir : getenv("HOME");
    return home != NULL ? home : ".";
}

// path is in dust already (also through a link to it)
static bool in_dust(const std::string &path) {
    char real[PATH_MAX], dust[PATH_MAX];
    if (realpath(path.c_str(), real) == NULL || realpath(dust_dir().c_str(), dust) == NULL) {
        return false;
    }
    size_t n = strlen(dust);
    return strncmp(real, dust, n) == 0 && real[n] == '/';
}

// a file an app wrote for the drop, and its folder of its own (mac_menu.m)
static void remove_taken(const std::string &path) {
    removefile(path.c_str(), NULL, REMOVEFILE_RECURSIVE);
    size_t slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        rmdir(path.substr(0, slash).c_str());
    }
}

// "kick.wav linked in dust/audio/mac" or "3 copied to dust/audio"
static std::string import_part(int count, const std::string &last, const char *what) {
    return (count == 1 ? last : std::to_string(count)) + " " + what;
}

static void *import_worker(void *arg) {
    (void)arg;
    int linked = 0, copied = 0, scripts = 0, skipped = 0, failed = 0;
    std::string last_linked, last_copied, last_script;
    for (;;) {
        pthread_mutex_lock(&import_lock);
        if (import_queue.empty()) {
            // a batch is done: one message for all of it
            if (linked + copied + scripts + skipped + failed > 0) {
                std::vector<std::string> parts;
                if (scripts > 0) {
                    parts.push_back(scripts == 1 ? "script " + last_script + " added to dust/code"
                                                 : std::to_string(scripts) + " scripts added to dust/code");
                }
                if (linked > 0) {
                    parts.push_back(import_part(linked, last_linked, "linked in dust/audio/" DUST_LINK_DIR));
                }
                if (copied > 0) {
                    parts.push_back(import_part(copied, last_copied, "copied to dust/audio"));
                }
                std::string msg;
                for (size_t i = 0; i < parts.size(); i++) {
                    msg += (i > 0 ? ", " : "") + parts[i];
                }
                if (msg.empty()) {
                    msg = failed > 0 ? "could not add it to dust" : "nothing to import: not audio or a script";
                } else if (skipped + failed > 0) {
                    msg += " (" + std::to_string(skipped + failed) + " left out)";
                }
                post_toast(msg);
                linked = copied = scripts = skipped = failed = 0;
            }
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 1;
            pthread_cond_timedwait(&import_cond, &import_lock, &until);
            pthread_mutex_unlock(&import_lock);
            continue;
        }
        import_item item = import_queue.front();
        import_queue.pop_front();
        pthread_mutex_unlock(&import_lock);

        dust_kind_t kind = dust_import_kind(item.path.c_str());
        // a script's file: audio from outside dust is linked in, anything else stays where it is
        // (a .lua file it reads is no script to install)
        if (item.chosen && (kind != DUST_AUDIO || in_dust(item.path))) {
            call_lua("file_chosen", {item.path});
            continue;
        }
        char dest[2048];
        int as_link = 0;
        // a link to a temporary file dies with it: a copy
        bool link = item.mode == MAC_IMPORT_LINK &&
                    !dust_import_volatile(item.path.c_str(), getenv("TMPDIR"), user_home().c_str());
        if (kind == DUST_SKIP) {
            skipped++;
        } else if (dust_import(dust_dir().c_str(), item.path.c_str(), link, dest, sizeof dest, &as_link)) {
            const char *slash = strrchr(dest, '/');
            std::string name = slash != NULL ? slash + 1 : dest;
            fprintf(stderr, "sdl_bridge: %s %s -> %s\n", as_link ? "linked" : "copied", item.path.c_str(), dest);
            if (kind == DUST_SCRIPT) {
                scripts++;
                last_script = name;
            } else if (as_link) {
                linked++;
                last_linked = name;
            } else {
                copied++;
                last_copied = name;
            }
            // to the script that asked for it, or to a file browser that is open (lua's
            // fileselect takes a dropped sample like a chosen one)
            if (item.chosen) {
                call_lua("file_chosen", {dest});
            } else if (kind == DUST_AUDIO) {
                call_lua("file_dropped", {dest});
            }
        } else {
            fprintf(stderr, "sdl_bridge: import of %s failed\n", item.path.c_str());
            failed++;
            if (item.chosen) {
                call_lua("file_chosen", {item.path});
            }
        }
        if (item.mode == MAC_IMPORT_TAKE) {
            remove_taken(item.path);
        }
    }
    return NULL;
}

static void import_queue_push(const import_item &item) {
    pthread_mutex_lock(&import_lock);
    if (!import_running) {
        pthread_t t;
        if (pthread_create(&t, NULL, import_worker, NULL) == 0) {
            pthread_detach(t);
            import_running = true;
        }
    }
    import_queue.push_back(item);
    pthread_cond_signal(&import_cond);
    pthread_mutex_unlock(&import_lock);
}

static void import_path(const char *path, int mode) {
    import_queue_push({path, mode, false});
}

// the open panel's choice for lua's fileselect (main thread)
static void import_chosen(const char *path) {
    import_queue_push({path, MAC_IMPORT_LINK, true});
}

//--- the screen out: png and gif on the desktop

// the user's own desktop (HOME is the norns home here)
static std::string desktop_dir(void) {
    std::string d = user_home() + "/Desktop";
    struct stat st;
    return stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? d : dust_dir();
}

// "Verdandi 2026-10-20 at 14.03.12.png", like a macOS screenshot
static std::string capture_path(const char *ext) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char name[64];
    strftime(name, sizeof name, MACOS_PRODUCT " %Y-%m-%d at %H.%M.%S", &tm);
    std::string dir = desktop_dir(), path = dir + "/" + name + ext;
    for (int i = 2; access(path.c_str(), F_OK) == 0 && i < 100; i++) {
        path = dir + "/" + name + " " + std::to_string(i) + ext;
    }
    return path;
}

static const char *where(const std::string &path) {
    return path.find("/Desktop/") != std::string::npos ? "Desktop" : "dust";
}

#define CAPTURE_SCALE 4

static void save_screenshot(void) {
    if (!have_frame) {
        show_toast("no screen to save yet");
        return;
    }
    int w = cairo_image_surface_get_width(oled), h = cairo_image_surface_get_height(oled);
    cairo_surface_t *big = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w * CAPTURE_SCALE, h * CAPTURE_SCALE);
    cairo_t *cr = cairo_create(big);
    cairo_scale(cr, CAPTURE_SCALE, CAPTURE_SCALE);
    cairo_set_source_surface(cr, oled, 0, 0);
    // crisp pixels, as on the oled
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_destroy(cr);
    std::string path = capture_path(".png");
    if (cairo_surface_write_to_png(big, path.c_str()) == CAIRO_STATUS_SUCCESS) {
        show_toast(std::string("screenshot saved to ") + where(path));
    } else {
        show_toast("screenshot could not be saved");
    }
    cairo_surface_destroy(big);
}

#define GIF_W 128
#define GIF_H 64
#define GIF_MAX_MS 60000

typedef struct {
    std::vector<std::vector<uint8_t>> frames;
    std::vector<Uint32> at; // ms each frame appeared
} gif_take_t;

static gif_take_t *gif_take = NULL;
static Uint32 gif_start = 0;

// the oled as 16 levels (its 4 bits)
static bool oled_levels(std::vector<uint8_t> &out) {
    if (oled == NULL || cairo_image_surface_get_width(oled) != GIF_W || cairo_image_surface_get_height(oled) != GIF_H) {
        return false;
    }
    cairo_surface_flush(oled);
    const uint8_t *data = cairo_image_surface_get_data(oled);
    int stride = cairo_image_surface_get_stride(oled);
    out.resize(GIF_W * GIF_H);
    for (int y = 0; y < GIF_H; y++) {
        const uint32_t *row = (const uint32_t *)(data + y * stride);
        for (int x = 0; x < GIF_W; x++) {
            uint32_t p = row[x];
            int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            int v = r > g ? (r > b ? r : b) : (g > b ? g : b);
            out[y * GIF_W + x] = (uint8_t)((v * 15 + 127) / 255);
        }
    }
    return true;
}

static void gif_stop(void);

static void gif_add_frame(void) {
    if (gif_take == NULL) {
        return;
    }
    Uint32 now = SDL_GetTicks() - gif_start;
    if (now > GIF_MAX_MS) {
        gif_stop();
        return;
    }
    std::vector<uint8_t> f;
    if (!oled_levels(f) || (!gif_take->frames.empty() && gif_take->frames.back() == f)) {
        return;
    }
    gif_take->frames.push_back(std::move(f));
    gif_take->at.push_back(now);
}

typedef struct {
    gif_take_t *take;
    Uint32 end;
    std::string path;
} gif_job_t;

static void *gif_encode(void *arg) {
    gif_job_t *job = (gif_job_t *)arg;
    gif_take_t *take = job->take;
    FILE *f = fopen(job->path.c_str(), "wb");
    bool ok = f != NULL;
    if (ok) {
        gif_writer_t g;
        ok = gif_begin(&g, f, GIF_W, GIF_H, CAPTURE_SCALE);
        // delays in hundredths, rounded so the total stays true
        Uint32 shown = 0;
        for (size_t i = 0; ok && i < take->frames.size(); i++) {
            Uint32 until = i + 1 < take->at.size() ? take->at[i + 1] : job->end;
            int cs = (int)((until - take->at[0]) / 10) - (int)(shown / 10);
            shown += (Uint32)(cs > 0 ? cs : 0) * 10;
            ok = gif_frame(&g, take->frames[i].data(), cs);
        }
        ok = ok && gif_end(&g);
        ok = fclose(f) == 0 && ok;
    }
    if (ok) {
        post_toast(std::string("GIF saved to ") + where(job->path));
    } else {
        unlink(job->path.c_str());
        post_toast("GIF could not be saved");
    }
    delete take;
    delete job;
    return NULL;
}

static void gif_stop(void) {
    if (gif_take == NULL) {
        return;
    }
    gif_job_t *job = new gif_job_t{gif_take, SDL_GetTicks() - gif_start, capture_path(".gif")};
    gif_take = NULL;
    recording = false;
    skin.recording = 0;
    mac_menu_set_recording(0);
    if (job->take->frames.empty()) {
        delete job->take;
        delete job;
        show_toast("nothing recorded");
        return;
    }
    show_toast("saving GIF", 0);
    pthread_t t;
    if (pthread_create(&t, NULL, gif_encode, job) == 0) {
        pthread_detach(t);
    } else {
        delete job->take;
        delete job;
        show_toast("GIF could not be saved");
    }
}

static void gif_toggle(void) {
    if (gif_take != NULL) {
        gif_stop();
        return;
    }
    if (!have_frame) {
        show_toast("no screen to record yet");
        return;
    }
    gif_take = new gif_take_t();
    gif_start = SDL_GetTicks();
    recording = true;
    skin.recording = 1;
    mac_menu_set_recording(1);
    // the screen as it is now: a still screen records too
    gif_add_frame();
    recording_toast();
}

//--- menu bar

static void show_in_finder(const std::string &dir) {
    mkdir(dir.c_str(), 0755);
    macos_open_url(dir.c_str());
}

static void menu_item(int id) {
    switch (id) {
    case MAC_MENU_DUST:
        show_in_finder(dust_dir());
        break;
    case MAC_MENU_TAPE:
        mkdir((dust_dir() + "/audio").c_str(), 0755);
        show_in_finder(dust_dir() + "/audio/tape");
        break;
    case MAC_MENU_SCREENSHOT:
        save_screenshot();
        break;
    case MAC_MENU_GIF:
        gif_toggle();
        break;
    case MAC_MENU_GRID:
        toggle_grid();
        break;
    case MAC_MENU_ARC:
        toggle_arc();
        break;
    case MAC_MENU_KEYS:
        toggle_keys();
        break;
    default:
        break;
    }
}

//--- input

static void post_key(int n, int z) {
    union event_data *ev = event_data_new(EVENT_KEY);
    ev->key.n = n;
    ev->key.val = z;
    event_post(ev);
}

static void post_enc(int n, int delta) {
    union event_data *ev = event_data_new(EVENT_ENC);
    ev->enc.n = n;
    ev->enc.delta = delta;
    event_post(ev);
}

// a button on the device for lua (_norns.window_button): 1-4 menu page, 5 manual, 6 mic
static void post_window_button(int n) {
    union event_data *ev = event_data_new(EVENT_WINDOW_BUTTON);
    ev->key.n = n;
    ev->key.val = 1;
    event_post(ev);
}

static void button_action(int control) {
    if (control >= SKIN_PAGE1 && control <= SKIN_PAGE4) {
        post_window_button(control - SKIN_PAGE1 + 1);
    } else if (control == SKIN_MANUAL) {
        post_window_button(5);
    } else if (control == SKIN_MIC) {
        post_window_button(6);
    } else if (control == SKIN_MAIDEN) {
        macos_open_url("http://127.0.0.1:5000/maiden");
    } else if (control == SKIN_GRID) {
        toggle_grid();
    } else if (control == SKIN_ARC) {
        toggle_arc();
    } else if (control == SKIN_KEYS) {
        toggle_keys();
    } else if (control == SKIN_MUTE) {
        call_lua("output_mute", {});
    }
}

// window points -> window surface pixels (retina)
static void to_pixels(Uint32 window_id, int x, int y, double *px, double *py) {
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    int ww = 1, wh = 1;
    if (window != NULL) {
        SDL_GetWindowSize(window, &ww, &wh);
    }
    double k = geom.w > 0 && ww > 0 ? (double)geom.w / ww : 1;
    *px = x * k;
    *py = y * k;
}

static int held_key = 0;        // key held by the mouse, 1-3
static int drag_enc = 0;        // knob turned by the mouse, 1-3
static bool drag_fader = false; // the OUT fader held by the mouse
static bool drag_oled = false;  // pressed on the oled on the MIX page (lua/core/menu/mix.lua)
static double drag_acc = 0;
static int drag_x, drag_y;
// mouse travel per detent, in layout units; with shift 4x finer, with command 4x faster
#define DRAG_STEP 5.0

static double drag_step(void) {
    return DRAG_STEP / turn_factor();
}

static SDL_Cursor *cursor_hand = NULL;
static SDL_Cursor *cursor_arrow = NULL;

static void set_hover(int control) {
    if (control == skin.hover) {
        return;
    }
    skin.hover = control;
    if (cursor_hand == NULL) {
        cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    }
    SDL_SetCursor(control != SKIN_NONE ? cursor_hand : cursor_arrow);
    redraw(true);
}

// the OUT fader to the pointer: shown at once, the level set by lua (output_level)
static void fader_to(double py) {
    skin.out_level = skin_fader_value(&geom, py);
    skin.out_muted = 0;
    redraw(true);
    call_lua("output_fader", {std::to_string(skin.out_level)});
}

// the mouse on the oled, as oled pixels, for the menu page (MIX only: its levels)
static bool mouse_on_mix(void) {
    return skin.page == 1;
}

static void oled_mouse(double px, double py, const char *kind, int value) {
    int ox, oy;
    if (!skin_oled_pixel(&geom, px, py, &ox, &oy)) {
        // dragged off the oled: the nearest pixel on it
        double fx = (px - geom.oled_x) / geom.oled_px, fy = (py - geom.oled_y) / geom.oled_px;
        ox = fx < 0 ? 0 : fx > 127 ? 127
                                   : (int)fx;
        oy = fy < 0 ? 0 : fy > 63 ? 63
                                  : (int)fy;
    }
    call_lua("screen_mouse", {std::to_string(ox), std::to_string(oy), kind, std::to_string(value)});
}

static void handle_button(const SDL_MouseButtonEvent *b) {
    if (b->button != SDL_BUTTON_LEFT) {
        return;
    }
    double px, py;
    to_pixels(b->windowID, b->x, b->y, &px, &py);
    if (b->type == SDL_MOUSEBUTTONDOWN) {
        if (held_key != 0 || drag_enc != 0 || drag_fader || drag_oled || skin.button_down != SKIN_NONE) {
            return;
        }
        int c = skin_hit(&geom, px, py);
        int ox, oy;
        if (c == SKIN_FADER) {
            if (b->clicks >= 2) {
                // double click: back to 0 dB, and no drag after it (the pointer's slightest
                // move would take the fader straight back to it)
                call_lua("output_reset", {});
            } else {
                drag_fader = true;
                SDL_CaptureMouse(SDL_TRUE);
                fader_to(py);
            }
        } else if (c == SKIN_NONE && mouse_on_mix() && skin_oled_pixel(&geom, px, py, &ox, &oy)) {
            drag_oled = true;
            SDL_CaptureMouse(SDL_TRUE);
            oled_mouse(px, py, b->clicks >= 2 ? "double" : "down", 0);
        } else if (c >= SKIN_PAGE1) {
            // buttons act on release over the same button, like any mac button
            skin.button_down = c;
            redraw(true);
        } else if (c >= SKIN_K1 && c <= SKIN_K3) {
            held_key = c - SKIN_K1 + 1;
            post_key(held_key, 1);
        } else if (c >= SKIN_E1 && c <= SKIN_E3) {
            drag_enc = c - SKIN_E1 + 1;
            drag_acc = 0;
            drag_x = b->x;
            drag_y = b->y;
            // keep getting motion when the pointer leaves the window
            SDL_CaptureMouse(SDL_TRUE);
        }
    } else {
        if (skin.button_down != SKIN_NONE) {
            int c = skin.button_down;
            skin.button_down = SKIN_NONE;
            redraw(true);
            if (skin_hit(&geom, px, py) == c) {
                button_action(c);
            }
        }
        if (held_key != 0) {
            post_key(held_key, 0);
            held_key = 0;
        }
        if (drag_enc != 0) {
            drag_enc = 0;
            SDL_CaptureMouse(SDL_FALSE);
        }
        if (drag_fader) {
            drag_fader = false;
            SDL_CaptureMouse(SDL_FALSE);
        }
        if (drag_oled) {
            drag_oled = false;
            SDL_CaptureMouse(SDL_FALSE);
            oled_mouse(px, py, "up", 0);
        }
    }
}

static void handle_motion(const SDL_MouseMotionEvent *m) {
    if (drag_fader || drag_oled) {
        double px, py;
        to_pixels(m->windowID, m->x, m->y, &px, &py);
        if (drag_fader) {
            fader_to(py);
        } else {
            oled_mouse(px, py, "drag", 0);
        }
        return;
    }
    if (drag_enc != 0) {
        // up and right turn clockwise
        double px0, py0, px1, py1;
        to_pixels(m->windowID, drag_x, drag_y, &px0, &py0);
        to_pixels(m->windowID, m->x, m->y, &px1, &py1);
        drag_x = m->x;
        drag_y = m->y;
        drag_acc += ((px1 - px0) - (py1 - py0)) / (geom.s > 0 ? geom.s : 1);
        double step = drag_step();
        int ticks = (int)(drag_acc / step);
        if (ticks != 0) {
            drag_acc -= ticks * step;
            post_enc(drag_enc, ticks);
        }
        return;
    }
    if (held_key != 0 || skin.button_down != SKIN_NONE) {
        return;
    }
    double px, py;
    to_pixels(m->windowID, m->x, m->y, &px, &py);
    set_hover(skin_hit(&geom, px, py));
}

static void handle_wheel(const SDL_MouseWheelEvent *w) {
    if (w->y == 0) {
        return;
    }
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    double px, py;
    to_pixels(w->windowID, mx, my, &px, &py);
    int c = skin_hit(&geom, px, py);
    int delta = w->direction == SDL_MOUSEWHEEL_FLIPPED ? -w->y : w->y;
    int ox, oy;
    // over the fader: the output level a step; over the oled on the MIX page: the level there
    if (c == SKIN_FADER || c == SKIN_MUTE) {
        call_lua("output_step", {std::to_string(delta)});
        return;
    }
    if (c == SKIN_NONE && mouse_on_mix() && skin_oled_pixel(&geom, px, py, &ox, &oy)) {
        oled_mouse(px, py, "wheel", delta);
        return;
    }
    int n = c >= SKIN_E1 && c <= SKIN_E3 ? c - SKIN_E1 + 1 : skin_nearest_enc(&geom, px, py);
    post_enc(n, delta);
}

static window_keys_t keys;

static void handle_key(const SDL_KeyboardEvent *k) {
    if (is_pane(&keys_pane, k->windowID)) {
        keys_typed(k);
        return;
    }
    window_key_event_t out;
    int option = (k->keysym.mod & KMOD_ALT) != 0;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    // SDL scancodes are USB HID keyboard usages
    if (!window_keys_process(&keys, (int)k->keysym.scancode, k->type == SDL_KEYDOWN, k->repeat, option, shift, &out)) {
        return;
    }
    switch (out.kind) {
    case WK_KEY:
        post_key(out.n, out.value);
        break;
    case WK_ENC:
        post_enc(out.n, out.value);
        break;
    case WK_HID:
        macos_hid_keyboard_event((uint16_t)out.n, out.value);
        break;
    case WK_MODE: {
        SDL_Window *window = SDL_GetWindowFromID(k->windowID);
        if (window != NULL) {
            SDL_SetWindowTitle(window, out.value ? MACOS_PRODUCT " · keys" : MACOS_PRODUCT);
        }
        break;
    }
    }
}

// a key/enc event reached norns: move the control on screen
static void handle_show(int kind, int n, int value) {
    if (kind == SDL_BRIDGE_SHOW_MIC) {
        mac_menu_set_input(n);
        if (skin.input != n) {
            skin.input = n;
            redraw(true);
        }
        return;
    }
    if (kind == SDL_BRIDGE_SHOW_OUT) {
        // while the mouse holds the fader it is where the mouse is
        if (!drag_fader) {
            skin.out_level = n / 1000.0;
            skin.out_muted = value != 0;
            redraw(true);
        }
        return;
    }
    if (kind == SDL_BRIDGE_SHOW_PAGE) {
        if (n >= 0 && n <= 4 && n != skin.page) {
            skin.page = n;
            redraw(true);
        }
        return;
    }
    if (n < 1 || n > 3) {
        return;
    }
    if (kind == SDL_BRIDGE_SHOW_KEY) {
        skin.key_down[n - 1] = value != 0;
    } else {
        skin.enc_ticks[n - 1] += value;
    }
    redraw(true);
}

static void toggle_window(int which) {
    if (which == SDL_BRIDGE_ARC) {
        toggle_arc();
    } else if (which == SDL_BRIDGE_KEYS) {
        toggle_keys();
    } else {
        toggle_grid();
    }
}

void sdl_bridge_init(void) {
    main_thread = pthread_self();
    window_keys_init(&keys);
    // until lua says otherwise (lua/core/menu.lua, MIC)
    skin.input = SKIN_INPUT_MIC;
    // keep default SIGINT/SIGTERM behaviour (terminate), as on linux; SDL would otherwise
    // swallow them as SDL_QUIT
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    // the first click into the window already presses a key
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    // the menu SDL makes is named after the process
    mac_set_app_name(MACOS_PRODUCT);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "sdl_bridge: SDL_Init failed: %s\n", SDL_GetError());
    }
    // right button on a trackpad: ctrl-click
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
    bridge_event_type = SDL_RegisterEvents(4);
    show_event_type = bridge_event_type + 1;
    device_event_type = bridge_event_type + 2;
    toast_event_type = bridge_event_type + 3;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    mac_menu_install(menu_item, import_path, note_toast, input_chosen);
    mac_update_install();
}

void sdl_bridge_show_control(int kind, int n, int value) {
    if (show_event_type == (Uint32)-1) {
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = show_event_type;
    ev.user.code = kind;
    ev.user.data1 = (void *)(intptr_t)n;
    ev.user.data2 = (void *)(intptr_t)value;
    SDL_PushEvent(&ev);
}

//--- what norns' input hears besides the Mac's own (lua/core/menu.lua decides)

static unsigned input_gen = 0;

static Uint32 input_check_due(Uint32 interval, void *gen) {
    (void)interval;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = device_event_type;
    ev.user.code = DEV_INPUT_CHECK;
    ev.user.data1 = gen;
    SDL_PushEvent(&ev);
    return 0;
}

// main thread; frees both
static void set_app_input(char *target, char *label) {
    const char *t = target != NULL && target[0] != '\0' ? target : NULL;
    const char *cur = app_audio_current();
    // the same again (lua at startup, the button's own state): nothing to restart
    bool same = (t == NULL && cur == NULL) || (t != NULL && cur != NULL && strcmp(t, cur) == 0);
    if (!same && !app_audio_select(t) && t != NULL) {
        show_toast("app input does not start (see norns.log)");
        call_lua("input_ended", {});
    } else if (label != NULL && label[0] != '\0') {
        show_toast(std::string("input: ") + label);
    }
    if (t != NULL && !same) {
        // the helper ends at once when it cannot tap here
        SDL_AddTimer(1200, input_check_due, (void *)(uintptr_t)++input_gen);
    }
    free(target);
    free(label);
}

static void check_app_input(unsigned gen) {
    if (gen == input_gen && app_audio_current() == NULL) {
        show_toast("app input stopped (see norns.log)");
        call_lua("input_ended", {});
    }
}

void sdl_bridge_app_input(const char *target, const char *label) {
    if (device_event_type == (Uint32)-1) {
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = device_event_type;
    ev.user.code = DEV_APP_INPUT;
    ev.user.data1 = strdup(target != NULL ? target : "");
    ev.user.data2 = strdup(label != NULL ? label : "");
    SDL_PushEvent(&ev);
}

void sdl_bridge_choose_file(const char *dir) {
    if (device_event_type == (Uint32)-1) {
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = device_event_type;
    ev.user.code = DEV_CHOOSE;
    ev.user.data1 = strdup(dir != NULL ? dir : "");
    SDL_PushEvent(&ev);
}

void sdl_bridge_choose_file_close(void) {
    if (device_event_type == (Uint32)-1) {
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = device_event_type;
    ev.user.code = DEV_CHOOSE_CLOSE;
    SDL_PushEvent(&ev);
}

void sdl_bridge_toggle_window(int which) {
    if (device_event_type == (Uint32)-1) {
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = device_event_type;
    ev.user.code = DEV_TOGGLE;
    ev.user.data1 = (void *)(intptr_t)which;
    SDL_PushEvent(&ev);
}

void sdl_bridge_run(void) {
    SDL_Event ev;
    for (;;) {
        if (!SDL_WaitEvent(&ev)) {
            continue;
        }
        if (ev.type == bridge_event_type) {
            bridge_req_t *req = (bridge_req_t *)ev.user.data1;
            bridge_do(req);
            dispatch_semaphore_signal(req->done);
            continue;
        }
        if (ev.type == show_event_type) {
            handle_show(ev.user.code, (int)(intptr_t)ev.user.data1, (int)(intptr_t)ev.user.data2);
            continue;
        }
        if (ev.type == device_event_type) {
            if (ev.user.code == DEV_TOGGLE) {
                toggle_window((int)(intptr_t)ev.user.data1);
            } else if (ev.user.code == DEV_APP_INPUT) {
                set_app_input((char *)ev.user.data1, (char *)ev.user.data2);
            } else if (ev.user.code == DEV_INPUT_CHECK) {
                check_app_input((unsigned)(uintptr_t)ev.user.data1);
            } else if (ev.user.code == DEV_CHOOSE) {
                char *dir = (char *)ev.user.data1;
                mac_menu_choose_file(dir, dust_dir().c_str(), import_chosen);
                free(dir);
            } else if (ev.user.code == DEV_CHOOSE_CLOSE) {
                mac_menu_choose_file_close();
            } else if (ev.user.code == DEV_ARC_LEDS) {
                arc_redraw();
            } else {
                grid_redraw();
            }
            continue;
        }
        if (ev.type == toast_event_type) {
            if (ev.user.code == 1) {
                toast_expired((unsigned)(uintptr_t)ev.user.data1);
            } else {
                char *text = (char *)ev.user.data1;
                show_toast(text);
                free(text);
            }
            continue;
        }
        switch (ev.type) {
        case SDL_QUIT:
            // window closed: let matron leave its event loop; the norns thread cleans up
            // and exits the process
            event_post(event_data_new(EVENT_QUIT));
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            handle_key(&ev.key);
            break;
        case SDL_DROPFILE:
            if (ev.drop.file != NULL) {
                // option held while dropping: a copy, as in the Finder
                import_path(ev.drop.file, mac_option_held() ? MAC_IMPORT_COPY : MAC_IMPORT_LINK);
                SDL_free(ev.drop.file);
            }
            break;
        case SDL_MOUSEWHEEL:
            if (is_pane(&arc_pane, ev.wheel.windowID)) {
                arc_wheel(&ev.wheel);
            } else if (!is_pane(&grid_pane, ev.wheel.windowID) && !is_pane(&keys_pane, ev.wheel.windowID)) {
                handle_wheel(&ev.wheel);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (is_pane(&grid_pane, ev.button.windowID)) {
                grid_button(&ev.button);
            } else if (is_pane(&arc_pane, ev.button.windowID)) {
                arc_button(&ev.button);
            } else if (is_pane(&keys_pane, ev.button.windowID)) {
                keys_button(&ev.button);
            } else {
                handle_button(&ev.button);
            }
            break;
        case SDL_MOUSEMOTION:
            if (is_pane(&grid_pane, ev.motion.windowID)) {
                grid_motion(&ev.motion);
            } else if (is_pane(&arc_pane, ev.motion.windowID)) {
                arc_motion(&ev.motion);
            } else if (is_pane(&keys_pane, ev.motion.windowID)) {
                keys_motion(&ev.motion);
            } else {
                handle_motion(&ev.motion);
            }
            break;
        case SDL_WINDOWEVENT: {
            Uint32 id = ev.window.windowID;
            Uint8 e = ev.window.event;
            bool repaint = e == SDL_WINDOWEVENT_SIZE_CHANGED || e == SDL_WINDOWEVENT_EXPOSED;
            if (is_pane(&grid_pane, id)) {
                if (e == SDL_WINDOWEVENT_CLOSE) {
                    toggle_grid();
                } else if (repaint) {
                    grid_redraw();
                }
                break;
            }
            if (is_pane(&arc_pane, id)) {
                if (e == SDL_WINDOWEVENT_CLOSE) {
                    toggle_arc();
                } else if (repaint) {
                    arc_redraw();
                }
                break;
            }
            if (is_pane(&keys_pane, id)) {
                if (e == SDL_WINDOWEVENT_CLOSE) {
                    toggle_keys();
                } else if (e == SDL_WINDOWEVENT_FOCUS_LOST) {
                    // typed notes would hang: their key-up goes to another window
                    keys_release_all(VKEYS_HELD_TYPED);
                    keys_redraw();
                } else if (e == SDL_WINDOWEVENT_LEAVE && vk.hover != VK_NONE) {
                    vk.hover = VK_NONE;
                    keys_redraw();
                } else if (repaint) {
                    keys_redraw();
                }
                break;
            }
            if (e == SDL_WINDOWEVENT_CLOSE) {
                // with a device window open, closing the norns window is no SDL_QUIT
                event_post(event_data_new(EVENT_QUIT));
            } else if (repaint) {
                redraw(true);
            } else if (e == SDL_WINDOWEVENT_LEAVE && held_key == 0 && drag_enc == 0) {
                set_hover(SKIN_NONE);
            }
            break;
        }
        default:
            break;
        }
    }
}

SDL_Window *sdl_bridge_create_window(const char *title, int w, int h) {
    bridge_req_t req = {};
    req.op = BRIDGE_CREATE_WINDOW;
    req.title = title;
    req.w = w;
    req.h = h;
    bridge_call(&req);
    return req.window;
}

void sdl_bridge_present(SDL_Window *window, SDL_Surface *src) {
    bridge_req_t req = {};
    req.op = BRIDGE_PRESENT;
    req.window = window;
    req.src = src;
    bridge_call(&req);
}

void sdl_bridge_destroy_window(SDL_Window *window) {
    bridge_req_t req = {};
    req.op = BRIDGE_DESTROY_WINDOW;
    req.window = window;
    bridge_call(&req);
}
