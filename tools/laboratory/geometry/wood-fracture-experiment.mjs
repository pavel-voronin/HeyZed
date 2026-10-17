import polygonClipping from 'polygon-clipping';
const rounded=points=>points.map(p=>p.map(v=>Math.round(v*1e9)/1e9));
const edges=p=>p.map((a,i)=>[a,p[(i+1)%p.length]]);
function distance(p,[a,b]){const x=b[0]-a[0],y=b[1]-a[1],length=x*x+y*y,t=length?Math.max(0,Math.min(1,((p[0]-a[0])*x+(p[1]-a[1])*y)/length)):0;return Math.hypot(p[0]-a[0]-t*x,p[1]-a[1]-t*y);}
const area=points=>Math.abs(points.reduce((s,a,i)=>{const b=points[(i+1)%points.length];return s+a[0]*b[1]-a[1]*b[0];},0))/2;
// Independent beam failure: the impact removes a middle span, while uneven
// grain tongues remain on the ends. The glass cell network is not an input.
export function fractureWoodPart(part,{seed=1,size=.56,strength=1,damage=1,impactPoint,bounds,frameWidth,cleared=false}){
 if(cleared)return [];
 if(damage<=0||strength<=0)return [part];
 const polygon=part.polygon||[[part.bounds[0],part.bounds[1]],[part.bounds[3],part.bounds[1]],[part.bounds[3],part.bounds[4]],[part.bounds[0],part.bounds[4]]],holes=part.holes||[],depth=part.depth||[part.bounds[2],part.bounds[5]];
 const low=[0,1].map(axis=>Math.min(...polygon.map(p=>p[axis]))),high=[0,1].map(axis=>Math.max(...polygon.map(p=>p[axis]))),axis=high[0]-low[0]>high[1]-low[1]?0:1,cross=1-axis,length=high[axis]-low[axis],width=high[cross]-low[cross],scale=Math.min(bounds[2]-bounds[0],bounds[3]-bounds[1]);
 const perpendicular=Math.abs(impactPoint[cross]-(low[cross]+high[cross])/2),radius=scale*(.18+size*.23)*Math.sqrt(strength);
 if(perpendicular>radius+width/2)return [part];
 let state=seed>>>0;for(const c of part.name)state=Math.imul(state^c.charCodeAt(0),16777619)>>>0;const random=()=>{state=(Math.imul(state,1664525)+1013904223)>>>0;return state/4294967296;};
 const location=Math.max(low[axis]+length*.25,Math.min(high[axis]-length*.25,impactPoint[axis])),gap=Math.min(length*.88,Math.max(frameWidth*2.5,radius*(1.6+random()*.4)*damage*Math.sqrt(strength))),amplitude=Math.min(frameWidth*1.8,gap*.35,length*.1);
 const map=(a,b)=>axis===0?[a,b]:[b,a],margin=frameWidth,parts=[];
 for(const end of ['min','max']){
  const sign=end==='min'?1:-1,cut=location-sign*gap/2,points=[];
  const strands=5+Math.floor(random()*3),weights=Array.from({length:strands},()=>.5+random()),total=weights.reduce((a,b)=>a+b,0);let position=low[cross];
  points.push(map(cut+(random()-.5)*amplitude,position-margin));
  for(let i=0;i<strands;i++){const next=position+width*weights[i]/total;points.push(map(cut-amplitude*(.3+random()*.4)*sign,position));points.push(map(cut+amplitude*(.4+random()*.6)*sign,(position+next)/2));position=next;}
  points.push(map(cut+(random()-.5)*amplitude,high[cross]+margin));
  const anchor=end==='min'?low[axis]-margin:high[axis]+margin,cutter=[map(anchor,low[cross]-margin),...points,map(anchor,high[cross]+margin)];
  for(const [i,region] of polygonClipping.intersection([[rounded(polygon),...holes.map(rounded)]],[[rounded(cutter)]]).entries()){
   if(area(region[0])<1e-10)continue;
   parts.push({name:part.name+'.end.'+end+'.'+i,sourceWood:part.name,grainAxis:axis,material:'frame',polygon:region[0].slice(0,-1),holes:region.slice(1).map(h=>h.slice(0,-1)),depth,woodFracture:true,fractureEdges:edges(region[0].slice(0,-1)).filter(([a,b])=>![polygon,...holes].flatMap(edges).some(edge=>distance(a,edge)<1e-8&&distance(b,edge)<1e-8))});
  }
 }
 return parts;
}
