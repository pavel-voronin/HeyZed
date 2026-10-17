// Scene context uses the canonical card; opening geometry remains unchanged.
export function laboratorySlabs(profile,width=profile.tileSize,reach=profile.tileSize*1.5){
 const {floorHeight,slabDepth,wallThickness}=profile;
 if(!(floorHeight>slabDepth&&slabDepth>0&&width>0&&reach>0))throw Error('Invalid floor / slab metrics');
 const front=wallThickness/2;
 return [0,floorHeight].map((top,i)=>({name:i?'ceiling':'floor',size:[width,slabDepth,i?reach+front:reach*2],center:[-width/2,top-slabDepth/2,i?(front-reach)/2:0]}));
}
export function createLaboratorySlabs(THREE,profile,width,reach){
 const group=new THREE.Group();group.name='floor-and-ceiling';
 for(const part of laboratorySlabs(profile,width,reach)){
  const mesh=new THREE.Mesh(new THREE.BoxGeometry(...part.size),new THREE.MeshStandardMaterial({color:'#8d969b',roughness:1}));
  mesh.name=part.name;mesh.position.set(...part.center);group.add(mesh);
 }
 return group;
}
export function windowCeilingFit(aperture,descriptor,recipe){
 const ceiling=descriptor.profile.floorHeight-descriptor.profile.slabDepth;
 const openingTop=Math.max(...aperture.points.map(point=>point[1]));
 const modelTop=Math.max(openingTop,...(recipe?.parts||[]).map(part=>part.polygon?Math.max(...part.polygon.map(point=>point[1])):part.bounds[4]));
 const modelBottom=Math.min(...aperture.points.map(point=>point[1]),...(recipe?.parts||[]).map(part=>part.polygon?Math.min(...part.polygon.map(point=>point[1])):part.bounds[1]));
 return {ceiling,openingTop,modelTop,modelBottom,floorExcess:Math.max(0,-modelBottom),openingExcess:Math.max(0,openingTop-ceiling),modelExcess:Math.max(0,modelTop-ceiling)};
}
