// Offline preparation only. Unreal consumes native mesh assets and construction facts.
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {dirname,join} from 'node:path';
import {createHash} from 'node:crypto';
import * as THREE from '../crown-lab/public/vendor/three.module.js';
import {assembleArchetypes,resolveType,boundaryGeometry,subtractionVolume,insertionAperture} from '../laboratory/geometry/archetype-assembly.mjs';
import {prismSolids} from '../laboratory/geometry/junction-solids.mjs';
import {windowPartGeometry} from '../laboratory/geometry/solid-parts.mjs';
import {createAttachment,doorSheetMount} from '../laboratory/geometry/attachments/attachment-model.mjs';

const input=JSON.parse(readFileSync(process.argv[2],'utf8'));
const destination=process.argv[3];
mkdirSync(join(destination,'meshes'),{recursive:true});
const {registry,geometry:descriptor}=input;
const inventoryPath=join(dirname(process.argv[2]),'resolved','inventory.json');
if(existsSync(inventoryPath)){
 const used=JSON.parse(readFileSync(inventoryPath,'utf8')).typeUsage,base=registry.types.find(t=>t.id==='door.garage.3');
 for(const id of Object.keys(used).filter(id=>id.startsWith('door.garage.')&&!registry.types.some(t=>t.id===id))){
  const cells=Number(id.split('.').at(-1)),dimensions=structuredClone(base.dimensions),right=Math.max(...dimensions.points.map(p=>p[0]));
  for(const point of dimensions.points)if(Math.abs(point[0]-right)<1e-8)point[0]+=(cells-3)*descriptor.profile.tileSize;
  registry.types.push({...base,id,cells,dimensions,subtraction:{dimensions,depth:'wall'},status:'provisional-parametric-native-assembly',modelStatus:'provisional',basis:'Observed contiguous native GarageDoor cells; canonical cross-section, variable span'});
 }
}
const definitions={},meshes=new Map(),recipes=new Map();
const native=p=>[p[0]*100,p[2]*100,p[1]*100];
const convex=part=>prismSolids(part,{axis:'x',x:0,y:0},descriptor.profile.tileSize).map(s=>s.faces.map(face=>face.map(native).reverse()));

function rawPart(part,extra={}){
 const {geometry,position}=windowPartGeometry(THREE,part);
 const vertices=[],normals=[],uv=[];
 const pivot=part.transform?.pivot||null;
 const attribute=geometry.attributes.position,n=geometry.attributes.normal,t=geometry.attributes.uv;
 for(let i=0;i<attribute.count;i++){
  const x=-attribute.getX(i)-position[0],z=attribute.getY(i)+position[1],d=attribute.getZ(i)+position[2];
  vertices.push((x-(pivot?.[0]||0))*100,(d-(pivot?.[2]||0))*100,(z-(pivot?.[1]||0))*100);
  if(n)normals.push(-n.getX(i),n.getZ(i),n.getY(i));
  if(n&&(part.bounds||part.polyhedron)){
   const nx=-n.getX(i),ny=n.getZ(i),nz=n.getY(i);
   uv.push(Math.abs(nz)>.5?x:Math.abs(ny)>.5?x:d,Math.abs(nz)>.5?d:z);
  }else uv.push(t?t.getX(i):0,t?t.getY(i):0);
 }
 const indices=geometry.index?Array.from(geometry.index.array):Array.from({length:attribute.count},(_,i)=>i);
 geometry.dispose();
 const recipe={vertices,indices,normals,uv,...(part.material==='glass'?{glass:true}:{})};
 const text=JSON.stringify(recipe),key=createHash('sha256').update(text).digest('hex');
 recipes.set(key,recipe);
 return {mesh:key,name:part.address||part.name,material:part.material||'frame',position:pivot?native(pivot):[0,0,0],
         ...(part.transform?{motion:{pivot:native(pivot),angle:part.transform.angleDegrees||0}}:{}),...extra};
}

function combine(parts){
 const groups=new Map();
 for(const part of parts){const key=JSON.stringify([part.material,part.position,part.motion,part.color]);if(!groups.has(key))groups.set(key,{...part,recipe:{vertices:[],indices:[],normals:[],uv:[]}});const group=groups.get(key),source=recipes.get(part.mesh),offset=group.recipe.vertices.length/3;for(const field of ['vertices','normals','uv'])group.recipe[field].push(...source[field]);group.recipe.indices.push(...source.indices.map(i=>i+offset));if(source.glass)group.recipe.glass=true;if(source.cloth)group.recipe.cloth=true;}
 return [...groups.values()].map(({recipe,...part})=>{const text=JSON.stringify(recipe),key=createHash('sha256').update(text).digest('hex');recipes.set(key,recipe);return {...part,mesh:key};});
}

for(const entry of registry.types){
 const type=resolveType(registry,entry.id);
 if(type.archetype==='boundary'){
  definitions[entry.id]={kind:'boundary',rank:type.rank||1,joinOperations:type.joinOperations||['middle','diagonal','abut'],
                         solids:boundaryGeometry(type,descriptor).flatMap(convex),material:type.material,status:type.status};
  continue;
 }
 const cut=subtractionVolume(type,descriptor);
 const closed=assembleArchetypes(registry,descriptor,{boundary:'none',insert:type.id,state:'closed'});
 const record={kind:type.archetype,cells:type.cells||1,cut:closed.cuts.flatMap(convex),bounds:[Math.min(...cut.polygon.map(p=>p[0]))*100,Math.max(...cut.polygon.map(p=>p[0]))*100,Math.min(...cut.polygon.map(p=>p[1]))*100,Math.max(...cut.polygon.map(p=>p[1]))*100],
               slots:type.slots,status:type.status,modelStatus:type.modelStatus,states:{}};
 const open=type.states.includes('open')?assembleArchetypes(registry,descriptor,{boundary:'none',insert:type.id,state:'open'}):null;
 for(const state of type.states){
  const model=assembleArchetypes(registry,descriptor,{boundary:'none',insert:type.id,state});
  record.states[state]=combine(model.parts.filter(p=>!p.address.startsWith('boundary/')).map(part=>{
   const counterpart=state==='closed'&&type.archetype==='door'?open?.parts.find(p=>p.address===part.address):null;
   return rawPart(counterpart?.transform?{...part,transform:{...counterpart.transform,angleDegrees:0}}:part);
  }));
 }
 definitions[entry.id]=record;
}
for(const template of registry.openingTemplates||[]){
 definitions[template.id]={kind:template.archetype,cut:convex({polygon:template.dimensions.points,depth:[-descriptor.profile.wallThickness/2,descriptor.profile.wallThickness/2]}),states:{},status:template.status};
}

function shell(recipe){
 const {vertices:v,indices,normals:n,uv}=recipe,count=v.length/3,edges=new Map();
 const point=i=>v.slice(i*3,i*3+3).map(x=>Math.round(x*1e5)).join(',');
 for(let i=0;i<indices.length;i+=3)for(const [a,b] of [[indices[i],indices[i+1]],[indices[i+1],indices[i+2]],[indices[i+2],indices[i]]]){const pa=point(a),pb=point(b),key=pa<pb?pa+'|'+pb:pb+'|'+pa;const edge=edges.get(key);if(edge)edge.count++;else edges.set(key,{a,b,count:1});}
 const boundary=[...edges.values()].filter(e=>e.count===1);if(!boundary.length)return recipe;
 const front=indices.slice();
 for(let i=0;i<count;i++){v.push(...v.slice(i*3,i*3+3).map((p,k)=>p-n[i*3+k]*.1));n.push(...n.slice(i*3,i*3+3).map(p=>-p));uv.push(...uv.slice(i*2,i*2+2));}
 for(let i=0;i<front.length;i+=3)indices.push(front[i]+count,front[i+2]+count,front[i+1]+count);
 for(const {a,b} of boundary){const A=v.slice(a*3,a*3+3),B=v.slice(b*3,b*3+3),D=v.slice((a+count)*3,(a+count)*3+3),C=v.slice((b+count)*3,(b+count)*3+3);const normal=new THREE.Vector3().crossVectors(new THREE.Vector3(...D).sub(new THREE.Vector3(...A)),new THREE.Vector3(...C).sub(new THREE.Vector3(...A))).normalize().toArray();for(const p of [A,D,C,A,C,B]){indices.push(v.length/3);v.push(...p);n.push(...normal);uv.push(0,0);}}
 return recipe;
}

function attachment(family,aperture,record,options={}){
 if(family.category==='covering')record.authoredNormal=(options.side||'inside')==='outside'?1:-1;
 for(const state of ['closed','open']){
  const group=createAttachment(THREE,family,aperture,descriptor.profile,{...options,state,open:state==='open',side:options.side||'inside',depthSign:options.depthSign??-1,lod:'medium'});
  group.updateMatrixWorld(true);const parts=[];const materials=new Set();
  group.traverse(mesh=>{
   if(!mesh.isMesh)return;
   const transformed=mesh.geometry.clone().applyMatrix4(mesh.matrixWorld);
   const position=transformed.attributes.position,normal=transformed.attributes.normal,texture=transformed.attributes.uv;
   const v=[],n=[],uv=[];
   const pivot=options.pivot||[0,0,0];
   for(let i=0;i<position.count;i++){v.push(-position.getX(i)*100-pivot[0],position.getZ(i)*100-pivot[1],position.getY(i)*100-pivot[2]);if(normal)n.push(-normal.getX(i),normal.getZ(i),normal.getY(i));uv.push(texture?texture.getX(i):0,texture?texture.getY(i):0);}
   const indices=transformed.index?Array.from(transformed.index.array):Array.from({length:position.count},(_,i)=>i);
   transformed.dispose();
   const recipe={vertices:v,normals:n,indices,uv,cloth:mesh.name==='cloth'||family.category==='rope'};
   if(recipe.cloth)shell(recipe);
   const text=JSON.stringify(recipe),key=createHash('sha256').update(text).digest('hex');recipes.set(key,recipe);
   let material=recipe.cloth?'fabric':mesh.material.metalness>.4?'metal':'wood';
   if(recipe.cloth){const tones={Light:'#cececa',Cream:'#c9bd9e',Blue:'#849eaf',Green:'#849876',Rose:'#b38e91',Dark:'#575957'},color=mesh.material.color;const tone=Object.entries(tones).sort(([,a],[,b])=>{const A=new THREE.Color(a),B=new THREE.Color(b);return (A.r-color.r)**2+(A.g-color.g)**2+(A.b-color.b)**2-((B.r-color.r)**2+(B.g-color.g)**2+(B.b-color.b)**2);})[0][0];material='Fabric_'+tone;}
   parts.push({mesh:key,name:mesh.name,position:pivot,material,color:mesh.material.color?.toArray()||[1,1,1],...(options.pivot?{motion:{pivot,angle:0}}:{})});
   mesh.geometry.dispose();materials.add(mesh.material);
  });
  for(const material of materials){material.map?.dispose();material.normalMap?.dispose();material.dispose();}
  record.states[state]=combine(parts);
 }
 return record;
}

// Preserve the authored static cloth/rope/shutter construction. No cloth simulation runs.
const defaultWindow=resolveType(registry,'type.window-9-0');
const windowDefinition=definitions[defaultWindow.id];
const aperture={...defaultWindow.aperture,id:defaultWindow.legacyProfile,points:defaultWindow.dimensions.points,
                bounds:windowDefinition.bounds.map(v=>v/100)};
for(const family of registry.attachments){
 const record={family:family.id,category:family.category,kind:family.kind,slot:family.slot,referenceBounds:windowDefinition.bounds,states:{}};
 definitions['attachment.'+family.id]=attachment(family,aperture,record);
}
const variantPath=join(dirname(process.argv[2]),'attachment-variants.json');
if(existsSync(variantPath))for(const [id,spec] of Object.entries(JSON.parse(readFileSync(variantPath,'utf8')))){
 const type=resolveType(registry,spec.type),definition=definitions[spec.type],family=registry.attachments.find(f=>f.id===spec.family);
 const opening=insertionAperture(type,descriptor);
 const options={...spec.options};
 if(type.archetype==='door'&&family.kind==='sheet'){
  const model=assembleArchetypes(registry,descriptor,{boundary:'none',insert:type.id,state:'closed'});options.doorSheetMount=doorSheetMount(model,descriptor.profile);
  const open=assembleArchetypes(registry,descriptor,{boundary:'none',insert:type.id,state:'open'});const moving=open.parts.find(p=>p.name.startsWith('leaf.')&&p.transform);
  if(moving)options.pivot=native(moving.transform.pivot);
 }
 definitions['attachment.variant.'+id]=attachment(family,opening,{family:family.id,kind:family.kind,category:family.category,slot:family.slot,referenceBounds:definition.bounds,nativeFit:true,states:{}},options);
}
for(const definition of Object.values(definitions))for(const parts of Object.values(definition.states||{}))for(const part of parts){
 const source=recipes.get(part.mesh);part.mirrors=[part.mesh];
 // Reflections are authored geometry, so translucent fallback meshes and
 // Nanite use the same front-face winding without negative instance scales.
 for(let mask=1;mask<4;mask++){
  const sx=mask&1?-1:1,sy=mask&2?-1:1;
  const reflected={...source,vertices:source.vertices.map((v,i)=>v*(i%3===0?sx:i%3===1?sy:1)),normals:source.normals.map((v,i)=>v*(i%3===0?sx:i%3===1?sy:1)),indices:source.indices.map((v,i)=>sx*sy<0?source.indices[i-i%3+(i%3===1?2:i%3===2?1:0)]:v)};
  const key=createHash('sha256').update(JSON.stringify(reflected)).digest('hex');recipes.set(key,reflected);part.mirrors.push(key);
 }
 for(const key of part.mirrors){const recipe=recipes.get(key);if(meshes.has(key))continue;writeFileSync(join(destination,'meshes',key+'.json'),JSON.stringify(recipe));meshes.set(key,{file:'meshes/'+key+'.json',vertices:recipe.vertices.length/3,triangles:recipe.indices.length/3,glass:!!recipe.glass,cloth:!!recipe.cloth});}
}
writeFileSync(join(destination,'library.json'),JSON.stringify({schema:1,metric:descriptor.profile,definitions,meshes:Object.fromEntries(meshes)}));
console.log(JSON.stringify({definitions:Object.keys(definitions).length,meshes:meshes.size}));
