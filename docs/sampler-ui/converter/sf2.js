// Browser-independent SoundFont 2 parser shared with the standalone converter.
const OP={start:0,end:1,startLoop:2,endLoop:3,startCoarse:4,endCoarse:12,attackVolEnv:34,releaseVolEnv:38,instrument:41,keyRange:43,velRange:44,startLoopCoarse:45,keynum:46,velocity:47,attenuation:48,coarseTune:51,fineTune:52,sampleID:53,sampleModes:54,rootKey:58};
const SUPPORTED=new Set([0,1,2,3,4,12,33,34,38,41,43,44,45,46,47,48,51,52,53,54,58]);
const UNSUPPORTED=new Map([[5,'modulation LFO'],[6,'vibrato LFO'],[7,'modulation envelope'],[8,'filter'],[9,'filter Q'],[10,'LFO → filter'],[11,'envelope → filter'],[13,'LFO → volume'],[15,'chorus'],[16,'reverb'],[17,'pan'],[21,'modulation LFO'],[22,'modulation LFO'],[23,'vibrato LFO'],[24,'vibrato LFO'],[25,'modulation envelope'],[26,'modulation envelope'],[27,'modulation envelope'],[28,'modulation envelope'],[29,'modulation envelope'],[30,'modulation envelope'],[35,'hold envelope'],[36,'decay envelope'],[37,'sustain envelope'],[56,'scale tuning'],[57,'exclusive class']]);

function fail(message){throw new Error(`Could not parse SF2: ${message}`);}
function text(dv,offset,length){const bytes=new Uint8Array(dv.buffer,dv.byteOffset+offset,length),zero=bytes.indexOf(0);return new TextDecoder('windows-1252').decode(zero<0?bytes:bytes.subarray(0,zero)).trim();}
function fourCC(dv,offset){return text(dv,offset,4);}
function chunks(dv,start,length){const out=[];let p=start,end=start+length;while(p+8<=end){const id=fourCC(dv,p),size=dv.getUint32(p+4,true),offset=p+8;if(offset+size>end)fail(`${id} chunk extends beyond the file`);out.push({id,offset,size});p=offset+size+(size&1);}return out;}
function list(dv,top,type){const found=top.find(c=>c.id==='LIST'&&c.size>=4&&fourCC(dv,c.offset)===type);if(!found)fail(`LIST ${type} is missing`);return chunks(dv,found.offset+4,found.size-4);}
function required(cs,id){const found=cs.find(c=>c.id===id);if(!found)fail(`${id} chunk is missing`);return found;}
function records(dv,chunk,size,read){if(chunk.size%size)fail(`${chunk.id} chunk has an invalid size`);const out=[];for(let p=chunk.offset;p<chunk.offset+chunk.size;p+=size)out.push(read(p));return out;}
function generators(dv,chunk){return records(dv,chunk,4,p=>{const op=dv.getUint16(p,true),raw=dv.getUint16(p+2,true);return{op,raw,signed:dv.getInt16(p+2,true),lo:raw&255,hi:raw>>>8};});}
function zones(headers,bags,gens,index,terminal){const first=headers[index].bagIndex,last=headers[Math.min(index+1,terminal)].bagIndex,out=[];for(let b=first;b<last;b++){if(!bags[b]||!bags[b+1])fail('Invalid bag index');const entries=gens.slice(bags[b].genIndex,bags[b+1].genIndex);out.push({entries,byOp:new Map(entries.map(g=>[g.op,g]))});}return out;}

export function parseSf2(input){
  const buffer=input instanceof ArrayBuffer?input:input.buffer.slice(input.byteOffset,input.byteOffset+input.byteLength),dv=new DataView(buffer);
  if(dv.byteLength<12||fourCC(dv,0)!=='RIFF'||fourCC(dv,8)!=='sfbk')fail('This is not a SoundFont 2 (RIFF sfbk) file. SF3 is not supported');
  const declared=dv.getUint32(4,true);if(declared+8>dv.byteLength)fail('Invalid RIFF size');
  const top=chunks(dv,12,declared-4),pdta=list(dv,top,'pdta'),sdta=list(dv,top,'sdta'),info=list(dv,top,'INFO');
  const phdr=records(dv,required(pdta,'phdr'),38,p=>({name:text(dv,p,20),preset:dv.getUint16(p+20,true),bank:dv.getUint16(p+22,true),bagIndex:dv.getUint16(p+24,true)}));
  const pbag=records(dv,required(pdta,'pbag'),4,p=>({genIndex:dv.getUint16(p,true),modIndex:dv.getUint16(p+2,true)})),pgen=generators(dv,required(pdta,'pgen'));
  const inst=records(dv,required(pdta,'inst'),22,p=>({name:text(dv,p,20),bagIndex:dv.getUint16(p+20,true)}));
  const ibag=records(dv,required(pdta,'ibag'),4,p=>({genIndex:dv.getUint16(p,true),modIndex:dv.getUint16(p+2,true)})),igen=generators(dv,required(pdta,'igen'));
  const shdr=records(dv,required(pdta,'shdr'),46,p=>({name:text(dv,p,20),start:dv.getUint32(p+20,true),end:dv.getUint32(p+24,true),startLoop:dv.getUint32(p+28,true),endLoop:dv.getUint32(p+32,true),sampleRate:dv.getUint32(p+36,true),originalPitch:dv.getUint8(p+40),pitchCorrection:dv.getInt8(p+41),sampleLink:dv.getUint16(p+42,true),sampleType:dv.getUint16(p+44,true)}));
  const smpl=required(sdta,'smpl');if(smpl.size&1)fail('The smpl chunk has an invalid size');
  const samples=new Int16Array(smpl.size/2);for(let i=0;i<samples.length;i++)samples[i]=dv.getInt16(smpl.offset+i*2,true);
  if(phdr.length<2||inst.length<2||shdr.length<2)fail('Terminal record is missing');
  const presets=phdr.slice(0,-1).map((x,i)=>({...x,index:i,zones:zones(phdr,pbag,pgen,i,phdr.length-1)}));
  const instruments=inst.slice(0,-1).map((x,i)=>({...x,index:i,zones:zones(inst,ibag,igen,i,inst.length-1)}));
  const name=info.find(c=>c.id==='INAM'),version=info.find(c=>c.id==='ifil');
  return{name:name?text(dv,name.offset,name.size):'SoundFont',version:version&&version.size>=4?`${dv.getUint16(version.offset,true)}.${dv.getUint16(version.offset+2,true)}`:'unknown',presets,instruments,sampleHeaders:shdr.slice(0,-1),samples,hasPresetModulators:pbag.some((b,i)=>i+1<pbag.length&&pbag[i+1].modIndex>b.modIndex),hasInstrumentModulators:ibag.some((b,i)=>i+1<ibag.length&&ibag[i+1].modIndex>b.modIndex)};
}

export function listSoundPrograms(sf2){return sf2.presets.map((p,i)=>({index:p.index??i,bank:p.bank,program:p.preset,name:p.name,label:`Bank ${p.bank} / Program ${p.preset+1} - ${p.name}`})).sort((a,b)=>a.bank-b.bank||a.program-b.program||a.index-b.index);}
export function initialSoundProgramIndex(programs){return programs.length===1?programs[0].index:null;}
const range=(z,op)=>{const g=z?.byOp.get(op);return g?[g.lo,g.hi]:[0,127];};
const intersect=(a,b)=>[Math.max(a[0],b[0]),Math.min(a[1],b[1])];
function split(zs,link){const global=zs.length&&!zs[0].byOp.has(link)?zs[0]:null;return{global,locals:zs.slice(global?1:0).filter(z=>z.byOp.has(link))};}
const matching=(zs,key,velocity)=>zs.filter(z=>{const kr=range(z,OP.keyRange),vr=range(z,OP.velRange);return key>=kr[0]&&key<=kr[1]&&velocity>=vr[0]&&velocity<=vr[1];});
const sum=(zs,op)=>zs.reduce((n,z)=>n+(z?.byOp.get(op)?.signed??0),0);
function last(zs,op){for(let i=zs.length-1;i>=0;i--){const g=zs[i]?.byOp.get(op);if(g)return g.raw;}}
function timecents(zs,op){const present=zs.filter(z=>z?.byOp.has(op)),tc=present.length?sum(present,op):-12000;return Math.max(0,1000*Math.pow(2,tc/1200));}
const clamp=(n,min,max)=>Math.max(min,Math.min(max,n));
export function normalizeTuning(root,total){const base=clamp(Math.round(root),0,127),move=clamp(Math.floor((total+50)/100),base-127,base);return{rootNote:base-move,tuneCents:Math.round(total-move*100)};}
export function firmwareEnvelopeMs(ms,type){return type==='attack'?clamp(Math.round(ms),0,5000):clamp(Math.round(ms),10,2000);}

export function resolvePresetRegions(sf2,presetIndex,key=60,velocity=110){
  const preset=sf2.presets[presetIndex];if(!preset)throw new Error('Choose a preset');const p=split(preset.zones,OP.instrument),out=[];
  for(const pz of matching(p.locals,key,velocity)){const instrument=sf2.instruments[pz.byOp.get(OP.instrument).raw];if(!instrument)continue;const izs=split(instrument.zones,OP.sampleID);
    for(const iz of matching(izs.locals,key,velocity)){const zs=[p.global,pz,izs.global,iz].filter(Boolean);let kr=[0,127],vr=[0,127];for(const z of zs){kr=intersect(kr,range(z,OP.keyRange));vr=intersect(vr,range(z,OP.velRange));}if(key<kr[0]||key>kr[1]||velocity<vr[0]||velocity>vr[1])continue;
      const sampleIndex=iz.byOp.get(OP.sampleID).raw,sample=sf2.sampleHeaders[sampleIndex];if(!sample)continue;const start=sample.start+sum(zs,OP.start)+sum(zs,OP.startCoarse)*32768,end=sample.end+sum(zs,OP.end)+sum(zs,OP.endCoarse)*32768,loopStart=sample.startLoop+sum(zs,OP.startLoop)+sum(zs,OP.startLoopCoarse)*32768,loopEnd=sample.endLoop+sum(zs,OP.endLoop),modes=last(zs,OP.sampleModes)??0;
      const unsupported=[...new Set(zs.flatMap(z=>z.entries.filter(g=>!SUPPORTED.has(g.op)&&g.op!==60).map(g=>UNSUPPORTED.get(g.op)||`generator ${g.op}`)))];if(sample.sampleType&0x8000)unsupported.push('ROM sample');
      const forcedKey=last(zs,OP.keynum),root=last(zs,OP.rootKey),sourceRoot=root!==undefined&&root<=127?root:(sample.originalPitch<=127?sample.originalPitch:60),tuning=normalizeTuning(sourceRoot,sample.pitchCorrection+sum(zs,OP.coarseTune)*100+sum(zs,OP.fineTune));
      out.push({id:`${sampleIndex}:${start}:${end}:${kr}:${vr}`,presetName:preset.name,instrumentName:instrument.name,sampleName:sample.name,sampleIndex,keyRange:kr,velRange:vr,sampleRate:sample.sampleRate,rootNote:tuning.rootNote,previewNote:forcedKey!==undefined&&forcedKey<=127?forcedKey:key,tuneCents:tuning.tuneCents,start,end,loopStart,loopEnd,sustainMode:(modes&1)?'loop':'oneShot',attackMs:firmwareEnvelopeMs(timecents(zs,OP.attackVolEnv),'attack'),releaseMs:firmwareEnvelopeMs(timecents(zs,OP.releaseVolEnv),'release'),initialAttenuationCb:Math.max(0,sum(zs,OP.attenuation)),sampleType:sample.sampleType,sampleLink:sample.sampleLink,unsupported});
    }
  }return out;
}

function linkedPcm(sf2,region,header){const base=sf2.sampleHeaders[region.sampleIndex],relativeStart=region.start-base.start,relativeEnd=region.end-base.start,start=Math.max(header.start,header.start+relativeStart),end=Math.min(header.end,header.start+relativeEnd);return sf2.samples.slice(start,Math.max(start,end));}
export function extractRegionPcm(sf2,region){if(region.start<0||region.end>sf2.samples.length||region.end<=region.start)throw new Error('Invalid sample start or end position');let pcm=sf2.samples.slice(region.start,region.end);const base=sf2.sampleHeaders[region.sampleIndex],type=base.sampleType&0x7fff;let stereo=false;if((type===2||type===4||type===8)&&sf2.sampleHeaders[base.sampleLink]){const linked=linkedPcm(sf2,region,sf2.sampleHeaders[base.sampleLink]),count=Math.min(pcm.length,linked.length),mono=new Int16Array(count);for(let i=0;i<count;i++)mono[i]=clamp(Math.round((pcm[i]+linked[i])/2),-32768,32767);pcm=mono;stereo=true;}return{pcm,loopStart:clamp(region.loopStart-region.start,0,pcm.length),loopEnd:clamp(region.loopEnd-region.start,0,pcm.length),stereo};}
