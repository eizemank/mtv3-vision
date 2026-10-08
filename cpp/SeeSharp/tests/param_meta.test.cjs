// Run with: node cpp/SeeSharp/tests/param_meta.test.cjs
// Проверяет метаданные параметров web UI (include/web/param_meta_js.hpp):
// списки допустимых значений, шаг дробных полей и полноту русских
// подписей/подсказок относительно английских таблиц из src/main.cpp.
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
function embedded(file,tag){return fs.readFileSync(path.join(root,file),'utf8').split(`R"${tag}(`)[1].split(`)${tag}"`)[0];}
const main=fs.readFileSync(path.join(root,'src/main.cpp'),'utf8');
// Таблицы labels/hints/ruLabels + modes/modeNames страницы.
const tables=main.slice(main.indexOf("const modes=['off'"),main.indexOf('const modeNames='))
  +main.slice(main.indexOf('const modeNames='),main.indexOf('function tr(key)'))
  +main.slice(main.indexOf('const labels={'),main.indexOf('function fieldLabel('));
const training=embedded('include/web/training_js.hpp','TRAINJS');
const trainingTables=training.slice(training.indexOf('Object.assign(labels,{region_mode'),training.indexOf('// --- состояние вкладки'));
const context={console,JSON,Math,Number,Object,Array,String,language:'ru',uiText:{ru:{},en:{}}};
vm.createContext(context);
// const/let не попадают в глобальный объект контекста — возвращаем их явно.
// Объекты из контекста vm — другого realm: сравниваем через JSON.
const eq=(actual,expected,message)=>assert.equal(JSON.stringify(actual),JSON.stringify(expected),message);
const{labels,hints,ruLabels,ruHints,fieldMeta,decimalStep,paramOptions}=vm.runInContext(
  tables+'\n'+trainingTables+'\n'+embedded('include/web/param_meta_js.hpp','PARAMJS')+'\n;({labels,hints,ruLabels,ruHints,fieldMeta,decimalStep,paramOptions})',context);

// Полнота перевода: у каждого английского ключа есть русская подпись и подсказка.
const missingLabels=Object.keys(labels).filter(k=>!ruLabels[k]);
eq(missingLabels,[],'ruLabels missing: '+missingLabels.join(', '));
const missingHints=Object.keys(hints).filter(k=>!ruHints[k]);
eq(missingHints,[],'ruHints missing: '+missingHints.join(', '));
const hintsWithoutLabel=Object.keys(ruLabels).filter(k=>!ruHints[k]&&!/_detection$|^general_params$|^transports$|^uart_binary$|^classification$|^training$|^format$/.test(k));
eq(hintsWithoutLabel,[],'ruHints missing for labelled keys: '+hintsWithoutLabel.join(', '));

// Canny: только 3/5/7 (иначе cv::Canny бросает исключение и роняет процесс).
eq(fieldMeta('canny_aperture_size',['line_detection','canny_aperture_size'],3).options,[[3,'3'],[5,'5'],[7,'7']]);
eq(fieldMeta('camera_rotation',['general_params','camera_rotation'],90).options.map(o=>o[0]),[0,90,180,270]);
eq(fieldMeta('ui_language',['general_params','ui_language'],'ru').options,[['ru','Русский'],['en','English']]);
eq(fieldMeta('processing_mode',['general_params','processing_mode'],'off').options[1],['aruco_detection','ArUco-маркеры']);
assert.equal(fieldMeta('size_measure',['blob_detection','multicolor_patterns',0,'size_measure'],'area').options.length,5);
assert.ok(paramOptions.dictionary.includes('DICT_ARUCO_ORIGINAL'));

// Дробные параметры: шаг не должен быть целым.
eq(fieldMeta('confidence_threshold',['object_detection','confidence_threshold'],0.35),{step:0.01,min:0,max:1});
eq(fieldMeta('min_circularity',['blob_detection','one_color_patterns',0,'min_circularity'],0),{step:0.01,min:0,max:1});
assert.equal(fieldMeta('contrast',['general_params','contrast'],1).step,0.05);
assert.equal(fieldMeta('hough_theta',['line_detection','hough_theta'],0.017453292519943295).step,0.0001);
// Критерии связанных объектов: min/max/goal зависят от родителя.
eq(fieldMeta('goal',['blob_detection','multicolor_patterns',0,'nodes',0,'circularity','goal'],0.5),{step:0.01,min:0,max:1});
eq(fieldMeta('goal',['blob_detection','multicolor_patterns',0,'nodes',0,'size','goal'],350),{step:1,min:0});
eq(fieldMeta('goal',['blob_detection','multicolor_patterns',0,'nodes',1,'size','goal'],1),{step:0.01,min:0});   // относительный размер
eq(fieldMeta('max',['blob_detection','multicolor_patterns',0,'links',0,'angle_absolute','max'],180),{step:1,min:-180,max:180});
// Неизвестный ключ: шаг по числу знаков после запятой.
eq(fieldMeta('custom',['x','custom'],84.85),{step:0.01});
eq(fieldMeta('custom',['x','custom'],7),{step:1});
assert.equal(decimalStep(0.1234567),0.0001);
assert.equal(decimalStep(1e-7),0.0001);
eq(fieldMeta('name',['x','name'],'text'),{});
eq(fieldMeta('flag',['x','flag'],true),{});
console.log('param meta tests: PASS');

// --- fieldRow (src/main.cpp): список вместо свободного ввода, шаг числовых полей.
// Минимальная заглушка DOM: достаточно для createElement/appendChild/dataset.
function fakeElement(tag){const e={tagName:tag,children:[],dataset:{},appendChild(c){this.children.push(c);return c;},classList:{add(){}}};return e;}
const rowSource=main.slice(main.indexOf('function fieldRow('),main.indexOf('function linkedDefaults('));
const rowContext={console,JSON,String,Number,Array,Object,Math,language:'ru',modes:context.modes,modeNames:context.modeNames,
  el:(t,c)=>{const e=fakeElement(t);if(c)e.className=c;return e;},
  contextualFieldLabel:k=>k,fieldHint:k=>'hint '+k,t2:(ru,en)=>ru,setLanguage:()=>{},addBlobColorPicker:()=>{},
  fieldMeta:context.fieldMeta};
vm.createContext(rowContext);
const fieldRow=vm.runInContext(rowSource+'\n;fieldRow',rowContext);
function control(key,val,path){return fieldRow(key,val,path).children[1];}
let c=control('canny_aperture_size',3,['line_detection','canny_aperture_size']);
assert.equal(c.tagName,'select');assert.equal(c.dataset.t,'n');
eq(c.children.map(o=>[o.value,o.selected]),[['3',true],['5',false],['7',false]]);
c=control('canny_aperture_size',4,['line_detection','canny_aperture_size']);   // недопустимое сохранённое значение видно и выбрано
eq(c.children.map(o=>o.textContent),['4 (недопустимо)','3','5','7']);assert.equal(c.children[0].selected,true);
c=control('ui_language','ru',['general_params','ui_language']);
assert.equal(c.tagName,'select');assert.equal(c.dataset.t,'s');assert.equal(typeof c.onchange,'function');
c=control('confidence_threshold',0.35,['object_detection','confidence_threshold']);
assert.equal(c.tagName,'input');assert.equal(c.type,'number');assert.equal(c.step,'0.01');assert.equal(c.min,0);assert.equal(c.max,1);
c=control('hough_threshold',50,['line_detection','hough_threshold']);
assert.equal(c.step,'1');assert.equal(c.min,1);assert.equal(c.max,undefined);
c=control('white_balance_bgr',[1,1,1],['general_params','white_balance_bgr']);
assert.equal(c.type,'text');assert.equal(c.dataset.t,'a');assert.equal(c.dataset.num,'1');
c=control('debug_mode',true,['general_params','debug_mode']);
assert.equal(c.type,'checkbox');assert.equal(c.dataset.t,'b');
console.log('fieldRow tests: PASS');
