import {assembleArchetypes,boundaryGeometry,resolveImplementation} from './archetype-assembly.mjs';
import {edgeKey,placementPoint,localPoint,prismSolids,clipSolid,subtractSolids,convexVolume} from './junction-solids.mjs';

export function solveJunctions(instances,size){
 const nodes=new Map(),pieces=instances.map(instance=>({...instance,solids:instance.solids.slice()}));
 for(const piece of pieces){
  if(!piece.solids.length)continue;
  const a=placementPoint(piece.edge,[0,0,0],size),b=placementPoint(piece.edge,[size,0,0],size);
  for(const [point,other] of [[a,b],[b,a]]){const key=point[0]+':'+point[2];if(!nodes.has(key))nodes.set(key,[]);nodes.get(key).push({piece,point,d:[(other[0]-point[0])/size,0,(other[2]-point[2])/size]});}
 }
 const joints=[];
 for(const [node,participants] of [...nodes].sort(([a],[b])=>a.localeCompare(b))){
  if(participants.length<2)continue;
  const occupied=[],ordered=participants.slice().sort((a,b)=>(a.d[0]!==0?0:1)-(b.d[0]!==0?0:1)||edgeKey(a.piece.edge).localeCompare(edgeKey(b.piece.edge)));
  const permits=(item,operation)=>(item.piece.joinOperations||['middle','diagonal','abut']).includes(operation);
  function abut(item){if(!permits(item,'abut'))throw Error('Type forbids abutment: '+item.piece.type);item.piece.solids=subtractSolids(item.piece.solids,occupied.flatMap(i=>i.piece.solids));occupied.push(item);joints.push({node,edge:edgeKey(item.piece.edge),operation:'abut'});}
  function plane(item,n,operation){const c=n.reduce((s,v,i)=>s+v*item.point[i],0);item.piece.solids=item.piece.solids.map(s=>clipSolid(s,n.map(v=>-v),-c)).filter(Boolean);joints.push({node,edge:edgeKey(item.piece.edge),operation});}
  function assemble(group){
   if(occupied.length){group.forEach(abut);return;}
   let pair;
   for(const axis of [0,2]){const a=group.find(i=>i.d[axis]===1),b=group.find(i=>i.d[axis]===-1);if(a&&b){pair=[a,b];break;}}
   if(pair&&pair.every(i=>permits(i,'middle'))){pair.forEach(i=>plane(i,i.d,'middle'));occupied.push(...pair);group.filter(i=>!pair.includes(i)).forEach(abut);}
   else if(!pair&&group.length===2&&group.every(i=>permits(i,'diagonal'))){const [a,b]=group,n=a.d.map((v,i)=>v-b.d[i]);plane(a,n,'diagonal');plane(b,n.map(v=>-v),'diagonal');occupied.push(a,b);}
   else group.forEach(i=>occupied.length?abut(i):occupied.push(i));
  }
  for(const rank of [1,2]){
   const ranked=ordered.filter(i=>i.piece.rank===rank),groups=new Map();
   for(const item of ranked){if(!groups.has(item.piece.type))groups.set(item.piece.type,[]);groups.get(item.piece.type).push(item);}
   const same=[...groups.values()].filter(g=>g.length>1).sort((a,b)=>a[0].piece.type.localeCompare(b[0].piece.type));
   same.forEach(assemble);assemble(ranked.filter(i=>!same.some(g=>g.includes(i))));
  }
 }
 return {pieces,joints};
}

// Join the original hosts first. Every intersecting part in a connected construction
// then receives every current opening, before the visible inserts are installed.
export function assemblePlatform(registry,descriptor,placements){
 const size=descriptor.profile.tileSize,keys=new Set(),assembled=placements.map(instance=>{
  const key=edgeKey(instance.edge);if(keys.has(key))throw Error('Duplicate shared edge: '+key);keys.add(key);
  const host=instance.boundary==='none'?null:resolveImplementation(registry,instance.boundary||'boundary.wall','boundary');
  const assembly=assembleArchetypes(registry,descriptor,{...instance,includeBoundaryGeometry:false});
  if(host&&![1,2].includes(host.rank||1))throw Error('Unsupported junction rank');
  return {...instance,key,type:host?.id,rank:host?.rank||1,joinOperations:host?.joinOperations,assembly,solids:host?boundaryGeometry(host,descriptor).flatMap(part=>prismSolids(part,instance.edge,size)):[]};
 });
 const {pieces,joints}=solveJunctions(assembled,size),connections=new Map(pieces.map(p=>[p.key,new Set([p.key])]));
 const endpoints=p=>[placementPoint(p.edge,[0,0,0],size),placementPoint(p.edge,[size,0,0],size)].map(v=>v[0]+':'+v[2]);
 for(const a of pieces)for(const b of pieces)if(endpoints(a).some(p=>endpoints(b).includes(p))){connections.get(a.key).add(b.key);}
 function component(key){const found=new Set([key]),pending=[key];while(pending.length){for(const neighbor of connections.get(pending.pop()))if(!found.has(neighbor)){found.add(neighbor);pending.push(neighbor);}}return found;}
 const parts=[],boundaries=[],attachmentPlans=[];
 for(const piece of pieces){
  const connected=component(piece.key),cuts=pieces.filter(p=>connected.has(p.key)).flatMap(p=>p.assembly.cuts.flatMap(cut=>prismSolids({...cut,name:'opening'},p.edge,size)));
  const joinedVolume=piece.solids.reduce((s,p)=>s+convexVolume(p),0),solids=subtractSolids(piece.solids,cuts);
  const boundaryParts=solids.map((solid,i)=>({...solid.part,polyhedron:solid.faces.map(face=>face.map(p=>localPoint(piece.edge,p,size))),address:'boundary/'+solid.part.name+'/'+i,placement:piece.edge,owner:piece.key,cutCover:piece.assembly.cutCover,appearance:piece.appearance}));
  boundaries.push({edge:piece.key,joinedVolume,volume:solids.reduce((s,p)=>s+convexVolume(p),0)});
  parts.push(...boundaryParts,...piece.assembly.parts.filter(p=>!p.address.startsWith('boundary/')).map(part=>({...part,placement:piece.edge,owner:piece.key,appearance:piece.appearance,mirrorWidth:piece.assembly.mirrorWidth})));
  attachmentPlans.push(...piece.assembly.attachmentPlans.map(plan=>({...plan,placement:piece.edge,owner:piece.key,mirrorWidth:piece.assembly.mirrorWidth})));
 }
 return {parts,attachmentPlans,joints,boundaries,instances:pieces.length,cutCover:null,omitted:pieces.flatMap(p=>p.assembly.omitted)};
}
