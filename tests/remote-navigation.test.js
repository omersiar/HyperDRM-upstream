const assert=require('assert');
const fs=require('fs');
const path=require('path');
const vm=require('vm');
const handlers={},windowHandlers={};
const doc={activeElement:null,addEventListener(type,handler){handlers[type]=handler;}};
const buttons=['start','stop','refresh','autostart','handover'].map(id=>({
 id,disabled:id==='handover',clicks:0,handlers:{},
 addEventListener(type,handler){this.handlers[type]=handler;},
 focus(){doc.activeElement=this;this.handlers.focus();},
 click(){this.clicks++;}
}));
const context={};vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../app/remote.js'),'utf8'),context);
context.setupRemoteNavigation(buttons,doc,{addEventListener(type,handler){windowHandlers[type]=handler;}});
function key(code,type='keydown',repeat=false){let prevented=false;handlers[type]({keyCode:code,repeat,preventDefault(){prevented=true;}});return prevented;}
buttons[0].focus();assert(key(39));assert.equal(doc.activeElement.id,'stop');
key(37);assert.equal(doc.activeElement.id,'start');key(37);assert.equal(doc.activeElement.id,'autostart');
key(39);assert.equal(doc.activeElement.id,'start');
buttons[1].disabled=true;key(39);assert.equal(doc.activeElement.id,'refresh');
key(13);assert.equal(buttons[2].clicks,1);key(13);key(13,'keydown',true);assert.equal(buttons[2].clicks,1);
key(13,'keyup');key(13);assert.equal(buttons[2].clicks,2);key(13,'keyup');
buttons[3].handlers.mouseenter();assert.equal(doc.activeElement.id,'autostart');key(37);assert.equal(doc.activeElement.id,'refresh');
doc.activeElement=null;key(39);assert.equal(doc.activeElement.id,'refresh');
assert(!key(461));assert(!key(65));
key(40);assert.equal(doc.activeElement.id,'autostart');key(38);assert.equal(doc.activeElement.id,'refresh');
key(13);windowHandlers.blur();key(13);assert.equal(buttons[2].clicks,4);
buttons[4].disabled=false;buttons[3].focus();key(39);assert.equal(doc.activeElement.id,'handover');
key(39);assert.equal(doc.activeElement.id,'start');key(37);assert.equal(doc.activeElement.id,'handover');
buttons[2].tagName='INPUT';buttons[2].type='text';buttons[2].focus();
assert(!key(37));assert(!key(39));assert(!key(13));assert.equal(doc.activeElement.id,'refresh');
assert(!key(40));assert(!key(38));assert.equal(doc.activeElement.id,'refresh');
buttons[2].tagName='TEXTAREA';buttons[2].focus();
assert(!key(37));assert(!key(39));assert(!key(13));
assert(!key(40));assert(!key(38));assert.equal(doc.activeElement.id,'refresh');
buttons[2].readOnly=true;assert(key(40));assert.equal(doc.activeElement.id,'autostart');
buttons[2].tagName='BUTTON';
buttons.forEach(button=>button.disabled=true);assert(key(39));
console.log('PASS: arrow navigation, wraparound, disabled controls, OK once per press, pointer handoff, focus recovery, Back preserved');
