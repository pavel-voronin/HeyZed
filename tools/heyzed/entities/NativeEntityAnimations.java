package com.pavelvoronin.heyzed.entities;

import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import org.json.*;
import jassimp.*;
import zombie.core.skinnedmodel.animation.*;
import zombie.core.skinnedmodel.model.jassimp.*;

/** Offline source clips only; no game, renderer or OpenGL context is started. */
public final class NativeEntityAnimations {
    private static final int FORMAT=2;
    private static String sha256(Path path) throws Exception {
        MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(var input=Files.newInputStream(path)) {
            byte[] buffer=new byte[65536];int count;
            while((count=input.read(buffer))!=-1)digest.update(buffer,0,count);
        }
        return HexFormat.of().formatHex(digest.digest());
    }
    private static JSONObject extract(Path source) throws Exception {
        AiScene scene=Jassimp.importFile(source.toString(),EnumSet.of(AiPostProcessSteps.MAKE_LEFT_HANDED,AiPostProcessSteps.TRIANGULATE));
        try {
            ProcessedAiSceneParams parameters=ProcessedAiSceneParams.create();
            parameters.scene=scene;parameters.mode=JAssImpImporter.LoadMode.AnimationOnly;
            if(source.toString().toLowerCase(Locale.ROOT).endsWith(".fbx")) {
                parameters.animBonesScaleModifier=.01f;
                parameters.animBonesRotateModifier=new org.lwjgl.util.vector.Quaternion();
                parameters.animBonesRotateModifier.setFromAxisAngle(new org.lwjgl.util.vector.Vector4f(1,0,0,-(float)Math.PI/2));
            }
            Object skeleton=NativeEntityAssets.field(ProcessedAiScene.process(parameters),"skeleton");
            if(skeleton==null)throw new IllegalArgumentException("Animation source has no skeleton: "+source);
            @SuppressWarnings("unchecked") Map<String,AnimationClip> clips=(Map<String,AnimationClip>)NativeEntityAssets.field(skeleton,"clips");
            JSONArray output=new JSONArray();
            for(var item:new TreeMap<>(clips).entrySet()) {
                AnimationClip clip=item.getValue();Map<String,JSONArray> tracks=new TreeMap<>();
                for(Keyframe frame:clip.getKeyframes()) {
                    if(frame.boneName==null||frame.position==null||frame.rotation==null)throw new IllegalStateException("Incomplete native animation frame");
                    var scale=frame.scale==null?new org.lwjgl.util.vector.Vector3f(1,1,1):frame.scale;
                    var matrix=zombie.core.skinnedmodel.HelperFunctions.CreateFromQuaternionPositionScale(frame.position,frame.rotation,scale,new org.lwjgl.util.vector.Matrix4f());
                    tracks.computeIfAbsent(frame.boneName,k->new JSONArray()).put(new JSONObject()
                        .put("time",frame.time).put("matrix",NativeEntityAssets.matrix(matrix)));
                }
                output.put(new JSONObject().put("name",clip.name).put("duration",clip.getDuration()).put("tracks",tracks));
            }
            if(output.isEmpty())throw new IllegalArgumentException("Animation source contains no clips: "+source);
            return new JSONObject().put("format",FORMAT).put("clips",output);
        } finally {JAssImpImporter.takeOutTheTrash(scene);}
    }
    public static void main(String[] args) throws Exception {
        Path game=Path.of(args[0]).toAbsolutePath(),destination=Path.of(args[1]).toAbsolutePath();
        Path importer=Path.of(AnimationClip.class.getProtectionDomain().getCodeSource().getLocation().toURI());
        if(!Files.isSameFile(importer,game.resolve("projectzomboid.jar")))throw new IllegalArgumentException("The installed game and importer classpath must match");
        String importerDigest=sha256(importer);
        Path source=game.resolve(args[2]).normalize();
        if(!source.startsWith(game)||!Files.exists(source))throw new IllegalArgumentException("Source must be inside the installed game");
        List<Path> sources;
        if(Files.isDirectory(source)) {
            try(var paths=Files.walk(source)) {sources=paths.filter(Files::isRegularFile).filter(p->p.toString().toLowerCase(Locale.ROOT).matches(".*\\.(x|fbx)$")).sorted().toList();}
        } else sources=List.of(source);
        String filter=args.length>3?args[3]:".*";
        Files.createDirectories(destination);boolean initialized=false;JSONObject catalog=new JSONObject(),errors=new JSONObject();
        int done=0;
        for(Path file:sources) {
            String key=game.relativize(file).toString().replace('\\','/').toLowerCase(Locale.ROOT);
            if(!key.matches(filter))continue;
            String digest=sha256(file);
            Path target=destination.resolve(HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(key.getBytes(java.nio.charset.StandardCharsets.UTF_8)))+".json");
            try {
                JSONObject data=Files.isRegularFile(target)?new JSONObject(Files.readString(target)):null;
                if(data==null||data.optInt("format")!=FORMAT||!digest.equals(data.optString("source_sha256"))||!importerDigest.equals(data.optString("importer_sha256"))) {
                    if(!initialized){JAssImpImporter.Init();initialized=true;}
                    data=extract(file).put("source",key).put("source_sha256",digest).put("importer_sha256",importerDigest);
                    Path pending=target.resolveSibling(target.getFileName()+".tmp");Files.writeString(pending,data.toString());
                    Files.move(pending,target,StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
                }
                catalog.put(key,new JSONObject().put("file",target.getFileName().toString()).put("sha256",digest));
            } catch(Exception failure){errors.put(key,failure.toString());System.err.println("heyzed animation resource: "+key+": "+failure);}
            if(++done%50==0)System.out.println("heyzed animation sources: "+done);
        }
        Path target=destination.resolve("catalog.json"),pending=destination.resolve("catalog.tmp");
        Files.writeString(pending,new JSONObject().put("format",FORMAT).put("importer_sha256",importerDigest).put("sources",catalog).put("filter",filter).put("errors",errors).toString());
        Files.move(pending,target,StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
        System.out.println("heyzed animation preparation complete: "+catalog.length()+" sources, "+errors.length()+" failures");
        if(errors.length()!=0)System.exit(2);
    }
}
