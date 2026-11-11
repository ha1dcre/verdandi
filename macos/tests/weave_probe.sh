#!/bin/bash
# weave_probe.sh: Engine_Weave in a running norns (macos/tests/norns_probe.sh, the sound muted):
# voices stolen at the voice limit, released, mono and legato, a patch with a loop refused, a
# sample loaded and played, killed voices and notes that end at once really freed.
# needs node, python3 and the built norns; nothing else may run norns meanwhile.
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NHOME="$(dirname "$REPO")"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
python3 -I - "$T/tone.wav" <<'PY'
import wave, struct, math, sys
w = wave.open(sys.argv[1], 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
w.writeframes(b''.join(struct.pack('<h', int(12000 * math.sin(2 * math.pi * 261.63 * i / 48000))) for i in range(48000))); w.close()
PY
SAMPLER=$(node -e "const W=require('$REPO/macos/blocks/weave.js'),c=require('$REPO/macos/blocks/weave.json');process.stdout.write(JSON.stringify(W.text(require('$REPO/macos/blocks/weave_presets.json').find(p=>p.name==='sampler').patch,c)))")
python3 -I - "$REPO/macos/tests/weave_probe.lua" "$T/probe.lua" "$T/tone.wav" "$SAMPLER" <<'PY'
import sys
src, dst, wav, sampler = sys.argv[1:5]
open(dst, 'w').write(open(src).read().replace('__WAV__', wav).replace('__SAMPLER__', sampler))
PY
: > "$NHOME/sclang.log"
"$REPO/macos/tests/norns_probe.sh" "$T/probe.lua" "$T/probe.log" > /dev/null
want="start 0
ten_held 8
after_all_off 0
mono_three_held 1
mono_one_left 1
mono_released 0
sampler_one 1
killed 0
instant_notes_ended 0"
got=$(grep '^VOICES ' "$T/probe.log" | sed 's/^VOICES //')
fail=0
if [ "$got" = "$want" ]; then echo "ok   voices: $(echo "$got" | tr '\n' ',' | sed 's/,$//')"; else echo "FAIL voices"; diff <(echo "$want") <(echo "$got") || true; fail=1; fi
grep -q "weave: the patch was not built: .*loop" "$NHOME/sclang.log" && echo "ok   a loop is refused" || { echo "FAIL the loop was not refused"; fail=1; }
grep -q "weave: sample 1 loaded" "$NHOME/sclang.log" && echo "ok   sample loaded" || { echo "FAIL sample not loaded"; fail=1; }
other=$(grep -E "ERROR|FAILURE|exception" "$NHOME/sclang.log" | grep -v "go round in a loop" || true)
[ -z "$other" ] && echo "ok   no other errors in sclang.log" || { echo "FAIL sclang.log:"; echo "$other" | head -5; fail=1; }
exit $fail
