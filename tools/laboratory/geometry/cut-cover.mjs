const dot=(a,b)=>a.reduce((sum,n,i)=>sum+n*b[i],0);
function onSegment(point,a,b){const v=[b[0]-a[0],b[1]-a[1]],q=[point[0]-a[0],point[1]-a[1]],length=dot(v,v);if(length<1e-16)return Math.hypot(...q)<1e-6;const t=dot(q,v)/length;return t>=-1e-6&&t<=1+1e-6&&Math.abs(v[0]*q[1]-v[1]*q[0])<Math.sqrt(length)*1e-6;}

// Positions and normals use the owning boundary's (u, z, depth) coordinates.
export function boundarySurfaceRole(part,point,normal){
 if(part.sourcePlanes){const original=part.sourcePlanes.some(({n,c})=>Math.abs(dot(n,point)-c)<1e-6&&dot(n,normal)>.999);return original?(normal[2]>.999?'front':normal[2]<-.999?'back':'edge'):'cut';}
 const depth=part.sourceDepth||part.depth||[part.bounds[2],part.bounds[5]];
 if(Math.abs(normal[2])>.999){
  if(Math.abs(point[2]-depth[1])<1e-6)return 'front';
  if(Math.abs(point[2]-depth[0])<1e-6)return 'back';
  return part.sourceShape?'cut':'edge';
 }
 const loops=part.sourceShape||[part.polygon||[[part.bounds[0],part.bounds[1]],[part.bounds[3],part.bounds[1]],[part.bounds[3],part.bounds[4]],[part.bounds[0],part.bounds[4]]],...(part.holes||[])];
 return loops.some(loop=>loop.some((a,i)=>onSegment(point, a,loop[(i+1)%loop.length])))?'edge':'cut';
}

export function projectCutCover(point,normal,cover){
 const origin=cover.origin||[0,0,0],size=cover.size||[1,1,1];
 if(size.some(n=>!Number.isFinite(n)||n<=0))throw Error('Invalid cut cover scale');
 const p=point.map((n,i)=>(n-origin[i])/size[i]),axis=[0,1,2].reduce((a,i)=>Math.abs(normal[i])>Math.abs(normal[a])?i:a,0),positive=normal[axis]>=0;
 const uv=axis===0?[positive?-p[2]:p[2],p[1]]:axis===1?[p[0],positive?p[2]:-p[2]]:[positive?p[0]:-p[0],p[1]];
 return {face:axis*2+(positive?0:1),uv:cover.wrap==='stretch'?uv.map(n=>Math.max(0,Math.min(1,n))):uv};
}

// Procedural boxes retain their original six-face layout through the cut.
// Arbitrary authored UVs are not regenerated using this optional box convention.
export function projectOriginalBox(point,normal,frame){
 const [l,r,b,t,d0,d1]=frame,[u,z,d]=point,axis=[0,1,2].reduce((a,i)=>Math.abs(normal[i])>Math.abs(normal[a])?i:a,0),positive=normal[axis]>=0;
 if(axis===0)return [positive?(d-d0)/(d1-d0):(d1-d)/(d1-d0),(z-b)/(t-b)];
 if(axis===1)return [(r-u)/(r-l),positive?(d1-d)/(d1-d0):(d-d0)/(d1-d0)];
 return [positive?(r-u)/(r-l):(u-l)/(r-l),(z-b)/(t-b)];
}

// Keep existing UVs; only newly exposed triangles receive wall-owned projection.
export function prepareBoundaryCover(THREE,input,part,position,cover){
 const geometry=input.index?input.toNonIndexed():input;geometry.clearGroups();
 const points=geometry.attributes.position,normals=geometry.attributes.normal,uv=geometry.attributes.uv;
 const counts={front:0,back:0,edge:0,cut:0};
 for(let i=0;i<points.count;i+=3){
  const positions=[0,1,2].map(j=>[-points.getX(i+j)-position[0],points.getY(i+j)+position[1],points.getZ(i+j)+position[2]]);
  const normal=[-normals.getX(i),normals.getY(i),normals.getZ(i)],center=[0,1,2].map(axis=>positions.reduce((sum,p)=>sum+p[axis],0)/3),role=boundarySurfaceRole(part,center,normal);counts[role]++;
  let material={front:0,back:1,edge:2}[role];
  if(role==='cut'){
   const projected=positions.map(p=>projectCutCover(p,normal,cover));material=3+projected[0].face;
   if(uv)projected.forEach((p,j)=>uv.setXY(i+j,...p.uv));
  }else if(part.sourceUVFrame&&uv)positions.forEach((p,j)=>uv.setXY(i+j,...projectOriginalBox(p,normal,part.sourceUVFrame)));
  geometry.addGroup(i,3,material);
 }
 if(uv)uv.needsUpdate=true;
 return {geometry,counts};
}
