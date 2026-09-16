// Browser-side WAV/MP3 decode and conservative source-pitch suggestion.
const NOTE_INDEX = new Map([
  ['C', 0], ['C#', 1], ['DB', 1], ['D', 2], ['D#', 3], ['EB', 3],
  ['E', 4], ['F', 5], ['F#', 6], ['GB', 6], ['G', 7], ['G#', 8],
  ['AB', 8], ['A', 9], ['A#', 10], ['BB', 10], ['B', 11],
]);

const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

export function midiNoteName(note) {
  if (!Number.isInteger(note) || note < 0 || note > 127) return '—';
  return `${['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][note % 12]}${Math.floor(note / 12) - 1}`;
}

export function parseWavUnityNote(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (bytes.length < 12) return null;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const id = offset => String.fromCharCode(bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]);
  if (id(0) !== 'RIFF' || id(8) !== 'WAVE') return null;
  const riffEnd = Math.min(bytes.length, dv.getUint32(4, true) + 8);
  for (let pos = 12; pos + 8 <= riffEnd;) {
    const size = dv.getUint32(pos + 4, true);
    const body = pos + 8;
    if (body + size > riffEnd) return null;
    if (id(pos) === 'smpl' && size >= 36) {
      const rootNote = dv.getUint32(body + 12, true);
      if (rootNote <= 127) {
        const fraction = dv.getUint32(body + 16, true) / 0x100000000;
        return { note: rootNote, tuneCents: Math.round(fraction * 100), source: 'WAV metadata', confidence: 1 };
      }
      return null;
    }
    pos = body + size + (size & 1);
  }
  return null;
}

export function noteFromFilename(filename) {
  const stem = String(filename || '').replace(/\.[^.]+$/, '');
  const matches = [...stem.matchAll(/(?:^|[^A-Za-z0-9])([A-Ga-g])([#b♯♭]?)[_-]?(-?\d)(?=$|[^0-9])/g)];
  if (matches.length !== 1) return null;
  const accidental = matches[0][2].replace('♯', '#').replace('♭', 'b');
  const key = (matches[0][1] + accidental).toUpperCase();
  const pitchClass = NOTE_INDEX.get(key);
  const octave = Number(matches[0][3]);
  const note = pitchClass === undefined ? -1 : (octave + 1) * 12 + pitchClass;
  return note >= 0 && note <= 127
    ? { note, tuneCents: 0, source: 'Filename', confidence: 0.9 }
    : null;
}

function windowPitch(samples, sampleRate, start, length) {
  let mean = 0;
  for (let i = 0; i < length; ++i) mean += samples[start + i];
  mean /= length;
  let energy = 0;
  for (let i = 0; i < length; ++i) {
    const value = samples[start + i] - mean;
    energy += value * value;
  }
  const rms = Math.sqrt(energy / length);
  if (rms < 0.008) return null;

  const minLag = Math.max(2, Math.floor(sampleRate / 1200));
  const maxLag = Math.min(length - 3, Math.ceil(sampleRate / 45));
  const scores = new Float32Array(maxLag + 1);
  let globalBest = 0;
  for (let lag = minLag; lag <= maxLag; ++lag) {
    let cross = 0, left = 0, right = 0;
    const count = length - lag;
    for (let i = 0; i < count; ++i) {
      const a = samples[start + i] - mean;
      const b = samples[start + i + lag] - mean;
      cross += a * b;
      left += a * a;
      right += b * b;
    }
    const score = cross / Math.sqrt(Math.max(1e-12, left * right));
    scores[lag] = score;
    if (score > globalBest) globalBest = score;
  }
  if (globalBest < 0.55) return null;

  // Prefer the shortest strong local maximum to reduce sub-harmonic errors.
  let bestLag = minLag;
  const threshold = Math.max(0.58, globalBest * 0.9);
  for (let lag = minLag + 1; lag < maxLag; ++lag) {
    if (scores[lag] >= threshold && scores[lag] >= scores[lag - 1] && scores[lag] >= scores[lag + 1]) {
      bestLag = lag;
      break;
    }
  }
  if (bestLag === minLag) {
    for (let lag = minLag + 1; lag <= maxLag; ++lag) if (scores[lag] > scores[bestLag]) bestLag = lag;
  }
  const y0 = scores[bestLag - 1] || scores[bestLag];
  const y1 = scores[bestLag];
  const y2 = scores[bestLag + 1] || scores[bestLag];
  const divisor = y0 - 2 * y1 + y2;
  const offset = Math.abs(divisor) > 1e-8 ? clamp(0.5 * (y0 - y2) / divisor, -0.5, 0.5) : 0;
  return { frequency: sampleRate / (bestLag + offset), clarity: y1, rms };
}

export function detectStablePitch(samples, sampleRate) {
  const pcm = samples instanceof Float32Array
    ? samples
    : Float32Array.from(samples, value => value / 32768);
  if (!Number.isFinite(sampleRate) || sampleRate < 8000 || pcm.length < sampleRate * 0.12) {
    return { reliable: false, confidence: 0, reason: 'The source is too short' };
  }
  const windowLength = Math.min(4096, Math.max(2048, 2 ** Math.floor(Math.log2(pcm.length / 3))));
  if (windowLength < 1024 || pcm.length < windowLength) return { reliable: false, confidence: 0 };
  const positions = [0.15, 0.3, 0.45, 0.6, 0.75].map(fraction =>
    clamp(Math.round(pcm.length * fraction - windowLength / 2), 0, pcm.length - windowLength));
  const windows = positions.map(start => windowPitch(pcm, sampleRate, start, windowLength)).filter(Boolean);
  if (windows.length < 3) return { reliable: false, confidence: 0, windows: windows.length };
  const midiValues = windows.map(value => 69 + 12 * Math.log2(value.frequency / 440)).sort((a, b) => a - b);
  const median = midiValues[Math.floor(midiValues.length / 2)];
  const deviations = midiValues.map(value => Math.abs(value - median) * 100).sort((a, b) => a - b);
  const spreadCents = deviations[Math.floor(deviations.length / 2)];
  const clarity = windows.reduce((sum, value) => sum + value.clarity, 0) / windows.length;
  const confidence = clamp(clarity * (windows.length / positions.length) * (1 - spreadCents / 80), 0, 1);
  const nearest = clamp(Math.round(median), 0, 127);
  const tuneCents = clamp(Math.round((median - nearest) * 100), -100, 100);
  return {
    note: nearest,
    tuneCents,
    frequency: 440 * Math.pow(2, (median - 69) / 12),
    confidence,
    spreadCents,
    windows: windows.length,
    reliable: confidence >= 0.72 && spreadCents <= 25,
    source: 'Audio analysis',
  };
}

export function choosePitchSuggestion(arrayBuffer, filename, pcm, sampleRate) {
  return parseWavUnityNote(arrayBuffer)
    || noteFromFilename(filename)
    || detectStablePitch(pcm, sampleRate);
}

export function audioBufferToMonoPcm(audioBuffer) {
  const frames = audioBuffer.length;
  const channels = audioBuffer.numberOfChannels;
  const out = new Int16Array(frames);
  for (let frame = 0; frame < frames; ++frame) {
    let mixed = 0;
    for (let channel = 0; channel < channels; ++channel) mixed += audioBuffer.getChannelData(channel)[frame];
    mixed = clamp(mixed / Math.max(1, channels), -1, 1);
    out[frame] = mixed < 0 ? Math.round(mixed * 32768) : Math.round(mixed * 32767);
  }
  return out;
}

export async function decodeAudioFile(file) {
  const arrayBuffer = await file.arrayBuffer();
  const Audio = globalThis.AudioContext || globalThis.webkitAudioContext;
  if (!Audio) throw new Error('Audio decoding is not available in this browser.');
  const context = new Audio();
  try {
    const decoded = await context.decodeAudioData(arrayBuffer.slice(0));
    const pcm = audioBufferToMonoPcm(decoded);
    return {
      arrayBuffer,
      pcm,
      sampleRate: decoded.sampleRate,
      duration: decoded.duration,
      channels: decoded.numberOfChannels,
      suggestion: choosePitchSuggestion(arrayBuffer, file.name, pcm, decoded.sampleRate),
    };
  } catch (error) {
    throw new Error(`The audio could not be analyzed. Choose a WAV or MP3 file. (${error.message})`);
  } finally {
    if (context.close) await context.close();
  }
}
