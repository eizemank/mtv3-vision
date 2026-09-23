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
// Optional browser harness exercises the real DOM and canvas without a running camera server.
if(process.argv[2]){
  const css=main.split('<style>')[1].split('</style>')[0];
  const test=`
let language='ru';
const cfgObj={blob_detection:{one_color_patterns:[{lower_range:[80,100,120],upper_range:[120,140,160]}]}};
function el(tag,cls){const e=document.createElement(tag);if(cls)e.className=cls;return e;}
const path=['blob_detection','one_color_patterns',0,'lower_range'],upperPath=[...path.slice(0,3),'upper_range'];
const row=el('div','row');document.body.appendChild(row);
for(const [p,v] of [[path,[80,100,120]],[upperPath,[120,140,160]]]){const f=el('input');f.dataset.path=JSON.stringify(p);f.value=v.join(', ');row.appendChild(f);}
function check(condition,message){if(!condition)throw Error(message);}
addBlobColorPicker(row,path,[80,100,120]);
queueMicrotask(()=>{try{
 const select=row.querySelector('select'),wheel=row.querySelector('canvas');
 const before=[pathInput(path).value,pathInput(upperPath).value];
 for(const mode of ['HSL','HSV','RGB','CMYK','HSLuv','YCbCr']){select.value=mode;select.onchange();check(wheel.hidden===!['HSL','HSV','HSLuv'].includes(mode),'wheel '+mode);check(row.querySelectorAll('input[type=number]').length===(mode==='CMYK'?4:3),'channels '+mode);}
 check(JSON.stringify(before)===JSON.stringify([pathInput(path).value,pathInput(upperPath).value]),'switch must not modify bounds');
 check([...select.options].find(o=>o.value==='CMS').disabled,'CMS disabled');
 select.value='RGB';select.onchange();
 const nums=row.querySelectorAll('input[type=number]');[255,0,0].forEach((v,i)=>{nums[i].value=v;nums[i].oninput();});
 let bounds=[pathInput(path),pathInput(upperPath)].map(f=>f.value.split(',').map(Number));
 check(bounds[0].every((v,i)=>bounds[1][i]-v===40),'preserve tolerance');
 pathInput(path).value='100, 128, 128';pathInput(upperPath).value='100, 128, 128';pathInput(upperPath).dispatchEvent(new Event('input'));
 check(row.querySelector('input[type=color]').value==='#646464','manual range sync');
 nums[0].value='';nums[0].oninput();check(pathInput(path).value==='100, 128, 128','empty field ignored');
 select.value='HSLuv';select.onchange();
 wheel.setPointerCapture=()=>{};
 const rect=wheel.getBoundingClientRect();wheel.onpointerdown({button:0,pointerId:1,clientX:rect.left+160,clientY:rect.top+90});wheel.onpointerup();
 check(pathInput(path).value!== '100, 128, 128','wheel updates bounds');
 document.body.dataset.test='PASS';
}catch(e){document.body.dataset.test='FAIL: '+e.message;}});
`;
 fs.writeFileSync(process.argv[2],`<!doctype html><meta charset="utf-8"><style>${css}</style><body><script>${source}\n${test}</script></body>`);
}
