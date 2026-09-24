// Вкладка «Обучение / Training» web UI (см. training_service.hpp для API).
// Подключается в kPage (main.cpp) после хелперов страницы: использует el(),
// tr(), st, cfgObj, cur, render(), send(), load(), selectionCanvas.
// Чистые функции в начале файла не трогают DOM — их проверяет
// tests/training_ui.test.cjs (извлекает блок TRAINJS и гоняет в node:vm).
R"TRAINJS(
// --- чистые хелперы (без DOM) ---
function rectFromPoints(a,b){return{x:Math.min(a.x,b.x),y:Math.min(a.y,b.y),w:Math.abs(b.x-a.x),h:Math.abs(b.y-a.y)};}
function clampRoi(roi){
  const r=roi.map(v=>Math.min(1,Math.max(0,Number(v)||0)));
  r[2]=Math.max(0.01,Math.min(r[2],1-r[0]));r[3]=Math.max(0.01,Math.min(r[3],1-r[1]));
  return r.map(v=>Math.round(v*10000)/10000);
}
function normalizeRect(rect,width,height){
  if(!rect||!width||!height)return null;
  return clampRoi([rect.x/width,rect.y/height,rect.w/width,rect.h/height]);
}
function rectToBox(rect,width,height){
  const n=normalizeRect(rect,width,height);if(!n)return null;
  return{cx:Math.round((n[0]+n[2]/2)*1e6)/1e6,cy:Math.round((n[1]+n[3]/2)*1e6)/1e6,w:n[2],h:n[3]};
}
function yoloLabelLines(boxes){return boxes.filter(b=>b.w>0&&b.h>0).map(b=>[b.class_id|0,b.cx,b.cy,b.w,b.h].map((v,i)=>i?v.toFixed(6):v).join(' ')).join('\n');}
function formatPrediction(objects,lang){
  if(!objects||!objects.length)return lang==='ru'?'нет уверенного результата':'no confident result';
  return objects.map(o=>(o.class_name||('class'+o.id))+' '+Math.round((o.confidence||0)*100)+'%').join(', ')+(objects.length>1?(lang==='ru'?' ('+objects.length+' обл.)':' ('+objects.length+' regions)'):'');
}
function formatBytes(bytes){bytes=Number(bytes)||0;return bytes<1024*1024?Math.round(bytes/1024)+' KiB':(bytes/1024/1024).toFixed(1)+' MiB';}
function trainerStateText(t,lang){
  const p=t.progress||{},names={idle:['не запускалось','idle'],running:['идёт обучение','training'],done:['готово','done'],failed:['ошибка','failed'],stopped:['остановлено','stopped']};
  let text=(names[t.state]||[t.state,t.state])[lang==='ru'?0:1];
  if(p.epoch)text+=': '+(lang==='ru'?'эпоха ':'epoch ')+p.epoch+'/'+p.epochs+', loss '+p.loss+', acc '+p.acc+(p.val_acc!==undefined?', val '+p.val_acc:'');
  if(t.state==='failed'&&t.error)text+=' — '+t.error;
  return text;
}

// --- локализация вкладки ---
Object.assign(uiText.ru,{trainingTab:'Обучение',trainingTitle:'Обучение нейросети',trainingGuide:'1. Выберите область классификации. 2. Заведите классы и наберите примеры с камеры. 3. Обучите модель и активируйте её.',
  regionTitle:'Область классификации',regionWhole:'весь кадр',regionRoi:'прямоугольник (ROI)',regionBlob:'области от blob-детектора',drawRoi:'Нарисовать ROI на видео',setRoi:'Взять ROI из выделения',roiHint:'Нажмите «Нарисовать ROI», обведите прямоугольник на исходном видео и нажмите «Взять ROI». Затем «Применить» или «Сохранить».',
  blobHint:'Рамки берутся у включённых одноцветных шаблонов blob. Выберите шаблоны или оставьте все.',blobNeedMode:'Для съёмки областей blob включите режим «Цветовые blobs».',switchBlob:'Включить blob-детекцию',
  classesTitle:'Классы и примеры',className:'Имя класса (латиница)',addClass:'Добавить класс',snapshot:'Снимок',burst:'Серия',burstCount:'кадров',burstInterval:'мс между кадрами',showSamples:'Примеры',deleteClass:'Удалить класс',confirmDelete:'Удалить класс и все его примеры?',deleteSelected:'Удалить выбранные',closeSamples:'Скрыть примеры',noSamples:'Примеров пока нет',stopCapture:'Стоп',
  trainTitle:'Обучение на устройстве',epochs:'Эпох',train:'Обучить',stopTrain:'Остановить',checkTrainer:'Проверить тренер',trainerReady:'Тренер готов',trainerMissing:'Тренер недоступен',trainerFallback:'Скачайте датасет, обучите на ПК и загрузите ONNX ниже.',
  modelsTitle:'Модели',activate:'Активировать',active:'активна',noModels:'Моделей пока нет',uploadModel:'Загрузить ONNX',uploadKind:'Тип',uploadClasses:'Классы через запятую',exportDataset:'Скачать датасет классификатора (zip)',exportYolo:'Скачать датасет YOLO (zip)',
  predictionTitle:'Текущий результат',yoloTitle:'Разметка YOLO (обучение на ПК)',yoloGuide:'Снимите кадры, задайте классы, для каждого кадра обведите объекты рамками и сохраните. Затем скачайте zip и обучите на ПК: board/raspberry-cm5/train_yolo.py.',
  yoloClasses:'Классы YOLO через запятую',yoloSaveClasses:'Сохранить классы',yoloCapture:'Снять кадры',yoloFrames:'Кадры',yoloLabel:'Разметить',yoloClass:'Класс рамки',yoloSave:'Сохранить разметку',yoloDone:'Завершить',yoloUndo:'Убрать последнюю рамку',yoloDelete:'Удалить кадр',yoloHint:'Обводите объекты прямоугольниками на левом изображении.',labeled:'размечен',notLabeled:'без разметки'});
Object.assign(uiText.en,{trainingTab:'Training',trainingTitle:'Neural network training',trainingGuide:'1. Choose the classification region. 2. Create classes and capture samples from the camera. 3. Train a model and activate it.',
  regionTitle:'Classification region',regionWhole:'whole frame',regionRoi:'rectangle (ROI)',regionBlob:'blob detector regions',drawRoi:'Draw ROI on video',setRoi:'Use ROI from selection',roiHint:'Click "Draw ROI", drag a rectangle on the source video and click "Use ROI". Then Apply or Save.',
  blobHint:'Boxes come from the enabled single-color blob patterns. Pick patterns or keep all.',blobNeedMode:'Switch to blob detection to capture blob regions.',switchBlob:'Switch to blob detection',
  classesTitle:'Classes and samples',className:'Class name (latin)',addClass:'Add class',snapshot:'Snapshot',burst:'Burst',burstCount:'frames',burstInterval:'ms between frames',showSamples:'Samples',deleteClass:'Delete class',confirmDelete:'Delete the class and all its samples?',deleteSelected:'Delete selected',closeSamples:'Hide samples',noSamples:'No samples yet',stopCapture:'Stop',
  trainTitle:'On-device training',epochs:'Epochs',train:'Train',stopTrain:'Stop',checkTrainer:'Check trainer',trainerReady:'Trainer is ready',trainerMissing:'Trainer unavailable',trainerFallback:'Download the dataset, train on a PC and upload the ONNX below.',
  modelsTitle:'Models',activate:'Activate',active:'active',noModels:'No models yet',uploadModel:'Upload ONNX',uploadKind:'Kind',uploadClasses:'Comma-separated classes',exportDataset:'Download classifier dataset (zip)',exportYolo:'Download YOLO dataset (zip)',
  predictionTitle:'Live prediction',yoloTitle:'YOLO labeling (train on a PC)',yoloGuide:'Capture frames, define classes, draw boxes on each frame and save. Then download the zip and train on a PC: board/raspberry-cm5/train_yolo.py.',
  yoloClasses:'Comma-separated YOLO classes',yoloSaveClasses:'Save classes',yoloCapture:'Capture frames',yoloFrames:'Frames',yoloLabel:'Label',yoloClass:'Box class',yoloSave:'Save labels',yoloDone:'Finish',yoloUndo:'Remove last box',yoloDelete:'Delete frame',yoloHint:'Drag rectangles around objects on the left image.',labeled:'labeled',notLabeled:'unlabeled'});
Object.assign(labels,{region_mode:'Classification region: whole | roi | blob',roi:'ROI [x, y, w, h], frame fractions',blob_pattern_ids:'Blob pattern IDs for regions (empty = all)',crop_padding:'Region padding, fraction of box size',max_regions:'Maximum classified regions per frame',score_threshold:'Score threshold, 0–1',input_size:'Neural network input size, px',model_rknn:'RKNN model path (MTV3)',resize_size:'Resize before center crop, px',center_crop:'Center crop instead of stretch',swap_rb:'Swap R and B channels',
  dataset_dir:'Dataset directory',models_dir:'Models directory',python:'Python interpreter for training',classifier_script:'Classifier training script',classifier_epochs:'Default classifier epochs',classifier_image_size:'Classifier image size, px',max_dataset_mb:'Dataset size limit, MiB',max_samples_per_class:'Samples per class limit',max_burst:'Maximum frames per burst',min_burst_interval_ms:'Minimum burst interval, ms',max_upload_mb:'Model upload limit, MiB',require_admin_token:'Require admin token for training API'});
Object.assign(hints,{region_mode:'whole classifies the entire frame, roi a fixed rectangle, blob every box found by the single-color blob detector.',roi:'Normalized rectangle used by the roi region mode and by ROI dataset capture.',blob_pattern_ids:'Only these single-color blob pattern IDs supply regions; an empty list uses all enabled patterns.',crop_padding:'Extra margin added around each blob box before classification and dataset capture; keep it equal to the value used during training.',max_regions:'Largest regions are classified first; the rest are skipped to bound inference time.',
  dataset_dir:'Where captured samples are stored, relative to config.json.',models_dir:'Where trained and uploaded ONNX models are stored, relative to config.json.',python:'Interpreter with PyTorch used to train the classifier on the device.',classifier_script:'Path to simple_classifier.py on the device.',require_admin_token:'When enabled, /training endpoints need the system_admin token like /admin.'});
Object.assign(ruLabels,{classification:'Классификация',training:'Обучение',region_mode:'Область классификации: whole | roi | blob',roi:'ROI [x, y, w, h], доли кадра',blob_pattern_ids:'ID шаблонов blob для областей (пусто = все)',crop_padding:'Отступ вокруг области, доля размера',max_regions:'Максимум областей за кадр',score_threshold:'Порог уверенности, 0–1',input_size:'Размер входа сети, пикс',model_onnx:'Путь к модели ONNX',model_rknn:'Путь к модели RKNN (MTV3)',class_names:'Имена классов',class_names_file:'Файл имён классов',
  dataset_dir:'Каталог датасета',models_dir:'Каталог моделей',python:'Интерпретатор Python для обучения',classifier_script:'Скрипт обучения классификатора',classifier_epochs:'Эпох по умолчанию',classifier_image_size:'Размер картинки классификатора, пикс',max_dataset_mb:'Лимит датасета, МиБ',max_samples_per_class:'Лимит примеров на класс',max_burst:'Максимум кадров в серии',min_burst_interval_ms:'Минимальный интервал серии, мс',max_upload_mb:'Лимит загрузки модели, МиБ',require_admin_token:'Требовать админ-токен для API обучения',jpeg_quality:'Качество JPEG'});

// --- состояние вкладки ---
let trainingState=null,trainingTimer=null,trainingBusy=false,trainingSignature='';
const trainingUi={regionMode:null,blobIds:null,newClass:'',burstCount:30,burstInterval:100,epochs:null,samplesClass:null,samples:[],selected:{},uploadKind:'classifier',uploadClasses:'',yoloCount:20,yoloClassText:null,yoloBoxClass:0,yoloImages:[]};
const labeling={active:false,file:null,boxes:[],classes:[],streamPaused:false};
function trainingClassification(){cfgObj.classification=cfgObj.classification||{region_mode:'whole',roi:[0.25,0.25,0.5,0.5],blob_pattern_ids:[],crop_padding:0.1,max_regions:8,class_names:[],input_size:64,score_threshold:0.5,model_onnx:''};return cfgObj.classification;}
function trainingRegionMode(){if(trainingUi.regionMode===null)trainingUi.regionMode=trainingClassification().region_mode||'whole';return trainingUi.regionMode;}
function trainingFetch(path,options){return fetch(path,Object.assign({cache:'no-store',headers:admHeaders()},options||{})).then(async r=>{const text=await r.text();let json=null;try{json=JSON.parse(text);}catch(e){}if(!r.ok||(json&&json.error))throw new Error((json&&json.error)||text||('HTTP '+r.status));return json===null?text:json;});}
function trainingPost(path,body){return trainingFetch(path,{method:'POST',body:JSON.stringify(body||{})});}
function trainingError(error){st.textContent=(language==='ru'?'Ошибка: ':'Error: ')+error.message;}

// поллинг статуса: полная перерисовка только при изменении структуры,
// живые поля (счётчики, прогресс, лог) обновляются по id
function trainingPoll(){
  clearTimeout(trainingTimer);
  if(paramTab!=='training'||trainingBusy)return;
  trainingBusy=true;
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
  trainingFetch('/training/status',{signal:controller.signal}).then(status=>{
    trainingState=status;
    const signature=JSON.stringify([status.dataset.classes.map(c=>c.name),status.models.map(m=>m.file+m.active),status.trainer.state,status.trainer_check.checked,status.active,status.yolo.classes,status.yolo.images]);
    if(signature!==trainingSignature){trainingSignature=signature;render();}else updateTrainingLive();
    if(cur==='classification')trainingFetch('/metadata/last').then(updatePrediction).catch(()=>{});
  }).catch(error=>{const box=document.getElementById('trainStatusLine');if(box)box.textContent=error.name==='AbortError'?'timeout':error.message;})
  .finally(()=>{clearTimeout(timeout);trainingBusy=false;const active=trainingState&&(trainingState.capture.active||trainingState.trainer.state==='running');if(paramTab==='training')trainingTimer=setTimeout(trainingPoll,active?700:1500);});
}
function updatePrediction(meta){const box=document.getElementById('trainPrediction');if(!box)return;box.textContent=formatPrediction(meta.objects,language);box.style.color=meta.objects.length?'#7f7':'#aaa';}
function updateTrainingLive(){
  const s=trainingState;if(!s)return;
  s.dataset.classes.forEach(c=>{const cell=document.getElementById('trainCount_'+c.name);if(cell)cell.textContent=c.count;});
  const cap=document.getElementById('trainCaptureStatus');
  if(cap){const c=s.capture;cap.textContent=c.active?((language==='ru'?'Съёмка ':'Capturing ')+c.class+': '+c.done+'/'+c.total+(language==='ru'?', сохранено ':', saved ')+c.saved):(c.error?(language==='ru'?'Ошибка съёмки: ':'Capture error: ')+c.error:(c.saved?(language==='ru'?'Сохранено ':'Saved ')+c.saved+(language==='ru'?' примеров':' samples'):''));cap.style.color=c.error?'#fbb':'#bbb';}
  const t=s.trainer,bar=document.getElementById('trainProgress'),text=document.getElementById('trainProgressText');
  if(bar){const p=t.progress||{};bar.max=p.epochs||1;bar.value=p.epoch||0;bar.style.display=t.state==='running'||p.epoch?'':'none';}
  if(text)text.textContent=trainerStateText(t,language);
  const trainButton=document.getElementById('trainStart'),stopButton=document.getElementById('trainStop');
  if(trainButton)trainButton.disabled=t.state==='running'||s.capture.active;if(stopButton)stopButton.disabled=t.state!=='running';
  const size=document.getElementById('trainDatasetSize');if(size)size.textContent=formatBytes(s.dataset.bytes)+' / '+formatBytes(s.dataset.max_bytes)+(language==='ru'?', свободно ':', free ')+formatBytes(s.dataset.free_bytes);
  const log=document.getElementById('trainLog');
  if(log&&(t.state==='running'||log.dataset.state!==t.state)){log.dataset.state=t.state;trainingFetch('/training/train/log').then(l=>{const atBottom=log.scrollHeight-log.scrollTop-log.clientHeight<30;log.textContent=l.entries.map(e=>e.line).join('\n');if(atBottom)log.scrollTop=log.scrollHeight;}).catch(()=>{});}
}

// --- построение панели ---
function renderTrainingPanel(parent){
  const s=trainingState;
  const panel=el('div','linkedEditor');panel.id='trainPanel';
  const title=el('h4');title.textContent=tr('trainingTitle');panel.appendChild(title);
  const guide=el('div','hint');guide.textContent=tr('trainingGuide');panel.appendChild(guide);
  const line=el('div','hint');line.id='trainStatusLine';panel.appendChild(line);
  if(!s){line.textContent=language==='ru'?'Загрузка…':'Loading…';parent.appendChild(panel);trainingPoll();return;}
  panel.appendChild(renderRegionSection(s));
  panel.appendChild(renderClassesSection(s));
  panel.appendChild(renderTrainSection(s));
  panel.appendChild(renderModelsSection(s));
  panel.appendChild(renderYoloSection(s));
  parent.appendChild(panel);
  updateTrainingLive();
  if(!trainingTimer)trainingPoll();
}
function section(titleKey,open){const d=el('details');d.open=open!==false;const sm=el('summary');sm.textContent=tr(titleKey);d.appendChild(sm);const body=el('div','body');d.appendChild(body);return[d,body];}
function button(text,onclick,cls){const b=el('button',cls);b.textContent=text;b.onclick=onclick;return b;}
function numberInput(value,min,max,onchange,width){const i=el('input');i.type='number';i.min=min;i.max=max;i.value=value;i.style.width=(width||70)+'px';i.oninput=()=>onchange(Number(i.value));return i;}

function renderRegionSection(s){
  const[d,body]=section('regionTitle');const mode=trainingRegionMode();
  const row=el('div','row');row.style.flexWrap='wrap';
  [['whole','regionWhole'],['roi','regionRoi'],['blob','regionBlob']].forEach(([value,key])=>{const label=el('label');label.style.flex='initial';label.style.cursor='pointer';const radio=el('input');radio.type='radio';radio.name='trainRegion';radio.value=value;radio.checked=mode===value;
    radio.onchange=()=>{trainingUi.regionMode=value;trainingClassification().region_mode=value;if(value!=='roi'&&selectionTool==='rect'&&!labeling.active)selectionTool='polygon';render();};label.appendChild(radio);label.appendChild(document.createTextNode(' '+tr(key)));row.appendChild(label);});
  body.appendChild(row);
  if(mode==='roi'){
    const roi=clampRoi(trainingClassification().roi||[0.25,0.25,0.5,0.5]);trainingClassification().roi=roi;
    const hint=el('div','hint');hint.textContent=tr('roiHint');body.appendChild(hint);
    const roiRow=el('div','row');const text=el('span');text.id='trainRoiText';text.textContent='ROI: ['+roi.join(', ')+']';text.style.color='#fb0';roiRow.appendChild(text);body.appendChild(roiRow);
    const draw=button(tr('drawRoi'),()=>{selectionTool=selectionTool==='rect'&&!labeling.active?'polygon':'rect';selectionRect=null;selectionPolygon=null;render();},selectionTool==='rect'&&!labeling.active?'selecting':'');body.appendChild(draw);
    body.appendChild(button(tr('setRoi'),setRoiFromSelection));
    body.appendChild(button(tr('apply'),()=>send('/apply')));body.appendChild(button(tr('save'),()=>send('/config')));
  }else if(mode==='blob'){
    const hint=el('div','hint');hint.textContent=tr('blobHint');body.appendChild(hint);
    const patterns=((cfgObj.blob_detection||{}).one_color_patterns||[]);
    if(trainingUi.blobIds===null)trainingUi.blobIds=(trainingClassification().blob_pattern_ids||[]).slice();
    const list=el('div','row');list.style.flexWrap='wrap';
    patterns.forEach(pattern=>{const label=el('label');label.style.flex='initial';const box=el('input');box.type='checkbox';box.checked=!trainingUi.blobIds.length||trainingUi.blobIds.includes(pattern.id);
      box.onchange=()=>{const all=patterns.map(p=>p.id);let ids=trainingUi.blobIds.length?trainingUi.blobIds.slice():all.slice();ids=box.checked?ids.concat([pattern.id]):ids.filter(id=>id!==pattern.id);ids=[...new Set(ids)];trainingUi.blobIds=ids.length===all.length?[]:ids;trainingClassification().blob_pattern_ids=trainingUi.blobIds;render();};
      label.appendChild(box);const sw=el('span','swatch');if(Array.isArray(pattern.lower_range)&&pattern.lower_range.length===3&&typeof yCrCbToHex==='function')sw.style.background=yCrCbToHex(pattern.lower_range.map((v,i)=>(v+(pattern.upper_range||pattern.lower_range)[i])/2));label.appendChild(document.createTextNode(' '));label.appendChild(sw);label.appendChild(document.createTextNode((language==='ru'?'шаблон ':'pattern ')+pattern.id+(pattern.enabled===false?(language==='ru'?' (откл.)':' (off)'):'')));list.appendChild(label);});
    body.appendChild(list);
    const blobReady=cur==='blob_detection'||(cur==='classification'&&(s.active||{}).region_mode==='blob');
    if(!blobReady){const need=el('div','hint');need.textContent=tr('blobNeedMode');need.style.color='#fb0';body.appendChild(need);body.appendChild(button(tr('switchBlob'),()=>fetch('/mode/blob_detection').then(r=>r.text()).then(t=>{st.textContent=t;load();})));}
    body.appendChild(button(tr('apply'),()=>send('/apply')));body.appendChild(button(tr('save'),()=>send('/config')));
  }
  const pred=el('div','row');const predLabel=el('label');predLabel.textContent=tr('predictionTitle');pred.appendChild(predLabel);const value=el('span');value.id='trainPrediction';value.textContent=cur==='classification'?'…':(language==='ru'?'(режим «Классификация» не активен)':'(classification mode is not active)');pred.appendChild(value);body.appendChild(pred);
  return d;
}
function setRoiFromSelection(){
  const image=document.getElementById('src');
  if(!selectionRect){st.textContent=language==='ru'?'Сначала обведите прямоугольник на исходном видео.':'Drag a rectangle on the source video first.';return;}
  const roi=normalizeRect(selectionRect,selectionCanvas.width,selectionCanvas.height);if(!roi)return;
  trainingClassification().roi=roi;trainingClassification().region_mode='roi';trainingUi.regionMode='roi';selectionTool='polygon';selectionRect=null;
  st.textContent=(language==='ru'?'ROI задан: ':'ROI set: ')+'['+roi.join(', ')+']'+(language==='ru'?'. Нажмите «Применить».':'. Click Apply.');render();
}

function renderClassesSection(s){
  const[d,body]=section('classesTitle');
  const size=el('div','hint');size.id='trainDatasetSize';body.appendChild(size);
  const controls=el('div','row');controls.style.flexWrap='wrap';
  const burstLabel=el('span','hint');burstLabel.textContent=tr('burst')+':';controls.appendChild(burstLabel);
  controls.appendChild(numberInput(trainingUi.burstCount,1,s.dataset.max_burst,v=>trainingUi.burstCount=v));const c1=el('span','hint');c1.textContent=tr('burstCount');controls.appendChild(c1);
  controls.appendChild(numberInput(trainingUi.burstInterval,s.dataset.min_burst_interval_ms,60000,v=>trainingUi.burstInterval=v));const c2=el('span','hint');c2.textContent=tr('burstInterval');controls.appendChild(c2);
  body.appendChild(controls);
  const table=el('div');
  if(!s.dataset.classes.length){const none=el('div','hint');none.textContent=language==='ru'?'Добавьте хотя бы два класса.':'Add at least two classes.';table.appendChild(none);}
  s.dataset.classes.forEach(c=>{
    const row=el('div','linkedRow');row.style.flexWrap='wrap';
    const name=el('span');name.textContent=c.name+(c.display&&c.display!==c.name?' ('+c.display+')':'');name.style.minWidth='140px';row.appendChild(name);
    const count=el('span');count.id='trainCount_'+c.name;count.textContent=c.count;count.style.minWidth='40px';count.style.color='#fb0';row.appendChild(count);
    row.appendChild(button(tr('snapshot'),()=>startCapture(c.name,1)));
    row.appendChild(button(tr('burst')+' '+trainingUi.burstCount,()=>startCapture(c.name,trainingUi.burstCount)));
    row.appendChild(button(tr('showSamples'),()=>{trainingUi.samplesClass=trainingUi.samplesClass===c.name?null:c.name;trainingUi.selected={};loadSamples();}));
    row.appendChild(button(tr('deleteClass'),()=>{if(confirm(tr('confirmDelete')+' ('+c.name+')'))trainingPost('/training/classes',{op:'delete',name:c.name,confirm:true}).then(()=>{trainingSignature='';trainingPoll();}).catch(trainingError);},'danger'));
    table.appendChild(row);
    if(trainingUi.samplesClass===c.name){const grid=el('div');grid.id='trainSamples';grid.style.cssText='display:flex;flex-wrap:wrap;gap:4px;margin:6px 0';table.appendChild(grid);
      const actions=el('div');actions.appendChild(button(tr('deleteSelected'),deleteSelectedSamples,'danger'));actions.appendChild(button(tr('closeSamples'),()=>{trainingUi.samplesClass=null;render();}));table.appendChild(actions);}
  });
  body.appendChild(table);
  const add=el('div','row');const input=el('input');input.type='text';input.placeholder=tr('className');input.value=trainingUi.newClass;input.oninput=()=>trainingUi.newClass=input.value;input.onkeydown=e=>{if(e.key==='Enter')addClass();};add.appendChild(input);add.appendChild(button(tr('addClass'),addClass));body.appendChild(add);
  const status=el('div','hint');status.id='trainCaptureStatus';body.appendChild(status);
  body.appendChild(button(tr('stopCapture'),()=>trainingPost('/training/capture/stop').then(()=>trainingPoll()).catch(trainingError)));
  if(trainingUi.samplesClass)loadSamples();
  return d;
}
function addClass(){const name=trainingUi.newClass.trim();if(!name)return;trainingPost('/training/classes',{op:'add',name}).then(()=>{trainingUi.newClass='';trainingSignature='';trainingPoll();}).catch(trainingError);}
function startCapture(className,count){
  const mode=trainingRegionMode(),body={class:className,region_mode:mode,count,interval_ms:trainingUi.burstInterval};
  if(mode==='roi')body.roi=clampRoi(trainingClassification().roi||[0.25,0.25,0.5,0.5]);
  if(mode==='blob'&&trainingUi.blobIds&&trainingUi.blobIds.length)body.blob_pattern_ids=trainingUi.blobIds;
  trainingPost('/training/capture',body).then(()=>{st.textContent=(language==='ru'?'Съёмка запущена: ':'Capture started: ')+className;trainingPoll();}).catch(trainingError);
}
function loadSamples(){
  const grid=document.getElementById('trainSamples');if(!grid||!trainingUi.samplesClass)return;
  trainingFetch('/training/samples?class='+encodeURIComponent(trainingUi.samplesClass)+'&limit=120').then(list=>{
    grid.innerHTML='';if(!list.items.length){grid.textContent=tr('noSamples');return;}
    list.items.forEach(item=>{const img=el('img');img.src='/training/thumb?class='+encodeURIComponent(trainingUi.samplesClass)+'&file='+encodeURIComponent(item.file);img.title=item.file;img.style.cssText='width:64px;height:64px;object-fit:cover;cursor:pointer;border:2px solid '+(trainingUi.selected[item.file]?'#f66':'#444');
      img.onclick=()=>{trainingUi.selected[item.file]=!trainingUi.selected[item.file];img.style.borderColor=trainingUi.selected[item.file]?'#f66':'#444';};grid.appendChild(img);});
  }).catch(trainingError);
}
function deleteSelectedSamples(){const files=Object.keys(trainingUi.selected).filter(f=>trainingUi.selected[f]);if(!files.length)return;
  trainingPost('/training/samples/delete',{class:trainingUi.samplesClass,files}).then(r=>{st.textContent=(language==='ru'?'Удалено: ':'Deleted: ')+r.deleted;trainingUi.selected={};loadSamples();trainingPoll();}).catch(trainingError);}

function renderTrainSection(s){
  const[d,body]=section('trainTitle');
  const check=s.trainer_check,line=el('div','hint');line.id='trainCheck';
  line.textContent=!check.checked?(language==='ru'?'Тренер ещё не проверялся.':'Trainer has not been checked yet.'):check.available?tr('trainerReady')+' ('+(check.output||'')+')':tr('trainerMissing')+': '+check.reason+'. '+tr('trainerFallback');
  line.style.color=!check.checked?'#bbb':check.available?'#7f7':'#fb0';body.appendChild(line);
  body.appendChild(button(tr('checkTrainer'),()=>{st.textContent=language==='ru'?'Проверка Python/PyTorch…':'Checking Python/PyTorch…';trainingPost('/training/check').then(()=>{st.textContent='';trainingSignature='';trainingPoll();}).catch(trainingError);}));
  const row=el('div','row');const label=el('label');label.textContent=tr('epochs');row.appendChild(label);
  if(trainingUi.epochs===null)trainingUi.epochs=s.settings.classifier_epochs;row.appendChild(numberInput(trainingUi.epochs,1,1000,v=>trainingUi.epochs=v));body.appendChild(row);
  const start=button(tr('train'),()=>trainingPost('/training/train',{epochs:trainingUi.epochs}).then(()=>{trainingSignature='';trainingPoll();}).catch(trainingError));start.id='trainStart';body.appendChild(start);
  const stop=button(tr('stopTrain'),()=>trainingPost('/training/train/stop').then(()=>trainingPoll()).catch(trainingError),'danger');stop.id='trainStop';body.appendChild(stop);
  const bar=el('progress');bar.id='trainProgress';bar.style.cssText='width:100%;display:none';body.appendChild(bar);
  const text=el('div');text.id='trainProgressText';text.style.color='#cda';body.appendChild(text);
  const log=el('pre');log.id='trainLog';log.style.cssText='max-height:200px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;font-size:11px;background:#181818;padding:4px';body.appendChild(log);
  return d;
}

function renderModelsSection(s){
  const[d,body]=section('modelsTitle');
  if(!s.models.length){const none=el('div','hint');none.textContent=tr('noModels');body.appendChild(none);}
  s.models.forEach(m=>{const row=el('div','linkedRow');row.style.flexWrap='wrap';
    const name=el('span');name.textContent=m.file;name.style.minWidth='220px';row.appendChild(name);
    const info=el('span','hint');info.textContent=(m.kind||'classifier')+' · '+(m.classes||[]).join(', ')+(m.best_val_acc!==undefined&&m.best_val_acc!==null?' · val '+m.best_val_acc:'')+' · '+formatBytes(m.bytes)+' · '+new Date(m.created_ms).toLocaleString();row.appendChild(info);
    if(m.active){const act=el('span');act.textContent='● '+tr('active');act.style.color='#7f7';row.appendChild(act);}
    row.appendChild(button(tr('activate'),()=>{st.textContent=language==='ru'?'Активация модели…':'Activating model…';const body={model:m.file,kind:m.kind||'classifier'};if((m.kind||'classifier')==='classifier')body.region_mode=trainingRegionMode();trainingPost('/training/activate',body).then(r=>{st.textContent=(language==='ru'?'Активирована ':'Activated ')+r.activated;trainingUi.regionMode=null;trainingUi.blobIds=null;load();}).catch(trainingError);}));
    body.appendChild(row);});
  const upload=el('div','row');upload.style.flexWrap='wrap';const file=el('input');file.type='file';file.accept='.onnx';file.id='trainUploadFile';upload.appendChild(file);
  const kind=el('select');[['classifier','classifier 64×64'],['yolo','YOLO']].forEach(([v,t])=>{const o=el('option');o.value=v;o.textContent=t;o.selected=trainingUi.uploadKind===v;kind.appendChild(o);});kind.onchange=()=>trainingUi.uploadKind=kind.value;kind.style.cssText='background:#222;color:#eee;padding:4px';upload.appendChild(kind);
  const classes=el('input');classes.type='text';classes.placeholder=tr('uploadClasses');classes.value=trainingUi.uploadClasses;classes.oninput=()=>trainingUi.uploadClasses=classes.value;classes.style.width='220px';upload.appendChild(classes);
  upload.appendChild(button(tr('uploadModel'),()=>{const f=file.files[0];if(!f){st.textContent=language==='ru'?'Выберите файл .onnx':'Choose an .onnx file';return;}
    const names=trainingUi.uploadClasses.split(',').map(v=>v.trim()).filter(Boolean);st.textContent=(language==='ru'?'Загрузка ':'Uploading ')+f.name+'…';
    const headers=Object.assign(admHeaders(),{'X-File-Name':f.name,'X-Model-Kind':trainingUi.uploadKind,'X-Input-Size':trainingUi.uploadKind==='yolo'?'640':String((cfgObj.classification||{}).input_size||64)});if(names.length)headers['X-Class-Names']=encodeURIComponent(JSON.stringify(names));
    fetch('/training/model',{method:'POST',headers,body:f}).then(r=>r.json()).then(r=>{if(r.error)throw new Error(r.error);st.textContent=(language==='ru'?'Загружено: ':'Uploaded: ')+r.saved+' ('+formatBytes(r.bytes)+')';trainingSignature='';trainingPoll();}).catch(trainingError);}));
  body.appendChild(upload);
  const links=el('div');const a1=el('a');a1.href='/training/export?kind=classifier';a1.textContent=tr('exportDataset');a1.style.color='#9bd';a1.setAttribute('download','');links.appendChild(a1);links.appendChild(document.createTextNode('  '));const a2=el('a');a2.href='/training/export?kind=yolo';a2.textContent=tr('exportYolo');a2.style.color='#9bd';a2.setAttribute('download','');links.appendChild(a2);body.appendChild(links);
  return d;
}

// --- этап 2: разметка YOLO ---
function renderYoloSection(s){
  const[d,body]=section('yoloTitle',labeling.active);
  const guide=el('div','hint');guide.textContent=tr('yoloGuide');body.appendChild(guide);
  const classesRow=el('div','row');const classes=el('input');classes.type='text';classes.placeholder=tr('yoloClasses');if(trainingUi.yoloClassText===null)trainingUi.yoloClassText=(s.yolo.classes||[]).join(', ');classes.value=trainingUi.yoloClassText;classes.oninput=()=>trainingUi.yoloClassText=classes.value;classes.style.width='320px';classesRow.appendChild(classes);
  classesRow.appendChild(button(tr('yoloSaveClasses'),()=>trainingPost('/training/yolo/classes',{classes:trainingUi.yoloClassText.split(',').map(v=>v.trim()).filter(Boolean)}).then(()=>{trainingSignature='';trainingPoll();}).catch(trainingError)));body.appendChild(classesRow);
  const cap=el('div','row');const capLabel=el('span','hint');capLabel.textContent=tr('yoloFrames')+': '+s.yolo.images+' ('+s.yolo.labels+' '+tr('labeled')+')';cap.appendChild(capLabel);cap.appendChild(numberInput(trainingUi.yoloCount,1,s.dataset.max_burst,v=>trainingUi.yoloCount=v));
  cap.appendChild(button(tr('yoloCapture'),()=>trainingPost('/training/capture',{kind:'yolo',count:trainingUi.yoloCount,interval_ms:Math.max(trainingUi.burstInterval,200)}).then(()=>trainingPoll()).catch(trainingError)));body.appendChild(cap);
  const list=el('div');list.id='yoloImages';list.style.cssText='display:flex;flex-wrap:wrap;gap:4px;max-height:120px;overflow:auto';body.appendChild(list);
  const tools=el('div');tools.id='yoloTools';body.appendChild(tools);
  loadYoloImages();renderYoloTools();
  return d;
}
function loadYoloImages(){const list=document.getElementById('yoloImages');if(!list)return;trainingFetch('/training/yolo/samples?limit=200').then(r=>{trainingUi.yoloImages=r.items;list.innerHTML='';r.items.forEach(item=>{const b=button(item.file.replace(/\.jpg$/,'')+(item.labeled?' ✓'+item.boxes:''),()=>startLabeling(item.file),labeling.file===item.file?'act':'');b.title=item.labeled?tr('labeled'):tr('notLabeled');b.style.fontSize='11px';list.appendChild(b);});}).catch(()=>{});}
function renderYoloTools(){
  const tools=document.getElementById('yoloTools');if(!tools)return;tools.innerHTML='';
  if(!labeling.active)return;
  const hint=el('div','hint');hint.textContent=tr('yoloHint')+' — '+labeling.file+(language==='ru'?', рамок: ':', boxes: ')+labeling.boxes.length;hint.style.color='#fb0';tools.appendChild(hint);
  const row=el('div','row');const label=el('label');label.textContent=tr('yoloClass');row.appendChild(label);const select=el('select');select.style.cssText='background:#222;color:#eee;padding:4px';
  const classes=(trainingState.yolo.classes||[]);if(!classes.length){const o=el('option');o.value=0;o.textContent='0';select.appendChild(o);}classes.forEach((name,index)=>{const o=el('option');o.value=index;o.textContent=index+': '+name;o.selected=trainingUi.yoloBoxClass===index;select.appendChild(o);});select.onchange=()=>trainingUi.yoloBoxClass=Number(select.value);row.appendChild(select);tools.appendChild(row);
  tools.appendChild(button(tr('yoloUndo'),()=>{labeling.boxes.pop();renderYoloTools();drawSelection();}));
  tools.appendChild(button(tr('yoloSave'),()=>trainingPost('/training/yolo/labels',{file:labeling.file,boxes:labeling.boxes}).then(r=>{st.textContent=(language==='ru'?'Разметка сохранена: ':'Labels saved: ')+r.boxes;loadYoloImages();trainingSignature='';}).catch(trainingError)));
  tools.appendChild(button(tr('yoloDelete'),()=>{if(!confirm(tr('yoloDelete')+' '+labeling.file+'?'))return;trainingPost('/training/yolo/delete',{files:[labeling.file]}).then(()=>{finishLabeling();trainingSignature='';trainingPoll();}).catch(trainingError);},'danger'));
  tools.appendChild(button(tr('yoloDone'),finishLabeling));
}
function startLabeling(file){
  const image=document.getElementById('src');
  if(!labeling.streamPaused){image.onerror=null;labeling.streamPaused=true;}
  labeling.active=true;labeling.file=file;labeling.boxes=[];selectionTool='rect';selectionRect=null;selectionPolygon=null;
  image.src='/training/yolo/image?file='+encodeURIComponent(file)+'&t='+Date.now();
  trainingFetch('/training/yolo/labels?file='+encodeURIComponent(file)).then(r=>{labeling.boxes=r.boxes||[];renderYoloTools();drawSelection();}).catch(()=>{});
  loadYoloImages();renderYoloTools();drawSelection();
}
function finishLabeling(){labeling.active=false;labeling.file=null;labeling.boxes=[];selectionTool='polygon';selectionRect=null;if(labeling.streamPaused){labeling.streamPaused=false;startVideoStream('src','/source.mjpg');}renderYoloTools();loadYoloImages();drawSelection();}
// вызывается из selectionCanvas.onpointerup при selectionTool==='rect'
function onRectSelected(rect){
  if(!labeling.active||!rect)return;
  const box=rectToBox(rect,selectionCanvas.width,selectionCanvas.height);if(!box)return;box.class_id=trainingUi.yoloBoxClass|0;labeling.boxes.push(box);selectionRect=null;renderYoloTools();drawSelection();
}
// дорисовка поверх видео: постоянный ROI, рамки разметки
function trainingDrawOverlay(ctx){
  const w=selectionCanvas.width,h=selectionCanvas.height;
  if(labeling.active){ctx.lineWidth=2;labeling.boxes.forEach(b=>{ctx.strokeStyle='#ff5';ctx.strokeRect((b.cx-b.w/2)*w,(b.cy-b.h/2)*h,b.w*w,b.h*h);ctx.fillStyle='#ff5';ctx.font='11px sans-serif';ctx.fillText(String(b.class_id),(b.cx-b.w/2)*w+3,(b.cy-b.h/2)*h+12);});return;}
  const cls=cfgObj.classification;if(!cls||!Array.isArray(cls.roi)||cls.roi.length!==4)return;
  const showRoi=(cur==='classification'&&cls.region_mode==='roi')||(paramTab==='training'&&trainingRegionMode()==='roi');
  if(!showRoi)return;const r=cls.roi;ctx.strokeStyle='#ffa500';ctx.lineWidth=2;ctx.setLineDash([6,4]);ctx.strokeRect(r[0]*w,r[1]*h,r[2]*w,r[3]*h);ctx.setLineDash([]);
}
)TRAINJS"
