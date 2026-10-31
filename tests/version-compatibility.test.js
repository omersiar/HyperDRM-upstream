'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const elements={},requests=[];let serviceVersion='0.12.0';
for(const match of fs.readFileSync('app/index.html','utf8').matchAll(/id="([^"]+)"/g))elements[match[1]]={style:{},value:'',textContent:'',disabled:false,attributes:{},addEventListener(){},focus(){},appendChild(){},getAttribute(k){return this.attributes[k];},setAttribute(k,v){this.attributes[k]=v;}};
class Bridge {
 call(uri){requests.push(uri);this.onservicecallback(JSON.stringify(uri.endsWith('/status')?{returnValue:true,version:serviceVersion,piccapInstalled:false,receiverConnecting:true,connectionTest:{state:'Not tested'}}:{returnValue:true}));}
 cancel(){}
}
const c={document:{getElementById(id){assert(elements[id],id);return elements[id];},createElement(){return {style:{}};},querySelectorAll(){return [];}},window:{},PalmServiceBridge:Bridge,setupRemoteNavigation(){},setInterval(){},setTimeout(){return 1;},clearTimeout(){}};
vm.createContext(c);vm.runInContext(fs.readFileSync('app/version.js','utf8'),c);vm.runInContext(fs.readFileSync('app/app.js','utf8'),c);
assert(!c.versionCompatible && elements.start.disabled && elements.saveTarget.disabled);
assert(elements.versionWarning.textContent.includes('0.12.0'));assert.equal(elements.reloadService.style.display,'inline-block');
elements.reloadService.onclick();assert(requests.some(uri=>uri.endsWith('/restartService')));
const count=requests.length;c.call('start',r=>assert.equal(r.returnValue,false));assert.equal(requests.length,count);
elements.stop.onclick();assert(requests.some(uri=>uri.endsWith('/stop')));
serviceVersion=c.HYPERDRM_APP_VERSION;c.call('status',c.display);
assert(c.versionCompatible && !elements.start.disabled && elements.versionWarning.style.display==='none');
assert.equal(elements.reloadService.style.display,'none');assert(elements.handover.disabled);assert(elements.status.textContent.includes('Connecting'));
serviceVersion=undefined;c.call('status',c.display);assert(!c.versionCompatible && elements.versionWarning.textContent.includes('unknown'));
c.display({returnValue:false,errorText:'Disconnected'});assert(!c.versionCompatible);
console.log('PASS: old/missing service versions block mutations, Stop remains available, matching service restores controls and unavailable PicCap remains disabled');
