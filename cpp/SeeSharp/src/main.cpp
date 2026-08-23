// SeeSharp — composition root (по образцу main.py из SeeSharpPy):
//   1. Конфиг  2. Процессор через фабрику  3. ProcessingManager
//   4. IFrameSource  5. Sink  6. Pipeline.start()
//
// MTV3_BOARD (CMake-опция): источник — shm-ринг демона mtv3_cam_daemon --shm,
// sink — headless (fps-лог, позже — отправка метаданных). Десктоп: камера,
// imshow + запись combined.avi (TEST CODE).

#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "config/config_reader.hpp"
#include "pipeline/pipeline.hpp"
#include "processing/processing_manager.hpp"

#ifdef RASPBERRY_CM5
#include "system/system_admin.hpp"
#include "transport/transport_manager.hpp"
#endif

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <atomic>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#endif

#ifdef MTV3_BOARD
#include "pipeline/shm_source.hpp"
#elif defined(HOST_WEB_UI)
#include "pipeline/synthetic_source.hpp"
#else
#include "pipeline/camera_source.hpp"
#endif

// путь к конфигу (--config); по умолчанию рядом с бинарником (cwd)
static std::string gConfigPath = "config.json";

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
static time_t cfgMtime(const char* path)
{
    struct stat st{};
    return stat(path, &st) ? 0 : st.st_mtime;
}

// применение конфига к живому конвейеру (ставится в main); "" = успех
static std::function<std::string(const nlohmann::json&)> gApply;
static std::mutex gPreviewMutex;
static cv::Mat gPreviewSourceFrame;
static cv::Mat gPreviewResultFrame;
#ifdef RASPBERRY_CM5
static std::unique_ptr<SystemAdmin> gSystemAdmin;
#endif

static void updatePreview(const cv::Mat& source, const cv::Mat& result)
{
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    gPreviewSourceFrame = source.clone();
    gPreviewResultFrame = result.clone();
}

static bool getPreviewJpeg(bool source, std::string& jpeg)
{
    cv::Mat frame;
    {
        std::lock_guard<std::mutex> lock(gPreviewMutex);
        const cv::Mat& current = source ? gPreviewSourceFrame : gPreviewResultFrame;
        if (current.empty())
            return false;
        frame = current.clone();
    }

    std::vector<uchar> encoded;
    if (!cv::imencode(".jpg", frame, encoded))
        return false;
    jpeg.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    return true;
}

// Атомарная запись: tmp -> fsync -> rename. На flash важно: обрыв питания
// посреди записи не оставит обрезанный config.json.
static std::string saveConfigAtomic(const nlohmann::json& j)
{
    std::string tmp = gConfigPath + ".tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o)
            return "не могу открыть " + tmp;
        o << j.dump(2) << std::endl;
        o.flush();
        if (!o)
            return "ошибка записи " + tmp;
    }
    int fd = open(tmp.c_str(), O_RDONLY);
    if (fd >= 0)
    {
        fsync(fd);
        close(fd);
    }
    // бэкап предыдущего (один шаг назад)
    rename(gConfigPath.c_str(), (gConfigPath + ".bak").c_str());
    if (rename(tmp.c_str(), gConfigPath.c_str()))
        return "rename failed";
    sync();
    return "";
}

// Веб-интерфейс управления (:8081):
//   GET  /            — страница: превью (:8080), выбор анализатора, редактор конфига
//   GET  /config      — текущий config.json
//   POST /config      — сохранить config.json (валидация json; применит вахтёр)
//   GET  /mode/<имя>  — быстрое переключение processing_mode
static const char kPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>SeeSharp vision</title><style>
body{font-family:sans-serif;background:#111;color:#eee;margin:12px;max-width:1100px}
button{margin:2px;padding:6px 12px;background:#333;color:#eee;border:1px solid #555;cursor:pointer}
button:hover{background:#464}
button.act{background:#464;border-color:#7a7}
textarea{width:100%;height:320px;background:#181818;color:#9e9;font-family:monospace;font-size:12px}
img{max-width:100%;border:1px solid #444}.streams{display:grid;grid-template-columns:1fr 1fr;gap:8px}.streams h4{margin:4px}#st{margin-left:10px;color:#fb0}
.selectFrame{position:relative;display:inline-block;max-width:100%}.selectFrame img{display:block}.selectFrame canvas{position:absolute;inset:0;width:100%;height:100%;cursor:crosshair}
.blobSampler{display:flex;align-items:center;gap:8px;margin:6px 0 10px}.blobSampler input{width:55px;background:#222;color:#eee;border:1px solid #555;padding:4px}
details{border:1px solid #383838;margin:6px 0;background:#161616}
details>summary{padding:5px 8px;cursor:pointer;background:#1e1e1e;color:#cda;font-weight:bold}
details details>summary{font-weight:normal;color:#9bd}
.body{padding:4px 10px 8px}
.row{display:flex;align-items:center;gap:8px;padding:2px 0}
.row label{flex:0 0 260px;color:#bbb;font-size:13px;cursor:help}
.row input[type=text],.row input[type=number]{background:#222;color:#eee;border:1px solid #555;padding:3px 6px;width:190px}
.row input[type=color]{width:38px;height:28px;padding:1px;border:1px solid #555;background:#222;cursor:pointer}
.row input:focus{border-color:#7a7;outline:none}
.hint{color:#666;font-size:12px}
</style></head><body>
<h3>SeeSharp vision</h3>
<div class="streams"><div><h4>Source</h4><div class="selectFrame"><img id="src" alt="source is not ready"><canvas id="selection"></canvas></div></div>
<div><h4>Detection result</h4><img id="v" alt="result is not ready"></div></div><br>
<div class="blobSampler" id="blobSampler"><span>Blob pattern index</span><input id="blobPattern" type="number" min="0" value="0">
<button onclick="configureBlobFromSelection()">Configure blob from selected area</button>
<span class="hint">Drag a rectangle over the source image</span></div>
<div id="modes"></div>
<h4>Параметры <span class="hint">(секция активного режима + общие)</span></h4>
<div><label class="hint"><input type="checkbox" id="all" onchange="render()"> показать все секции</label>
&nbsp;<label class="hint"><input type="checkbox" id="raw" onchange="render()"> редактировать JSON</label></div>
<div id="paramTabs"><button id="detectorTab" class="act" onclick="setParamTab('detector')">Detector parameters</button>
<button id="generalTab" onclick="setParamTab('general')">General parameters</button></div>
<div id="form"></div>
<textarea id="cfg" spellcheck="false" style="display:none"></textarea><br>
<button onclick="send('/apply')">Применить (до перезапуска)</button>
<button onclick="send('/config')">Сохранить (постоянно)</button>
<button onclick="revert()">Откатить к сохранённому</button><span id="st"></span>
<details><summary>CM5 system administration</summary><div class="body">
<div class="row"><label>Admin token</label><input id="admToken" type="password"></div>
<button onclick="adminGet('status')">System status</button>
<button onclick="adminGet('processes')">Processes</button>
<button onclick="adminGet('network')">Network</button>
<button onclick="adminPost({op:'files',path:document.getElementById('admPath').value})">Files</button>
<div class="row"><label>Path under file_root</label><input id="admPath" type="text" value="."></div>
<button onclick="adminPost({op:'read_file',path:admPath.value})">Read file</button>
<button onclick="adminPost({op:'write_file',path:admPath.value,content:admOut.value})">Write output to file</button>
<div class="row"><label>PID</label><input id="admPid" type="number" min="2"></div>
<button onclick="adminPost({op:'process',pid:Number(admPid.value),action:'stop'})">Stop</button>
<button onclick="adminPost({op:'process',pid:Number(admPid.value),action:'kill'})">Kill</button>
<button onclick="adminPost({op:'process',pid:Number(admPid.value),action:'pause'})">Pause</button>
<button onclick="adminPost({op:'process',pid:Number(admPid.value),action:'resume'})">Resume</button>
<div class="row"><label>Network request JSON</label><input id="admNet" type="text" value='{"op":"network","connection":"Wi-Fi","mode":"dhcp"}'></div>
<button onclick="adminNetwork()">Apply network configuration</button>
<div class="row"><label>Terminal command</label><input id="admCmd" type="text"></div>
<button onclick="adminPost({op:'terminal',command:document.getElementById('admCmd').value})">Execute</button>
<textarea id="admOut" spellcheck="false"></textarea>
</div></details>
<script>
const modes=['off','aruco_detection','object_detection','blob_detection','line_detection','circle_detection'];
const imageUrls={};
async function refreshImage(id,path){
  try{
    const response=await fetch(path+'?t='+Date.now(),{cache:'no-store'});
    const type=response.headers.get('content-type')||'';
    if(!response.ok||!type.startsWith('image/jpeg'))throw new Error('frame is not ready');
    const blob=await response.blob(),url=URL.createObjectURL(blob),image=document.getElementById(id);
    await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=reject;image.src=url;});
    if(id==='src')resizeSelectionOverlay();
    if(imageUrls[id])URL.revokeObjectURL(imageUrls[id]);
    imageUrls[id]=url;
  }catch(error){}
  setTimeout(()=>refreshImage(id,path),200);
}
refreshImage('src','/source.jpg');
refreshImage('v','/preview.jpg');
const selectionCanvas=document.getElementById('selection'),selectionContext=selectionCanvas.getContext('2d');
let selectionStart=null,selectionRect=null;
function resizeSelectionOverlay(){
  const image=document.getElementById('src'),rect=image.getBoundingClientRect();
  selectionCanvas.width=Math.max(1,Math.round(rect.width));selectionCanvas.height=Math.max(1,Math.round(rect.height));drawSelection();
}
function selectionPoint(event){const rect=selectionCanvas.getBoundingClientRect();return{x:(event.clientX-rect.left)*selectionCanvas.width/rect.width,y:(event.clientY-rect.top)*selectionCanvas.height/rect.height};}
function drawSelection(){selectionContext.clearRect(0,0,selectionCanvas.width,selectionCanvas.height);if(!selectionRect)return;
  selectionContext.fillStyle='rgba(40,180,255,.18)';selectionContext.strokeStyle='#28b4ff';selectionContext.lineWidth=2;
  selectionContext.fillRect(selectionRect.x,selectionRect.y,selectionRect.w,selectionRect.h);selectionContext.strokeRect(selectionRect.x,selectionRect.y,selectionRect.w,selectionRect.h);}
selectionCanvas.onpointerdown=event=>{selectionStart=selectionPoint(event);selectionCanvas.setPointerCapture(event.pointerId);};
selectionCanvas.onpointermove=event=>{if(!selectionStart)return;const point=selectionPoint(event);
  selectionRect={x:Math.min(selectionStart.x,point.x),y:Math.min(selectionStart.y,point.y),w:Math.abs(point.x-selectionStart.x),h:Math.abs(point.y-selectionStart.y)};drawSelection();};
selectionCanvas.onpointerup=()=>{selectionStart=null;};
function percentile(values,fraction){values.sort((a,b)=>a-b);return values[Math.min(values.length-1,Math.floor(values.length*fraction))];}
function configureBlobFromSelection(){
  const currentConfig=current();if(!currentConfig)return;cfgObj=currentConfig;
  const image=document.getElementById('src'),patterns=((cfgObj.blob_detection||{}).one_color_patterns||[]);
  const index=Number(document.getElementById('blobPattern').value);
  if(!selectionRect||selectionRect.w<4||selectionRect.h<4){st.textContent='Select a blob area on the source image first.';return;}
  if(!image.naturalWidth||!image.naturalHeight){st.textContent='Source frame is not ready.';return;}
  if(!Number.isInteger(index)||index<0||index>=patterns.length){st.textContent='Blob pattern index is out of range.';return;}
  const scaleX=image.naturalWidth/selectionCanvas.width,scaleY=image.naturalHeight/selectionCanvas.height;
  const roi={x:Math.max(0,Math.floor(selectionRect.x*scaleX)),y:Math.max(0,Math.floor(selectionRect.y*scaleY)),
    w:Math.max(1,Math.floor(selectionRect.w*scaleX)),h:Math.max(1,Math.floor(selectionRect.h*scaleY))};
  roi.w=Math.min(roi.w,image.naturalWidth-roi.x);roi.h=Math.min(roi.h,image.naturalHeight-roi.y);
  const sample=document.createElement('canvas');sample.width=image.naturalWidth;sample.height=image.naturalHeight;
  const context=sample.getContext('2d',{willReadFrequently:true});context.drawImage(image,0,0);
  const pixels=context.getImageData(roi.x,roi.y,roi.w,roi.h).data,samples=[],centerSamples=[];
  const stride=Math.max(1,Math.floor((roi.w*roi.h)/12000));
  for(let pixel=0;pixel<roi.w*roi.h;pixel+=stride){const offset=pixel*4,r=pixels[offset],g=pixels[offset+1],b=pixels[offset+2],y=.299*r+.587*g+.114*b;
    const sample=[y,(r-y)*.713+128,(b-y)*.564+128],x=pixel%roi.w,row=Math.floor(pixel/roi.w);samples.push(sample);
    if(x>roi.w*.35&&x<roi.w*.65&&row>roi.h*.35&&row<roi.h*.65)centerSamples.push(sample);}
  const trainingSamples=centerSamples.length?centerSamples:samples;
  const center=[0,1,2].map(channel=>percentile(trainingSamples.map(sample=>sample[channel]),.5));
  let foreground=samples.filter(sample=>Math.hypot(sample[1]-center[1],sample[2]-center[2])<=24&&Math.abs(sample[0]-center[0])<=55);
  if(foreground.length<samples.length*.1)foreground=samples.sort((a,b)=>Math.hypot(a[0]-center[0],a[1]-center[1],a[2]-center[2])-Math.hypot(b[0]-center[0],b[1]-center[1],b[2]-center[2])).slice(0,Math.max(1,Math.floor(samples.length*.5)));
  const channels=[0,1,2].map(channel=>foreground.map(sample=>sample[channel]));
  const lower=channels.map(values=>clampByte(percentile(values,.05)-6));
  const upper=channels.map(values=>clampByte(percentile(values,.95)+6));
  const pattern=patterns[index],area=roi.w*roi.h*foreground.length/samples.length;
  pattern.lower_range=lower;pattern.upper_range=upper;pattern.min_area=Math.max(1,Math.round(area*.45));pattern.max_area=Math.round(area*1.9);
  pattern.min_width=Math.max(1,Math.round(roi.w*.5));pattern.min_height=Math.max(1,Math.round(roi.h*.5));
  pattern.min_luminance=lower[0];pattern.max_luminance=upper[0];
  pattern.min_chrominance_red=lower[1];pattern.max_chrominance_red=upper[1];
  pattern.min_chrominance_blue=lower[2];pattern.max_chrominance_blue=upper[2];
  st.textContent='Blob pattern '+index+' configured from '+roi.w+'×'+roi.h+' px selection. Review and Apply settings.';
  render();
}
const md=document.getElementById('modes');
let cur='', cfgObj={}, paramTab='detector';
modes.forEach(m=>{const b=document.createElement('button');b.textContent=m;b.dataset.m=m;
b.onclick=()=>fetch('/mode/'+m).then(r=>r.text()).then(t=>{st.textContent=t;load();});
md.appendChild(b);});
function mark(){[...md.children].forEach(b=>b.className=b.dataset.m===cur?'act':'');document.getElementById('blobSampler').style.display=cur==='blob_detection'?'flex':'none';}
function setParamTab(tab){
  paramTab=tab;
  document.getElementById('detectorTab').className=tab==='detector'?'act':'';
  document.getElementById('generalTab').className=tab==='general'?'act':'';
  document.getElementById('all').parentElement.style.display=tab==='detector'?'':'none';
  render();
}
function el(t,c){const e=document.createElement(t);if(c)e.className=c;return e;}
const labels={
min_radius:'Minimum radius, px',max_radius:'Maximum radius, px',distance:'Minimum center distance, px',
hough_param1:'Edge threshold',hough_param2:'Accumulator threshold',min_area:'Minimum area, px²',max_area:'Maximum area, px²',
min_width:'Minimum width, px',min_height:'Minimum height, px',min_circularity:'Minimum circularity, 0–1',max_circularity:'Maximum circularity, 0–1',
min_inertia:'Minimum inertia, 0–1',max_inertia:'Maximum inertia, 0–1',min_convexity:'Minimum convexity, 0–1',max_convexity:'Maximum convexity, 0–1',
min_vertices:'Minimum vertices',max_vertices:'Maximum vertices',polygon_approximation:'Polygon approximation, contour fraction',
lower_range:'Lower YCrCb range [Y, Cr, Cb]',upper_range:'Upper YCrCb range [Y, Cr, Cb]',min_luminance:'Minimum luminance Y, 0–255',max_luminance:'Maximum luminance Y, 0–255',
min_chrominance_red:'Minimum chrominance Cr, 0–255',max_chrominance_red:'Maximum chrominance Cr, 0–255',min_chrominance_blue:'Minimum chrominance Cb, 0–255',max_chrominance_blue:'Maximum chrominance Cb, 0–255',
enable_one_color_detection:'Enable single-color detection',enable_multicolor_detection:'Enable composite-color detection',max_composite_objects:'Maximum composite objects',
canny_threshold1:'Canny lower threshold',canny_threshold2:'Canny upper threshold',canny_aperture_size:'Canny aperture size, px',canny_l2_gradient:'Use precise L2 gradient',
hough_rho:'Hough distance resolution, px',hough_theta:'Hough angle resolution, rad',hough_threshold:'Hough vote threshold',hough_min_line_length:'Minimum line length, px',hough_max_line_gap:'Maximum line gap, px',
min_angle:'Minimum angle, deg',max_angle:'Maximum angle, deg',max_lines:'Maximum detected lines',roi_x:'ROI X, frame fraction',roi_y:'ROI Y, frame fraction',roi_width:'ROI width, frame fraction',roi_height:'ROI height, frame fraction',
dictionary:'ArUco dictionary',marker_length:'Marker side length, m',allowed_ids:'Allowed marker IDs',confidence_threshold:'Confidence threshold, 0–1',nms_threshold:'NMS IoU threshold, 0–1',max_objects:'Maximum detected objects',
input_width:'Neural network input width, px',input_height:'Neural network input height, px',model_onnx:'ONNX model path',class_names_file:'Class names file',camera_rotation:'Camera rotation, deg',exposure_ev:'Exposure compensation, EV',
white_balance_bgr:'White balance gains [B, G, R]',contrast:'Contrast multiplier',brightness:'Brightness offset',processing_mode:'Processing mode',debug_mode:'Enable debug mode',max_fps:'Maximum frame rate, FPS',
jpeg_quality:'JPEG quality, 0–100',packet_size:'UDP packet size, bytes',port:'Network port',baud:'UART baud rate, bit/s',push_interval_ms:'Push interval, ms',image_size:'Training image size, px',epochs:'Training epochs',
threshold:'Score threshold, 0–1',overall_threshold:'Overall score threshold, 0–1',weight:'Criterion weight, 0–255',goal:'Target value',angle:'Angle, deg',angle_absolute:'Absolute angle, deg',angle_relative:'Relative angle, deg',length_absolute:'Absolute length, px',length_relative:'Relative length ratio'
};
const hints={
min_radius:'Smallest circle radius accepted by the detector, in pixels.',max_radius:'Largest circle radius accepted by the detector, in pixels.',distance:'Minimum distance between centers of two detected circles, in pixels.',
hough_param1:'Upper edge threshold used internally by the Hough circle detector.',hough_param2:'Circle-center accumulator threshold. Lower values detect more circles and more false positives.',
min_area:'Reject regions whose contour area is smaller than this value.',max_area:'Reject regions whose contour area is larger than this value.',min_width:'Minimum accepted bounding-box width.',min_height:'Minimum accepted bounding-box height.',
min_circularity:'Minimum contour circularity: 1 is a perfect circle and 0 is highly irregular.',max_circularity:'Maximum accepted contour circularity.',min_inertia:'Minimum inertia ratio; values near 1 describe round shapes and values near 0 elongated shapes.',max_inertia:'Maximum accepted inertia ratio.',
min_convexity:'Minimum contour area divided by convex-hull area.',max_convexity:'Maximum contour area divided by convex-hull area.',min_vertices:'Minimum number of vertices after polygon approximation; 0 disables this limit.',max_vertices:'Maximum number of vertices after polygon approximation; 0 disables this limit.',polygon_approximation:'Approximation accuracy relative to contour perimeter. Smaller values preserve more vertices.',
lower_range:'Inclusive lower color threshold in YCrCb channel order: luminance, red chrominance, blue chrominance.',upper_range:'Inclusive upper color threshold in YCrCb channel order.',min_luminance:'Minimum accepted brightness in the Y channel.',max_luminance:'Maximum accepted brightness in the Y channel.',min_chrominance_red:'Minimum accepted Cr channel value.',max_chrominance_red:'Maximum accepted Cr channel value.',min_chrominance_blue:'Minimum accepted Cb channel value.',max_chrominance_blue:'Maximum accepted Cb channel value.',
enable_one_color_detection:'Enables detection and reporting of individual color regions.',enable_multicolor_detection:'Enables matching of composite objects formed by linked color regions.',max_composite_objects:'Maximum number of composite objects reported in one frame.',one_color_patterns:'Definitions of individual YCrCb color patterns.',multicolor_patterns:'Definitions of composite objects and spatial links between their parts.',blob_id:'IDs of color patterns allowed for this composite-object node.',
canny_threshold1:'Lower hysteresis threshold for the Canny edge detector.',canny_threshold2:'Upper hysteresis threshold for the Canny edge detector.',canny_aperture_size:'Sobel kernel size used by Canny; normally 3, 5, or 7.',canny_l2_gradient:'Uses the more accurate Euclidean gradient magnitude when enabled.',
hough_rho:'Distance resolution of the Hough line accumulator.',hough_theta:'Angular resolution of the Hough line accumulator in radians.',hough_threshold:'Minimum accumulator votes required to accept a line.',hough_min_line_length:'Reject line segments shorter than this length.',hough_max_line_gap:'Maximum gap between collinear segments that may be joined.',min_angle:'Smallest accepted line orientation in degrees.',max_angle:'Largest accepted line orientation in degrees.',max_lines:'Maximum number of line segments returned per frame.',
roi_x:'Horizontal start of the processing region, normalized to frame width from 0 to 1.',roi_y:'Vertical start of the processing region, normalized to frame height from 0 to 1.',roi_width:'Processing-region width as a fraction of frame width.',roi_height:'Processing-region height as a fraction of frame height.',
dictionary:'Predefined ArUco dictionary used to decode markers; it must match the printed markers.',marker_length:'Physical marker side length used for pose estimation, in meters.',allowed_ids:'Optional marker ID allowlist. An empty list accepts every ID.',
model_onnx:'Path to the YOLO ONNX model, relative to the application working directory or absolute.',class_names_file:'Text file containing one class name per line in model class order.',class_names:'Inline class-name list used when no external names are supplied.',input_width:'Width to which the neural-network input is letterboxed.',input_height:'Height to which the neural-network input is letterboxed.',confidence_threshold:'Minimum class confidence required before non-maximum suppression.',nms_threshold:'Intersection-over-union threshold used to suppress overlapping boxes.',max_objects:'Maximum number of neural-network detections returned per frame.',
processing_mode:'Selects the active image-processing algorithm.',camera_rotation:'Clockwise rotation applied to captured frames; use 0, 90, 180, or 270 degrees.',exposure_ev:'Exposure compensation in exposure-value stops; positive values brighten the image.',white_balance_bgr:'Per-channel gain multipliers applied in blue, green, red order.',contrast:'Pixel contrast multiplier; 1 leaves contrast unchanged.',brightness:'Brightness offset added to pixel channels.',debug_mode:'Enables additional diagnostic output and debug behavior.',
enabled:'Enables this pattern, transport, or subsystem. A disabled color pattern is ignored by single-color and composite blob detection.',host:'Destination host name or IP address.',port:'UDP or TCP destination/listening port.',jpeg_quality:'JPEG encoding quality; larger values improve quality and increase traffic.',packet_size:'Maximum UDP datagram payload size.',max_fps:'Maximum video frames transmitted each second.',device:'Linux device path used by this transport.',metadata:'Enables metadata records on this transport.',video:'Enables encoded video frames on this transport.',
baud:'UART line speed; both ends must use the same value.',rs485:'Enables Linux RS-485 direction-control mode when supported by the UART driver.',startup_push:'Starts periodic DXL metadata transmission immediately after launch.',push_interval_ms:'Delay between automatic metadata packets.',eeprom_file:'File used to persist virtual Dynamixel EEPROM values.',
token:'Token required by privileged web-administration endpoints.',file_root:'Filesystem root exposed by the web file manager.',terminal_enabled:'Allows execution of terminal commands through the administration API.',
data_yaml:'YOLO dataset description containing train/validation paths and class names.',base_model:'Pretrained model used as the starting point for training.',epochs:'Number of complete passes over the training dataset.',image_size:'Square image size used during training.',output_onnx:'Output path for the exported trained ONNX model.',
id:'Numeric identifier of this pattern, object, node, transport, or marker.',threshold:'Minimum normalized score required for this criterion.',overall_threshold:'Minimum combined score required to accept a composite object.',weight:'Relative contribution of this criterion; zero disables its contribution.',goal:'Ideal criterion value that receives the highest score.',
size:'Size-matching criteria for a composite-object node.',size_measure:'Blob property used for relative size comparisons, such as area, width, or maximum axis.',circularity:'Circularity-matching criteria for a composite-object node.',inertia:'Inertia-ratio matching criteria for a composite-object node.',convexity:'Convexity-matching criteria for a composite-object node.',angle:'Orientation-matching criteria in degrees.',
min:'Lowest value accepted by this criterion.',max:'Highest value accepted by this criterion.',nodes:'Primitive parts required to form this composite object.',links:'Spatial relationships required between composite-object parts.',length_absolute:'Allowed absolute distance between linked parts, in pixels.',length_relative:'Allowed distance relative to the base part size.',angle_absolute:'Allowed absolute direction between linked parts, in degrees.',angle_relative:'Allowed direction relative to the base link, in degrees.'
};
function fieldLabel(key){return labels[key]||key.replaceAll('_',' ').replace(/^./,c=>c.toUpperCase());}
function fieldHint(key){return hints[key]||'Configuration parameter: '+fieldLabel(key)+'.';}
function clampByte(v){return Math.max(0,Math.min(255,Math.round(v)));}
function yCrCbToHex(v){
  const y=v[0],cr=v[1]-128,cb=v[2]-128;
  const r=clampByte(y+1.403*cr),g=clampByte(y-.714*cr-.344*cb),b=clampByte(y+1.773*cb);
  return '#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
}
function hexToYCrCb(hex){
  const r=parseInt(hex.slice(1,3),16),g=parseInt(hex.slice(3,5),16),b=parseInt(hex.slice(5,7),16);
  const y=.299*r+.587*g+.114*b;
  return [clampByte(y),clampByte((r-y)*.713+128),clampByte((b-y)*.564+128)];
}
function pathInput(path){
  const encoded=JSON.stringify(path);
  return [...document.querySelectorAll('[data-path]')].find(e=>e.dataset.path===encoded);
}
function addBlobColorPicker(row,path,lower){
  if(path.length<4||path[0]!=='blob_detection'||path[1]!=='one_color_patterns'||path[path.length-1]!=='lower_range')return;
  const upperPath=path.slice(); upperPath[upperPath.length-1]='upper_range';
  const configuredUpper=cfgObj.blob_detection.one_color_patterns[path[2]].upper_range;
  const center=lower.map((value,index)=>(value+configuredUpper[index])/2);
  const caption=el('span','hint'); caption.textContent='Color'; row.appendChild(caption);
  const picker=el('input'); picker.type='color'; picker.value=yCrCbToHex(center);
  picker.title='Select the target blob color. The current YCrCb tolerance width is preserved.';
  picker.oninput=()=>{
    const lowerInput=pathInput(path),upperInput=pathInput(upperPath);
    if(!lowerInput||!upperInput)return;
    const low=lowerInput.value.split(',').map(Number),high=upperInput.value.split(',').map(Number);
    const half=[0,1,2].map(index=>Math.max(1,(high[index]-low[index])/2));
    const selected=hexToYCrCb(picker.value);
    lowerInput.value=selected.map((value,index)=>clampByte(value-half[index])).join(', ');
    upperInput.value=selected.map((value,index)=>clampByte(value+half[index])).join(', ');
  };
  row.appendChild(picker);
}
function fieldRow(key,val,path){
  const r=el('div','row'), l=el('label'); l.textContent=fieldLabel(key); l.title=fieldHint(key)+' JSON key: '+key; r.appendChild(l);
  let i=el('input');
  if(typeof val==='boolean'){i.type='checkbox';i.checked=val;i.dataset.t='b';}
  else if(typeof val==='number'){i.type='number';i.value=val;i.dataset.t='n';
    i.step=Number.isInteger(val)?'1':'any';}
  else if(Array.isArray(val)){i.type='text';i.value=val.join(', ');i.dataset.t='a';
    i.dataset.num=val.every(x=>typeof x==='number')?'1':'0';}
  else{i.type='text';i.value=val;i.dataset.t='s';}
  i.title=l.title; i.dataset.path=JSON.stringify(path); r.appendChild(i);
  if(key==='lower_range'&&Array.isArray(val))addBlobColorPicker(r,path,val);
  return r;
}
function buildForm(obj,path,parent,open){
  for(const k of Object.keys(obj)){
    const v=obj[k], p=path.concat([k]);
    const isObj=v&&typeof v==='object'&&!Array.isArray(v);
    const isObjArr=Array.isArray(v)&&v.some(x=>x&&typeof x==='object');
    if(isObj||isObjArr){
      const d=el('details'); if(open)d.open=true;
      const s=el('summary'); s.textContent=k+(isObjArr?' ['+v.length+']':''); d.appendChild(s);
      const b=el('div','body'); d.appendChild(b);
      if(isObj) buildForm(v,p,b,false);
      else v.forEach((item,idx)=>{
        if(item&&typeof item==='object'){
          const dd=el('details'), ss=el('summary');
          ss.textContent='['+idx+']'+(item.id!==undefined?'  id='+item.id:'');
          dd.appendChild(ss);
          const bb=el('div','body'); dd.appendChild(bb);
          buildForm(item,p.concat([idx]),bb,false); b.appendChild(dd);
        }else b.appendChild(fieldRow('['+idx+']',item,p.concat([idx])));
      });
      parent.appendChild(d);
    }else parent.appendChild(fieldRow(k,v,p));
  }
}
function render(){
  const rawMode=document.getElementById('raw').checked;
  document.getElementById('cfg').style.display=rawMode?'':'none';
  document.getElementById('form').style.display=rawMode?'none':'';
  if(rawMode){document.getElementById('cfg').value=JSON.stringify(cfgObj,null,2);return;}
  const showAll=document.getElementById('all').checked;
  const f=document.getElementById('form'); f.innerHTML='';
  const sub={}; Object.keys(cfgObj).filter(k=>paramTab==='general'?k==='general_params':(k!=='general_params'&&(showAll||k===cur)))
    .forEach(k=>sub[k]=cfgObj[k]);
  buildForm(sub,[],f,true);
}
function collect(){
  const o=JSON.parse(JSON.stringify(cfgObj));
  document.querySelectorAll('[data-path]').forEach(i=>{
    const p=JSON.parse(i.dataset.path);
    let t=o; for(let k=0;k<p.length-1;k++) t=t[p[k]];
    const last=p[p.length-1];
    if(i.dataset.t==='b') t[last]=i.checked;
    else if(i.dataset.t==='n') t[last]=parseFloat(i.value);
    else if(i.dataset.t==='a'){
      const parts=i.value.split(',').map(s=>s.trim()).filter(s=>s!=='');
      t[last]=i.dataset.num==='1'?parts.map(Number):parts;
    }else t[last]=i.value;
  });
  return o;
}
function normalizeConfig(config){
  (((config||{}).blob_detection||{}).one_color_patterns||[]).forEach(pattern=>{
    if(pattern.enabled===undefined)pattern.enabled=true;
  });
  return config;
}
function load(){fetch('/config').then(r=>r.json()).then(j=>{
  cfgObj=normalizeConfig(j); cur=(j.general_params||{}).processing_mode||cur; mark(); render();});}
function current(){
  if(document.getElementById('raw').checked){
    try{return JSON.parse(document.getElementById('cfg').value);}
    catch(e){st.textContent='невалидный json: '+e.message;return null;}
  }
  return collect();
}
function send(ep){
  const o=current(); if(!o)return;
  fetch(ep,{method:'POST',body:JSON.stringify(o,null,2)})
    .then(r=>r.text()).then(t=>{st.textContent=t;
      if(ep==='/config')load(); else {cfgObj=o;cur=(o.general_params||{}).processing_mode||cur;mark();}});
}
function revert(){fetch('/revert').then(r=>r.text()).then(t=>{st.textContent=t;load();});}
function admHeaders(){return {'X-Admin-Token':document.getElementById('admToken').value};}
function adminShow(t){try{admOut.value=JSON.stringify(JSON.parse(t),null,2);}catch(e){admOut.value=t;}}
function adminGet(view){fetch('/admin/'+view,{headers:admHeaders()}).then(r=>r.text()).then(adminShow);}
function adminPost(body){fetch('/admin',{method:'POST',headers:admHeaders(),body:JSON.stringify(body)})
  .then(r=>r.text()).then(adminShow);}
function adminNetwork(){try{adminPost(JSON.parse(admNet.value));}catch(e){adminShow(e.message);}}
load();
</script></body></html>)HTML";

static bool sendAll(int socketFd, const char* data, size_t size)
{
    while (size > 0)
    {
        ssize_t sent = send(socketFd, data, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return false;
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

static void sendResp(int c, const char* type, const std::string& body)
{
    char hdr[256];
    int k = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n"
                     "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                     type, body.size());
    if (sendAll(c, hdr, static_cast<size_t>(k)))
        sendAll(c, body.data(), body.size());
}

static void controlServer(int port)
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(sfd, (struct sockaddr*)&a, sizeof(a)) || listen(sfd, 2))
    {
        perror("control bind/listen");
        return;
    }
    std::cout << "control UI: http://<ip>:" << port << "/" << std::endl;

    for (;;)
    {
        int c = accept(sfd, nullptr, nullptr);
        if (c < 0)
            continue;

        // читаем запрос целиком (заголовки + тело по Content-Length)
        std::string req;
        char buf[4096];
        size_t bodyStart = std::string::npos;
        long contentLen = 0;
        while (req.size() < 256 * 1024)
        {
            ssize_t n = read(c, buf, sizeof(buf));
            if (n <= 0)
                break;
            req.append(buf, n);
            if (bodyStart == std::string::npos)
            {
                size_t p = req.find("\r\n\r\n");
                if (p == std::string::npos)
                    continue;
                bodyStart = p + 4;
                size_t cl = req.find("Content-Length:");
                if (cl != std::string::npos && cl < p)
                    contentLen = atol(req.c_str() + cl + 15);
            }
            if (bodyStart != std::string::npos &&
                req.size() >= bodyStart + (size_t)contentLen)
                break;
        }

        char mode[64] = {0};
        bool isApply = req.compare(0, 12, "POST /apply ") == 0;
        bool isSave = req.compare(0, 13, "POST /config ") == 0;

#ifdef RASPBERRY_CM5
        const bool isAdminPost = req.compare(0, 12, "POST /admin ") == 0;
        const bool isAdminGet = req.compare(0, 11, "GET /admin/") == 0;
        if ((isAdminPost || isAdminGet) &&
            (!gSystemAdmin || !gSystemAdmin->authorized(req)))
        {
            sendResp(c, "application/json", R"({"error":"unauthorized"})");
        }
        else if (isAdminGet)
        {
            try
            {
                if (req.compare(0, 18, "GET /admin/status ") == 0)
                    sendResp(c, "application/json", gSystemAdmin->status().dump());
                else if (req.compare(0, 21, "GET /admin/processes ") == 0)
                    sendResp(c, "application/json", gSystemAdmin->processes().dump());
                else if (req.compare(0, 19, "GET /admin/network ") == 0)
                    sendResp(c, "application/json", gSystemAdmin->network().dump());
                else
                    sendResp(c, "application/json", R"({"error":"unknown view"})");
            }
            catch (const std::exception& error)
            {
                sendResp(c, "application/json", nlohmann::json{{"error", error.what()}}.dump());
            }
        }
        else if (isAdminPost && bodyStart != std::string::npos)
        {
            try
            {
                const auto body = nlohmann::json::parse(req.substr(bodyStart));
                const std::string operation = body.at("op").get<std::string>();
                if (operation == "files")
                    sendResp(c, "application/json",
                             gSystemAdmin->listFiles(body.value("path", ".")).dump());
                else if (operation == "read_file")
                    sendResp(c, "application/json", nlohmann::json{{"content",
                        gSystemAdmin->readFile(body.at("path").get<std::string>())}}.dump());
                else if (operation == "write_file")
                {
                    gSystemAdmin->writeFile(body.at("path").get<std::string>(),
                                            body.at("content").get<std::string>());
                    sendResp(c, "application/json", R"({"status":"ok"})");
                }
                else if (operation == "process")
                    sendResp(c, "application/json", nlohmann::json{{"status",
                        gSystemAdmin->processAction(body.at("pid").get<int>(),
                                                    body.at("action").get<std::string>())}}.dump());
                else if (operation == "network")
                    sendResp(c, "application/json", nlohmann::json{{"output",
                        gSystemAdmin->configureNetwork(body)}}.dump());
                else if (operation == "terminal")
                    sendResp(c, "application/json", nlohmann::json{{"output",
                        gSystemAdmin->execute(body.at("command").get<std::string>())}}.dump());
                else
                    sendResp(c, "application/json", R"({"error":"unknown operation"})");
            }
            catch (const std::exception& error)
            {
                sendResp(c, "application/json", nlohmann::json{{"error", error.what()}}.dump());
            }
        }
        else
#endif
        if ((isApply || isSave) && bodyStart != std::string::npos)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(req.substr(bodyStart));
                std::string err = gApply ? gApply(j) : "";   // сначала проверка боем
                if (!err.empty())
                {
                    sendResp(c, "text/plain", "не применилось: " + err + "\n");
                }
                else if (isApply)
                {
                    // только в память: перезапуск вернёт сохранённый конфиг
                    sendResp(c, "text/plain",
                             "применено (до перезапуска)\n");
                }
                else
                {
                    std::string se = saveConfigAtomic(j);
                    sendResp(c, "text/plain", se.empty()
                        ? "сохранено в " + gConfigPath + " (действует после перезапуска)\n"
                        : "применено, но НЕ сохранено: " + se + "\n");
                }
            }
            catch (const std::exception& e)
            {
                sendResp(c, "text/plain",
                         std::string("невалидный json: ") + e.what() + "\n");
            }
        }
        else if (sscanf(req.c_str(), "GET /mode/%63[a-z_]", mode) == 1 && mode[0])
        {
            // смена режима — сразу постоянная (сохраняется в файл)
            try
            {
                std::ifstream f(gConfigPath);
                nlohmann::json j;
                f >> j;
                j["general_params"]["processing_mode"] = mode;
                std::string err = gApply ? gApply(j) : "";
                if (!err.empty())
                    sendResp(c, "text/plain", "не применилось: " + err + "\n");
                else
                {
                    std::string se = saveConfigAtomic(j);
                    sendResp(c, "text/plain", std::string("mode=") + mode +
                             (se.empty() ? " (сохранено)\n"
                                         : " (НЕ сохранено: " + se + ")\n"));
                }
            }
            catch (const std::exception& e)
            {
                sendResp(c, "text/plain",
                         std::string("error: ") + e.what() + "\n");
            }
        }
        else if (req.compare(0, 12, "GET /revert ") == 0)
        {
            // откат к сохранённому на диске
            try
            {
                std::ifstream f(gConfigPath);
                nlohmann::json j;
                f >> j;
                std::string err = gApply ? gApply(j) : "";
                sendResp(c, "text/plain", err.empty()
                    ? "восстановлен сохранённый конфиг\n" : "ошибка: " + err + "\n");
            }
            catch (const std::exception& e)
            {
                sendResp(c, "text/plain", std::string("error: ") + e.what() + "\n");
            }
        }
        else if (req.compare(0, 16, "GET /preview.jpg") == 0)
        {
            std::string jpeg;
            if (getPreviewJpeg(false, jpeg))
                sendResp(c, "image/jpeg", jpeg);
            else
                sendResp(c, "text/plain", "preview is not ready\n");
        }
        else if (req.compare(0, 15, "GET /source.jpg") == 0)
        {
            std::string jpeg;
            if (getPreviewJpeg(true, jpeg))
                sendResp(c, "image/jpeg", jpeg);
            else
                sendResp(c, "text/plain", "source is not ready\n");
        }
        else if (req.compare(0, 12, "GET /config ") == 0)
        {
            std::ifstream f(gConfigPath);
            std::stringstream ss;
            ss << f.rdbuf();
            sendResp(c, "application/json", ss.str());
        }
        else
        {
            sendResp(c, "text/html; charset=utf-8", kPage);
        }
        close(c);
    }
}
#endif // MTV3_BOARD || RASPBERRY_CM5

int main(int argc, char** argv)
{
    // --dump N [--dump-dir D]: каждые N кадров сохранять D/last.jpg (результат)
    // и D/last_src.jpg (исходник); файлы перезаписываются — tmpfs не растёт
    // --config P: путь к config.json (по умолчанию ./config.json)
    int dumpEvery = 0;
    int cameraDevice = 0;
    std::string dumpDir = "/tmp";
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dumpEvery = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc)
            dumpDir = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc)
            gConfigPath = argv[++i];
        else if (!strcmp(argv[i], "--camera") && i + 1 < argc)
            cameraDevice = atoi(argv[++i]);
    }

    // 1. Конфиг
    if (dumpEvery > 0)
    {
        std::error_code error;
        std::filesystem::create_directories(dumpDir, error);
        if (error)
        {
            std::cerr << "Failed to create dump directory: " << dumpDir
                      << ": " << error.message() << std::endl;
            return -1;
        }
    }

    ConfigReader reader;
    if (!reader.loadFromFile(gConfigPath))
    {
        std::cerr << "Failed to open config file: " << gConfigPath << std::endl;
        return -1;
    }

    // 2-3. Процессор (фабрика внутри менеджера)
    ProcessingManager manager(reader.getRawConfig());

    // 4. Источник кадров
#ifdef MTV3_BOARD
    ShmSource source;                    // ждёт /dev/shm/mtv3cam до 5 с
    if (!source.isOpened())
    {
        std::cerr << "Failed to open /dev/shm/mtv3cam (daemon not running?)" << std::endl;
        return -1;
    }
    std::cout << "shm source: " << source.width() << "x" << source.height() << std::endl;
#elif defined(HOST_WEB_UI)
    SyntheticSource source;
    std::cout << "host UI source: synthetic 640x480 @ 30 FPS" << std::endl;
#else
    CameraSource source(cameraDevice);
    if (!source.isOpened())
    {
        std::cerr << "Failed to open video source" << std::endl;
        return -1;
    }
#endif

    // 5. Sink
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
    // headless: fps раз в 100 кадров; сюда же встанет отправка metadata наружу
    auto t0 = std::chrono::steady_clock::now();
    long frames = 0;
    double currentFps = 0.0;
#ifdef RASPBERRY_CM5
    std::unique_ptr<TransportManager> transports;
#endif
    Pipeline pipeline(source, manager, [&](const ProcessedItem& item) {
        ++frames;
        updatePreview(item.frame, item.result);
        if (!item.metadata.empty() && frames % 10 == 0)
        {
            std::cout << "det f" << frames << ":";
            constexpr size_t kMaxLoggedObjects = 20;
            size_t loggedObjects = 0;
            for (const auto& m : item.metadata)
            {
                if (loggedObjects++ >= kMaxLoggedObjects)
                    break;
                std::cout << " id=" << m.id
                          << "(" << (int)m.center.x << "," << (int)m.center.y << ")";
            }
            if (item.metadata.size() > kMaxLoggedObjects)
                std::cout << " ... +" << item.metadata.size() - kMaxLoggedObjects;
            std::cout << std::endl;
        }
        if (dumpEvery && frames % dumpEvery == 0)
        {
            cv::imwrite(dumpDir + "/last.jpg", item.result);
            cv::imwrite(dumpDir + "/last_src.jpg", item.frame);
        }
#ifdef RASPBERRY_CM5
        if (transports)
        {
            VisionFrame transportFrame;
            transportFrame.frameId = item.frameId;
            transportFrame.timestampMs = item.timestampMs;
            transportFrame.imageWidth = static_cast<uint16_t>(item.frame.cols);
            transportFrame.imageHeight = static_cast<uint16_t>(item.frame.rows);
            transportFrame.detectorType = manager.processingType();
            transportFrame.inferenceUs = item.inferenceUs;
            transportFrame.fps = static_cast<float>(currentFps);
            transportFrame.objects = item.metadata;
            transports->publish(transportFrame, item.result);
        }
#endif
        if (frames % 100 == 0)
        {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - t0).count();
            currentFps = 100.0 / dt;
            std::cout << "fps=" << currentFps
                      << " frames=" << frames
                      << " objects=" << item.metadata.size() << std::endl;
            t0 = now;
        }
        return true;
    });
#else
    // TEST CODE: показ + запись до/после в combined.avi
    cv::VideoWriter writer;
    double fps = source.fps();
    Pipeline pipeline(source, manager, [&](const ProcessedItem& item) {
        cv::Mat combined;
        cv::hconcat(item.frame, item.result, combined);
        if (!writer.isOpened())
            writer.open("combined.avi",
                        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                        fps, combined.size());
        writer.write(combined);
        cv::imshow("Result", combined);
        return cv::waitKey(1) != 27;     // ESC — остановка
    });
#endif

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
    // применение конфига из веб-интерфейса (проверка боем: ошибка -> текст,
    // старый анализатор остаётся живым)
    gApply = [&manager](const nlohmann::json& j) -> std::string {
        try
        {
            manager.reconfigure(j);
#ifdef RASPBERRY_CM5
            gSystemAdmin = std::make_unique<SystemAdmin>(j);
#endif
            return "";
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    };

#ifdef RASPBERRY_CM5
    gSystemAdmin = std::make_unique<SystemAdmin>(reader.getRawConfig());
    transports = std::make_unique<TransportManager>(
        reader.getRawConfig(), [](uint8_t detectorCode) -> bool {
            const char* mode = nullptr;
            switch (detectorCode)
            {
                case 0x00: mode = "off"; break;
                case 0x01: mode = "object_detection"; break;
                case 0x02: mode = "aruco_detection"; break;
                case 0x03: mode = "blob_detection"; break;
                case 0x04: mode = "line_detection"; break;
                case 0x05: mode = "circle_detection"; break;
                default: return false;
            }
            try
            {
                std::ifstream input(gConfigPath);
                nlohmann::json config;
                input >> config;
                config["general_params"]["processing_mode"] = mode;
                const std::string error = gApply ? gApply(config) : "not ready";
                return error.empty() && saveConfigAtomic(config).empty();
            }
            catch (...)
            {
                return false;
            }
        });
#endif

    // Вахтёр: правка config.json руками/по сети применяется без рестарта
    std::atomic<bool> ctlRun{true};
    std::thread watcher([&] {
        time_t last = cfgMtime(gConfigPath.c_str());
        while (ctlRun)
        {
            sleep(1);
            time_t m = cfgMtime(gConfigPath.c_str());
            if (m == 0 || m == last)
                continue;
            last = m;
            ConfigReader r2;
            if (!r2.loadFromFile(gConfigPath))
                continue;
            try
            {
                manager.reconfigure(r2.getRawConfig());
                std::cout << "config reloaded" << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cerr << "reconfig failed (старый анализатор оставлен): "
                          << e.what() << std::endl;
            }
        }
    });
    std::thread control(controlServer, 8081);
    control.detach();
#endif

    // 6. Запуск (блокируется до остановки)
    pipeline.start();

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
    ctlRun = false;
    watcher.join();
#endif

    source.release();
#if !defined(MTV3_BOARD) && !defined(RASPBERRY_CM5) && !defined(HOST_WEB_UI)
    cv::destroyAllWindows();
#endif
    return 0;
}
