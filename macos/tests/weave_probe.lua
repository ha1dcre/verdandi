-- live scenarios for Weave in a running norns (sound muted), fed by macos/tests/weave_probe.sh
audio.level_dac(0)
#sleep 5
print('LOAD', engine.load('Weave', function() print('WEAVE_LOADED') end))
#sleep 8
function V(tag) local p = poll.set('voices', function(v) print('VOICES '..tag..' '..math.floor(v)) end) p:update() end
V('start')
#sleep 0.5
for i = 1, 10 do engine.note_on(i, 200 + i * 30, 0.8) end
#sleep 0.5
V('ten_held')
#sleep 0.5
engine.all_off(0)
#sleep 1.5
V('after_all_off')
#sleep 0.5
engine.set('voice.mode', 1)
engine.note_on(1, 220, 0.8); engine.note_on(2, 330, 0.8); engine.note_on(3, 440, 0.8)
#sleep 0.5
V('mono_three_held')
#sleep 0.3
engine.note_off(3); engine.note_off(2)
#sleep 0.3
V('mono_one_left')
#sleep 0.3
engine.note_off(1)
#sleep 1.5
V('mono_released')
#sleep 0.3
engine.set('voice.mode', 0)
engine.patch("m osc1 osc\nm osc2 osc\nc osc1.out osc2.fm\nc osc2.out osc1.fm\n")
#sleep 1
engine.patch(__SAMPLER__)
#sleep 2
engine.load_sample(1, '__WAV__')
#sleep 1
engine.note_on(60, 261.63, 1)
#sleep 0.4
V('sampler_one')
#sleep 0.3
engine.note_off(60)
engine.note_on(61, 277, 1); engine.note_on(62, 293, 1)
engine.kill(0)
#sleep 0.5
V('killed')
#sleep 0.5
for i = 1, 5 do engine.note_on(100 + i, 300, 1); engine.note_off(100 + i) end
#sleep 1.5
V('instant_notes_ended')
#sleep 0.5
print('PROBE_END')
