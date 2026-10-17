import bake from './military-bake.json' with {type:'json'};

export function militaryGeometry(THREE,spec){
 const source=bake.states[spec.gathered?'open':'closed'].levels[spec.lod||'medium'];
 const positions=[],width=spec.right-spec.left,height=spec.top-spec.bottom;
 for(let i=0;i<source.positions.length;i+=3){
  const u=spec.left+width*source.positions[i],v=source.positions[i+1];
  const blend=spec.seamCenter===undefined?1:Math.max(0,Math.min(1,(Math.abs(u-spec.seamCenter)-.055)/.06));
  positions.push(-u,spec.top-height*v,spec.depth+spec.side*(.004+Math.max(.002,source.positions[i+2])*blend+(spec.panelIndex||0)*.003*(1-blend)));
 }
 const geometry=new THREE.BufferGeometry();
 geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
 geometry.setAttribute('uv',new THREE.Float32BufferAttribute(source.uvs,2));
 geometry.setIndex(source.indices);geometry.computeVertexNormals();return geometry;
}

export function canvasNormalMap(THREE){
 const size=64,data=new Uint8Array(size*size*4);
 for(let y=0;y<size;y++)for(let x=0;x<size;x++){
  const i=(y*size+x)*4,a=Math.sin(x*Math.PI/4)*.025,b=Math.sin(y*Math.PI/4)*.025;
  data[i]=Math.round((.5+a*.5)*255);data[i+1]=Math.round((.5+b*.5)*255);data[i+2]=253;data[i+3]=255;
 }
 const map=new THREE.DataTexture(data,size,size,THREE.RGBAFormat);map.wrapS=map.wrapT=THREE.RepeatWrapping;map.repeat.set(8,24);map.magFilter=THREE.LinearFilter;map.minFilter=THREE.LinearFilter;map.needsUpdate=true;return map;
}
