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

/* Extent sweeps: a white/black split at s leaves (1-(s-x0)/(x1-x0)) black. */
const known=Array.from({length:16},(_,r)=>({region:r,x0:(r%4)/4,y0:Math.floor(r/4)/4,x1:(r%4+1)/4,y1:(Math.floor(r/4)+1)/4}));
const black=Array(16).fill(100),white=Array(16).fill(0);
function splitBlack(axis){
 return Array.from({length:9},(_,k)=>Array.from({length:16},(_,r)=>{
  const s=(k+1)/10,rect=known[r],start=axis==='x'?rect.x0:rect.y0,end=axis==='x'?rect.x1:rect.y1;
  return Math.round(100*(1-Math.max(0,Math.min(1,(s-start)/(end-start)))));
 }));
}
const rects=c.calibrationExtents(black,white,splitBlack('x'),splitBlack('y'));
assert.equal(rects.length,16);
rects.forEach((got,i)=>{const want=known[got.region];assert.equal(got.region,i);assert(Math.abs(got.x0-want.x0)<=.02);assert(Math.abs(got.x1-want.x1)<=.02);assert(Math.abs(got.y0-want.y0)<=.02);assert(Math.abs(got.y1-want.y1)<=.02);});
assert.throws(()=>c.calibrationExtents(black,black,splitBlack('x'),splitBlack('y')),/No reliable region extents/);
assert.throws(()=>c.calibrationExtents(black,white,splitBlack('x').slice(1),splitBlack('y')),/Incomplete extent measurements/);
assert.equal(c.calibrationMean([]),null);
assert.deepEqual(Array.from(c.calibrationMean([black,black])),black);
const rgbSamples=[Array.from({length:16},()=>[10,20,30]),Array.from({length:16},()=>[30,40,50])];
const rgbMean=c.calibrationMeanRgb(rgbSamples);assert.equal(rgbMean.length,16);assert.deepEqual(Array.from(rgbMean[0]),[20,30,40]);assert.equal(c.calibrationMeanRgb([]),null);
console.log('PASS: measured edge positions, global/absent response rejection, fresh sample minimum, split-sweep rectangles and RGB references');
