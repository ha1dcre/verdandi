// mix.js: the final video: full.mp4 (record.js --full) with the narration laid in, each paragraph
// 0.4 s after the start of its scene; Verdandi's sound (the tape) evened out and ducked under
// the voice; loudness -16 LUFS.
//   node macos/manual/video/mix.js <out dir> <voice file> <voice-segments.json>
// voice-segments.json: {"segments": [[start, end] ...] in the voice file, "scenes": [scene names]}
'use strict';
const fs = require('fs');
const path = require('path');
const {execFileSync} = require('child_process');
const [out, voice, segFile] = process.argv.slice(2).map(p => path.resolve(p));
const seg = JSON.parse(fs.readFileSync(segFile));
const scenes = JSON.parse(fs.readFileSync(path.join(out, 'scenes.json'))).scenes;
const at = name => scenes.find(s => s.name === name).at;
const dur = Number(execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path.join(out, 'full.mp4')]));
let parts = '', labels = '', last = 0;
seg.segments.forEach(([a, b], i) => {
  // in its scene, and never over the paragraph before it
  const start = Math.max(at(seg.scenes[i]) + 0.4, last + 0.3);
  last = start + (b - a);
  if (last > dur) throw new Error('paragraph ' + (i + 1) + ' runs past the end of the video');
  const ms = Math.round(start * 1000);
  parts += '[1:a]atrim=' + a + ':' + b + ',asetpts=PTS-STARTPTS,afade=t=in:d=0.03,afade=t=out:st=' + Math.max(0, b - a - 0.08).toFixed(2) + ':d=0.08,adelay=' + ms + '|' + ms + '[v' + i + '];';
  labels += '[v' + i + ']';
  console.log('paragraph ' + (i + 1) + ' (' + seg.scenes[i] + '): ' + start.toFixed(2) + ' - ' + last.toFixed(2) + ' s');
});
const filter = parts + labels + 'amix=inputs=' + seg.segments.length + ':normalize=0,aformat=channel_layouts=stereo,loudnorm=I=-16:TP=-2:LRA=7,asplit=2[voice][key];' +
  '[0:a]aformat=channel_layouts=stereo,loudnorm=I=-20:TP=-3:LRA=11[bed];' +
  '[bed][key]sidechaincompress=threshold=0.03:ratio=5:attack=30:release=500[ducked];' +
  '[ducked][voice]amix=inputs=2:normalize=0,alimiter=limit=0.9,apad[mix]';
const file = path.join(out, 'script-builder-video.mp4');
execFileSync('ffmpeg', ['-loglevel', 'error', '-y', '-i', path.join(out, 'full.mp4'), '-i', voice, '-filter_complex', filter,
  '-map', '0:v', '-map', '[mix]', '-c:v', 'copy', '-c:a', 'aac', '-b:a', '192k', '-t', dur.toFixed(3), file], {stdio: 'inherit'});
console.log('wrote ' + file);
