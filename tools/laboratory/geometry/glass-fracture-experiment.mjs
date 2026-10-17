import polygonClipping from 'polygon-clipping';
import {fractureWoodPart} from './wood-fracture-experiment.mjs';
const rounded=points=>points.map(p=>p.map(v=>Math.round(v*1e9)/1e9));
const area=points=>Math.abs(points.reduce((s,a,i)=>{const b=points[(i+1)%points.length];return s+a[0]*b[1]-a[1]*b[0];},0))/2;
const edges=p=>p.map((a,i)=>[a,p[(i+1)%p.length]]);
function distance(p,[a,b]){const x=b[0]-a[0],y=b[1]-a[1],q=x*x+y*y,t=q?Math.max(0,Math.min(1,((p[0]-a[0])*x+(p[1]-a[1])*y)/q)):0;return Math.hypot(p[0]-a[0]-t*x,p[1]-a[1]-t*y);}
export function plateContour(p){const [l,b,,r,t]=p.bounds||[];return p.polygon||[[l,b],[r,b],[r,t],[l,t]];}
export function glazingBounds(panes){const points=panes.flatMap(plateContour);return [Math.min(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),Math.max(...points.map(p=>p[0])),Math.max(...points.map(p=>p[1]))];}
// One shared network: radial cracks intersect irregular cross-crack rings.
// Neighbouring cells share the exact same vertices. No raster mask, radial
// remapping of a sprite, artificial scallops, or independent pane impacts.
export function createFracturePattern(bounds,{seed=1,size=.56,impact=[.5,.5],strength=1,grip=1}={}){
 const [l,b,r,t]=bounds,c=[l+(r-l)*impact[0],b+(t-b)*impact[1]],scale=Math.min(r-l,t-b),extent=Math.hypot(r-l,t-b)*3;
 let state=seed>>>0;const random=()=>{state=(Math.imul(state,1664525)+1013904223)>>>0;return state/4294967296;};
 const count=32+Math.floor(random()*18),phase=random()*Math.PI*2;
 // Unequal angular gaps create dense crack bundles beside broad shards.
 const gaps=Array.from({length:count},()=>.3+Math.pow(random(),2)*3.5),total=gaps.reduce((a,b)=>a+b,0),angles=[];let angle=phase;
 for(const gap of gaps){angles.push(angle);angle+=gap/total*Math.PI*2;}
 const rays=angles.map((angle,i)=>{const first=scale*(.06+random()*.15),second=Math.max(first*1.45,scale*(.3+random()*.45)*size/.56),bend=Math.min(gaps[i],gaps[(i+count-1)%count])/total*Math.PI*2*(random()-.5)*.6;
  const dx=Math.cos(angle),dy=Math.sin(angle),boundary=Math.min(dx>0?(r-c[0])/dx:(l-c[0])/dx,dy>0?(t-c[1])/dy:(b-c[1])/dy),third=Math.max(second*1.25,boundary*(.65+random()*.2));
  return [[first,angle],[second,angle+bend],[third,angle],[extent,angle]].map(([radius,direction])=>[c[0]+Math.cos(direction)*radius,c[1]+Math.sin(direction)*radius]);});
 const cells=[];
 for(let i=0;i<count;i++){const a=rays[i],b=rays[(i+1)%count];cells.push([c,a[0],b[0]]);for(let ring=0;ring<2;ring++)cells.push([a[ring],a[ring+1],b[ring+1],b[ring]]);}
 // Half the fine radial cracks terminate at the outer cross crack.
 for(let i=0;i<count;i+=2){const a=rays[i],b=rays[(i+1)%count],c=rays[(i+2)%count];cells.push(i+1<count?[a[2],a[3],c[3],c[2],b[2]]:[a[2],a[3],b[3],b[2]]);}
 return {bounds,cells:cells.map(rounded),seed,size,impactPoint:c,radialAngles:angles,strength,grip};
}

function supportingEdges(pane,bounds){const [l,b,r,t]=bounds;return edges(plateContour(pane)).filter(([a,c])=>{
 const vertical=Math.abs(a[0]-c[0])<1e-8,horizontal=Math.abs(a[1]-c[1])<1e-8;
 return (!vertical&&!horizontal)||(vertical&&(Math.abs(a[0]-l)<1e-8||Math.abs(a[0]-r)<1e-8))||(horizontal&&(Math.abs(a[1]-b)<1e-8||Math.abs(a[1]-t)<1e-8));
 });}
// Dimensionless experimental attachment model, not calibrated material physics.
// The reference length comes from the actual glazing, never a fixed metre size.
export const ATTACHMENT_MODEL={loadLimit:1,areaLoad:.35,impulseBase:.65,impulsePeak:1.8,bondScatter:.4,remnantLimit:.22,slenderLimit:8};
function keyedRandom(seed,key){let hash=seed>>>0;for(const character of key)hash=Math.imul(hash^character.charCodeAt(0),16777619)>>>0;hash=Math.imul(hash^(hash>>>16),2246822507)>>>0;return (hash^(hash>>>13))>>>0;}
function centroid(polygon){let sum=0,x=0,y=0;for(const [a,b] of edges(polygon)){const cross=a[0]*b[1]-b[0]*a[1];sum+=cross;x+=(a[0]+b[0])*cross;y+=(a[1]+b[1])*cross;}return [x/(3*sum),y/(3*sum)];}
export function evaluateShardAttachment(polygon,holes,support,pattern,key=''){
 const contacts=edges(polygon).filter(([a,b])=>Math.hypot(a[0]-b[0],a[1]-b[1])>1e-8&&support.some(edge=>distance(a,edge)<1e-8&&distance(b,edge)<1e-8));
 const contactLength=contacts.reduce((sum,[a,b])=>sum+Math.hypot(a[0]-b[0],a[1]-b[1]),0),surfaceArea=area(polygon)-holes.reduce((sum,h)=>sum+area(h),0);
 if(!contacts.length)return {contactLength,surfaceArea,loadRatio:Infinity,outcome:'fall',contacts};
 const c=centroid(polygon),scale=Math.min(pattern.bounds[2]-pattern.bounds[0],pattern.bounds[3]-pattern.bounds[1]);
 const lever=Math.min(...contacts.map(edge=>distance(c,edge))),reach=Math.max(...polygon.map(p=>Math.min(...contacts.map(edge=>distance(p,edge)))));
 const impactDistance=Math.min(...contacts.map(edge=>distance(pattern.impactPoint,edge)));
 const random=keyedRandom(pattern.seed,key)/4294967296,bondStrength=1-ATTACHMENT_MODEL.bondScatter/2+random*ATTACHMENT_MODEL.bondScatter;
 const force=pattern.strength??1,grip=pattern.grip??1;
 const impulse=ATTACHMENT_MODEL.impulseBase+ATTACHMENT_MODEL.impulsePeak*Math.exp(-impactDistance/(scale*(.35+pattern.size)));
 const perimeter=edges(polygon).reduce((sum,[a,b])=>sum+Math.hypot(a[0]-b[0],a[1]-b[1]),0),effectiveWidth=2*surfaceArea/perimeter,span=Math.max(...polygon.flatMap((a,i)=>polygon.slice(i+1).map(b=>Math.hypot(a[0]-b[0],a[1]-b[1])))),slenderness=span/effectiveWidth;
 const loadRatio=Math.max(slenderness/ATTACHMENT_MODEL.slenderLimit,surfaceArea/(contactLength*scale)*(ATTACHMENT_MODEL.areaLoad+lever/contactLength)*impulse*force/(bondStrength*grip));
 const releaseLoad=force*impulse/(grip*(3+random*3));
 const outcome=releaseLoad>=1?'fall':loadRatio<=ATTACHMENT_MODEL.loadLimit?'keep':reach>contactLength*.45&&random>.22?'snap':'fall';
 return {contactLength,surfaceArea,lever,reach,impactDistance,bondStrength,impulse,effectiveWidth,slenderness,loadRatio,releaseLoad,outcome,contacts};
}
function supportedRemnant(polygon,holes,attachment,pattern,key){
 const anchor=attachment.contacts.reduce((a,b)=>Math.hypot(b[1][0]-b[0][0],b[1][1]-b[0][1])>Math.hypot(a[1][0]-a[0][0],a[1][1]-a[0][1])?b:a),[a,b]=anchor;
 const length=Math.hypot(b[0]-a[0],b[1]-a[1]),c=centroid(polygon),mid=[(a[0]+b[0])/2,(a[1]+b[1])/2];
 let normal=[-(b[1]-a[1])/length,(b[0]-a[0])/length];if((c[0]-mid[0])*normal[0]+(c[1]-mid[1])*normal[1]<0)normal=normal.map(v=>-v);
 const scale=Math.min(pattern.bounds[2]-pattern.bounds[0],pattern.bounds[3]-pattern.bounds[1]),random=keyedRandom(pattern.seed,key+'.snap')/4294967296;
 const initialDepth=Math.min(attachment.reach*.6,scale*ATTACHMENT_MODEL.remnantLimit,length*(.65+random*.7)/Math.sqrt(attachment.impulse),length*.95);
 const depth=initialDepth/Math.sqrt(Math.max(1,attachment.loadRatio));
 const along=.2+random*.6,tip=[a[0]+(b[0]-a[0])*along+normal[0]*depth,a[1]+(b[1]-a[1])*along+normal[1]*depth];
 // Extend the cutter base outside the pane to preserve the full bonded edge.
 const margin=scale*1e-5,baseA=[a[0]-normal[0]*margin-(b[0]-a[0])*.001,a[1]-normal[1]*margin-(b[1]-a[1])*.001],baseB=[b[0]-normal[0]*margin+(b[0]-a[0])*.001,b[1]-normal[1]*margin+(b[1]-a[1])*.001];
 return polygonClipping.intersection([[polygon,...holes]],[[rounded([baseA,baseB,tip])]]).filter(region=>{if(area(region[0])<1e-10)return false;const remaining=evaluateShardAttachment(region[0].slice(0,-1),region.slice(1).map(h=>h.slice(0,-1)),attachment.contacts,pattern,key);return remaining.outcome==='keep';});
}
export function breakGlassPlate(pane,options={}){
 const [l,b,d0,r,t,d1]=pane.bounds,pattern=options.pattern||createFracturePattern([l,b,r,t],options),plate=plateContour(pane),support=supportingEdges(pane,pattern.bounds),parts=[];
 for(let cell=0;cell<pattern.cells.length;cell++)for(const [component,region] of polygonClipping.intersection([[rounded(plate),...(pane.holes||[]).map(rounded)]],[[pattern.cells[cell]]]).entries()){
  let polygon=region[0].slice(0,-1),holes=region.slice(1).map(h=>h.slice(0,-1));if(area(polygon)<1e-10)continue;
  const attachment=evaluateShardAttachment(polygon,holes,support,pattern,pane.name+'.'+cell+'.'+component);
  const remnants=attachment.outcome==='snap'?supportedRemnant(polygon,holes,attachment,pattern,pane.name+'.'+cell):[];
  const secondaryCrack=remnants.flatMap(region=>edges(region[0].slice(0,-1)).filter(([a,b])=>!edges(polygon).some(edge=>distance(a,edge)<1e-8&&distance(b,edge)<1e-8)));
  if(!options.cracked){
   if(attachment.outcome==='fall')continue;
   if(attachment.outcome==='snap'){if(!remnants.length)continue;polygon=remnants[0][0].slice(0,-1);holes=remnants[0].slice(1).map(h=>h.slice(0,-1));}
  }
  const contacts=polygon.filter(p=>support.some(edge=>distance(p,edge)<1e-8));
  if(options.cleared){const peak=contacts.reduce((a,b)=>!a||b[1]>a[1]?b:a,null);if(!peak||peak[1]<pattern.bounds[1]+(pattern.bounds[3]-pattern.bounds[1])*.75)continue;
   const s=Math.min(pattern.bounds[2]-pattern.bounds[0],pattern.bounds[3]-pattern.bounds[1])*.025;
   const zones=polygonClipping.intersection([[polygon,...holes]],[[[[peak[0]-s,peak[1]+s],[peak[0]+s,peak[1]+s],[peak[0],peak[1]-s*2]]]]);
   if(!zones.length)continue;polygon=zones[0][0].slice(0,-1);holes=zones[0].slice(1).map(h=>h.slice(0,-1));if(area(polygon)<1e-10)continue;
  }
  parts.push({name:pane.name+'.fracture.'+cell+'.'+parts.length,sourcePane:pane.name,sash:pane.sash,material:'glass',polygon,holes,depth:[d0,d1],crackCell:cell,crackComponent:component,attachment:{...attachment,contacts:undefined},secondaryCrack});
 }
 return parts;
}
export function fractureWindow(recipe,aperture,descriptor,options){
 if(options.state==='closed')return recipe;
 const panes=recipe.parts.filter(p=>p.material==='glass'),bounds=glazingBounds(panes),pattern=createFracturePattern(bounds,options),cracked=options.state==='cracked',cleared=options.state==='glassRemoved';
 const parts=recipe.parts.flatMap(part=>{
  if(options.preserveParts?.includes(part.name))return [part];
  if(part.material==='glass')return breakGlassPlate(part,{pattern,cracked,cleared});
  if(cracked||part.name.startsWith('casing')||part.name.startsWith('trim.'))return [part];
  return fractureWoodPart(part,{seed:options.seed,size:options.size,strength:options.strength,damage:options.woodDamage,impactPoint:pattern.impactPoint,bounds,frameWidth:descriptor.profile.windowFrameWidth,cleared});
 });
 const crackLines=cracked?parts.filter(p=>p.material==='glass').flatMap(p=>[...edges(p.polygon),...(p.secondaryCrack||[])].map(([a,b])=>[[-a[0],a[1],p.depth[1]+descriptor.profile.windowPaneThickness*.03],[-b[0],b[1],p.depth[1]+descriptor.profile.windowPaneThickness*.03]])):[];
 return {...recipe,parts,crackLines};
}
