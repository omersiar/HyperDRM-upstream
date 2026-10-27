'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const elements={},events={},requests=[];let active=false,sequence=0,globalResponse=false,holdStatus=false,heldStatus=null;
['calibrationVideo','cancelCalibration','calibrate','validateCalibration','wizardStatus','customProfile'].forEach(id=>elements[id]={style:{},disabled:false,currentTime:0,textContent:'',value:'',events:{},focus(){},pause(){},play(){return Promise.resolve();},addEventListener(k,f){this.events[k]=f;}});
const c={profile:'auto',profileDirty:false,targetDirty:false,showProfile(){},display(){},setInterval(){return 1;},clearInterval(){},setTimeout(){},Date,Array,JSON,
 window:{addEventListener(k,f){events[k]=f;}},document:{getElementById(id){return elements[id];},addEventListener(k,f){events[k]=f;}},
 call(method,done,data){
  requests.push({method,data});if(method==='status' && holdStatus){heldStatus=done;return;}if(method==='calibrationSession')active=data.enabled;
  const t=elements.calibrationVideo.currentTime,step=t<4?-1:Math.floor((t-4)/2);
  const regions=Array.from({length:16},(_,r)=>{
   const center=r*2+.5,d=Math.min(Math.abs(step-center),32-Math.abs(step-center));
   const value=step<0?10:10+80*(globalResponse?1:Math.max(0,1-d/2));return [value,value,value];
  });
  done(method==='status'?{returnValue:true,running:true,activeSource:'HyperDRM',tvModel:'OTHER-TV',colorValid:true,calibrating:active,sampleSequence:++sequence,sampleAgeMs:10,regions}:{returnValue:true});
 }
};vm.createContext(c);
vm.runInContext(fs.readFileSync('app/calibration.js','utf8'),c);vm.runInContext(fs.readFileSync('app/wizard.js','utf8'),c);
function measure(){
 elements.calibrate.onclick();assert(active);assert.deepEqual(c.window.remoteNavigationScope,['cancelCalibration']);
 [1,1.5,2].forEach(t=>{elements.calibrationVideo.currentTime=t;c.wizardSample();});
 for(let step=0;step<32;step++)[.8,1.1,1.4].forEach(t=>{elements.calibrationVideo.currentTime=4+step*2+t;c.wizardSample();});
 elements.calibrationVideo.currentTime=68;c.wizardSample();
 assert(!active && c.wizard===null && c.window.remoteNavigationScope===null);
}
measure();assert.equal(c.profile,'custom');assert(c.profileDirty);
const proposal=JSON.parse(elements.customProfile.value);assert.equal(proposal.points.length,16);
assert(requests.some(r=>r.method==='calibrationDraft'));assert(!requests.some(r=>r.method==='calibration'));
c.profileDirty=false;globalResponse=true;measure();assert(elements.wizardStatus.textContent.includes('reliable spatial'));assert.equal(elements.customProfile.value,JSON.stringify(proposal,null,2));
globalResponse=false;c.profileDirty=false;elements.customProfile.value=JSON.stringify({model:'OTHER-TV',points:proposal.points,geometry:'rectangles',rectangles:[{region:3,x0:0,y0:0,x1:0.5,y1:0.19}]});measure();
const kept=JSON.parse(elements.customProfile.value);assert.equal(kept.geometry,'rectangles');assert.equal(kept.rectangles.length,1);assert.equal(kept.rectangles[0].region,3);assert(elements.wizardStatus.textContent.includes('region rectangles'));
c.profileDirty=false;globalResponse=false;elements.calibrate.onclick();elements.cancelCalibration.onclick();assert(!active && !c.wizard);
elements.validateCalibration.onclick();assert.equal(elements.calibrationVideo.currentTime,68);elements.calibrationVideo.events.ended();assert(!active && elements.wizardStatus.textContent.includes('Top-left red'));
elements.calibrate.onclick();let prevented=false;events.keydown({keyCode:461,preventDefault(){prevented=true;}});assert(prevented && !active);
elements.calibrate.onclick();holdStatus=true;elements.calibrationVideo.currentTime=1;c.wizardSample();assert(heldStatus);const oldReply=heldStatus;
elements.cancelCalibration.onclick();holdStatus=false;elements.calibrate.onclick();const newSession=c.wizard;oldReply({returnValue:false,errorText:'Old failure'});assert.strictEqual(c.wizard,newSession);assert(!c.wizard.busy);elements.cancelCalibration.onclick();
console.log('PASS: guided measurement, proposal without save, geometry kept, invalid-response preservation, validation, cancel/Back and scoped remote navigation');
