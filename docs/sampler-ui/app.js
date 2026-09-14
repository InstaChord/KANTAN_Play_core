import { parseSf2, listSoundPrograms, initialSoundProgramIndex, resolvePresetRegions, extractRegionPcm } from './converter/sf2.js?v=094-kts2-v21';
import { resamplePcm, trimLoopTail, PreviewPlayer } from './converter/audio.js?v=094-kts2-v21';
import { encodeKtSynth, parseKtSynth, estimateKtSynthBytes, gainPercentToQ8, attenuationCbToGainPercent, KTSYNTH_MAX_BYTES } from './converter/ktsynth.js?v=094-kts2-v21';
import { decodeAudioFile, midiNoteName } from './converter/audio-input.js';

(() => {
  const PREVIEW = !(window.KANPLAY && window.KANPLAY.api) || new URLSearchParams(location.search).has('demo');
  const API = PREVIEW ? '' : window.KANPLAY.api;
  const $ = (s, root = document) => root.querySelector(s);
  const el = (tag, attrs = {}, ...children) => {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') node.className = v;
      else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
      else if (v !== undefined && v !== null) node.setAttribute(k, v);
    }
    node.append(...children.flat().filter(v => v !== null && v !== undefined && v !== false)
      .map(v => typeof v === 'string' ? document.createTextNode(v) : v));
    return node;
  };
  let state = null;
  let selectedPad = 0;
  let files = { samples: [], loops: [], kits: [], projects: [], music: [] };
  let folders = { samples: [], loops: [], kits: [], projects: [], music: [] };
  const DEVICE_PRESET = '@device-preset';
  let browseFolders = { samples:DEVICE_PRESET, loops:'', kits:'', projects:'', music:'' };
  let loopEventsDraft = null;
  const sf2Player = new PreviewPlayer();
  const newSf2Layer = () => ({regionId:null,pcmMode:'own',sampleRate:32000,crossfadeMs:10,delayMs:null,attackMs:null,holdMs:null,decayMs:null,sustainPercent:null,releaseMs:null,tuneOffset:0,volumePercent:100,volumeCustomized:false});
  const newAudioLayer = () => ({file:null,audio:null,pitchSuggestion:null,pitchNote:60,pitchConfirmed:false,sampleRate:32000,tuneOffset:0,volumePercent:100,delayMs:0,attackMs:0,holdMs:0,decayMs:0,sustainPercent:100,releaseMs:120});
  const newSf2Editor = () => ({open:false,method:null,file:null,sf2:null,programs:[],programIndex:null,
    regions:[],sf2Layers:[newSf2Layer()],audio:newAudioLayer(),ktsLayers:[],key:60,velocity:110,
    name:'',error:'',busy:false,output:null,savedPath:'',overwrite:false});
  let sf2Editor = newSf2Editor();

  function previewWave(seed) {
    return Array.from({ length:96 }, (_, i) => {
      const envelope = Math.max(0.08, 1 - i / 110);
      const a = Math.sin((i + seed * 7) * (0.31 + seed * 0.013)) * envelope;
      const b = Math.sin((i + seed * 11) * (0.57 + seed * 0.009)) * envelope * .6;
      const peak = Math.round(Math.min(1, Math.abs(a + b)) * 27000);
      return [-peak, peak];
    });
  }
  function previewPad(index, name) {
    const active = Boolean(name);
    const frames = active ? 48000 : 0;
    return { pad:index, label:index + 1, name:name || '', frames, sampleRate:48000,
      start:active ? 160 : 0, end:active ? frames - 480 : 0, volume:256, pitch:256,
      reverse:false, hold:false, loop:false, wave:active ? previewWave(index + 1) : [] };
  }
  function createPreviewState() {
    const names = ['KICK 808', 'SNARE', 'CLAP', 'HAT', 'PIKO', 'COWBELL', 'CHIN', 'TOM', '', '', '', ''];
    return {
      pads:names.map((name, index) => previewPad(index, name)),
      builtinSamples:names.slice(0,8).map((name, index) => ({
        name,file:'builtin:'+name,
        category:['Kick','Snare','Percussion','HiHat','FX','Percussion','Cymbal','Tom'][index]
      })),
      builtinBackgrounds:[],
      builtinBeatPatterns:[
        {name:'POP',file:'pattern:POP'},{name:'ROCK',file:'pattern:ROCK'},
        {name:'HOUSE',file:'pattern:HOUSE'},{name:'HIP HOP',file:'pattern:HIP HOP'},
        {name:'DISCO',file:'pattern:DISCO'},{name:'BREAK',file:'pattern:BREAK'}
      ],
      beat:{format:'pattern',name:'HOUSE PATTERN',volume:100,drumKit:'dance'},
      loop:{ lengthMs:4000, lengthFixed:true, quantize:true, noteGridIndex:4, noteOffGridIndex:4,
        background:{ file:'', name:'', frames:0, sampleRate:48000, volume:208 },
        events:[
          {pad:0,pos:0,type:'on',layer:0,velocity:127}, {pad:3,pos:500,type:'on',layer:0,velocity:80},
          {pad:1,pos:1000,type:'on',layer:0,velocity:110}, {pad:3,pos:1500,type:'on',layer:0,velocity:80},
          {pad:0,pos:2000,type:'on',layer:0,velocity:110}, {pad:4,pos:2250,type:'on',layer:0,velocity:50},
          {pad:1,pos:3000,type:'on',layer:0,velocity:110}, {pad:3,pos:3500,type:'on',layer:0,velocity:80}
        ] },
      folders:{ samples:'/sampler/samples', loops:'/sampler/loops', kits:'/sampler/kits', projects:'/sampler/projects', music:'/sampler/music' },
      project:{file:''}, commandRevision:0
    };
  }
  const previewState = createPreviewState();
  const previewFiles = {
    samples:[
      {name:'Vocal Hit.wav',size:20032,folder:''},
      {name:'Kick.wav',size:44482,folder:'Drum'}, {name:'Snare.wav',size:8620,folder:'Drum'},
      {name:'Song Intro.wav',size:80896,folder:'Song1'}
    ],
    loops:[{name:'night-drive.wav',size:704000}],
    kits:[{name:'Starter Beat.ktkit',size:184320}, {name:'Pentatonic Jam.ktkit',size:233472}],
    projects:[{name:'First Jam.json',size:8420}, {name:'Night Session.json',size:9172}],
    music:[{name:'Demo Track.mp3',size:3840000}]
  };
  const previewFolders = {
    samples:['Drum', 'Song1', 'Song1/Vocal'], loops:['Practice'], kits:['Favorites'],
    projects:['Ideas','Live Sets'], music:['DJ Sets']
  };

  function rootFolder(kind) { return '/sampler/' + kind; }
  function audioPath(kind, value) {
    return value && (value.startsWith('builtin:') || value.startsWith('pattern:'))
      ? value : state.folders[kind] + '/' + value;
  }
  function builtinFiles(kind) {
    const source = kind === 'samples' ? state.builtinSamples
      : kind === 'loops' ? [...(state.builtinBeatPatterns || []), ...(state.builtinBackgrounds || [])] : [];
    return (source || []).map(item => ({...item, builtin:true}));
  }
  function relativeFolder(kind, full = state && state.folders && state.folders[kind]) {
    const root = rootFolder(kind);
    return full && full.startsWith(root + '/') ? full.slice(root.length + 1) : '';
  }
  function activeFolder(kind) {
    return browseFolders[kind];
  }
  function browserFilePath(kind, name) {
    const relative = activeFolder(kind);
    return rootFolder(kind) + '/' + (relative ? relative + '/' : '') + name;
  }
  async function listFiles(kind) {
    if (kind === 'samples' && activeFolder(kind) === DEVICE_PRESET) return {files:[]};
    const path = activeFolder(kind);
    return request('/api/sampler/files/' + kind + (path ? '?path=' + encodeURIComponent(path) : '')).then(r => r.json());
  }
  async function listFolders(kind, relative = activeFolder(kind)) {
    const path = relative ? '?path=' + encodeURIComponent(relative) : '';
    return request('/api/sampler/folders/' + kind + path).then(r => r.json());
  }
  async function listFolderTree(kind) {
    const result = [];
    const queue = [''];
    while (queue.length && result.length < 128) {
      const parent = queue.shift();
      const response = await listFolders(kind, parent);
      for (const name of response.folders || []) {
        const path = parent ? parent + '/' + name : name;
        result.push(path);
        queue.push(path);
        if (result.length >= 128) break;
      }
    }
    return {folders:result.sort((a,b) => a.localeCompare(b, undefined, {numeric:true}))};
  }

  async function request(path, options = {}) {
    const res = await fetch(API + path, options);
    if (!res.ok) { let msg = res.statusText; try { msg = (await res.json()).error || msg; } catch (_) {} throw new Error(msg); }
    return res;
  }
  function status(text, error = false) { const n = $('#status'); n.textContent = text; n.style.color = error ? 'var(--danger)' : ''; }
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function waitForCommandApplied(payload) {
    if (['saveProject','loadProject','newProject','projectRenamed','kitRenamed','documentDeleted'].includes(payload.action)) {
      const revision = Number(state && state.commandRevision || 0);
      const until = Date.now() + 20000;
      while (Date.now() < until) {
        const next = await request('/api/sampler/state').then(r => r.json());
        if (Number(next.commandRevision || 0) !== revision) { state = next; return true; }
        await sleep(150);
      }
      throw new Error('project operation timed out');
    }
    if (!['assignSample', 'clearPad'].includes(payload.action)) {
      await sleep(180);
      return false;
    }
    const until = Date.now() + 4000;
    const matches = next => {
      const pad = next.pads && next.pads.find(p => p.pad === payload.pad);
      if (payload.action === 'assignSample') {
        return pad && pad.frames > 0 && (pad.file === payload.file || pad.name === previewFileName(payload.file));
      }
      if (payload.action === 'clearPad') return pad && !pad.frames;
      return true;
    };
    while (Date.now() < until) {
      const next = await request('/api/sampler/state').then(r => r.json());
      if (matches(next)) { state = next; return true; }
      await sleep(120);
    }
    return false;
  }
  async function command(payload, refreshAfter = true) {
    if (PREVIEW) {
      applyPreviewCommand(payload);
      loopEventsDraft = null;
      if (refreshAfter) render();
      status('Preview mode');
      return;
    }
    status('Applying…');
    await request('/api/sampler/command', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload) });
    if (!refreshAfter) {
      status(payload.action === 'previewWav' ? 'Previewing…' : 'Playing…');
      return;
    }
    await waitForCommandApplied(payload);
    await refresh();
  }
  async function refresh() {
    if (PREVIEW) {
      state = previewState;
      files = Object.fromEntries(Object.entries(previewFiles).map(([kind, entries]) => [
        kind,
        kind === 'samples' && activeFolder(kind) === DEVICE_PRESET
          ? [] : entries.filter(file => (file.folder || '') === activeFolder(kind))
      ]));
      folders = previewFolders;
      loopEventsDraft = null;
      if (!state.pads.some(p => p.pad === selectedPad)) selectedPad = 0;
      render(); status('Preview mode');
      return;
    }
    try {
      state = await request('/api/sampler/state').then(r => r.json());
      const projectApi = Boolean(state.folders && state.folders.projects && $('#project-view'));
      const musicApi = Boolean(state.folders && state.folders.music && $('#music-view'));
      const results = await Promise.allSettled([
        listFiles('samples'), listFiles('loops'), listFiles('kits'),
        projectApi ? listFiles('projects') : Promise.resolve({files:[]}),
        musicApi ? listFiles('music') : Promise.resolve({files:[]}),
        listFolderTree('samples'), listFolderTree('loops'), listFolderTree('kits'),
        projectApi ? listFolderTree('projects') : Promise.resolve({folders:[]}),
        musicApi ? listFolderTree('music') : Promise.resolve({folders:[]})
      ]);
      const value = (index, fallback) => results[index].status === 'fulfilled' ? results[index].value : fallback;
      files = {
        samples:value(0,{files:[]}).files || [],
        loops:value(1,{files:[]}).files || [],
        kits:value(2,{files:[]}).files || [],
        projects:value(3,{files:[]}).files || [],
        music:value(4,{files:[]}).files || []
      };
      folders = {
        samples:value(5,{folders:[]}).folders || [],
        loops:value(6,{folders:[]}).folders || [],
        kits:value(7,{folders:[]}).folders || [],
        projects:value(8,{folders:[]}).folders || [],
        music:value(9,{folders:[]}).folders || []
      };
      loopEventsDraft = null;
      if (!state.pads.some(p => p.pad === selectedPad)) selectedPad = 0;
      render(); status(results.some(result => result.status === 'rejected') ? 'Connected / SD unavailable' : 'Connected');
    } catch (err) { status('Connection error: ' + err.message, true); }
  }
  function previewFileName(path) { return (path || '').split('/').pop().replace(/\.(wav|mp3)$/i, ''); }
  function applyPreviewCommand(payload) {
    const pad = previewState.pads.find(p => p.pad === payload.pad);
    if (payload.action === 'setPad' && pad) Object.assign(pad, payload);
    if (payload.action === 'clearPad' && pad) Object.assign(pad, previewPad(pad.pad, ''));
    if (payload.action === 'assignSample' && pad) {
      Object.assign(pad, previewPad(pad.pad, previewFileName(payload.file)));
    }
    if (payload.action === 'loadBeat' || payload.action === 'loadBgm') {
      if (String(payload.file).startsWith('pattern:')) {
        previewState.beat = {format:'pattern',name:String(payload.file).slice(8),volume:previewState.beat.volume};
        previewState.loop.background = { file:'', name:'', frames:0, sampleRate:48000, volume:208 };
        return;
      }
      const name = previewFileName(payload.file);
      previewState.beat = {format:'audio',name,volume:previewState.beat.volume};
      previewState.loop.background = { file:payload.file, name, frames:192000, sampleRate:48000, volume:208 };
    }
    if (payload.action === 'newBeatPattern') previewState.beat = {format:'pattern',name:'NEW PATTERN',volume:previewState.beat.volume};
    if (payload.action === 'clearBeat' || payload.action === 'clearBgm') {
      previewState.beat = {format:'none',name:'',volume:previewState.beat.volume};
      previewState.loop.background = { file:'', name:'', frames:0, sampleRate:48000, volume:208 };
    }
    if (payload.action === 'setLoop') {
      const patch = {...payload}; delete patch.action;
      if (patch.backgroundVolume !== undefined) {
        previewState.loop.background.volume = patch.backgroundVolume;
        delete patch.backgroundVolume;
      }
      if (patch.beatVolume !== undefined) {
        previewState.beat.volume = patch.beatVolume;
        delete patch.beatVolume;
      }
      Object.assign(previewState.loop, patch);
    }
    if (payload.action === 'setEvents') previewState.loop.events = payload.events.map(event => ({...event}));
    if (payload.action === 'saveKit') {
      const name = payload.file.split('/').pop();
      if (!previewFiles.kits.some(file => file.name === name)) previewFiles.kits.push({name, size:2200});
    }
    if (payload.action === 'saveProject') {
      const name = payload.file.split('/').pop();
      if (!previewFiles.projects.some(file => file.name === name)) previewFiles.projects.push({name,size:8600});
      previewState.project.file = payload.file;
    }
    if (payload.action === 'loadProject') previewState.project.file = payload.file;
    if (payload.action === 'projectRenamed' && previewState.project.file === payload.old) previewState.project.file = payload.file;
    if (payload.action === 'documentDeleted' && payload.kind === 'projects' && previewState.project.file === payload.file) previewState.project.file = '';
    if (payload.action === 'newProject') previewState.project.file = '';
    previewState.commandRevision++;
    if (payload.action === 'setFolder' && previewState.folders[payload.kind] !== undefined) {
      previewState.folders[payload.kind] = payload.path;
    }
  }
  function fileDisplayLabel(kind, file) {
    const name = String(file.name || file.file || '');
    return kind === 'kits' ? name.replace(/\.(ktkit|json)$/i,'') : name;
  }
  function padCard(pad) {
    return el('button', {
      class:'pad' + (pad.pad === selectedPad ? ' selected' : '') + (!pad.frames ? ' empty' : ''),
      onclick:() => { selectedPad = pad.pad; renderSamples(); }
    }, el('strong', {}, 'P' + pad.label), el('small', {}, pad.name || 'Empty'));
  }
  function assignmentPanel() {
    const pad = state.pads.find(item => item.pad === selectedPad) || state.pads[0];
    const panel = el('div', {class:'panel assignment-panel'},
      el('h2', {}, 'Assignment target'),
      el('div', {class:'pad-grid compact'}, [...state.pads].sort((a,b) => a.pad - b.pad).map(padCard)),
      el('p', {class:'assignment-current'}, 'Selected: Pad ' + pad.label + ' · ' + (pad.name || 'Empty')));
    panel.append(el('div', {class:'actions'},
      el('button', {onclick:async()=>await command({action:'playPad',pad:pad.pad},false),disabled:pad.frames?null:''}, 'Play Pad'),
      el('button', {class:'danger',onclick:async()=>await command({action:'clearPad',pad:pad.pad})}, 'Clear assignment')));
    return panel;
  }
  function presetFilePanel() {
    const list = el('ul',{class:'file-list'});
    for (const file of builtinFiles('samples')) {
      const details = file.category ? el('small',{},file.category) : null;
      const play = el('button',{title:'Preview file',onclick:async()=>await command({action:'previewWav',file:file.file,maxMs:1000},false)},'Play');
      const assign = el('button',{class:'primary',onclick:async()=>await command({action:'assignSample',pad:selectedPad,file:file.file})},'Assign');
      list.append(el('li',{},el('span',{class:'name'},fileDisplayLabel('samples',file)),details,play,assign));
    }
    return list;
  }
  function renderSamples() {
    const root = $('#sample-view'); root.innerHTML = '';
    const library = el('div', {class:'panel'}, el('h2', {}, 'Sample source'), folderPanel('samples'));
    library.append(activeFolder('samples') === DEVICE_PRESET
      ? presetFilePanel() : filePanel('samples', '.wav,.mp3,.ktsynth', true));
    root.append(assignmentPanel(), library);
    renderSynth();
  }
  function renderSynth() {
    const root = $('#synth-view');
    if (!root) return;
    root.innerHTML = '';
    sf2Editor.open = true;
    root.append(synthConverterPanel());
  }
  const safeSynthName = value => {
    const clean=String(value||'').trim().replace(/[\\/:*?"<>|]/g,'_').replace(/^\.+/,''),encoder=new TextEncoder();let result='';
    for(const character of clean){if(encoder.encode(result+character).length>63)break;result+=character;}
    return result;
  };
  const sf2Field = (label,control,hint='') => el('label',{class:'sf2-field'},el('span',{},label),control,hint?el('small',{},hint):null);
  const selectedSf2Layers = () => sf2Editor.sf2Layers.filter(layer=>layer.regionId).map(layer=>({settings:layer,region:sf2Editor.regions.find(region=>region.id===layer.regionId)})).filter(layer=>layer.region);
  const totalLayerGain = layers => layers.reduce((sum,layer)=>sum+gainPercentToQ8(layer.volumePercent),0);
  const percentLabel = value => `${Math.round(Number(value)*10)/10}%`;
  function invalidateSynth(){sf2Editor.output=null;sf2Editor.overwrite=false;}
  function numberControl(target,key,min,max,label,hint='',step=1){
    const input=el('input',{type:'number',value:target[key]===null?'':target[key],min,max,step,placeholder:target[key]===null?'SoundFont value':''});
    input.addEventListener('change',()=>{target[key]=input.value===''?null:Math.max(min,Math.min(max,Number(input.value)));if(target===sf2Editor&&(key==='key'||key==='velocity'))updateSf2Regions(true);else invalidateSynth();renderSamples();});
    return sf2Field(label,input,hint);
  }
  function rateControl(target,label='Sample Rate'){
    const rates=[...new Set([8000,12000,18000,24000,32000,44100,48000,target.sampleRate])].sort((a,b)=>a-b),input=el('select',{},rates.map(rate=>el('option',{value:rate,selected:rate===target.sampleRate?'':null},rate/1000+' kHz')));
    input.addEventListener('change',()=>{target.sampleRate=Number(input.value);invalidateSynth();renderSamples();});return sf2Field(label,input);
  }
  function volumeControl(target,reset){
    const value=el('output',{class:'range-value','aria-live':'polite'},percentLabel(target.volumePercent)),input=el('input',{type:'range',min:0,max:200,step:1,value:target.volumePercent,'aria-label':'Layer volume'});
    input.addEventListener('input',()=>{target.volumePercent=Number(input.value);target.volumeCustomized=true;invalidateSynth();value.textContent=percentLabel(target.volumePercent);sf2Player.setGainQ8(gainPercentToQ8(target.volumePercent));});
    const resetButton=reset?el('button',{type:'button',onclick:()=>{reset();invalidateSynth();renderSamples();}},'Reset to SF2 value'):null;
    return el('div',{class:'sf2-volume'},el('div',{class:'sf2-volume-heading'},el('strong',{},'Volume'),resetButton),el('div',{class:'sf2-volume-row'},input,value));
  }
  const q15ToPercent = value => Math.max(0,Math.min(100,Number(value)*100/32768));
  const percentToQ15 = value => Math.max(0,Math.min(32768,Math.round((Number(value)||0)*32768/100)));
  const envelopeControls = target => [numberControl(target,'delayMs',0,6553.5,'Delay (ms)','',0.1),numberControl(target,'attackMs',0,5000,'Attack (ms)'),numberControl(target,'holdMs',0,5000,'Hold (ms)'),numberControl(target,'decayMs',0,60000,'Decay (ms)'),numberControl(target,'sustainPercent',0,100,'Sustain Level (%)'),numberControl(target,'releaseMs',10,10000,'Release (ms)')];
  function friendlySynthError(error){
    const message=String(error&&error.message||error||'An unknown error occurred.');
    if(/same name|already exists|409/i.test(message))return'A synth sound with the same name already exists. Rename it or choose overwrite.';
    if(/full|storage|write failed|507/i.test(message))return'The SD card does not have enough free space or could not be written. Check the card and try again.';
    if(/connection|interrupted|timed out|network/i.test(message))return'The connection was interrupted. Check the Wi-Fi connection to KANTAN Sampler and try again.';
    return message;
  }
  async function previewSynthOnDevice(buildOutput){
    sf2Player.stop(true);sf2Editor.busy=true;sf2Editor.error='';renderSamples();
    try{const bytes=buildOutput();parseKtSynth(bytes);if(PREVIEW){await sleep(250);status('Preview mode: connect KANTAN Sampler to audition the converted sound');return;}await uploadRequest('/api/sampler/preview-ktsynth',new Blob([bytes],{type:'application/vnd.instachord.ktsynth'}),()=>{});await command({action:'previewWav',file:'/sampler/samples/Synth/.web-preview.ktsynth',maxMs:2000},false);}
    catch(err){sf2Editor.error=friendlySynthError(err);}finally{sf2Editor.busy=false;renderSamples();}
  }
  function devicePreviewControl(buildOutput,disabled=false){return el('div',{class:'device-preview'},el('button',{class:'primary',disabled:disabled?'':null,onclick:()=>previewSynthOnDevice(buildOutput)},sf2Editor.busy?'Sending…':'Play on KANTAN Sampler'),el('small',{},'Preview only — not saved.'));}
  function chooseSynthMethod(method){sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method};renderSamples();}
  function synthMethodPanel(){return el('section',{class:'panel sf2-converter'},el('h2',{},'Synth Sound'),el('p',{},'Make sounds for Bass, Melody, and Chord.'),el('div',{class:'synth-methods'},el('button',{class:'primary',onclick:()=>chooseSynthMethod('sf2')},el('strong',{},'From SoundFont'),el('small',{},'.sf2 · up to 2 layers')),el('button',{class:'primary',onclick:()=>chooseSynthMethod('audio')},el('strong',{},'From WAV / MP3'),el('small',{},'1 audio file')),el('button',{class:'primary',onclick:()=>chooseSynthMethod('ktsynth')},el('strong',{},'Edit Synth'),el('small',{},'.ktsynth file'))));}
  async function ensureSynthFolder(){if(PREVIEW){if(!previewFolders.samples.includes('Synth'))previewFolders.samples.push('Synth');return;}try{await request('/api/sampler/folders/samples?path=&name=Synth',{method:'POST'});}catch(err){if(!/exist|failed|conflict/i.test(err.message))throw err;}}
  async function saveSynthOutput(builder){
    sf2Editor.busy=true;sf2Editor.error='';renderSamples();
    try{const bytes=builder(),filename=safeSynthName(sf2Editor.name)+'.ktsynth',path='Synth/'+filename;parseKtSynth(bytes);if(PREVIEW){await sleep(350);const at=previewFiles.samples.findIndex(file=>file.name===filename&&file.folder==='Synth'),entry={name:filename,size:bytes.length,folder:'Synth'};if(at>=0&&!sf2Editor.overwrite)throw new Error('a KANTAN Synth tone with the same name already exists');if(at>=0)previewFiles.samples[at]=entry;else previewFiles.samples.push(entry);}else{await ensureSynthFolder();await uploadRequest('/api/sampler/files/samples/'+encodeURIComponent(path)+'?overwrite='+(sf2Editor.overwrite?'1':'0'),new Blob([bytes],{type:'application/vnd.instachord.ktsynth'}),()=>{});}sf2Editor.savedPath='/sampler/samples/'+path;sf2Editor.overwrite=false;}
    catch(err){sf2Editor.error=friendlySynthError(err);if(/already exists|same name/i.test(err.message))sf2Editor.overwrite=true;}finally{sf2Editor.busy=false;renderSamples();}
  }
  function downloadSynthOutput(builder){try{const bytes=sf2Editor.output||builder();parseKtSynth(bytes);const a=el('a',{href:URL.createObjectURL(new Blob([bytes],{type:'application/vnd.instachord.ktsynth'})),download:safeSynthName(sf2Editor.name)+'.ktsynth'});a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);renderSamples();}catch(err){sf2Editor.error=friendlySynthError(err);renderSamples();}}

  function applySf2RegionDefaults(layer,region){
    if(!region)return;if(layer.delayMs===null)layer.delayMs=region.delayMs;if(layer.attackMs===null)layer.attackMs=region.attackMs;if(layer.holdMs===null)layer.holdMs=region.holdMs;if(layer.decayMs===null)layer.decayMs=region.decayMs;if(layer.sustainPercent===null)layer.sustainPercent=q15ToPercent(region.sustainLevelQ15);if(layer.releaseMs===null)layer.releaseMs=region.releaseMs;
  }
  const sameSf2Waveform = (a,b) => Boolean(a&&b&&a.sampleIndex===b.sampleIndex&&a.start===b.start&&a.end===b.end&&a.loopStart===b.loopStart&&a.loopEnd===b.loopEnd&&a.sustainMode===b.sustainMode&&a.sampleType===b.sampleType&&a.sampleLink===b.sampleLink);
  function updateSf2Regions(preserve=true){
    const former=preserve?sf2Editor.sf2Layers.map(layer=>layer.regionId):[];
    sf2Editor.regions=sf2Editor.sf2&&sf2Editor.programIndex!==null?resolvePresetRegions(sf2Editor.sf2,Number(sf2Editor.programIndex),sf2Editor.key,sf2Editor.velocity):[];
    sf2Editor.sf2Layers=sf2Editor.sf2Layers.filter(layer=>sf2Editor.regions.some(region=>region.id===layer.regionId));
    if(!preserve||!sf2Editor.sf2Layers.length){sf2Editor.sf2Layers=[newSf2Layer()];if(sf2Editor.regions.length===1)sf2Editor.sf2Layers[0].regionId=sf2Editor.regions[0].id;}
    else sf2Editor.sf2Layers.sort((a,b)=>former.indexOf(a.regionId)-former.indexOf(b.regionId));
    for(const [index,layer] of sf2Editor.sf2Layers.entries()){const region=sf2Editor.regions.find(item=>item.id===layer.regionId);if(region&&!layer.volumeCustomized)layer.volumePercent=attenuationCbToGainPercent(region.initialAttenuationCb);applySf2RegionDefaults(layer,region);if(index===1&&sameSf2Waveform(sf2Editor.regions.find(item=>item.id===sf2Editor.sf2Layers[0].regionId),region))layer.pcmMode='same';}
    invalidateSynth();
  }
  async function loadSoundFont(file){
    sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method:'sf2',file,busy:true};renderSamples();
    try{if(!/\.sf2$/i.test(file.name))throw new Error(/\.sf3$/i.test(file.name)?'SF3 is not supported. Choose an uncompressed SoundFont 2 (.sf2) file.':'Choose an .sf2 file.');sf2Editor.sf2=parseSf2(await file.arrayBuffer());sf2Editor.programs=listSoundPrograms(sf2Editor.sf2);if(!sf2Editor.programs.length)throw new Error('No presets were found.');sf2Editor.programIndex=initialSoundProgramIndex(sf2Editor.programs);if(sf2Editor.programIndex!==null){const selected=sf2Editor.programs.find(program=>program.index===sf2Editor.programIndex);sf2Editor.name=selected?selected.name:sf2Editor.sf2.name;}updateSf2Regions(false);}
    catch(err){sf2Editor.error=err.message;}finally{sf2Editor.busy=false;renderSamples();}
  }
  function toggleSf2Region(region){
    const at=sf2Editor.sf2Layers.findIndex(layer=>layer.regionId===region.id);
    if(at>=0){sf2Editor.sf2Layers.splice(at,1);if(!sf2Editor.sf2Layers.length)sf2Editor.sf2Layers.push(newSf2Layer());}
    else{const empty=sf2Editor.sf2Layers.find(layer=>!layer.regionId);if(empty)empty.regionId=region.id;else if(sf2Editor.sf2Layers.length<2)sf2Editor.sf2Layers.push({...newSf2Layer(),regionId:region.id});const layer=sf2Editor.sf2Layers.find(item=>item.regionId===region.id);if(layer){layer.volumePercent=attenuationCbToGainPercent(region.initialAttenuationCb);layer.volumeCustomized=false;applySf2RegionDefaults(layer,region);const index=sf2Editor.sf2Layers.indexOf(layer),first=sf2Editor.regions.find(item=>item.id===sf2Editor.sf2Layers[0].regionId);if(index===1&&sameSf2Waveform(first,region))layer.pcmMode='same';}}
    invalidateSynth();renderSamples();
  }
  async function previewSf2Layer(waveRegion,settings,parameterRegion=waveRegion){
    try{const source=extractRegionPcm(sf2Editor.sf2,waveRegion);await sf2Player.play(source.pcm,{sampleRate:waveRegion.sampleRate,previewNote:sf2Editor.key,rootNote:parameterRegion.rootNote,tuneCents:parameterRegion.tuneCents+(settings.tuneOffset||0),sustainMode:waveRegion.sustainMode,loopStart:source.loopStart,loopEnd:source.loopEnd,delayMs:settings.delayMs??parameterRegion.delayMs,attackMs:settings.attackMs??parameterRegion.attackMs,holdMs:settings.holdMs??parameterRegion.holdMs,decayMs:settings.decayMs??parameterRegion.decayMs,sustainLevelQ15:settings.sustainPercent===null?parameterRegion.sustainLevelQ15:percentToQ15(settings.sustainPercent),releaseMs:settings.releaseMs??parameterRegion.releaseMs,gainQ8:gainPercentToQ8(settings.volumePercent)});sf2Editor.error='';}
    catch(err){sf2Editor.error='Could not preview: '+err.message;renderSamples();}
  }
  function sf2Candidate(region,index){
    const selected=sf2Editor.sf2Layers.findIndex(layer=>layer.regionId===region.id),id='sf2-region-'+index,input=el('input',{id,type:'checkbox',value:region.id,checked:selected>=0?'':null,disabled:selected<0&&selectedSf2Layers().length>=2?'':null});input.addEventListener('change',()=>toggleSf2Region(region));
    return el('div',{class:'sf2-candidate'+(selected>=0?' selected':'')},input,el('label',{for:id,class:'sf2-candidate-name'},`${selected>=0?`Layer ${selected+1} · `:''}${region.sampleName||`Sound ${index+1}`}`),el('small',{},`${region.instrumentName} · Key ${region.keyRange[0]}–${region.keyRange[1]} · Velocity ${region.velRange[0]}–${region.velRange[1]}`),el('button',{type:'button',onclick:()=>previewSf2Layer(region,selected>=0?sf2Editor.sf2Layers[selected]:newSf2Layer())},'Preview'));
  }
  function buildSf2Layer(waveRegion,settings,parameterRegion=waveRegion,pcmSourceLayer=0,sharedRate=null){
    const outputRate=sharedRate||settings.sampleRate,source=extractRegionPcm(sf2Editor.sf2,waveRegion),ratio=outputRate/waveRegion.sampleRate,looped=waveRegion.sustainMode==='loop';let pcm=resamplePcm(source.pcm,waveRegion.sampleRate,outputRate),loopStart=looped?Math.round(source.loopStart*ratio):0,loopEnd=looped?Math.round(source.loopEnd*ratio):0;if(looped){pcm=trimLoopTail(pcm,loopEnd,true);loopEnd=Math.min(loopEnd,pcm.length);}const crossfade=looped?Math.max(0,Math.min(65535,Math.floor((loopEnd-loopStart)/4),Math.round(settings.crossfadeMs*outputRate/1000))):0;return{pcm,sampleRate:outputRate,startFrame:0,endFrameExclusive:pcm.length,loopStartFrame:looped?loopStart:0,loopEndFrameExclusive:looped?loopEnd:0,loopCrossfadeFrames:crossfade,sustainMode:looped?1:0,delayMs:settings.delayMs,attackMs:Math.round(settings.attackMs),holdMs:Math.round(settings.holdMs),decayMs:Math.round(settings.decayMs),sustainLevelQ15:percentToQ15(settings.sustainPercent),releaseMs:Math.round(settings.releaseMs),tuneCents:Math.max(-100,Math.min(100,Math.round(parameterRegion.tuneCents+(settings.tuneOffset||0)))),defaultGainQ8:gainPercentToQ8(settings.volumePercent),rootNote:parameterRegion.rootNote,pcmSourceLayer};
  }
  function buildSf2Output(){const selected=selectedSf2Layers(),name=safeSynthName(sf2Editor.name);if(!selected.length)throw new Error('Choose at least one source sound.');if(!name)throw new Error('Enter a sound name.');const layers=[buildSf2Layer(selected[0].region,selected[0].settings,selected[0].region,0)];if(selected[1]){const shared=selected[1].settings.pcmMode==='same';layers.push(buildSf2Layer(shared?selected[0].region:selected[1].region,selected[1].settings,selected[1].region,shared?0:1,shared?layers[0].sampleRate:null));}const bytes=encodeKtSynth(layers,{name});parseKtSynth(bytes);sf2Editor.output=bytes;return bytes;}
  function sf2LayerCard(item,index){
    const {region,settings}=item,selected=selectedSf2Layers(),waveRegion=index===1&&settings.pcmMode==='same'?selected[0].region:region,source=index===1?el('select',{},el('option',{value:'same',selected:settings.pcmMode==='same'?'':null},'Same as Layer 1'),el('option',{value:'own',selected:settings.pcmMode!=='same'?'':null},'Different sound')):null;if(source)source.addEventListener('change',()=>{settings.pcmMode=source.value;invalidateSynth();renderSamples();});const card=el('section',{class:'synth-layer'},el('div',{class:'sf2-launcher'},el('h3',{},`Layer ${index+1}: ${region.sampleName||'Sound'}`),index===1?el('button',{onclick:()=>toggleSf2Region(region)},'Remove Layer 2'):null),index===1?sf2Field('Layer 2 Source',source):null,el('div',{class:'actions'},el('button',{onclick:()=>previewSf2Layer(waveRegion,settings,region)},`Preview Layer ${index+1}`),el('button',{onclick:()=>sf2Player.stop(true)},'Stop')));
    card.append(volumeControl(settings,()=>{settings.volumePercent=attenuationCbToGainPercent(region.initialAttenuationCb);settings.volumeCustomized=false;}));
    const controls=[index===1&&settings.pcmMode==='same'?sf2Field('Sample Rate',el('output',{},`${selected[0].settings.sampleRate/1000} kHz`),'Shared with Layer 1'):rateControl(settings),numberControl(settings,'crossfadeMs',0,1000,'Loop Crossfade (ms)'),numberControl(settings,'tuneOffset',-100,100,'Pitch Correction (cents)'),...envelopeControls(settings)];card.append(el('details',{class:'sf2-advanced'},el('summary',{},`Layer ${index+1} Settings`),el('div',{class:'sf2-grid'},controls)));return card;
  }
  function sf2Panel(){
    const file=el('input',{type:'file',accept:'.sf2,application/octet-stream'});file.addEventListener('change',()=>file.files[0]&&loadSoundFont(file.files[0]));const panel=el('section',{class:'panel sf2-converter'},el('div',{class:'sf2-launcher'},el('h2',{},'SoundFont'),el('button',{onclick:()=>chooseSynthMethod(null)},'Back')),sf2Field('SF2 File',file,sf2Editor.file?sf2Editor.file.name:'Choose .sf2'));
    if(sf2Editor.busy)panel.append(el('p',{class:'sf2-working','aria-live':'polite'},'Processing…'));if(sf2Editor.error)panel.append(el('p',{class:'error',role:'alert'},sf2Editor.error));if(!sf2Editor.sf2)return panel;
    const program=el('select',{},sf2Editor.programs.length>1?el('option',{value:'',selected:sf2Editor.programIndex===null?'':null},'Choose a preset'):null,sf2Editor.programs.map(item=>el('option',{value:item.index,selected:item.index===sf2Editor.programIndex?'':null},item.label)));program.addEventListener('change',()=>{sf2Editor.programIndex=program.value===''?null:Number(program.value);const selected=sf2Editor.programs.find(item=>item.index===sf2Editor.programIndex);if(selected)sf2Editor.name=selected.name;updateSf2Regions(false);renderSamples();});panel.append(sf2Field('Preset',program,`${sf2Editor.sf2.name} · SF2 ${sf2Editor.sf2.version}`));if(sf2Editor.programIndex===null)return panel;
    panel.append(el('details',{class:'sf2-advanced'},el('summary',{},'Sound Matching Settings'),el('div',{class:'sf2-grid'},numberControl(sf2Editor,'key',0,127,'Root Note','Updates the sound choices'),numberControl(sf2Editor,'velocity',1,127,'Reference Velocity','Updates the sound choices'))));
    panel.append(el('h3',{},'Sounds'));if(sf2Editor.regions.length>1)panel.append(el('p',{class:'warning',role:'status'},sf2Editor.regions.length>2?'Choose up to 2. Extra features are ignored.':'Choose 1 or 2. Unsupported features are ignored.'));if(!sf2Editor.regions.length)panel.append(el('p',{class:'error'},'No matching sound.'));else panel.append(el('fieldset',{class:'sf2-candidates'},el('legend',{},'Choose up to 2'),sf2Editor.regions.map(sf2Candidate)));
    const selected=selectedSf2Layers();for(const item of selected)if(item.region.unsupported.length)panel.append(el('p',{class:'warning'},`${item.region.sampleName} — ignored: ${item.region.unsupported.join(', ')}`));selected.forEach((item,index)=>panel.append(sf2LayerCard(item,index)));
    const layerFrames=selected.map((item,index)=>{const shared=index===1&&item.settings.pcmMode==='same',wave=shared?selected[0]:item,rate=shared?selected[0].settings.sampleRate:item.settings.sampleRate,frames=Math.round((wave.region.sustainMode==='loop'?wave.region.loopEnd-wave.region.start:wave.region.end-wave.region.start)*rate/wave.region.sampleRate);return{frames,looped:wave.region.sustainMode==='loop',pcmSourceLayer:shared?0:index};}),estimated=selected.length?estimateKtSynthBytes(sf2Editor.name||'tone',layerFrames):0,gainTooHigh=totalLayerGain(selected.map(item=>item.settings))>512,tooLarge=estimated>KTSYNTH_MAX_BYTES;if(gainTooHigh)panel.append(el('p',{class:'error'},'Combined Layer 1 and Layer 2 volume cannot exceed 200%.'));if(tooLarge)panel.append(el('p',{class:'error'},'The output exceeds 2 MiB. Reduce a sample rate or choose shorter sounds.'));
    const name=el('input',{type:'text',value:sf2Editor.name,maxlength:80,autocomplete:'off'});name.addEventListener('input',()=>{sf2Editor.name=name.value;invalidateSynth();});panel.append(sf2Field('Sound Name',name,'Filename: '+(safeSynthName(sf2Editor.name)||'(not entered)')+'.ktsynth'),el('p',{class:'sf2-size'},`KTS2 · ${selected.length||0} layer${selected.length===1?'':'s'} · ${estimated?Math.ceil(estimated/1024):0} KB`));
    const disabled=!selected.length||!safeSynthName(sf2Editor.name)||sf2Editor.busy||gainTooHigh||tooLarge;panel.append(devicePreviewControl(buildSf2Output,disabled),el('div',{class:'sf2-save-row'},el('button',{class:'primary',disabled:disabled?'':null,onclick:()=>saveSynthOutput(buildSf2Output)},sf2Editor.overwrite?'Overwrite on SD':'Save to SD'),el('button',{disabled:disabled?'':null,onclick:()=>downloadSynthOutput(buildSf2Output)},'Download')));return panel;
  }

  const gainQ8ToPercent = value => Math.max(0,Math.min(200,Number(value)*100/256));
  async function loadKtSynth(file){
    sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method:'ktsynth',file,busy:true};renderSamples();
    try{
      if(!/\.ktsynth$/i.test(file.name))throw new Error('Choose a .ktsynth file.');
      const parsed=parseKtSynth(new Uint8Array(await file.arrayBuffer()));sf2Editor.name=parsed.name;
      sf2Editor.ktsLayers=parsed.layers.map((layer,index)=>({sourcePcm:layer.pcm,independentPcm:index===1&&layer.pcmSourceLayer===1?layer.pcm:null,sourceSampleRate:layer.sampleRate,sourceStartFrame:layer.startFrame,sourceEndFrame:layer.endFrameExclusive,sourceLoopStart:layer.loopStartFrame,sourceLoopEnd:layer.loopEndFrameExclusive,sustainMode:layer.sustainMode,pcmMode:index===1&&layer.pcmSourceLayer===0?'same':'own',sampleRate:layer.sampleRate,crossfadeMs:layer.sampleRate?layer.loopCrossfadeFrames*1000/layer.sampleRate:0,delayMs:layer.delay100us/10,attackMs:layer.attackMs,holdMs:layer.holdMs,decayMs:layer.decayMs,sustainPercent:q15ToPercent(layer.sustainLevelQ15),releaseMs:layer.releaseMs,tuneOffset:layer.tuneCents,volumePercent:gainQ8ToPercent(layer.defaultGainQ8),volumeCustomized:true,rootNote:layer.rootNote}));
    }catch(err){sf2Editor.error=friendlySynthError(err);}finally{sf2Editor.busy=false;invalidateSynth();renderSamples();}
  }
  function buildImportedKtsLayer(settings,index,built){
    const shared=index===1&&settings.pcmMode==='same',sourcePcm=shared?sf2Editor.ktsLayers[0].sourcePcm:(settings.independentPcm||settings.sourcePcm),sourceRate=shared?sf2Editor.ktsLayers[0].sourceSampleRate:settings.sourceSampleRate,outputRate=shared?built[0].sampleRate:settings.sampleRate,ratio=outputRate/sourceRate,pcm=shared?built[0].pcm:resamplePcm(sourcePcm,sourceRate,outputRate),limit=value=>Math.max(0,Math.min(pcm.length,Math.round(value*ratio))),startFrame=Math.min(pcm.length-1,limit(settings.sourceStartFrame)),endFrameExclusive=Math.max(startFrame+1,limit(settings.sourceEndFrame)),looped=settings.sustainMode===1;
    const loopStartFrame=looped?Math.max(startFrame,Math.min(endFrameExclusive-1,limit(settings.sourceLoopStart))):0,loopEndFrameExclusive=looped?Math.max(loopStartFrame+1,Math.min(endFrameExclusive,limit(settings.sourceLoopEnd))):0,loopLimit=looped?Math.min(65535,Math.floor((loopEndFrameExclusive-loopStartFrame)/4)):0,loopCrossfadeFrames=looped?Math.max(0,Math.min(loopLimit,Math.round(settings.crossfadeMs*outputRate/1000))):0;
    return{pcm,pcmSourceLayer:shared?0:index,sampleRate:outputRate,startFrame,endFrameExclusive,loopStartFrame,loopEndFrameExclusive,loopCrossfadeFrames,sustainMode:looped?1:0,delayMs:settings.delayMs,attackMs:Math.round(settings.attackMs),holdMs:Math.round(settings.holdMs),decayMs:Math.round(settings.decayMs),sustainLevelQ15:percentToQ15(settings.sustainPercent),releaseMs:Math.round(settings.releaseMs),tuneCents:Math.max(-100,Math.min(100,Math.round(settings.tuneOffset||0))),defaultGainQ8:gainPercentToQ8(settings.volumePercent),rootNote:Math.round(settings.rootNote)};
  }
  function buildImportedKtsOutput(){const name=safeSynthName(sf2Editor.name);if(!sf2Editor.ktsLayers.length)throw new Error('Choose a KANTAN Synth file.');if(!name)throw new Error('Enter a sound name.');const layers=[];sf2Editor.ktsLayers.forEach((settings,index)=>layers.push(buildImportedKtsLayer(settings,index,layers)));const bytes=encodeKtSynth(layers,{name});parseKtSynth(bytes);sf2Editor.output=bytes;return bytes;}
  async function previewImportedKtsLayer(settings){
    try{const index=sf2Editor.ktsLayers.indexOf(settings),built=[];for(let i=0;i<=index;i++)built.push(buildImportedKtsLayer(sf2Editor.ktsLayers[i],i,built));const layer=built[index];await sf2Player.play(layer.pcm,{sampleRate:layer.sampleRate,previewNote:layer.rootNote,rootNote:layer.rootNote,tuneCents:layer.tuneCents,sustainMode:layer.sustainMode===1?'loop':'off',loopStart:layer.loopStartFrame,loopEnd:layer.loopEndFrameExclusive,delayMs:layer.delayMs,attackMs:layer.attackMs,holdMs:layer.holdMs,decayMs:layer.decayMs,sustainLevelQ15:layer.sustainLevelQ15,releaseMs:layer.releaseMs,gainQ8:layer.defaultGainQ8});sf2Editor.error='';}
    catch(err){sf2Editor.error='Could not preview: '+err.message;renderSamples();}
  }
  function importedKtsLayerCard(settings,index){
    const remove=index===1?el('button',{onclick:()=>{sf2Editor.ktsLayers.splice(index,1);invalidateSynth();renderSamples();}},'Remove Layer 2'):null,source=index===1?el('select',{},el('option',{value:'same',selected:settings.pcmMode==='same'?'':null},'Same as Layer 1'),el('option',{value:'own',selected:settings.pcmMode!=='same'?'':null,disabled:settings.independentPcm?null:''},'Different sound')):null;if(source)source.addEventListener('change',()=>{settings.pcmMode=source.value;invalidateSynth();renderSamples();});const card=el('section',{class:'synth-layer'},el('div',{class:'sf2-launcher'},el('h3',{},`Layer ${index+1}`),remove),index===1?sf2Field('Layer 2 Source',source,settings.independentPcm?'':'Shared sound'):null,el('div',{class:'actions'},el('button',{onclick:()=>previewImportedKtsLayer(settings)},`Preview Layer ${index+1}`),el('button',{onclick:()=>sf2Player.stop(true)},'Stop')));
    card.append(volumeControl(settings));
    const controls=[index===1&&settings.pcmMode==='same'?sf2Field('Sample Rate',el('output',{},`${sf2Editor.ktsLayers[0].sampleRate/1000} kHz`),'Shared with Layer 1'):rateControl(settings),numberControl(settings,'rootNote',0,127,'Root Note'),numberControl(settings,'tuneOffset',-100,100,'Pitch Correction (cents)'),...envelopeControls(settings)];if(settings.sustainMode===1)controls.splice(1,0,numberControl(settings,'crossfadeMs',0,1000,'Loop Crossfade (ms)'));
    card.append(el('details',{class:'sf2-advanced'},el('summary',{},`Layer ${index+1} Settings`),el('div',{class:'sf2-grid'},controls)));return card;
  }
  function ktsynthPanel(){
    const file=el('input',{type:'file',accept:'.ktsynth,application/vnd.instachord.ktsynth'});file.addEventListener('change',()=>file.files[0]&&loadKtSynth(file.files[0]));const panel=el('section',{class:'panel sf2-converter'},el('div',{class:'sf2-launcher'},el('h2',{},'Edit Synth'),el('button',{onclick:()=>chooseSynthMethod(null)},'Back')),sf2Field('Synth File',file,sf2Editor.file?sf2Editor.file.name:'Choose .ktsynth'));
    if(sf2Editor.busy)panel.append(el('p',{class:'sf2-working','aria-live':'polite'},'Processing…'));if(sf2Editor.error)panel.append(el('p',{class:'error',role:'alert'},sf2Editor.error));if(!sf2Editor.ktsLayers.length)return panel;
    sf2Editor.ktsLayers.forEach((layer,index)=>panel.append(importedKtsLayerCard(layer,index)));
    const estimated=estimateKtSynthBytes(sf2Editor.name||'tone',sf2Editor.ktsLayers.map((layer,index)=>({frames:Math.round(layer.sourcePcm.length*layer.sampleRate/layer.sourceSampleRate),looped:layer.sustainMode===1,pcmSourceLayer:index===1&&layer.pcmMode==='same'?0:index}))),gainTooHigh=totalLayerGain(sf2Editor.ktsLayers)>512,tooLarge=estimated>KTSYNTH_MAX_BYTES;if(gainTooHigh)panel.append(el('p',{class:'error'},'Combined Layer 1 and Layer 2 volume cannot exceed 200%.'));if(tooLarge)panel.append(el('p',{class:'error'},'The output exceeds 2 MiB. Reduce a sample rate.'));
    const name=el('input',{type:'text',value:sf2Editor.name,maxlength:80,autocomplete:'off'});name.addEventListener('input',()=>{sf2Editor.name=name.value;invalidateSynth();});panel.append(sf2Field('Sound Name',name,'Filename: '+(safeSynthName(sf2Editor.name)||'(not entered)')+'.ktsynth'),el('p',{class:'sf2-size'},`KTS2 · ${sf2Editor.ktsLayers.length} layer${sf2Editor.ktsLayers.length===1?'':'s'} · ${Math.ceil(estimated/1024)} KB`));
    const disabled=!safeSynthName(sf2Editor.name)||sf2Editor.busy||gainTooHigh||tooLarge;panel.append(devicePreviewControl(buildImportedKtsOutput,disabled),el('div',{class:'sf2-save-row'},el('button',{class:'primary',disabled:disabled?'':null,onclick:()=>saveSynthOutput(buildImportedKtsOutput)},sf2Editor.overwrite?'Overwrite on SD':'Save to SD'),el('button',{disabled:disabled?'':null,onclick:()=>downloadSynthOutput(buildImportedKtsOutput)},'Download')));return panel;
  }

  async function loadAudioSource(file){
    const layer=sf2Editor.audio;sf2Player.stop(true);layer.file=file;layer.audio=null;layer.pitchConfirmed=false;sf2Editor.busy=true;sf2Editor.error='';if(!sf2Editor.name)sf2Editor.name=safeSynthName(file.name.replace(/\.(wav|mp3)$/i,''));renderSamples();
    try{if(!/\.(wav|mp3)$/i.test(file.name))throw new Error('Choose a WAV or MP3 file.');const audio=await decodeAudioFile(file);if(audio.duration>20)throw new Error(`The audio must be 20 seconds or shorter (currently ${audio.duration.toFixed(1)} seconds).`);layer.audio=audio;layer.pitchSuggestion=audio.suggestion;const suggestion=audio.suggestion;layer.pitchNote=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false?suggestion.note:60;layer.tuneOffset=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false?suggestion.tuneCents||0:0;}
    catch(err){sf2Editor.error=err.message;}finally{sf2Editor.busy=false;invalidateSynth();renderSamples();}
  }
  function setPitchPart(layer,part,value){const current=layer.pitchNote,pitchClass=part==='name'?Number(value):current%12,octave=part==='octave'?Number(value):Math.floor(current/12)-1;layer.pitchNote=Math.max(0,Math.min(127,(octave+1)*12+pitchClass));layer.pitchConfirmed=false;invalidateSynth();renderSamples();}
  function pitchControls(layer){const note=layer.pitchNote,names=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'],pitch=el('select',{'aria-label':'Note'},names.map((name,index)=>el('option',{value:index,selected:index===note%12?'':null},name))),octave=el('select',{'aria-label':'Octave'},Array.from({length:11},(_,index)=>index-1).map(value=>el('option',{value,selected:value===Math.floor(note/12)-1?'':null},value)));pitch.addEventListener('change',()=>setPitchPart(layer,'name',pitch.value));octave.addEventListener('change',()=>setPitchPart(layer,'octave',octave.value));return el('div',{class:'pitch-controls'},el('label',{},'Note',pitch),el('label',{},'Octave',octave));}
  function audioSourceCard(layer){
    const file=el('input',{type:'file',accept:'.wav,.mp3,audio/wav,audio/mpeg'});file.addEventListener('change',()=>file.files[0]&&loadAudioSource(file.files[0]));const card=el('section',{class:'synth-layer'},sf2Field('Audio File',file,layer.file?layer.file.name:'WAV or MP3, up to 20 seconds'));if(!layer.audio)return card;
    const suggestion=layer.pitchSuggestion,usable=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false;card.append(el('section',{class:'pitch-confirm'},el('h3',{},'Source Pitch'),usable?el('p',{class:'warning'},`Detected: ${midiNoteName(suggestion.note)} (please confirm) · ${suggestion.source}`):el('p',{class:'warning'},'The pitch could not be detected. Choose the source pitch.'),pitchControls(layer),el('button',{class:layer.pitchConfirmed?'':'primary',onclick:()=>{layer.pitchConfirmed=true;invalidateSynth();renderSamples();}},layer.pitchConfirmed?`✓ Use ${midiNoteName(layer.pitchNote)}`:'Use This Pitch')));
    card.append(el('div',{class:'actions'},el('button',{onclick:()=>sf2Player.play(layer.audio.pcm,{sampleRate:layer.audio.sampleRate,previewNote:layer.pitchNote,rootNote:layer.pitchNote,tuneCents:layer.tuneOffset||0,sustainMode:'off',loopStart:0,loopEnd:0,delayMs:layer.delayMs,attackMs:layer.attackMs,holdMs:layer.holdMs,decayMs:layer.decayMs,sustainLevelQ15:percentToQ15(layer.sustainPercent),releaseMs:layer.releaseMs,gainQ8:gainPercentToQ8(layer.volumePercent)})},'Preview Source Sound'),el('button',{onclick:()=>sf2Player.stop(true)},'Stop')),volumeControl(layer));
    card.append(el('details',{class:'sf2-advanced'},el('summary',{},'Sound Settings'),el('div',{class:'sf2-grid'},rateControl(layer,'Output Sample Rate'),numberControl(layer,'tuneOffset',-100,100,'Pitch Correction (cents)'),...envelopeControls(layer),el('div',{class:'sf2-size'},el('span',{},'Duration'),el('strong',{},layer.audio.duration.toFixed(1)+' sec')))));return card;
  }
  function buildAudioOutput(){const layer=sf2Editor.audio,name=safeSynthName(sf2Editor.name);if(!layer.audio)throw new Error('Choose an audio file.');if(!layer.pitchConfirmed)throw new Error('Confirm the source pitch.');if(!name)throw new Error('Enter a sound name.');const pcm=resamplePcm(layer.audio.pcm,layer.audio.sampleRate,layer.sampleRate),bytes=encodeKtSynth([{pcm,pcmSourceLayer:0,sampleRate:layer.sampleRate,startFrame:0,endFrameExclusive:pcm.length,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,sustainMode:0,delayMs:layer.delayMs,attackMs:layer.attackMs,holdMs:layer.holdMs,decayMs:layer.decayMs,sustainLevelQ15:percentToQ15(layer.sustainPercent),releaseMs:layer.releaseMs,tuneCents:Math.max(-100,Math.min(100,Math.round(layer.tuneOffset||0))),defaultGainQ8:gainPercentToQ8(layer.volumePercent),rootNote:layer.pitchNote}],{name});parseKtSynth(bytes);sf2Editor.output=bytes;return bytes;}
  function audioPanel(){
    const panel=el('section',{class:'panel sf2-converter'},el('div',{class:'sf2-launcher'},el('h2',{},'WAV / MP3'),el('button',{onclick:()=>chooseSynthMethod(null)},'Back')));if(sf2Editor.error)panel.append(el('p',{class:'error',role:'alert'},sf2Editor.error));if(sf2Editor.busy)panel.append(el('p',{class:'sf2-working'},'Analyzing…'));const layer=sf2Editor.audio;panel.append(audioSourceCard(layer));
    const name=el('input',{type:'text',value:sf2Editor.name,maxlength:80,autocomplete:'off'});name.addEventListener('input',()=>{sf2Editor.name=name.value;invalidateSynth();});if(layer.audio)panel.append(sf2Field('Sound Name',name,'Filename: '+(safeSynthName(sf2Editor.name)||'(not entered)')+'.ktsynth'));
    const estimated=layer.audio?estimateKtSynthBytes(sf2Editor.name||'tone',[{frames:Math.round(layer.audio.pcm.length*layer.sampleRate/layer.audio.sampleRate),looped:false}]):0,tooLarge=estimated>KTSYNTH_MAX_BYTES,disabled=!layer.audio||!layer.pitchConfirmed||!safeSynthName(sf2Editor.name)||sf2Editor.busy||tooLarge;if(tooLarge)panel.append(el('p',{class:'error'},'Over 2 MiB. Use a lower rate or shorter sound.'));if(layer.audio)panel.append(el('p',{class:'sf2-size'},`KTS2 · 1 layer · ${Math.ceil(estimated/1024)} KB`),devicePreviewControl(buildAudioOutput,disabled),el('div',{class:'sf2-save-row'},el('button',{class:'primary',disabled:disabled?'':null,onclick:()=>saveSynthOutput(buildAudioOutput)},sf2Editor.overwrite?'Overwrite on SD':'Save to SD'),el('button',{disabled:disabled?'':null,onclick:()=>downloadSynthOutput(buildAudioOutput)},'Download')));return panel;
  }
  function synthConverterPanel(){
    const builder=sf2Editor.method==='audio'?buildAudioOutput:sf2Editor.method==='ktsynth'?buildImportedKtsOutput:buildSf2Output;
    if(sf2Editor.savedPath)return el('section',{class:'panel sf2-converter','aria-live':'polite'},el('h2',{},'Saved to SD'),el('p',{class:'success'},sf2Editor.savedPath),el('p',{},'Ready for Bass, Melody, and Chord.'),el('div',{class:'actions'},el('button',{onclick:()=>downloadSynthOutput(builder)},'Download'),el('button',{class:'primary',onclick:()=>{sf2Editor=newSf2Editor();sf2Editor.open=true;renderSamples();}},'Make Another')));
    if(!sf2Editor.method)return synthMethodPanel();return sf2Editor.method==='audio'?audioPanel():sf2Editor.method==='ktsynth'?ktsynthPanel():sf2Panel();
  }
  function renderBeat() {
    const root = $('#beat-view');
    if (!root) return;
    root.innerHTML = '';
    root.append(el('div',{class:'panel'},el('h2',{},'Beat files'),folderPanel('loops'),filePanel('loops','.wav,.mp3,.mid,.midi')));
  }
  function renderKit() {
    const root = $('#kit-view');
    if (!root) return;
    root.innerHTML='';
    root.append(el('div',{class:'panel'},el('h2',{},'Kit package files'),folderPanel('kits'),filePanel('kits','.ktkit,.json')));
  }
  function cleanJsonName(name, fallback='New_Project') {
    const clean = String(name || fallback).replace(/[\\/]/g,'_').trim();
    return (clean || fallback).replace(/\.json$/i,'') + '.json';
  }
  function cleanKitName(name, legacy=false, fallback='New_Kit') {
    const clean = String(name || fallback).replace(/[\\/]/g,'_').trim();
    return (clean || fallback).replace(/\.(ktkit|json)$/i,'') + (legacy ? '.json' : '.ktkit');
  }
  function renderProject() {
    const root = $('#project-view');
    if (!root) return;
    root.innerHTML='';
    root.append(el('div',{class:'panel'},el('h2',{},'Project files'),folderPanel('projects'),filePanel('projects','.json')));
  }
  function renderMusic() {
    const root = $('#music-view');
    if (!root) return;
    root.innerHTML='';
    root.append(el('div',{class:'panel'},
      el('h2',{},'Music files'),
      folderPanel('music'),
      filePanel('music','.wav,.mp3')));
  }
  function folderRootLabel(kind) {
    return {samples:'Samples',loops:'Beat',kits:'Kits',projects:'Projects',music:'Music'}[kind] || kind;
  }
  function folderLabel(kind, path) {
    return 'SD / ' + folderRootLabel(kind) + (path ? ' / ' + path.split('/').join(' / ') : '');
  }
  function folderPanel(kind) {
    const current = activeFolder(kind);
    const options = [];
    if (kind === 'samples') options.push(el('option',{value:DEVICE_PRESET,selected:current===DEVICE_PRESET?'':null},'Device Preset'));
    options.push(el('option',{value:'',selected:current===''?'':null},folderLabel(kind,'')));
    for (const path of [...folders[kind]].sort((a,b) => a.localeCompare(b, undefined, {numeric:true}))) {
      options.push(el('option',{value:path,selected:current===path?'':null},folderLabel(kind,path)));
    }
    const choose = el('select', {}, options);
    choose.addEventListener('change', async () => { browseFolders[kind] = choose.value; await refresh(); });
    const create = el('button',{onclick:async()=>{const name=prompt('Folder name');if(name) await createFolder(kind,current,name);}},'New folder');
    return el('div',{class:'folder-panel'},el('div',{class:'row folder-picker'},el('label',{},'Location'),choose,current===DEVICE_PRESET?null:create));
  }
  function filePanel(kind, accept, assignable = false) {
    const list = el('ul',{class:'file-list'});
    for (const file of files[kind]) {
      const preview = kind !== 'music' && /\.(wav|mp3)$/i.test(file.name)
        ? el('button',{title:'Preview file',onclick:async()=>await command({action:'previewWav',file:rootFolder(kind)+'/'+(activeFolder(kind) ? activeFolder(kind)+'/' : '')+file.name,maxMs:1000},false)},'Play')
        : null;
      const assign = assignable
        ? el('button',{class:'primary',onclick:async()=>await command({action:'assignSample',pad:selectedPad,file:browserFilePath(kind,file.name)})},'Assign')
        : null;
      const download = el('button',{onclick:()=>downloadFile(kind,file.name)},'↓');
      const rename = el('button',{onclick:async()=>{
        let next=prompt('New file name',file.name);
        if(kind==='projects'&&next!==null)next=cleanJsonName(next);
        if(kind==='kits'&&next!==null)next=cleanKitName(next,/\.json$/i.test(file.name));
        if(!next||next===file.name)return;
        try { await renameFile(kind,file.name,next); status('Renamed to '+next); }
        catch(err) { status('Rename failed: '+err.message,true); }
      }},'Rename');
      const remove = el('button',{class:'danger',onclick:async()=>{if(confirm('Delete '+file.name+'?')) await deleteFile(kind,file.name);}},'×');
      list.append(el('li',{},el('span',{class:'name'},fileDisplayLabel(kind,file)),el('small',{},Math.ceil(file.size/1024)+' KB'),preview,assign,download,rename,remove));
    }
    let queuedFiles = [];
    const input = el('input',{type:'file',accept,multiple:'',class:'upload-input'});
    const dropTitle = el('strong',{},'Drop files here');
    const dropDetail = el('span',{},'or click to choose multiple files');
    const dropZone = el('div',{class:'upload-dropzone',role:'button',tabindex:'0'},dropTitle,dropDetail);
    const progressLabel = el('span',{class:'upload-progress-label'},'Preparing upload…');
    const progressValue = el('span',{class:'upload-progress-value'},'0%');
    const progressFill = el('span',{class:'upload-progress-fill'});
    const progress = el('div',{class:'upload-progress',hidden:''},
      el('div',{class:'upload-progress-head'},
        el('span',{class:'upload-spinner','aria-hidden':'true'}),progressLabel,progressValue),
      el('div',{class:'upload-progress-track'},progressFill));
    const showProgress = (percent, saving=false, label='') => {
      progress.hidden=false;
      progress.classList.toggle('saving',saving);
      progressLabel.textContent=label || (saving?'Saving to sampler…':'Uploading files…');
      progressValue.textContent=Math.max(0,Math.min(100,Math.round(percent)))+'%';
      progressFill.style.width=Math.max(2,Math.min(100,percent))+'%';
    };
    const upload = el('button',{class:'primary'},'Upload files');
    upload.disabled=true;
    const acceptsFile = file => {
      const name=file.name.toLowerCase();
      return accept.split(',').map(rule=>rule.trim().toLowerCase()).filter(Boolean)
        .some(rule=>rule[0]==='.' ? name.endsWith(rule) : file.type===rule);
    };
    const selectFiles = selected => {
      const supported=[]; const seen=new Set(); let skipped=0;
      for(const file of Array.from(selected||[])) {
        const key=file.name.toLowerCase();
        if(!acceptsFile(file)||seen.has(key)){skipped++;continue;}
        seen.add(key); supported.push(file);
      }
      queuedFiles=supported;
      upload.disabled=queuedFiles.length===0;
      upload.textContent=queuedFiles.length ? 'Upload '+queuedFiles.length+(queuedFiles.length===1?' file':' files') : 'Upload files';
      dropZone.classList.toggle('has-files',queuedFiles.length>0);
      dropTitle.textContent=queuedFiles.length ? queuedFiles.length+(queuedFiles.length===1?' file selected':' files selected') : 'Drop files here';
      dropDetail.textContent=queuedFiles.length
        ? queuedFiles.slice(0,3).map(file=>file.name).join(', ')+(queuedFiles.length>3?' +'+(queuedFiles.length-3)+' more':'')
        : 'or click to choose multiple files';
      if(skipped)status(skipped+' unsupported or duplicate '+(skipped===1?'file was':'files were')+' skipped',true);
    };
    dropZone.addEventListener('click',()=>{if(!upload.disabled||queuedFiles.length===0)input.click();});
    dropZone.addEventListener('keydown',event=>{
      if(event.key==='Enter'||event.key===' '){event.preventDefault();input.click();}
    });
    input.addEventListener('change',()=>selectFiles(input.files));
    for(const eventName of ['dragenter','dragover'])dropZone.addEventListener(eventName,event=>{
      event.preventDefault(); event.stopPropagation(); dropZone.classList.add('dragging');
      if(event.dataTransfer)event.dataTransfer.dropEffect='copy';
    });
    for(const eventName of ['dragleave','drop'])dropZone.addEventListener(eventName,event=>{
      event.preventDefault(); event.stopPropagation(); dropZone.classList.remove('dragging');
    });
    dropZone.addEventListener('drop',event=>selectFiles(event.dataTransfer?.files));
    upload.addEventListener('click',async()=>{
      if(!queuedFiles.length)return;
      const batch=[...queuedFiles];
      const totalBytes=batch.reduce((sum,file)=>sum+Math.max(1,file.size),0);
      let completedBytes=0; let succeeded=0; const failures=[];
      upload.disabled=true; input.disabled=true; dropZone.classList.add('disabled'); showProgress(0);
      for(let index=0;index<batch.length;index++) {
        const file=batch[index]; const prefix=(index+1)+' / '+batch.length+' · ';
        try {
          await uploadFile(kind,file,(percent,saving)=>{
            const overall=(completedBytes+Math.max(1,file.size)*Math.max(0,Math.min(100,percent))/100)*100/totalBytes;
            showProgress(overall,saving,prefix+(saving?'Saving ':'Uploading ')+file.name+'…');
          },false);
          succeeded++;
        } catch(err) { failures.push(file.name+': '+err.message); }
        completedBytes+=Math.max(1,file.size);
      }
      input.value=''; queuedFiles=[];
      upload.disabled=false; input.disabled=false; dropZone.classList.remove('disabled');
      progress.hidden=true; progress.classList.remove('saving');
      try { await refresh(); }
      finally {
        if(failures.length)status('Uploaded '+succeeded+' of '+batch.length+'. '+failures.join(' | '),true);
        else status('Uploaded '+succeeded+(succeeded===1?' file':' files'));
      }
    });
    return el('div',{},el('div',{class:'upload-area'},input,dropZone,upload),progress,list);
  }
  async function renameFile(kind, name, next) {
    const relative = activeFolder(kind);
    const oldPath = rootFolder(kind)+'/'+(relative ? relative+'/' : '')+name;
    const newPath = rootFolder(kind)+'/'+(relative ? relative+'/' : '')+next;
    if (PREVIEW) {
      const file = previewFiles[kind].find(entry => entry.name === name && (entry.folder || '') === activeFolder(kind));
      if (file) file.name = next;
      if(kind==='projects'&&previewState.project.file===oldPath)previewState.project.file=newPath;
      await refresh();
      return;
    }
    const path = relative ? relative + '/' + name : name;
    await request('/api/sampler/files/'+kind+'/'+encodeURIComponent(path)+'?to='+encodeURIComponent(relative ? relative + '/' + next : next),{method:'POST'});
    if(kind==='projects')await command({action:'projectRenamed',old:oldPath,file:newPath});
    else if(kind==='kits')await command({action:'kitRenamed',old:oldPath,file:newPath});
    else await refresh();
  }
  async function deleteFile(kind, name) {
    const relative = activeFolder(kind);
    const fullPath = rootFolder(kind)+'/'+(relative ? relative+'/' : '')+name;
    if (PREVIEW) {
      const index = previewFiles[kind].findIndex(file => file.name === name && (file.folder || '') === activeFolder(kind));
      if (index >= 0) previewFiles[kind].splice(index,1);
      if(kind==='projects'&&previewState.project.file===fullPath)previewState.project.file='';
      await refresh();
      return;
    }
    await request('/api/sampler/files/'+kind+'/'+encodeURIComponent(relative ? relative + '/' + name : name),{method:'DELETE'});
    if(kind==='projects'||kind==='kits')await command({action:'documentDeleted',kind,file:fullPath});
    else await refresh();
  }
  function uploadRequest(path,file,onProgress) {
    return new Promise((resolve,reject)=>{
      const xhr=new XMLHttpRequest();
      xhr.open('PUT',API+path);
      xhr.timeout=120000;
      xhr.upload.onprogress=e=>{
        if(e.lengthComputable)onProgress(e.loaded*100/e.total,false);
        if(e.lengthComputable&&e.loaded>=e.total)onProgress(100,true);
      };
      xhr.onload=()=>{
        if(xhr.status>=200&&xhr.status<300){resolve();return;}
        let message=xhr.statusText||'upload failed';
        try{message=JSON.parse(xhr.responseText).error||message;}catch(_){}
        reject(new Error(message));
      };
      xhr.onerror=()=>reject(new Error('connection lost'));
      xhr.ontimeout=()=>reject(new Error('upload timed out'));
      xhr.onabort=()=>reject(new Error('upload cancelled'));
      xhr.send(file);
    });
  }
  async function uploadFile(kind, file, onProgress=()=>{}, refreshAfter=true) {
    if (PREVIEW) {
      onProgress(35,false); await sleep(180); onProgress(78,false); await sleep(180); onProgress(100,true); await sleep(220);
      const existing = previewFiles[kind].findIndex(entry => entry.name === file.name && (entry.folder || '') === activeFolder(kind));
      const entry = {name:file.name, size:file.size, folder:activeFolder(kind)};
      if (existing >= 0) previewFiles[kind][existing] = entry;
      else previewFiles[kind].push(entry);
      if(refreshAfter)await refresh();
      return;
    }
    status('Uploading '+file.name+'…');
    const path = activeFolder(kind);
    await uploadRequest('/api/sampler/files/'+kind+'/'+encodeURIComponent(path ? path + '/' + file.name : file.name),file,onProgress);
    if(refreshAfter)await refresh();
  }
  async function downloadFile(kind,name) {
    if (PREVIEW) {
      const a = el('a',{href:URL.createObjectURL(new Blob(['KANTAN Sampler preview file'], {type:'text/plain'})),download:name});
      a.click(); setTimeout(()=>URL.revokeObjectURL(a.href),1000);
      return;
    }
    const path = activeFolder(kind); const blob=await request('/api/sampler/files/'+kind+'/'+encodeURIComponent(path ? path + '/' + name : name)).then(r=>r.blob()); const a=el('a',{href:URL.createObjectURL(blob),download:name}); a.click(); setTimeout(()=>URL.revokeObjectURL(a.href),1000);
  }
  async function createFolder(kind, current, name) {
    if (PREVIEW) {
      const path = current ? current + '/' + name : name;
      if (!previewFolders[kind].includes(path)) previewFolders[kind].push(path);
      await refresh();
      return;
    }
    const query = '?path=' + encodeURIComponent(current) + '&name=' + encodeURIComponent(name);
    await request('/api/sampler/folders/'+kind+query,{method:'POST'});
    await refresh();
  }
  function render() { if(!state)return; renderSamples();renderBeat();renderKit();renderProject();renderMusic(); }
  function setupTabs() { for(const tab of document.querySelectorAll('.tab')) tab.addEventListener('click',()=>{for(const t of document.querySelectorAll('.tab'))t.classList.toggle('active',t===tab);for(const v of document.querySelectorAll('.view'))v.classList.toggle('active',v.id===tab.dataset.view);}); }
  document.addEventListener('DOMContentLoaded',()=>{setupTabs();$('#refresh').addEventListener('click',refresh);refresh();});
})();
