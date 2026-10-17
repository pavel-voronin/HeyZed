import {placementPoint} from './junction-solids.mjs';

export function platformEdges(){
 const edges=[];
 for(const axis of ['x','y'])for(let x=0;x<=(axis==='x'?2:3);x++)for(let y=0;y<=(axis==='x'?3:2);y++)edges.push({axis,x,y});
 return edges;
}
export function renderPoint(point){return [-point[0],point[1],point[2]];}
export function edgeRenderPoint(edge,point,size){return renderPoint(placementPoint(edge,point,size));}
export function projectedPlatform(size,project){
 const lines=platformEdges().map(edge=>({edge,a:project(edgeRenderPoint(edge,[0,0,0],size)),b:project(edgeRenderPoint(edge,[size,0,0],size))}));
 const points=lines.flatMap(line=>[line.a,line.b]),low=[0,1].map(i=>Math.min(...points.map(p=>p[i]))),high=[0,1].map(i=>Math.max(...points.map(p=>p[i]))),span=Math.max(high[0]-low[0],high[1]-low[1],1e-9);
 const map=p=>p.map((n,i)=>(n-(low[i]+high[i])/2)*3/span+1.5);
 return lines.map(line=>({...line,a:map(line.a),b:map(line.b)}));
}
export function planHitPolygon({a,b}){
 const dx=b[0]-a[0],dy=b[1]-a[1],length=Math.hypot(dx,dy);
 if(length<1e-9)return [[a[0]-.06,a[1]-.06],[a[0]+.06,a[1]-.06],[a[0]+.06,a[1]+.06],[a[0]-.06,a[1]+.06]];
 const u=[dx/length*.06,dy/length*.06],v=[-u[1],u[0]];
 return [[a[0]-u[0]+v[0],a[1]-u[1]+v[1]],[b[0]+u[0]+v[0],b[1]+u[1]+v[1]],[b[0]+u[0]-v[0],b[1]+u[1]-v[1]],[a[0]-u[0]-v[0],a[1]-u[1]-v[1]]];
}
