// Режим разработчика web UI: переключатель, панель самотестов (/dev/tests)
// и хелперы журналов UART. Подключается в kPage (main.cpp) отдельным
// <script> до журналов UART и не зависит от видео/конфига.
// Чистые функции в начале не трогают DOM — их проверяет
// tests/dev_mode_ui.test.cjs (извлекает блок DEVJS и гоняет в node:vm).
R"DEVJS(
// --- чистые хелперы (без DOM) ---
function uartEntryVisible(entry,showRaw){return showRaw||entry.kind!=='BYTES';}
function uartLogSummary(log,direction){
  const counters=Object.entries(log.counters||{}).map(([k,v])=>k+'='+v).join(', ');
  return `${log.state}${log.device?' '+log.device:''} | ${direction} bytes=${log.bytes}, packets=${log.packets}`+
    (counters?' | '+counters:'')+(log.last_error?' | last error: '+log.last_error:'');
}
function uartEntryText(entry){
  const t=entry.time_ms,time=new Date(t).toLocaleTimeString()+'.'+String(t%1000).padStart(3,'0');
  return `${time} #${entry.sequence} ${entry.kind} ${entry.detail}`+(entry.hex?'\n'+entry.hex:'');
}
function devModeEnabled(search,stored){
  const match=/[?&]dev=([01])/.exec(search||'');
  return match?match[1]==='1':stored==='1';
}
function devRunSummary(report){
  if(!report||!Array.isArray(report.results))return 'Invalid response / Некорректный ответ';
  return `PASS ${report.passed} · FAIL ${report.failed} · SKIP ${report.skipped}`;
}
function devResultClass(status){return {PASS:'pass',FAIL:'fail',SKIP:'skip'}[status]||'';}

// --- DOM ---
function setupDevMode(){
  const toggle=document.getElementById('devMode'),panel=document.getElementById('devPanel');
  const table=document.getElementById('devTests'),status=document.getElementById('devStatus');
  const hardware=document.getElementById('devHardware');
  let stored=null;
  try{stored=localStorage.getItem('seesharp.devMode');}catch(e){}
  toggle.checked=devModeEnabled(location.search,stored);
  let loaded=false,results={};
  function headers(){const token=document.getElementById('admToken');return token&&token.value?{'X-Admin-Token':token.value}:{};}
  async function request(path,options){
    const response=await fetch(path,Object.assign({cache:'no-store',headers:headers()},options||{}));
    const text=await response.text();
    let body;try{body=JSON.parse(text);}catch(e){throw new Error(path+': '+text.slice(0,60));}
    if(!response.ok||body.error)throw new Error(body.error||('HTTP '+response.status));
    return body;
  }
  function renderTests(tests){
    table.textContent='';
    tests.forEach(test=>{
      const row=table.insertRow(),check=document.createElement('input');
      check.type='checkbox';check.checked=test.kind!=='hardware';check.dataset.id=test.id;
      row.insertCell().appendChild(check);
      row.insertCell().textContent=test.kind;
      const title=row.insertCell();title.textContent=test.title;title.title=test.id;
      const result=row.insertCell();result.dataset.result=test.id;
      const again=document.createElement('button');again.textContent='▶';again.title='Run / Запустить';
      again.onclick=()=>run([test.id]);row.insertCell().appendChild(again);
      showResult(test.id);
    });
  }
  function showResult(id){
    const cell=table.querySelector(`[data-result="${id}"]`),r=results[id];
    if(!cell)return;
    cell.textContent='';
    if(!r)return;
    const badge=document.createElement('b');badge.className=devResultClass(r.status);badge.textContent=r.status;
    cell.append(badge,` ${r.duration_ms} ms — ${r.message}`);
  }
  async function loadTests(){
    status.textContent='Loading / Загрузка…';
    try{renderTests((await request('/dev/tests')).tests);loaded=true;status.textContent='';}
    catch(error){status.textContent='Tests / Тесты: '+error.message;}
  }
  async function run(ids){
    status.textContent='Running / Выполняется…';
    table.querySelectorAll('button').forEach(b=>b.disabled=true);
    try{
      const report=await request('/dev/tests/run',{method:'POST',headers:Object.assign({'Content-Type':'application/json'},headers()),
        body:JSON.stringify({ids,allow_hardware:hardware.checked})});
      report.results.forEach(r=>{results[r.id]=r;showResult(r.id);});
      status.textContent=devRunSummary(report);
    }catch(error){status.textContent='Run / Запуск: '+error.message;}
    finally{table.querySelectorAll('button').forEach(b=>b.disabled=false);}
  }
  function apply(){
    panel.hidden=!toggle.checked;
    try{localStorage.setItem('seesharp.devMode',toggle.checked?'1':'0');}catch(e){}
    if(toggle.checked&&!loaded)loadTests();
  }
  toggle.addEventListener('change',apply);
  document.getElementById('devRunAll').onclick=()=>run([...table.querySelectorAll('input[data-id]')].map(i=>i.dataset.id));
  document.getElementById('devRunSelected').onclick=()=>run([...table.querySelectorAll('input[data-id]:checked')].map(i=>i.dataset.id));
  document.getElementById('devReload').onclick=loadTests;
  apply();
}
)DEVJS"
