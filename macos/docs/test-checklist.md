# Verdandi (norns on macOS): test checklist

what has not been tested on the machine it was built on. tick what works, and for what
does not, send the logs: `~/norns/norns.log`, `sclang.log`, `jackd.log`, `auv3_bridge.log`
(they contain paths of your Mac, so look them over before you share them).

## a Mac with macOS 15 (Sequoia), not the one it was built on

- [ ] open `Verdandi.dmg`, drag Verdandi into Applications, start it: macOS refuses at first
      (not notarized); System Settings > Privacy & Security > Open Anyway lets it start (macOS 15
      has no right-click > Open for this any more). if it says "damaged":
      `xattr -dr com.apple.quarantine /Applications/Verdandi.app`
- [ ] allow the microphone; Verdandi shows the norns screen, the HOME menu reacts to K2/K3 (2, 3, return, esc)
- [ ] SELECT a script (awake, then something with an engine: passersby, cranes): it plays
- [ ] MIC > SYS: play a video in the browser, allow "record audio from other apps"; the video
      plays on as before, once (not doubled), and MIX: in moves; TAPE: record a few seconds, the
      video is in the recording
- [ ] SYSTEM > RESTART while on SYS: norns comes back (no crash), on SYS
- [ ] quit Verdandi while on SYS, start it again: SYS again, with the note; the video goes into norns
- [ ] Manual.pdf and the Licenses folder open from the dmg

## script builder (maiden > blocks)

- [ ] maiden shows the orange `blocks` button bottom right; it opens the builder with first_steps
- [ ] the screen preview runs (notes under it); K2 stops and starts it, E2 changes cutoff
- [ ] name it, run: the script plays in Verdandi with sound, the console shows matron's output;
      stop ends it; maiden's file list has `code/<name>/<name>.lua`
- [ ] open > pulse: the dial, slider and breathing ring look the same in the preview and in Verdandi
- [ ] save under the name of a script that was not made in the builder (awake): it refuses
- [ ] reload the page: the blocks of last time come back

## audio interface

- [ ] SYSTEM > AUDIO: choose the interface as INPUT and OUTPUT, APPLY: norns restarts its audio
      on it; a rate the interface cannot do falls back to the defaults with a note
- [ ] BUFFER 64 / 128: no dropouts with a script running (cranes, mlr)
- [ ] the interface unplugged and plugged back in while norns runs

## hardware

- [ ] monome grid (USB): shows in SYSTEM > DEVICES > GRID, a grid script (mlr, awake) works
- [ ] monome arc: shows as an arc, rings light, turning and pushing work
- [ ] crow (USB): a crow script (crow-studies) sends and receives
- [ ] a MIDI keyboard / controller: notes and CCs reach a script (passersby), MIDI out to a synth
- [ ] a gamepad, if you have one (SYSTEM > DEVICES > HID)

## the AUv3 plugin in a DAW

- [ ] Logic (Audio FX or MIDI-controlled effect), Ableton Live, Bitwig or Reaper: the plugin
      shows up as "Verdandi" after starting Verdandi once; insert it on a track, press play: "connected"
- [ ] the track's audio goes into norns (MIX: in), norns comes back into the track
- [ ] MIDI on the plugin's track reaches norns (device "Verdandi AUv3")
- [ ] stop the DAW: norns goes back to the interface within a second
- [ ] with MIC on SYS, play the DAW through the plugin: no feedback (SYS pauses); stop it: SYS
      takes the apps again

## the window

- [ ] drop a wav from the Finder (linked into dust/audio/mac), with option (copied), a voice
      memo from Voice Memos, a sample from a sample browser or DAW
- [ ] a script's file parameter (barcode: buffer 1 sample): the Finder panel opens, the chosen
      sample is loaded; dropping a sample on the window while the browser is up does the same
- [ ] the MIX page by mouse (drag, scroll), the OUT fader and MUTE, the arc and grid windows
