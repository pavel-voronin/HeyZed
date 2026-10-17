function cordMaps(THREE){
 const size=128,color=new Uint8Array(size*size*4),normal=new Uint8Array(size*size*4);
 function relief(u,v){return .65*Math.cos(Math.PI*2*(3*u-2*v))+.12*Math.cos(Math.PI*2*(21*u+7*v));}
 for(let y=0;y<size;y++)for(let x=0;x<size;x++){
  const u=x/size,v=y/size,k=(y*size+x)*4,h=relief(u,v),shade=210+h*25;
  color[k]=shade;color[k+1]=shade;color[k+2]=shade;color[k+3]=255;
  const dx=(relief(u+1/size,v)-relief(u-1/size,v))*.65,dy=(relief(u,v+1/size)-relief(u,v-1/size))*.65,n=new THREE.Vector3(-dx,-dy,1).normalize();
  normal[k]=(n.x*.5+.5)*255;normal[k+1]=(n.y*.5+.5)*255;normal[k+2]=(n.z*.5+.5)*255;normal[k+3]=255;
 }
 const textures=[color,normal].map(data=>{const t=new THREE.DataTexture(data,size,size,THREE.RGBAFormat);t.wrapS=t.wrapT=THREE.RepeatWrapping;t.magFilter=THREE.LinearFilter;t.minFilter=THREE.LinearMipmapLinearFilter;t.generateMipmaps=true;t.needsUpdate=true;return t;});textures[0].colorSpace=THREE.SRGBColorSpace;textures[0].userData.attachmentOwned=true;return textures;
}

function strand(THREE,points,steps,sides,wide,narrow,twist,ground){
 const curve=new THREE.CatmullRomCurve3(points.map(p=>new THREE.Vector3(...p))),positions=[],uvs=[],indices=[],centers=Array.from({length:steps+1},(_,i)=>curve.getPoint(i/steps));
 let distance=0;
 for(let i=0;i<=steps;i++){
  const u=i/steps,center=centers[i],tangent=curve.getTangent(u).normalize(),normal=new THREE.Vector3(1,0,0).addScaledVector(tangent,-tangent.x).normalize(),binormal=new THREE.Vector3().crossVectors(tangent,normal).normalize(),phase=twist*u;
  if(i)distance+=center.distanceTo(centers[i-1]);
  for(let j=0;j<=sides;j++){
   const a=j/sides*Math.PI*2+phase,point=center.clone().addScaledVector(normal,Math.cos(a)*wide).addScaledVector(binormal,Math.sin(a)*narrow);
   positions.push(point.x,Math.max(ground,point.y),point.z);uvs.push(j/sides,distance/.12);
  }
 }
 const ring=sides+1;
 for(let i=0;i<steps;i++)for(let j=0;j<sides;j++){const a=i*ring+j,b=a+1,c=a+ring,d=c+1;indices.push(a,b,c,b,d,c);}
 for(const [row,reverse] of [[0,true],[steps,false]]){const center=centers[row],v=positions.length/3;positions.push(center.x,Math.max(ground,center.y),center.z);uvs.push(.5,row?distance/.12:0);for(let j=0;j<sides;j++){const a=row*ring+j,b=a+1;indices.push(v,reverse?b:a,reverse?a:b);}}
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));geometry.setIndex(indices);geometry.computeVertexNormals();
 const n=geometry.attributes.normal;for(let i=0;i<=steps;i++){const a=i*ring,b=a+sides,v=new THREE.Vector3().fromBufferAttribute(n,a).add(new THREE.Vector3().fromBufferAttribute(n,b)).normalize();n.setXYZ(a,v.x,v.y,v.z);n.setXYZ(b,v.x,v.y,v.z);}
 return geometry;
}

export function createRope(THREE,family,{center,bottom,wallThickness,frameProjection=0,floorHeight,ceiling,floors=1,lod='medium'}){
 const group=new THREE.Group(),cloth=family.kind==='sheet-rope',ground=-floors*floorHeight,front=wallThickness/2+frameProjection,outside=front+.048,x=-center,anchorY=Math.min(bottom+.014,ceiling-.02),anchor=[x,anchorY,front+.0005];
 const sides={high:12,medium:8,low:6}[lod],bendSteps={high:14,medium:10,low:6}[lod],dropStep={high:.26,medium:.42,low:.65}[lod];
 const bend=new THREE.CubicBezierCurve3(new THREE.Vector3(...anchor),new THREE.Vector3(x,anchorY,outside),new THREE.Vector3(x,anchorY-.025,outside),new THREE.Vector3(x,anchorY-.07,outside));
 const points=bend.getPoints(bendSteps).map(v=>v.toArray());
 for(let y=anchorY-.07-dropStep;y>ground;y-=dropStep){const drop=anchorY-y;points.push([x+.003*Math.sin(drop*1.8),y,outside+.002*Math.sin(drop*1.4)]);}
 points.push([x,ground,outside]);
 const geometry=strand(THREE,points,(points.length-1)*(lod==='high'?2:1),sides,cloth?.014:.011,cloth?.010:.011,cloth?1.5:0,ground),[map,normalMap]=cordMaps(THREE);
 const mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color:cloth?'#c4bdab':'#aa9065',map,normalMap,normalScale:new THREE.Vector2(.65,.65),roughness:.97}));mesh.name='rope.strand';group.add(mesh);
 group.userData.ground=ground;group.userData.mountPoint=anchor;return group;
}
