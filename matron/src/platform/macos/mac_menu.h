/*
 * mac_menu.h
 *
 * the macOS menu bar of the norns window (SDL only makes the app and window menus):
 * File for getting samples and scripts in and recordings out, Devices for the virtual
 * grid, arc and keyboard windows and what norns' input hears. every item calls back with
 * its MAC_MENU_ id on the main thread.
 *
 * also what the norns windows take when something is dropped on them beyond SDL's Finder
 * files: files an app promises (written for the drop: Voice Memos, Music, DAWs, sample
 * browsers) and audio data.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MAC_MENU_IMPORT = 1, // the open panel; the chosen paths come to the import callback
    MAC_MENU_DUST,       // dust in Finder
    MAC_MENU_TAPE,       // dust/audio/tape in Finder
    MAC_MENU_SCREENSHOT, // the screen as png
    MAC_MENU_GIF,        // start / stop a gif recording of the screen
    MAC_MENU_GRID,
    MAC_MENU_ARC,
    MAC_MENU_KEYS,
    MAC_MENU_SKIN_DEVICE, // View: the windows drawn as the hardware
    MAC_MENU_SKIN_FLAT,   // View: dark flat
};

// the look checked in View (SKIN_STYLE_*)
enum {
    MAC_SKIN_DEVICE = 0,
    MAC_SKIN_FLAT = 1,
};

// how the import callback takes a path
enum {
    MAC_IMPORT_LINK = 0, // audio linked from dust/audio/mac (scripts are always copied)
    MAC_IMPORT_COPY = 1, // copied into dust
    MAC_IMPORT_TAKE = 2, // copied into dust, then the path is removed: a file an app wrote
                         // for the drop into a temporary folder of ours
};

// what the MIC button says: the input checked in Devices > Audio Input (SKIN_INPUT_*)
enum {
    MAC_INPUT_OFF = 0,
    MAC_INPUT_MIC = 1,
    MAC_INPUT_SYSTEM = 2,
    MAC_INPUT_APP = 3,
};

// main thread, after SDL_Init(SDL_INIT_VIDEO). on_note shows a short message on the
// device (any thread). on_input: Devices > Audio Input chose mode "mic", "system", "off" or
// "app" (with the app's bundle id and name, else NULL); lua decides, and says so through
// mac_menu_set_input
extern void mac_menu_install(void (*on_item)(int id), void (*on_import)(const char *path, int mode),
                             void (*on_note)(const char *text),
                             void (*on_input)(const char *mode, const char *bundle, const char *name));

// what the input hears now (MAC_INPUT_*), for the checkmarks
extern void mac_menu_set_input(int input);

// the app's name in the menu bar and its menu (About, Hide, Quit ...): the binary is norns, the
// app is not. before SDL_Init
extern void mac_set_app_name(const char *name);

// the look (MAC_SKIN_*) chosen last time, device if never; before mac_menu_install
extern int mac_menu_saved_skin(void);

// check this look in View and keep it for the next start
extern void mac_menu_set_skin(int skin);

// the GIF item says "Stop" while recording
extern void mac_menu_set_recording(int on);

// the import panel (MAC_MENU_IMPORT calls this itself); does not block
extern void mac_menu_choose_import(void);

// the option key is down right now (whichever app is in front: also during a drag from the
// Finder, when SDL has not seen the key)
extern int mac_option_held(void);

// SDL windows made since the last call also take promised files and audio data; main
// thread, after each SDL_CreateWindow
extern void mac_drop_adopt_windows(void);

// a script asks for a file (lua's fileselect): the Finder's open panel, not modal, starting
// in dir (dust itself, or NULL: wherever the panel was last). done gets the chosen path,
// nothing on cancel. again while it is open: it goes to dir. main thread
extern void mac_menu_choose_file(const char *dir, const char *dust, void (*done)(const char *path));

// the script got its file another way (norns' own browser): the panel goes away
extern void mac_menu_choose_file_close(void);

#ifdef __cplusplus
}
#endif
