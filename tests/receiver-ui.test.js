'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const ids=['reloadService','versionWarning','host','port','piccapPort','captureInterval','receiver','token','clearToken','endpoint','handover','autostart','error','status','regions','start','stop','refresh','saveTarget','testConnection','testResult','diagnostics','profile','customEditor','customProfile','saveProfile','profileStatus','calibrate','validateCalibration','cancelCalibration','editProfile','customProfileField','overviewTab','receiverTab','calibrationTab','calibrationShortcut','hostField','portField','piccapPortField','captureIntervalField','tokenField','grantRoot','rootHint'];
const elements={};
ids.forEach(id=>elements[id]={value:'',textContent:'',disabled:false,checked:false,attributes:{},style:{},addEventListener(){},focus(){},appendChild(){},setAttribute(k,v){this.attributes[k]=v;},getAttribute(k){return this.attributes[k];}});
const requests=[];
const status={returnValue:true,version:'0.9.0',host:'127.0.0.1',port:19444,piccapPort:19400,receiver:'HyperHDR',tokenSet:true,piccapInstalled:true,autostart:true,captureState:'Histogram',playbackProtection:'protected',connectionTest:{state:'Passed',ledCount:120,elapsedMs:300,error:''},uid:0,calibrationProfile:'auto',effectiveCalibration:'g4',tvModel:'OLED65G42LW.DEUQLJP',customModel:'',customPoints:[]};
class Bridge {
 call(uri,data){requests.push({uri,data:JSON.parse(data)});this.onservicecallback(JSON.stringify(uri.endsWith('/status')?status:{returnValue:true}));}
 cancel(){}
}
let controls;
const context={PalmServiceBridge:Bridge,document:{getElementById:id=>elements[id],createElement:()=>({style:{}})},window:{},setTimeout:()=>1,clearTimeout(){},setInterval(){},setupRemoteNavigation(list){controls=list;}};
vm.runInNewContext(fs.readFileSync('app/app.js','utf8'),context);
assert(elements.testResult.textContent.includes('120 LEDs'));
assert(elements.diagnostics.textContent.includes('Root service'));
assert(controls.includes(elements.piccapPortField)&&controls.includes(elements.testConnection));
context.targetDirty=true;elements.testConnection.onclick();assert(elements.error.textContent.includes('Save receiver'));
elements.piccapPort.value='0';elements.saveTarget.onclick();assert(elements.error.textContent.includes('FlatBuffer port'));
elements.piccapPort.value='19446';elements.captureInterval.value='26';elements.saveTarget.onclick();assert(elements.error.textContent.includes('Capture interval'));elements.captureInterval.value='100';elements.piccapPort.value='19446';elements.host.value='10.1.0.100';elements.token.value='';elements.saveTarget.onclick();
const saved=requests.find(r=>r.uri.endsWith('/settings'));assert.strictEqual(saved.data.piccapPort,19446);assert.strictEqual(saved.data.captureIntervalMs,100);assert.strictEqual(saved.data.host,'10.1.0.100');assert(!('token' in saved.data));
elements.testConnection.onclick();assert(requests.some(r=>r.uri.endsWith('/testConnection')));
assert.strictEqual(elements.token.value,'');assert(!elements.diagnostics.textContent.includes('fixture-token'));
elements.profile.onclick();assert(elements.profile.textContent.includes('LG G4'));elements.saveProfile.onclick();assert(requests.some(r=>r.uri.endsWith('/calibration')&&r.data.profile==='g4'));
context.profile='custom';context.showProfile();elements.customProfile.value='invalid';elements.saveProfile.onclick();assert(elements.error.textContent.includes('valid JSON'));
elements.customProfile.value=JSON.stringify({model:'OTHER-TV',points:[{position:0,region:1},{position:2,region:5}]});elements.saveProfile.onclick();assert(requests.some(r=>r.uri.endsWith('/calibration')&&r.data.model==='OTHER-TV'&&r.data.points.length===2));
console.log('PASS: receiver fields, port validation, saved-token retention, test control and diagnostics');
