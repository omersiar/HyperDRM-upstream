'use strict';
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const c={};vm.createContext(c);vm.runInContext(fs.readFileSync('app/calibration.js','utf8'),c);
const baseline=Array(16).fill(10);
const patches=Array.from({length:32},(_,i)=>Array.from({length:16},(_,r)=>{
 const center=r*2+.5,distance=Math.min(Math.abs(i-center),32-Math.abs(i-center));
 return 10+80*Math.max(0,1-distance/2);
}));
const fit=c.calibrationFit(baseline,patches,'OTHER-TV');assert.equal(fit.points.length,16);
fit.points.forEach((p,i)=>{assert.equal(p.region,i);assert(Math.abs(p.position-(i*2+1)/8)<.05);});
assert.throws(()=>c.calibrationFit(baseline,Array.from({length:32},()=>Array(16).fill(90)),'OTHER-TV'),/reliable spatial/);
assert.throws(()=>c.calibrationFit(baseline,Array.from({length:32},()=>baseline),'OTHER-TV'),/reliable spatial/);
assert.throws(()=>c.calibrationFit(baseline,patches.slice(1),'OTHER-TV'),/Incomplete/);
assert.throws(()=>c.calibrationAverage([baseline,baseline]),/fresh/);
const nested=patches.map(p=>p.map((v,r)=>r===15?p[0]:v));
const reduced=c.calibrationFit(baseline,nested,'NESTED-TV');assert.equal(reduced.points.length,15);assert(!reduced.points.some(p=>p.region===15));
const duplicate=patches.map(p=>Array(16).fill(p[0]));assert.throws(()=>c.calibrationFit(baseline,duplicate,'DUPLICATE-TV'),/reliable spatial/);
assert.deepEqual(Array.from(c.calibrationAverage([baseline,baseline,baseline])),baseline);
console.log('PASS: measured edge positions, global/absent response rejection and fresh sample minimum');
