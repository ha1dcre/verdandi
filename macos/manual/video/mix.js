// mix.js: the final video: full.mp4 (record.js --full) with the narration laid in, each paragraph
// 0.4 s after the start of its scene; Verdandi's sound (the tape) evened out and ducked under
// the voice; loudness -16 LUFS. it ends 3.5 s after the last word: the sound fades out, the
// picture fades to black (the take goes on to "stop", which cuts the sound hard).
// voice-placement.json says where each paragraph lies in the video (for captions.js). (the duck's key is padded with silence: it ended with the last
// paragraph and cut the sound after it)
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
const placement = [];
seg.segments.forEach(([a, b], i) => {
  // in its scene, and never over the paragraph before it
  const start = Math.max(at(seg.scenes[i]) + 0.4, last + 0.3);
  last = start + (b - a);
  if (last > dur) throw new Error('paragraph ' + (i + 1) + ' runs past the end of the video');
  placement.push({voice: [a, b], video: start});
  const ms = Math.round(start * 1000);
  parts += '[1:a]atrim=' + a + ':' + b + ',asetpts=PTS-STARTPTS,afade=t=in:d=0.03,afade=t=out:st=' + Math.max(0, b - a - 0.08).toFixed(2) + ':d=0.08,adelay=' + ms + '|' + ms + '[v' + i + '];';
  labels += '[v' + i + ']';
  console.log('paragraph ' + (i + 1) + ' (' + seg.scenes[i] + '): ' + start.toFixed(2) + ' - ' + last.toFixed(2) + ' s');
});
const filter = parts + labels + 'amix=inputs=' + seg.segments.length + ':normalize=0,aformat=channel_layouts=stereo,loudnorm=I=-16:TP=-2:LRA=7,asplit=2[voice][k];[k]apad[key];' +
  '[0:a]aformat=channel_layouts=stereo,loudnorm=I=-20:TP=-3:LRA=11[bed];' +
  '[bed][key]sidechaincompress=threshold=0.03:ratio=5:attack=30:release=500[ducked];' +
  '[ducked][voice]amix=inputs=2:normalize=0,alimiter=limit=0.9,apad[mix]';
const end = Math.min(dur, last + 3.5);
const filterEnd = filter.replace('alimiter=limit=0.9,apad[mix]', 'alimiter=limit=0.9,apad,afade=t=out:st=' + (last + 0.2).toFixed(2) + ':d=' + (end - last - 0.3).toFixed(2) + '[mix]') +
  ';[0:v]fade=t=out:st=' + (end - 1).toFixed(2) + ':d=1[vid]';
const file = path.join(out, 'script-builder-video.mp4');
execFileSync('ffmpeg', ['-loglevel', 'error', '-y', '-i', path.join(out, 'full.mp4'), '-i', voice, '-filter_complex', filterEnd,
  '-map', '[vid]', '-map', '[mix]', '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-b:a', '192k', '-t', end.toFixed(3), file], {stdio: 'inherit'});
fs.writeFileSync(path.join(out, 'voice-placement.json'), JSON.stringify({end, paragraphs: placement}, null, 1));
console.log('ends at ' + end.toFixed(2) + ' s');
console.log('wrote ' + file);
