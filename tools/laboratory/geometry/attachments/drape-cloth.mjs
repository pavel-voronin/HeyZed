const tau=Math.PI*2;
function noise(seed,index){const n=Math.sin(seed*1.37+index*17.13)*43758.5453;return n-Math.floor(n);}
export function drapeSeed(identity){let seed=17;for(const c of identity)seed=(seed*31+c.charCodeAt(0))>>>0;return seed%10007;}
export const defaultDrapeLevel='medium';
export const drapeLevels=['high','medium','low'];
function drapeSurface({left,right,top,bottom,depth,side,folds,seed,gathered=false,seamCenter,panelIndex=0,hemScale=1}){
 function distribution(offset){const weights=Array.from({length:folds},(_,i)=>.55+noise(seed,i+offset)*.90),sum=weights.reduce((a,b)=>a+b,0),knots=[0];for(const weight of weights)knots.push(knots.at(-1)+weight/sum);return knots;}
 const upper=distribution(0),lower=distribution(10),width=right-left,height=top-bottom;
 function knot(j,v){const relax=Math.sin(Math.PI*v/2)**2*.55;return upper[j]*(1-relax)+lower[j]*relax;}
 function sample(i,q,v){
  const hang=Math.sin(Math.PI*v/2),a=knot(i,v)+(knot(i+1,v)-knot(i,v))*q;
  const drift=.012*Math.sin(v*Math.PI)*(Math.sin(a*tau*1.3+seed)+.45*Math.sin(a*tau*3.1+seed*.7));
  const flare=gathered?(a-.5)*width*.12*hang:0;
  const amplitude=(gathered?.043:.030)*(.50+.90*noise(seed,i+30));
  const phase=tau*q+.65*Math.sin(v*Math.PI)*Math.sin(i*1.7+seed)*Math.sin(q*Math.PI);
  const fold=(.5-.5*Math.cos(phase))*amplitude*(1+.25*hang*Math.sin(i*2.1+seed));
  const belly=.012*Math.sin(v*Math.PI)*(.5+.5*Math.sin(a*tau*1.7+seed));
  const scallop=.004*(.5-.5*Math.cos(tau*q));
  const hem=.004+.010*(.5+.5*Math.sin(a*tau*2.3+seed))+.004*(.5-.5*Math.cos(phase));
  const u=left+width*a,blend=seamCenter===undefined?1:Math.max(0,Math.min(1,(Math.abs(u-seamCenter)-.055)/.06));
  return {a,x:-(u+drift*blend+flare),y:top-height*v-scallop*(1-v)+Math.min(hem,height*.08)*hemScale*v**5,z:depth+side*(.004+(fold+belly)*blend+panelIndex*.003*(1-blend))};
 }
 function at(a,v){let i=0;while(i<folds-1&&a>knot(i+1,v))i++;return sample(i,(a-knot(i,v))/(knot(i+1,v)-knot(i,v)),v);}
 return {sample,at,width,height};
}

export function drapeNormalMap(THREE,spec){
 const surface=drapeSurface(spec),size=128,data=new Uint8Array(size*size*4),epsilon=1/1024;
 for(let y=0;y<size;y++)for(let x=0;x<size;x++){
  const a=(x+.5)/size,v=1-(y+.5)/size;
  const dx=-(surface.at(Math.min(1,a+epsilon),v).z-surface.at(Math.max(0,a-epsilon),v).z)/(2*epsilon*surface.width);
  const dy=(surface.at(a,Math.min(1,v+epsilon)).z-surface.at(a,Math.max(0,v-epsilon)).z)/(2*epsilon*surface.height);
  const length=Math.hypot(dx,dy,1),index=(y*size+x)*4;
  data[index]=Math.round((dx/length*.5+.5)*255);data[index+1]=Math.round((dy/length*.5+.5)*255);data[index+2]=Math.round((1/length*.5+.5)*255);data[index+3]=255;
 }
 const map=new THREE.DataTexture(data,size,size,THREE.RGBAFormat);map.magFilter=THREE.LinearFilter;map.minFilter=THREE.LinearFilter;map.needsUpdate=true;return map;
}

// A small, deterministic baked surface: irregular hanging folds, not a live solver.
export function drapeGeometry(THREE,spec){
 const {folds,lod=defaultDrapeLevel,depth,side,panelIndex=0}=spec,surface=drapeSurface(spec);
 const samples=lod==='medium'?4:8,rows=lod==='low'?2:lod==='medium'?3:8,nx=lod==='low'?3:folds*samples,positions=[],uvs=[],indices=[],thickness=.0012;
 for(let face=0;face<2;face++)for(let row=0;row<=rows;row++)for(let col=0;col<=nx;col++){
  const v=row/rows,i=Math.min(folds-1,Math.floor(col/samples)),q=(col-i*samples)/samples;
  const p=lod==='low'?surface.at(col/nx,v):surface.sample(i,q,v);
  if(lod==='low')p.z=depth+side*(.025+panelIndex*.003);
  positions.push(p.x,p.y,p.z+(face?-.5:.5)*thickness);uvs.push(p.a,1-v);
 }
 const stride=(nx+1)*(rows+1);
 for(let face=0;face<2;face++)for(let row=0;row<rows;row++)for(let col=0;col<nx;col++){
  const a=face*stride+row*(nx+1)+col,b=a+1,c=a+nx+1,d=c+1;
  if(face)indices.push(a,d,b,a,c,d);else indices.push(a,b,d,a,d,c);
 }
 for(let col=0;col<nx;col++){
  const a=col,b=a+1,c=rows*(nx+1)+col,d=c+1;
  indices.push(a,a+stride,b+stride,a,b+stride,b,c,d,d+stride,c,d+stride,c+stride);
 }
 for(let row=0;row<rows;row++){
  const a=row*(nx+1),b=a+nx+1,c=a+nx,d=c+nx+1;
  indices.push(a,b,b+stride,a,b+stride,a+stride,c,c+stride,d+stride,c,d+stride,d);
 }
 const geometry=new THREE.BufferGeometry();
 geometry.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
 geometry.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));geometry.setIndex(indices);geometry.computeVertexNormals();
 return geometry;
}
