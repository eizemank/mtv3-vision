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
    std::lock_guard<std::mutex> lock(gPreviewMutex);
    const cv::Mat& frame = source ? gPreviewSourceFrame : gPreviewResultFrame;
    if (frame.empty())
        return false;

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
details{border:1px solid #383838;margin:6px 0;background:#161616}
details>summary{padding:5px 8px;cursor:pointer;background:#1e1e1e;color:#cda;font-weight:bold}
details details>summary{font-weight:normal;color:#9bd}
.body{padding:4px 10px 8px}
.row{display:flex;align-items:center;gap:8px;padding:2px 0}
.row label{flex:0 0 260px;color:#bbb;font-size:13px}
.row input[type=text],.row input[type=number]{background:#222;color:#eee;border:1px solid #555;padding:3px 6px;width:190px}
.row input:focus{border-color:#7a7;outline:none}
.hint{color:#666;font-size:12px}
</style></head><body>
<h3>SeeSharp vision</h3>
<div class="streams"><div><h4>Source</h4><img id="src" alt="source is not ready"></div>
<div><h4>Detection result</h4><img id="v" alt="result is not ready"></div></div><br>
<div id="modes"></div>
<h4>Параметры <span class="hint">(секция активного режима + общие)</span></h4>
<div><label class="hint"><input type="checkbox" id="all" onchange="render()"> показать все секции</label>
&nbsp;<label class="hint"><input type="checkbox" id="raw" onchange="render()"> редактировать JSON</label></div>
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
setInterval(()=>{const t=Date.now();document.getElementById('src').src='/source.jpg?t='+t;
document.getElementById('v').src='/preview.jpg?t='+t;},200);
const md=document.getElementById('modes');
let cur='', cfgObj={};
modes.forEach(m=>{const b=document.createElement('button');b.textContent=m;b.dataset.m=m;
b.onclick=()=>fetch('/mode/'+m).then(r=>r.text()).then(t=>{st.textContent=t;load();});
md.appendChild(b);});
function mark(){[...md.children].forEach(b=>b.className=b.dataset.m===cur?'act':'');}
function el(t,c){const e=document.createElement(t);if(c)e.className=c;return e;}
function fieldRow(key,val,path){
  const r=el('div','row'), l=el('label'); l.textContent=key; r.appendChild(l);
  let i=el('input');
  if(typeof val==='boolean'){i.type='checkbox';i.checked=val;i.dataset.t='b';}
  else if(typeof val==='number'){i.type='number';i.value=val;i.dataset.t='n';
    i.step=Number.isInteger(val)?'1':'any';}
  else if(Array.isArray(val)){i.type='text';i.value=val.join(', ');i.dataset.t='a';
    i.dataset.num=val.every(x=>typeof x==='number')?'1':'0';}
  else{i.type='text';i.value=val;i.dataset.t='s';}
  i.dataset.path=JSON.stringify(path); r.appendChild(i); return r;
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
  const sub={}; Object.keys(cfgObj).filter(k=>showAll||k===cur||k==='general_params')
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
function load(){fetch('/config').then(r=>r.json()).then(j=>{
  cfgObj=j; cur=(j.general_params||{}).processing_mode||cur; mark(); render();});}
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
