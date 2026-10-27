'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const elements={},requests=[],timers=new Map();let next=0,uid=1000,enabled=false,deny=false,delayRoot=false,pendingRoot=null;
for(const match of fs.readFileSync('app/index.html','utf8').matchAll(/id="([^"]+)"/g))elements[match[1]]={style:{},value:'',textContent:'',attributes:{},disabled:false,addEventListener(){},focus(){},appendChild(){},setAttribute(k,v){this.attributes[k]=v;},getAttribute(k){return this.attributes[k];}};
class Bridge {
 call(uri,raw){
  const data=JSON.parse(raw);requests.push({uri,data});let r={returnValue:true};
  if(uri.endsWith('/status'))r={returnValue:true,uid,autostart:enabled,piccapInstalled:true,connectionTest:{state:'Not tested'},calibrationProfile:'auto',effectiveCalibration:'g4',customPoints:[],tvModel:'OLED65G42LW.DEUQLJP'};
  if(uri.endsWith('/autostart')){if(uid)r={returnValue:false,rootRequired:true};else enabled=data.enabled;}
  if(uri.includes('hbchannel')){if(delayRoot){pendingRoot=this;return;}if(deny)r={returnValue:false,errorText:'Denied by Homebrew Channel'};}
  if(uri.endsWith('/restartService'))uid=0;
  this.onservicecallback(JSON.stringify(r));
 }
 cancel(){}
}
const c={document:{getElementById(id){assert(elements[id],id);return elements[id];},createElement(){return {};}},window:{},PalmServiceBridge:Bridge,setupRemoteNavigation(){},setInterval(){},setTimeout(fn){const id=++next;timers.set(id,fn);return id;},clearTimeout(id){timers.delete(id);}};
vm.createContext(c);vm.runInContext(fs.readFileSync('app/app.js','utf8'),c);
function flush(){for(let i=0;timers.size && i<20;i++){const [id,fn]=timers.entries().next().value;timers.delete(id);fn();}assert(!timers.size);}
assert.equal(elements.grantRoot.style.display,'inline-block');elements.autostart.onclick();flush();
assert(enabled && !elements.autostart.disabled && uid===0);assert.equal(elements.grantRoot.style.display,'none');
const grant=requests.find(r=>r.uri.includes('hbchannel'));assert.deepEqual(grant.data,{id:'org.webosbrew.histogramhyperion.service'});assert(requests.some(r=>r.uri.endsWith('/restartService')));
const grantCount=requests.filter(r=>r.uri.includes('hbchannel')).length;
elements.autostart.onclick();flush();assert(!enabled);assert.equal(requests.filter(r=>r.uri.includes('hbchannel')).length,grantCount);
uid=1000;deny=true;elements.autostart.onclick();flush();assert(!enabled && !elements.autostart.disabled && !elements.grantRoot.disabled);assert(elements.error.textContent.includes('could not grant root'));
const retainedError=elements.error.textContent;c.call('status',c.display);assert.equal(elements.error.textContent,retainedError);
c.showActionError('Invalid receiver settings');c.call('status',c.display);assert.equal(elements.error.textContent,'Invalid receiver settings');
deny=false;delayRoot=true;elements.grantRoot.onclick();elements.autostart.onclick();assert(pendingRoot && c.rootWaiters.length===2);
delayRoot=false;pendingRoot.onservicecallback(JSON.stringify({returnValue:true}));flush();assert(enabled && !elements.autostart.disabled && !c.rootWaiters.length);
assert.equal(c.actionError,'');
console.log('PASS: root-required autostart repairs only own service, confirms root after restart, retries setting, avoids unnecessary elevation and reports denial');
