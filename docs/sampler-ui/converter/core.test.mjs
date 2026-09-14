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

test('resampling, crossfade and KTSYNTH verification round-trip',()=>{
  const source=Int16Array.from({length:640},(_,i)=>Math.round(Math.sin(i/11)*14000));
  const pcm=resamplePcm(source,32000,24000),mixed=applyLoopCrossfade(pcm,60,420,20);
  const bytes=encodeKtSynth(mixed.pcm,{name:'Web Tone',sampleRate:24000,startFrame:0,endFrameExclusive:mixed.pcm.length,loopStartFrame:60,loopEndFrameExclusive:420,loopCrossfadeFrames:mixed.frames,sustainMode:1,attackMs:10,releaseMs:400,tuneCents:0,defaultGainQ8:256,rootNote:60});
  assert.ok(bytes.length<KTSYNTH_MAX_BYTES);assert.equal(parseKtSynth(bytes).verified,true);
  bytes[bytes.length-1]^=1;assert.throws(()=>parseKtSynth(bytes),/CRC/);
});

test('CRC implementation uses the ISO-HDLC check vector',()=>assert.equal(crc32IsoHdlc(new TextEncoder().encode('123456789')),0xcbf43926));

test('SF2 volume maps consistently between percent, attenuation and KTSYNTH gain',()=>{
  assert.equal(gainPercentToQ8(0),0);assert.equal(gainPercentToQ8(100),256);assert.equal(gainPercentToQ8(200),512);
  assert.equal(gainPercentToQ8(250),512);assert.equal(attenuationCbToGainPercent(0),100);assert.equal(attenuationCbToGainPercent(60),50);
});

test('duration and 2 MiB limits fail instead of truncating',()=>{
  const base={sampleRate:48000,frameCount:1,startFrame:0,endFrameExclusive:1,rootNote:60,tuneCents:0,attackMs:0,releaseMs:120,defaultGainQ8:256,sustainMode:0,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0};
  assert.throws(()=>validateKtSynthMetadata({...base,frameCount:48000*21,endFrameExclusive:48000*21},48000*21*2),/20秒/);
  assert.throws(()=>validateKtSynthMetadata(base,2,KTSYNTH_MAX_BYTES+1),/2 MiB/);
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
  assert.match(source,/使用するサウンド/);assert.match(source,/複数のサウンドが重ねられています/);
  assert.match(source,/sf2Editor\.regions\.length===1\?sf2Editor\.regions\[0\]\.id:null/);
  assert.match(source,/上書きしてSDカードに保存/);assert.match(source,/Melody／Chord／Bass/);
  assert.match(source,/\.sf3\$\/i/);assert.match(source,/application\/vnd\.instachord\.ktsynth/);
  assert.match(source,/WAV／MP3から作る/);assert.match(source,/自動検出：/);
  assert.match(source,/音程を判定できませんでした/);assert.match(source,/pitchConfirmed/);
  assert.match(source,/変換後の音量/);assert.match(source,/SF2の値に戻す/);
  assert.match(source,/defaultGainQ8:sf2GainQ8\(\)/);assert.match(source,/sf2Player\.setGainQ8/);
});
