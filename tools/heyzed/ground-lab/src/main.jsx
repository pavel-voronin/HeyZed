import React, {useEffect, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import './style.css';

const square=[[0,0],[1,0],[1,1],[0,1]];
const images=new Map(), imageListeners=new Set();
function picture(url){
  if(!url)return {status:'missing'};
  if(!images.has(url)){
    const image=new Image(),entry={image,status:'loading'};
    images.set(url,entry);
    const done=status=>{entry.status=status;for(const listener of imageListeners)listener();};
    image.onload=()=>done('ready');image.onerror=()=>done('missing');image.src=url;
    if(images.size>160)images.delete(images.keys().next().value);
  }
  return images.get(url);
}
function spritePixel(image,x,y){
  if(x<0||y<0||x>=image.width||y>=image.height)return false;
  if(!image.sourceAlpha){const helper=document.createElement('canvas');helper.width=image.width;helper.height=image.height;const ctx=helper.getContext('2d',{willReadFrequently:true});ctx.drawImage(image,0,0);image.sourceAlpha=ctx.getImageData(0,0,image.width,image.height).data;}
  return image.sourceAlpha[(Math.floor(y)*image.width+Math.floor(x))*4+3]>0;
}
function NativeInspector({inspection,cells,materials}){
  const cell=inspection?cells.find(c=>c.x===inspection.cellX&&c.y===inspection.cellY):null;
  const sourceCell=inspection?cells.find(c=>c.x===inspection.spriteCellX&&c.y===inspection.spriteCellY):null;
  function metadata(layer,index){const m=layer.metadata??{};return <article className="source-card" key={layer.name+index}>
    <strong className="name">{index+1}. {layer.name}</strong>
    <div className="source-summary"><img className="source-sprite" src={'/api/sprite/'+encodeURIComponent(layer.name)} alt={layer.name}/><dl className="source-values"><dt>Stack</dt><dd>{layer.overlay?'Overlay':'Base floor'}</dd><dt>Native material</dt><dd>{m.nativeMaterial||'Unspecified'}</dd><dt>Edges</dt><dd>{layer.edges||'None'}</dd><dt>Ground geometry</dt><dd>{m.geometry?.length?m.geometry.map((p,i)=><div key={i}>{p.material} | {p.triangles} triangles | {(p.coverage*100).toFixed(1)}% of cell{!materials.includes(p.material)?' | Missing material':''}</div>):'None'}</dd></dl></div>
    <details><summary>Native properties</summary><dl className="property-list">{Object.entries(m.properties??{}).map(([key,value])=><React.Fragment key={key}><dt>{key}</dt><dd>{value===''?'true':value}</dd></React.Fragment>)}</dl></details>
    <details><summary>Source</summary><div className="name">{m.definitions?.join(', ')||'No definition'}</div><div className="name">{m.sprite?.pack??'No sprite'}</div>{m.sprite&&<dl className="source-values"><dt>Atlas XYWH</dt><dd>{m.sprite.rectangle.slice(0,4).join(', ')}</dd><dt>Sprite offset</dt><dd>{m.sprite.rectangle.slice(4,6).join(', ')}</dd><dt>Sprite frame</dt><dd>{m.sprite.rectangle.slice(6).join(' x ')}</dd></dl>}</details>
  </article>;}
  return <div className="inspector">{cell?<><header className="panel-title">Tile {cell.worldX??cell.x}, {cell.worldY??cell.y}</header><dl className="source-values"><dt>Local XY</dt><dd>{inspection.x.toFixed(3)}, {inspection.y.toFixed(3)}</dd><dt>Layers</dt><dd>{cell.layers.length}</dd><dt>Visible sprite</dt><dd className="name">{inspection.spriteName??'None'}</dd>{sourceCell&&<><dt>Sprite origin</dt><dd>{sourceCell.worldX??sourceCell.x}, {sourceCell.worldY??sourceCell.y}</dd></>}</dl>{cell.layers.map(metadata)}{sourceCell&&sourceCell!==cell&&<><header className="panel-title">Visible sprite source</header>{sourceCell.layers.filter(l=>l.name===inspection.spriteName).map(metadata)}</>}</>:<span className="muted">No tile selected</span>}</div>;
}
function inside(p,a,b,c){const cross=(u,v,w)=>(v[0]-u[0])*(w[1]-u[1])-(v[1]-u[1])*(w[0]-u[0]);const s=[cross(a,b,p),cross(b,c,p),cross(c,a,p)];return s.every(v=>v>=-1e-7)||s.every(v=>v<=1e-7);}
function parts(layer,data){return data.tiles.find(t=>t.name===layer.name)?.parts??(layer.overlay?[]:[{material:layer.material?.startsWith('Grass')?'Grass':layer.material,outline:square,vertices:[[0,0],[1,0],[1,1],[0,0],[1,1],[0,1]]}]);}
function View({original,cells,data,grid,zoom,markers,onPoint,selected,context,outline,worldX,worldY,inspection}){
  const canvas=useRef(null),frame=useRef(null);const [loaded,refresh]=useState(0);
  useEffect(()=>{const update=()=>refresh(v=>v+1);imageListeners.add(update);return()=>imageListeners.delete(update);},[]);
  useEffect(()=>{
    const el=canvas.current;
    const draw=()=>{
      const width=el.clientWidth,height=el.clientHeight,dpr=devicePixelRatio||1;el.width=width*dpr;el.height=height*dpr;
      const ctx=el.getContext('2d');ctx.scale(dpr,dpr);ctx.fillStyle='#171e28';ctx.fillRect(0,0,width,height);
      const n=3,scale=Math.min(width/(128*n+48),height/(64*n+70))*zoom;
      const ox=width/2,oy=height/2-n*32*scale;
      const project=(x,y)=>[ox+(x-y)*64*scale,oy+(x+y)*32*scale];
      frame.current={scale,ox,oy,hits:[]};
      const polygon=(poly,x,y)=>{ctx.beginPath();poly.forEach(([px,py],i)=>{const p=project(x+px,y+py);i?ctx.lineTo(...p):ctx.moveTo(...p);});ctx.closePath();};
      for(const cell of [...cells].sort((a,b)=>(a.x+a.y)-(b.x+b.y)||a.x-b.x)){
        for(const layer of cell.layers){
          if(original){
            const source=picture(layer.name?'/api/sprite/'+encodeURIComponent(layer.name):null);
            if(source.status==='ready'){const p=project(cell.x,cell.y);ctx.drawImage(source.image,p[0]-64*scale,p[1]-192*scale,source.image.width*scale,source.image.height*scale);frame.current.hits.push({image:source.image,left:p[0]-64*scale,top:p[1]-192*scale,cell,name:layer.name});}
          }else for(const part of parts(layer,data)){
            if(part.material==='PaintHidden')continue;
            const available=data.materials?.includes(part.material);
            const source=available?picture(`/api/material/${encodeURIComponent(part.material)}?x=${worldX}&y=${worldY}`):{status:'missing'};
            ctx.save();ctx.beginPath();
            const v=part.vertices;
            for(let i=0;i<v.length;i+=3){v.slice(i,i+3).forEach(([px,py],j)=>{const p=project(cell.x+px,cell.y+py);j?ctx.lineTo(...p):ctx.moveTo(...p);});ctx.closePath();}
            if(source.status==='ready'){
              ctx.clip();const unit=3/source.image.width;
              ctx.transform(64*scale*unit,32*scale*unit,-64*scale*unit,32*scale*unit,ox,oy);
              ctx.drawImage(source.image,0,0);
            }else{ctx.fillStyle=source.status==='missing'?'#ff00ff':'#283346';ctx.fill();}
            ctx.restore();
          }
        }
        if(grid){polygon(square,cell.x,cell.y);ctx.strokeStyle='#95a9bd77';ctx.lineWidth=1;ctx.stroke();const p=project(cell.x+.5,cell.y+.5);ctx.fillStyle='#ffffff';ctx.font='11px monospace';ctx.fillText(cell.worldX!==undefined?`${cell.worldX},${cell.worldY}`:`${cell.x},${cell.y}`,p[0]-35,p[1]+4);}
        for(const [index,m] of markers.entries()){if(m.context!==context||m.cellX!==cell.x||m.cellY!==cell.y)continue;const p=project(cell.x+m.x,cell.y+m.y);ctx.beginPath();ctx.arc(...p,8,0,Math.PI*2);ctx.fillStyle='#ef6f66';ctx.fill();ctx.fillStyle='#111';ctx.font='bold 11px monospace';ctx.fillText(String(index+1),p[0]-3,p[1]+4);}
      }
      const tile=inspection?cells.find(c=>c.x===inspection.cellX&&c.y===inspection.cellY):cells.find(c=>c.x===1&&c.y===1&&c.layers.some(l=>l.name===selected));
      if(outline&&tile){polygon(square,tile.x,tile.y);ctx.strokeStyle='#8eb9ff';ctx.lineWidth=2;ctx.stroke();}
      const origin=project(0,0);ctx.fillStyle='#a4b1c2';ctx.font='12px monospace';ctx.fillText('+X',origin[0]+33*scale,origin[1]+16*scale);ctx.fillText('+Y',origin[0]-52*scale,origin[1]+16*scale);
    };
    const observer=new ResizeObserver(draw);observer.observe(el);draw();return()=>observer.disconnect();
  },[cells,data,grid,zoom,markers,selected,loaded,original,context,outline,worldX,worldY,inspection]);
  function point(e){
    if(!frame.current)return;
    const r=e.currentTarget.getBoundingClientRect(),{scale,ox,oy,hits}=frame.current;
    const sx=e.clientX-r.left,sy=e.clientY-r.top,a=(sx-ox)/(64*scale),b=(sy-oy)/(32*scale),x=(a+b)/2,y=(b-a)/2;
    const spriteHit=original?[...hits].reverse().find(h=>spritePixel(h.image,(sx-h.left)/scale,(sy-h.top)/scale)):null;
    const cell=cells.find(c=>Math.floor(x)===c.x&&Math.floor(y)===c.y)??spriteHit?.cell;
    if(!cell)return;
    const p=[Math.max(0,Math.min(1,x-cell.x)),Math.max(0,Math.min(1,y-cell.y))];let hit=null;
    for(const layer of cell.layers)for(const part of parts(layer,data).filter(p=>p.material!=='PaintHidden'))for(let i=0;i<part.vertices.length;i+=3)if(inside(p,...part.vertices.slice(i,i+3)))hit={name:layer.name,actual:part.material};
    onPoint({...hit,name:hit?.name??cell.layers.at(-1)?.name,actual:hit?.actual??'Unknown',pane:original?'Native':'Unreal',context,x:p[0],y:p[1],cellX:cell.x,cellY:cell.y,worldX:cell.worldX,worldY:cell.worldY,spriteName:spriteHit?.name,spriteCellX:spriteHit?.cell.x,spriteCellY:spriteHit?.cell.y});
  }
  return <section className="viewport"><header className="panel-title">{original?'Native source':'Unreal materials | Base color'}</header><canvas ref={canvas} className="canvas" onPointerDown={point}/></section>;
}
async function request(url,options){const r=await fetch(url,options),data=await r.json();if(!r.ok)throw Error(data.error??r.statusText);return data;}
function App(){
  const [data,setData]=useState(null),[error,setError]=useState(''),[selected,select]=useState(''),[search,setSearch]=useState(''),[family,setFamily]=useState('All'),[filter,setFilter]=useState('All');
  const [scene,setScene]=useState(null),[loadingScene,setLoadingScene]=useState(false),[grid,setGrid]=useState(false),[outline,setOutline]=useState(false),[zoom,setZoom]=useState(1);
  const [leftTab,setLeftTab]=useState('Catalog'),[inspection,setInspection]=useState(null);
  const [review,setReview]=useState(null),[issue,setIssue]=useState('Material'),[expected,setExpected]=useState('Concrete'),[saved,setSaved]=useState(''),[busy,setBusy]=useState(false),[dirty,setDirty]=useState(false);
  useEffect(()=>{request('/api/catalog').then(d=>{setData(d);select(d.tiles.find(t=>t.name==='street_curbs_01_0')?.name??d.tiles[0].name);}).catch(e=>setError(e.message));},[]);
  useEffect(()=>{if(!data||!selected)return;const t=data.tiles.find(t=>t.name===selected),old=data.reviews[selected];setReview(old?{...old,status:old.revision===t.revision?old.status:'Unreviewed',revision:t.revision}:{name:selected,revision:t.revision,status:'Unreviewed',note:'',markers:[]});setSaved(old?.revision!==t.revision&&old?'Profile changed':'');setDirty(false);},[selected,data]);
  async function save(value=review){if(!value)return;setBusy(true);try{await request('/api/review',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});setData(d=>({...d,reviews:{...d.reviews,[value.name]:value}}));setSaved('Saved');setDirty(false);}catch(e){setError(e.message);}finally{setBusy(false);}}
  async function choose(name){if(dirty){try{await request('/api/review',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(review)});setData(d=>({...d,reviews:{...d.reviews,[review.name]:review}}));}catch(e){setError(e.message);return;}}select(name);}
  function edit(next){setReview(next);setDirty(true);setSaved('Unsaved');}
  useEffect(()=>{
    if(!selected)return;
    const controller=new AbortController();let timer,active=true;
    setLoadingScene(true);setError('');setInspection(null);
    async function load(){
      try{
        const value=await request('/api/neighborhood?name='+encodeURIComponent(selected),{signal:controller.signal});
        if(!active)return;
        setScene(value);setLoadingScene(false);
        if(value.pending)timer=setTimeout(load,1200);
      }catch(e){if(active&&e.name!=='AbortError'){setError(e.message);setLoadingScene(false);}}
    }
    load();return()=>{active=false;clearTimeout(timer);controller.abort();};
  },[selected]);
  function inspect(point){setInspection(point);setLeftTab('Native Source');}
  function mark(point){edit({...review,status:'Rework',markers:[...review.markers,{...point,sourceName:point.name,issue,expected}]});setError('');}
  if(!data||!review)return <main className="loading">{error||'Loading native catalog...'}</main>;
  const status=t=>{const r=data.reviews[t.name];return r?.revision===t.revision?r.status:'Unreviewed';};
  const visible=data.tiles.filter(t=>t.name.includes(search)&&(family==='All'||t.family===family)&&(filter==='All'||status(t)===filter));
  const context=scene?.identity==='base-context'?'Base context':`Map:${scene?.centerX},${scene?.centerY}`;
  const cells=scene?.cells??[];
  const worldX=scene?.centerX===null?0:(scene?.centerX??1)-1,worldY=scene?.centerY===null?0:(scene?.centerY??1)-1;
  return <main className="app"><header className="top"><strong>Unreal Ground Lab</strong><span className="muted">{data.tiles.length} tiles | {data.tiles.filter(t=>data.reviews[t.name]?.status==='Approved'&&data.reviews[t.name]?.revision===t.revision).length} approved</span><span className="error">{error}</span></header>
    <aside className="catalog"><nav className="sidebar-tabs">{['Catalog','Native Source'].map(tab=><button className={leftTab===tab?'tab-active':'tab'} key={tab} onClick={()=>setLeftTab(tab)}>{tab}</button>)}</nav>{leftTab==='Catalog'?<><input aria-label="Search tiles" value={search} onChange={e=>setSearch(e.target.value)} placeholder="Tile name"/><select aria-label="Family" value={family} onChange={e=>setFamily(e.target.value)}><option>All</option>{[...new Set(data.tiles.map(t=>t.family))].map(f=><option key={f}>{f}</option>)}</select><select aria-label="Review filter" value={filter} onChange={e=>setFilter(e.target.value)}>{['All','Unreviewed','Approved','Rework'].map(v=><option key={v}>{v}</option>)}</select><div className="tile-list">{visible.map(t=><button key={t.name} className={selected===t.name?'tile-active':'tile'} onClick={()=>choose(t.name)}><span>{t.name}</span><small>{status(t)}{!t.sprite?' | No source':''}</small></button>)}</div><span className="muted">{visible.length} results</span></>:<NativeInspector inspection={inspection} cells={cells} materials={data.materials??[]}/>}</aside>
    <section className="workspace"><div className="toolbar"><span className="muted">{loadingScene?'Loading neighborhood...':scene?.status}</span><label>Grid <input type="checkbox" checked={grid} onChange={e=>setGrid(e.target.checked)}/></label><label>Outline <input type="checkbox" checked={outline} onChange={e=>setOutline(e.target.checked)}/></label><label>Zoom <select value={zoom} onChange={e=>setZoom(Number(e.target.value))}>{[.75,1,1.5,2].map(v=><option key={v} value={v}>{v}x</option>)}</select></label></div>
    <div className="views"><View original cells={cells} data={data} grid={grid} zoom={zoom} markers={review.markers} onPoint={inspect} selected={selected} context={context} outline={outline} worldX={worldX} worldY={worldY} inspection={inspection}/><View cells={cells} data={data} grid={grid} zoom={zoom} markers={review.markers} onPoint={mark} selected={selected} context={context} outline={outline} worldX={worldX} worldY={worldY} inspection={inspection}/></div>
    </section>
    <aside className="review"><strong className="name">{selected}</strong><span className="muted">Revision {review.revision}</span><label>Status<select value={review.status} onChange={e=>edit({...review,status:e.target.value})}>{['Unreviewed','Approved','Rework'].map(v=><option key={v}>{v}</option>)}</select></label><label>Issue<select value={issue} onChange={e=>setIssue(e.target.value)}>{['Shape','Material','Side','Join'].map(v=><option key={v}>{v}</option>)}</select></label><label>Expected<select value={expected} onChange={e=>setExpected(e.target.value)}>{Object.keys(data.colors).map(v=><option key={v}>{v}</option>)}</select></label><textarea aria-label="Review note" value={review.note} onChange={e=>edit({...review,note:e.target.value})} placeholder="Note"/><button disabled={busy} onClick={()=>save()}>Save review</button><span className="muted">{saved}</span><header className="panel-title">Markers | {review.markers.length}</header><div className="markers">{review.markers.map((m,i)=><div className="marker" key={i}><span>{i+1}. {m.issue}<br/>{m.sourceName??m.name??selected}<br/>{m.actual} -&gt; {m.expected}<br/>{m.x.toFixed(3)}, {m.y.toFixed(3)} | {m.context}</span><button onClick={()=>edit({...review,markers:review.markers.filter((_,j)=>j!==i)})}>X</button></div>)}</div><header className="panel-title">Materials</header><div className="legend">{[...new Set(cells.flatMap(c=>c.layers.flatMap(l=>parts(l,data).map(p=>p.material))))].map(m=><span className="legend-item" key={m}><svg width="16" height="12" aria-hidden="true"><rect width="16" height="12" fill={data.materials?.includes(m)?'#94a3b8':'#ff00ff'}/></svg>{m||'Unspecified'}{!data.materials?.includes(m)?' | Missing':''}</span>)}</div></aside></main>;
}
createRoot(document.getElementById('root')).render(<App/>);
