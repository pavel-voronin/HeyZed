// One fixed light and one translating sash. Native orientation is a placement
// transform; it never selects a different door model. Laboratories and world
// preparation consume this same approved recipe.
export function buildSlidingDoor(aperture,descriptor,state='closed',fixedState='closed'){
 if(!['closed','open'].includes(state))throw Error('Unsupported sliding door state');
 if(!['closed','glassRemoved'].includes(fixedState))throw Error('Unsupported fixed glass state');
 const p=descriptor.profile,[l,r,b,t]=aperture.bounds,parts=[];
 const frame=p.windowFrameWidth,stile=p.windowFrameWidth,rail=p.doorRailWidth;
 const half=p.doorThickness/2,gap=p.doorLeafClearance,trackGap=p.windowSashGap;
 const trackCenter=half+trackGap/2,depth=p.wallThickness/2;
 if(trackCenter+half>depth)throw Error('Sliding tracks do not fit inside the wall section');
 const left=l+frame+gap,right=r-frame-gap;
 const z0=b+frame+gap,z1=t-frame-gap;
 const sashWidth=(right-left+stile)/2,travel=right-left-sashWidth;
 if(travel<=stile||z1-z0<=2*rail)throw Error('Sliding door opening is too small');
 function box(name,material,a,z,d,c,y,e){
  if(!(c>a&&y>z&&e>d))throw Error('Invalid sliding door part: '+name);
  parts.push({name,material,bounds:[a,z,d,c,y,e]});
 }
 // Full-depth jambs and a low sill close the opening around both tracks.
 box('casing.left','frame',l,b,-depth,l+frame,t,depth);
 box('casing.right','frame',r-frame,b,-depth,r,t,depth);
 box('casing.top','frame',l+frame,t-frame,-depth,r-frame,t,depth);
 box('casing.sill','frame',l+frame,b,-depth,r-frame,b+frame,depth);
 const trackWidth=Math.min(gap,trackGap/2);
 for(const [role,n] of [['fixed',-trackCenter],['sliding',trackCenter]]){
  box('track.'+role+'.bottom','hardware',left,b+frame,n-trackWidth/2,right,z0+gap,n+trackWidth/2);
  box('track.'+role+'.top','hardware',left,z1-gap,n-trackWidth/2,right,t-frame,n+trackWidth/2);
 }
 function sash(prefix,a,c,n){
  box(prefix+'.stile.left','frame',a,z0,n-half,a+stile,z1,n+half);
  box(prefix+'.stile.right','frame',c-stile,z0,n-half,c,z1,n+half);
  box(prefix+'.rail.bottom','frame',a+stile,z0,n-half,c-stile,z0+rail,n+half);
  box(prefix+'.rail.top','frame',a+stile,z1-stile,n-half,c-stile,z1,n+half);
  if(prefix!=='fixed'||fixedState==='closed')box(prefix+'.glass','glass',a+stile,z0+rail,n-p.windowPaneThickness/2,c-stile,z1-stile,n+p.windowPaneThickness/2);
 }
 sash('fixed',right-sashWidth,right,-trackCenter);
 const offset=state==='open'?travel:0,a=left+offset,c=left+sashWidth+offset;
 sash('leaf',a,c,trackCenter);
 // The pull and its backplate remain on the latch stile, on both faces.
 // They slide with the leaf and have no swing into the adjacent wall.
 const handleU=a+stile/2,handleZ=b+p.doorHandleHeight;
 const handleWidth=Math.min(stile-2*gap,p.doorHandlePlateWidth);
 const handleLength=p.doorHandlePlateHeight,bar=p.doorHandleDiameter/2;
 for(const side of [-1,1]){
  const surface=trackCenter+side*half,plate=surface+side*p.doorHandlePlateDepth;
  // The pull facing the fixed pane is recessed into the inter-track gap;
  // its complete cross-section clears the fixed sash during travel.
  const grip=side>0?bar:Math.min(bar,trackGap/3);
  const projection=side>0?p.doorHandleProjection:trackGap-gap-grip/2;
  const end=surface+side*projection;
  box('leaf.handle.plate.'+side,'hardware',handleU-handleWidth/2,handleZ-handleLength/2,Math.min(surface,plate),handleU+handleWidth/2,handleZ+handleLength/2,Math.max(surface,plate));
  for(const dz of [-1,1])box('leaf.handle.mount.'+side+'.'+dz,'hardware',handleU-bar/2,handleZ+dz*handleLength*.35-bar/2,Math.min(surface,end),handleU+bar/2,handleZ+dz*handleLength*.35+bar/2,Math.max(surface,end));
  box('leaf.handle.pull.'+side,'hardware',handleU-bar/2,handleZ-handleLength*.35,end-grip/2,handleU+bar/2,handleZ+handleLength*.35,end+grip/2);
 }
 return {profileId:aperture.id,recipe:'door.sliding-glass/1',status:'approved',visualState:state,
  travel,trackCenters:[-trackCenter,trackCenter],openingBounds:[l,r,b,t],parts};
}
