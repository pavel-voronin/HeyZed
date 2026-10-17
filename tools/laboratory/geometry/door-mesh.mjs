import {buildApprovedWindow} from './approved-window-fracture.mjs';

// Renderer-independent proposals. All dimensions come from the shared section
// and metric card; placement orientation belongs to the scene instance.
export function buildOpeningInsert(aperture,descriptor,state='closed',reference=null,preset=null){
 const construction=aperture.construction,p=descriptor.profile;
 if(!construction)throw Error('Opening has no construction proposal');
 if(construction.mechanism==='fixed-window'){
  const shape=reference?.properties?.WindowShape||'11';
  const recipe=buildApprovedWindow({...aperture,id:'window-'+shape+'-0',nativeShape:shape},descriptor,state,preset);
  return {...recipe,profileId:aperture.id,status:'candidate',visualState:state};
 }
 if(!['closed','open'].includes(state))throw Error('Unsupported door state');
 const [l,r,b,t]=aperture.bounds,w=p.windowFrameWidth,d=p.wallThickness/2,gap=p.doorLeafClearance,parts=[];
 const joins=aperture.joins||[];
 function box(name,material,a,z0,n0,c,z1,n1,leaf=false){
  if(!(c>a&&z1>z0&&n1>n0))throw Error('Invalid door part: '+name);
  const center=leaf?p.doorLeafCenter:0;
  const part={name,material,bounds:[a,z0,n0+center,c,z1,n1+center]};
  if(leaf&&state==='open')part.transform={pivot:[cHinge,b,p.doorHingeDepth],angleDegrees:(construction.hinge==='right'?-1:1)*construction.openAngleDegrees};
  parts.push(part);
 }
 if(!joins.includes('left'))box('casing.left','frame',l,b,-d,l+w,t,d);
 if(!joins.includes('right'))box('casing.right','frame',r-w,b,-d,r,t,d);
 box('casing.top','frame',l+(joins.includes('left')?0:w),t-w,-d,r-(joins.includes('right')?0:w),t,d);
 const a=l+(joins.includes('left')?gap:w+gap),z0=(construction.leafBottom??b)+gap,z1=(construction.leafTop??(t-w))-gap,end=r-(joins.includes('right')?gap:w+gap),half=p.doorThickness/2;
 const cHinge=construction.hinge==='right'?end:a;
 const style=descriptor.doorStyles.styles.find(s=>s.id===reference?.doorAppearance?.typeId)||descriptor.doorStyles.styles.find(s=>s.id==='solid');
 const leafMaterial=style.kind==='bars'||reference?.properties?.DoorSound?.includes('Metal')?'metal':'wood';
 if(style.kind==='solid'){
 // A thin continuous core and proud panels form a closed solid leaf.
 const core=half-Math.min(p.doorPanelRelief,half/2);
 box('leaf.core',leafMaterial,a,z0,-core,end,z1,core,true);
 const cellWidth=(end-a)/construction.panelColumns,cellHeight=(z1-z0)/construction.panelRows;
 for(let x=0;x<construction.panelColumns;x++)for(let y=0;y<construction.panelRows;y++){
  const insetX=cellWidth*construction.panelInsetFraction,insetZ=cellHeight*construction.panelInsetFraction;
  for(const side of [-1,1])box('leaf.panel.'+x+'.'+y+'.'+side,leafMaterial,a+x*cellWidth+insetX,z0+y*cellHeight+insetZ,side<0?-half:core,a+(x+1)*cellWidth-insetX,z0+(y+1)*cellHeight-insetZ,side<0?-core:half,true);
 }
 }else{
  const stile=p.doorLatchStileWidth,rail=p.doorRailWidth;
  const x0=a+stile,x1=end-stile;
  box('leaf.stile.left',leafMaterial,a,z0,-half,x0,z1,half,true);
  box('leaf.stile.right',leafMaterial,x1,z0,-half,end,z1,half,true);
  const lower=style.kind==='glass'?Math.max(z0+(z1-z0)*style.bottom,style.id==='upper-glazed'?b+p.doorHandleHeight+p.doorHandlePlateHeight/2+rail:0):z0+rail,upper=style.kind==='glass'?z0+(z1-z0)*style.top:z1-rail;
  box('leaf.rail.bottom',leafMaterial,x0,z0,-half,x1,lower,half,true);
  box('leaf.rail.top',leafMaterial,x0,upper,-half,x1,z1,half,true);
  const bar=style.kind==='bars'?p.doorBarWidth:p.windowFrameWidth;
  const dx=(x1-x0)/style.columns,dz=(upper-lower)/style.rows;
  for(let col=1;col<style.columns;col++)box('leaf.mullion.'+col,leafMaterial,x0+dx*col-bar/2,lower,-half,x0+dx*col+bar/2,upper,half,true);
  for(let row=1;row<style.rows;row++)box('leaf.crossbar.'+row,leafMaterial,x0,lower+dz*row-bar/2,-half,x1,lower+dz*row+bar/2,half,true);
  if(style.kind==='glass')for(let col=0;col<style.columns;col++)for(let row=0;row<style.rows;row++){
   const insetLeft=col?bar/2:0,insetRight=col<style.columns-1?bar/2:0,insetBottom=row?bar/2:0,insetTop=row<style.rows-1?bar/2:0;
   box('leaf.glass.'+col+'.'+row,'glass',x0+dx*col+insetLeft,lower+dz*row+insetBottom,-p.windowPaneThickness/2,x0+dx*(col+1)-insetRight,lower+dz*(row+1)-insetTop,p.windowPaneThickness/2,true);
  }
 }
 const handleBackset=style.kind==='solid'?p.doorHandleBackset:p.doorLatchStileWidth/2;
 const handleU=construction.hinge==='right'?a+handleBackset:end-handleBackset,handleZ=Math.min(z1-p.doorHandleDiameter,b+p.doorHandleHeight),radius=p.doorHandleDiameter/2;
 const leverEnd=handleU+(construction.hinge==='right'?1:-1)*p.doorHandleLength;
 for(const side of [-1,1]){
  const outer=side*(half+p.doorHandleProjection),near=side*half,plateOuter=side*(half+p.doorHandlePlateDepth);
  box('leaf.handle.plate.'+side,'hardware',handleU-p.doorHandlePlateWidth/2,handleZ-p.doorHandlePlateHeight/2,Math.min(near,plateOuter),handleU+p.doorHandlePlateWidth/2,handleZ+p.doorHandlePlateHeight/2,Math.max(near,plateOuter),true);
  box('leaf.handle.stem.'+side,'hardware',handleU-radius,handleZ-radius,Math.min(near,outer),handleU+radius,handleZ+radius,Math.max(near,outer),true);
  box('leaf.handle.lever.'+side,'hardware',Math.min(handleU,leverEnd)-radius,handleZ-radius,outer-radius,Math.max(handleU,leverEnd)+radius,handleZ+radius,outer+radius,true);
 }
 return {profileId:aperture.id,recipe:'door.hinged-'+style.id+'/1',doorStyle:style.id,status:'candidate',visualState:state,parts};
}
