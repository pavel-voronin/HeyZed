import {ShapeUtils,Vector2} from '../../crown-lab/public/vendor/three.module.js';

const EPS=1e-9;
const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0);
const sub=(a,b)=>a.map((v,i)=>v-b[i]);
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const mean=points=>[0,1,2].map(i=>points.reduce((s,p)=>s+p[i],0)/points.length);
const unit=v=>v.map(n=>n/Math.hypot(...v));
const same=(a,b)=>Math.hypot(...sub(a,b))<EPS;
const clean=points=>points.filter((p,i)=>!same(p,points[(i+points.length-1)%points.length]));
function normal(face){for(let i=1;i<face.length-1;i++){const n=cross(sub(face[i],face[0]),sub(face[i+1],face[0]));if(Math.hypot(...n)>EPS)return unit(n);}return null;}
function oriented(faces){const center=mean(faces.flat());return faces.map(face=>dot(normal(face),sub(mean(face),center))<0?[...face].reverse():face);}
export function convexVolume(solid){return Math.abs(solid.faces.reduce((sum,face)=>sum+face.slice(1,-1).reduce((s,p,i)=>s+dot(face[0],cross(p,face[i+2]))/6,0),0));}
export function solidPlanes(solid){return solid.faces.map(face=>{const n=normal(face);return {n,c:dot(n,face[0])};});}

// Convex clipping produces a closed cap, preserving the owner's source metadata.
export function clipSolid(solid,n,c){
 const values=solid.faces.flat().map(p=>dot(n,p)-c);
 if(values.every(v=>v<=EPS))return solid;
 if(values.every(v=>v>=-EPS))return null;
 const faces=[],cap=[];
 for(const face of solid.faces){
  const loop=[];
  for(let i=0;i<face.length;i++){
   const a=face[i],b=face[(i+1)%face.length],da=dot(n,a)-c,db=dot(n,b)-c;
   if(da<=EPS)loop.push(a);
   if((da< -EPS&&db>EPS)||(da>EPS&&db< -EPS)){const t=da/(da-db),p=a.map((v,k)=>v+t*(b[k]-v));loop.push(p);cap.push(p);}
   if(Math.abs(da)<=EPS)cap.push(a);
  }
  const result=clean(loop);if(result.length>=3&&normal(result))faces.push(result);
 }
 const unique=cap.filter((p,i)=>!cap.slice(0,i).some(q=>same(p,q)));
 if(unique.length>=3){
  const center=mean(unique),axis=Math.abs(n[0])<.8?[1,0,0]:[0,1,0],u=unit(cross(axis,n)),v=cross(n,u);
  unique.sort((a,b)=>Math.atan2(dot(sub(a,center),v),dot(sub(a,center),u))-Math.atan2(dot(sub(b,center),v),dot(sub(b,center),u)));
  faces.push(unique);
 }
 const result={...solid,faces};return faces.length>=4&&convexVolume(result)>1e-12?result:null;
}
function separated(a,b){return [0,1,2].some(i=>Math.max(...a.faces.flat().map(p=>p[i]))<=Math.min(...b.faces.flat().map(p=>p[i]))+EPS||Math.max(...b.faces.flat().map(p=>p[i]))<=Math.min(...a.faces.flat().map(p=>p[i]))+EPS);}
export function intersectSolid(a,b){if(separated(a,b))return null;let rest=a;for(const {n,c} of solidPlanes(b)){rest=clipSolid(rest,n,c);if(!rest)return null;}return rest;}
export function subtractSolid(a,b){
 if(!intersectSolid(a,b))return [a];
 const result=[];let rest=a;
 for(const {n,c} of solidPlanes(b)){
  const outside=clipSolid(rest,n.map(v=>-v),-c);if(outside)result.push(outside);
  rest=clipSolid(rest,n,c);if(!rest)break;
 }
 return result;
}
export function subtractSolids(parts,cutters){let result=parts;for(const cutter of cutters)result=result.flatMap(part=>subtractSolid(part,cutter));return result;}

export function placementPoint(edge,point,size=1){const [u,z,d]=point;return edge.axis==='x'?[edge.x*size+u,z,edge.y*size+d]:[edge.x*size-d,z,edge.y*size+u];}
export function localPoint(edge,point,size=1){const [x,z,y]=point;return edge.axis==='x'?[x-edge.x*size,z,y-edge.y*size]:[y-edge.y*size,z,edge.x*size-x];}
export const edgeKey=edge=>`${edge.axis}:${edge.x}:${edge.y}`;
export function cellEdge(x,y,side){if(side==='north')return {axis:'x',x,y};if(side==='south')return {axis:'x',x,y:y+1};if(side==='west')return {axis:'y',x,y};if(side==='east')return {axis:'y',x:x+1,y};throw Error('Invalid cell side');}

export function prismSolids(part,edge,size=1){
 const bounds=part.bounds,shape=part.polygon||[[bounds[0],bounds[1]],[bounds[0],bounds[4]],[bounds[3],bounds[4]],[bounds[3],bounds[1]]],holes=part.holes||[],depth=part.depth||[bounds[2],bounds[5]],loops=[shape,...holes];
 const vectors=loops.map(loop=>loop.map(p=>new Vector2(...p))),vertices=loops.flat();
 const turns=shape.map((p,i)=>{const a=shape[(i+1)%shape.length],b=shape[(i+2)%shape.length];return (a[0]-p[0])*(b[1]-a[1])-(a[1]-p[1])*(b[0]-a[0]);}),convex=!holes.length&&(turns.every(t=>t<=EPS)||turns.every(t=>t>=-EPS));
 const polygons=convex?[shape]:ShapeUtils.triangulateShape(vectors[0],vectors.slice(1)).map(indices=>indices.map(i=>vertices[i]));
 return polygons.map(polygon=>{
  function world(point){if(part.axis==='u')point=[point[2],point[1],point[0]];if(part.transform){const [u,z,d]=point,[pu,pz,pd]=part.transform.pivot,a=part.transform.angleDegrees*Math.PI/180,c=Math.cos(a),s=Math.sin(a);point=[pu+c*(u-pu)-s*(d-pd),z,pd+s*(u-pu)+c*(d-pd)];}return placementPoint(edge,point,size);}
  const low=polygon.map(([u,z])=>world([u,z,depth[0]])),high=polygon.map(([u,z])=>world([u,z,depth[1]]));
  const faces=[low,high,...polygon.map((_,i)=>[low[i],low[(i+1)%low.length],high[(i+1)%low.length],high[i]])];
  const orientedFaces=oriented(faces),sourcePlanes=part.axis==='u'?solidPlanes({faces:oriented(orientedFaces.map(face=>face.map(p=>localPoint(edge,p,size))))}):null;
  const {axis,...rest}=part;
  return {part:{...rest,sourcePlanes,sourceShape:part.sourceShape||loops,sourceDepth:part.sourceDepth||depth,sourceUVFrame:part.sourceUVFrame||(bounds?[bounds[0],bounds[3],bounds[1],bounds[4],bounds[2],bounds[5]]:null)},faces:orientedFaces};
 });
}
