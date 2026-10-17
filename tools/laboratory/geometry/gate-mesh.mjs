// Gate leaves share native single/double topology and the architectural card.
export function buildGate(aperture,descriptor,state='closed'){
 const p=descriptor.profile,c=aperture.construction,design=c.gateDesign;
 const [l,r,b,t]=aperture.bounds,parts=[],gap=p.doorLeafClearance;
 const wood=design.startsWith('wood')||design==='log',material=design==='wood-painted-low'?'frame':wood?'wood':'metal';
 const bar=wood?p.doorStileWidth:p.doorBarWidth,half=p.doorThickness/2;
 function box(name,mat,a,z0,n0,x,z1,n1,pivot=null,right=false){
  if(x<=a||z1<=z0||n1<=n0)return;
  const part={name,material:mat,bounds:[a,z0,n0,x,z1,n1]};
  if(pivot!==null&&state==='open')part.transform={pivot:[pivot,b,0],angleDegrees:right?-90:90};
  parts.push(part);
 }
 const role=c.sectionRole?((c.sectionRole-1)%4)+1:null;
 const total=c.double&&!role?r-l:descriptor.metric.tileSize;
 const post=p.doorStileWidth;
 // Posts stand at the actual hinges, without an invented overhead lintel.
 if(!role||role===1)box('post.left',material,l-post/2,b,-post/2,l+post/2,t+Math.min(post,p.floorHeight-p.slabDepth-t),post/2);
 if(!role||role===4)box('post.right',material,r-post/2,b,-post/2,r+post/2,t+Math.min(post,p.floorHeight-p.slabDepth-t),post/2);
 const leaves=c.double&&!role?[[l,r-(r-l)/2,l,false],[l+(r-l)/2,r,r,true]]:[[l,r,role?(role<=2?l-(role-1)*total:r+(4-role)*total):l,role?role>=3:false]];
 for(const [i,[left,right,pivot,reverse]] of leaves.entries()){
  const a=left+gap,z0=b+gap,x=right-gap,z1=t-gap,prefix='leaf.'+i+'.';
  const put=(name,mat,u,v,h0,h1,depth=half)=>box(prefix+name,mat,u,h0,-depth,v,h1,depth,pivot,reverse);
  put('stile.left',material,a,a+bar,z0,z1);put('stile.right',material,x-bar,x,z0,z1);
  put('rail.bottom',material,a+bar,x-bar,z0,z0+bar);put('rail.top',material,a+bar,x-bar,z1-bar,z1);
  if(design.startsWith('wood')){
   const count=Math.ceil((x-a)/p.doorStileWidth),step=(x-a)/count;
   const cutout=design==='wood-tall';
   for(let j=0;j<count;j++){
    const u=a+j*step,v=a+(j+1)*step-gap;
    put('plank.'+j,design==='wood-painted-low'?'frame':'wood',u,v,z0,cutout?z1-(z1-z0)*.3:z1,half*.7);
    if(cutout)put('plank.top.'+j,'wood',u,v,z1-(z1-z0)*.16,z1,half*.7);
   }
   for(const side of [-1,1]){
    const near=side*half,far=side*(half+p.doorPanelRelief),depth=[Math.min(near,far),Math.max(near,far)];
    for(const [j,f] of [.2,.8].entries())box(prefix+'brace.'+j+'.'+side,material,a,z0+(z1-z0)*f,depth[0],x,z0+(z1-z0)*f+bar,depth[1],pivot,reverse);
    if(!cutout){
     const start=[a+bar,z0+(z1-z0)*.2+bar],end=[x-bar,z0+(z1-z0)*.8];
     const dx=end[0]-start[0],dz=end[1]-start[1],length=Math.hypot(dx,dz),offset=[-dz/length*bar/2,dx/length*bar/2];
     const polygon=[[start[0]-offset[0],start[1]-offset[1]],[end[0]-offset[0],end[1]-offset[1]],[end[0]+offset[0],end[1]+offset[1]],[start[0]+offset[0],start[1]+offset[1]]];
     parts.push({name:prefix+'brace.diagonal.'+side,material,polygon,depth,...(state==='open'?{transform:{pivot:[pivot,b,0],angleDegrees:reverse?-90:90}}:{})});
    }
   }
  }else if(design==='farm'||design==='log'){
   for(let j=1;j<4;j++)put('rail.'+j,material,a+bar,x-bar,z0+(z1-z0)*j/4,z0+(z1-z0)*j/4+bar);
  }else if(design.startsWith('chain')){
   const spacing=p.doorRailWidth,width=p.doorBarWidth/3;
   // Two diagonal wire directions, each clipped to the framed rectangle.
   const low=z0+bar,high=z1-bar,u0=a+bar,u1=x-bar;
   for(const sign of [-1,1])for(let k=Math.floor((low-u1)/spacing);k<=Math.ceil((high+u1)/spacing);k++){
    const intercept=k*spacing;
    const polygon=[[u0,sign*u0+intercept-width/2],[u1,sign*u1+intercept-width/2],[u1,sign*u1+intercept+width/2],[u0,sign*u0+intercept+width/2]];
    let clipped=polygon;
    for(const [height,above] of [[low,true],[high,false]]){
     const next=[];clipped.forEach((q,j)=>{const v=clipped[(j+clipped.length-1)%clipped.length],inside=w=>above?w[1]>=height:w[1]<=height;if(inside(v)!==inside(q)){const f=(height-v[1])/(q[1]-v[1]);next.push([v[0]+f*(q[0]-v[0]),height]);}if(inside(q))next.push(q);});clipped=next;
    }
    if(clipped.length>=3)parts.push({name:prefix+'wire.'+sign+'.'+k,material:'metal',polygon:clipped,depth:[-width/2,width/2],...(state==='open'?{transform:{pivot:[pivot,b,0],angleDegrees:reverse?-90:90}}:{})});
   }
  }else{
   const count=Math.max(3,Math.round((x-a)/p.doorHandleLength)),step=(x-a)/count;
   for(let j=1;j<count;j++)put('bar.'+j,'metal',a+step*j-bar/2,a+step*j+bar/2,z0,z1);
   put('rail.middle','metal',a+bar,x-bar,z0+(z1-z0)*.7,z0+(z1-z0)*.7+bar);
  }
  const latch=reverse?a+bar/2:x-bar/2,h=Math.min(b+p.doorHandleHeight,z1-bar*2);
  box(prefix+'latch','hardware',latch-bar,h-bar,half,latch+bar,h+bar,half+p.doorHandlePlateDepth,pivot,reverse);
  for(const f of [.2,.8])box(prefix+'hinge.'+f,'hardware',reverse?x-bar:a,z0+(z1-z0)*f,-half-p.doorHandlePlateDepth,reverse?x:a+bar,z0+(z1-z0)*f+bar,half+p.doorHandlePlateDepth,pivot,reverse);
 }
 return {parts,profileId:aperture.id,recipe:'gate.'+design+'/1',visualState:state};
}
