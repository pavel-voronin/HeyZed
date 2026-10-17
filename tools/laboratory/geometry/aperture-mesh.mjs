                                                                                  
                                                                                
const snap=x=>Math.round(x*1e10)/1e10;
const key=p=>p.map(snap).join(',');
export function buildApertureWall(points,metric,thickness){
 const w=metric.tileSize,h=metric.floorHeight;
 if(!(w>0&&h>0&&thickness>0)||points.length<3)throw Error('Invalid wall dimensions or aperture');
 points=points.map(p=>p.map(snap));
 const edges=points.map((a,i)=>[a,points[(i+1)%points.length]]);
 const cuts=[0,w,...points.map(p=>p[0])];
 for(const [a,b] of edges)for(const z of [0,h])if((a[1]-z)*(b[1]-z)<0)cuts.push(a[0]+(b[0]-a[0])*(z-a[1])/(b[1]-a[1]));
 const xs=[...new Set(cuts.filter(x=>x>=0&&x<=w).map(snap))].sort((a,b)=>a-b),cells=[];
 const clamp=z=>snap(Math.max(0,Math.min(h,z)));
 for(let i=0;i<xs.length-1;i++){
  const l=xs[i],r=xs[i+1],mid=(l+r)/2;
  const crossing=edges.filter(([a,b])=>(a[0]<=mid&&mid<b[0])||(b[0]<=mid&&mid<a[0]));
  const at=(edge,x)=>{const [a,b]=edge;if(x===a[0])return a[1];if(x===b[0])return b[1];return a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0]);};
  crossing.sort((a,b)=>at(a,mid)-at(b,mid));
  let low=[0,0];
  const cell=high=>{if((high[0]+high[1]-low[0]-low[1])*(r-l)>1e-12)cells.push({l,r,low:[...low],high});};
  for(let j=0;j<crossing.length;j+=2){if(!crossing[j+1])throw Error('Unclosed aperture');cell([clamp(at(crossing[j],l)),clamp(at(crossing[j],r))]);low=[clamp(at(crossing[j+1],l)),clamp(at(crossing[j+1],r))];}
  cell([snap(h),snap(h)]);
 }
 const levels=new Map();
 for(const c of cells)for(const [x,i] of [[c.l,0],[c.r,1]]){if(!levels.has(x))levels.set(x,new Set());levels.get(x).add(c.low[i]);levels.get(x).add(c.high[i]);}
 const positions=[],indices=[],vertices=new Map(),boundary=new Map();let area=0;
 function vertex(u,z,d){const p=[snap(u),snap(z),snap(d)],k=key(p);if(!vertices.has(k)){vertices.set(k,positions.length/3);positions.push(...p);}return vertices.get(k);}
 const face=(a,b,c)=>indices.push(a,b,c);
 for(const c of cells){
  const right=[...levels.get(c.r)].filter(z=>z>=c.low[1]&&z<=c.high[1]).sort((a,b)=>a-b);
  const left=[...levels.get(c.l)].filter(z=>z>=c.low[0]&&z<=c.high[0]).sort((a,b)=>b-a);
  let ring=[[c.l,c.low[0]],...right.map(z=>[c.r,z]),...left.map(z=>[c.l,z])];
  ring=ring.filter((p,i)=>key(p)!==key(ring[(i+ring.length-1)%ring.length]));
  const center=[(c.l+c.r)/2,(c.low[0]+c.low[1]+c.high[0]+c.high[1])/4];
  area+=(c.r-c.l)*(c.high[0]+c.high[1]-c.low[0]-c.low[1])/2;
  const front=vertex(...center,thickness/2),back=vertex(...center,-thickness/2);
  for(let i=0;i<ring.length;i++){
   const a=ring[i],b=ring[(i+1)%ring.length];if(key(a)===key(b))continue;
   face(front,vertex(...a,thickness/2),vertex(...b,thickness/2));face(back,vertex(...b,-thickness/2),vertex(...a,-thickness/2));
   const ak=key(a),bk=key(b),k=[ak,bk].sort().join('|');
   if(boundary.has(k))boundary.delete(k);else boundary.set(k,[a,b]);
  }
 }
 for(const [a,b] of boundary.values()){
  const ab=vertex(...a,-thickness/2),bb=vertex(...b,-thickness/2),af=vertex(...a,thickness/2),bf=vertex(...b,thickness/2);
  face(ab,bb,bf);face(ab,bf,af);
 }
 const boxes=cells.every(c=>c.low[0]===c.low[1]&&c.high[0]===c.high[1])?
  cells.map(c=>[c.l,-thickness/2,c.low[0],c.r,thickness/2,c.high[0]]):null;
 return {positions,indices,area,volume:area*thickness,boxes,axes:'u,z,depth'};
}
