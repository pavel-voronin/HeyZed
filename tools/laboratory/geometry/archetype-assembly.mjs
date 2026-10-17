import polygonClipping from 'polygon-clipping';
import {buildOpeningInsert} from './door-mesh.mjs';
import {buildApprovedWindow} from './approved-window-fracture.mjs';
import fracturePreset from './window-fracture-preset.json' with {type:'json'};
import {buildSlidingDoor} from './sliding-door.mjs';
import {buildGate} from './gate-mesh.mjs';
import {buildRemainingInsert,garageGateGeometry,logBoundaryParts} from './remaining-inserts.mjs';
import attachmentCatalog from './attachments/families.json' with {type:'json'};
import {doorSheetMount} from './attachments/attachment-model.mjs';
import {prismSolids,subtractSolids,convexVolume} from './junction-solids.mjs';

const rectangle=(l,r,b,t)=>[[l,b],[l,t],[r,t],[r,b]];
const area=ring=>Math.abs(ring.reduce((sum,p,i)=>{const q=ring[(i+1)%ring.length];return sum+p[0]*q[1]-q[0]*p[1];},0))/2;
const shapeOf=part=>part.polygon?[part.polygon,...(part.holes||[])]:[rectangle(part.bounds[0],part.bounds[3],part.bounds[1],part.bounds[4])];
const depthOf=part=>part.depth||[part.bounds[2],part.bounds[5]];

export function resolveArchetype(registry,kind){
 const archetype=registry.archetypes[kind];if(!archetype?.implementation)throw Error('Unknown archetype: '+kind);
 return {...archetype.implementation,id:'archetype.'+kind,label:archetype.label+' default',archetype:kind,states:archetype.states,slots:archetype.slots,contract:archetype.contract,
         subtraction:archetype.subtraction,status:'archetype-default'};
}
export function resolveImplementation(registry,id,kind){
 return registry.types.some(t=>t.id===id)||id?.startsWith('archetype.')?resolveType(registry,id):resolveArchetype(registry,kind);
}
export function implementationId(registry,id){
 return id?.startsWith('archetype.')?registry?.archetypes[id.slice(10)]?.defaultType||id:id;
}
export function implementationOptions(registry){
 const defaults=Object.entries(registry.archetypes).map(([kind,definition])=>({...resolveArchetype(registry,kind),id:definition.defaultType||'archetype.'+kind}));
 const defaultIds=new Set(defaults.map(type=>type.id));
 return [...defaults,...registry.types.filter(type=>!defaultIds.has(type.id)).map(type=>resolveType(registry,type.id))];
}
export function resolveType(registry,id,parents=[]){
 if(id?.startsWith('archetype.'))return resolveArchetype(registry,id.slice(10));
 if(parents.includes(id))throw Error('Cyclic type inheritance: '+id);
 const entry=registry.types.find(type=>type.id===id);if(!entry)throw Error('Unknown type: '+id);
 const inherited=entry.extends?(registry.types.some(t=>t.id===entry.extends)?resolveType(registry,entry.extends,[...parents,id]):resolveArchetype(registry,entry.archetype)):resolveArchetype(registry,entry.archetype);
 const result={...inherited,...entry},archetype=registry.archetypes[result.archetype];
 result.status=entry.status||(archetype?.defaultType===id?'archetype-default':inherited.status==='archetype-default'?'type':inherited.status);
 if(entry.dimensions&&!entry.subtraction)result.subtraction={dimensions:entry.dimensions,depth:'wall'};
 if(!archetype||inherited.archetype&&entry.archetype&&entry.archetype!==inherited.archetype)throw Error('Invalid archetype inheritance: '+id);
 if(result.stateAliases&&Object.entries(result.stateAliases).some(([from,to])=>!archetype.states.includes(from)||!archetype.states.includes(to)))throw Error('Invalid state alias');
 return {slots:archetype.slots,...result,states:archetype.states,contract:archetype.contract};
}

export function typeContour(type,descriptor){
 const p=descriptor.profile,d=type.dimensions;
 if(d&&typeof d==='object')return d.points.map(point=>[...point]);
 if(d==='window.standard')return rectangle(p.windowLeft,p.windowRight,p.windowBottom,p.windowTop);
 if(d==='window.full-cell')return rectangle(0,p.tileSize,0,p.floorHeight-p.slabDepth);
 if(d==='door.standard')return rectangle(p.doorLeft,p.doorRight,0,p.doorTop);
 if(d==='gate.standard')return rectangle(p.doorLeft,p.doorRight,0,p.lowWallHeight);
 throw Error('Type has no declared subtraction contour: '+type.id);
}

export function subtractionVolume(type,descriptor){
 if(type.contract!=='subtract-and-install')throw Error('Type is not an insertion');
 const polygon=typeContour({...type,dimensions:type.subtraction?.dimensions||type.dimensions},descriptor),depth=type.cutDepth||(Array.isArray(type.subtraction?.depth)?type.subtraction.depth:[-descriptor.profile.wallThickness/2,descriptor.profile.wallThickness/2]);
 if(polygon.length<3||polygon.some(p=>p.length!==2||p.some(n=>!Number.isFinite(n)))||area(polygon)<=0||depth.length!==2||depth.some(n=>!Number.isFinite(n))||depth[0]>=depth[1])throw Error('Invalid subtraction volume');
 return {polygon,depth:[...depth],owner:type.id};
}

export function configuredOpening(registry,descriptor,configuration){
 if(!configuration)return null;
 const kind=configuration.archetype;if(!['door','window'].includes(kind))throw Error('Invalid opening archetype');
 let type=resolveImplementation(registry,configuration.type,kind);
 if(configuration.template){const template=registry.openingTemplates?.find(t=>t.id===configuration.template&&t.archetype===kind);if(!template)throw Error('Unknown opening template');type={...type,dimensions:template.dimensions,subtraction:{dimensions:template.dimensions,depth:'wall'}};}
 return {...subtractionVolume(type,descriptor),owner:'boundary/opening',configuration:{...configuration}};
}

// Split the host's depth before planar subtraction; unrelated depths are intact.
// Multiple cuts are unioned by the polygon operation, including existing voids.
export function subtractVolumes(parts,cuts){
 const result=[];
 for(const part of parts){
  if(part.transform||part.axis)throw Error('Host provider must expose canonical subtraction geometry');
  const [low,high]=depthOf(part),levels=[...new Set([low,high,...cuts.flatMap(c=>c.depth).filter(z=>z>low&&z<high)])].sort((a,b)=>a-b);
  for(let i=0;i<levels.length-1;i++){
   const depth=[levels[i],levels[i+1]],mid=(depth[0]+depth[1])/2,active=cuts.filter(c=>mid>c.depth[0]&&mid<c.depth[1]);
   const polygons=active.length?polygonClipping.difference(shapeOf(part),...active.map(c=>[c.polygon])):[shapeOf(part)];
   for(const polygon of polygons){
    const [outer,...holes]=polygon.map(loop=>loop.length>1&&loop[0][0]===loop.at(-1)[0]&&loop[0][1]===loop.at(-1)[1]?loop.slice(0,-1):loop);
    if(outer.length<3||area(outer)<=1e-12)continue;
    const {bounds,...rest}=part;result.push({...rest,polygon:outer,holes,depth,sourceShape:part.sourceShape||shapeOf(part),sourceDepth:part.sourceDepth||[low,high],sourceUVFrame:part.sourceUVFrame||(bounds?[bounds[0],bounds[3],bounds[1],bounds[4],bounds[2],bounds[5]]:null)});
   }
  }
 }
 return result;
}

export function solidVolume(parts){return parts.reduce((sum,part)=>sum+(part.polyhedron?convexVolume({faces:part.polyhedron}):(area(shapeOf(part)[0])-shapeOf(part).slice(1).reduce((n,ring)=>n+area(ring),0))*(depthOf(part)[1]-depthOf(part)[0])),0);}
function cutBoundary(parts,cuts){
 if(!parts.some(p=>p.axis)&&!cuts.some(c=>c.transform))return subtractVolumes(parts,cuts);
 const edge={axis:'x',x:0,y:0},solids=parts.flatMap(p=>prismSolids(p,edge)),cutters=cuts.flatMap(c=>prismSolids(c,edge));
 return subtractSolids(solids,cutters).map(s=>({...s.part,polyhedron:s.faces}));
}

export function boundaryGeometry(type,descriptor){
 const p=descriptor.profile,d=p.wallThickness/2,w=p.tileSize,h=p.floorHeight-p.slabDepth;
 const box=(name,l,r,b,t,depth=d)=>({name,material:type.material,bounds:[l,b,-depth,r,t,depth],surfaces:{front:'front',back:'back',edge:'edge'}});
 if(type.generator==='logs')return logBoundaryParts(p);
 if(type.generator==='wall')return [box('solid',-d,w+d,0,h)];
 if(type.generator==='fence'){
  const width=p.railingWidth,parts=[];
  for(let i=0;i<=8;i++){const center=i*w/8;parts.push(box('picket.'+i,center-width/2,center+width/2,0,p.lowWallHeight,d/3));}
  for(const z of [p.lowWallHeight*.25,p.lowWallHeight*.8])parts.push({name:'rail.'+z,material:type.material,bounds:[-width/2,z-width/2,-d,w+width/2,z+width/2,-d/3],surfaces:{front:'front',back:'back',edge:'edge'}});
  return parts;
 }
 if(type.generator==='arch'){
  const l=p.doorLeft,r=p.doorRight,s=p.doorTop-(r-l)/2,mid=(l+r)/2,hole=[[l,0],[l,s]];
  for(let i=1;i<=32;i++){const angle=Math.PI-i*Math.PI/32;hole.push([mid+(r-l)/2*Math.cos(angle),s+(r-l)/2*Math.sin(angle)]);}
  hole.push([r,0]);return subtractVolumes([box('solid',-d,w+d,0,h)],[{polygon:hole,depth:[-d,d]}]).map(({sourceShape,sourceDepth,...part})=>part);
 }
 throw Error('Unsupported boundary provider: '+type.generator);
}

export function insertionAperture(type,descriptor){
 const points=typeContour(type,descriptor),bounds=[Math.min(...points.map(p=>p[0])),Math.max(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),Math.max(...points.map(p=>p[1]))];
 return {...type.aperture,id:type.legacyProfile||type.aperture?.id||type.id,confirmed:true,shape:typeof type.dimensions==='object'?type.dimensions.shape:'rectangle',points,bounds,joins:type.aperture?.joins||[],constructionRecipe:type.recipe,reviewRecipe:type.reviewRecipe,cornerReverse:type.cornerReverse};
}
function insertGeometry(type,descriptor,state){
 if(!type.states.includes(state))throw Error('Unsupported visual state: '+state);
 const visualState=type.stateAliases?.[state]||state,aperture=insertionAperture(type,descriptor);
 if(['window','approved-window'].includes(type.generator)){
  const recipe=buildApprovedWindow(aperture,descriptor,visualState,fracturePreset);if(!recipe)throw Error('Window provider did not produce geometry: '+type.id);return recipe.parts;
 }
 if(['door','fixed-window','double-door','cubicle','damaged','log'].includes(type.generator)){
  aperture.construction=aperture.construction||{mechanism:'hinged-door',hinge:'left',openAngleDegrees:90,panelColumns:2,panelRows:2,panelInsetFraction:.16};
  const reference={doorAppearance:{typeId:type.style},properties:type.nativeProperties||{}};
  if(!['door','fixed-window'].includes(type.generator))return buildRemainingInsert(aperture,descriptor,type.generator,visualState,reference).parts.filter(p=>!p.name.startsWith('surround.')&&!p.name.startsWith('log.'));
  return buildOpeningInsert(aperture,descriptor,visualState,reference,fracturePreset).parts;
 }
 if(type.generator==='sliding-door')return buildSlidingDoor(aperture,descriptor,visualState,type.fixedState||'closed').parts;
 if(type.generator==='native-gate'){aperture.construction=aperture.construction||{mechanism:'hinged-gate',gateDesign:type.gateDesign||'wood-low',double:false,hinge:'left',openAngleDegrees:90};return buildGate(aperture,descriptor,visualState).parts;}
 if(type.generator==='garage'){
  const recipe=garageGateGeometry(type.cells*descriptor.profile.tileSize-2*descriptor.profile.wallThickness,descriptor.profile.floorHeight,descriptor,visualState==='open'),offset=descriptor.profile.wallThickness;
  return recipe.parts.map(part=>({...part,...(part.axis==='u'?{depth:part.depth.map(n=>n+offset)}:{bounds:part.bounds.map((n,i)=>i===0||i===3?n+offset:n)})}));
 }
 throw Error('Unsupported insertion provider: '+type.generator);
}

export function attachmentFamilies(registry){return registry.attachments?.length?registry.attachments:attachmentCatalog.families;}

export function migrateAttachmentRequest(registry,request){
 const aliases={curtain:'curtain-0',shutters:'shutter-16',roller:'shade-0'},id=aliases[request.id],entry=id&&attachmentFamilies(registry).find(f=>f.id===id&&f.generator==='accepted-attachment');
 return entry?{...request,id,state:entry.states.length===1?entry.states[0]:request.state||entry.states[0]}:request;
}

export function assembleArchetypes(registry,descriptor,{boundary='boundary.wall',insert='window.standard',insertArchetype='window',opening=null,state='closed',fixedState='closed',mirrorU=false,nativeProperties={},attachments=[],materials={},includeBoundaryGeometry=true}={}){
 const host=boundary==='none'?null:resolveImplementation(registry,boundary,'boundary');if(host&&host.archetype!=='boundary')throw Error('Invalid boundary type');
 const original=host&&includeBoundaryGeometry?boundaryGeometry(host,descriptor):[],type=insert==='none'?null:{...resolveImplementation(registry,insert,insertArchetype),nativeProperties,fixedState};
 const rawCut=type?subtractionVolume(type,descriptor):configuredOpening(registry,descriptor,opening),cutType=type||(opening?.type?resolveImplementation(registry,opening.type,opening.archetype):null),mirrorWidth=mirrorU?(type?.cells||1)*descriptor.profile.tileSize:null;
 const rawCuts=rawCut?[rawCut,...(cutType?.reviewRecipe==='storefront-corner'?[{...rawCut,transform:{pivot:[cutType.cornerReverse?descriptor.profile.tileSize:0,0,0],angleDegrees:cutType.cornerReverse?-90:90}}]:[])]:[];
 const cuts=mirrorU?rawCuts.map(c=>({...c,polygon:c.polygon.map(([u,z])=>[mirrorWidth-u,z]).reverse(),...(c.transform?{transform:{pivot:[mirrorWidth-c.transform.pivot[0],c.transform.pivot[1],c.transform.pivot[2]],angleDegrees:-c.transform.angleDegrees}}:{})})):rawCuts,cut=cuts[0]||null,boundaryParts=cut?cutBoundary(original,cuts):original;
 const insertParts=type?insertGeometry(type,descriptor,state):[];
 const attachmentPlans=[],omitted=[],mounts=new Set();
 for(const request of attachments){
  const attachment=migrateAttachmentRequest(registry,request);
  const entry=attachmentFamilies(registry).find(a=>a.id===attachment.id);
  const slot=entry?.slot||attachment.id;
  const reason=!type?'No receiver':!type.slots.includes(slot)?'Unsupported slot':!entry||!entry.states.includes(attachment.state||entry.states[0])?'Unknown attachment state':null;
  if(reason){omitted.push({id:request.id,reason});continue;}
  const side=entry.category==='rope'?'outside':attachment.side||'inside',mountKey=entry.category+'|'+side;
  if(mounts.has(mountKey)){omitted.push({id:attachment.id,reason:'Occupied mount'});continue;}mounts.add(mountKey);
  const mount=type.archetype==='door'&&entry.kind==='sheet'?doorSheetMount({parts:insertParts},descriptor.profile):null;
  if(type.archetype==='door'&&entry.kind==='sheet'&&!mount){omitted.push({id:attachment.id,reason:'No sheet mount'});continue;}
  attachmentPlans.push({family:entry,aperture:insertionAperture(type,descriptor),options:{...attachment,open:attachment.state==='open',side,doorSheetMount:mount},transform:mount?insertParts.find(p=>p.name.startsWith('leaf.')&&p.transform)?.transform:null,address:'insert/attachment/'+entry.id+'/'+side});
 }
 const addressed=(parts,owner)=>parts.map(part=>({...part,address:owner+'/'+part.name,material:materials[owner]||part.material}));
 return {schema:1,contract:'subtract-and-install',status:'laboratory',boundary:host?.id||null,insert:type?.id||null,state,representationState:type?.stateAliases?.[state]||state,
         cut,cuts,mirrorWidth,cutOwner:type?'insertion':cut?'boundary-opening':null,opening,cutCover:host?.cutCover||resolveArchetype(registry,'boundary').cutCover,attachmentPlans,
         parts:[...addressed(boundaryParts,'boundary'),...addressed(insertParts,'insert')],
         boundaryParts,boundaryVolume:solidVolume(boundaryParts),removedVolume:solidVolume(original)-solidVolume(boundaryParts),
         slots:type?.slots||[],omitted,implementationAddress:type?'type/'+type.id+'/implementation':host?'type/'+host.id+'/implementation':null};
}
