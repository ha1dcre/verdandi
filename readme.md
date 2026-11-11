# Verdandi

**norns on macOS.** Verdandi runs [norns](https://monome.org/docs/norns/), the sound computer by
monome, natively on a Mac: the same scripts, engines, softcut and maiden, in a window that
shows the device. as a standalone app, and as an AUv3 plugin that puts norns into a DAW track.

an unofficial community port, not affiliated with monome. the norns scripts from the
community catalog run unchanged.

**support:** please ask here, in the [GitHub issues](../../issues), not on lines or at
monome: they have nothing to do with this port.

## get it

download `Verdandi.dmg` from the [releases](../../releases), open it and drag Verdandi into
Applications. it is not notarized, so macOS blocks the first start: open Verdandi once, close
the message, then System Settings > Privacy & Security, scroll down to Verdandi and click
**Open Anyway** (macOS 15 no longer offers right-click > Open for this). if macOS says the app
is damaged, run `xattr -dr com.apple.quarantine /Applications/Verdandi.app` in Terminal.

needs a Mac with Apple silicon and macOS 15 (Sequoia) or newer.

## what it does

- the norns device in a window: click the keys, drag or scroll the knobs, or use the keyboard
  (1 2 3 for K1 K2 K3, return / space and esc, Q W A S Z X for the encoders)
- the script catalog in maiden, the norns web editor
- a virtual grid, arc and MIDI keyboard; real grids, arcs, crow and MIDI devices over USB
- MIC / SYS / OFF: record the microphone or interface, or the sound of every other app (a
  video in the browser, a synth) straight into a script, without a loopback driver
- drop samples on the window from the Finder or other apps; a script's file parameter opens
  the Finder
- the MIX page by mouse, an output fader and mute
- the AUv3 plugin: a DAW track goes into norns and norns comes back into the track
- screenshots and GIFs of the screen for sharing
- the script builder (the **blocks** button in maiden): make your own norns scripts without
  writing Lua. blocks say what the script does, the sound page builds its instrument from
  modules and cables (played by Weave, a modular engine that comes with Verdandi), the screen
  page puts moving visuals on the norns screen. a guide teaches it step by step; chapter 8 of
  the manual explains it

the manual is in the dmg (`Manual.pdf`) and in [macos/manual](macos/manual/manual.html).

## status

works on the Mac it was built on. not yet tested by others: other Macs with macOS 15,
audio interfaces, monome grid and arc hardware, crow, the AUv3 in Logic, Ableton Live, Bitwig
and Reaper. the [test checklist](macos/docs/test-checklist.md) says what to try; reports
are welcome as issues.

## build it

[readme-macos.md](readme-macos.md): `macos/setup-deps.sh`, `macos/build.sh`,
`macos/make-app.sh --dmg`.

## licenses

GPL-3.0, as norns. the app contains free software by many people, each part under its own
license; `Licenses/PACKAGES.txt` in the dmg lists every part with its version, license and
source.

## based on norns

norns is by [monome](https://monome.org) and its contributors; their original readme is
[readme-norns.md](readme-norns.md). the name: norns is monome's, so the port has its own.
Verdandi is one of the three norns of the old stories, the one of the present.
