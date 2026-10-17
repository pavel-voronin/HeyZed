package com.pavelvoronin.heyzed.entities;

import java.nio.*;
import java.nio.file.*;
import java.lang.reflect.*;
import java.util.*;
import org.json.*;
import jassimp.*;
import zombie.core.skinnedmodel.model.*;
import zombie.core.skinnedmodel.model.jassimp.*;

/** Offline conversion through the installed game's own importer, never through rendered frames. */
public final class NativeEntityAssets {
    static Object field(Object value,String name)throws Exception {
        Field f=value.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(value);
    }
    static JSONArray matrix(org.lwjgl.util.vector.Matrix4f m) {
        return new JSONArray(new float[]{m.m00,m.m10,m.m20,m.m30,m.m01,m.m11,m.m21,m.m31,m.m02,m.m12,m.m22,m.m32,m.m03,m.m13,m.m23,m.m33});
    }
    static JSONObject extract(Object geometry,Object skeleton,boolean txt)throws Exception {
        VertexBufferObject.VertexArray vertices=(VertexBufferObject.VertexArray)field(geometry,txt?"vertices":skeleton==null?"verticesUnskinned":"vertices");
        int[] indices=(int[])field(geometry,"elements");
        if(vertices==null||indices.length==0)throw new IllegalArgumentException("Empty mesh");
        JSONObject out=new JSONObject().put("schema",1);
        int[] widths={3,3,2,4,4};
        var types=new VertexBufferObject.VertexType[]{VertexBufferObject.VertexType.VertexArray,VertexBufferObject.VertexType.NormalArray,VertexBufferObject.VertexType.TextureCoordArray,VertexBufferObject.VertexType.BlendWeightArray,VertexBufferObject.VertexType.BlendIndexArray};
        var keys=new String[]{"positions","normals","uv","weights","joints"};
        for(int i=0;i<keys.length;i++){
            int element=vertices.format.indexOf(types[i]);if(element<0)continue;
            JSONArray values=new JSONArray();
            for(int v=0;v<vertices.numVertices;v++)for(int c=0;c<widths[i];c++)values.put(vertices.getElementFloat(v,element,c));
            out.put(keys[i],values);
        }
        int uvSeen=0;var elements=(VertexBufferObject.VertexElement[])field(vertices.format,"elements");
        for(int e=0;e<elements.length;e++)if(elements[e].type==VertexBufferObject.VertexType.TextureCoordArray){
            if(++uvSeen==2){JSONArray values=new JSONArray();for(int v=0;v<vertices.numVertices;v++)for(int c=0;c<2;c++)values.put(vertices.getElementFloat(v,e,c));out.put("uv2",values);break;}
        }
        out.put("indices",new JSONArray(indices));
        if(skeleton!=null){
            @SuppressWarnings("unchecked") Map<String,Integer> names=(Map<String,Integer>)field(skeleton,"boneIndices");
            @SuppressWarnings("unchecked") List<Integer> parents=(List<Integer>)field(skeleton,"skeletonHierarchy");
            @SuppressWarnings("unchecked") List<org.lwjgl.util.vector.Matrix4f> offsets=(List<org.lwjgl.util.vector.Matrix4f>)field(skeleton,"skinOffsetMatrices");
            String[] boneNames=new String[names.size()];names.forEach((k,v)->boneNames[v]=k);
            JSONArray bones=new JSONArray();
            for(int i=0;i<boneNames.length;i++)bones.put(new JSONObject().put("name",boneNames[i]).put("parent",parents.get(i)).put("inverse_bind",matrix(offsets.get(i))));
            out.put("bones",bones);
        }
        return out;
    }
    static JSONObject x(Path file,String name)throws Exception {
        var flags=EnumSet.of(AiPostProcessSteps.FIND_INSTANCES,AiPostProcessSteps.MAKE_LEFT_HANDED,AiPostProcessSteps.LIMIT_BONE_WEIGHTS,AiPostProcessSteps.TRIANGULATE,AiPostProcessSteps.OPTIMIZE_MESHES,AiPostProcessSteps.REMOVE_REDUNDANT_MATERIALS,AiPostProcessSteps.JOIN_IDENTICAL_VERTICES);
        AiScene scene=Jassimp.importFile(file.toString(),flags);
        try {
            ProcessedAiSceneParams p=ProcessedAiSceneParams.create();p.scene=scene;p.mode=JAssImpImporter.LoadMode.Normal;p.meshName=name;
            if(file.toString().toLowerCase(Locale.ROOT).endsWith(".fbx")){
                p.animBonesScaleModifier=.01f;
                p.animBonesRotateModifier=new org.lwjgl.util.vector.Quaternion();
                p.animBonesRotateModifier.setFromAxisAngle(new org.lwjgl.util.vector.Vector4f(1,0,0,-(float)Math.PI/2));
            }
            var processed=ProcessedAiScene.process(p);
            Object skeleton=field(processed,"skeleton"),mesh=field(processed,"skinnedMesh");
            if(mesh==null){var list=(List<?>)field(processed,"staticMeshes");if(list.isEmpty())throw new IllegalArgumentException("No usable mesh");mesh=list.get(0);}
            return extract(mesh,skeleton,false);
        } finally {JAssImpImporter.takeOutTheTrash(scene);}
    }
    static void nodeAliases(AiNode node,Map<String,Integer> aliases){if(node.getNumMeshes()==1&&!node.getName().isBlank())aliases.putIfAbsent(node.getName(),node.getMeshes()[0]);for(AiNode child:node.getChildren())nodeAliases(child,aliases);}
    public static void main(String[] args)throws Exception {
        Path game=Path.of(args[0]).toAbsolutePath(),destination=Path.of(args[1]).toAbsolutePath();Files.createDirectories(destination);
        JAssImpImporter.Init();
        JSONObject catalog=new JSONObject(),errors=new JSONObject();int done=0;
        List<Path> files=new ArrayList<>();
        for(String dir:List.of("media/models_X","media/models"))
            try(var walk=Files.walk(game.resolve(dir))){walk.filter(Files::isRegularFile).filter(f->f.toString().toLowerCase(Locale.ROOT).matches(".*\\.(x|fbx|txt)$")).sorted().forEach(files::add);}
        if(args.length>2)files.removeIf(f->!f.getFileName().toString().equalsIgnoreCase(args[2]));
        for(Path file:files){
            String key=game.relativize(file).toString().replace('\\','/').toLowerCase(Locale.ROOT);
            String id=HexFormat.of().formatHex(java.security.MessageDigest.getInstance("SHA-256").digest(key.getBytes(java.nio.charset.StandardCharsets.UTF_8))).substring(0,24);
            Path output=destination.resolve(id+".json");
            try {
                JSONObject mesh;
                if(Files.exists(output)&&Files.getLastModifiedTime(output).compareTo(Files.getLastModifiedTime(file))>=0&&new JSONObject(Files.readString(output)).optInt("format")==2)mesh=new JSONObject(Files.readString(output));
                else {
                    if(key.endsWith(".txt")){
                        Method load=ModelLoader.class.getDeclaredMethod("loadTxt",String.class,boolean.class,boolean.class,SkinningData.class);load.setAccessible(true);
                        boolean skinned=Files.readString(file).contains("BlendWeightArray");
                        Object txt=load.invoke(ModelLoader.instance,file.toString(),!skinned,false,null);mesh=extract(txt,skinned?txt:null,true);
                    }else mesh=x(file,null);
                    mesh.put("format",2).put("key",key).put("id",id).put("source_size",Files.size(file));Files.writeString(output,mesh.toString());
                }
                if(!mesh.has("uv")){JSONArray uv=new JSONArray();for(int i=0;i<mesh.getJSONArray("positions").length()/3*2;i++)uv.put(0);mesh.put("uv",uv);Files.writeString(output,mesh.toString());}
                catalog.put(key,new JSONObject().put("id",id).put("file",output.getFileName().toString()).put("skeletal",mesh.has("bones")));
                if(!key.endsWith(".txt")){
                    AiScene aliases=Jassimp.importFile(file.toString(),EnumSet.of(AiPostProcessSteps.TRIANGULATE));
                    try{
                        int defaultMesh=0;for(int i=0;i<aliases.getNumMeshes();i++)if(aliases.getMeshes().get(i).hasBones()){defaultMesh=i;break;}
                        Map<String,Integer> selectors=new LinkedHashMap<>();
                        for(int i=0;i<aliases.getNumMeshes();i++){String n=aliases.getMeshes().get(i).getName();if(n!=null&&!n.isBlank())selectors.put(n,i);}
                        nodeAliases(aliases.getSceneRoot(new AiBuiltInWrapperProvider()),selectors);
                        Map<Integer,JSONObject> records=new HashMap<>();records.put(defaultMesh,catalog.getJSONObject(key));
                        for(var selector:selectors.entrySet()){
                            JSONObject record=records.get(selector.getValue());
                            if(record==null){
                                String selectedKey=key+"|"+selector.getKey().toLowerCase(Locale.ROOT);
                                String selectedId=HexFormat.of().formatHex(java.security.MessageDigest.getInstance("SHA-256").digest(selectedKey.getBytes(java.nio.charset.StandardCharsets.UTF_8))).substring(0,24);
                                Path selectedFile=destination.resolve(selectedId+".json");JSONObject selected;
                                if(Files.exists(selectedFile)&&Files.getLastModifiedTime(selectedFile).compareTo(Files.getLastModifiedTime(file))>=0&&new JSONObject(Files.readString(selectedFile)).optInt("format")==2)selected=new JSONObject(Files.readString(selectedFile));
                                else{selected=x(file,selector.getKey());selected.put("format",2).put("key",selectedKey).put("id",selectedId).put("source_size",Files.size(file));Files.writeString(selectedFile,selected.toString());}
                                record=new JSONObject().put("id",selectedId).put("file",selectedFile.getFileName().toString()).put("skeletal",selected.has("bones"));records.put(selector.getValue(),record);
                            }
                            catalog.put(key+"|"+selector.getKey().toLowerCase(Locale.ROOT),record);
                        }
                    }finally{JAssImpImporter.takeOutTheTrash(aliases);}
                }
            }catch(Throwable error){Throwable cause=error instanceof InvocationTargetException?error.getCause():error;errors.put(key,cause.toString());System.err.println("heyzed entity resource: "+key+": "+cause);}
            if(++done%50==0)System.out.println("heyzed entity meshes: "+done+" / "+files.size());
        }
        Path pending=destination.resolve("catalog.tmp");Files.writeString(pending,new JSONObject().put("schema",1).put("models",catalog).put("errors",errors).toString());Files.move(pending,destination.resolve("catalog.json"),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
        System.out.println("heyzed entity mesh preparation complete: "+catalog.length()+" meshes, "+errors.length()+" unavailable");
        System.exit(errors.length()==0?0:2);
    }
}
