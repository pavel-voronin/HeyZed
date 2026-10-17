import {createRope} from './rope-model.mjs';
import {createBarricade} from './barricade-model.mjs';
import {createShutters} from './shutter-model.mjs';
import {militaryGeometry,canvasNormalMap} from './military-cloth.mjs';
import {sheetGeometry,sheetAnchors} from './sheet-cloth.mjs';
import {laboratorySlabs} from '../laboratory-slabs.mjs';
import {drapeGeometry,drapeSeed,drapeNormalMap,drapeLevels,defaultDrapeLevel} from './drape-cloth.mjs';
// Attachment geometry uses the accepted aperture bounds; native state remains authoritative.
export function apertureBounds(aperture){const pts=aperture.points;return [Math.min(...pts.map(p=>p[0])),Math.min(...pts.map(p=>p[1])),Math.max(...pts.map(p=>p[0])),Math.max(...pts.map(p=>p[1]))];}
export function attachmentAllowed(family,target,reference,floor=1){
 const props=reference?.properties||{};
 if(family.category==='decor')return target==='window';
 if(family.category==='rope')return target==='window'&&floor>0;
 if(target==='window')return true;
 if(family.category==='covering')return family.kind==='sheet'&&('doorTrans' in props)&&!('GarageDoor' in props)&&props.DoorSound?.toLowerCase()!=='slidingglassdoor';
 return !('DoubleDoor' in props)&&!('GarageDoor' in props);
}
export function attachmentVerticalLimits(profile,side='inside'){
 if(!(profile.floorHeight>profile.slabDepth&&profile.slabDepth>0))throw Error('Invalid floor / slab metrics');
 return {bottom:.012,top:profile.floorHeight-(side==='inside'?profile.slabDepth:0)-.012};
}
export function attachmentSlabs(profile){return laboratorySlabs(profile,profile.tileSize*3);}
export function doorSheetMount(recipe,profile){
 const panes=recipe.parts.filter(p=>p.name.startsWith('leaf.glass.'));
 const left=recipe.parts.find(p=>p.name==='leaf.stile.left'),right=recipe.parts.find(p=>p.name==='leaf.stile.right'),bottom=recipe.parts.find(p=>p.name==='leaf.rail.bottom'),top=recipe.parts.find(p=>p.name==='leaf.rail.top');
 if(!left||!right||!bottom||!top)return null;
 const bounds=panes.length?[Math.min(...panes.map(p=>p.bounds[0])),Math.min(...panes.map(p=>p.bounds[1])),Math.max(...panes.map(p=>p.bounds[3])),Math.max(...panes.map(p=>p.bounds[4]))]:[left.bounds[3],bottom.bounds[4],right.bounds[0],top.bounds[1]];
 const [l,b,r,t]=bounds,padding=Math.max(.002,Math.min(.025,(profile.doorLatchStileWidth-profile.doorHandlePlateWidth)/2-.008));
 const edges=[Math.max(left.bounds[0]+.01,l-padding),Math.max(bottom.bounds[1]+.01,b-padding),Math.min(right.bounds[3]-.01,r+padding),Math.min(top.bounds[4]-.01,t+padding)];
 return {bounds:edges,glazing:bounds,surface:profile.doorLeafCenter,halfDepth:profile.doorThickness/2};
}
export function createAttachment(THREE,family,aperture,profile,options={}){
 const lod=drapeLevels.includes(options.lod)?options.lod:defaultDrapeLevel,segments={high:24,medium:12,low:6}[lod];
 const group=new THREE.Group(),[l,openingBottom,r,openingTop]=apertureBounds(aperture);
 const mountSide=family.category==='rope'?'outside':options.side||'inside',limits=attachmentVerticalLimits(profile,mountSide);
 // Reserve the complete hardware envelope, including cloth sag and rotated slats.
 const topMargin=family.kind.startsWith('drape')?.165:family.kind==='roller'?.11:family.kind==='sheet'?.059:family.kind.startsWith('shutter')?.05:family.kind==='metal-sheet'?.05:family.kind==='metal-bars'?.025:0;
 const bottomMargin=family.kind==='sheet'?.15:family.kind==='roller'?.043:family.kind.startsWith('shutter')||family.kind==='metal-sheet'?.05:family.kind==='metal-bars'?.025:family.kind==='wood'?.10:0;
 const b=Math.max(openingBottom,limits.bottom+bottomMargin),t=Math.min(openingTop,limits.top-topMargin),width=r-l,height=t-b;
 const side=mountSide==='inside'?-1:1,depth=side*(profile.wallThickness/2+.055),open=options.open;
 const material=new THREE.MeshStandardMaterial({color:family.color||'#777b80',roughness:.9,side:THREE.DoubleSide});
 const metal=new THREE.MeshStandardMaterial({color:'#79818a',roughness:.55,metalness:.55});
 function add(geometry,position,mat=material,name='attachment'){const mesh=new THREE.Mesh(geometry,mat);mesh.position.set(...position);mesh.name=name;group.add(mesh);return mesh;}
 function box(u,z,w,h,d=.03,offset=depth,mat=material){return add(new THREE.BoxGeometry(w,h,d),[-u,z,offset],mat);}
 function cylinder(u,z,radius,length,axis='x',mat=metal,offset=depth){const geometry=new THREE.CylinderGeometry(radius,radius,length,segments);if(axis==='x')geometry.rotateZ(Math.PI/2);return add(geometry,[-u,z,offset],mat);}
 function cloth(left,right,top,bottom,folds=12,sag=.015){
  const nx=folds?16:1,ny=folds?10:1,vertices=[],indices=[],thickness=.0025;
  for(let face=0;face<2;face++)for(let y=0;y<=ny;y++)for(let x=0;x<=nx;x++){
   const a=x/nx,v=y/ny,u=left+(right-left)*a;
   const wave=Math.sin(a*folds*Math.PI*2)*.018+Math.sin(a*folds*Math.PI*4)*.004;
   vertices.push(-u,top+(bottom-top)*v-sag*Math.sin(a*Math.PI)*v,depth+side*(wave+.008*Math.sin(v*Math.PI)*Math.sin(a*9))+(face?-.5:.5)*thickness);
  }
  const stride=(nx+1)*(ny+1);
  for(let face=0;face<2;face++)for(let y=0;y<ny;y++)for(let x=0;x<nx;x++){const a=face*stride+y*(nx+1)+x,c=a+nx+1;indices.push(a,a+1,c+1,a,c+1,c);}
  for(let x=0;x<nx;x++)for(const y of [0,ny]){const a=y*(nx+1)+x;indices.push(a,a+stride,a+stride+1,a,a+stride+1,a+1);}
  for(let y=0;y<ny;y++)for(const x of [0,nx]){const a=y*(nx+1)+x,c=a+nx+1;indices.push(a,c,c+stride,a,c+stride,a+stride);}
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));geometry.setIndex(indices);geometry.computeVertexNormals();add(geometry,[0,0,0],material,'cloth');
 }
 if(family.kind.startsWith('drape')){
  const military=family.id?.startsWith('tent-'),floorLength=openingBottom<=limits.bottom,hemScale=floorLength?.1:1;
  const top=t+.10,bottom=floorLength?0:(family.kind==='drape-long'||military)?.07:Math.max(.08,b-.1),edge=.10;
  material.roughness=1;
  if(military)material.normalMap=canvasNormalMap(THREE);
  const folds=Math.max(4,Math.min(6,Math.round((width/2+edge)/.09)));
  const seed=drapeSeed((aperture.id||'window')+'|'+family.kind);

  function panel(left,right,index){
   const spec={left,right,top,bottom,depth,side,folds,hemScale,seed:seed+index*97,gathered:!!open,lod,panelIndex:index,seamCenter:open?undefined:(l+r)/2};
   const mat=lod==='low'?material.clone():material;if(lod==='low'&&!military)mat.normalMap=drapeNormalMap(THREE,spec);
   add(military?militaryGeometry(THREE,spec):drapeGeometry(THREE,spec),[0,0,0],mat,'cloth');
  }
  cylinder((l+r)/2,top+.04,.013,width+.26);
  for(const u of [l-.09,r+.09])box(u,top+.04,.04,.05,.08);
  if(open){panel(l-edge,l+width*.16,0);panel(r-width*.16,r+edge,1);}else{panel(l-edge,(l+r)/2+.02,0);panel((l+r)/2-.02,r+edge,1);}
 }else if(family.kind==='sheet'){
  const mount=options.doorSheetMount;
  const top=mount?mount.bounds[3]:Math.min(openingTop+.035,limits.top+.010),bottom=mount?mount.bounds[1]:Math.max(openingBottom-.04,limits.bottom+.002);
  material.roughness=1;
  const overlap=Math.max(.12,(r-l)*.18),mountPlane=mount?mount.surface+side*mount.halfDepth:side*profile.wallThickness/2;
  const spec={left:mount?mount.bounds[0]:l-overlap,right:mount?mount.bounds[2]:r+overlap,top,bottom,depth:mountPlane+side*.018,side,open:!!open,lod,edgeOverhangScale:mount?.08:1,coverPerimeter:!!mount};
  const sheet=add(sheetGeometry(THREE,spec),[0,0,0],material,'cloth');
  const topDepth=spec.depth+side*(spec.right-spec.left)*.0175,wallDepth=mountPlane;
  const rail=box((l+r)/2,Math.min(top-.009,limits.top-.003),spec.right-spec.left,.014,Math.abs(topDepth-wallDepth),wallDepth+(topDepth-wallDepth)/2,metal);rail.name='sheet.mount';
  const mountingSpec={...spec,open:false},mountingGeometry=open?sheetGeometry(THREE,mountingSpec):sheet.geometry;
  const anchors=sheetAnchors(THREE,mountingSpec,mountingGeometry);if(open){
   mountingGeometry.dispose();const positions=sheet.geometry.attributes.position,point=new THREE.Vector3();
   for(const anchor of anchors.slice(0,2)){const target=new THREE.Vector3(...anchor);let nearest=Infinity,index=0;for(let i=0;i<positions.count;i++){point.fromBufferAttribute(positions,i);const distance=point.distanceToSquared(target);if(distance<nearest){nearest=distance;index=i;}}positions.setXYZ(index,...anchor);}
   positions.needsUpdate=true;sheet.geometry.computeVertexNormals();
  }
  for(const [x,y,z] of (open?anchors.slice(0,2):anchors)){
   const wall=mountPlane,reach=Math.abs(z-wall),clipY=Math.min(y,limits.top);
   const stem=box(-x,clipY,.014,.014,reach,wall+side*reach/2,metal);stem.name='sheet.mount';
   const head=box(-x,clipY,.024,.024,.008,z,metal);head.name='sheet.clip';head.userData.anchor=[x,y,z];
  }
 }else if(family.kind==='roller'){
  cylinder((l+r)/2,t+.065,open?.045:.025,width+.10);
  if(!open){cloth(l-.025,r+.025,t+.04,b-.035,0,0);cylinder((l+r)/2,b-.035,.008,width+.05);}
  box(r+.035,t-.18,.008,.40,.008);
 }else if(family.kind.startsWith('shutter')){
  const shutters=createShutters(THREE,family,{left:l,right:r,bottom:Math.max(limits.bottom,openingBottom-.025),top:Math.min(attachmentVerticalLimits(profile,'inside').top,openingTop+.025),wall:side*profile.wallThickness/2,side,panels:options.panels});
  group.add(...shutters.children.slice());
 }else if(['wood','metal-sheet','metal-bars'].includes(family.kind)){
  material.dispose();metal.dispose();
  const boundary=attachmentVerticalLimits(profile,'inside');
  const barricade=createBarricade(THREE,family,{left:l,right:r,bottom:Math.max(boundary.bottom,openingBottom-.06),top:Math.min(boundary.top,openingTop+.06),wall:side*profile.wallThickness/2,side,lod,frameProjection:profile.windowFrameProjection,planks:options.planks,damaged:!!options.damaged});
  group.add(...barricade.children.slice());
 }else if(family.category==='rope'){
  material.dispose();metal.dispose();
  const rope=createRope(THREE,family,{center:(l+r)/2,bottom:openingBottom,wallThickness:profile.wallThickness,frameProjection:profile.windowFrameProjection,floorHeight:profile.floorHeight,ceiling:limits.top,floors:options.floors||1,lod});
  group.userData.ground=rope.userData.ground;group.userData.mountPoint=rope.userData.mountPoint;group.add(...rope.children.slice());
 }
 return group;
}
