/*
 * sdl_bridge.h
 *
 * cocoa only allows windows and the event pump on the main thread, but matron draws from
 * its own thread. on macOS the main thread runs sdl_bridge_run(); matron asks it to create
 * the window and to present frames, and it turns window input into key/enc events.
 */

#pragma once

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// main thread only
extern void sdl_bridge_init(void);
extern void sdl_bridge_run(void); // does not return; window close quits matron

// any thread; blocks until the main thread has done the work
extern SDL_Window *sdl_bridge_create_window(const char *title, int w, int h);
extern void sdl_bridge_present(SDL_Window *window, SDL_Surface *src);
extern void sdl_bridge_destroy_window(SDL_Window *window);

// any thread, does not block: a key (value 0/1) or enc (value = delta) event reached
// norns; the window moves the control to match
#define SDL_BRIDGE_SHOW_KEY 1
#define SDL_BRIDGE_SHOW_ENC 2
#define SDL_BRIDGE_SHOW_PAGE 3 // n = menu page 1-4, 0 = script
#define SDL_BRIDGE_SHOW_MIC 4  // n = what the input hears: SKIN_INPUT_* (0 off, 1 mic, 2 system, 3 app)
#define SDL_BRIDGE_SHOW_OUT 5  // n = the output level's place on its scale x 1000, value = muted
extern void sdl_bridge_show_control(int kind, int n, int value);

// any thread, does not block: open or close a virtual device window
#define SDL_BRIDGE_GRID 1
#define SDL_BRIDGE_ARC 2
#define SDL_BRIDGE_KEYS 3
#define SDL_BRIDGE_CROW 4
extern void sdl_bridge_toggle_window(int which);

// any thread, does not block: what norns' input hears besides the Mac's own input (the MIC
// button, lua/core/menu.lua): "system" (every other app), an app's bundle id, or NULL. label:
// the note on the device ("input: <label>"), NULL for none. lua hears back through
// _norns.input_ended() when it stops on its own
extern void sdl_bridge_app_input(const char *target, const char *label);

// any thread, does not block: the Finder's open panel for a script's file (lua's fileselect),
// starting in dir. the choice comes back as _norns.file_chosen(path), audio from outside dust
// linked into dust/audio/mac first; files dropped on the window come as _norns.file_dropped.
// again while it is open: it goes to dir. close: it goes away
extern void sdl_bridge_choose_file(const char *dir);
extern void sdl_bridge_choose_file_close(void);

#ifdef __cplusplus
}
#endif
