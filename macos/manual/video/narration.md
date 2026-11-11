# Script builder video: narration

For ElevenLabs: one audio file per scene (`01-open.mp3`, `02-control.mp3` ...), English, calm and friendly,
normal pace. The times come from the recorded take (`scenes.json`); "room" is how long the picture of
that scene runs. A line may run up to a second or two longer: the cut holds the picture a little.
Keep the scene numbers in the file names: the final cut puts each file at the start of its scene.

| # | scene | starts | room | text |
|---|-------|--------|------|------|
| 01 | open | 0:00 | 10 s | This is the script builder in Verdandi. It makes norns scripts without writing any Lua. You open it from maiden, with the blocks button. |
| 02 | control | 0:10 | 14 s | A script is made of event blocks: they say when something happens. When the script starts, we add a control called cutoff, from two hundred to four thousand. |
| 03 | melody | 0:24 | 20 s | Every quarter beat, a counter steps from one to sixteen, and play note plays the melody: note numbers written as text, a dot is a rest. On the right, the preview already runs it, silently. |
| 04 | encoder | 0:44 | 26 s | When encoder two turns, it changes the control. And when the control changes, it sets the filter cutoff of our sound. Everything you can set has a block like this, and a dropdown with every knob of the instrument. |
| 05 | sound | 1:10 | 13 s | This is the sound page: the instrument the notes play. We add an LFO, plug its output into the filter, make it slow, and add a delay. |
| 06 | screen | 1:23 | 22 s | The screen page puts things on the norns screen, and makes them move. Stars, rings on every note, and the name of the note, jumping with each one. The meter follows the cutoff control. |
| 07 | run | 1:45 | 5 s | Give it a name, and press run. |
| 08 | verdandi | 1:50 | 26 s | And there it is, running in Verdandi, with sound. Turning encoder two opens and closes the filter, and the meter moves with it. The script is a real norns script, saved in maiden, ready for any norns. |
| 09 | live | 2:16 | 16 s | While it runs, the sound page plays live: the keyboard, and every knob. Open the guide in the builder to learn it step by step. Have fun. |

Total: about 2:33. Words per line fit at about 2.4 words per second.
