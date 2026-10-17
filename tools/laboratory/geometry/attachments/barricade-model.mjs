function merge(THREE,parts,material,name){
 const positions=[],normals=[],uvs=[],indices=[];
 for(const geometry of parts){const offset=positions.length/3;positions.push(...geometry.attributes.position.array);normals.push(...geometry.attributes.normal.array);uvs.push(...geometry.attributes.uv.array);indices.push(...Array.from(geometry.index.array,i=>i+offset));geometry.dispose();}
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('normal',new THREE.Float32BufferAttribute(normals,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));geometry.setIndex(indices);
 const mesh=new THREE.Mesh(geometry,material);mesh.name=name;return mesh;
}

function timberMap(THREE){
 const size=128,data=new Uint8Array(size*size*4);
 for(let y=0;y<size;y++)for(let x=0;x<size;x++){
  const i=(y*size+x)*4,grain=Math.sin(y*.7+Math.sin(x*.05)*.7)+.35*Math.sin(y*2.3+x*.015),knot=Math.exp(-((x-38)**2/110+(y-47)**2/24));
  const value=Math.max(100,Math.min(240,202+grain*14-knot*40));data[i]=value;data[i+1]=value;data[i+2]=value;data[i+3]=255;
 }
 const map=new THREE.DataTexture(data,size,size,THREE.RGBAFormat);map.colorSpace=THREE.SRGBColorSpace;map.magFilter=THREE.LinearFilter;map.minFilter=THREE.LinearFilter;map.needsUpdate=true;return map;
}

function sheetGeometry(THREE,width,height,side,damaged,lod){
 if(!damaged)return new THREE.BoxGeometry(width,height,.006);
 const shape=new THREE.Shape(),outline=[[0,0],[.20,0],[.25,.16],[.30,0],[1,0],[1,1],[.80,1],[.75,.88],[.70,1],[0,1]];
 outline.forEach(([u,v],i)=>shape[i?'lineTo':'moveTo']((u-.5)*width,(v-.5)*height));shape.closePath();
 const geometry=new THREE.ExtrudeGeometry(shape,{depth:.006,bevelEnabled:false,steps:1});geometry.translate(0,0,-.003);
 const p=geometry.attributes.position,uv=geometry.attributes.uv;
 for(let i=0;i<p.count;i++){const u=p.getX(i)/width+.5,v=p.getY(i)/height+.5;p.setZ(i,p.getZ(i)+side*.013*Math.max(0,1-Math.abs(u-.25)/.05)*(1-v));uv.setXY(i,u,v);}
 geometry.setIndex(Array.from({length:p.count},(_,i)=>i));geometry.computeVertexNormals();return geometry;
}

export function createBarricade(THREE,family,{left,right,bottom,top,wall,side,lod='medium',planks=4,damaged=false,frameProjection=0}){
 const group=new THREE.Group(),width=right-left,height=top-bottom,center=(left+right)/2,thickness=family.kind==='wood'?.028:family.kind==='metal-sheet'?.006:.025,depth=wall+side*(frameProjection+thickness/2);
 const steel=new THREE.MeshStandardMaterial({color:family.kind==='metal-bars'?'#626c6c':'#a3a7a3',roughness:family.kind==='metal-bars'?.73:.62,metalness:.55});
 const fastener=new THREE.MeshStandardMaterial({color:'#555853',roughness:.64,metalness:.65,side:THREE.DoubleSide}),hardware=[];
 function box(parts,x,y,z,w,h,d){const g=new THREE.BoxGeometry(w,h,d);g.translate(x,y,z);parts.push(g);}
 function bolt(x,y,face){
  const head=new THREE.PlaneGeometry(.009,.009);head.translate(x,y,face);hardware.push(head);
 }
 if(family.kind==='wood'){
  steel.dispose();
  const count=Math.max(1,Math.min(4,Math.round(planks))),length=width+.08,bandHeight=Math.min(.96,height-.006),bandBottom=Math.max(bottom,Math.min(top-bandHeight,.85)),pitch=bandHeight/4,boardHeight=pitch*.94,map=timberMap(THREE);
  for(let i=0;i<count;i++){
   const scale=Math.min(1,height/.96),angle=[.022,.006,-.017,-.002][i]*Math.min(1,.9/length)*scale,shift=[-.009,.006,-.004,.010][i],half=boardHeight/2*Math.abs(Math.cos(angle))+length/2*Math.abs(Math.sin(angle))+.002;
   const y=Math.max(bottom+half,Math.min(top-half,bandBottom+pitch*(i+.5)+[-.012,-.003,.004,.018][i]*scale));
   const geometry=new THREE.BoxGeometry(length,boardHeight,.028);
   const uv=geometry.attributes.uv,positions=geometry.attributes.position;for(let v=0;v<positions.count;v++)uv.setXY(v,positions.getX(v)/length+.5,positions.getY(v)/boardHeight+.5);
   const material=new THREE.MeshStandardMaterial({color:['#94734b','#8d6943','#9b7b50','#886640'][i],map,roughness:.98});
   const mesh=new THREE.Mesh(geometry,material);mesh.name='barricade.plank';mesh.position.set(-center+shift,y,depth);mesh.rotation.z=angle;group.add(mesh);
   for(const x of [-(Math.max(.006,width/2-.013)),Math.max(.006,width/2-.013)])for(const localY of [-boardHeight*.25,boardHeight*.25]){
    bolt(-center+shift+x*Math.cos(angle)-localY*Math.sin(angle),y+x*Math.sin(angle)+localY*Math.cos(angle),depth+side*.0145);
   }
  }
 }else{
  const parts=[];
  if(family.kind==='metal-sheet'){
   const geometry=sheetGeometry(THREE,width+.12,height,side,damaged,lod);geometry.translate(-center,(top+bottom)/2,depth);parts.push(geometry);
  }else{
   for(const y of [bottom+.02,top-.02])box(parts,-center,y,depth,width+.12,.04,.025);
   for(let i=0;i<4;i++)box(parts,-(left+width*i/3),(bottom+top)/2,depth,.019,height-.04,.019);
  }
  for(const x of [-(left-.045),-(right+.045)])for(const y of [bottom+.03,top-.03]){
   const rear=wall+side*frameProjection;box(hardware,x,y,(wall+rear)/2,.024,.036,Math.max(.0001,frameProjection));bolt(x,y,depth+side*(thickness/2+.0005));
  }
  group.add(merge(THREE,parts,steel,family.kind==='metal-sheet'?'barricade.sheet':'barricade.bars'));
 }
 group.add(merge(THREE,hardware,fastener,'barricade.mount'));return group;
}
