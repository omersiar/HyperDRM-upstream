'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const ids=['reloadService','versionWarning','host','port','piccapPort','captureInterval','receiver','output','token','clearToken','endpoint','handover','autostart','error','status','regions','start','stop','refresh','saveTarget','testConnection','testResult','diagnostics','profile','geometry','compensate','colorStat','grade','customEditor','customProfile','saveProfile','profileStatus','calibrate','validateCalibration','cancelCalibration','editProfile','customProfileField','overviewTab','receiverTab','calibrationTab','calibrationShortcut','hostField','portField','piccapPortField','captureIntervalField','tokenField','grantRoot','rootHint','help-automatic','help-calibration','help-receiver','help-output','help-network','help-profile','help-geometry','help-compensate','help-colour','help-grade','helpClose'];
const elements={};
ids.forEach(id=>elements[id]={value:'',textContent:'',disabled:false,checked:false,attributes:{},style:{},addEventListener(){},focus(){},appendChild(){},setAttribute(k,v){this.attributes[k]=v;},getAttribute(k){return this.attributes[k];}});
const requests=[];
const status={returnValue:true,version:'0.9.0',host:'127.0.0.1',port:19444,piccapPort:19400,receiver:'HyperHDR',tokenSet:true,piccapInstalled:true,autostart:true,captureState:'Histogram',captureHold:'',connectionTest:{state:'Passed',ledCount:120,elapsedMs:300,error:''},uid:0,calibrationProfile:'auto',effectiveCalibration:'g4',tvModel:'OLED65G42LW.DEUQLJP',customModel:'',customPoints:[]};
class Bridge {
 call(uri,data){requests.push({uri,data:JSON.parse(data)});this.onservicecallback(JSON.stringify(uri.endsWith('/status')?status:{returnValue:true}));}
 cancel(){}
}
let controls;
const markup=fs.readFileSync('app/index.html','utf8');
/* Document order of every control the remote can reach: plain buttons, the
 * focusable field wrappers and the token checkbox, exactly what app.js asks the
 * document for. */
const navOrder=[...markup.matchAll(/<(button|div|input)\b([^>]*)>/g)].filter(m=>m[1]==='button'||(m[1]==='div'&&/class="field/.test(m[2]))||(m[1]==='input'&&/type="checkbox"/.test(m[2]))).map(m=>{const id=/\bid="([^"]+)"/.exec(m[2]);return id&&id[1];}).filter(id=>id&&elements[id]);
const context={PalmServiceBridge:Bridge,document:{getElementById:id=>elements[id],createElement:()=>({style:{}}),querySelectorAll:()=>navOrder.map(id=>elements[id])},window:{},setTimeout:()=>1,clearTimeout(){},setInterval(){},setupRemoteNavigation(list){controls=list;}};
vm.runInNewContext(fs.readFileSync('app/app.js','utf8'),context);
assert(elements.testResult.textContent.includes('120 LEDs'));
assert(elements.diagnostics.textContent.includes('Root service'));
assert(controls.includes(elements.piccapPortField)&&controls.includes(elements.testConnection)&&controls.includes(elements.output));
const domButtons=[...markup.matchAll(/<button id="([^"]+)"/g)].map(m=>m[1]);
const domIndex=id=>domButtons.indexOf(id),navIndex=id=>controls.indexOf(elements[id]);
['profile','saveProfile','editProfile','geometry','compensate','colorStat','grade'].reduce((previous,id)=>{assert(domIndex(previous)<domIndex(id),'markup order '+previous+'<'+id);assert(navIndex(previous)<navIndex(id),'remote order '+previous+'<'+id);return id;});
/* Every help button is reached directly after the control it explains. */
const helpOwner={'help-automatic':'handover','help-calibration':'calibrationShortcut','help-receiver':'receiver','help-output':'output','help-network':'testConnection','help-profile':'profile','help-geometry':'geometry','help-compensate':'compensate','help-colour':'colorStat','help-grade':'grade'};
Object.keys(helpOwner).forEach(id=>{assert(controls.includes(elements[id]),'help button '+id+' is unreachable');assert.equal(navIndex(id),navIndex(helpOwner[id])+1,'help order: '+id+' must follow '+helpOwner[id]);});
context.targetDirty=true;elements.testConnection.onclick();assert(elements.error.textContent.includes('Save receiver'));
elements.piccapPort.value='0';elements.saveTarget.onclick();assert(elements.error.textContent.includes('FlatBuffer port'));
elements.piccapPort.value='19446';elements.captureInterval.value='26';elements.saveTarget.onclick();assert(elements.error.textContent.includes('Capture interval'));elements.captureInterval.value='100';elements.piccapPort.value='19446';elements.host.value='10.1.0.100';elements.token.value='';
assert.strictEqual(elements.output.textContent,'Output: JSON, one instance');elements.output.onclick();assert(elements.output.textContent.includes('FlatBuffer'));elements.saveTarget.onclick();
const saved=requests.find(r=>r.uri.endsWith('/settings'));assert.strictEqual(saved.data.piccapPort,19446);assert.strictEqual(saved.data.captureIntervalMs,100);assert.strictEqual(saved.data.host,'10.1.0.100');assert(!('token' in saved.data));assert.strictEqual(saved.data.output,'flatbuffer');assert.strictEqual(saved.data.receiver,'HyperHDR');
elements.testConnection.onclick();assert(requests.some(r=>r.uri.endsWith('/testConnection')));
assert.strictEqual(elements.token.value,'');assert(!elements.diagnostics.textContent.includes('fixture-token'));
elements.profile.onclick();assert(elements.profile.textContent.includes('LG G4'));elements.saveProfile.onclick();assert(requests.some(r=>r.uri.endsWith('/calibration')&&r.data.profile==='g4'));
context.profile='custom';context.showProfile();elements.customProfile.value='invalid';elements.saveProfile.onclick();assert(elements.error.textContent.includes('valid JSON'));
elements.customProfile.value=JSON.stringify({model:'OTHER-TV',points:[{position:0,region:1},{position:2,region:5}]});elements.saveProfile.onclick();assert(requests.some(r=>r.uri.endsWith('/calibration')&&r.data.model==='OTHER-TV'&&r.data.points.length===2));
assert(!('rectangles' in requests.filter(r=>r.uri.endsWith('/calibration')).pop().data));
context.profile='custom';
elements.geometry.onclick();assert(elements.geometry.textContent==='Geometry: Region rectangles');assert(JSON.parse(elements.customProfile.value).geometry==='rectangles');
elements.compensate.onclick();assert(elements.compensate.textContent.includes('On'));assert(JSON.parse(elements.customProfile.value).compensate===true);
elements.colorStat.onclick();assert(elements.colorStat.textContent==='Colour: Vivid');assert(JSON.parse(elements.customProfile.value).colorStat==='vivid');
elements.customProfile.value=JSON.stringify({model:'OTHER-TV',points:[{position:0,region:1},{position:2,region:5}],geometry:'rectangles',rectangles:[{region:15,x0:0,y0:0.4,x1:0.19,y1:0.8}]});elements.saveProfile.onclick();
const withRectangles=requests.filter(r=>r.uri.endsWith('/calibration')).pop().data;assert.strictEqual(withRectangles.geometry,'rectangles');assert.strictEqual(withRectangles.rectangles.length,1);assert.strictEqual(withRectangles.rectangles[0].region,15);
status.calibrationProfile='custom';status.customRectangles=[{region:15,x0:0,y0:0.4,x1:0.19,y1:0.8}];status.customGeometry='rectangles';status.spatialGeometry='rectangles';status.customCompensate=true;status.colorStat='active';status.customBlack=Array.from({length:16},()=>[0,0,0]);status.customWhite=Array.from({length:16},()=>[255,255,255]);context.display(status);
assert(elements.compensate.textContent.includes('On'));assert(elements.colorStat.textContent==='Colour: Active');assert(elements.customProfile.value.includes('"compensate": true'));assert(elements.customProfile.value.includes('"colorStat": "active"'));assert(elements.customProfile.value.includes('"black"'));
assert(elements.customProfile.value.includes('"region": 15')&&elements.customProfile.value.includes('"geometry": "rectangles"'));
assert(elements.status.textContent.includes('Region rectangles'));
status.customGeometry='rim';status.spatialGeometry='rim';context.display(status);assert(!elements.status.textContent.includes('Region rectangles'));assert(elements.customProfile.value.includes('"geometry": "rim"'));
console.log('PASS: receiver fields, output transport, port validation, saved-token retention, test control, diagnostics and rectangle profile round-trip');
