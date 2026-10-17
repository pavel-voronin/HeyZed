import breakMask from './window-break-mask.json' with {type:'json'};
import nativeMasks from './window-break-masks.json' with {type:'json'};
import polygonClipping from 'polygon-clipping';
const area=rows=>Math.abs(rows.reduce((sum,p,i)=>{const q=rows[(i+1)%rows.length];return sum+p[0]*q[1]-q[0]*p[1];},0))/2;
// Clip retained mask regions to the actual closed glass plate. Depth and sash
// ownership come only from that plate, never from a fragment's position.
export function clipGlassPolygon(rows,boundary){
 let result=rows;
 for(let i=0;i<boundary.length;i++){
  const a=boundary[i],b=boundary[(i+1)%boundary.length],side=p=>(b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]);
  const input=result;result=[];
  input.forEach((q,j)=>{const p=input[(j+input.length-1)%input.length],dp=side(p),dq=side(q),pin=dp>=-1e-12,qin=dq>=-1e-12;
   if(pin!==qin){const t=dp/(dp-dq);result.push([p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])]);}
   if(qin)result.push(q);
  });
 }
 return result.filter((p,i)=>{const q=result[(i+result.length-1)%result.length];return Math.hypot(q[0]-p[0],q[1]-p[1])>1e-10;});
}
export function fracturePane(pane,maskBounds,cleared,profile){
 if(cleared&&pane.sash==='lower'&&!pane.breakMaskId)return [];
 const [l,b,d0,r,t,d1]=pane.bounds,[ml,mr,mb,mt]=maskBounds;
 const signed=rows=>rows.reduce((sum,p,i)=>{const q=rows[(i+1)%rows.length];return sum+p[0]*q[1]-q[0]*p[1];},0);
 const plate=pane.polygon?(signed(pane.polygon)<0?[...pane.polygon].reverse():pane.polygon):[[l,b],[r,b],[r,t],[l,t]],pieces=[];
 const w=profile.windowFrameWidth;
 const keep=[[[l+(r-l)*.23,t],[l+(r-l)*.23+w*2.6,t-w*3.8],[l+(r-l)*.23+w*3.2,t]],[[l+(r-l)*.7,t],[l+(r-l)*.7+w*1.4,t-w*3.4],[l+(r-l)*.7+w*2.8,t]],[[r,t-w*2.6],[r-w*2.6,t-w*3.3],[r,t-w*5.2]]];
 if(pane.breakMaskId){
  const source=nativeMasks.profiles[pane.breakMaskId];
  const regions=source?.retainedRegions||breakMask.retainedRegions.map(polygon=>({polygon,holes:[]}));
  const nearest=(point,edges)=>edges.reduce((best,[a,b])=>{const dx=b[0]-a[0],dy=b[1]-a[1],length=dx*dx+dy*dy;const f=length?Math.max(0,Math.min(1,((point[0]-a[0])*dx+(point[1]-a[1])*dy)/length)):0,q=[a[0]+f*dx,a[1]+f*dy],distance=Math.hypot(q[0]-point[0],q[1]-point[1]);return !best||distance<best.distance?{point:q,distance}:best;},null);
  const edges=contour=>contour.map((p,i)=>[p,contour[(i+1)%contour.length]]);
  const supports=pane.supportEdges||edges(pane.supportContour||plate),frame=edges(pane.maskFrameContour||pane.supportContour||plate);
  const round=p=>p.map(v=>Math.round(v*1e9)/1e9);
  const clean=rows=>{const points=rows.map(round).filter((p,i,a)=>!i||p.some((v,k)=>v!==a[i-1][k]));if(points.length>1&&points[0].every((v,k)=>v===points.at(-1)[k]))points.pop();return points;};
  const point=([u,z],anchor=false)=>{const p=[ml+u*(mr-ml),mb+z*(mt-mb)];return round(anchor?nearest(p,frame).point:p);};
  const attached=polygon=>polygon.some(p=>nearest(p,supports).distance<1e-8);
  // Full boolean intersection handles concave remnants, holes and disconnected
  // pieces. Independent half-plane clipping can bridge separate shards.
  const projectRegions=rows=>rows.length?polygonClipping.union(...rows.map(region=>[[clean(region.polygon.map((p,i)=>point(p,region.anchors?.[i]||false))),...region.holes.map(h=>clean(h.map(p=>point(p))))]])):[];
  const mask=projectRegions(regions);
  const broken=polygonClipping.intersection(mask,[[clean(plate)]]);
  let remaining=broken;
  if(cleared){
   const zones=[];
   for(const polygon of broken){
    const contacts=polygon[0].filter(p=>nearest(p,supports).distance<1e-8);if(!contacts.length)continue;
    const peak=contacts.reduce((a,b)=>b[1]>a[1]?b:a);
    if(peak[1]<b+(t-b)*.65)continue;
    zones.push([[[peak[0]-w,peak[1]+w],[peak[0],peak[1]-w*1.5],[peak[0]+w,peak[1]+w]]]);
   }
   const chips=pane.sash!=='lower'&&zones.length?polygonClipping.intersection(broken,polygonClipping.union(...zones)):[];
   const fixed=polygonClipping.intersection(projectRegions(source?.persistentRegions||[]),[[clean(plate)]]);
   remaining=polygonClipping.union(chips,fixed);
  }
  for(const region of remaining){
   const polygon=clean(region[0]),holes=region.slice(1).map(clean).filter(h=>h.length>=3&&area(h)>1e-12);
   if(area(polygon)-holes.reduce((sum,h)=>sum+area(h),0)<1e-9||!attached(polygon))continue;
   pieces.push({name:pane.name+'.fragment.'+pieces.length,sash:pane.sash,sourcePane:pane.name,maskSource:source?.brokenSource||null,material:'glass',polygon,holes,depth:[d0,d1]});
  }
  return pieces;
 }
 // Preserve the accepted 9/A and 9/B glass construction.
 for(const region of breakMask.retainedRegions){
  const original=clipGlassPolygon(region.map(([u,z])=>[ml+u*(mr-ml),mb+z*(mt-mb)]),plate);
  for(const polygon of cleared?keep.map(zone=>clipGlassPolygon(original,zone)):[original]){
   if(polygon.length<3||area(polygon)<1e-9)continue;
   pieces.push({name:pane.name+'.fragment.'+pieces.length,sash:pane.sash,sourcePane:pane.name,material:'glass',polygon,holes:[],depth:[d0,d1]});
  }
 }
 return pieces;
}
