export const KTSYNTH_MAX_BYTES=2*1024*1024;
export const KTSYNTH_MAX_SECONDS=20;
export const KTSYNTH_MAX_LAYERS=2;
const HEADER_BYTES=128,DESCRIPTOR_BYTES=48,DESCRIPTOR_OFFSET=24;
export function gainPercentToQ8(percent){return Math.max(0,Math.min(512,Math.round((Number(percent)||0)*256/100)));}
export function attenuationCbToGainPercent(centibels){return Math.max(0,Math.min(200,Math.round(Math.pow(10,-Math.max(0,Number(centibels)||0)/200)*100)));}
const ascii=(dv,p,s)=>{for(let i=0;i<s.length;i++)dv.setUint8(p+i,s.charCodeAt(i));};
const fourCC=(dv,p)=>String.fromCharCode(dv.getUint8(p),dv.getUint8(p+1),dv.getUint8(p+2),dv.getUint8(p+3));
const check=(ok,message)=>{if(!ok)throw new Error(`KTSYNTH validation error: ${message}`);};
let crcTable;
function table(){if(crcTable)return crcTable;crcTable=new Uint32Array(256);for(let n=0;n<256;n++){let c=n;for(let i=0;i<8;i++)c=(c&1)?0xedb88320^(c>>>1):c>>>1;crcTable[n]=c>>>0;}return crcTable;}
export function crc32IsoHdlc(...parts){let crc=0xffffffff,t=table();for(const part of parts)for(const byte of part)crc=t[(crc^byte)&255]^(crc>>>8);return(crc^0xffffffff)>>>0;}
export function utf8Name(name){const encoder=new TextEncoder();let value='';for(const char of String(name)){const next=value+char;if(encoder.encode(next).length>63)break;value=next;}const bytes=encoder.encode(value);check(bytes.length>0,'Sound name cannot be empty');return{value,bytes};}
const padded=n=>n+(n&1);
const chunkBytes=n=>8+padded(n);
export function estimateKtSynthBytes(name,layers){
  const layerList=Array.isArray(layers)?layers:[{frames:Number(layers)||0,looped:false}];
  check(layerList.length>=1&&layerList.length<=KTSYNTH_MAX_LAYERS,'One or two layers are required');
  const metadata=HEADER_BYTES+utf8Name(name).bytes.length;
  const smpl=layerList[0]&&(layerList[0].looped||layerList[0].sustainMode===1||layerList[0].sustainMode==='loop');
  return 12+chunkBytes(16)+(smpl?chunkBytes(60):0)+chunkBytes(metadata)
    +layerList.reduce((sum,layer)=>sum+chunkBytes(Math.max(0,Math.floor(layer.frames??layer.frameCount??layer.pcm?.length??0))*2),0);
}
function normalizedLayer(layer,index){
  check(layer&&layer.pcm instanceof Int16Array,`Layer ${index+1} PCM must be an Int16Array`);
  const looped=layer.sustainMode==='loop'||layer.sustainMode===1;
  return {...layer,frameCount:layer.pcm.length,startFrame:layer.startFrame??0,endFrameExclusive:layer.endFrameExclusive??layer.pcm.length,
    loopStartFrame:looped?(layer.loopStartFrame??0):0,loopEndFrameExclusive:looped?(layer.loopEndFrameExclusive??0):0,
    loopCrossfadeFrames:looped?(layer.loopCrossfadeFrames??0):0,sustainMode:looped?1:0,pcmChunk:index};
}
function validateLayer(meta,dataBytes,index){
  check(Number.isInteger(meta.sampleRate)&&meta.sampleRate>=8000&&meta.sampleRate<=48000,`Layer ${index+1} sample rate must be between 8000 and 48000 Hz`);
  check(dataBytes%2===0,`Layer ${index+1} has an invalid PCM16 data length`);
  check(meta.frameCount===dataBytes/2,`Layer ${index+1} frameCount does not match its PCM data`);
  check(meta.frameCount/meta.sampleRate<=KTSYNTH_MAX_SECONDS,`Layer ${index+1} duration exceeds 20 seconds`);
  check(meta.startFrame>=0&&meta.startFrame<meta.endFrameExclusive&&meta.endFrameExclusive<=meta.frameCount,`Layer ${index+1} has an invalid start or end range`);
  check(Number.isInteger(meta.rootNote)&&meta.rootNote>=0&&meta.rootNote<=127,`Layer ${index+1} root note is out of range`);
  check(Number.isInteger(meta.tuneCents)&&meta.tuneCents>=-100&&meta.tuneCents<=100,`Layer ${index+1} pitch correction must be between -100 and 100 cents`);
  check(Number.isInteger(meta.attackMs)&&meta.attackMs>=0&&meta.attackMs<=5000,`Layer ${index+1} attack must be between 0 and 5000 ms`);
  check(Number.isInteger(meta.releaseMs)&&meta.releaseMs>=10&&meta.releaseMs<=2000,`Layer ${index+1} release must be between 10 and 2000 ms`);
  check(Number.isInteger(meta.defaultGainQ8)&&meta.defaultGainQ8>=0&&meta.defaultGainQ8<=512,`Layer ${index+1} volume is out of range`);
  check(meta.sustainMode===0||meta.sustainMode===1,`Layer ${index+1} has an invalid sustain mode`);
  check(meta.pcmChunk===index,`Layer ${index+1} references the wrong PCM chunk`);
  if(meta.sustainMode===1)check(meta.startFrame<=meta.loopStartFrame&&meta.loopStartFrame<meta.loopEndFrameExclusive&&meta.loopEndFrameExclusive<=meta.endFrameExclusive&&meta.loopCrossfadeFrames>=0&&meta.loopCrossfadeFrames<=Math.min(65535,Math.floor((meta.loopEndFrameExclusive-meta.loopStartFrame)/4)),`Layer ${index+1} has an invalid loop or crossfade`);
  else check(!meta.loopStartFrame&&!meta.loopEndFrameExclusive&&!meta.loopCrossfadeFrames,`Layer ${index+1} loop values must be zero when looping is off`);
}
export function validateKtSynthMetadata(meta,dataBytes,fileBytes=0){
  const layers=meta.layers||[meta],sizes=Array.isArray(dataBytes)?dataBytes:[dataBytes];
  check(layers.length>=1&&layers.length<=KTSYNTH_MAX_LAYERS,'layerCount must be 1 or 2');
  check(sizes.length===layers.length,'layerCount does not match the PCM chunks');
  layers.forEach((layer,index)=>validateLayer(layer,sizes[index],index));
  check(layers.reduce((sum,layer)=>sum+layer.defaultGainQ8,0)<=512,'Combined layer volume exceeds 200%');
  if(fileBytes)check(fileBytes<=KTSYNTH_MAX_BYTES,'Output exceeds 2 MiB. Reduce a sample rate or choose shorter sounds');
}
function writeFmt(dv,p,rate){ascii(dv,p,'fmt ');dv.setUint32(p+4,16,true);dv.setUint16(p+8,1,true);dv.setUint16(p+10,1,true);dv.setUint32(p+12,rate,true);dv.setUint32(p+16,rate*2,true);dv.setUint16(p+20,2,true);dv.setUint16(p+22,16,true);}
function writeSmpl(dv,p,m){ascii(dv,p,'smpl');dv.setUint32(p+4,60,true);dv.setUint32(p+16,Math.round(1e9/m.sampleRate),true);dv.setUint32(p+20,m.rootNote,true);dv.setUint32(p+36,1,true);dv.setUint32(p+52,m.loopStartFrame,true);dv.setUint32(p+56,m.loopEndFrameExclusive-1,true);}
function writeDescriptor(dv,p,m){dv.setUint32(p,m.sampleRate,true);dv.setUint32(p+4,m.frameCount,true);dv.setUint32(p+8,m.startFrame,true);dv.setUint32(p+12,m.endFrameExclusive,true);dv.setUint32(p+16,m.loopStartFrame,true);dv.setUint32(p+20,m.loopEndFrameExclusive,true);dv.setUint32(p+24,m.loopCrossfadeFrames,true);dv.setUint16(p+28,m.attackMs,true);dv.setUint16(p+30,m.releaseMs,true);dv.setInt16(p+32,m.tuneCents,true);dv.setUint16(p+34,m.defaultGainQ8,true);dv.setUint8(p+36,m.rootNote);dv.setUint8(p+37,m.sustainMode);dv.setUint8(p+38,m.pcmChunk);}
function writePcm(out,dv,p,id,pcm){ascii(dv,p,id);dv.setUint32(p+4,pcm.length*2,true);for(let i=0;i<pcm.length;i++)dv.setInt16(p+8+i*2,pcm[i],true);return p+chunkBytes(pcm.length*2);}
export function encodeKtSynth(layerInputs,input,{includeSmpl=true}={}){
  const layers=(Array.isArray(layerInputs)?layerInputs:[layerInputs]).map(normalizedLayer);
  const named=utf8Name(input.name),smpl=includeSmpl&&layers[0].sustainMode===1,total=estimateKtSynthBytes(named.value,layers);
  validateKtSynthMetadata({layers},layers.map(layer=>layer.pcm.length*2),total);
  const out=new Uint8Array(total),dv=new DataView(out.buffer);ascii(dv,0,'RIFF');dv.setUint32(4,total-8,true);ascii(dv,8,'WAVE');let p=12;
  writeFmt(dv,p,layers[0].sampleRate);p+=chunkBytes(16);if(smpl){writeSmpl(dv,p,layers[0]);p+=chunkBytes(60);}
  const payload=HEADER_BYTES+named.bytes.length;ascii(dv,p,'KNTN');dv.setUint32(p+4,payload,true);const k=p+8;ascii(dv,k,'KTS2');dv.setUint16(k+4,2,true);dv.setUint16(k+6,0,true);dv.setUint16(k+8,HEADER_BYTES,true);dv.setUint16(k+10,named.bytes.length,true);dv.setUint8(k+20,layers.length);layers.forEach((layer,index)=>writeDescriptor(dv,k+DESCRIPTOR_OFFSET+index*DESCRIPTOR_BYTES,layer));out.set(named.bytes,k+HEADER_BYTES);p+=chunkBytes(payload);
  const pcmParts=[],dataBody=p+8;p=writePcm(out,dv,p,'data',layers[0].pcm);pcmParts.push(out.subarray(dataBody,dataBody+layers[0].pcm.byteLength));
  if(layers.length===2){const layer2Body=p+8;p=writePcm(out,dv,p,'KT2D',layers[1].pcm);pcmParts.push(out.subarray(layer2Body,layer2Body+layers[1].pcm.byteLength));}
  check(p===total,'Internal RIFF size mismatch');const metadata=out.slice(k,k+payload);dv.setUint32(k+16,crc32IsoHdlc(metadata,...pcmParts),true);return out;
}
function readDescriptor(dv,p){return{sampleRate:dv.getUint32(p,true),frameCount:dv.getUint32(p+4,true),startFrame:dv.getUint32(p+8,true),endFrameExclusive:dv.getUint32(p+12,true),loopStartFrame:dv.getUint32(p+16,true),loopEndFrameExclusive:dv.getUint32(p+20,true),loopCrossfadeFrames:dv.getUint32(p+24,true),attackMs:dv.getUint16(p+28,true),releaseMs:dv.getUint16(p+30,true),tuneCents:dv.getInt16(p+32,true),defaultGainQ8:dv.getUint16(p+34,true),rootNote:dv.getUint8(p+36),sustainMode:dv.getUint8(p+37),pcmChunk:dv.getUint8(p+38)};}
export function parseKtSynth(input){
  const bytes=input instanceof Uint8Array?input:new Uint8Array(input),dv=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);check(bytes.length<=KTSYNTH_MAX_BYTES,'File exceeds 2 MiB');check(bytes.length>=12&&fourCC(dv,0)==='RIFF'&&fourCC(dv,8)==='WAVE','Not a RIFF/WAVE file');check(dv.getUint32(4,true)+8===bytes.length,'RIFF size does not match the file');
  const cs=new Map();let p=12;while(p+8<=bytes.length){const id=fourCC(dv,p),size=dv.getUint32(p+4,true),offset=p+8;check(offset+size<=bytes.length,`${id} chunk is out of range`);if(['fmt ','KNTN','data','KT2D'].includes(id))check(!cs.has(id),`${id} chunk is duplicated`);cs.set(id,{offset,size});p=offset+size+(size&1);}check(p===bytes.length,'Invalid RIFF padding');
  const fmt=cs.get('fmt '),kntn=cs.get('KNTN'),data=cs.get('data'),kt2d=cs.get('KT2D');check(fmt&&kntn&&data,'fmt, KNTN, and data chunks are required');check(fmt.size>=16&&dv.getUint16(fmt.offset,true)===1&&dv.getUint16(fmt.offset+2,true)===1&&dv.getUint16(fmt.offset+12,true)===2&&dv.getUint16(fmt.offset+14,true)===16,'Audio must be mono PCM16');
  const k=kntn.offset;check(kntn.size>=HEADER_BYTES&&fourCC(dv,k)==='KTS2','Invalid KTS2 signature');check(dv.getUint16(k+4,true)===2,'Unsupported KNTN major version');const header=dv.getUint16(k+8,true),nameBytes=dv.getUint16(k+10,true),layerCount=dv.getUint8(k+20);check(header>=HEADER_BYTES&&header<=kntn.size&&nameBytes<=63&&header+nameBytes<=kntn.size,'Invalid KNTN header or name');check(layerCount===1||layerCount===2,'layerCount must be 1 or 2');check((layerCount===2)===Boolean(kt2d),'layerCount does not match KT2D');
  const chunks=[data,kt2d].slice(0,layerCount),layers=chunks.map((chunk,index)=>{const meta=readDescriptor(dv,k+DESCRIPTOR_OFFSET+index*DESCRIPTOR_BYTES);const pcm=new Int16Array(chunk.size/2);for(let i=0;i<pcm.length;i++)pcm[i]=dv.getInt16(chunk.offset+i*2,true);return{...meta,pcm};});
  check(layers[0].sampleRate===dv.getUint32(fmt.offset+4,true)&&layers[0].frameCount===data.size/2,'Layer 1 does not match fmt/data');validateKtSynthMetadata({layers},chunks.map(chunk=>chunk.size),bytes.length);
  const crcPayload=bytes.slice(k,k+kntn.size);new DataView(crcPayload.buffer).setUint32(16,0,true);check(crc32IsoHdlc(crcPayload,...chunks.map(chunk=>bytes.subarray(chunk.offset,chunk.offset+chunk.size)))===dv.getUint32(k+16,true),'CRC does not match');
  const smpl=cs.get('smpl');if(smpl&&layers[0].sustainMode===1){check(smpl.size>=60&&dv.getUint32(smpl.offset+28,true)>=1,'smpl loop is missing');check(dv.getUint32(smpl.offset+44,true)===layers[0].loopStartFrame&&dv.getUint32(smpl.offset+48,true)===layers[0].loopEndFrameExclusive-1,'smpl and KNTN loop ranges do not match');}
  const name=new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(k+header,k+header+nameBytes));return{name,layers,metadata:{name,layerCount,layers,contentCrc32:dv.getUint32(k+16,true)},verified:true};
}
