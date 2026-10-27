'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const events={},windowEvents={},elements={};let inputFocuses=0;
const doc={activeElement:null,getElementById(id){return elements[id];},addEventListener(k,f){(events[k]||(events[k]=[])).push(f);}};
function element(id,input){return {id,tagName:input?'INPUT':'DIV',type:'text',readOnly:true,disabled:false,style:{},attributes:{},handlers:{},
 addEventListener(k,f){this.handlers[k]=f;},setAttribute(k,v){this.attributes[k]=v;},
 focus(){if(doc.activeElement && doc.activeElement!==this)doc.activeElement.blur();doc.activeElement=this;if(input)inputFocuses++;},
 blur(){if(this.handlers.blur)this.handlers.blur();if(doc.activeElement===this)doc.activeElement=null;}
};}
['host','port','piccapPort','captureInterval','token','customProfile'].forEach(id=>{elements[id]=element(id,true);elements[id+'Field']=element(id+'Field',false);});
['overview','receiver','calibration'].forEach(key=>{elements[key+'Panel']=element(key+'Panel');elements[key+'Tab']=element(key+'Tab');});
['start','receiver','calibrate','calibrationShortcut'].forEach(id=>elements[id]=element(id));
const win={addEventListener(k,f){windowEvents[k]=f;}};
const c={document:doc,window:win};vm.createContext(c);vm.runInContext(fs.readFileSync('app/navigation.js','utf8'),c);
elements.receiverTab.onclick();assert.equal(elements.receiverPanel.style.display,'block');assert.equal(elements.overviewPanel.style.display,'none');assert.equal(doc.activeElement.id,'receiver');
elements.hostField.focus();assert.equal(inputFocuses,0);assert(elements.host.readOnly);
elements.hostField.onclick();assert.equal(inputFocuses,1);assert(!elements.host.readOnly);assert.equal(doc.activeElement.id,'host');
events.keyboardStateChange[0]({detail:{visibility:false}});assert(elements.host.readOnly);assert.equal(doc.activeElement.id,'hostField');
elements.portField.focus();assert.equal(inputFocuses,1);elements.portField.onclick();assert.equal(inputFocuses,2);
let prevented=false;events.keydown[0]({keyCode:461,preventDefault(){prevented=true;}});assert(prevented && elements.port.readOnly && doc.activeElement.id==='portField');
elements.token.disabled=true;elements.tokenField.onclick();assert.equal(inputFocuses,2);
elements.calibrationShortcut.onclick();assert.equal(doc.activeElement.id,'calibrate');assert.equal(elements.calibrationPanel.style.display,'block');assert.equal(elements.receiverPanel.style.display,'none');
elements.customProfileField.onclick();assert.equal(inputFocuses,3);elements.start.focus();assert(elements.customProfile.readOnly);
console.log('PASS: three sections, calibration shortcut, highlight without input focus, explicit editing, keyboard close/Back and disabled input');
