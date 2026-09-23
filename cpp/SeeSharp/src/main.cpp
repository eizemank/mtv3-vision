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
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
#include "system/system_admin.hpp"
#endif
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
#include "transport/transport_manager.hpp"
#endif

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <utility>
#endif

#ifdef MTV3_BOARD
#include "pipeline/shm_source.hpp"
#elif defined(HOST_WEB_UI)
#ifdef HOST_CAMERA_SOURCE
#include "pipeline/camera_source.hpp"
#else
#include "pipeline/synthetic_source.hpp"
#endif
#else
#include "pipeline/camera_source.hpp"
#endif

// путь к конфигу (--config); по умолчанию рядом с бинарником (cwd)
static std::string gConfigPath = "config.json";

static nlohmann::json runtimeConfig(nlohmann::json config)
{
    auto objectDetection = config.find("object_detection");
    if (objectDetection == config.end() || !objectDetection->is_object())
        return config;

    std::error_code error;
    std::filesystem::path configPath = std::filesystem::absolute(gConfigPath, error);
    if (error)
        configPath = gConfigPath;
    const std::filesystem::path configDirectory = configPath.parent_path();
    for (const char* key : {"model_onnx", "model_rknn", "class_names_file"})
    {
        auto value = objectDetection->find(key);
        if (value == objectDetection->end() || !value->is_string())
            continue;
        std::filesystem::path path = value->get<std::string>();
        if (!path.empty() && path.is_relative())
            *value = (configDirectory / path).lexically_normal().string();
    }
    return config;
}

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
static time_t cfgMtime(const char* path)
{
    struct stat st{};
    return stat(path, &st) ? 0 : st.st_mtime;
}

// применение конфига к живому конвейеру (ставится в main); "" = успех
static std::function<std::string(const nlohmann::json&)> gApply;
static std::mutex gPreviewMutex;
static std::condition_variable gPreviewInputReady;
static std::condition_variable gPreviewJpegReady;
static cv::Mat gPreviewSourceFrame;
static cv::Mat gPreviewResultFrame;
static std::string gPreviewSourceJpeg;
static std::string gPreviewResultJpeg;
static uint64_t gPreviewInputSequence = 0;
static uint64_t gPreviewJpegSequence = 0;
static bool gPreviewRunning = false;
static bool gPreviewEnabled = true;
static int gPreviewJpegQuality = 75;
static int gPreviewMaxFps = 15;
static int gPreviewMaxWidth = 960;
static std::chrono::steady_clock::time_point gPreviewNextFrame{};
static std::thread gPreviewThread;
static std::atomic<int> gPreviewClients{0};
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
static std::unique_ptr<SystemAdmin> gSystemAdmin;
#endif

static void configurePreview(const nlohmann::json& config)
{
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    const auto preview = config.value("web_preview", nlohmann::json::object());
    gPreviewEnabled = preview.value("enabled", true);
    gPreviewJpegQuality = std::max(30, std::min(preview.value("jpeg_quality", 75), 95));
    gPreviewMaxFps = std::max(1, std::min(preview.value("max_fps", 15), 30));
    gPreviewMaxWidth = std::max(160, std::min(preview.value("max_width", 960), 1920));
    gPreviewNextFrame = {};
}

static void updatePreview(const cv::Mat& source, const cv::Mat& result)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    if (!gPreviewEnabled || gPreviewClients.load() == 0 || now < gPreviewNextFrame)
        return;
    gPreviewNextFrame = now + std::chrono::milliseconds(1000 / gPreviewMaxFps);
    gPreviewSourceFrame = source.clone();
    gPreviewResultFrame = result.clone();
    ++gPreviewInputSequence;
    gPreviewInputReady.notify_one();
}

static cv::Mat resizePreview(const cv::Mat& frame, int maxWidth)
{
    if (frame.cols <= maxWidth)
        return frame;
    cv::Mat resized;
    const double scale = static_cast<double>(maxWidth) / frame.cols;
    cv::resize(frame, resized, {maxWidth, std::max(1, cvRound(frame.rows * scale))},
               0.0, 0.0, cv::INTER_AREA);
    return resized;
}

static std::string encodePreview(const cv::Mat& frame, int maxWidth, int quality)
{
    if (frame.empty())
        return {};
    const cv::Mat resized = resizePreview(frame, maxWidth);
    std::vector<uchar> encoded;
    const std::vector<int> encodeParams{cv::IMWRITE_JPEG_QUALITY, quality};
    if (!cv::imencode(".jpg", resized, encoded, encodeParams))
        return {};
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

static void previewEncoderLoop()
{
    uint64_t consumedSequence = 0;
    for (;;)
    {
        cv::Mat source;
        cv::Mat result;
        int quality;
        int maxWidth;
        uint64_t sequence;
        {
            std::unique_lock<std::mutex> lock(gPreviewMutex);
            gPreviewInputReady.wait(lock, [&] {
                return !gPreviewRunning || gPreviewInputSequence != consumedSequence;
            });
            if (!gPreviewRunning)
                return;
            source = gPreviewSourceFrame;
            result = gPreviewResultFrame;
            quality = gPreviewJpegQuality;
            maxWidth = gPreviewMaxWidth;
            sequence = gPreviewInputSequence;
        }
        std::string sourceJpeg = encodePreview(source, maxWidth, quality);
        std::string resultJpeg = encodePreview(result, maxWidth, quality);
        {
            std::lock_guard<std::mutex> lock(gPreviewMutex);
            if (!sourceJpeg.empty())
                gPreviewSourceJpeg = std::move(sourceJpeg);
            if (!resultJpeg.empty())
                gPreviewResultJpeg = std::move(resultJpeg);
            consumedSequence = sequence;
            gPreviewJpegSequence = sequence;
        }
        gPreviewJpegReady.notify_all();
    }
}

static void startPreviewEncoder()
{
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    if (gPreviewRunning)
        return;
    gPreviewRunning = true;
    gPreviewThread = std::thread(previewEncoderLoop);
}

static void stopPreviewEncoder()
{
    {
        std::lock_guard<std::mutex> lock(gPreviewMutex);
        gPreviewRunning = false;
    }
    gPreviewInputReady.notify_all();
    gPreviewJpegReady.notify_all();
    if (gPreviewThread.joinable())
        gPreviewThread.join();
}

static bool getPreviewJpeg(bool source, std::string& jpeg, uint64_t* sequence = nullptr)
{
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    const std::string& current = source ? gPreviewSourceJpeg : gPreviewResultJpeg;
    if (current.empty())
        return false;
    jpeg = current;
    if (sequence)
        *sequence = gPreviewJpegSequence;
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
button.selecting{background:#075b75;border-color:#28b4ff}
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
.linkedEditor{border:1px solid #475847;background:#141a14;padding:10px;margin:8px 0}
.linkedEditor h4{margin:4px 0 8px;color:#bdb}.linkedGrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:6px}
.linkedCard,.linkedRow{border:1px solid #384838;background:#1a211a;padding:7px}.linkedCard select,.linkedRow select{background:#222;color:#eee;border:1px solid #555;padding:4px}
.linkedRow{display:flex;align-items:center;gap:6px;margin-top:5px}.linkedRow span{color:#9bd}.danger{color:#fbb;border-color:#744}
.swatch{display:inline-block;width:16px;height:16px;border:1px solid #888;vertical-align:middle;margin-right:5px}
</style></head><body>
<h3 id="appTitle">SeeSharp vision</h3><label><span id="languageLabel">Language</span> <select id="language" onchange="setLanguage(this.value)"><option value="ru">Русский</option><option value="en">English</option></select></label>
<div class="streams"><div><h4 id="sourceTitle">Source</h4><div class="selectFrame"><img id="src" alt="source is not ready"><canvas id="selection"></canvas></div></div>
<div><h4 id="resultTitle">Detection result</h4><img id="v" alt="result is not ready"></div></div><br>
<div class="blobSampler" id="blobSampler"><span id="blobPatternLabel">Blob pattern index</span><input id="blobPattern" type="number" min="0" value="0">
<button id="configureBlobButton" onclick="configureBlobFromSelection()">Configure blob from selected contour</button>
<span class="hint" id="blobSelectionHint">Hold the pointer and trace the object contour</span></div>
<div id="modes"></div>
<h4><span id="parametersTitle">Параметры</span> <span class="hint" id="parametersHint">(секция активного режима + общие)</span></h4>
<div><label class="hint"><input type="checkbox" id="all" onchange="render()"> <span id="showAllLabel">показать все секции</span></label>
&nbsp;<label class="hint"><input type="checkbox" id="raw" onchange="render()"> <span id="rawLabel">редактировать JSON</span></label></div>
<div id="paramTabs"><button id="detectorTab" class="act" onclick="setParamTab('detector')">Detector parameters</button>
<button id="generalTab" onclick="setParamTab('general')">General parameters</button>
<button id="uartTab" onclick="setParamTab('uart')">UART metadata</button></div>
<div id="form"></div>
<textarea id="cfg" spellcheck="false" style="display:none"></textarea><br>
<button id="applyButton" onclick="send('/apply')">Применить (до перезапуска)</button>
<button id="saveButton" onclick="send('/config')">Сохранить (постоянно)</button>
<button id="revertButton" onclick="revert()">Откатить к сохранённому</button><span id="st"></span>
<details id="uartLogPanel"><summary>UART RX log / Журнал приёма UART</summary>
<p>CM5 RX ← controller TX. BYTES: raw data; PACKET: DXL command. Last 100 entries.</p>
<button id="uartLogPause">Pause / Пауза</button>
<label><input id="uartLogRaw" type="checkbox">Show raw bytes / Показать байты</label>
<div id="uartLogStatus" role="status">Откройте журнал для загрузки / Open log to load</div>
<pre id="uartLogOutput" style="max-height:360px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere"></pre>
</details>
<details id="uartTxLogPanel"><summary>UART TX log / Журнал передачи UART</summary>
<p>UART TX → controller RX. BYTES: written data; PACKET: DXL response or binary frame. Last 100 entries.</p>
<button id="uartTxLogPause">Pause / Пауза</button>
<label><input id="uartTxLogRaw" type="checkbox">Show raw bytes / Показать байты</label>
<div id="uartTxLogStatus" role="status">Откройте журнал для загрузки / Open log to load</div>
<pre id="uartTxLogOutput" style="max-height:360px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere"></pre>
</details>
<details><summary id="adminTitle">System administration</summary><div class="body">
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
<div class="row"><label>Network request JSON</label><input id="admNet" type="text" value='{"op":"network","connection":"wlan0","mode":"dhcp"}'></div>
<button onclick="adminNetwork()">Apply network configuration</button>
<div class="row"><label>Terminal command</label><input id="admCmd" type="text"></div>
<button onclick="adminPost({op:'terminal',command:document.getElementById('admCmd').value})">Execute</button>
<textarea id="admOut" spellcheck="false"></textarea>
</div></details>
<script>
// Keep UART diagnostics independent of video, canvas and configuration setup.
function setupUartLog(prefix,direction){
  const panel=document.getElementById(prefix+'Panel');
  const status=document.getElementById(prefix+'Status');
  const out=document.getElementById(prefix+'Output');
  const pause=document.getElementById(prefix+'Pause');
  const raw=document.getElementById(prefix+'Raw');
  let paused=false,busy=false,timer=null,lastLog=null;
  function renderLog(log){
    status.textContent=`${log.state} ${log.device} | ${direction} bytes=${log.bytes}, packets=${log.packets}`;
    const atBottom=out.scrollHeight-out.scrollTop-out.clientHeight<30;
    out.textContent=log.entries.filter(e=>raw.checked||e.kind==='PACKET').map(e=>
      `${new Date(e.time_ms).toLocaleTimeString()}.${String(e.time_ms%1000).padStart(3,'0')} #${e.sequence} ${e.kind} ${e.detail}\n${e.hex}`
    ).join('\n')||(direction==='TX'?'No transmitted packets / Нет переданных пакетов':'No received packets / Нет принятых пакетов');
    if(atBottom)out.scrollTop=out.scrollHeight;
  }
  async function poll(){
    if(busy)return;
    clearTimeout(timer);
    if(!panel.open||paused)return;
    busy=true;
    if(!lastLog)status.textContent='Loading UART '+direction+' / Загрузка…';
    const controller=new AbortController();
    const timeout=setTimeout(()=>controller.abort(),5000);
    try{
      const response=await fetch('/uart/'+direction.toLowerCase()+'-log',{cache:'no-store',signal:controller.signal});
      if(!response.ok)throw new Error('HTTP '+response.status);
      const log=await response.json();
      if(!Array.isArray(log.entries))throw new Error('Invalid UART log response');
      lastLog=log;
      if(!paused)renderLog(log);
    }catch(error){
      status.textContent='UART '+direction+': '+(error.name==='AbortError'?'Request timed out / Сервер не ответил за 5 секунд':error.message);
    }finally{
      clearTimeout(timeout);
      busy=false;
      if(panel.open&&!paused)timer=setTimeout(poll,1000);
    }
  }
  panel.addEventListener('toggle',poll);
  pause.addEventListener('click',()=>{
    paused=!paused;
    pause.textContent=paused?'Resume / Продолжить':'Pause / Пауза';
    if(paused)clearTimeout(timer);else poll();
  });
  raw.addEventListener('change',()=>{if(lastLog)renderLog(lastLog);});
  poll();
}
setupUartLog('uartLog','RX');
setupUartLog('uartTxLog','TX');
</script>
<script>
const modes=['off','aruco_detection','object_detection','blob_detection','line_detection','circle_detection'];
let language='ru';
const uiText={
  ru:{language:'Язык',source:'Исходное видео',result:'Результат детекции',blobPattern:'Номер цветового шаблона',configureBlob:'Настроить blob по выделенному контуру',selectHint:'Удерживая кнопку, обведите контур объекта',parameters:'Параметры',parametersHint:'(активный алгоритм и общие настройки)',showAll:'показать все разделы',raw:'редактировать JSON',detectorTab:'Параметры детектора',generalTab:'Общие параметры',uartTab:'Метаданные UART',apply:'Применить',save:'Сохранить',revert:'Откатить',linkedEditor:'Редактор связанного объекта',components:'Количество blobs в связанном объекте',selectVideo:'Выбрать на видео',remove:'Удалить',drawLink:'Провести связь на видео',manualLink:'Добавить связь вручную',base:'базовый',baseLink:'Базовая связь',admin:'Системное администрирование'},
  en:{language:'Language',source:'Source video',result:'Detection result',blobPattern:'Blob pattern index',configureBlob:'Configure blob from selected contour',selectHint:'Hold the pointer and trace the object contour',parameters:'Parameters',parametersHint:'(active detector and general settings)',showAll:'show all sections',raw:'edit JSON',detectorTab:'Detector parameters',generalTab:'General parameters',uartTab:'UART metadata',apply:'Apply',save:'Save',revert:'Revert',linkedEditor:'Linked blob object editor',components:'Number of blobs in linked object',selectVideo:'Select on video',remove:'Remove',drawLink:'Draw link on video',manualLink:'Add link manually',base:'base',baseLink:'Base link',admin:'System administration'}
};
const modeNames={ru:{off:'Выключено',aruco_detection:'ArUco-маркеры',object_detection:'Объекты YOLO',blob_detection:'Цветовые blobs',line_detection:'Линии',circle_detection:'Окружности'},en:{off:'Off',aruco_detection:'ArUco detection',object_detection:'Object detection',blob_detection:'Blob detection',line_detection:'Line detection',circle_detection:'Circle detection'}};
function tr(key){return (uiText[language]||uiText.en)[key]||key;}
function applyLocalization(){
  const values={languageLabel:tr('language'),sourceTitle:tr('source'),resultTitle:tr('result'),blobPatternLabel:tr('blobPattern'),configureBlobButton:tr('configureBlob'),blobSelectionHint:tr('selectHint'),parametersTitle:tr('parameters'),parametersHint:tr('parametersHint'),showAllLabel:tr('showAll'),rawLabel:tr('raw'),detectorTab:tr('detectorTab'),generalTab:tr('generalTab'),uartTab:tr('uartTab'),applyButton:tr('apply'),saveButton:tr('save'),revertButton:tr('revert'),adminTitle:tr('admin')};
  Object.entries(values).forEach(([id,value])=>{const element=document.getElementById(id);if(element)element.textContent=value;});
  document.getElementById('language').value=language;
  [...document.getElementById('modes').children].forEach(button=>button.textContent=(modeNames[language]||modeNames.en)[button.dataset.m]||button.dataset.m);
}
function setLanguage(value){
  if(Object.keys(cfgObj).length)cfgObj=collect();language=value==='en'?'en':'ru';
  cfgObj.general_params=cfgObj.general_params||{};cfgObj.general_params.ui_language=language;applyLocalization();render();
}
function startVideoStream(id,path){
  const image=document.getElementById(id);
  image.onload=()=>{if(id==='src')resizeSelectionOverlay();};
  image.onerror=()=>setTimeout(()=>{image.src=path+'?t='+Date.now();},1000);
  image.src=path+'?t='+Date.now();
}
startVideoStream('src','/source.mjpg');
startVideoStream('v','/preview.mjpg');
const selectionCanvas=document.getElementById('selection'),selectionContext=selectionCanvas.getContext('2d');
let selectionStart=null,selectionPolygon=null,linkedAction=null,linkPreview=null;
const linkedPoints={};
function resizeSelectionOverlay(){
  const image=document.getElementById('src'),rect=image.getBoundingClientRect();
  selectionCanvas.width=Math.max(1,Math.round(rect.width));selectionCanvas.height=Math.max(1,Math.round(rect.height));drawSelection();
}
function selectionPoint(event){const rect=selectionCanvas.getBoundingClientRect();return{x:(event.clientX-rect.left)*selectionCanvas.width/rect.width,y:(event.clientY-rect.top)*selectionCanvas.height/rect.height};}
function drawSelection(){selectionContext.clearRect(0,0,selectionCanvas.width,selectionCanvas.height);
  Object.entries(linkedPoints).forEach(([patternIndex,points])=>{const pattern=(((cfgObj||{}).blob_detection||{}).multicolor_patterns||[])[Number(patternIndex)];if(!pattern)return;
    (pattern.links||[]).forEach(link=>{const ids=link.id.split('-').map(Number),a=points[ids[0]],b=points[ids[1]];if(!a||!b)return;
      selectionContext.strokeStyle=link.id==='0-1'?'#ffa500':'#00ffff';selectionContext.lineWidth=3;selectionContext.beginPath();selectionContext.moveTo(a.x*selectionCanvas.width,a.y*selectionCanvas.height);selectionContext.lineTo(b.x*selectionCanvas.width,b.y*selectionCanvas.height);selectionContext.stroke();});
    Object.entries(points).forEach(([nodeId,point])=>{const x=point.x*selectionCanvas.width,y=point.y*selectionCanvas.height;selectionContext.fillStyle='#fff';selectionContext.beginPath();selectionContext.arc(x,y,7,0,Math.PI*2);selectionContext.fill();selectionContext.fillStyle='#111';selectionContext.font='11px sans-serif';selectionContext.fillText('N'+nodeId,x-6,y+4);});});
  if(linkPreview){selectionContext.strokeStyle='#28b4ff';selectionContext.lineWidth=3;selectionContext.setLineDash([7,5]);selectionContext.beginPath();selectionContext.moveTo(linkPreview.a.x,linkPreview.a.y);selectionContext.lineTo(linkPreview.b.x,linkPreview.b.y);selectionContext.stroke();selectionContext.setLineDash([]);}
  if(selectionPolygon&&selectionPolygon.length){selectionContext.fillStyle='rgba(40,180,255,.18)';selectionContext.strokeStyle='#28b4ff';selectionContext.lineWidth=2;
    selectionContext.beginPath();selectionContext.moveTo(selectionPolygon[0].x,selectionPolygon[0].y);selectionPolygon.slice(1).forEach(point=>selectionContext.lineTo(point.x,point.y));
    if(!selectionStart&&selectionPolygon.length>=3)selectionContext.closePath();selectionContext.fill();selectionContext.stroke();}}
selectionCanvas.onpointerdown=event=>{selectionStart=selectionPoint(event);selectionCanvas.setPointerCapture(event.pointerId);if(linkedAction&&linkedAction.type==='link')linkPreview={a:selectionStart,b:selectionStart};else selectionPolygon=[selectionStart];};
selectionCanvas.onpointermove=event=>{if(!selectionStart)return;const point=selectionPoint(event);
  if(linkedAction&&linkedAction.type==='link')linkPreview={a:selectionStart,b:point};
  else if(!selectionPolygon.length||Math.hypot(point.x-selectionPolygon[selectionPolygon.length-1].x,point.y-selectionPolygon[selectionPolygon.length-1].y)>=3)selectionPolygon.push(point);drawSelection();};
selectionCanvas.onpointerup=event=>{const end=selectionPoint(event),start=selectionStart;selectionStart=null;
  if(linkedAction&&linkedAction.type==='link'){finishLinkedLine(start,end);linkPreview=null;drawSelection();return;}
  if(selectionPolygon&&selectionPolygon.length<3)selectionPolygon=null;drawSelection();
  if(linkedAction&&linkedAction.type==='part')assignLinkedPartFromSelection(linkedAction.patternIndex,linkedAction.nodeIndex);};
function percentile(values,fraction){values.sort((a,b)=>a-b);return values[Math.min(values.length-1,Math.floor(values.length*fraction))];}
function pointInPolygon(x,y,polygon){let inside=false;for(let i=0,j=polygon.length-1;i<polygon.length;j=i++){
  const a=polygon[i],b=polygon[j];if(((a.y>y)!==(b.y>y))&&(x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x))inside=!inside;}return inside;}
function polygonArea(polygon){let area=0;for(let i=0,j=polygon.length-1;i<polygon.length;j=i++)area+=polygon[j].x*polygon[i].y-polygon[i].x*polygon[j].y;return Math.abs(area)*.5;}
function polygonCenter(polygon){return{x:polygon.reduce((sum,p)=>sum+p.x,0)/polygon.length,y:polygon.reduce((sum,p)=>sum+p.y,0)/polygon.length};}
function configureBlobFromSelection(){
  const currentConfig=current();if(!currentConfig)return;cfgObj=currentConfig;
  const image=document.getElementById('src'),patterns=((cfgObj.blob_detection||{}).one_color_patterns||[]);
  const index=Number(document.getElementById('blobPattern').value);
  if(!selectionPolygon||selectionPolygon.length<3){st.textContent='Trace a closed contour around the blob first.';return;}
  if(!image.naturalWidth||!image.naturalHeight){st.textContent='Source frame is not ready.';return;}
  if(!Number.isInteger(index)||index<0||index>=patterns.length){st.textContent='Blob pattern index is out of range.';return;}
  const scaleX=image.naturalWidth/selectionCanvas.width,scaleY=image.naturalHeight/selectionCanvas.height;
  const polygon=selectionPolygon.map(point=>({x:point.x*scaleX,y:point.y*scaleY}));
  const xs=polygon.map(point=>point.x),ys=polygon.map(point=>point.y);
  const roi={x:Math.max(0,Math.floor(Math.min(...xs))),y:Math.max(0,Math.floor(Math.min(...ys))),
    w:Math.max(1,Math.ceil(Math.max(...xs))-Math.floor(Math.min(...xs))),h:Math.max(1,Math.ceil(Math.max(...ys))-Math.floor(Math.min(...ys)))};
  roi.w=Math.min(roi.w,image.naturalWidth-roi.x);roi.h=Math.min(roi.h,image.naturalHeight-roi.y);
  const sample=document.createElement('canvas');sample.width=image.naturalWidth;sample.height=image.naturalHeight;
  const context=sample.getContext('2d',{willReadFrequently:true});context.drawImage(image,0,0);
  const pixels=context.getImageData(roi.x,roi.y,roi.w,roi.h).data,samples=[];
  const stride=Math.max(1,Math.floor((roi.w*roi.h)/12000));
  for(let pixel=0;pixel<roi.w*roi.h;pixel+=stride){const x=pixel%roi.w,row=Math.floor(pixel/roi.w);if(!pointInPolygon(roi.x+x+.5,roi.y+row+.5,polygon))continue;
    const offset=pixel*4,r=pixels[offset],g=pixels[offset+1],b=pixels[offset+2],y=.299*r+.587*g+.114*b;samples.push([y,(r-y)*.713+128,(b-y)*.564+128]);}
  if(samples.length<10){st.textContent='Selected contour is too small.';return;}
  const center=[0,1,2].map(channel=>percentile(samples.map(sample=>sample[channel]),.5));
  let foreground=samples.filter(sample=>Math.hypot(sample[1]-center[1],sample[2]-center[2])<=24&&Math.abs(sample[0]-center[0])<=55);
  if(foreground.length<samples.length*.1)foreground=samples.sort((a,b)=>Math.hypot(a[0]-center[0],a[1]-center[1],a[2]-center[2])-Math.hypot(b[0]-center[0],b[1]-center[1],b[2]-center[2])).slice(0,Math.max(1,Math.floor(samples.length*.5)));
  const channels=[0,1,2].map(channel=>foreground.map(sample=>sample[channel]));
  const lower=channels.map(values=>clampByte(percentile(values,.05)-6));
  const upper=channels.map(values=>clampByte(percentile(values,.95)+6));
  const pattern=patterns[index],area=polygonArea(polygon)*foreground.length/samples.length;
  pattern.lower_range=lower;pattern.upper_range=upper;pattern.min_area=Math.max(1,Math.round(area*.45));pattern.max_area=Math.round(area*1.9);
  pattern.min_width=Math.max(1,Math.round(roi.w*.5));pattern.min_height=Math.max(1,Math.round(roi.h*.5));
  pattern.min_luminance=lower[0];pattern.max_luminance=upper[0];
  pattern.min_chrominance_red=lower[1];pattern.max_chrominance_red=upper[1];
  pattern.min_chrominance_blue=lower[2];pattern.max_chrominance_blue=upper[2];
  st.textContent='Blob pattern '+index+' configured from a '+Math.round(polygonArea(polygon))+' px² contour. Review and Apply settings.';
  render();
}
const md=document.getElementById('modes');
let cur='', cfgObj={}, paramTab='detector';
modes.forEach(m=>{const b=document.createElement('button');b.textContent=(modeNames[language]||modeNames.en)[m]||m;b.dataset.m=m;
b.onclick=()=>{st.textContent=language==='ru'?'Запуск режима…':'Starting mode…';fetch('/mode/'+m).then(r=>r.text()).then(t=>{st.textContent=t;if(t.startsWith('mode='))load();}).catch(error=>{st.textContent=(language==='ru'?'Ошибка переключения режима: ':'Mode switch failed: ')+error.message;});};
md.appendChild(b);});
function mark(){[...md.children].forEach(b=>b.className=b.dataset.m===cur?'act':'');document.getElementById('blobSampler').style.display=cur==='blob_detection'?'flex':'none';}
function setParamTab(tab){
  paramTab=tab;
  document.getElementById('detectorTab').className=tab==='detector'?'act':'';
  document.getElementById('generalTab').className=tab==='general'?'act':'';
  document.getElementById('uartTab').className=tab==='uart'?'act':'';
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
device:'UART device',rs485:'Use RS-485 direction control',startup_push:'Send metadata automatically',eeprom_file:'DXL EEPROM state file',
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
baud:'UART line speed; both ends must use the same value.',uart_binary:'Lightweight binary UART transport used to send detection metadata.',
token:'Token required by privileged web-administration endpoints.',file_root:'Filesystem root exposed by the web file manager.',terminal_enabled:'Allows execution of terminal commands through the administration API.',
data_yaml:'YOLO dataset description containing train/validation paths and class names.',base_model:'Pretrained model used as the starting point for training.',epochs:'Number of complete passes over the training dataset.',image_size:'Square image size used during training.',output_onnx:'Output path for the exported trained ONNX model.',
id:'Numeric identifier of this pattern, object, node, transport, or marker.',threshold:'Minimum normalized score required for this criterion.',overall_threshold:'Minimum combined score required to accept a composite object.',weight:'Relative contribution of this criterion; zero disables its contribution.',goal:'Ideal criterion value that receives the highest score.',
size:'Size-matching criteria for a composite-object node.',size_measure:'Blob property used for relative size comparisons, such as area, width, or maximum axis.',circularity:'Circularity-matching criteria for a composite-object node.',inertia:'Inertia-ratio matching criteria for a composite-object node.',convexity:'Convexity-matching criteria for a composite-object node.',angle:'Orientation-matching criteria in degrees.',
min:'Lowest value accepted by this criterion.',max:'Highest value accepted by this criterion.',nodes:'Primitive parts required to form this composite object.',links:'Spatial relationships required between composite-object parts.',length_absolute:'Allowed absolute distance between linked parts, in pixels.',length_relative:'Allowed distance relative to the base part size.',angle_absolute:'Allowed absolute direction between linked parts, in degrees.',angle_relative:'Allowed direction relative to the base link, in degrees.'
};
const ruLabels={
enabled:'Включено',device:'Устройство',baud:'Скорость UART, бит/с',format:'Формат',host:'Адрес получателя',port:'Порт',max_objects:'Максимум объектов',blob_detection:'Детекция blobs',aruco_detection:'Детекция ArUco',object_detection:'Детекция объектов',line_detection:'Детекция линий',circle_detection:'Детекция окружностей',general_params:'Общие параметры',transports:'Транспортные протоколы',uart_binary:'Бинарный UART',processing_mode:'Режим обработки',ui_language:'Язык интерфейса',debug_mode:'Отладочный режим',camera_rotation:'Поворот камеры, градусы',exposure_ev:'Экспозиция, EV',white_balance_bgr:'Баланс белого [B, G, R]',contrast:'Контрастность',brightness:'Яркость',enable_one_color_detection:'Детекция отдельных цветовых областей',enable_multicolor_detection:'Детекция связанных объектов',max_composite_objects:'Максимум связанных объектов',one_color_patterns:'Цветовые шаблоны',multicolor_patterns:'Шаблоны связанных объектов',min_area:'Минимальная площадь, пикс²',max_area:'Максимальная площадь, пикс²',min_width:'Минимальная ширина, пикс',min_height:'Минимальная высота, пикс',lower_range:'Нижняя граница YCrCb [Y, Cr, Cb]',upper_range:'Верхняя граница YCrCb [Y, Cr, Cb]',min_circularity:'Минимальная округлость',max_circularity:'Максимальная округлость',min_inertia:'Минимальная инерция',max_inertia:'Максимальная инерция',min_convexity:'Минимальная выпуклость',max_convexity:'Максимальная выпуклость',min_vertices:'Минимум вершин',max_vertices:'Максимум вершин',polygon_approximation:'Аппроксимация контура',dictionary:'Словарь ArUco',marker_length:'Размер маркера, м',allowed_ids:'Разрешённые ID',model_onnx:'Файл модели ONNX',model_rknn:'Файл модели RKNN',class_names_file:'Файл названий классов',input_width:'Ширина входа, пикс',input_height:'Высота входа, пикс',confidence_threshold:'Порог уверенности',nms_threshold:'Порог NMS IoU',output_layout:'Расположение данных выхода',output_attributes:'Количество атрибутов выхода',output_has_objectness:'Выход содержит objectness',min_radius:'Минимальный радиус, пикс',max_radius:'Максимальный радиус, пикс',distance:'Минимальное расстояние центров, пикс',min_angle:'Минимальный угол, градусы',max_angle:'Максимальный угол, градусы',max_lines:'Максимум линий',threshold:'Порог',overall_threshold:'Общий порог',weight:'Вес',goal:'Целевое значение',size:'Размер',size_measure:'Способ измерения размера',nodes:'Компоненты',links:'Связи',length_absolute:'Абсолютная длина',length_relative:'Относительная длина',angle_absolute:'Абсолютный угол',angle_relative:'Относительный угол'};
Object.assign(labels,{web_preview:'Web preview',max_width:'Maximum preview width, px'});
Object.assign(hints,{web_preview:'Controls the shared MJPEG cache used by all web clients.',max_width:'Frames wider than this value are downscaled before JPEG encoding.'});
Object.assign(ruLabels,{web_preview:'Веб-просмотр',jpeg_quality:'Качество JPEG, 30–95',max_fps:'Максимальная частота, FPS',max_width:'Максимальная ширина, пикс'});
function fieldLabel(key){return language==='ru'?(ruLabels[key]||labels[key]||key.replaceAll('_',' ')):(labels[key]||key.replaceAll('_',' ').replace(/^./,c=>c.toUpperCase()));}
function fieldHint(key){return language==='ru'?('Настройка «'+fieldLabel(key)+'».'):(hints[key]||'Configuration parameter: '+fieldLabel(key)+'.');}
function contextualFieldLabel(key,path){
  if(path[0]==='transports'&&path[1]==='uart_binary')return({enabled:'Enable UART metadata',max_objects:'Maximum objects per packet'}[key]||fieldLabel(key));
  return fieldLabel(key);
}
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
  const r=el('div','row'), l=el('label'); l.textContent=contextualFieldLabel(key,path); l.title=fieldHint(key)+' JSON key: '+key; r.appendChild(l);
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
function linkedDefaults(){return{
  node:id=>({id,blob_id:[id],threshold:.06,weight:255,size:{min:0,max:5000,goal:350,weight:255},circularity:{min:0,max:1,goal:.5,weight:0},inertia:{min:0,max:1,goal:.5,weight:0},convexity:{min:0,max:1,goal:.5,weight:0},angle:{min:-180,max:180,goal:0,weight:0}}),
  link:(a,b)=>({id:a+'-'+b,threshold:0,weight:255,length_absolute:{min:0,max:100000,goal:350,weight:0},length_relative:{min:0,max:10000,goal:.5,weight:0},angle_absolute:{min:-180,max:180,goal:0,weight:0},angle_relative:{min:-180,max:180,goal:0,weight:0}})
};}
function syncLinkedEdit(change){cfgObj=collect();change(cfgObj.blob_detection);render();}
function setLinkedNodeCount(patternIndex,value){syncLinkedEdit(blob=>{
  const pattern=blob.multicolor_patterns[patternIndex],count=Math.max(2,Math.min(5,Number(value)||2)),colors=blob.one_color_patterns||[];
  while(pattern.nodes.length<count){const id=pattern.nodes.length,node=linkedDefaults().node(id);if(colors.length)node.blob_id=[colors[Math.min(id,colors.length-1)].id];pattern.nodes.push(node);}
  if(pattern.nodes.length>count)pattern.nodes.length=count;
  pattern.nodes.forEach((node,index)=>node.id=index);pattern.links=pattern.links.filter(link=>link.id.split('-').every(id=>Number(id)<count));
  if(count>=2&&!pattern.links.some(link=>link.id==='0-1'))pattern.links.unshift(linkedDefaults().link(0,1));
  const points=linkedPoints[patternIndex]||{};Object.keys(points).forEach(id=>{if(Number(id)>=count)delete points[id];});
});}
function startLinkedPartSelection(patternIndex,nodeIndex){
  cfgObj=collect();linkedAction={type:'part',patternIndex,nodeIndex};selectionPolygon=null;linkPreview=null;st.textContent='Trace the object contour for component N'+nodeIndex+'.';render();
}
function assignLinkedPartFromSelection(patternIndex,nodeIndex){
  if(!selectionPolygon||selectionPolygon.length<3){st.textContent='Trace the component contour first.';return;}
  cfgObj=collect();const node=cfgObj.blob_detection.multicolor_patterns[patternIndex].nodes[nodeIndex],colorId=(node.blob_id||[])[0];
  document.getElementById('blobPattern').value=colorId;linkedPoints[patternIndex]=linkedPoints[patternIndex]||{};
  const center=polygonCenter(selectionPolygon);linkedPoints[patternIndex][nodeIndex]={x:center.x/selectionCanvas.width,y:center.y/selectionCanvas.height};
  linkedAction=null;configureBlobFromSelection();selectionPolygon=null;st.textContent='Component N'+nodeIndex+' assigned to blob pattern '+colorId+'.';drawSelection();
}
function startLinkedLine(patternIndex){
  cfgObj=collect();const points=linkedPoints[patternIndex]||{};if(Object.keys(points).length<2){st.textContent='Assign at least two components on the video first.';return;}
  linkedAction={type:'link',patternIndex};selectionPolygon=null;st.textContent='Draw a line from one assigned component to another.';render();
}
function nearestLinkedNode(patternIndex,point){let result=null,best=45;const points=linkedPoints[patternIndex]||{};
  Object.entries(points).forEach(([id,p])=>{const distance=Math.hypot(point.x-p.x*selectionCanvas.width,point.y-p.y*selectionCanvas.height);if(distance<best){best=distance;result=Number(id);}});return result;}
function finishLinkedLine(start,end){
  const patternIndex=linkedAction.patternIndex,first=nearestLinkedNode(patternIndex,start),second=nearestLinkedNode(patternIndex,end);linkedAction=null;
  if(first===null||second===null){st.textContent='Start and finish the line on assigned component markers.';render();return;}
  if(first===second){st.textContent='A component cannot be linked to itself.';render();return;}
  cfgObj=collect();const pattern=cfgObj.blob_detection.multicolor_patterns[patternIndex],a=Math.min(first,second),b=Math.max(first,second),id=a+'-'+b,points=linkedPoints[patternIndex];
  let link=pattern.links.find(item=>item.id===id);if(!link){link=linkedDefaults().link(a,b);pattern.links.push(link);}
  const image=document.getElementById('src'),dx=(points[b].x-points[a].x)*image.naturalWidth,dy=(points[b].y-points[a].y)*image.naturalHeight;
  const length=Math.hypot(dx,dy),angle=(Math.atan2(dy,dx)*180/Math.PI+180)%180;
  link.length_absolute={min:Math.max(0,length*.75),max:length*1.25,goal:length,weight:255};
  link.angle_absolute={min:Math.max(0,angle-15),max:Math.min(180,angle+15),goal:angle,weight:255};link.threshold=.7;
  if(id==='0-1'){pattern.links=pattern.links.filter(item=>item!==link);pattern.links.unshift(link);}
  st.textContent='Link '+id+' configured: '+Math.round(length)+' px, '+Math.round(angle)+'°.';render();
}
function addLinkedNode(patternIndex){syncLinkedEdit(blob=>{
  const pattern=blob.multicolor_patterns[patternIndex];
  if(pattern.nodes.length>=5){st.textContent='A linked object may contain at most 5 blobs.';return;}
  const id=pattern.nodes.length,colors=blob.one_color_patterns||[];
  pattern.nodes.push(linkedDefaults().node(id));
  if(colors.length)pattern.nodes[id].blob_id=[colors[Math.min(id,colors.length-1)].id];
});}
function removeLinkedNode(patternIndex,nodeIndex){syncLinkedEdit(blob=>{
  const pattern=blob.multicolor_patterns[patternIndex];
  if(pattern.nodes.length<=2){st.textContent='A linked object must contain at least two blobs.';return;}
  pattern.nodes.splice(nodeIndex,1);pattern.nodes.forEach((node,index)=>node.id=index);
  pattern.links=pattern.links.map(link=>{const ids=link.id.split('-').map(Number);if(ids.includes(nodeIndex))return null;
    link.id=(ids[0]-(ids[0]>nodeIndex?1:0))+'-'+(ids[1]-(ids[1]>nodeIndex?1:0));return link;}).filter(Boolean);
});}
function addLinkedRelation(patternIndex){syncLinkedEdit(blob=>{
  const pattern=blob.multicolor_patterns[patternIndex];if(pattern.nodes.length<2)return;
  for(let a=0;a<pattern.nodes.length;a++)for(let b=a+1;b<pattern.nodes.length;b++)if(!pattern.links.some(link=>link.id===a+'-'+b)){pattern.links.push(linkedDefaults().link(a,b));return;}
  st.textContent='All component pairs are already linked.';
});}
function updateLinkedRelation(patternIndex,linkIndex,side,value){syncLinkedEdit(blob=>{
  const link=blob.multicolor_patterns[patternIndex].links[linkIndex],ids=link.id.split('-').map(Number);ids[side]=Number(value);
  if(ids[0]===ids[1]){st.textContent='A component cannot be linked to itself.';return;}link.id=ids.join('-');
});}
function renderLinkedBlobEditor(parent){
  const blob=cfgObj.blob_detection;if(!blob)return;
  const editor=el('div','linkedEditor'),title=el('h4');title.textContent=tr('linkedEditor');editor.appendChild(title);
  (blob.multicolor_patterns||[]).forEach((pattern,patternIndex)=>{
    const section=el('details');section.open=true;const summary=el('summary');summary.textContent=(language==='ru'?'Составной объект ':'Composite object ')+pattern.id+' — '+pattern.nodes.length+(language==='ru'?' блобов, ':' blobs, ')+pattern.links.length+(language==='ru'?' связей':' links');section.appendChild(summary);
    const body=el('div','body'),countRow=el('div','row'),countLabel=el('label');countLabel.textContent=tr('components');countRow.appendChild(countLabel);
    const count=el('input');count.type='number';count.min='2';count.max='5';count.value=pattern.nodes.length;count.onchange=()=>setLinkedNodeCount(patternIndex,count.value);countRow.appendChild(count);body.appendChild(countRow);
    const guide=el('div','hint');guide.textContent=language==='ru'?'1. Задайте число компонентов. 2. Выберите каждый компонент на исходном видео. 3. Проведите связи между назначенными маркерами.':'1. Set component count. 2. Select each component on the source video. 3. Draw links between assigned markers.';body.appendChild(guide);
    const grid=el('div','linkedGrid');
    pattern.nodes.forEach((node,nodeIndex)=>{const card=el('div','linkedCard'),caption=el('div');caption.textContent=(language==='ru'?'Компонент ':'Component ')+nodeIndex+(nodeIndex===0?(language==='ru'?' (базовый)':' (base)'):'');card.appendChild(caption);
      const select=el('select');(blob.one_color_patterns||[]).forEach(color=>{const option=el('option');option.value=color.id;option.textContent=(language==='ru'?'Шаблон блоба ':'Blob pattern ')+color.id+(color.enabled===false?(language==='ru'?' (отключён)':' (disabled)'):'');option.selected=(node.blob_id||[]).includes(color.id);select.appendChild(option);});
      select.title=language==='ru'?'Назначьте шаблон цвета или блоба для этого компонента.':'Assign the detected color/blob pattern used by this component.';select.onchange=()=>syncLinkedEdit(b=>b.multicolor_patterns[patternIndex].nodes[nodeIndex].blob_id=[Number(select.value)]);card.appendChild(select);
      const pick=el('button',linkedAction&&linkedAction.type==='part'&&linkedAction.patternIndex===patternIndex&&linkedAction.nodeIndex===nodeIndex?'selecting':'');pick.textContent=tr('selectVideo');pick.onclick=()=>startLinkedPartSelection(patternIndex,nodeIndex);card.appendChild(pick);
      const remove=el('button','danger');remove.textContent=tr('remove');remove.disabled=pattern.nodes.length<=2||nodeIndex<2;remove.title=nodeIndex<2?(language==='ru'?'Компоненты 0 и 1 задают базовую связь.':'Components 0 and 1 define the base link.'):(language==='ru'?'Удалить компонент и его связи.':'Remove this component and its links.');remove.onclick=()=>removeLinkedNode(patternIndex,nodeIndex);card.appendChild(remove);grid.appendChild(card);});
    body.appendChild(grid);const addNode=el('button');addNode.textContent=language==='ru'?'+ Добавить компонент':'+ Add component';addNode.disabled=pattern.nodes.length>=5;addNode.onclick=()=>addLinkedNode(patternIndex);body.appendChild(addNode);
    const linksTitle=el('div');linksTitle.textContent=language==='ru'?'Пространственные связи':'Spatial links';linksTitle.style.marginTop='8px';body.appendChild(linksTitle);
    pattern.links.forEach((link,linkIndex)=>{const ids=link.id.split('-').map(Number),row=el('div','linkedRow');
      const baseLink=link.id==='0-1';
      [0,1].forEach(side=>{const select=el('select');pattern.nodes.forEach((node,index)=>{const option=el('option');option.value=index;option.textContent=(language==='ru'?'Компонент ':'Component ')+index;option.selected=ids[side]===index;select.appendChild(option);});select.disabled=baseLink;select.onchange=()=>updateLinkedRelation(patternIndex,linkIndex,side,select.value);row.appendChild(select);if(side===0){const arrow=el('span');arrow.textContent='↔';row.appendChild(arrow);}});
      if(baseLink){const base=el('span','hint');base.textContent=language==='ru'?'Базовая связь':'Base link';row.appendChild(base);}const remove=el('button','danger');remove.textContent=language==='ru'?'Удалить связь':'Remove link';remove.disabled=baseLink;remove.title=baseLink?(language==='ru'?'Связь 0–1 необходима алгоритму сопоставления.':'The 0-1 base link is required by the matcher.'):'';remove.onclick=()=>syncLinkedEdit(b=>b.multicolor_patterns[patternIndex].links.splice(linkIndex,1));row.appendChild(remove);body.appendChild(row);});
    const drawLink=el('button',linkedAction&&linkedAction.type==='link'&&linkedAction.patternIndex===patternIndex?'selecting':'');drawLink.textContent=tr('drawLink');drawLink.onclick=()=>startLinkedLine(patternIndex);body.appendChild(drawLink);
    const addLink=el('button');addLink.textContent='+ '+tr('manualLink');addLink.onclick=()=>addLinkedRelation(patternIndex);body.appendChild(addLink);section.appendChild(body);editor.appendChild(section);
  });parent.appendChild(editor);
}
function buildForm(obj,path,parent,open){
  for(const k of Object.keys(obj)){
    const v=obj[k], p=path.concat([k]);
    const isObj=v&&typeof v==='object'&&!Array.isArray(v);
    const isObjArr=Array.isArray(v)&&v.some(x=>x&&typeof x==='object');
    if(isObj||isObjArr){
      const d=el('details'); if(open)d.open=true;
      const s=el('summary'); s.textContent=fieldLabel(k)+(isObjArr?' ['+v.length+']':''); d.appendChild(s);
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
  if(paramTab==='detector'&&cur==='blob_detection'&&!showAll)renderLinkedBlobEditor(f);
  const sub={};
  if(paramTab==='general')sub.general_params=cfgObj.general_params;
  else if(paramTab==='uart'){
    const info=el('div','linkedEditor');info.textContent=language==='ru'?'Облегчённый бинарный UART: AA 55 | длина данных (uint16 LE) | ID сообщения | данные | CRC-16/CCITT | 55. Пакеты детекции отправляются автоматически после обработки каждого кадра.':'Lightweight binary UART: AA 55 | payload length (uint16 LE) | message ID | payload | CRC-16/CCITT | 55. Detection packets are sent automatically after each processed frame.';f.appendChild(info);
    sub.transports={uart_binary:((cfgObj.transports||{}).uart_binary||{})};
  }
  else Object.keys(cfgObj).filter(k=>k!=='general_params'&&(showAll||k===cur)).forEach(k=>sub[k]=cfgObj[k]);
  buildForm(sub,[],f,true);
  drawSelection();
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
  cfgObj=normalizeConfig(j);language=(j.general_params||{}).ui_language==='en'?'en':'ru';cur=(j.general_params||{}).processing_mode||cur;applyLocalization();mark();render();});}
function current(){
  if(document.getElementById('raw').checked){
    try{return JSON.parse(document.getElementById('cfg').value);}
    catch(e){st.textContent='невалидный json: '+e.message;return null;}
  }
  return collect();
}
function send(ep){
  const o=current(); if(!o)return;
  fetch(ep,{method:'POST',headers:admHeaders(),body:JSON.stringify(o,null,2)})
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

static void sendMjpegStream(int c, bool source)
{
    struct ClientGuard
    {
        ClientGuard() { ++gPreviewClients; }
        ~ClientGuard() { --gPreviewClients; }
    } clientGuard;
    static constexpr char header[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    if (!sendAll(c, header, sizeof(header) - 1))
        return;

    uint64_t sentSequence = 0;
    while (true)
    {
        std::string jpeg;
        {
            std::unique_lock<std::mutex> lock(gPreviewMutex);
            gPreviewJpegReady.wait_for(lock, std::chrono::seconds(2), [&] {
                return !gPreviewRunning || gPreviewJpegSequence != sentSequence;
            });
            if (!gPreviewRunning)
                return;
            if (gPreviewJpegSequence == sentSequence)
                continue;
            const std::string& current = source ? gPreviewSourceJpeg
                                                : gPreviewResultJpeg;
            if (current.empty())
                continue;
            jpeg = current;
            sentSequence = gPreviewJpegSequence;
        }
        char partHeader[128];
        int size = snprintf(partHeader, sizeof(partHeader),
                            "--frame\r\nContent-Type: image/jpeg\r\n"
                            "Content-Length: %zu\r\n\r\n",
                            jpeg.size());
        if (!sendAll(c, partHeader, static_cast<size_t>(size)) ||
            !sendAll(c, jpeg.data(), jpeg.size()) ||
            !sendAll(c, "\r\n", 2))
            return;
    }
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
    if (bind(sfd, (struct sockaddr*)&a, sizeof(a)) || listen(sfd, 16))
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
        timeval sendTimeout{};
        sendTimeout.tv_sec = 2;
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &sendTimeout, sizeof(sendTimeout));

        std::thread([c]() {

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

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
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
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
                if (gSystemAdmin && !gSystemAdmin->permitsConfiguration(j) &&
                    !gSystemAdmin->authorized(req))
                {
                    sendResp(c, "application/json",
                             R"({"error":"admin token is required to change system_admin"})");
                    close(c);
                    return;
                }
#endif
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
        else if (req.compare(0, 17, "GET /preview.mjpg") == 0)
        {
            sendMjpegStream(c, false);
        }
        else if (req.compare(0, 16, "GET /source.mjpg") == 0)
        {
            sendMjpegStream(c, true);
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
        else if (req.compare(0, 16, "GET /uart/rx-log ") == 0 ||
                 req.compare(0, 16, "GET /uart/tx-log ") == 0)
        {
            const auto snapshot = req.compare(0, 16, "GET /uart/tx-log ") == 0
                ? UartTxLog::instance().snapshot() : UartRxLog::instance().snapshot();
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& entry : snapshot.entries)
                entries.push_back({{"sequence", entry.sequence}, {"time_ms", entry.timeMs},
                    {"kind", entry.kind}, {"detail", entry.detail}, {"hex", entry.hex}});
            nlohmann::json body = {{"state", snapshot.state}, {"device", snapshot.device},
                {"bytes", snapshot.bytes}, {"packets", snapshot.packets}, {"entries", entries}};
            sendResp(c, "application/json", body.dump());
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
        }).detach();
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
    int cameraWidth = 640;
    int cameraHeight = 480;
    int cameraFps = 15;
    bool cameraMjpeg = false;
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
        else if (!strcmp(argv[i], "--camera-width") && i + 1 < argc)
            cameraWidth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--camera-height") && i + 1 < argc)
            cameraHeight = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--camera-fps") && i + 1 < argc)
            cameraFps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--camera-mjpeg"))
            cameraMjpeg = true;
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
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
    configurePreview(reader.getRawConfig());
#endif

    // 2-3. Процессор (фабрика внутри менеджера)
    ProcessingManager manager(runtimeConfig(reader.getRawConfig()));

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
#ifdef HOST_CAMERA_SOURCE
    CameraSource source(cameraDevice, cv::CAP_V4L2, cameraWidth, cameraHeight,
                        cameraFps, cameraMjpeg);
    if (!source.isOpened())
    {
        std::cerr << "Failed to open host camera " << cameraDevice << std::endl;
        return -1;
    }
    std::cout << "host UI source: camera " << cameraDevice << " requested "
              << cameraWidth << 'x' << cameraHeight << '@' << cameraFps
              << (cameraMjpeg ? " MJPEG" : " default format") << std::endl;
#else
    SyntheticSource source;
    std::cout << "host UI source: synthetic 640x480 @ 30 FPS" << std::endl;
#endif
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
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
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
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
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
            manager.reconfigure(runtimeConfig(j));
            configurePreview(j);
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
            gSystemAdmin = std::make_unique<SystemAdmin>(j);
#endif
            return "";
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    };

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5)
    gSystemAdmin = std::make_unique<SystemAdmin>(reader.getRawConfig());
#endif
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
                const std::string error = gApply ? gApply(r2.getRawConfig())
                                                 : "apply handler is not ready";
                if (!error.empty())
                    throw std::runtime_error(error);
                std::cout << "config reloaded" << std::endl;
            }
            catch (const std::exception& e)
            {
                std::cerr << "reconfig failed (старый анализатор оставлен): "
                          << e.what() << std::endl;
            }
        }
    });
    startPreviewEncoder();
    std::thread control(controlServer, 8081);
    control.detach();
#endif

    // 6. Запуск (блокируется до остановки)
    pipeline.start();

#if defined(MTV3_BOARD) || defined(RASPBERRY_CM5) || defined(HOST_WEB_UI)
    ctlRun = false;
    watcher.join();
    stopPreviewEncoder();
#endif

    source.release();
#if !defined(MTV3_BOARD) && !defined(RASPBERRY_CM5) && !defined(HOST_WEB_UI)
    cv::destroyAllWindows();
#endif
    return 0;
}
