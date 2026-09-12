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
