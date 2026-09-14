import { parseSf2, listSoundPrograms, initialSoundProgramIndex, resolvePresetRegions, extractRegionPcm } from './converter/sf2.js';
import { resamplePcm, trimLoopTail, applyLoopCrossfade, PreviewPlayer } from './converter/audio.js';
import { encodeKtSynth, parseKtSynth, estimateKtSynthBytes, gainPercentToQ8, attenuationCbToGainPercent, KTSYNTH_MAX_BYTES } from './converter/ktsynth.js';
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
  const newSf2Editor = () => ({open:false,method:null,file:null,sf2:null,programs:[],programIndex:null,
    regions:[],regionId:null,key:60,velocity:110,sampleRate:32000,crossfadeMs:10,
    attackMs:null,releaseMs:null,tuneOffset:0,volumePercent:100,volumeCustomized:false,name:'',error:'',busy:false,output:null,
    savedPath:'',overwrite:false,audio:null,pitchSuggestion:null,pitchNote:60,pitchConfirmed:false});
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
    root.append(sf2ConverterLauncher());
    if (sf2Editor.open) root.append(sf2ConverterPanel());
    const library = el('div', {class:'panel'}, el('h2', {}, 'Sample source'), folderPanel('samples'));
    library.append(activeFolder('samples') === DEVICE_PRESET
      ? presetFilePanel() : filePanel('samples', '.wav,.mp3,.ktsynth', true));
    root.append(assignmentPanel(), library);
  }
  function sf2ConverterLauncher() {
    return sf2Editor.open
      ? el('div',{class:'sf2-launcher'},el('strong',{},'KANTANシンセを作成中'),el('button',{onclick:()=>{sf2Player.stop(true);sf2Editor.open=false;renderSamples();}},'閉じる'))
      : el('button',{class:'sf2-open primary',onclick:()=>{sf2Editor.open=true;renderSamples();}},'KANTANシンセを作る');
  }
  const selectedSf2Region = () => sf2Editor.regions.find(region=>region.id===sf2Editor.regionId)||null;
  const safeSynthName = value => String(value||'').trim().replace(/[\\/:*?"<>|]/g,'_').replace(/^\.+/,'').slice(0,80);
  const sf2RegionVolumePercent = region => attenuationCbToGainPercent(region&&region.initialAttenuationCb);
  const sf2GainQ8 = () => gainPercentToQ8(sf2Editor.volumePercent);
  function syncSf2Volume(region=selectedSf2Region()) { if(region&&!sf2Editor.volumeCustomized)sf2Editor.volumePercent=sf2RegionVolumePercent(region); }
  function updateSf2Regions(preserve=true) {
    const former=preserve?sf2Editor.regionId:null;
    sf2Editor.regions=sf2Editor.sf2&&sf2Editor.programIndex!==null?resolvePresetRegions(sf2Editor.sf2,Number(sf2Editor.programIndex),sf2Editor.key,sf2Editor.velocity):[];
    sf2Editor.regionId=sf2Editor.regions.some(region=>region.id===former)?former:sf2Editor.regions.length===1?sf2Editor.regions[0].id:null;
    syncSf2Volume();
    sf2Editor.output=null;sf2Editor.overwrite=false;
  }
  async function loadSoundFont(file) {
    sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method:'sf2',file,busy:true};renderSamples();
    try {
      if(!/\.sf2$/i.test(file.name))throw new Error(/\.sf3$/i.test(file.name)?'SF3は非対応です。非圧縮のSoundFont 2（.sf2）を選んでください。':'.sf2ファイルを選んでください。');
      sf2Editor.sf2=parseSf2(await file.arrayBuffer());sf2Editor.programs=listSoundPrograms(sf2Editor.sf2);
      if(!sf2Editor.programs.length)throw new Error('音色が見つかりません。');
      sf2Editor.programIndex=initialSoundProgramIndex(sf2Editor.programs);
      if(sf2Editor.programIndex!==null){const selected=sf2Editor.programs.find(p=>p.index===sf2Editor.programIndex);sf2Editor.name=selected?selected.name:sf2Editor.sf2.name;}
      updateSf2Regions(false);
    } catch(err) { sf2Editor.error=err.message; }
    finally { sf2Editor.busy=false;renderSamples(); }
  }
  function sf2Field(label,control,hint='') { return el('label',{class:'sf2-field'},el('span',{},label),control,hint?el('small',{},hint):null); }
  function sf2Number(label,key,min,max,hint='') {
    const value=sf2Editor[key],input=el('input',{type:'number',value:value===null?'':value,min,max,step:1,placeholder:value===null?'SoundFont値':''});
    input.addEventListener('change',()=>{sf2Editor[key]=input.value===''?null:Math.max(min,Math.min(max,Number(input.value)));if(key==='key'||key==='velocity')updateSf2Regions(true);else sf2Editor.output=null;renderSamples();});
    return sf2Field(label,input,hint);
  }
  async function previewSf2Region(region=selectedSf2Region()) {
    if(!region)return;
    try { const source=extractRegionPcm(sf2Editor.sf2,region),gainQ8=gainPercentToQ8(sf2Editor.volumeCustomized||region.id===sf2Editor.regionId?sf2Editor.volumePercent:sf2RegionVolumePercent(region));await sf2Player.play(source.pcm,{sampleRate:region.sampleRate,previewNote:sf2Editor.key,rootNote:region.rootNote,tuneCents:region.tuneCents+(sf2Editor.tuneOffset||0),sustainMode:region.sustainMode,loopStart:source.loopStart,loopEnd:source.loopEnd,attackMs:sf2Editor.attackMs===null?region.attackMs:sf2Editor.attackMs,gainQ8});sf2Editor.error=''; }
    catch(err) { sf2Editor.error='試聴できません: '+err.message;renderSamples(); }
  }
  function sf2Candidate(region,index) {
    const id='sf2-region-'+index,radio=el('input',{id,type:'radio',name:'sf2-region',value:region.id,checked:region.id===sf2Editor.regionId?'':null});
    radio.addEventListener('change',()=>{sf2Editor.regionId=region.id;syncSf2Volume(region);sf2Editor.output=null;sf2Editor.overwrite=false;renderSamples();});
    return el('div',{class:'sf2-candidate'+(region.id===sf2Editor.regionId?' selected':'')},radio,el('label',{for:id,class:'sf2-candidate-name'},region.sampleName||`サウンド ${index+1}`),el('small',{},`${region.instrumentName} · 音域 ${region.keyRange[0]}–${region.keyRange[1]} · 強さ ${region.velRange[0]}–${region.velRange[1]}`),el('button',{type:'button',onclick:()=>previewSf2Region(region),'aria-label':`${region.sampleName||`サウンド ${index+1}`}を試聴`},'試聴'));
  }
  function sf2VolumeControl(region) {
    const value=el('output',{class:'range-value','aria-live':'polite'},`${sf2Editor.volumePercent}%`),input=el('input',{type:'range',min:0,max:200,step:1,value:sf2Editor.volumePercent,'aria-label':'変換後の音量'});
    input.addEventListener('input',()=>{sf2Editor.volumePercent=Number(input.value);sf2Editor.volumeCustomized=true;sf2Editor.output=null;sf2Editor.overwrite=false;value.textContent=`${sf2Editor.volumePercent}%`;sf2Player.setGainQ8(sf2GainQ8());});
    const reset=el('button',{type:'button',onclick:()=>{sf2Editor.volumeCustomized=false;syncSf2Volume(region);sf2Editor.output=null;sf2Editor.overwrite=false;sf2Player.setGainQ8(sf2GainQ8());renderSamples();}},'SF2の値に戻す');
    return el('div',{class:'sf2-volume'},el('div',{class:'sf2-volume-heading'},el('strong',{},'音量'),reset),el('div',{class:'sf2-volume-row'},input,value),el('small',{},'100%が標準です。100%を超えると、音色や再生環境によっては音割れする場合があります。'));
  }

  function friendlySynthError(error) {
    const message=String(error&&error.message||error||'不明なエラーです。');
    if(/same name|already exists|同名|409/i.test(message))return'同じ名前のKANTANシンセがあります。名前を変えるか、上書きを選んでください。';
    if(/full|storage|write failed|507|容量/i.test(message))return'SDカードの空き容量が足りないか、書き込めません。空き容量を確認してもう一度試してください。';
    if(/connection|interrupted|timed out|network|通信/i.test(message))return'通信が途切れました。KANTAN SamplerとのWi-Fi接続を確認し、もう一度保存してください。';
    if(/invalid|format|metadata|CRC|loop|range|422|形式|検証/i.test(message))return'生成ファイルの形式を検証できませんでした。入力ファイルまたは設定を確認してください。';
    return message;
  }
  function buildSf2Output() {
    const region=selectedSf2Region(),name=safeSynthName(sf2Editor.name);if(!region)throw new Error('使用するサウンドを選んでください。');if(!name)throw new Error('音色名を入力してください。');if(region.unsupported.length)throw new Error('このサウンドには非対応機能があります: '+region.unsupported.join(', '));
    const source=extractRegionPcm(sf2Editor.sf2,region),ratio=sf2Editor.sampleRate/region.sampleRate,looped=region.sustainMode==='loop';
    let pcm=resamplePcm(source.pcm,region.sampleRate,sf2Editor.sampleRate),loopStart=looped?Math.round(source.loopStart*ratio):0,loopEnd=looped?Math.round(source.loopEnd*ratio):0;
    if(looped){pcm=trimLoopTail(pcm,loopEnd,true);loopEnd=Math.min(loopEnd,pcm.length);}
    const mixed=looped?applyLoopCrossfade(pcm,loopStart,loopEnd,Math.round(sf2Editor.crossfadeMs*sf2Editor.sampleRate/1000)):{pcm,frames:0};
    const bytes=encodeKtSynth(mixed.pcm,{name,sampleRate:sf2Editor.sampleRate,startFrame:0,endFrameExclusive:mixed.pcm.length,loopStartFrame:looped?loopStart:0,loopEndFrameExclusive:looped?loopEnd:0,loopCrossfadeFrames:mixed.frames,sustainMode:looped?1:0,attackMs:Math.round(sf2Editor.attackMs===null?region.attackMs:sf2Editor.attackMs),releaseMs:Math.round(sf2Editor.releaseMs===null?region.releaseMs:sf2Editor.releaseMs),tuneCents:Math.max(-100,Math.min(100,Math.round(region.tuneCents+(sf2Editor.tuneOffset||0)))),defaultGainQ8:sf2GainQ8(),rootNote:region.rootNote});
    parseKtSynth(bytes);sf2Editor.output=bytes;return bytes;
  }
  async function ensureSynthFolder() {
    if(PREVIEW){if(!previewFolders.samples.includes('Synth'))previewFolders.samples.push('Synth');return;}
    try { await request('/api/sampler/folders/samples?path=&name=Synth',{method:'POST'}); }
    catch(err) { if(!/exist|failed|conflict/i.test(err.message))throw err; }
  }
  async function saveSf2ToSd() {
    sf2Editor.busy=true;sf2Editor.error='';renderSamples();
    try {
      const bytes=buildSf2Output(),filename=safeSynthName(sf2Editor.name)+'.ktsynth',path='Synth/'+filename;
      if(PREVIEW){await sleep(350);const at=previewFiles.samples.findIndex(x=>x.name===filename&&x.folder==='Synth'),entry={name:filename,size:bytes.length,folder:'Synth'};if(at>=0&&!sf2Editor.overwrite)throw new Error('a KANTAN Synth tone with the same name already exists');if(at>=0)previewFiles.samples[at]=entry;else previewFiles.samples.push(entry);}
      else {await ensureSynthFolder();await uploadRequest('/api/sampler/files/samples/'+encodeURIComponent(path)+'?overwrite='+(sf2Editor.overwrite?'1':'0'),new Blob([bytes],{type:'application/vnd.instachord.ktsynth'}),()=>{});}
      sf2Editor.savedPath='/sampler/samples/'+path;sf2Editor.overwrite=false;
    } catch(err) { sf2Editor.error=friendlySynthError(err);if(/already exists|same name|同名/i.test(err.message))sf2Editor.overwrite=true; }
    finally { sf2Editor.busy=false;renderSamples(); }
  }
  function downloadSf2Output() {
    try {const bytes=sf2Editor.output||buildSf2Output(),a=el('a',{href:URL.createObjectURL(new Blob([bytes],{type:'application/vnd.instachord.ktsynth'})),download:safeSynthName(sf2Editor.name)+'.ktsynth'});a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);renderSamples();}
    catch(err){sf2Editor.error=err.message;renderSamples();}
  }
  function chooseSynthMethod(method) {
    sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method};renderSamples();
  }
  function synthMethodPanel() {
    return el('section',{class:'panel sf2-converter'},el('h2',{},'KANTANシンセを作る'),el('p',{class:'notice'},'元の音源ファイルは外部へ送信せず、このブラウザ内で処理します。'),el('div',{class:'synth-methods'},el('button',{class:'primary',onclick:()=>chooseSynthMethod('sf2')},el('strong',{},'SoundFont（.sf2）から作る'),el('small',{},'音色と使用サウンドを選びます')),el('button',{class:'primary',onclick:()=>chooseSynthMethod('audio')},el('strong',{},'WAV／MP3から作る'),el('small',{},'音の最初から最後までを使います'))));
  }
  async function loadAudioSource(file) {
    sf2Player.stop(true);sf2Editor={...newSf2Editor(),open:true,method:'audio',file,busy:true,name:safeSynthName(file.name.replace(/\.(wav|mp3)$/i,''))};renderSamples();
    try {
      if(!/\.(wav|mp3)$/i.test(file.name))throw new Error('WAVまたはMP3ファイルを選んでください。');
      const audio=await decodeAudioFile(file);
      if(audio.duration>20)throw new Error(`入力は20秒以内にしてください（現在 ${audio.duration.toFixed(1)}秒）。短い素材を用意してください。`);
      sf2Editor.audio=audio;sf2Editor.pitchSuggestion=audio.suggestion;
      const suggestion=audio.suggestion;
      sf2Editor.pitchNote=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false?suggestion.note:60;
      sf2Editor.tuneOffset=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false?suggestion.tuneCents||0:0;
    } catch(err) { sf2Editor.error=err.message; }
    finally { sf2Editor.busy=false;renderSamples(); }
  }
  function setPitchPart(part,value) {
    const current=sf2Editor.pitchNote;
    const pitchClass=part==='name'?Number(value):current%12;
    const octave=part==='octave'?Number(value):Math.floor(current/12)-1;
    sf2Editor.pitchNote=Math.max(0,Math.min(127,(octave+1)*12+pitchClass));sf2Editor.pitchConfirmed=false;sf2Editor.output=null;renderSamples();
  }
  function buildAudioOutput() {
    if(!sf2Editor.audio)throw new Error('音声ファイルを選んでください。');
    if(!sf2Editor.pitchConfirmed)throw new Error('元の音程を確認してください。');
    const name=safeSynthName(sf2Editor.name);if(!name)throw new Error('音色名を入力してください。');
    const pcm=resamplePcm(sf2Editor.audio.pcm,sf2Editor.audio.sampleRate,sf2Editor.sampleRate);
    const bytes=encodeKtSynth(pcm,{name,sampleRate:sf2Editor.sampleRate,startFrame:0,endFrameExclusive:pcm.length,loopStartFrame:0,loopEndFrameExclusive:0,loopCrossfadeFrames:0,sustainMode:0,attackMs:0,releaseMs:120,tuneCents:Math.max(-100,Math.min(100,Math.round(sf2Editor.tuneOffset||0))),defaultGainQ8:256,rootNote:sf2Editor.pitchNote});
    parseKtSynth(bytes);sf2Editor.output=bytes;return bytes;
  }
  async function saveAudioToSd() {
    sf2Editor.busy=true;sf2Editor.error='';renderSamples();
    try {
      const bytes=buildAudioOutput(),filename=safeSynthName(sf2Editor.name)+'.ktsynth',path='Synth/'+filename;
      if(PREVIEW){await sleep(350);const at=previewFiles.samples.findIndex(x=>x.name===filename&&x.folder==='Synth'),entry={name:filename,size:bytes.length,folder:'Synth'};if(at>=0&&!sf2Editor.overwrite)throw new Error('a KANTAN Synth tone with the same name already exists');if(at>=0)previewFiles.samples[at]=entry;else previewFiles.samples.push(entry);}
      else {await ensureSynthFolder();await uploadRequest('/api/sampler/files/samples/'+encodeURIComponent(path)+'?overwrite='+(sf2Editor.overwrite?'1':'0'),new Blob([bytes],{type:'application/vnd.instachord.ktsynth'}),()=>{});}
      sf2Editor.savedPath='/sampler/samples/'+path;sf2Editor.overwrite=false;
    } catch(err) {sf2Editor.error=friendlySynthError(err);if(/already exists|same name|同名/i.test(err.message))sf2Editor.overwrite=true;}
    finally {sf2Editor.busy=false;renderSamples();}
  }
  function downloadAudioOutput() {
    try {const bytes=sf2Editor.output||buildAudioOutput(),a=el('a',{href:URL.createObjectURL(new Blob([bytes],{type:'application/vnd.instachord.ktsynth'})),download:safeSynthName(sf2Editor.name)+'.ktsynth'});a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);renderSamples();}
    catch(err){sf2Editor.error=friendlySynthError(err);renderSamples();}
  }
  function audioConverterPanel() {
    const file=el('input',{type:'file',accept:'.wav,.mp3,audio/wav,audio/mpeg'});file.addEventListener('change',()=>file.files[0]&&loadAudioSource(file.files[0]));
    const panel=el('section',{class:'panel sf2-converter'},el('div',{class:'sf2-launcher'},el('h2',{},'WAV／MP3から作る'),el('button',{onclick:()=>chooseSynthMethod(null)},'入力方法に戻る')),el('p',{class:'notice'},'ファイルはブラウザ内でPCMに変換し、先頭から最後までを鳴らします。ループは作成しません。'),sf2Field('音声ファイル',file,sf2Editor.file?sf2Editor.file.name:'20秒以内のWAVまたはMP3'));
    if(sf2Editor.busy)panel.append(el('p',{class:'sf2-working','aria-live':'polite'},'ブラウザ内で解析中…'));if(sf2Editor.error)panel.append(el('p',{class:'error',role:'alert'},sf2Editor.error));if(!sf2Editor.audio)return panel;
    const suggestion=sf2Editor.pitchSuggestion,usable=suggestion&&suggestion.note!==undefined&&suggestion.reliable!==false;
    panel.append(el('section',{class:'pitch-confirm','aria-labelledby':'source-pitch-heading'},el('h3',{id:'source-pitch-heading'},'元の音程'),usable?el('p',{class:'warning'},`自動検出：${midiNoteName(suggestion.note)}（確認してください） · ${suggestion.source}${suggestion.source==='音声解析'?` · 信頼度 ${Math.round(suggestion.confidence*100)}%`:''}`):el('p',{class:'warning'},'音程を判定できませんでした。元の音程を選んでください。'),pitchControls(),el('button',{class:sf2Editor.pitchConfirmed?'':'primary',onclick:()=>{sf2Editor.pitchConfirmed=true;sf2Editor.output=null;renderSamples();}},sf2Editor.pitchConfirmed?`✓ ${midiNoteName(sf2Editor.pitchNote)}で使います`:sf2Editor.pitchNote===60&&!usable?'C4として使う':'この音程で使う')));
    const name=el('input',{type:'text',value:sf2Editor.name,maxlength:80,autocomplete:'off'});name.addEventListener('input',()=>{sf2Editor.name=name.value;sf2Editor.output=null;sf2Editor.overwrite=false;});panel.append(sf2Field('音色名',name,'保存名: '+(safeSynthName(sf2Editor.name)||'（未入力）')+'.ktsynth'));
    const rate=el('select',{},[18000,24000,32000,48000].map(n=>el('option',{value:n,selected:n===sf2Editor.sampleRate?'':null},n/1000+' kHz')));rate.addEventListener('change',()=>{sf2Editor.sampleRate=Number(rate.value);sf2Editor.output=null;renderSamples();});
    const frames=Math.round(sf2Editor.audio.pcm.length*sf2Editor.sampleRate/sf2Editor.audio.sampleRate),estimated=estimateKtSynthBytes(sf2Editor.name||'tone',frames,false),tooLarge=estimated>KTSYNTH_MAX_BYTES;
    panel.append(el('details',{class:'sf2-advanced'},el('summary',{},'詳細設定'),el('div',{class:'sf2-grid'},sf2Field('出力サンプルレート',rate),sf2Number('音程補正 (cent)','tuneOffset',-100,100),el('div',{class:'sf2-size'},el('span',{},'再生時間'),el('strong',{},sf2Editor.audio.duration.toFixed(1)+' 秒')),el('div',{class:'sf2-size'},el('span',{},'推定出力サイズ'),el('strong',{class:tooLarge?'danger-text':''},Math.ceil(estimated/1024)+' KB')))));
    if(/\.mp3$/i.test(sf2Editor.file.name))panel.append(el('p',{class:'notice'},'MP3はPCMへデコードするため、出力サイズは元のMP3より大きくなります。'));
    if(tooLarge)panel.append(el('p',{class:'error',role:'alert'},'出力が2 MiBを超えます。短い素材を使うか、出力サンプルレートを下げてください。'));
    panel.append(el('div',{class:'actions'},el('button',{onclick:()=>sf2Player.play(sf2Editor.audio.pcm,{sampleRate:sf2Editor.audio.sampleRate,previewNote:60,rootNote:60,tuneCents:0,sustainMode:'off',loopStart:0,loopEnd:0,attackMs:0,attenuationCb:0})},'元の音を試聴'),el('button',{onclick:()=>sf2Player.stop(true)},'停止')));
    panel.append(el('div',{class:'sf2-save-row'},el('button',{class:'primary',disabled:!sf2Editor.pitchConfirmed||!safeSynthName(sf2Editor.name)||tooLarge||sf2Editor.busy?'':null,onclick:saveAudioToSd},sf2Editor.overwrite?'上書きしてSDカードに保存':'SDカードに保存'),el('button',{disabled:!sf2Editor.pitchConfirmed||tooLarge?'':null,onclick:downloadAudioOutput},'パソコンに保存')));return panel;
  }
  function pitchControls() {
    const note=sf2Editor.pitchNote,names=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
    const pitch=el('select',{'aria-label':'音名'},names.map((name,index)=>el('option',{value:index,selected:index===note%12?'':null},name)));pitch.addEventListener('change',()=>setPitchPart('name',pitch.value));
    const octave=el('select',{'aria-label':'オクターブ'},Array.from({length:11},(_,index)=>index-1).map(value=>el('option',{value,selected:value===Math.floor(note/12)-1?'':null},value)));octave.addEventListener('change',()=>setPitchPart('octave',octave.value));
    return el('div',{class:'pitch-controls'},el('label',{},'音名',pitch),el('label',{},'オクターブ',octave));
  }
  function sf2ConverterPanel() {
    if(sf2Editor.savedPath)return el('section',{class:'panel sf2-converter','aria-live':'polite'},el('h2',{},'SDカードに保存しました'),el('p',{class:'success'},sf2Editor.savedPath),el('p',{},'本体の Melody／Chord／Bass の「KANTAN Synth」から選べます。'),el('div',{class:'actions'},el('button',{onclick:sf2Editor.method==='audio'?downloadAudioOutput:downloadSf2Output},'パソコンにも保存'),el('button',{class:'primary',onclick:()=>{sf2Editor=newSf2Editor();sf2Editor.open=true;renderSamples();}},'別の音色を作る')));
    if(!sf2Editor.method)return synthMethodPanel();
    if(sf2Editor.method==='audio')return audioConverterPanel();
    const file=el('input',{type:'file',accept:'.sf2,application/octet-stream'});file.addEventListener('change',()=>file.files[0]&&loadSoundFont(file.files[0]));
    const panel=el('section',{class:'panel sf2-converter'},el('div',{class:'sf2-launcher'},el('h2',{},'SoundFontから作る'),el('button',{onclick:()=>chooseSynthMethod(null)},'入力方法に戻る')),el('p',{class:'notice'},'SoundFontはこのブラウザ内だけで解析され、本体や外部サーバーへ送信されません。'),sf2Field('SoundFont',file,sf2Editor.file?sf2Editor.file.name:'SoundFont 2（.sf2）を選択'));
    if(sf2Editor.busy)panel.append(el('p',{class:'sf2-working','aria-live':'polite'},'処理中…'));if(sf2Editor.error)panel.append(el('p',{class:'error','role':'alert'},sf2Editor.error));if(!sf2Editor.sf2)return panel;
    const program=el('select',{},sf2Editor.programs.length>1?el('option',{value:'',selected:sf2Editor.programIndex===null?'':null},'音色を選んでください'):null,sf2Editor.programs.map(p=>el('option',{value:p.index,selected:p.index===sf2Editor.programIndex?'':null},p.label)));
    program.addEventListener('change',()=>{sf2Editor.programIndex=program.value===''?null:Number(program.value);sf2Editor.volumeCustomized=false;const p=sf2Editor.programs.find(x=>x.index===sf2Editor.programIndex);if(p)sf2Editor.name=p.name;updateSf2Regions(false);renderSamples();});panel.append(sf2Field('音色',program,`${sf2Editor.sf2.name} · SF2 ${sf2Editor.sf2.version}`));if(sf2Editor.programIndex===null)return panel;
    panel.append(el('h3',{},'使用するサウンド'));if(sf2Editor.regions.length>1)panel.append(el('p',{class:'warning','role':'status'},'この音色には複数のサウンドが重ねられています。KANTANシンセでは1つだけ使用します。試聴して選んでください。元の音色とは聴こえ方が変わる場合があります。'));
    if(!sf2Editor.regions.length)panel.append(el('p',{class:'error'},'現在の基準音と強さに該当するサウンドがありません。詳細設定を変更してください。'));else panel.append(el('fieldset',{class:'sf2-candidates'},el('legend',{},sf2Editor.regions.length>1?'1つ選んでください':'使用されるサウンド'),sf2Editor.regions.map(sf2Candidate)));
    const region=selectedSf2Region();panel.append(el('div',{class:'actions'},el('button',{disabled:region?null:'',onclick:()=>previewSf2Region()},'選んだサウンドを試聴'),el('button',{onclick:()=>sf2Player.stop(false,(sf2Editor.releaseMs===null?(region?region.releaseMs:0):sf2Editor.releaseMs)||0)},'停止')));if(region)panel.append(sf2VolumeControl(region));
    const name=el('input',{type:'text',value:sf2Editor.name,maxlength:80,autocomplete:'off'});name.addEventListener('input',()=>{sf2Editor.name=name.value;sf2Editor.output=null;sf2Editor.overwrite=false;});panel.append(sf2Field('音色名',name,'保存名: '+(safeSynthName(sf2Editor.name)||'（未入力）')+'.ktsynth'));
    const rate=el('select',{},[18000,24000,32000,48000].map(n=>el('option',{value:n,selected:n===sf2Editor.sampleRate?'':null},n/1000+' kHz')));rate.addEventListener('change',()=>{sf2Editor.sampleRate=Number(rate.value);sf2Editor.output=null;renderSamples();});const sourceFrames=region?(region.sustainMode==='loop'?region.loopEnd-region.start:region.end-region.start):0,targetFrames=region?Math.round(sourceFrames*sf2Editor.sampleRate/region.sampleRate):0,estimated=region?estimateKtSynthBytes(sf2Editor.name||'tone',targetFrames,region.sustainMode==='loop'):0,tooLarge=estimated>KTSYNTH_MAX_BYTES,tooLong=region&&sourceFrames/region.sampleRate>20;
    panel.append(el('details',{class:'sf2-advanced'},el('summary',{},'詳細設定'),el('div',{class:'sf2-grid'},sf2Number('基準音','key',0,127,'候補も更新されます'),sf2Number('代表Velocity','velocity',1,127,'候補も更新されます'),sf2Field('サンプルレート',rate),sf2Number('Loop crossfade (ms)','crossfadeMs',0,1000),sf2Number('Attack (ms)','attackMs',0,5000,'空欄時はSoundFont値'),sf2Number('Release (ms)','releaseMs',10,2000,'空欄時はSoundFont値'),sf2Number('音程補正 (cent)','tuneOffset',-100,100),el('div',{class:'sf2-size'},el('span',{},'推定出力サイズ'),el('strong',{class:estimated>KTSYNTH_MAX_BYTES?'danger-text':''},estimated?Math.ceil(estimated/1024)+' KB':'—')))));
    if(tooLong||tooLarge)panel.append(el('p',{class:'error',role:'alert'},tooLong?'このサウンドは20秒を超えます。短いサウンドを選んでください。':'出力が2 MiBを超えます。短いサウンドを選ぶか、サンプルレートを下げてください。'));
    panel.append(el('div',{class:'sf2-save-row'},el('button',{class:'primary',disabled:!region||!safeSynthName(sf2Editor.name)||sf2Editor.busy||tooLong||tooLarge?'':null,onclick:saveSf2ToSd},sf2Editor.overwrite?'上書きしてSDカードに保存':'SDカードに保存'),el('button',{disabled:region&&!tooLong&&!tooLarge?null:'',onclick:downloadSf2Output},'パソコンに保存')));return panel;
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
