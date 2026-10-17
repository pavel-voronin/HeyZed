// Construction proposals are separate from approved aperture geometry.
export function windowConstruction(profile){
 if(profile.constructionRecipe)return {id:profile.constructionRecipe,label:profile.label||'Window type',status:'laboratory',modelStatus:'Laboratory',previewRecipe:profile.constructionRecipe};
 if(profile.confirmed===false)return profile.reviewRecipe?.startsWith('storefront')?{id:'storefront',label:'Storefront section',status:'candidate',modelStatus:'Proposal',previewRecipe:profile.reviewRecipe}:{id:'unresolved',label:'Native window',status:'candidate',modelStatus:'Geometry pending'};
 const joins=profile.joins||[];
 if(profile.id==='window-10-0'&&profile.shape==='elliptic-arch')return {id:'arched-sash',label:'Arched sliding sash',status:'candidate',modelStatus:'Preview',previewRecipe:'arched-sash'};
 if(profile.id==='window-9-0')return {id:'sliding-sash',label:'Sliding sash',status:'approved',modelStatus:'Approved',previewRecipe:'two-sash'};
 if(profile.id==='window-9-1')return {id:'sliding-sash',label:'Sliding sash',status:'approved',modelStatus:'Approved',previewRecipe:'two-sash'};
 if(profile.id==='window-20-0'||profile.id==='window-21-0')return {id:'pointed-sections',label:'Pointed arch section',status:'candidate',modelStatus:'Preview',previewRecipe:'fixed-section'};
 if(profile.shape==='quarter-ellipse')return {id:'curved-sections',label:'Curved section',status:'candidate',modelStatus:'Preview',previewRecipe:'fixed-section'};
 if(profile.shape==='ellipse')return {id:'round-glazing',label:'Round glazing',status:'candidate',modelStatus:'Preview',previewRecipe:'fixed-section'};
 if(profile.shape==='elliptic-arch')return {id:'arched-glazing',label:joins.length?'Arch section':'Arched glazing',status:'candidate',modelStatus:'Preview',previewRecipe:'fixed-section'};
 if(profile.shape==='rectangle'&&joins.length)return {id:'rectangular-sections',label:'Rectangular section',status:'candidate',modelStatus:'Preview',previewRecipe:'fixed-section'};
 return {id:'unresolved',label:'Unresolved',status:'candidate',modelStatus:'Model pending'};
}
