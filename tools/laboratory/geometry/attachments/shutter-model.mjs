export function createShutters(THREE,family,{left,right,bottom,top,wall,side,panels=[0,1]}){
 const group=new THREE.Group(),width=(right-left)*.42,height=top-bottom,gap=.035;
 const paint=new THREE.MeshStandardMaterial({color:family.color||'#d6d4c7',roughness:.92});
 const iron=new THREE.MeshStandardMaterial({color:'#4d514e',roughness:.7,metalness:.4});
 const frame=Math.min(.038,width*.14),depth=wall+side*.022;
 for(const [index,center] of [left-gap-width/2,right+gap+width/2].entries()){
  if(!panels.includes(index))continue;
  const wood=[],hardware=[];
  function part(list,x,y,z,w,h,d){const geometry=new THREE.BoxGeometry(w,h,d);geometry.translate(x,y,z);list.push(geometry);}
  for(const sign of [-1,1])part(wood,sign*(width-frame)/2,0,0,frame,height,.034);
  for(const y of [-(height-frame)/2,0,(height-frame)/2])part(wood,0,y,0,width-2*frame,frame,.034);
  const innerWidth=width-2*frame,sectionHeight=(height-3*frame)/2;
  for(const y of [-(height-frame)/4,(height-frame)/4]){
   if(family.kind==='shutter-louver'){
    const count=Math.max(2,Math.min(24,Math.ceil(sectionHeight/.055))),pitch=sectionHeight/count;
    for(let i=0;i<count;i++){
     const slat=new THREE.BoxGeometry(innerWidth,pitch*.95,.008);slat.rotateX(side*.35);slat.translate(0,y-sectionHeight/2+pitch*(i+.5),side*.003);wood.push(slat);
    }
   }else{
    part(wood,0,y,-side*.007,innerWidth,sectionHeight,.012);
    const trim=Math.min(.012,frame*.4);
    for(const sign of [-1,1]){
     part(wood,sign*(innerWidth-trim)/2,y,side*.009,trim,sectionHeight,.009);
     part(wood,0,y+sign*(sectionHeight-trim)/2,side*.009,innerWidth-2*trim,trim,.009);
    }
   }
  }
  const hingeX=(index===0?-1:1)*(width/2+.013);
  for(const y of [-height*.30,height*.30]){
   part(hardware,hingeX,y,side*.009,.04,.052,.012);
   part(hardware,hingeX,y,-side*.011,.02,.035,.022);
  }
  function combined(parts,material,name){
   const positions=[],normals=[],uvs=[],indices=[];
   for(const geometry of parts){const offset=positions.length/3;positions.push(...geometry.attributes.position.array);normals.push(...geometry.attributes.normal.array);uvs.push(...geometry.attributes.uv.array);indices.push(...Array.from(geometry.index.array,i=>i+offset));geometry.dispose();}
   const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('normal',new THREE.Float32BufferAttribute(normals,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));geometry.setIndex(indices);
   const mesh=new THREE.Mesh(geometry,material);mesh.position.set(-center,(top+bottom)/2,depth);mesh.name=name;group.add(mesh);
  }
  combined(wood,paint,'shutter.panel');combined(hardware,iron,'shutter.mount');
 }
 return group;
}
