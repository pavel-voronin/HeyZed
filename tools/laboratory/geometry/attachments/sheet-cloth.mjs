import bake from './sheet-bake.json' with {type:'json'};
export const sheetLevels=['high','medium','low'];
const edgeTopology=new Map();
function transform(spec,p){
 const width=spec.right-spec.left,scale=(spec.top-spec.bottom)/bake.states.closed.height;
 const state=bake.states[spec.open?'open':'closed'];
 const edgeScale=spec.edgeOverhangScale??1,u=p[0]<0?p[0]*edgeScale:p[0]>1?1+(p[0]-1)*edgeScale:p[0];
 return [-(spec.left+width*u),Math.min(spec.top,spec.top-scale*state.height*p[1]),spec.depth+spec.side*width*p[2]];
}
export function sheetAnchors(THREE,spec,geometry){
 const p=geometry.attributes.position,indices=geometry.index.array;
 return bake.states[spec.open?'open':'closed'].pins.map(pin=>{
  const target=new THREE.Vector3(...transform(spec,pin)),triangle=new THREE.Triangle(),candidate=new THREE.Vector3();let nearest=Infinity,point=target.clone();
  for(let i=0;i<indices.length;i+=3){triangle.a.fromBufferAttribute(p,indices[i]);triangle.b.fromBufferAttribute(p,indices[i+1]);triangle.c.fromBufferAttribute(p,indices[i+2]);triangle.closestPointToPoint(target,candidate);const d=candidate.distanceToSquared(target);if(d<nearest){nearest=d;point.copy(candidate);}}
  return point.toArray();
 });
}
export function sheetGeometry(THREE,spec){
 const source=bake.states[spec.open?'open':'closed'].levels[spec.lod||'medium'],count=source.positions.length/3;
 const positions=[];for(let i=0;i<count;i++){const p=transform(spec,source.positions.slice(i*3,i*3+3));if(!spec.open&&source.positions[i*3+1]>.7){const hem=Math.max(0,1-source.uvs[i*2+1]/.15);p[1]+=(spec.bottom-p[1])*hem*hem;}positions.push(...p);}
 if(spec.coverPerimeter&&!spec.open){
  const uvs=[...source.uvs],indices=[...source.indices],edges=new Map();
  for(let i=0;i<source.indices.length;i+=3)for(const [a,b] of [[0,1],[1,2],[2,0]]){const x=source.indices[i+a],y=source.indices[i+b],key=Math.min(x,y)+':'+Math.max(x,y),entry=edges.get(key);if(entry)entry.count++;else edges.set(key,{a:x,b:y,count:1});}
  const boundary=[...edges.values()].filter(e=>e.count===1),ids=[...new Set(boundary.flatMap(e=>[e.a,e.b]))],corners=new Map();
  for(const [u,v] of [[0,0],[1,0],[0,1],[1,1]]){let nearest=Infinity,id;for(const i of ids){const d=(source.uvs[i*2]-u)**2+(source.uvs[i*2+1]-v)**2;if(d<nearest){nearest=d;id=i;}}corners.set(id,[u,v]);}
  const xs=positions.filter((_,i)=>i%3===0),epsilon=(spec.right-spec.left)*.001,left=Math.max(-spec.left,Math.max(...xs))+epsilon,right=Math.min(-spec.right,Math.min(...xs))-epsilon,outer=new Map();
  for(const i of ids){
   const u=Math.max(0,Math.min(1,source.uvs[i*2])),v=Math.max(0,Math.min(1,source.uvs[i*2+1])),distances=[u,1-u,v,1-v],edge=distances.indexOf(Math.min(...distances));
   let x=Math.max(right,Math.min(left,positions[i*3])),y=Math.max(spec.bottom,Math.min(spec.top,positions[i*3+1]));
   if(edge===0)x=left;if(edge===1)x=right;if(edge===2)y=spec.bottom;if(edge===3)y=spec.top;
   if(corners.has(i)){const [u,v]=corners.get(i);x=u?right:left;y=v?spec.top:spec.bottom;}
   outer.set(i,positions.length/3);positions.push(x,y,positions[i*3+2]);uvs.push(u,v);
  }
  const key=(spec.lod||'medium')+'|'+(spec.edgeOverhangScale??1);
  if(edgeTopology.has(key))indices.push(...edgeTopology.get(key));else{
   const triangles=[];
   function triangle(ids){const vertices=ids.map(i=>new THREE.Vector3(...positions.slice(i*3,i*3+3)));return {vertices,box:new THREE.Box3().setFromPoints(vertices)};}
   for(let i=0;i<source.indices.length;i+=3)triangles.push(triangle(source.indices.slice(i,i+3)));
   function crosses(a,b){
    if(!a.box.intersectsBox(b.box)||a.vertices.some(x=>b.vertices.some(y=>x.distanceToSquared(y)<1e-12)))return false;
    for(const [source,target] of [[a,b],[b,a]])for(let i=0;i<3;i++){const start=source.vertices[i],end=source.vertices[(i+1)%3],direction=end.clone().sub(start),length=direction.length();if(length<1e-9)continue;const hit=new THREE.Ray(start,direction.divideScalar(length)).intersectTriangle(...target.vertices,false,new THREE.Vector3());if(hit&&hit.distanceTo(start)>1e-7&&hit.distanceTo(start)<length-1e-7)return true;}return false;
   }
   const added=[];for(const {a,b} of boundary){const c=outer.get(a),d=outer.get(b);for(const ids of [[b,a,c],[b,c,d]]){const face=triangle(ids);if(triangles.some(t=>crosses(face,t)))continue;added.push(...ids);triangles.push(face);}}
   edgeTopology.set(key,added);indices.push(...added);
  }
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));geometry.setIndex(indices);geometry.computeVertexNormals();return geometry;
 }
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(source.uvs,2));geometry.setIndex(source.indices);geometry.computeVertexNormals();return geometry;
}
