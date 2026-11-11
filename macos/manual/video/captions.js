// captions.js: captions for the final video: <take>/script-builder-video.srt, and
// <take>/script-builder-video-captions.mp4 with them burned in (this ffmpeg has no libass:
// each caption is drawn by Chrome as a transparent image and laid over its time).
//   node macos/manual/video/captions.js <take folder> <cues.json>
// times: cues.json has them in the voice file; voice-placement.json (mix.js) says where each
// paragraph of the voice lies in the video.
'use strict';
const fs = require('fs');
const path = require('path');
const {execFileSync} = require('child_process');
const [take, cueFile] = process.argv.slice(2).map(p => path.resolve(p));
const cues = JSON.parse(fs.readFileSync(cueFile)).cues;
const place = JSON.parse(fs.readFileSync(path.join(take, 'voice-placement.json')));
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const W = 1440, H = 860;

// voice time -> video time, paragraph by paragraph; a caption stays until shortly before the next
const timed = cues.map(([p, a, b, text]) => {
  const off = place.paragraphs[p].video - place.paragraphs[p].voice[0];
  return {start: a + off, end: b + off + 0.35, text};
});
timed.forEach((c, i) => { if (timed[i + 1]) c.end = Math.min(c.end, timed[i + 1].start - 0.04); c.end = Math.min(c.end, place.end); });

const stamp = t => {
  const ms = Math.round(t * 1000);
  const h = Math.floor(ms / 3600000), m = Math.floor(ms / 60000) % 60, s = Math.floor(ms / 1000) % 60;
  return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0') + ',' + String(ms % 1000).padStart(3, '0');
};
const srt = timed.map((c, i) => (i + 1) + '\n' + stamp(c.start) + ' --> ' + stamp(c.end) + '\n' + c.text + '\n').join('\n');
fs.writeFileSync(path.join(take, 'script-builder-video.srt'), srt);

// one transparent image per caption
const dir = path.join(take, 'captions');
fs.mkdirSync(dir, {recursive: true});
const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
timed.forEach((c, i) => {
  const html = path.join(dir, 'c' + i + '.html');
  fs.writeFileSync(html, '<!doctype html><html><head><meta charset="utf-8"><style>' +
    'html,body{margin:0;width:' + W + 'px;height:' + H + 'px;background:transparent;overflow:hidden}' +
    'div{position:absolute;left:50%;bottom:14px;transform:translateX(-50%);max-width:1080px;width:max-content;' +
    'padding:8px 20px 10px;border-radius:10px;background:rgba(10,10,10,.78);color:#fff;text-align:center;' +
    'font:600 27px/1.3 -apple-system,BlinkMacSystemFont,"Helvetica Neue",sans-serif;letter-spacing:.005em}' +
    '</style></head><body><div>' + esc(c.text) + '</div></body></html>');
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', '--default-background-color=00000000',
    '--user-data-dir=' + path.join(dir, 'profile'), '--window-size=' + W + ',' + H, '--screenshot=' + path.join(dir, 'c' + i + '.png'),
    'file://' + html], {stdio: 'ignore'});
});

// burned in: every image over the video, only while its caption is on
const inputs = ['-i', path.join(take, 'script-builder-video.mp4')];
let chain = '', prev = '[0:v]';
timed.forEach((c, i) => {
  inputs.push('-i', path.join(dir, 'c' + i + '.png'));
  const outLabel = i === timed.length - 1 ? '[v]' : '[o' + i + ']';
  chain += prev + '[' + (i + 1) + ':v]overlay=0:0:enable=\'between(t,' + c.start.toFixed(3) + ',' + c.end.toFixed(3) + ')\'' + outLabel + ';';
  prev = outLabel;
});
execFileSync('ffmpeg', ['-loglevel', 'error', '-y', ...inputs, '-filter_complex', chain.replace(/;$/, ''),
  '-map', '[v]', '-map', '0:a', '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p', '-c:a', 'copy',
  path.join(take, 'script-builder-video-captions.mp4')], {stdio: 'inherit'});
console.log(timed.length + ' captions, ' + timed[0].start.toFixed(2) + ' - ' + timed[timed.length - 1].end.toFixed(2) + ' s');
