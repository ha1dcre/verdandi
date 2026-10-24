# Verdandi: norns on macOS

Verdandi is a community port of norns to macOS (Apple Silicon, macOS 15 or newer:
`macos/MACOS_MIN`); it runs norns scripts on a Mac. not affiliated with monome: norns is
theirs, so the app has a name of its own (Verdandi, the norn of the present; product name in
`matron/src/platform/macos/product.h`, `macos/make-app.sh`, `macos/auv3/build-appex.sh`).
scripts from the norns catalog are meant to run unchanged: all compatibility lives here.

users: get `Verdandi.dmg` and read the manual (`macos/manual/manual.html`, also in the dmg).
this file is for building and changing the port.

## layout

the norns home is a folder with the same layout as `/home/we` on the device; norns runs
with `HOME` set to it, so the lua core, sclang and scripts find everything where they expect.

```
<norns home>/
  norns/                 this repository
  dust/code, audio, data scripts and their data
  matronrc.lua           written on first start (desktop screen)
  Library/Application Support/SuperCollider/   sclang config, sc3-plugins
```

## build

```
macos/setup-deps.sh                      # homebrew packages + lua 5.3, nng 1.x, libmonome,
                                         # SuperCollider (jack backend), sc3-plugins, waf 2.1, jack
macos/build.sh                           # waf configure (first time) + build; darwin is detected
```

both run the build tools in a neutral environment (`env -i`, home in `deps/.home`, an
anonymous git identity): waf, cmake and git otherwise write the builder's home, user name,
PATH and git identity into caches, logs and reflogs inside the folder. build from a path
without the user name in it, e.g. a link `/Users/Shared/norns-dev` to the folder (a path
with a space breaks lua's makefile).

## app, dmg, AUv3

```
macos/make-app.sh --dmg      # Verdandi.dmg (Verdandi.app + AUv3, the manual) on top of the folder with deps/
```

the app bundles everything (jackd, SuperCollider, sc3-plugins, GNU tools, maiden, all
dylibs rewritten to @rpath, see `macos/bundle_dylibs.py`) and is ad-hoc signed. its
launcher (`macos/app-launcher.sh`) uses `~/norns` as the norns home.

the version is `CFBundleShortVersionString` in `macos/make-app.sh` (and `macos/auv3/build-appex.sh`).
the app compares it with the latest GitHub release (`matron/src/platform/macos/mac_update.m`, at
start and once a day; the launcher passes the app's path as `VERDANDI_APP`), so a release's tag
must be that version (`v0.1.3`) and must not be a pre-release: `/releases/latest` skips those.

the AUv3 (`macos/auv3/`, "Verdandi", type `aumf`, subtype `vrdn`, manufacturer `Vrdn`) is a sandboxed
extension inside the app. it sends the DAW's audio and MIDI over UDP on 127.0.0.1 to
`auv3_bridge`, a jack client next to norns that resamples between the two clocks (jack's
adapter) and takes over crone's main in/out while the plugin renders. check it with
`auval -v aumf vrdn Vrdn`.

## run

```
macos/norns.sh               # maiden: http://127.0.0.1:5000/maiden
macos/norns.sh --terminal    # matron's lua REPL and log in the terminal instead
```

starts jackd (coreaudio), norns (matron + crone + softcut), sclang and maiden. as on the
device, maiden is the editor, the REPLs (matron :5555, sclang :5556) and the script
catalog (install / update). unlike the device everything listens on 127.0.0.1 only:
the REPLs run arbitrary code, and a laptop is on networks a norns is not.

the window is the norns screen (resizable). controls:

| keys (window focused) | norns                                  |
|----------------------|----------------------------------------|
| 1 2 3                | K1 K2 K3 (held while held)             |
| Q / W                | E1 left / right                        |
| A / S                | E2 left / right (in scripts also ↑ / ↓) |
| Z / X                | E3 left / right (in scripts also ← / →) |
| ⇧ added              | 5 steps; holding a key keeps turning   |

this works wherever nothing else wants the keys: in the menu (arrows keep their menu
bindings, F1-F5 the menu pages) and in every script that does not use the keyboard itself.
text entry types as usual, and scripts with their own `keyboard.code` / `keyboard.char`
(14 of the 351 catalog scripts) get the keyboard; there:

| keys               | norns                                         |
|--------------------|-----------------------------------------------|
| ⌥ + the keys above | always K / E                                  |
| ⌥Esc               | force hardware keys on/off (title "norns · keys") |

the automatic part is `lua/core/keyboard.lua` (enabled by `NORNS_HARDWARE_KEYS=1`, which
`macos/norns.sh` sets; off on the device), tests: `NORNS_HARDWARE_KEYS=1 <lua 5.3>
macos/tests/keyboard_hardware_test.lua`.

mouse: the window draws the device (`matron/src/platform/macos/device_skin.c`, layout of
the standard norns). click a key; drag a knob (up/right = clockwise, shift fine, command
fast) or scroll over it; the wheel elsewhere turns the nearest knob. keys and knobs show
every key/enc event norns receives (`sdl_bridge_show_control`, also called by the keyboard
path in lua).

buttons above the device (not on the hardware): the menu pages (`EVENT_WINDOW_BUTTON` ->
`_norns.window_button` in `lua/core/menu.lua`), MIC (unplugs system:capture from
crone:input_1/2 through jack_disconnect, kept in `~/norns/.mic-off`), GRID, ARC, KEYS,
MAIDEN, MANUAL (`macos/manual/manual.html`; opened by `open_url.c`, http(s) and paths only).
opening a device window lays all windows out to fit the screen (`arrange_windows`).

virtual grid: `vgrid_proto.c` is the grid side of the mext serial protocol, `vgrid_device.c`
puts it on a pty that libmonome opens like a usb grid (`dev_list_add`), `vgrid_skin.c`
draws the window. GRID or F6 opens it; 64, 128 or 256 (switch under it, kept in
`~/norns/.vgrid-size`). the virtual arc is the same pty device answering as 4 encoders
(libmonome then reports 0x0 keys, which matron takes for an arc; its name starts "monome arc"
because `lua/core/startup.lua` routes by name), drawn by `varc_skin.c`; ARC or F8.

virtual keyboard: KEYS or F7. `vkeys_skin.c` (layout, hit test, computer keys by HID usage)
and a CoreMIDI source "norns keys" made in `sdl_bridge.cc`; the device monitor picks it up
like any app's port.

menu bar (`mac_menu.m`): import (open panel), show dust / tape folder, screenshot (png, 4x)
and gif recording (`gif_writer.c`, 16 grey levels) to the desktop. files dropped on a window
go into dust by `dust_import.c` on a worker thread: audio is symlinked from
`dust/audio/mac` (option held, or the panel's box: copied to dust/audio; dead links are
pruned on the next import), scripts are copied to dust/code; nothing is overwritten.
what other apps drag (file promises, audio data on the pasteboard) is taken by drop methods
`mac_menu.m` puts on SDL's window class: written into a temporary folder, copied into
dust/audio, removed again; files in temporary folders and caches are copied, not linked.

input: the MIC button goes MIC > SYS > OFF (lua/core/menu.lua `_menu.set_input`; APP: one app
from Devices > Audio Input). MIC plugs system:capture into crone:input_1/2; SYS and APP run
`macos/app_input.m` through `_norns.app_input` (`app_audio.m`), a jack client that taps every
app but norns' own processes (`--system`) or the chosen one (Core Audio process tap, the apps
not muted) through a private aggregate device and plays it into crone:input_1/2. meanwhile
`_norns.monitor_out(false)` sends crone's input monitor to the tape only (the apps are heard
directly). it is spawned with nothing of norns' open files (POSIX_SPAWN_CLOEXEC_DEFAULT: with
crone's jack sockets it kept the old crone in jackd after SYSTEM > RESTART) and ends with norns
(parent watch), with jack or on SIGTERM.

OUT fader and MUTE (right of E3, `device_skin.c`): `_norns.output_fader` / `output_step` /
`output_mute` (lua/core/menu.lua) set output_level, whose action shows it back
(`SDL_BRIDGE_SHOW_OUT`). the mouse on the oled on the MIX page goes to lua as
`_norns.screen_mouse(x, y, kind, value)`, oled pixels, for the page's `mouse` handler.

a script's file: lua's fileselect also opens the Finder's open panel (`_norns.choose_file`,
`mac_menu.m`); the choice, or a sample dropped on the window while the browser is up, comes
back as `_norns.file_chosen` / `file_dropped` (audio from outside dust linked in first).

audio: SYSTEM > AUDIO (`lua/core/menu/audio.lua`, devices from `macos/audio_devices.c`)
writes `~/norns/audio.conf`; `macos/norns.sh` restarts jackd with it and falls back to the
defaults (leaving `audio.conf.failed`) if it does not start.

the key mapping is plain C (`matron/src/platform/macos/window_keys.c`) with scenario tests:
`cc -std=c11 -I matron/src/platform/macos macos/tests/window_keys_test.c matron/src/platform/macos/window_keys.c -o /tmp/wkt && /tmp/wkt`

## tests

```
cc -std=c11 -I matron/src/platform/macos macos/tests/window_keys_test.c \
   matron/src/platform/macos/window_keys.c -o /tmp/wkt && /tmp/wkt
cc -std=c11 -I matron/src/platform/macos $(pkg-config --cflags --libs cairo) \
   macos/tests/device_skin_test.c matron/src/platform/macos/device_skin.c \
   matron/src/platform/macos/vgrid_skin.c matron/src/platform/macos/varc_skin.c \
   matron/src/platform/macos/vkeys_skin.c -o /tmp/dst && /tmp/dst [png_dir]
P=<deps>/prefix; cc -std=c11 -I matron/src/platform/macos -I $P/include macos/tests/vgrid_test.c \
   matron/src/platform/macos/vgrid_proto.c matron/src/platform/macos/vgrid_device.c \
   -L $P/lib -lmonome -o /tmp/vgt && DYLD_LIBRARY_PATH=$P/lib /tmp/vgt
cc -std=c11 -I matron/src/platform/macos macos/tests/gif_test.c matron/src/platform/macos/gif_writer.c \
   -framework ImageIO -framework CoreGraphics -framework CoreFoundation -o /tmp/gift && /tmp/gift
cc -std=c11 -I matron/src/platform/macos macos/tests/dust_import_test.c \
   matron/src/platform/macos/dust_import.c -o /tmp/dit && /tmp/dit [dir on another disk]
P=matron/src/platform/macos; cc -c $P/mac_menu.m $P/app_audio.m -I $P && \
cc -fobjc-arc macos/tests/drop_test.m mac_menu.o app_audio.o -I $P $(pkg-config --cflags --libs sdl2) \
   -framework AppKit -framework CoreAudio -framework UniformTypeIdentifiers -o /tmp/dropt && /tmp/dropt [--live]
cc -fobjc-arc macos/tests/app_input_test.m $(pkg-config --cflags --libs jack) -framework AudioToolbox \
   -framework Foundation -o /tmp/ait && /tmp/ait build/macos/app_input   # own jack server, tone app
cc -c $P/mac_update.m $P/open_url.c -I $P && cc -fobjc-arc macos/tests/update_test.m mac_update.o \
   open_url.o -I $P -framework AppKit -o /tmp/upt && /tmp/upt [--github]   # window off the screen
NORNS_HARDWARE_KEYS=1 <lua 5.3> macos/tests/keyboard_hardware_test.lua
<lua 5.3> macos/tests/menu_pages_test.lua
<lua 5.3> macos/tests/input_modes_test.lua
<lua 5.3> macos/tests/fileselect_finder_test.lua
<lua 5.3> macos/tests/output_fader_test.lua
d=$(mktemp -d); HOME=$d <lua 5.3> macos/tests/virtual_home_test.lua lua/core/virtual_home.lua
macos/tests/norns_probe.sh <commands.lua>   # boot norns, drive its REPL, stop it
```

what only a person with the hardware can test: `macos/docs/test-checklist.md`.

licenses: `macos/setup-deps.sh` runs `macos/licenses.py fetch` (the license texts and exact
sources of what is built here, maiden's Go modules and web packages, into `deps/licenses`);
`macos/make-app.sh` runs `licenses.py collect`, which puts `Licenses/` (texts per package,
`PACKAGES.txt` with version, license, source and checksum or commit) into the app and the dmg
for exactly the packages the app contains, and stops if a bundled library belongs to none.

oldest macOS: `macos/MACOS_MIN`. waf builds norns for it, `setup-deps.sh` builds lua, nng,
libmonome and SuperCollider for it, and `fetch_bottles.py` fetches homebrew's bottles of the
bundled packages built for it (homebrew installs the ones for the running macOS, which need
that macOS); `make-app.sh` bundles those and stops if any file needs a newer macOS. 15:
homebrew no longer builds bottles for 14 (cairo, SDL3, readline ... would have to be built
from source).

`macos/docs/` has the notes behind the port (linux inventory, catalog scan).

## what is different from the device

see `matron/src/platform/macos/` and `macos/`:

- devices: CoreMIDI (midi, plus a virtual "norns" port), IOKit (grid, arc, crow, serial),
  IOHIDManager (gamepads, joysticks), the window keyboard as hid keyboard; all translated
  to the linux device model and evdev codes the lua libraries expect
- the cocoa main thread owns the window; matron runs on its own thread
- no pi hardware: oled, gpio keys/encoders, battery, i2c are stubbed; stat via mach
- GNU coreutils/findutils/sed/grep are first on norns' PATH, as scripts expect GNU tools
- lua is built as C (C modules from scripts need the C symbols)

## Intel Macs

The same scripts build on an Intel Mac (x86_64): Homebrew's prefix (`/usr/local`), the
Homebrew bottle tags (`sequoia` instead of `arm64_sequoia`) and maiden's Node download follow
`uname -m`. Set `HOMEBREW_PREFIX` or `NORNS_ARCH` to override. Nothing is compiled with
`-march=native`, so a build runs on older CPUs too (tested target: Ivy Bridge, mid-2012
MacBook on macOS 15). Build on the Intel Mac itself, natively, not under Rosetta.
