// Run with: node cpp/SeeSharp/tests/training_ui.test.cjs
// Проверяет чистые хелперы вкладки «Обучение» (include/web/training_js.hpp):
// извлекает блок TRAINJS и выполняет его в node:vm с минимальными заглушками.
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
// Объекты приходят из другого vm-контекста (иной Object.prototype), поэтому структуры сравниваем через JSON.
const same=(actual,expected,message)=>assert.equal(JSON.stringify(actual),JSON.stringify(expected),message);
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'include/web/training_js.hpp'),'utf8').split('R"TRAINJS(')[1].split(')TRAINJS"')[0];
// Заглушки страницы: локализация и словари, к которым хелпер добавляет ключи.
const context={uiText:{ru:{},en:{}},labels:{},hints:{},ruLabels:{},cfgObj:{},cur:'',paramTab:'training',language:'ru',
  selectionCanvas:{width:640,height:480},document:{getElementById(){return null;}},fetch(){return Promise.reject(new Error('no network in tests'));},
  clearTimeout(){},setTimeout(){return 0;},AbortController:function(){this.signal=null;this.abort=()=>{};},console,Date,JSON,Math,Number,Object,Array,Set,String,Promise,confirm(){return false;}};
vm.createContext(context);
vm.runInContext(source,context);
const{rectFromPoints,clampRoi,normalizeRect,rectToBox,yoloLabelLines,formatPrediction,formatBytes,trainerStateText}=context;

same(rectFromPoints({x:10,y:20},{x:4,y:30}),{x:4,y:20,w:6,h:10});
same(clampRoi([-0.2,0.5,2,0.75]),[0,0.5,1,0.5]);       // clamp в кадр
same(clampRoi([0.9,0.9,0,0]),[0.9,0.9,0.01,0.01]);     // нулевой размер -> минимум
same(normalizeRect({x:320,y:120,w:160,h:240},640,480),[0.5,0.25,0.25,0.5]);
assert.equal(normalizeRect(null,640,480),null);
assert.equal(normalizeRect({x:0,y:0,w:10,h:10},0,480),null);
same(rectToBox({x:0,y:0,w:320,h:240},640,480),{cx:0.25,cy:0.25,w:0.5,h:0.5});
assert.equal(yoloLabelLines([{class_id:1,cx:0.5,cy:0.25,w:0.5,h:0.5},{class_id:0,cx:0.1,cy:0.1,w:0,h:0.1}]),'1 0.500000 0.250000 0.500000 0.500000');
assert.equal(formatPrediction([],'ru'),'нет уверенного результата');
assert.equal(formatPrediction([{class_name:'car',confidence:0.664}],'en'),'car 66%');
assert.equal(formatPrediction([{id:3,confidence:0.5},{class_name:'b',confidence:1}],'en'),'class3 50%, b 100% (2 regions)');
assert.equal(formatBytes(512*1024),'512 KiB');
assert.equal(formatBytes(3*1024*1024),'3.0 MiB');
assert.equal(trainerStateText({state:'running',progress:{epoch:2,epochs:5,loss:0.3,acc:0.9,val_acc:1}},'en'),'training: epoch 2/5, loss 0.3, acc 0.9, val 1');
assert.equal(trainerStateText({state:'failed',error:'PyTorch is not installed',progress:{}},'ru'),'ошибка — PyTorch is not installed');
assert.ok(context.uiText.ru.trainingTab&&context.uiText.en.trainingTab,'localization keys registered');
assert.ok(context.labels.region_mode&&context.ruLabels.region_mode,'parameter labels registered');
console.log('Training UI helpers: rect/ROI normalization, YOLO label format, prediction text, trainer state passed.');
