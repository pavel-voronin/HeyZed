export function serialiseAttachmentMaterial(material){
 const result={model:true,color:material.color.toArray(),roughness:material.roughness,metalness:material.metalness,transparent:material.transparent,opacity:material.opacity,alphaTest:material.alphaTest,depthWrite:material.depthWrite};
 for(const key of ['map','normalMap'])if(material[key]?.isDataTexture){
  const t=material[key];(result.attachmentTextures??={})[key]={width:t.image.width,height:t.image.height,data:Array.from(t.image.data),repeat:t.repeat.toArray(),offset:t.offset.toArray(),minFilter:t.minFilter,magFilter:t.magFilter,generateMipmaps:t.generateMipmaps};
 }
 if(material.normalMap)result.normalScale=material.normalScale.toArray();
 return result;
}
