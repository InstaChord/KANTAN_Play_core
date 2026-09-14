import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {resolvePresetRegions,extractRegionPcm,initialSoundProgramIndex} from './sf2.js';
import {resamplePcm,applyLoopCrossfade} from './audio.js';
import {crc32IsoHdlc,encodeKtSynth,parseKtSynth,validateKtSynthMetadata,gainPercentToQ8,attenuationCbToGainPercent,KTSYNTH_MAX_BYTES} from './ktsynth.js';
import {detectStablePitch,midiNoteName,noteFromFilename,parseWavUnityNote} from './audio-input.js';

const gen=(op,raw)=>({op,raw,signed:raw>32767?raw-65536:raw,lo:raw&255,hi:raw>>>8});
const zone=(...entries)=>({entries,byOp:new Map(entries.map(g=>[g.op,g]))});
function layeredSf2(){
  const samples=Int16Array.from({length:64},(_,i)=>Math.round(Math.sin(i/5)*12000));
  return {samples,presets:[{name:'Layered',zones:[zone(gen(41,0))]}],instruments:[{name:'Two sounds',zones:[zone(gen(53,0),gen(43,0x7f00),gen(44,0x7f00)),zone(gen(53,1),gen(43,0x7f00),gen(44,0x7f00))]}],sampleHeaders:[{name:'Warm',start:0,end:32,startLoop:4,endLoop:28,sampleRate:32000,originalPitch:60,pitchCorrection:0,sampleLink:0,sampleType:1},{name:'Bright',start:32,end:64,startLoop:36,endLoop:60,sampleRate:32000,originalPitch:60,pitchCorrection:0,sampleLink:0,sampleType:1}]};
}

test('a layered tone exposes every matching sound instead of choosing the first',()=>{
  const sf2=layeredSf2(),regions=resolvePresetRegions(sf2,0,60,110);
  assert.equal(regions.length,2);assert.deepEqual(regions.map(r=>r.sampleName),['Warm','Bright']);
  assert.equal(initialSoundProgramIndex([{index:0},{index:1}]),null);
  assert.deepEqual([...extractRegionPcm(sf2,regions[1]).pcm],[...sf2.samples.slice(32,64)]);
});

const ktsLayer=(pcm,overrides={})=>({pcm,sampleRate:24000,startFrame:0,endFrameExclusive:pcm.length,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,sustainMode:0,attackMs:10,releaseMs:400,tuneCents:0,defaultGainQ8:256,rootNote:60,...overrides});
function chunkOffset(bytes,id){const dv=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);for(let p=12;p+8<=bytes.length;){const size=dv.getUint32(p+4,true);if(String.fromCharCode(...bytes.subarray(p,p+4))===id)return p;p+=8+size+(size&1);}return-1;}

test('single-layer KTS2 resampling, crossfade and verification round-trip',()=>{
  const source=Int16Array.from({length:640},(_,i)=>Math.round(Math.sin(i/11)*14000));
  const pcm=resamplePcm(source,32000,24000),mixed=applyLoopCrossfade(pcm,60,420,20);
  const bytes=encodeKtSynth([ktsLayer(mixed.pcm,{loopStartFrame:60,loopEndFrameExclusive:420,loopCrossfadeFrames:mixed.frames,sustainMode:1})],{name:'Web Tone'});
  const parsed=parseKtSynth(bytes);assert.ok(bytes.length<KTSYNTH_MAX_BYTES);assert.equal(parsed.verified,true);assert.equal(parsed.metadata.layerCount,1);assert.equal(new TextDecoder().decode(bytes.subarray(chunkOffset(bytes,'KNTN')+8,chunkOffset(bytes,'KNTN')+12)),'KTS2');
  bytes[bytes.length-1]^=1;assert.throws(()=>parseKtSynth(bytes),/CRC/);
});

test('two-layer KTS2 preserves independent rate, root, loop and gain',()=>{
  const a=Int16Array.from({length:480},(_,i)=>i*3),b=Int16Array.from({length:360},(_,i)=>-i*2);
  const bytes=encodeKtSynth([ktsLayer(a,{sampleRate:24000,rootNote:60,defaultGainQ8:300}),ktsLayer(b,{sampleRate:18000,rootNote:72,tuneCents:-7,defaultGainQ8:212,loopStartFrame:40,loopEndFrameExclusive:320,loopCrossfadeFrames:20,sustainMode:1})],{name:'Dual Tone'});
  const parsed=parseKtSynth(bytes);assert.equal(parsed.metadata.layerCount,2);assert.equal(parsed.layers[0].sampleRate,24000);assert.equal(parsed.layers[1].sampleRate,18000);assert.equal(parsed.layers[1].rootNote,72);assert.equal(parsed.layers[1].tuneCents,-7);assert.equal(parsed.layers[1].loopEndFrameExclusive,320);assert.equal(parsed.layers[0].defaultGainQ8+parsed.layers[1].defaultGainQ8,512);assert.ok(chunkOffset(bytes,'KT2D')>0);
});

test('CRC implementation uses the ISO-HDLC check vector',()=>assert.equal(crc32IsoHdlc(new TextEncoder().encode('123456789')),0xcbf43926));

test('SF2 volume maps consistently between percent, attenuation and KTSYNTH gain',()=>{
  assert.equal(gainPercentToQ8(0),0);assert.equal(gainPercentToQ8(100),256);assert.equal(gainPercentToQ8(200),512);
  assert.equal(gainPercentToQ8(250),512);assert.equal(attenuationCbToGainPercent(0),100);assert.equal(attenuationCbToGainPercent(60),50);
});

test('duration and 2 MiB limits fail instead of truncating',()=>{
  const base={sampleRate:48000,frameCount:1,startFrame:0,endFrameExclusive:1,rootNote:60,tuneCents:0,attackMs:0,releaseMs:120,defaultGainQ8:256,sustainMode:0,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,pcmChunk:0};
  assert.throws(()=>validateKtSynthMetadata({...base,frameCount:48000*21,endFrameExclusive:48000*21},48000*21*2),/20 seconds/);
  assert.throws(()=>validateKtSynthMetadata(base,2,KTSYNTH_MAX_BYTES+1),/2 MiB/);
  assert.throws(()=>validateKtSynthMetadata({layers:[base,{...base,pcmChunk:1,defaultGainQ8:257}]},[2,2]),/Combined layer volume/);
});

test('KTS2 rejects missing or duplicate KT2D and layerCount mismatch',()=>{
  const pcm=new Int16Array(32),dual=encodeKtSynth([ktsLayer(pcm,{defaultGainQ8:256}),ktsLayer(pcm,{defaultGainQ8:256})],{name:'Checks'}),kt2d=chunkOffset(dual,'KT2D'),kntn=chunkOffset(dual,'KNTN');
  const missing=dual.slice(0,kt2d);new DataView(missing.buffer).setUint32(4,missing.length-8,true);assert.throws(()=>parseKtSynth(missing),/layerCount does not match KT2D/);
  const mismatch=dual.slice();mismatch[kntn+8+20]=1;assert.throws(()=>parseKtSynth(mismatch),/layerCount does not match KT2D/);
  const chunk=dual.slice(kt2d),duplicate=new Uint8Array(dual.length+chunk.length);duplicate.set(dual);duplicate.set(chunk,dual.length);new DataView(duplicate.buffer).setUint32(4,duplicate.length-8,true);assert.throws(()=>parseKtSynth(duplicate),/duplicated/);
});

function wavWithUnityNote(note,fraction=0){
  const out=new Uint8Array(56),dv=new DataView(out.buffer),ascii=(at,value)=>{for(let i=0;i<value.length;i++)out[at+i]=value.charCodeAt(i);};
  ascii(0,'RIFF');dv.setUint32(4,48,true);ascii(8,'WAVE');ascii(12,'smpl');dv.setUint32(16,36,true);dv.setUint32(32,note,true);dv.setUint32(36,fraction,true);
  return out;
}

test('WAV metadata and unambiguous filename pitches have priority-ready values',()=>{
  assert.deepEqual(parseWavUnityNote(wavWithUnityNote(69)),{note:69,tuneCents:0,source:'WAV metadata',confidence:1});
  assert.equal(noteFromFilename('Piano_C#4.wav').note,61);
  assert.equal(noteFromFilename('ambiguous_C4_take_D4.wav'),null);
  assert.equal(midiNoteName(60),'C4');
});

test('multi-window pitch detection accepts a stable single tone and rejects noise',()=>{
  const rate=32000,length=rate,stable=Float32Array.from({length},(_,i)=>0.5*Math.sin(2*Math.PI*440*i/rate));
  const detected=detectStablePitch(stable,rate);
  assert.equal(detected.reliable,true);assert.equal(detected.note,69);assert.ok(detected.confidence>=0.72);
  let seed=1;const noise=Float32Array.from({length},()=>{seed=(seed*1664525+1013904223)>>>0;return((seed/0xffffffff)*2-1)*0.4;});
  assert.equal(detectStablePitch(noise,rate).reliable,false);
});

test('the simple UI keeps layered selection and primary save in the visible flow',async()=>{
  const source=await readFile(new URL('../app.js',import.meta.url),'utf8');
  const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
  const api=await readFile(new URL('../../../main/sampler/sampler_web_api.cpp',import.meta.url),'utf8');
  const parser=await readFile(new URL('../../../main/sampler/sampler_ktsynth.hpp',import.meta.url),'utf8');
  assert.match(source,/Source Sounds/);assert.match(source,/Select up to two sounds/);
  assert.match(source,/sf2Layers:\[newSf2Layer\(\)\]/);assert.match(source,/type:'checkbox'/);
  assert.doesNotMatch(source,/Add Layer 2|second audio file/);assert.match(source,/Remove Layer 2/);
  assert.match(source,/audio:newAudioLayer\(\)/);assert.match(source,/Combined Layer 1 and Layer 2 volume/);
  assert.match(source,/Create from one audio file/);assert.match(source,/KTS2 · 1 layer/);
  assert.match(source,/Overwrite and Save to SD Card/);assert.match(source,/Melody, Chord, or Bass/);
  assert.match(source,/\.sf3\$\/i/);assert.match(source,/application\/vnd\.instachord\.ktsynth/);
  assert.match(source,/Create from WAV \/ MP3/);assert.match(source,/Detected:/);
  assert.match(source,/pitch could not be detected/);assert.match(source,/pitchConfirmed/);
  assert.match(source,/sf2-volume-heading/);assert.match(source,/Reset to SF2 value/);
  assert.match(source,/defaultGainQ8:gainPercentToQ8\(settings\.volumePercent\)/);
  assert.match(source,/gainQ8:gainPercentToQ8\(layer\.volumePercent\)/);
  assert.match(html,/data-view="synth-view">Synth/);assert.doesNotMatch(source,/if\(region\.unsupported\.length\)throw/);
  assert.match(source,/Create a Synth Sound/);assert.match(source,/Bass, Melody, and Chord parts/);
  assert.doesNotMatch(source,/external server|within this browser/i);
  assert.match(source,/unsupported features (?:are|will be) ignored/i);assert.match(source,/Audition Converted Sound on Device/);
  assert.match(source,/\/api\/sampler\/preview-ktsynth/);assert.match(source,/\.web-preview\.ktsynth/);
  assert.match(api,/response_ktsynth_preview/);assert.match(api,/Synth\/\.web-preview\.ktsynth/);
  assert.match(parser,/memcmp\(metadata, "KTS2", 4\)/);assert.match(parser,/"KT2D"/);
  assert.match(parser,/layer_count/);assert.match(parser,/metadata, metadata_bytes, 16, 20/);
  assert.doesNotMatch(source,/KTS1/);assert.doesNotMatch(parser,/KTS1/);
});
