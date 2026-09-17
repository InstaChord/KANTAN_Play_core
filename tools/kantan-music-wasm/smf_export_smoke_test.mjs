import fs from 'node:fs';
import vm from 'node:vm';

const appJs = fs.readFileSync('docs/ui/app.js', 'utf8');

const sandbox = {
  URL,
  Blob,
  console,
  location: { origin: 'http://localhost', href: 'http://localhost/ui/' },
  window: {},
  _elements: new Map(),
  document: {
    currentScript: { src: 'http://localhost/ui/app.js' },
    getElementById(id) {
      if (!sandbox._elements.has(id)) sandbox._elements.set(id, makeElement('div'));
      return sandbox._elements.get(id);
    },
    createElement(tag) {
      return makeElement(tag);
    },
    createElementNS(_namespace, tag) {
      return makeElement(tag);
    },
    createTextNode(text) {
      return { text };
    },
    querySelectorAll() {
      return [];
    },
    head: { appendChild() {} },
    body: { appendChild() {} },
  },
  fetch: async () => ({ ok: true, headers: { get: () => 'application/json' }, json: async () => ({ files: [] }) }),
};
sandbox.window = sandbox;

function makeElement(tag) {
  return {
    tag,
    innerHTML: '',
    textContent: '',
    className: '',
    checked: false,
    indeterminate: false,
    disabled: false,
    dataset: {},
    style: {},
    setAttribute() {},
    appendChild() {},
    addEventListener() {},
  };
}

vm.runInNewContext(appJs, sandbox, { filename: 'docs/ui/app.js' });

const song = {
  format: 'KANTANPlayCore',
  type: 'Song',
  version: 3,
  tempo: 120,
  base_key: 0,
  progression: {
    version: 1,
    length: 1,
    timeline: {
      0: { main: '1', slot: 0, part: [0] },
    },
  },
  drum_note: [[57, 42, 46, 50, 39, 38, 36]],
  slot: [{
    chord_mode: {
      part: [{
        tone: 1,
        loop_step: 0,
        arpeggio: [[100]],
      }],
    },
  }],
};

const kantan = {
  getMidiNoteNumber(pitch, degree, key) {
    return 60 + key + degree + pitch;
  },
};

const smf = sandbox.KANPLAY_FILE_UI_TEST.songToSmfWithKantan(song, kantan);
const text = Buffer.from(smf).toString('latin1');
if (!text.startsWith('MThd')) throw new Error('missing MThd header');
if (!text.includes('MTrk')) throw new Error('missing MTrk chunk');
if (sandbox.KANPLAY_FILE_UI_TEST.subStepTick(0, 1, 2, 480, 100) !== 320) {
  throw new Error('unexpected swing tick');
}

const chordChangeSong = structuredClone(song);
chordChangeSong.progression.length = 2;
chordChangeSong.progression.timeline[1] = { main: '4', slot: 0, part: [0] };
const chordChangeSmf = sandbox.KANPLAY_FILE_UI_TEST.songToSmfWithKantan(chordChangeSong, kantan);
const midiEvents = parseMidiEvents(chordChangeSmf);
if (!midiEvents.some(e => e.tick === 480 && e.status === 0x80 && e.note === 67)) {
  throw new Error('missing note off at chord change');
}
if (!midiEvents.some(e => e.tick === 480 && e.status === 0x90 && e.note === 70 && e.velocity > 0)) {
  throw new Error('missing next chord note on');
}

const anchoredSong = structuredClone(song);
anchoredSong.progression.length = 2;
anchoredSong.progression.timeline[1] = { main: '4', slot: 0, part: [0] };
anchoredSong.slot[0].chord_mode.part[0].loop_step = 7;
anchoredSong.slot[0].chord_mode.part[0].anchor_step = 4;
anchoredSong.slot[0].chord_mode.part[0].arpeggio = [[100]];
const anchoredEvents = parseMidiEvents(sandbox.KANPLAY_FILE_UI_TEST.songToSmfWithKantan(anchoredSong, kantan));
if (anchoredEvents.some(e => e.tick === 480 && e.status === 0x80 && e.note === 67)) {
  throw new Error('unexpected note off before anchor step');
}
if (!anchoredEvents.some(e => e.tick === 960 && e.status === 0x80 && e.note === 67)) {
  throw new Error('missing song-end note off for sustained pre-anchor note');
}
console.log(`smf_ok ${smf.length} bytes`);

function parseMidiEvents(bytes) {
  const data = Buffer.from(bytes);
  let pos = 14;
  const events = [];
  while (pos < data.length) {
    if (data.toString('ascii', pos, pos + 4) !== 'MTrk') break;
    pos += 4;
    const trackEnd = pos + 4 + data.readUInt32BE(pos);
    pos += 4;
    let tick = 0;
    while (pos < trackEnd) {
      const delta = readVlq();
      tick += delta;
      const status = data[pos++];
      if (status === 0xff) {
        const type = data[pos++];
        const len = readVlq();
        pos += len;
        if (type === 0x2f) break;
      } else if ((status & 0xf0) === 0x80 || (status & 0xf0) === 0x90) {
        events.push({ tick, status: status & 0xf0, channel: status & 0x0f, note: data[pos++], velocity: data[pos++] });
      } else if ((status & 0xf0) === 0xc0 || (status & 0xf0) === 0xd0) {
        pos += 1;
      } else {
        pos += 2;
      }
    }
    pos = trackEnd;
  }
  return events;

  function readVlq() {
    let value = 0;
    let b;
    do {
      b = data[pos++];
      value = (value << 7) | (b & 0x7f);
    } while (b & 0x80);
    return value;
  }
}
