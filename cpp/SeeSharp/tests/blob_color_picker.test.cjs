// Run with: node cpp/SeeSharp/tests/blob_color_picker.test.cjs
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
function embedded(file,tag){return fs.readFileSync(path.join(root,file),'utf8').split(`R"${tag}(`)[1].split(`)${tag}"`)[0];}
const main=fs.readFileSync(path.join(root,'src/main.cpp'),'utf8');
const helpers=main.slice(main.indexOf('function clampByte('),main.indexOf(')HTML"\n#include "web/vendor/hsluv'));
const source=embedded('include/web/vendor/hsluv/hsluv_js.hpp','HSLUV')+'\n'+helpers+'\n'+embedded('include/web/blob_color_picker_js.hpp','COLORJS');
vm.runInThisContext(source);
for(const mode of ['RGB','HSL','HSV','CMYK','HSLuv']){
  for(const r of [0,1,64,128,254,255])for(const g of [0,1,64,128,254,255])for(const b of [0,1,64,128,254,255]){
    const rgb=[r,g,b],restored=colorModelToRgb(colorRgbToModel(rgb,mode),mode);
    assert.ok(restored.every((v,i)=>Number.isFinite(v)&&Math.abs(v-rgb[i])<0.001),`${mode}: ${rgb} => ${restored}`);
  }
}
assert.equal(colorRgbHex(colorModelToRgb([10,75,65],'HSLuv')),'#ec7d82'); // Upstream reference example.
assert.deepEqual(hexToYCrCb('#ff0000'),[76,255,85]);
assert.deepEqual(colorRgbToModel([128,128,128],'YCbCr').map(Math.round),[128,128,128]);
assert.equal(colorRgbHex(colorModelToRgb([76,85,255],'YCbCr')),'#fe0000');
for(const center of [[0,0,0],[255,255,255],[12,128,242]]){
  const shifted=colorShiftRange([0,100,120],[255,100,140],center);
  assert.deepEqual(shifted.map(v=>v[1]-v[0]),[255,0,20]);
  assert.ok(shifted.every(([lo,hi])=>lo>=0&&hi<=255&&lo<=hi));
}
console.log('Color conversions: 1080 round trips, HSLuv reference, YCbCr order, tolerance boundaries passed.');
const legacy={lower_range:[0,173,96],upper_range:[255,200,128],min_luminance:0};
normalizeBlobColor(legacy);
assert.deepEqual(legacy,{color_model:'YCbCr',lower_range:[0,96,173],upper_range:[255,128,200]});
normalizeBlobColor(legacy);assert.deepEqual(legacy.lower_range,[0,96,173]);
assert.deepEqual(colorRangeCenter([350,50,40],[10,100,80],'HSV'),[0,75,60]);
assert.deepEqual(colorShiftRange([350,50,40],[10,100,80],[5,75,60],'HSV')[0],[355,15]);
assert.deepEqual(colorShiftRange([0,0,0],[360,100,100],[5,75,60],'HSV')[0],[0,360]);
for(const model of Object.keys(blobColorModels)){
 const bounds=colorConvertBounds([180,0,0],[255,30,30],'RGB',model),max=colorModelMaxima(model);
 assert.ok(bounds.every(row=>row.length===max.length&&row.every((v,i)=>Number.isFinite(v)&&v>=0&&v<=max[i])));
}
// Compare native detector conversion math against the JavaScript editor.
const cp=require('node:child_process'),os=require('node:os');
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'seesharp-color-'));
try{
 const exe=path.join(tmp,'color-test');
 cp.execFileSync('g++',['-std=c++17','-O2','-I',path.join(root,'include'),path.join(__dirname,'color_model.test.cpp'),'-o',exe]);
 const cases=[];
 for(const model of Object.keys(blobColorModels))for(const r of [0,1,64,128,254,255])for(const g of [0,1,64,128,254,255])for(const b of [0,1,64,128,254,255])cases.push({model,rgb:[r,g,b]});
 const output=cp.execFileSync(exe,[],{input:cases.map(c=>[c.model,...c.rgb].join(' ')).join('\n'),encoding:'utf8'}).trim().split('\n');
 assert.equal(output.length,cases.length);
 cases.forEach((c,i)=>{const expected=colorRgbToModel(c.rgb,c.model),actual=output[i].split(' ').map(Number);assert.ok(expected.every((v,j)=>Math.abs(v-actual[j])<1e-6),JSON.stringify({c,expected,actual}));});
 console.log('Native/editor parity: 1296 samples passed; legacy migration and hue wrap passed.');
}finally{fs.rmSync(tmp,{recursive:true,force:true});}
// Optional browser harness exercises actual form construction and collection.
if(process.argv[2]){
 const css=main.split('<style>')[1].split('</style>')[0];
 const formSource=main.slice(main.indexOf('function fieldRow('),main.indexOf('function linkedDefaults('))+
 main.slice(main.indexOf('function buildForm('),main.indexOf('function render('))+
 main.slice(main.indexOf('function collect('),main.indexOf('function load('));
 const test=`
let language='ru';
let cfgObj={blob_detection:{one_color_patterns:[{lower_range:[80,100,120],upper_range:[120,140,160],min_luminance:0,max_chrominance_red:255}]}};
normalizeConfig(cfgObj);
function el(tag,cls){const e=document.createElement(tag);if(cls)e.className=cls;return e;}
function fieldLabel(k){return k;}function contextualFieldLabel(k){return k;}function fieldHint(k){return k;}
const path=['blob_detection','one_color_patterns',0,'lower_range'],upperPath=[...path.slice(0,3),'upper_range'];
const row=el('div');document.body.appendChild(row);
function check(condition,message){if(!condition)throw Error(message);}
buildForm(cfgObj,[],row,true);row.querySelectorAll('details').forEach(d=>d.open=true);
queueMicrotask(()=>{try{
 let select=row.querySelector('select');
 check(select.value==='YCbCr','legacy model');
 check(!row.textContent.includes('luminance')&&!row.textContent.includes('chrominance'),'legacy fields removed');
 check(pathInput(path).type==='hidden'&&pathInput(upperPath).type==='hidden','raw arrays hidden');
 check(![...select.options].some(o=>o.value==='CMS'),'CMS removed');
 const before=JSON.stringify(collect());
 for(const mode of ['HSL','HSV','RGB','CMYK','HSLuv','YCbCr']){
  select.value=mode;select.onchange();
  check(row.querySelector('canvas').hidden===!colorHasHue(mode),'wheel '+mode);
  check(row.querySelectorAll('.blobColorBounds input').length===(mode==='CMYK'?8:6),'bounds '+mode);
  const saved=collect().blob_detection.one_color_patterns[0];
  check(saved.color_model===mode&&saved.lower_range.length===blobColorModels[mode].length,'serialized '+mode);
 }
 check(JSON.stringify(collect())===before,'restore original model bounds');
 select.value='HSV';select.onchange();
 const fields=row.querySelectorAll('.blobColorBounds input');
 // Wrapped red hue interval and independent saturation/value bounds.
 for(const [i,v] of [[0,350],[1,10],[3,100],[2,50],[5,100],[4,20]]){fields[i].value=v;fields[i].oninput();}
 let saved=collect().blob_detection.one_color_patterns[0];
 check(JSON.stringify(saved.lower_range)==='[350,50,20]'&&JSON.stringify(saved.upper_range)==='[10,100,100]','saved HSV interval');
 fields[2].value=101;fields[2].oninput();check(!fields[2].checkValidity(),'channel limits');
 fields[2].value=50;fields[2].oninput();
 cfgObj=collect();row.replaceChildren();buildForm(cfgObj,[],row,true);row.querySelectorAll('details').forEach(d=>d.open=true);
 select=row.querySelector('select');check(select.value==='HSV','reload selected model');
 check(row.querySelector('.blobColorBounds input').value==='350','reload thresholds');
 select.value='CMYK';select.onchange();
 let bounds=row.querySelectorAll('.blobColorBounds input');bounds[7].value=100;bounds[7].oninput();bounds[6].value=25;bounds[6].oninput();
 check(collect().blob_detection.one_color_patterns[0].lower_range[3]===25,'CMYK K is saved');
 bounds[6].value=101;bounds[6].oninput();check(!bounds[6].checkValidity(),'invalid K rejected');bounds[6].value=25;bounds[6].oninput();
 select.value='HSLuv';select.onchange();
 const wheel=row.querySelector('canvas'),old=pathInput(path).value;
 wheel.setPointerCapture=()=>{};const rect=wheel.getBoundingClientRect();
 wheel.onpointerdown({button:0,pointerId:1,clientX:rect.left+160,clientY:rect.top+90});wheel.onpointerup();
 check(pathInput(path).value!==old,'wheel changes model bounds');
 document.body.dataset.test='PASS';
}catch(e){document.body.dataset.test='FAIL: '+e.message;}});
`;
 fs.writeFileSync(process.argv[2],`<!doctype html><meta charset="utf-8"><style>${css}</style><body><script>${source}\n${formSource}\n${test}</script></body>`);
}
