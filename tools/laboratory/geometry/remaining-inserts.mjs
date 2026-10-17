import {buildGate} from './gate-mesh.mjs';
import {buildOpeningInsert} from './door-mesh.mjs';

export function logBoundaryParts(profile){
 const p=profile,top=p.floorHeight-p.slabDepth,step=p.wallThickness,d=step/2,bevel=step*.16;
 const header=p.doorTop-p.windowFrameWidth,start=header-Math.ceil(header/step)*step,parts=[];
 for(let row=0;start+row*step<top-1e-9;row++){
  const z0=start+row*step,z1=z0+step,polygon=[[-d+bevel,z0],[d-bevel,z0],[d,z0+bevel],[d,z1-bevel],[d-bevel,z1],[-d+bevel,z1],[-d,z1-bevel],[-d,z0+bevel]];
  let clipped=polygon;
  for(const [height,above] of [[0,true],[top,false]]){const next=[];clipped.forEach((q,i)=>{const a=clipped[(i+clipped.length-1)%clipped.length],inside=v=>above?v[1]>=height:v[1]<=height;if(inside(a)!==inside(q)){const f=(height-a[1])/(q[1]-a[1]);next.push([a[0]+f*(q[0]-a[0]),height]);}if(inside(q))next.push(q);});clipped=next;}
  if(clipped.length>=3)parts.push({name:'log.'+row,material:'wood',axis:'u',polygon:clipped,depth:[-d,p.tileSize+d]});
 }
 return parts;
}

// Appearance variants consume the accepted door contour without measuring skins.
export function buildRemainingInsert(aperture,descriptor,kind,state='closed',reference=null){
 if(kind==='gate')return buildGate(aperture,descriptor,state);
 if(kind==='standalone-door'){
  const original=reference?.mapInserts?.[0]||reference;
  const source=original?.doorAppearance?.transparent&&!original.doorAppearance.typeId?{...original,doorAppearance:{...original.doorAppearance,typeId:'full-glazed'}}:original;
  return buildOpeningInsert(aperture,descriptor,state,source);
 }
 const p=descriptor.profile,[l,r,b,t]=aperture.bounds;
 const top=descriptor.metric.floorHeight-p.slabDepth;
 if(t>top+1e-7)throw Error('Opening exceeds the clear ceiling');
 const extent=['double-door','tall-passage'].includes(kind)?descriptor.metric.tileSize*2:descriptor.metric.tileSize;
 const surroundings=[];
 const add=(name,material,a,z0,c,z1)=>{if(c>a&&z1>z0)surroundings.push({name,material,bounds:[a,z0,-p.wallThickness/2,c,z1,p.wallThickness/2]});};
 if(kind!=='cubicle'&&kind!=='log'){
  const mat=kind==='damaged'?'charred':'frame';
  add('surround.left',mat,0,b,l,top);add('surround.right',mat,r,b,extent,top);add('surround.header',mat,l,t,r,top);
 }
 if(['open-passage','tall-passage'].includes(kind))return {parts:surroundings,profileId:aperture.id,recipe:'passage/'+kind};
 if(kind==='double-door'){
  const mid=(l+r)/2,parts=[...surroundings],source=reference?.mapInserts?.[0]||reference;
  for(const side of [0,1]){
   const a={...aperture,bounds:side?[mid,r,b,t]:[l,mid,b,t],joins:[side?'left':'right'],construction:{...aperture.construction,hinge:side?'right':'left'}};
   for(const part of buildOpeningInsert(a,descriptor,state,source).parts)parts.push({...part,name:'door.'+side+'.'+part.name});
  }
  return {parts,profileId:aperture.id,recipe:'door.double-glazed/1'};
 }
 const recipe=buildOpeningInsert(aperture,descriptor,state);
 let parts=recipe.parts.filter(part=>kind!=='cubicle'||part.name!=='casing.top');
 if(kind==='cubicle'){
  const post=p.doorStileWidth;
  parts=parts.filter(part=>!part.name.startsWith('leaf.panel.'));
  for(const part of parts)if(part.name==='leaf.core'){part.material='cubicle';part.bounds=[l,b,-p.doorThickness/2,r,t,p.doorThickness/2];}
  for(const part of parts){
   if(part.name.includes('handle.plate')){const c=(part.bounds[1]+part.bounds[4])/2;part.bounds[1]=c-p.doorHandleDiameter;part.bounds[4]=c+p.doorHandleDiameter;}
   if(part.name.includes('handle.lever')){part.bounds[0]=part.bounds[3]-p.doorHandleLength/2-p.doorHandleDiameter;}
   if(part.name==='casing.left')part.bounds=[l-post,b,-post/2,l,t,post/2];
   if(part.name==='casing.right')part.bounds=[r,b,-post/2,r+post,t,post/2];
   if(part.name.startsWith('leaf.')){
    if(part.transform)part.transform.pivot[0]=aperture.construction.hinge==='right'?r:l;
   }
  }
 }else if(kind==='damaged'){
  parts=parts.filter(part=>!part.name.startsWith('leaf.panel.'));
  for(const part of parts)if(part.material==='wood'||part.material==='frame')part.material='charred';
  parts.push(...surroundings);
 }else if(kind==='log'){
  const headerBottom=t-p.windowFrameWidth,step=p.wallThickness,d=step/2,bevel=step*.16;
  const start=headerBottom-Math.ceil((headerBottom-b)/step)*step;
  const clip=(polygon,height,above)=>{
   const result=[];
   polygon.forEach((q,i)=>{
    const a=polygon[(i+polygon.length-1)%polygon.length],inside=v=>above?v[1]>=height:v[1]<=height;
    if(inside(a)!==inside(q)){const f=(height-a[1])/(q[1]-a[1]);result.push([a[0]+f*(q[0]-a[0]),height]);}
    if(inside(q))result.push(q);
   });
   return result;
  };
  for(let row=0;start+row*step<top-1e-9;row++){
   const z0=start+row*step,z1=z0+step,header=z0>=headerBottom-1e-9;
   const section=[[-d+bevel,z0],[d-bevel,z0],[d,z0+bevel],[d,z1-bevel],[d-bevel,z1],[-d+bevel,z1],[-d,z1-bevel],[-d,z0+bevel]];
   const polygon=clip(clip(section,b,true),top,false);
   const spans=header?[[0,descriptor.metric.tileSize]]:[[0,l],[r,descriptor.metric.tileSize]];
   for(const [a,c] of spans)if(c>a)parts.push({name:'log.'+row+'.'+a,material:'wood',axis:'u',polygon,depth:[a,c]});
  }
 }
 return {...recipe,recipe:'door.'+kind+'/1',parts};
}

// The opening is an assembly working envelope, never a per-tile runtime profile.
export function garageGateGeometry(width,height,descriptor,opened=false){
 const p=descriptor.profile,rail=p.doorRailWidth,depth=p.doorThickness;
 const ceiling=height-p.slabDepth,radius=rail*2;
 const clearHeight=ceiling-radius-depth-p.doorLeafClearance,parts=[];
 const trackDepth=-(p.wallThickness/2+depth/2+p.doorLeafClearance);
 const box=(name,material,bounds)=>parts.push({name,material,bounds});
 for(const side of [-1,1]){
  const u=side<0?0:width;
  box('jamb.'+side,'frame',[u-rail/2,0,-p.wallThickness/2,u+rail/2,clearHeight+rail,p.wallThickness/2]);
  box('track.vertical.'+side,'metal',[u-depth/2,0,trackDepth-depth/2,u+depth/2,clearHeight,trackDepth+depth/2]);
  const arc=[];
  for(const [r,reverse] of [[radius+depth/2,false],[radius-depth/2,true]])for(let i=0;i<=12;i++){
   const a=(reverse?12-i:i)*Math.PI/24;
   arc.push([trackDepth-radius+r*Math.cos(a),clearHeight+r*Math.sin(a)]);
  }
  parts.push({name:'track.curve.'+side,material:'metal',axis:'u',polygon:arc,depth:[u-depth/2,u+depth/2]});
  box('track.overhead.'+side,'metal',[u-depth/2,clearHeight+radius-depth/2,trackDepth-radius-clearHeight,u+depth/2,clearHeight+radius+depth/2,trackDepth-radius]);
 }
 box('header','frame',[-rail/2,clearHeight,-p.wallThickness/2,width+rail/2,clearHeight+rail,p.wallThickness/2]);
 const count=Math.ceil(clearHeight/(rail*3)),panelHeight=clearHeight/count;
 for(let i=0;i<count;i++){
  const z0=i*panelHeight,z1=(i+1)*panelHeight;
  if(opened){
   box('panel.'+i,'metal',[p.doorLeafClearance,clearHeight+radius-depth/2,trackDepth-radius-z1,width-p.doorLeafClearance,clearHeight+radius+depth/2,trackDepth-radius-z0]);
  }else{
   box('panel.'+i,'metal',[p.doorLeafClearance,z0,trackDepth-depth/2,width-p.doorLeafClearance,z1,trackDepth+depth/2]);
  }
 }
 return {parts,width,clearHeight,height,ceiling,opened};
}
