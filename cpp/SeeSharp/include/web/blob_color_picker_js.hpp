// Embedded in kPage: no network dependencies on the device.
R"COLORJS(
const blobColorModels={HSL:['H°','S%','L%'],HSV:['H°','S%','V%'],RGB:['R','G','B'],CMYK:['C%','M%','Y%','K%'],HSLuv:['H°','S%','L%'],YCbCr:['Y','Cb','Cr']};
const legacyBlobColorFields=['min_luminance','max_luminance','min_chrominance_red','max_chrominance_red','min_chrominance_blue','max_chrominance_blue'];
function colorModelMaxima(mode){return blobColorModels[mode].map((_,i)=>mode==='RGB'||mode==='YCbCr'?255:i===0&&['HSL','HSV','HSLuv'].includes(mode)?360:100);}
function colorHasHue(mode){return ['HSL','HSV','HSLuv'].includes(mode);}
function normalizeBlobColor(pattern){
  if(!pattern.color_model||pattern.color_model==='YCrCb'){
    pattern.color_model='YCbCr';
    for(const key of ['lower_range','upper_range'])if(Array.isArray(pattern[key]))pattern[key]=[pattern[key][0],pattern[key][2],pattern[key][1]];
  }
  legacyBlobColorFields.forEach(key=>delete pattern[key]);
}
function colorRangeCenter(low,high,mode){return low.map((v,i)=>i===0&&colorHasHue(mode)&&v>high[i]?(v+(high[i]+360-v)/2)%360:(v+high[i])/2);}
// Axis-aligned boxes are not invariant under a color-space conversion.
// Sample the current box for an initial approximation; the saved bounds are
// always evaluated directly in the selected model by the detector.
function colorConvertBounds(low,high,from,to){
  const samples=[],point=[];
  function visit(i){
    if(i===low.length){samples.push(colorRgbToModel(colorModelToRgb(point,from),to));return;}
    const end=i===0&&colorHasHue(from)&&low[i]>high[i]?high[i]+360:high[i];
    for(let n=0;n<=4;n++){point[i]=low[i]+(end-low[i])*n/4;if(i===0&&colorHasHue(from))point[i]%=360;visit(i+1);}
  }
  visit(0);
  const max=colorModelMaxima(to),lower=max.map((_,i)=>Math.max(0,Math.floor(Math.min(...samples.map(v=>v[i]))*100)/100)),upper=max.map((m,i)=>Math.min(m,Math.ceil(Math.max(...samples.map(v=>v[i]))*100)/100));
  if(colorHasHue(to)){
    const hues=samples.map(v=>v[0]).sort((a,b)=>a-b);let gap=-1,start=0,end=360;
    hues.forEach((h,i)=>{const next=i+1<hues.length?hues[i+1]:hues[0]+360;if(next-h>gap){gap=next-h;start=next%360;end=h;}});
    lower[0]=Math.floor(start*100)/100;upper[0]=Math.ceil(end*100)/100;
  }
  return [lower,upper];
}
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
function colorShiftRange(low,high,center,mode='YCbCr'){
  return center.map((v,i)=>{
    const max=colorModelMaxima(mode)[i];
    if(i===0&&colorHasHue(mode)){
      const width=high[i]>=low[i]?high[i]-low[i]:high[i]+360-low[i];
      if(width>=360)return [0,360];
      return [(v-width/2+360)%360,(v+width/2)%360];
    }
    const width=Math.max(0,Math.min(max,high[i]-low[i])),start=Math.max(0,Math.min(max-width,v-width/2));
    return [start,start+width];
  });
}
function addBlobColorPicker(row,path,lower){
  if(path.length!==4||path[0]!=='blob_detection'||path[1]!=='one_color_patterns'||path[3]!=='lower_range')return;
  const upperPath=path.slice();upperPath[3]='upper_range';
  const pattern=cfgObj.blob_detection.one_color_patterns[path[2]],upper=pattern.upper_range;
  const ru=language==='ru',box=el('div','blobColorEditor');row.classList.add('blobColorRow');row.appendChild(box);
  const label=el('label');label.appendChild(document.createTextNode(ru?'Цвет · модель':'Color · model'));
  const select=el('select');label.appendChild(select);box.appendChild(label);
  for(const mode of Object.keys(blobColorModels)){const o=el('option');o.value=mode;o.textContent=mode;select.appendChild(o);}
  select.value=pattern.color_model;
  select.dataset.path=JSON.stringify([...path.slice(0,3),'color_model']);select.dataset.t='s';
  const palette=el('input');palette.type='color';palette.setAttribute('aria-label',ru?'Палитра цвета':'Color palette');box.appendChild(palette);
  const wheel=el('canvas');wheel.width=wheel.height=180;wheel.setAttribute('aria-label',ru?'Цветовой круг; точный ввод доступен в полях каналов':'Color wheel; use channel fields for precise input');box.appendChild(wheel);
  const channels=el('div','blobColorChannels');box.appendChild(channels);
  const boundsBox=el('div','blobColorBounds');box.appendChild(boundsBox);
  const note=el('span','hint');box.appendChild(note);
  let mode=select.value,bounds=[lower.slice(),upper.slice()],values=colorRangeCenter(lower,upper,mode);
  let rgb=colorModelToRgb(values,mode),inputs=[],ranges=[],boundInputs=[],cachedWheel=null,cachedKey='';
  const remembered=new Map();
  function writeBounds(){
    [path,upperPath].forEach((p,j)=>{const field=pathInput(p);if(field)field.value=bounds[j].join(', ');});
    boundInputs.forEach((pair,i)=>pair.forEach((input,j)=>{input.value=Number(bounds[j][i].toFixed(4));input.setCustomValidity('');}));
  }
  function writeColor(){
    const shifted=colorShiftRange(...bounds,values,mode);
    bounds=[shifted.map(v=>v[0]),shifted.map(v=>v[1])];writeBounds();
  }
  function buildBounds(){
    boundsBox.replaceChildren();boundInputs=[];
    const heading=el('div');heading.textContent=ru?'Диапазоны детекции':'Detection ranges';boundsBox.appendChild(heading);
    blobColorModels[mode].forEach((name,i)=>{
      const line=el('div','blobColorChannels'),pair=[];
      for(let j=0;j<2;j++){
        const label=el('label'),input=el('input');label.textContent=(ru?(j?'Максимум ':'Минимум '):(j?'Maximum ':'Minimum '))+name;
        input.type='number';input.min=0;input.max=colorModelMaxima(mode)[i];input.step='any';input.required=true;
        input.setAttribute('aria-label',label.textContent);
        input.oninput=()=>{
          input.setCustomValidity('');if(input.value===''||!input.checkValidity())return;
          const otherInput=pair[1-j];
          if(otherInput.value===''||!Number.isFinite(Number(otherInput.value))||Number(otherInput.value)<0||Number(otherInput.value)>colorModelMaxima(mode)[i])return;
          const v=Number(input.value),other=Number(otherInput.value);
          if(!(i===0&&colorHasHue(mode))&&(j===0?v>other:v<other)){
            input.setCustomValidity(ru?'Минимум должен быть не больше максимума':'Minimum must not exceed maximum');return;
          }
          pair.forEach(f=>f.setCustomValidity(''));
          bounds[j][i]=v;bounds[1-j][i]=other;values=colorRangeCenter(...bounds,mode);rgb=colorModelToRgb(values,mode);writeBounds();refresh();
        };
        label.appendChild(input);line.appendChild(label);pair.push(input);
      }
      boundInputs.push(pair);boundsBox.appendChild(line);
    });
    writeBounds();
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
    channels.replaceChildren();inputs=[];ranges=[];
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
    note.textContent=ru?'Диапазоны применяются в выбранной модели. При смене модели границы пересчитываются приближённо — проверьте их.':'Ranges are evaluated in the selected model. Switching models approximates the bounds; review them.';
    if(colorHasHue(mode))note.textContent+=ru?' H: минимум > максимума означает переход через 0°.':' H: minimum > maximum crosses 0°.';
    if(select.value==='CMYK')note.textContent+=ru?' CMYK — приближённый пересчёт в sRGB без профиля печати.':' CMYK is an approximate sRGB conversion without a print profile.';
    buildBounds();refresh();
  }
  select.onchange=()=>{
    remembered.set(mode,bounds.map(v=>v.slice()));
    bounds=remembered.has(select.value)?remembered.get(select.value).map(v=>v.slice()):colorConvertBounds(...bounds,mode,select.value);
    mode=select.value;values=colorRangeCenter(...bounds,mode);rgb=colorModelToRgb(values,mode);build();
  };
  palette.oninput=()=>{rgb=colorHexRgb(palette.value);values=colorRgbToModel(rgb,select.value);writeColor();refresh();};
  function point(event){const rect=wheel.getBoundingClientRect(),x=(event.clientX-rect.left)*180/rect.width-89.5,y=(event.clientY-rect.top)*180/rect.height-89.5;values[0]=(Math.atan2(y,x)*180/Math.PI+360)%360;values[1]=Math.min(100,Math.hypot(x,y)/88*100);change();}
  let dragging=false;
  wheel.onpointerdown=e=>{if(e.button!==0)return;dragging=true;wheel.setPointerCapture(e.pointerId);point(e);};
  wheel.onpointermove=e=>{if(dragging)point(e);};
  wheel.onpointerup=wheel.onpointercancel=wheel.onlostpointercapture=()=>{dragging=false;};
  // The upper hidden field is inserted after this editor during form construction.
  queueMicrotask(writeBounds);
  build();
}
)COLORJS"
