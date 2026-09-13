
var cycles = 0;
function gc() {}
function sleep() {}
var region = function () { return {freeBytes: 100000-cycles*16, largestFreeBlockBytes: 64000}; };
var memory = {};
['internal','psram','dma','default'].forEach(function (name) {
  Object.defineProperty(memory,name,{enumerable:true,get:region});
});
Object.defineProperty(memory,'manager',{enumerable:true,get:function () { return {
  managedPsramBytes: LEAK ? cycles*16 : 0, managedInternalBytes:0
}; }});
var sys = {status:{memory:memory}, tasks:function () {return {truncated:false,tasks:CLEANUP_PENDING ? [{state:'deleted'}] : []};}};
var ble = {open:function () { cycles++; return {
  scan:function () { return {stats:function () {return {dropped:0};}, close:function () {return true;}}; },
  close:function () {return true;}
};}};
var espNow = {open:function () {return {status:function () {return {open:true};},close:function () {}};}};
var wifi = {
  status:function () {return {radio:{clients:{total:0,espNow:0,wifiCsi:0}}};},
  csi:{capabilities:function () {return {configSchema:'wifi-csi-he/1'};},
    open:function () {return {close:function () {},stats:function () {return {leasedFrames:0};}};}}
};
function test(name, run) {
  try { console.log(JSON.stringify({value:run()})); }
  catch (error) { console.log(JSON.stringify({error:error.message})); }
}
