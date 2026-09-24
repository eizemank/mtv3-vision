// Run with: node cpp/SeeSharp/tests/dev_mode_ui.test.cjs
// Проверяет чистые хелперы режима разработчика и журналов UART
// (include/web/dev_mode_js.hpp): извлекает блок DEVJS и выполняет его в node:vm.
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'include/web/dev_mode_js.hpp'),'utf8').split('R"DEVJS(')[1].split(')DEVJS"')[0];
const context={console,Date,JSON,Math,Number,Object,Array,String};
vm.createContext(context);
vm.runInContext(source,context);
const{uartEntryVisible,uartLogSummary,uartEntryText,devModeEnabled,devRunSummary,devResultClass}=context;

// Журнал: сырые байты скрыты, события и пакеты видны
assert.equal(uartEntryVisible({kind:'BYTES'},false),false);
assert.equal(uartEntryVisible({kind:'BYTES'},true),true);
for(const kind of ['PACKET','INFO','WARN','ERROR'])assert.equal(uartEntryVisible({kind},false),true,kind);

assert.equal(uartLogSummary({state:'Binary TX active @ 115200',device:'/dev/ttyAMA0',bytes:50,packets:1,counters:{frames_sent:1,write_errors:0},last_error:''},'TX'),
  'Binary TX active @ 115200 /dev/ttyAMA0 | TX bytes=50, packets=1 | frames_sent=1, write_errors=0');
assert.equal(uartLogSummary({state:'UART disabled in config',device:'',bytes:0,packets:0,last_error:'open: No such file'},'RX'),
  'UART disabled in config | RX bytes=0, packets=0 | last error: open: No such file');

const entry={time_ms:Date.UTC(2026,8,24,10,0,0,7),sequence:3,kind:'INFO',detail:'Binary TX stopped',hex:''};
assert.match(uartEntryText(entry),/\.007 #3 INFO Binary TX stopped$/);
assert.match(uartEntryText({...entry,kind:'PACKET',hex:'AA 55'}),/\nAA 55$/);

// Режим разработчика: ?dev=1/0 главнее сохранённого значения
assert.equal(devModeEnabled('',null),false);
assert.equal(devModeEnabled('','1'),true);
assert.equal(devModeEnabled('?dev=1','0'),true);
assert.equal(devModeEnabled('?a=2&dev=0','1'),false);

assert.equal(devRunSummary({results:[],passed:5,failed:1,skipped:2}),'PASS 5 · FAIL 1 · SKIP 2');
assert.match(devRunSummary({error:'x'}),/Invalid response/);
assert.equal(devResultClass('PASS'),'pass');
assert.equal(devResultClass('FAIL'),'fail');
assert.equal(devResultClass('SKIP'),'skip');
assert.equal(devResultClass('???'),'');
console.log('dev mode UI tests: PASS');
