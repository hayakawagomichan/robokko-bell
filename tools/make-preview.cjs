// PCでの音色見本。node tools/make-preview.cjs で生成。本体への書き込みには不要。
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'AtomChristmasBell/AtomChristmasBell.ino'), 'utf8');
const rate = 16000;
const volume = Number(source.match(/constexpr float VOLUME = ([\d.]+)/)[1]);
const readArray = name => source.match(new RegExp(`const (?:float|uint8_t) ${name}\\[\\] = \\{([\\s\\S]*?)\\};`))[1]
  .split(',').map(v => Number(v.trim().replace(/f$/, '')));
function bell(hz, ms) {
  const n = Math.floor(rate * ms / 1000);
  return Array.from({length:n}, (_, k) => {
    const t = k / rate, w = 2 * Math.PI * hz * t;
    const a = Math.min(1, t / .006) * Math.min(1, (n - 1 - k) / (rate * .025));
    return Math.trunc((Math.sin(w)*Math.exp(-3.8*t) + .30*Math.sin(2.76*w)*Math.exp(-8*t)
       + .12*Math.sin(4.07*w)*Math.exp(-13*t)) * a * 21000 * volume);
  });
}
function save(name, notes, times, gap) {
  const samples = [];
  notes.forEach((note, i) => {
    for (const s of bell(note, times[i])) samples.push(s);
    for (let j = 0; j < rate * gap / 1000; j++) samples.push(0);
  });
  const b = Buffer.alloc(44 + samples.length * 2);
  b.write('RIFF'); b.writeUInt32LE(b.length-8, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16,16); b.writeUInt16LE(1,20); b.writeUInt16LE(1,22);
  b.writeUInt32LE(rate,24); b.writeUInt32LE(rate*2,28); b.writeUInt16LE(2,32);
  b.writeUInt16LE(16,34); b.write('data',36); b.writeUInt32LE(samples.length*2,40);
  let peak = 0;
  samples.forEach((s,i) => { if (Math.abs(s)>32767) throw Error('Clipping'); peak=Math.max(peak,Math.abs(s)); b.writeInt16LE(s,44+i*2); });
  fs.writeFileSync(path.join(root,'docs/audio',name),b);
  console.log(`${name}: ${(samples.length/rate).toFixed(2)} seconds, peak ${peak}/32767, no clipping`);
}
const notes=readArray('notes'), beats=readArray('beats');
if(notes.length !== beats.length) throw Error('Notes/beats mismatch');
save('bell-preview.wav', readArray('bells'), [520,520,520], 60);
save('jingle-preview.wav', notes, beats.map(v=>v*240-25), 25);
