import sectionLayouts from './window-section-layouts.json' with {type:'json'};
import {windowConstruction} from './window-constructions.mjs';
import {fracturePane,clipGlassPolygon} from './glass-break.mjs';
export const WINDOW_STATES=['closed','open','broken','glassRemoved'];
export function windowVisualState({open=false,destroyed=false,glassRemoved=false}={}){return destroyed?(glassRemoved?'glassRemoved':'broken'):(open?'open':'closed');}
export function buildStandardWindow(aperture,descriptor,visualState='closed'){
 if(!WINDOW_STATES.includes(visualState))throw Error('Unknown window state');
 const recipe=windowConstruction(aperture).previewRecipe;
 if(!recipe)return null;
 if(recipe.startsWith('storefront'))return buildStorefrontWindow(aperture,descriptor,visualState);
 if(sectionLayouts.profiles[aperture.id]?.rowCuts)return buildSectionedWindow(aperture,descriptor,visualState,sectionLayouts.profiles[aperture.id]);
 if(recipe==='fixed-section')return buildFixedWindow(aperture,descriptor,visualState);
 if(recipe==='arched-sash')return buildArchedWindow(aperture,descriptor,visualState);
 if(aperture.shape!=='rectangle')return null;
 const {profile:p}=descriptor,points=aperture.points;
 const l=Math.min(...points.map(v=>v[0])),r=Math.max(...points.map(v=>v[0])),b=Math.min(...points.map(v=>v[1])),t=Math.max(...points.map(v=>v[1]));
 const w=p.windowFrameWidth,o=p.windowFrameOverlap,d=p.wallThickness/2,g=p.windowPaneThickness/2,parts=[];
 function box(name,material,a,c,z0,z1,depth=d,center=0){if(c<=a||z1<=z0||depth<=0)throw Error('Invalid window part');parts.push({name,material,bounds:[a,z0,center-depth,c,z1,center+depth]});}
 function frame(name,a,c,z0,z1,depth,center=0,missing=[]){box(name+'.left','frame',a,a+w,z0,z1,depth,center);box(name+'.right','frame',c-w,c,z0,z1,depth,center);if(!missing.includes('bottom'))box(name+'.bottom','frame',a+w,c-w,z0,z0+w,depth,center);if(!missing.includes('top'))box(name+'.top','frame',a+w,c-w,z1-w,z1,depth,center);}
 // The casing fits the hole. Only the face trims overlap the wall, outside
 // its structural depth, so their overlap does not intersect the wall solid.
 frame('casing',l,r,b,t,d);
 const trim=p.windowFrameProjection/2;
 frame('trim.front',l-o,r+o,aperture.bottomPlacement==='floor'?b:b-o,t+o,trim,d+trim);
 frame('trim.back',l-o,r+o,aperture.bottomPlacement==='floor'?b:b-o,t+o,trim,-d-trim);
 const a=l+w,c=r-w,z0=b+w,z1=t-w,mid=(z0+z1)/2;
 const damaged=visualState==='broken'||visualState==='glassRemoved';
 for(const [name,start,end,center] of [['lower',z0,mid,p.windowSashTrackOffset],['upper',mid,z1,-p.windowSashTrackOffset]]){
  const travel=visualState==='open'&&name==='lower'?mid-z0:0,low=start+travel,high=end+travel;
  frame(name,a,c,low,high,p.windowSashHalfDepth,center,damaged?[name==='lower'?'top':'bottom']:[]);
  const pane={name:name+'.glass',sash:name,material:'glass',bounds:[a+w,low+w,center-g,c-w,high-w,center+g]};
  if(!damaged)parts.push(pane);
  else parts.push(...fracturePane(pane,[a+w,c-w,z0+w,z1-w],visualState==='glassRemoved',p));
 }
 if(visualState==='broken'){
  // Short jagged ends of the two removed meeting rails stay at the jambs.
  for(const [rail,low,high,center] of [['lower',mid-w,mid,p.windowSashTrackOffset],['upper',mid,mid+w,-p.windowSashTrackOffset]])for(const [side,edge,direction] of [['left',a+w,1],['right',c-w,-1]]){
   const length=w*(rail==='lower'?(side==='left'?1.15:.8):(side==='left'?.7:1.3));
   const polygon=[[edge,low],[edge+direction*length*.82,low],[edge+direction*length*.54,low+(high-low)*.39],[edge+direction*length,high],[edge,high]];
   parts.push({name:rail+'.splinter.'+side,material:'frame',polygon,holes:[],depth:[center-p.windowSashHalfDepth,center+p.windowSashHalfDepth]});
  }
 }
 return {profileId:aperture.id,recipe:'window.two-sash/2',status:windowConstruction(aperture).status,visualState,parts};
}

// Parallel offsets of the shared convex aperture contour retain its arch shape.
function offsetContour(points,distance){
 if(typeof distance==='number'&&distance>0){
  // Intersect shifted half-planes: short segments may disappear at an inset
  // corner. Moving vertices alone folds those segments across the glass.
  let result=points;
  for(let i=0;i<points.length;i++){
   const a=points[i],b=points[(i+1)%points.length],dx=b[0]-a[0],dy=b[1]-a[1],length=Math.hypot(dx,dy),origin=[a[0]+distance*dy/length,a[1]-distance*dx/length],side=p=>dx*(p[1]-origin[1])-dy*(p[0]-origin[0]),input=result;result=[];
   input.forEach((q,j)=>{const p=input[(j+input.length-1)%input.length],dp=side(p),dq=side(q),pin=dp<=1e-12,qin=dq<=1e-12;
    if(pin!==qin){const f=dp/(dp-dq);result.push([p[0]+f*(q[0]-p[0]),p[1]+f*(q[1]-p[1])]);}if(qin)result.push(q);
   });
  }
  return result.filter((p,i)=>Math.hypot(p[0]-result[(i+result.length-1)%result.length][0],p[1]-result[(i+result.length-1)%result.length][1])>1e-10);
 }
 return points.map((q,i)=>{
  const a=points[(i+points.length-1)%points.length],b=points[(i+1)%points.length];
  const v=[q[0]-a[0],q[1]-a[1]],w=[b[0]-q[0],b[1]-q[1]],vl=Math.hypot(...v),wl=Math.hypot(...w);
  const n=[v[1]/vl,-v[0]/vl],m=[w[1]/wl,-w[0]/wl];
  const dn=typeof distance==='function'?distance(a,q):distance,dm=typeof distance==='function'?distance(q,b):distance;
  const det=n[0]*m[1]-n[1]*m[0];
  if(Math.abs(det)<1e-10)return [q[0]+n[0]*dn,q[1]+n[1]*dn];
  return [q[0]+(dn*m[1]-n[1]*dm)/det,q[1]+(n[0]*dm-dn*m[0])/det];
 });
}
function buildArchedWindow(aperture,descriptor,visualState){
 const p=descriptor.profile,w=p.windowFrameWidth,d=p.wallThickness/2,g=p.windowPaneThickness/2,o=p.windowFrameOverlap,trim=p.windowFrameProjection/2;
 const outer=aperture.points,inner=offsetContour(outer,w),parts=[];
 const l=Math.min(...outer.map(v=>v[0])),r=Math.max(...outer.map(v=>v[0])),b=Math.min(...outer.map(v=>v[1])),t=Math.max(...outer.map(v=>v[1])),spring=aperture.contour.segments[0].to[1];
 const damaged=visualState==='broken'||visualState==='glassRemoved';
 function ring(name,polygon,hole,depth,center=0){parts.push({name,material:'frame',polygon,holes:[hole],depth:[center-depth,center+depth]});}
 function box(name,a,c,z0,z1,depth,center=0){parts.push({name,material:'frame',bounds:[a,z0,center-depth,c,z1,center+depth]});}
 ring('casing.arch',outer,inner,d);
 ring('trim.front.arch',offsetContour(outer,-o),inner,trim,d+trim);
 ring('trim.back.arch',offsetContour(outer,-o),inner,trim,-d-trim);
 const arch=clipGlassPolygon([...inner].reverse(),[[l,spring],[r,spring],[r,t],[l,t]]).reverse(),archInner=offsetContour(arch,w),archCenter=-p.windowSashTrackOffset;
 const breakBounds=[l+2*w,r-2*w,b+2*w,Math.max(...archInner.map(v=>v[1]))],maskFrameContour=offsetContour(outer,2*w);
 ring('arch.frame',arch,archInner,p.windowSashHalfDepth,archCenter);
 function glass(pane,maskBounds){
  if(!damaged||(aperture.id==='window-10-0'&&pane.name==='arch.glass'))parts.push(pane);else parts.push(...fracturePane({...pane,breakMaskId:aperture.id,maskFrameContour},breakBounds,visualState==='glassRemoved',p));
 }
 const archBounds=[Math.min(...archInner.map(v=>v[0])),spring+w,archCenter-g,Math.max(...archInner.map(v=>v[0])),Math.max(...archInner.map(v=>v[1])),archCenter+g];
 glass({name:'arch.glass',sash:'upper',material:'glass',polygon:archInner,supportContour:archInner,holes:[],depth:[archCenter-g,archCenter+g],bounds:archBounds},[archBounds[0],archBounds[3],archBounds[1],archBounds[4]]);
 const a=l+w,c=r-w,z0=b+w,z1=spring,mid=(z0+z1)/2;
 for(const [name,start,end,center] of [['lower',z0,mid,p.windowSashTrackOffset],['upper',mid,z1,archCenter]]){
  const travel=visualState==='open'&&name==='lower'?mid-z0:0,low=start+travel,high=end+travel;
  box(name+'.left',a,a+w,low,high,p.windowSashHalfDepth,center);box(name+'.right',c-w,c,low,high,p.windowSashHalfDepth,center);
  if(!damaged||name==='lower')box(name+'.bottom',a+w,c-w,low,low+w,p.windowSashHalfDepth,center);
  if(!damaged||name==='upper')box(name+'.top',a+w,c-w,high-w,high,p.windowSashHalfDepth,center);
  glass({name:name+'.glass',sash:name,material:'glass',supportEdges:[[[a+w,low+w],[a+w,high-w]],[[c-w,low+w],[c-w,high-w]],name==='lower'?[[a+w,low+w],[c-w,low+w]]:[[a+w,high-w],[c-w,high-w]]],bounds:[a+w,low+w,center-g,c-w,high-w,center+g]},[a+w,c-w,z0+w,z1-w]);
 }
 return {profileId:aperture.id,recipe:'window.arched-sash/1',status:windowConstruction(aperture).status,visualState,parts};
}

function buildFixedWindow(aperture,descriptor,visualState){
 if(visualState==='open')return null;
 const p=descriptor.profile,w=p.windowFrameWidth,d=p.wallThickness/2,g=p.windowPaneThickness/2,trim=p.windowFrameProjection/2,outer=aperture.points,parts=[],joins=aperture.joins||[];
 const {tileSize,floorHeight}=descriptor.metric;
 function joined(a,b){return joins.some(edge=>{const axis=edge==='left'||edge==='right'?0:1,value=aperture.joinPlanes?.[edge]??(edge==='right'?tileSize:edge==='top'?floorHeight:0);return Math.abs(a[axis]-value)<1e-9&&Math.abs(b[axis]-value)<1e-9;});}
 const inner=offsetContour(outer,w);
 const overlap=offsetContour(outer,(a,b)=>joined(a,b)?0:-p.windowFrameOverlap);
 for(const [name,edge,depth,center] of [['casing',outer,d,0],['trim.front',overlap,trim,d+trim],['trim.back',overlap,trim,-d-trim]])
  parts.push({name:name+'.section',material:'frame',polygon:edge,holes:[inner],depth:[center-depth,center+depth]});
 const l=Math.min(...inner.map(v=>v[0])),r=Math.max(...inner.map(v=>v[0])),b=Math.min(...inner.map(v=>v[1])),t=Math.max(...inner.map(v=>v[1])),mid=(b+t)/2,cx=(l+r)/2;
 const ccw=[...inner].reverse(),damaged=visualState==='broken'||visualState==='glassRemoved';
 // A fixed glazing section has solid muntins. The broken passage loses their
 // centre; each remaining fragment is cut from its own original glass plate.
 const divider=w/2;
 for(const [name,region] of [['vertical',[[cx-divider,b],[cx+divider,b],[cx+divider,t],[cx-divider,t]]],['horizontal',[[l,mid-divider],[r,mid-divider],[r,mid+divider],[l,mid+divider]]]]){
  const polygon=clipGlassPolygon(region,ccw);
  if(polygon.length<3)continue;
  const shape={name:'muntin.'+name,material:'frame',polygon,holes:[],depth:[-p.windowSashHalfDepth,p.windowSashHalfDepth]};
  if(!damaged)parts.push(shape);
  else if(visualState==='broken'){
   const axis=name==='vertical'?1:0,other=1-axis;
   for(const end of ['min','max']){
    const peak=polygon.reduce((a,b)=>end==='min'?(b[axis]<a[axis]?b:a):(b[axis]>a[axis]?b:a)),sign=end==='min'?1:-1;
    const zone=[[peak[axis]-sign*w,peak[other]-w],[peak[axis]+sign*w*1.7,peak[other]-w],[peak[axis]+sign*w*.7,peak[other]],[peak[axis]+sign*w*1.2,peak[other]+w],[peak[axis]-sign*w,peak[other]+w]].map(v=>axis===0?v:[v[1],v[0]]);
    const area=zone.reduce((sum,a,i)=>{const b=zone[(i+1)%zone.length];return sum+a[0]*b[1]-b[0]*a[1];},0);
    const stub=clipGlassPolygon(polygon,area<0?zone.reverse():zone);
    if(stub.length>=3)parts.push({...shape,name:shape.name+'.splinter.'+end,polygon:stub});
   }
  }
 }
 for(const [row,z0,z1] of [['lower',b,mid-divider],['upper',mid+divider,t]])for(const [side,u0,u1] of [['left',l,cx-divider],['right',cx+divider,r]]){
  const polygon=clipGlassPolygon([[u0,z0],[u1,z0],[u1,z1],[u0,z1]],ccw);if(polygon.length<3)continue;
  const bounds=[Math.min(...polygon.map(v=>v[0])),Math.min(...polygon.map(v=>v[1])),-g,Math.max(...polygon.map(v=>v[0])),Math.max(...polygon.map(v=>v[1])),g];
  const pane={name:row+'.'+side+'.glass',sash:row,material:'glass',polygon,breakMaskId:aperture.id,supportContour:inner,maskFrameContour:inner,holes:[],depth:[-g,g],bounds};
  if(!damaged)parts.push(pane);else parts.push(...fracturePane(pane,[l,r,b,t],visualState==='glassRemoved',p));
 }
 return {profileId:aperture.id,recipe:'window.fixed-section/1',status:windowConstruction(aperture).status,visualState,parts};
}


function buildStorefrontWindow(aperture,descriptor,visualState){
 const p=descriptor.profile,width=descriptor.metric.tileSize,top=Math.max(...aperture.points.map(q=>q[1])),bottom=top/4;
 const glazing={...aperture,points:[[0,bottom],[0,top],[width,top],[width,bottom]]};
 const recipe=buildSectionedWindow(glazing,descriptor,visualState,{columns:2,rowCuts:[1/3,2/3],fixedRows:[]});
 if(!recipe)return null;
 recipe.parts.push({name:'storefront.plinth',material:'frame',bounds:[0,0,-p.wallThickness/2,descriptor.metric.tileSize,bottom,p.wallThickness/2],breakable:false});
 if(aperture.reviewRecipe==='storefront-corner'){
  const pivot=[aperture.cornerReverse?width:0,0,0],angleDegrees=aperture.cornerReverse?-90:90;
  recipe.parts.push(...recipe.parts.map(part=>({...part,name:'corner.'+part.name,transform:{pivot,angleDegrees}})));
 }
 return {...recipe,recipe:'window.storefront-proposal/1'};
}

function buildSectionedWindow(aperture,descriptor,visualState,layout){
 if(visualState==='open')return null;
 const p=descriptor.profile,w=p.windowFrameWidth,g=p.windowPaneThickness/2,d=p.wallThickness/2,trim=p.windowFrameProjection/2;
 const outer=aperture.points,inner=offsetContour(outer,w),ccw=[...inner].reverse(),parts=[];
 const overlap=offsetContour(outer,(a,b)=>aperture.bottomPlacement==='floor'&&Math.abs(a[1])<1e-9&&Math.abs(b[1])<1e-9?0:-p.windowFrameOverlap);
 for(const [name,polygon,depth,center] of [['casing.section',outer,d,0],['trim.front.section',overlap,trim,d+trim],['trim.back.section',overlap,trim,-d-trim]])parts.push({name,material:'frame',polygon,holes:[inner],depth:[center-depth,center+depth]});
 const l=Math.min(...inner.map(p=>p[0])),r=Math.max(...inner.map(p=>p[0])),b=Math.min(...inner.map(p=>p[1])),t=Math.max(...inner.map(p=>p[1]));
 const cuts=layout.rowCuts.map(f=>b+(t-b)*f);
 if(layout.archBoundary)cuts.push(aperture.contour.segments[0].to[1]);
 const rows=[b,...cuts,t],columns=Array.from({length:layout.columns+1},(_,i)=>l+(r-l)*i/layout.columns),damaged=['broken','glassRemoved'].includes(visualState);
 const fixed=row=>layout.fixedRows.includes(row);
 function clipped(region){return clipGlassPolygon(region,ccw);}
 function wood(name,region,breakable){const polygon=clipped(region);if(polygon.length<3)return;const part={name,material:'frame',polygon,holes:[],depth:[-p.windowSashHalfDepth,p.windowSashHalfDepth],breakable};if(!damaged||!breakable)parts.push(part);}
 for(let row=0;row<rows.length-1;row++){
  const low=rows[row]+(row?w/2:0),high=rows[row+1]-(row<rows.length-2?w/2:0);
  for(let col=1;col<columns.length-1;col++)wood('section.'+row+'.vertical.'+col,[[columns[col]-w/2,low],[columns[col]+w/2,low],[columns[col]+w/2,high],[columns[col]-w/2,high]],!fixed(row));
  for(let col=0;col<columns.length-1;col++){
   const left=columns[col]+(col?w/2:0),right=columns[col+1]-(col<columns.length-2?w/2:0),polygon=clipped([[left,low],[right,low],[right,high],[left,high]]);if(polygon.length<3)continue;
   const pane={name:'section.'+row+'.'+col+'.glass',sash:fixed(row)?'fixed':'section.'+row,material:'glass',polygon,holes:[],depth:[-g,g],bounds:[Math.min(...polygon.map(p=>p[0])),Math.min(...polygon.map(p=>p[1])),-g,Math.max(...polygon.map(p=>p[0])),Math.max(...polygon.map(p=>p[1])),g],supportContour:polygon,breakable:!fixed(row),breakMaskId:aperture.id,maskFrameContour:inner};
   if(!damaged||fixed(row))parts.push(pane);else parts.push(...fracturePane(pane,[l,r,b,t],visualState==='glassRemoved',p));
  }
 }
 for(let cut=0;cut<cuts.length;cut++)wood('section.divider.'+cut,[[l,cuts[cut]-w/2],[r,cuts[cut]-w/2],[r,cuts[cut]+w/2],[l,cuts[cut]+w/2]],!fixed(cut)&&!fixed(cut+1));
 return {profileId:aperture.id,recipe:'window.section-grid/1',status:windowConstruction(aperture).status,visualState,parts};
}
