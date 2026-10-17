export function windowPartGeometry(THREE,part){
 let geometry,position;
 if(part.cylinder){geometry=new THREE.CylinderGeometry(part.cylinder.radius,part.cylinder.radius,part.cylinder.length,16);geometry.rotateZ(Math.PI/2);position=part.cylinder.center;
 }else if(part.polyhedron){
  const points=[];
  for(const face of part.polyhedron)for(let i=1;i<face.length-1;i++)for(const [u,z,d] of [face[0],face[i+1],face[i]])points.push(-u,z,d);
  geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(points,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(new Array(points.length/3*2).fill(0),2));geometry.computeVertexNormals();position=[0,0,0];
 }else if(part.polygon){
  const shape=new THREE.Shape();part.polygon.forEach(([u,z],i)=>i?shape.lineTo(-u,z):shape.moveTo(-u,z));shape.closePath();
  for(const loop of part.holes||[]){const hole=new THREE.Path();loop.forEach(([u,z],i)=>i?hole.lineTo(-u,z):hole.moveTo(-u,z));hole.closePath();shape.holes.push(hole);}
  geometry=new THREE.ExtrudeGeometry(shape,{depth:part.depth[1]-part.depth[0],bevelEnabled:false,steps:1});
  if(part.axis==='u'){geometry.rotateY(Math.PI/2);geometry.translate(-part.depth[1],0,0);}
  else geometry.translate(0,0,part.depth[0]);
  position=[0,0,0];

 }else{const [l,b,d0,r,t,d1]=part.bounds;geometry=new THREE.BoxGeometry(r-l,t-b,d1-d0);position=[-(l+r)/2,(b+t)/2,(d0+d1)/2];}
 return {geometry,position};
}
