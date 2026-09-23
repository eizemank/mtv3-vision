// Embedded in kPage: no network dependencies on the device.
R"COLORJS(
const blobColorModels={HSL:['H°','S%','L%'],HSV:['H°','S%','V%'],RGB:['R','G','B'],CMYK:['C%','M%','Y%','K%'],HSLuv:['H°','S%','L%'],YCbCr:['Y','Cb','Cr']};
const blobColorModes=new Map();
function colorRgbToModel(rgb,mode){
  const [r,g,b]=rgb.map(v=>v/255),hi=Math.max(r,g,b),lo=Math.min(r,g,b),d=hi-lo;
  if(mode==='RGB')return rgb.slice();
  if(mode==='YCbCr'){const y=.299*rgb[0]+.587*rgb[1]+.114*rgb[2];return [y,(rgb[2]-y)*.564+128,(rgb[0]-y)*.713+128].map(v=>Math.max(0,Math.min(255,v)));}
  if(mode==='CMYK')return hi===0?[0,0,0,100]:[(hi-r)/hi*100,(hi-g)/hi*100,(hi-b)/hi*100,(1-hi)*100];
  if(mode==='HSLuv'){const c=new Hsluv();[c.rgb_r,c.rgb_g,c.rgb_b]=[r,g,b];c.rgbToHsluv();return [c.hsluv_h,c.hsluv_s,c.hsluv_l];}
  const h=d===0?0:((hi===r?(g-b)/d:hi===g?(b-r)/d+2:(r-g)/d+4)*60+360)%360,l=(hi+lo)/2;
  return mode==='HSV'?[h,hi===0?0:d/hi*100,hi*100]:[h,d===0?0:d/(1-Math.abs(2*l-1))*100,l*100];
}
function colorModelToRgb(values,mode){
  if(mode==='RGB')return values.slice();
  if(mode==='YCbCr'){const [y,cb,cr]=values;return [y+1.403*(cr-128),y-.714*(cr-128)-.344*(cb-128),y+1.773*(cb-128)].map(clampByte);}
  if(mode==='CMYK')return values.slice(0,3).map(v=>255*(1-v/100)*(1-values[3]/100));
  if(mode==='HSLuv'){const c=new Hsluv();[c.hsluv_h,c.hsluv_s,c.hsluv_l]=values;c.hsluvToRgb();return [c.rgb_r,c.rgb_g,c.rgb_b].map(v=>v*255);}
  const [h,s,v]=[values[0]%360,values[1]/100,values[2]/100],chroma=mode==='HSV'?v*s:(1-Math.abs(2*v-1))*s;
  const x=chroma*(1-Math.abs((h/60)%2-1)),m=mode==='HSV'?v-chroma:v-chroma/2;
  const segments=[[chroma,x,0],[x,chroma,0],[0,chroma,x],[0,x,chroma],[x,0,chroma],[chroma,0,x]];
  return segments[Math.floor(h/60)].map(c=>(c+m)*255);
}
function colorRgbHex(rgb){return '#'+rgb.map(v=>clampByte(v).toString(16).padStart(2,'0')).join('');}
function colorHexRgb(hex){return [1,3,5].map(i=>parseInt(hex.slice(i,i+2),16));}
function colorShiftRange(low,high,center){
  // Shift the entire interval at channel limits instead of shrinking its width.
  return center.map((v,i)=>{const width=Math.max(0,Math.min(255,high[i]-low[i]));const start=Math.max(0,Math.min(255-width,Math.round(v-width/2)));return [start,start+width];});
}
function addBlobColorPicker(row,path,lower){
  if(path.length!==4||path[0]!=='blob_detection'||path[1]!=='one_color_patterns'||path[3]!=='lower_range')return;
  const upperPath=path.slice();upperPath[3]='upper_range';
  const upper=cfgObj.blob_detection.one_color_patterns[path[2]].upper_range;
  if(!Array.isArray(upper)||upper.length!==3||lower.length!==3)return;
  const key=JSON.stringify(path),ru=language==='ru',box=el('div','blobColorEditor');row.classList.add('blobColorRow');row.appendChild(box);
  const label=el('label');label.appendChild(document.createTextNode(ru?'Цвет · модель':'Color · model'));
  const select=el('select');label.appendChild(select);box.appendChild(label);
  for(const mode of ['HSL','HSV','RGB','CMYK','CMS','HSLuv','YCbCr']){const o=el('option');o.value=mode;o.textContent=mode==='CMS'?'CMS (ICC — '+(ru?'недоступно':'unavailable')+')':mode;o.disabled=mode==='CMS';select.appendChild(o);}
  select.value=blobColorModes.get(key)||'HSL';
  const palette=el('input');palette.type='color';palette.setAttribute('aria-label',ru?'Палитра цвета':'Color palette');box.appendChild(palette);
  const wheel=el('canvas');wheel.width=wheel.height=180;wheel.setAttribute('aria-label',ru?'Цветовой круг; точный ввод доступен в полях каналов':'Color wheel; use channel fields for precise input');box.appendChild(wheel);
  const channels=el('div','blobColorChannels');box.appendChild(channels);
  const note=el('span','hint');box.appendChild(note);
  let rgb=colorHexRgb(yCrCbToHex(lower.map((v,i)=>(v+upper[i])/2))),values=[],inputs=[],ranges=[],cachedWheel=null,cachedKey='';
  function readBounds(){
    const fields=[pathInput(path),pathInput(upperPath)];
    if(fields.some(f=>!f))return null;
    const bounds=fields.map(f=>f.value.split(',').map(Number));
    if(bounds.some(v=>v.length!==3||v.some(n=>!Number.isFinite(n)||n<0||n>255))||bounds[0].some((v,i)=>v>bounds[1][i]))return null;
    return {fields,bounds};
  }
  function writeColor(){
    const current=readBounds();if(!current)return;
    const selected=select.value==='YCbCr'?[values[0],values[2],values[1]].map(clampByte):hexToYCrCb(colorRgbHex(rgb));
    const shifted=colorShiftRange(...current.bounds,selected);
    current.fields.forEach((f,j)=>f.value=shifted.map(v=>v[j]).join(', '));
  }
  function draw(){
    if(wheel.hidden)return;
    const ctx=wheel.getContext('2d');if(!ctx)return;
    const cacheKey=select.value+':'+values[2];
    if(cacheKey!==cachedKey){
      cachedWheel=ctx.createImageData(180,180);
      for(let y=0;y<180;y++)for(let x=0;x<180;x++){
        const dx=(x-89.5)/88,dy=(y-89.5)/88,s=Math.hypot(dx,dy);if(s>1)continue;
        const h=(Math.atan2(dy,dx)*180/Math.PI+360)%360,c=colorModelToRgb([h,s*100,values[2]],select.value),i=(y*180+x)*4;
        cachedWheel.data.set([...c.map(clampByte),255],i);
      }
      cachedKey=cacheKey;
    }
    ctx.putImageData(cachedWheel,0,0);
    const angle=values[0]*Math.PI/180,x=89.5+88*values[1]/100*Math.cos(angle),y=89.5+88*values[1]/100*Math.sin(angle);
    ctx.beginPath();ctx.arc(x,y,5,0,2*Math.PI);ctx.strokeStyle='#000';ctx.lineWidth=3;ctx.stroke();ctx.strokeStyle='#fff';ctx.lineWidth=1.5;ctx.stroke();
  }
  function refresh(){palette.value=colorRgbHex(rgb);inputs.forEach((input,i)=>{input.value=Number(values[i].toFixed(2));ranges[i].value=values[i];});draw();}
  function change(){rgb=colorModelToRgb(values,select.value);writeColor();refresh();}
  function build(){
    values=colorRgbToModel(rgb,select.value);channels.replaceChildren();inputs=[];ranges=[];
    wheel.hidden=!['HSL','HSV','HSLuv'].includes(select.value);cachedKey='';
    blobColorModels[select.value].forEach((name,i)=>{
      const l=el('label');l.appendChild(document.createTextNode(name));
      const max=select.value==='RGB'||select.value==='YCbCr'?255:i===0&&!wheel.hidden?360:100;
      const n=el('input'),slider=el('input');n.type='number';slider.type='range';
      for(const input of [n,slider]){input.min=0;input.max=max;input.step='any';input.setAttribute('aria-label',name);}
      n.oninput=()=>{if(n.value===''||!n.checkValidity())return;values[i]=Number(n.value);rgb=colorModelToRgb(values,select.value);writeColor();palette.value=colorRgbHex(rgb);slider.value=values[i];draw();};
      slider.oninput=()=>{values[i]=Number(slider.value);change();};
      l.appendChild(n);l.appendChild(slider);channels.appendChild(l);inputs.push(n);ranges.push(slider);
    });
    note.textContent=ru?'Допуски YCrCb сохраняются. YCbCr: порядок Y, Cb, Cr; в конфигурации — Y, Cr, Cb. CMS требует ICC-профиля.':'YCrCb tolerance widths are preserved. YCbCr order: Y, Cb, Cr; configuration: Y, Cr, Cb. CMS requires an ICC profile.';
    if(select.value==='CMYK')note.textContent+=ru?' CMYK — приближённый пересчёт в sRGB без профиля печати.':' CMYK is an approximate sRGB conversion without a print profile.';
    refresh();
  }
  select.onchange=()=>{blobColorModes.set(key,select.value);build();};
  palette.oninput=()=>{rgb=colorHexRgb(palette.value);values=colorRgbToModel(rgb,select.value);writeColor();refresh();};
  function point(event){const rect=wheel.getBoundingClientRect(),x=(event.clientX-rect.left)*180/rect.width-89.5,y=(event.clientY-rect.top)*180/rect.height-89.5;values[0]=(Math.atan2(y,x)*180/Math.PI+360)%360;values[1]=Math.min(100,Math.hypot(x,y)/88*100);change();}
  let dragging=false;
  wheel.onpointerdown=e=>{if(e.button!==0)return;dragging=true;wheel.setPointerCapture(e.pointerId);point(e);};
  wheel.onpointermove=e=>{if(dragging)point(e);};
  wheel.onpointerup=wheel.onpointercancel=wheel.onlostpointercapture=()=>{dragging=false;};
  // Both range fields are inserted after this editor is created; delegate locally.
  queueMicrotask(()=>{for(const p of [path,upperPath]){const field=pathInput(p);if(field)field.addEventListener('input',()=>{const current=readBounds();if(!current)return;rgb=colorHexRgb(yCrCbToHex(current.bounds[0].map((v,i)=>(v+current.bounds[1][i])/2)));values=colorRgbToModel(rgb,select.value);refresh();});}});
  build();
}
)COLORJS"
