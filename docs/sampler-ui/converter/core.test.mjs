import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {resolvePresetRegions,extractRegionPcm,initialSoundProgramIndex,sustainCentibelsToQ15} from './sf2.js';
import {resamplePcm,applyLoopCrossfade,PreviewPlayer} from './audio.js';
import {crc32IsoHdlc,encodeKtSynth,parseKtSynth,validateKtSynthMetadata,gainPercentToQ8,attenuationCbToGainPercent,KTSYNTH_MAX_BYTES} from './ktsynth.js';
import {detectStablePitch,midiNoteName,noteFromFilename,parseWavUnityNote} from './audio-input.js';

const gen=(op,raw)=>({op,raw,signed:raw>32767?raw-65536:raw,lo:raw&255,hi:raw>>>8});
const signedGen=(op,value)=>gen(op,value<0?value+65536:value);
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

test('preset and instrument global/local envelope generators are combined',()=>{
  const sf2={samples:new Int16Array(40000),presets:[{name:'Envelope',zones:[zone(signedGen(33,-1200),gen(37,100)),zone(gen(41,0),signedGen(33,1200))]}],instruments:[{name:'Combined',zones:[zone(signedGen(35,-1200),signedGen(36,0)),zone(gen(53,0),signedGen(38,1200),gen(37,100),gen(50,1))]}],sampleHeaders:[{name:'Wave',start:0,end:40000,startLoop:100,endLoop:200,sampleRate:32000,originalPitch:60,pitchCorrection:0,sampleLink:0,sampleType:1}]};
  const region=resolvePresetRegions(sf2,0,60,110)[0];assert.equal(region.delayMs,1000);assert.equal(region.attackMs,1);assert.equal(region.holdMs,500);assert.equal(region.decayMs,1000);assert.equal(region.sustainLevelQ15,sustainCentibelsToQ15(200));assert.equal(region.releaseMs,2000);assert.equal(region.loopEnd,200+32768);
  const ids=resolvePresetRegions({...sf2,instruments:[{...sf2.instruments[0],zones:[sf2.instruments[0].zones[0],sf2.instruments[0].zones[1],zone(gen(53,0),signedGen(52,7))]}]},0,60,110).map(item=>item.id);assert.equal(new Set(ids).size,2);
});

const ktsLayer=(pcm,overrides={})=>({pcm,sampleRate:24000,startFrame:0,endFrameExclusive:pcm.length,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,sustainMode:0,delayMs:0,attackMs:10,holdMs:0,decayMs:0,sustainLevelQ15:32768,releaseMs:400,tuneCents:0,defaultGainQ8:256,rootNote:60,...overrides});
function chunkOffset(bytes,id){const dv=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);for(let p=12;p+8<=bytes.length;){const size=dv.getUint32(p+4,true);if(String.fromCharCode(...bytes.subarray(p,p+4))===id)return p;p+=8+size+(size&1);}return-1;}

test('single-layer KTS2 resampling, crossfade and verification round-trip',()=>{
  const source=Int16Array.from({length:640},(_,i)=>Math.round(Math.sin(i/11)*14000));
  const pcm=resamplePcm(source,32000,24000),mixed=applyLoopCrossfade(pcm,60,420,20);
  const bytes=encodeKtSynth([ktsLayer(mixed.pcm,{loopStartFrame:60,loopEndFrameExclusive:420,loopCrossfadeFrames:mixed.frames,sustainMode:1})],{name:'Web Tone'});
  const parsed=parseKtSynth(bytes),kntn=chunkOffset(bytes,'KNTN'),dv=new DataView(bytes.buffer);assert.ok(bytes.length<KTSYNTH_MAX_BYTES);assert.equal(parsed.verified,true);assert.equal(parsed.metadata.layerCount,1);assert.equal(new TextDecoder().decode(bytes.subarray(kntn+8,kntn+12)),'KTS2');assert.equal(dv.getUint16(kntn+14,true),1);
  bytes[bytes.length-1]^=1;assert.throws(()=>parseKtSynth(bytes),/CRC/);
});

test('two-layer KTS2 preserves independent rate, root, loop and gain',()=>{
  const a=Int16Array.from({length:480},(_,i)=>i*3),b=Int16Array.from({length:360},(_,i)=>-i*2);
  const bytes=encodeKtSynth([ktsLayer(a,{sampleRate:24000,rootNote:60,defaultGainQ8:300}),ktsLayer(b,{sampleRate:18000,pcmSourceLayer:1,rootNote:72,tuneCents:-7,delayMs:1.2,holdMs:9,decayMs:1200,sustainLevelQ15:16384,releaseMs:8000,defaultGainQ8:212,loopStartFrame:40,loopEndFrameExclusive:320,loopCrossfadeFrames:20,sustainMode:1})],{name:'Dual Tone'});
  const parsed=parseKtSynth(bytes);assert.equal(parsed.metadata.layerCount,2);assert.equal(parsed.layers[0].sampleRate,24000);assert.equal(parsed.layers[1].sampleRate,18000);assert.equal(parsed.layers[1].pcmSourceLayer,1);assert.equal(parsed.layers[1].rootNote,72);assert.equal(parsed.layers[1].tuneCents,-7);assert.equal(parsed.layers[1].delay100us,12);assert.equal(parsed.layers[1].holdMs,9);assert.equal(parsed.layers[1].decayMs,1200);assert.equal(parsed.layers[1].sustainLevelQ15,16384);assert.equal(parsed.layers[1].releaseMs,8000);assert.equal(parsed.layers[1].loopEndFrameExclusive,320);assert.equal(parsed.layers[0].defaultGainQ8+parsed.layers[1].defaultGainQ8,512);assert.ok(chunkOffset(bytes,'KT2D')>0);
});

test('two descriptors can share Layer 1 PCM without KT2D or duplicate CRC input',()=>{
  const pcm=Int16Array.from({length:480},(_,i)=>i*5-1000),shared={...ktsLayer(pcm,{pcmSourceLayer:0,defaultGainQ8:200,tuneCents:17,delayMs:.3,attackMs:40,holdMs:80,decayMs:900,sustainLevelQ15:12000,releaseMs:6000}),pcm:undefined},bytes=encodeKtSynth([ktsLayer(pcm,{defaultGainQ8:256,tuneCents:-5}),shared],{name:'Shared Wave'}),parsed=parseKtSynth(bytes);
  assert.equal(chunkOffset(bytes,'KT2D'),-1);assert.equal(parsed.layers[1].pcmSourceLayer,0);assert.equal(parsed.layers[1].pcm,parsed.layers[0].pcm);assert.equal(parsed.layers[1].tuneCents,17);assert.equal(parsed.layers[1].delay100us,3);assert.equal(parsed.layers[1].sustainLevelQ15,12000);assert.equal(parsed.layers[1].releaseMs,6000);
  const broken=bytes.slice();broken[broken.length-1]^=1;assert.throws(()=>parseKtSynth(broken),/CRC/);
});

test('Saw Lead fixture keeps shared PCM, layer settings, chunks, and CRC after re-save',async()=>{
  const fixture=new Uint8Array(await readFile(new URL('../../Sample_Sound/KANTAN_Synth/Saw_Lead_Test-F_4.ktsynth',import.meta.url))),parsed=parseKtSynth(fixture);
  assert.equal(parsed.verified,true);assert.equal(parsed.hasSmpl,false);assert.equal(parsed.layers.length,2);assert.equal(chunkOffset(fixture,'KT2D'),-1);
  assert.equal(parsed.layers[0].pcmSourceLayer,0);assert.equal(parsed.layers[1].pcmSourceLayer,0);assert.equal(parsed.layers[0].pcm,parsed.layers[1].pcm);
  for(const layer of parsed.layers){assert.equal(layer.sampleRate,48000);assert.equal(layer.frameCount,256);assert.equal(layer.rootNote,66);assert.equal(layer.loopStartFrame,64);assert.equal(layer.loopEndFrameExclusive,192);assert.equal(layer.attackMs,1);assert.equal(layer.holdMs,1);assert.equal(layer.decayMs,1000);assert.equal(layer.sustainLevelQ15,32768);assert.equal(layer.releaseMs,500);assert.equal(layer.defaultGainQ8,128);}
  assert.equal(parsed.layers[0].tuneCents,-19);assert.equal(parsed.layers[0].delay100us,10);assert.equal(parsed.layers[1].tuneCents,-27);assert.equal(parsed.layers[1].delay100us,3);
  const saved=encodeKtSynth(parsed.layers,{name:parsed.name},{includeSmpl:parsed.hasSmpl}),reopened=parseKtSynth(saved);
  assert.equal(chunkOffset(saved,'KT2D'),-1);assert.equal(reopened.layers[1].pcmSourceLayer,0);assert.equal(reopened.metadata.contentCrc32,parsed.metadata.contentCrc32);assert.deepEqual(saved,fixture);
});

test('an existing KANTAN Synth can be loaded, edited and encoded again',()=>{
  const first=Int16Array.from({length:480},(_,i)=>i*3),second=Int16Array.from({length:360},(_,i)=>-i*2);
  const original=parseKtSynth(encodeKtSynth([ktsLayer(first,{rootNote:60,defaultGainQ8:256}),ktsLayer(second,{sampleRate:18000,rootNote:72,tuneCents:-7,attackMs:20,releaseMs:700,defaultGainQ8:200,loopStartFrame:40,loopEndFrameExclusive:320,loopCrossfadeFrames:20,sustainMode:1})],{name:'Editable'}));
  const edited=original.layers.map((layer,index)=>({...layer,pcm:layer.pcm.slice(),rootNote:index?74:layer.rootNote,tuneCents:index?12:layer.tuneCents,attackMs:index?35:layer.attackMs,releaseMs:index?900:layer.releaseMs,defaultGainQ8:index?180:layer.defaultGainQ8}));
  const reopened=parseKtSynth(encodeKtSynth(edited,{name:'Edited Tone'}));
  assert.equal(reopened.name,'Edited Tone');assert.equal(reopened.layers.length,2);assert.deepEqual([...reopened.layers[0].pcm],[...first]);assert.deepEqual([...reopened.layers[1].pcm],[...second]);assert.equal(reopened.layers[1].rootNote,74);assert.equal(reopened.layers[1].tuneCents,12);assert.equal(reopened.layers[1].attackMs,35);assert.equal(reopened.layers[1].releaseMs,900);assert.equal(reopened.layers[1].loopStartFrame,40);assert.equal(reopened.layers[1].loopEndFrameExclusive,320);
});

test('CRC implementation uses the ISO-HDLC check vector',()=>assert.equal(crc32IsoHdlc(new TextEncoder().encode('123456789')),0xcbf43926));

test('SF2 volume maps consistently between percent, attenuation and KTSYNTH gain',()=>{
  assert.equal(gainPercentToQ8(0),0);assert.equal(gainPercentToQ8(100),256);assert.equal(gainPercentToQ8(200),512);
  assert.equal(gainPercentToQ8(250),512);assert.equal(attenuationCbToGainPercent(0),100);assert.equal(attenuationCbToGainPercent(60),50);
});

test('duration and 2 MiB limits fail instead of truncating',()=>{
  const base={sampleRate:48000,frameCount:1,startFrame:0,endFrameExclusive:1,rootNote:60,tuneCents:0,delay100us:0,attackMs:0,holdMs:0,decayMs:0,sustainLevelQ15:32768,releaseMs:120,defaultGainQ8:256,sustainMode:0,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,pcmSourceLayer:0,envelopeFlags:0};
  assert.throws(()=>validateKtSynthMetadata({...base,frameCount:48000*21,endFrameExclusive:48000*21},48000*21*2),/20 seconds/);
  assert.throws(()=>validateKtSynthMetadata(base,2,KTSYNTH_MAX_BYTES+1),/2 MiB/);
  assert.throws(()=>validateKtSynthMetadata({layers:[base,{...base,pcmSourceLayer:1,defaultGainQ8:257}]},[2,2]),/Combined layer volume/);
  assert.throws(()=>validateKtSynthMetadata({...base,releaseMs:10001},2),/10000 ms/);assert.throws(()=>validateKtSynthMetadata({...base,sustainLevelQ15:32769},2),/sustain level/);
});

test('KTS2 v2.1 rejects PCM-source/chunk mismatches, duplicates and v2.0',()=>{
  const pcm=new Int16Array(32),dual=encodeKtSynth([ktsLayer(pcm,{defaultGainQ8:256}),ktsLayer(pcm,{defaultGainQ8:256})],{name:'Checks'}),kt2d=chunkOffset(dual,'KT2D'),kntn=chunkOffset(dual,'KNTN');
  const missing=dual.slice(0,kt2d);new DataView(missing.buffer).setUint32(4,missing.length-8,true);assert.throws(()=>parseKtSynth(missing),/PCM source descriptors/);
  const mismatch=dual.slice();mismatch[kntn+8+20]=1;assert.throws(()=>parseKtSynth(mismatch),/Unused Layer 2 descriptor|PCM source descriptors/);
  const chunk=dual.slice(kt2d),duplicate=new Uint8Array(dual.length+chunk.length);duplicate.set(dual);duplicate.set(chunk,dual.length);new DataView(duplicate.buffer).setUint32(4,duplicate.length-8,true);assert.throws(()=>parseKtSynth(duplicate),/duplicated/);
  const old=dual.slice();new DataView(old.buffer).setUint16(kntn+14,0,true);assert.throws(()=>parseKtSynth(old),/Unsupported KNTN version/);
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

test('browser preview schedules Delay, Attack, Hold, Decay and Sustain before playback',async()=>{
  const events=[],parameter={value:0,setValueAtTime:(value,time)=>events.push(['set',value,time]),linearRampToValueAtTime:(value,time)=>events.push(['ramp',value,time]),cancelScheduledValues:()=>{},setTargetAtTime:()=>{}},source={playbackRate:{value:1},connect:gain=>gain,start:time=>events.push(['start',time]),stop:()=>{}},gain={gain:parameter,connect:()=>({})};
  class AudioContextMock{constructor(){this.currentTime=2;this.destination={};}resume(){return Promise.resolve();}createBuffer(){return{getChannelData:()=>new Float32Array(8)};}createBufferSource(){return source;}createGain(){return gain;}}
  global.window={AudioContext:AudioContextMock};const player=new PreviewPlayer();await player.play(new Int16Array(8),{sampleRate:8000,previewNote:60,rootNote:60,tuneCents:0,sustainMode:'off',loopStart:0,loopEnd:0,delayMs:100,attackMs:200,holdMs:300,decayMs:400,sustainLevelQ15:16384,releaseMs:900,gainQ8:256});delete global.window;
  assert.deepEqual(events.map(event=>event.slice(0,2)),[['set',0],['set',0],['ramp',1],['set',1],['ramp',.5],['start',2.1]]);assert.ok(Math.abs(events[2][2]-2.3)<1e-9);assert.ok(Math.abs(events[3][2]-2.6)<1e-9);assert.equal(events[4][2],3);assert.equal(player.releaseMs,900);
});

test('the simple UI keeps layered selection and primary save in the visible flow',async()=>{
  const source=await readFile(new URL('../app.js',import.meta.url),'utf8');
  const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
  const api=await readFile(new URL('../../../main/sampler/sampler_web_api.cpp',import.meta.url),'utf8');
  const firmware=await readFile(new URL('../../../main/sampler/sampler_app.cpp',import.meta.url),'utf8');
  const parser=await readFile(new URL('../../../main/sampler/sampler_ktsynth.hpp',import.meta.url),'utf8');
  assert.match(source,/Sounds/);assert.match(source,/Choose up to 2/);
  assert.match(source,/sf2Layers:\[newSf2Layer\(\)\]/);assert.match(source,/type:'checkbox'/);
  assert.doesNotMatch(source,/Add Layer 2|second audio file/);assert.match(source,/Remove Layer 2/);
  assert.match(source,/audio:newAudioLayer\(\)/);assert.match(source,/Combined Layer 1 and Layer 2 volume/);
  assert.match(source,/1 audio file/);assert.match(source,/KTS2 · 1 layer/);
  assert.match(source,/Edit Synth/);assert.match(source,/\.ktsynth file/);
  assert.match(source,/parseKtSynth\(new Uint8Array\(await file\.arrayBuffer\(\)\)\)/);
  assert.match(source,/ktsLayers:\[\]/);
  assert.match(source,/Same as Layer 1/);assert.match(source,/Different sound/);assert.match(source,/pcmSourceLayer/);
  assert.match(source,/Delay \(ms\)/);assert.match(source,/Hold \(ms\)/);assert.match(source,/Decay \(ms\)/);assert.match(source,/Sustain Level \(%\)/);assert.match(source,/releaseMs',10,10000/);
  assert.match(source,/Overwrite on SD/);assert.match(source,/Bass, Melody, and Chord/);
  assert.match(source,/\.sf3\$\/i/);assert.match(source,/application\/vnd\.instachord\.ktsynth/);
  assert.match(source,/From WAV \/ MP3/);assert.match(source,/Detected:/);
  assert.match(source,/pitch could not be detected/);assert.match(source,/pitchConfirmed/);
  assert.match(source,/sf2-volume-heading/);assert.match(source,/Reset to SF2 value/);
  assert.match(source,/defaultGainQ8:gainPercentToQ8\(settings\.volumePercent\)/);
  assert.match(source,/gainQ8:gainPercentToQ8\(layer\.volumePercent\)/);
  assert.match(html,/data-view="synth-view">Synth/);assert.doesNotMatch(source,/if\(region\.unsupported\.length\)throw/);
  assert.match(source,/Synth Sound/);assert.match(source,/Bass, Melody, and Chord/);
  assert.doesNotMatch(source,/external server|within this browser/i);
  assert.match(source,/unsupported features are ignored/i);assert.match(source,/Play on KANTAN Sampler/);
  assert.match(source,/function synthFilePicker/);assert.match(source,/addEventListener\('drop'/);assert.match(source,/WAV or MP3 · max 20 sec/);
  assert.match(source,/hold:true/);assert.match(source,/stopSynthPreview/);assert.match(source,/'Off'/);
  assert.match(firmware,/doc\["hold"\]/);assert.match(firmware,/playSynth\(menu_preview_voice/);assert.match(firmware,/strcmp\(action, "stopSynthPreview"\)/);
  assert.match(source,/\/api\/sampler\/preview-ktsynth/);assert.match(source,/\.web-preview\.ktsynth/);
  assert.match(api,/response_ktsynth_preview/);assert.match(api,/Synth\/\.web-preview\.ktsynth/);
  assert.match(parser,/memcmp\(metadata, "KTS2", 4\)/);assert.match(parser,/"KT2D"/);
  assert.match(parser,/layer_count/);assert.match(parser,/metadata, metadata_bytes, 16, 20/);
  assert.doesNotMatch(source,/KTS1/);assert.doesNotMatch(parser,/KTS1/);
});
