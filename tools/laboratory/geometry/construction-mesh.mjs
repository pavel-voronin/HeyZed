import * as THREE from '../../crown-lab/public/vendor/three.module.js';
import {windowPartGeometry} from './solid-parts.mjs';
import {buildApprovedWindow} from './approved-window-fracture.mjs';
import {buildOpeningInsert} from './door-mesh.mjs';
import {buildSlidingDoor} from './sliding-door.mjs';
import {buildRemainingInsert,garageGateGeometry} from './remaining-inserts.mjs';
import {buildApertureWall} from './aperture-mesh.mjs';
import fracturePreset from './window-fracture-preset.json' with {type:'json'};
import {createAttachment,doorSheetMount} from './attachments/attachment-model.mjs';
import {serialiseAttachmentMaterial} from './attachments/material.mjs';

const colors={frame:'#e9e5da',wood:'#c4a885',hardware:'#c6c9cc',metal:'#777b7d',glass:'#c2d4d2',charred:'#151719',cubicle:'#77778e'};
const nativeFrameColors={charred:'#151719',soot:'#090b0d',ash:'#686860',hardware:'#aeb7bd',wood:'#ba9d76',metal:'#303b3d',glass:'#aac7ce',frame:'#d4d3c7',cubicle:'#77778e'};
function pose(transform){
 if(!transform)return null;
 const [x,z,y]=transform.pivot,angle=transform.angleDegrees*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle);
 return [c,-s,0,x-c*x+s*y,s,c,0,y-s*x-c*y,0,0,1,0];
}
function compose(a,b){
 if(!a)return b;if(!b)return a;
 const result=[];for(let row=0;row<3;row++)for(let col=0;col<4;col++)result.push(a[row*4]*b[col]+a[row*4+1]*b[4+col]+a[row*4+2]*b[8+col]+(col===3?a[row*4+3]:0));return result;
}
function meshPose(matrix){
 const origin=new THREE.Vector3().applyMatrix4(matrix),basis=[[1,0,0],[0,1,0],[0,0,1]].map(([x,y,z])=>new THREE.Vector3(-x,z,y).applyMatrix4(matrix).sub(origin));
 return [-basis[0].x,-basis[1].x,-basis[2].x,-origin.x,basis[0].z,basis[1].z,basis[2].z,origin.z,basis[0].y,basis[1].y,basis[2].y,origin.y];
}
function reflect(part,axis,offset,shared){
 if(shared){
  const m=part.pose||[1,0,0,0,0,1,0,0,0,0,1,0];
  for(let i=axis*4;i<axis*4+4;i++)m[i]=-m[i];m[axis*4+3]+=offset;part.pose=m;
 }else{
  for(let i=axis;i<part.vertices.length;i+=3)part.vertices[i]=offset-part.vertices[i];
  if(part.normals)for(let i=axis;i<part.normals.length;i+=3)part.normals[i]=-part.normals[i];
  for(let i=0;i<part.indices.length;i+=3)[part.indices[i+1],part.indices[i+2]]=[part.indices[i+2],part.indices[i+1]];
 }
}
function serialise(geometry,position=[0,0,0],transform=null,mirror=true){
 const vertices=[],v=new THREE.Vector3(),pivot=transform?new THREE.Vector3(-transform.pivot[0],transform.pivot[1],transform.pivot[2]):null;
 for(let i=0;i<geometry.attributes.position.count;i++){
  v.fromBufferAttribute(geometry.attributes.position,i).add(new THREE.Vector3(...position));
  if(pivot)v.sub(pivot).applyAxisAngle(new THREE.Vector3(0,1,0),transform.angleDegrees*Math.PI/180).add(pivot);
  vertices.push(mirror?-v.x:v.x,v.z,v.y);
 }
 const indices=geometry.index?Array.from(geometry.index.array):vertices.map((_,i)=>i).filter(i=>i%3===0).map(i=>i/3);
 if(!mirror)for(let i=0;i<indices.length;i+=3)[indices[i+1],indices[i+2]]=[indices[i+2],indices[i+1]];
 const uv=geometry.attributes.uv?Array.from(geometry.attributes.uv.array):Array(vertices.length/3*2).fill(0),normals=[];
 if(geometry.attributes.normal)for(let i=0;i<geometry.attributes.normal.count;i++){
  v.fromBufferAttribute(geometry.attributes.normal,i);
  if(transform)v.applyAxisAngle(new THREE.Vector3(0,1,0),transform.angleDegrees*Math.PI/180);
  normals.push(mirror?-v.x:v.x,v.z,v.y);
 }
 geometry.dispose();return {vertices,indices,uv,normals};
}
export function buildConstruction(request,descriptor){
 const {kind,aperture,state,reference}=request,p=descriptor.profile,parts=[];
 let recipe;
 if(kind==='garage')recipe=garageGateGeometry(request.cells*descriptor.metric.tileSize-2*p.wallThickness,descriptor.metric.floorHeight,descriptor,state==='open');
 else if(kind==='window'){
  recipe=buildApprovedWindow(aperture,descriptor,state,fracturePreset);
  if(!recipe&&state==='open')recipe=buildApprovedWindow(aperture,descriptor,'closed',fracturePreset);
  if(!recipe)throw Error('Missing approved window recipe: '+aperture.id);
 }else if(kind==='sliding-door')recipe=buildSlidingDoor(aperture,descriptor,state,request.fixedState);
 else if(kind==='door')recipe=buildOpeningInsert(aperture,descriptor,state,reference,fracturePreset);
 else recipe=buildRemainingInsert(aperture,descriptor,kind,state,{mapInserts:request.insert?[reference]:[]});
 if(request.wall&&['window','door'].includes(kind)){
  const wall=buildApertureWall(aperture.points,{...descriptor.metric,floorHeight:descriptor.metric.floorHeight-p.slabDepth},p.wallThickness);
  if(wall.volume>0){const vertices=[];for(let i=0;i<wall.positions.length;i+=3)vertices.push(wall.positions[i],wall.positions[i+2],wall.positions[i+1]);const indices=[...wall.indices];for(let i=0;i<indices.length;i+=3)[indices[i+1],indices[i+2]]=[indices[i+2],indices[i+1]];parts.push({vertices,indices,name:'wall',wallBoxes:wall.boxes,material:{color:'#b8c1c6',roughness:1,transparent:false}});}
 }
 const needsInsert=request.insert||!request.wall||kind==='door'||!['window','door'].includes(kind);
 for(const part of needsInsert?recipe.parts:[]){
  if(!request.insert&&kind==='door'&&part.name.startsWith('leaf.'))continue;
  let geometry,position;
  if(part.cylinder){geometry=new THREE.CylinderGeometry(part.cylinder.radius,part.cylinder.radius,part.cylinder.length,16);geometry.rotateZ(Math.PI/2);position=part.cylinder.center;}
  else({geometry,position}=windowPartGeometry(THREE,part));
  const mesh=serialise(geometry,position,request.sharedParts?null:part.transform);
  if(request.sharedParts&&part.transform)mesh.pose=pose(part.transform);
  if(kind==='garage')for(let i=0;i<mesh.vertices.length;i+=3)mesh.vertices[i]+=p.wallThickness;
  const cubiclePink=/_(58|62|64|65)$/.test(reference?.name||'');
  const nativeFrame=!['door','window','garage','sliding-door'].includes(kind),palette=nativeFrame?nativeFrameColors:colors;
  const material={color:part.material==='cubicle'&&cubiclePink?'#967c7b':palette[part.material]||palette.frame,roughness:['charred','soot','ash'].includes(part.material)?1:part.material==='glass'&&!nativeFrame?.08:.7,alphaTest:0,transparent:false};
  if(part.material==='glass')Object.assign(material,{transparent:true,opacity:nativeFrame?.35:.4,depthWrite:false});
  parts.push({...mesh,name:part.name,material,...(part.name.startsWith('surround.')?{wallBoxes:[[part.bounds[0],part.bounds[2],part.bounds[1],part.bounds[3],part.bounds[5],part.bounds[4]]]}:{})});
 }
 if(kind==='garage'){
  const fullWidth=request.cells*descriptor.metric.tileSize,l=p.wallThickness-p.doorRailWidth/2,r=fullWidth-l;
  for(const [name,bounds] of [['wall.left',[0,0,-p.wallThickness/2,l,recipe.ceiling,p.wallThickness/2]],['wall.right',[r,0,-p.wallThickness/2,fullWidth,recipe.ceiling,p.wallThickness/2]],['wall.header',[l,recipe.clearHeight+p.doorRailWidth,-p.wallThickness/2,r,recipe.ceiling,p.wallThickness/2]]]){
   const {geometry,position}=windowPartGeometry(THREE,{bounds});
   parts.push({...serialise(geometry,position),name,wallBoxes:[[bounds[0],bounds[2],bounds[1],bounds[3],bounds[5],bounds[4]]],material:{color:'#b8c1c6',transparent:false}});
  }
  if((request.interiorSign??1)>0)for(const part of parts)reflect(part,1,0,request.sharedParts);
 }
 for(const [index,item] of (request.attachments||[]).entries()){
  const mount=kind!=='window'&&item.family.kind==='sheet'?doorSheetMount(recipe,p):null;
  const group=createAttachment(THREE,item.family,aperture,p,{...item,doorSheetMount:mount});
  group.updateMatrixWorld(true);
  const transform=mount?recipe.parts.find(part=>part.name.startsWith('leaf.')&&part.transform)?.transform:null;
  const materials=new Set();
  group.traverse(mesh=>{
   if(!mesh.isMesh)return;
   const geometry=mesh.geometry.clone();if(!request.sharedParts)geometry.applyMatrix4(mesh.matrixWorld);
   const data=serialise(geometry,[0,0,0],request.sharedParts?null:transform);
   if(request.sharedParts)data.pose=compose(pose(transform),meshPose(mesh.matrixWorld));
   if(item.family.category==='rope'&&item.depthSign<0){
    reflect(data,1,0,request.sharedParts);
   }
   parts.push({...data,name:`attachment.${index}.${item.family.id}.${mesh.name}`,material:serialiseAttachmentMaterial(mesh.material),attachment:{id:item.family.id,state:item.open?'open':'closed',lod:item.lod||'medium',sources:item.sources}});
   mesh.geometry.dispose();materials.add(mesh.material);
  });
  for(const material of materials){material.map?.dispose();material.normalMap?.dispose();material.dispose();}
 }
 const binding=aperture?.bindings?.[request.side];
 if(kind==='sliding-door')for(const part of parts){
  if(request.mirrorU){
   reflect(part,0,request.cells*descriptor.metric.tileSize,request.sharedParts);
  }
  if(request.interiorSign<0){
   reflect(part,1,0,request.sharedParts);
  }
 }
 if(binding?.quarterTurns>=2)for(const part of parts){
  reflect(part,0,descriptor.metric.tileSize,request.sharedParts);
 }
 return {parts,profileId:aperture?.id||'garage.standard',recipe:recipe.recipe||'garage.sectional/1',state:recipe.visualState||state};
}
