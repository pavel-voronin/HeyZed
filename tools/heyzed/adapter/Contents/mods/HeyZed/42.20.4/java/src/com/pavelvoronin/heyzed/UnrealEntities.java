package com.pavelvoronin.heyzed;

import java.nio.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import org.json.*;
import org.joml.Matrix4f;
import me.zed_0xff.zombie_buddy.Patch;
import zombie.characters.*;
import zombie.characters.animals.IsoAnimal;
import zombie.core.skinnedmodel.ModelManager;
import zombie.core.skinnedmodel.advancedanimation.AnimatedModel.AnimatedModelInstanceRenderData;
import zombie.core.skinnedmodel.model.*;
import zombie.core.textures.*;
import zombie.core.opengl.RenderThread;
import zombie.iso.*;
import zombie.iso.objects.IsoDeadBody;
import zombie.network.id.ObjectIDType;
import zombie.vehicles.BaseVehicle;

/** Entity lifecycle, appearance and pose have independent ownership from terrain. */
public final class UnrealEntities {
    private static final class UnfinishedPlayerAppearance extends IllegalStateException {
        UnfinishedPlayerAppearance(){super("Native player skinned appearance is still empty");}
    }
    private static final IdentityHashMap<IsoMovingObject,Entry> entries=new IdentityHashMap<>();
    private static final IdentityHashMap<IsoGameCharacter,Boolean> managed=new IdentityHashMap<>();
    private static volatile Set<IsoGameCharacter> ownedSnapshot=Set.of();
    private static final ConcurrentLinkedQueue<String> events=new ConcurrentLinkedQueue<>();
    private static final ConcurrentHashMap<String,PosePacket> poses=new ConcurrentHashMap<>();
    private static final ConcurrentLinkedQueue<String> poseOrder=new ConcurrentLinkedQueue<>();
    private static final Set<String> queuedPoses=ConcurrentHashMap.newKeySet();
    private static final AtomicReference<PosePacket> seatTransition=new AtomicReference<>();
    private static final ConcurrentLinkedQueue<Appearance> ready=new ConcurrentLinkedQueue<>();
    private static final AtomicInteger jobs=new AtomicInteger();
    private static final ExecutorService encoder=Executors.newSingleThreadExecutor(r->{Thread t=new Thread(r,"heyzed-entity-appearance");t.setDaemon(true);return t;});
    private static final Path textures=Path.of(System.getProperty("user.home"),".heyzed","bridge","entity-textures");
    private static final Matrix4f basis=new Matrix4f().m00(-100).m11(0).m12(100).m21(100).m22(0);
    private static long nextId,nextSample;
    private static long nextStatus,nextPosePrune;
    private static volatile long scope;
    private static boolean internalCull;
    private static volatile String loadingPoseStatus="not_requested";
    private static final class Entry {
        final String id;IsoMovingObject object;final long generation=scope;
        UnrealCorpseVisual corpse;long corpseRevision=1;float corpseScale;Matrix4f corpsePlacement;
        zombie.core.skinnedmodel.animation.AnimationPlayer sourceAnimation;
        String localAnimationKey="",boneAction="";long revision,sequence,appearance,retryAt,vehiclePose,nextPose,nextBones;int failures;boolean busy,published,wasSeated,localAnimation,detailWanted=true;double x,y,z;long at;
        Entry(IsoMovingObject object){this.object=object;String inherited=object instanceof IsoZombie zombie?UnrealCrowd.claimVirtual(zombie):null;id=inherited==null?Long.toString(++nextId):inherited;}
    }
    private static final class PosePacket {
        final String id;final long epoch,sequence,sample,appearance;final float[] world;final double vx,vy,vz;final byte[] bones,locals;final boolean seated;final String vehicle;final int seat;final JSONObject animation;String encoded;byte[] encodedBinary;
        PosePacket(String id,long epoch,long sequence,long sample,long appearance,float[] world,double vx,double vy,double vz,byte[] bones,byte[] locals,boolean seated,String vehicle,int seat,JSONObject animation){this.animation=animation;this.seated=seated;this.vehicle=vehicle;this.seat=seat;this.id=id;this.epoch=epoch;this.sequence=sequence;this.sample=sample;this.appearance=appearance;this.world=world;this.vx=vx;this.vy=vy;this.vz=vz;this.bones=bones;this.locals=locals;}
        String text(){if(encoded==null)encoded=new JSONObject().put("type","entity_pose").put("animation",animation).put("epoch",epoch).put("id",id).put("sequence",sequence).put("sample_ns",sample).put("world",new JSONArray(world)).put("velocity",new JSONArray(new double[]{vx,vy,vz})).put("pose",Base64.getEncoder().encodeToString(bones)).put("seated",seated).put("vehicle",vehicle).put("seat",seat).put("appearance_revision",appearance).put("locals",locals==null?"":Base64.getEncoder().encodeToString(locals)).toString();return encoded;}
        byte[] binary(){
            if(encodedBinary!=null)return encodedBinary;
            byte[] key=id.getBytes(java.nio.charset.StandardCharsets.UTF_8),car=vehicle.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            byte[] animationBytes=animation.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);
            int localBytes=locals==null?0:locals.length;
            if(world.length!=16||bones.length%64!=0||localBytes%64!=0)throw new IllegalArgumentException("Invalid native pose matrices");
            long size=156L+key.length+car.length+bones.length+localBytes+animationBytes.length;
            if(size>UnrealMemory.SLOT-16)throw new IllegalArgumentException("Native pose exceeds shared slot");
            ByteBuffer data=ByteBuffer.allocate((int)size).order(ByteOrder.LITTLE_ENDIAN);
            data.putInt(0x31504542).putInt(2).putInt(seated?1:0);
            data.putLong(epoch).putLong(sequence).putLong(sample).putLong(appearance);
            data.putInt(key.length).putInt(car.length).putInt(seat).putInt(bones.length/64).putInt(localBytes/64).putInt(animationBytes.length);
            for(float value:world)data.putFloat(value);
            data.putDouble(vx).putDouble(vy).putDouble(vz);
            data.put(key).put(car).put(bones);if(locals!=null)data.put(locals);data.put(animationBytes);
            encodedBinary=data.array();return encodedBinary;
        }
    }
    private record Appearance(Entry entry,long signature,JSONObject state,Throwable error){}
    private static void dirtyCorpse(IsoDeadBody body){Entry entry=entries.get(body);if(entry!=null)++entry.corpseRevision;}
    @Patch(className="zombie.iso.objects.IsoDeadBody",methodName="invalidateCorpse",warmUp=true)
    public static class CorpseChanged {@Patch.OnEnter public static void enter(@Patch.This IsoDeadBody body){dirtyCorpse(body);}}
    @Patch(className="zombie.iso.objects.IsoDeadBody",methodName="setInvalidateNextRender",warmUp=true)
    public static class CorpseRenderChanged {@Patch.OnEnter public static void enter(@Patch.This IsoDeadBody body,@Patch.Argument(0) boolean invalidate){if(invalidate)dirtyCorpse(body);}}
    private static void closeCorpse(Entry entry){if(entry.corpse!=null){entry.corpse.close();entry.corpse=null;}}
    private static Matrix4f corpseWorld(IsoDeadBody body,float scale){
        return new Matrix4f().translation(body.getX()*100,body.getY()*100,body.getZ()*(float)UnrealMetrics.FLOOR_CM).scale(scale)
            .mul(new Matrix4f().m11(0).m12(1).m21(1).m22(0)).rotateY(-body.getAngle()-(float)Math.PI/2).scale(-1,1,1).mul(new Matrix4f(basis).invert());
    }
    private static boolean captureCorpse(Entry entry,IsoDeadBody body,long epoch,long now,boolean budget){
        boolean preparing=false;
        if(!entry.busy&&entry.appearance!=entry.corpseRevision&&now>=entry.retryAt&&budget&&jobs.get()<4){
            preparing=true;
            try{
                if(entry.corpse==null)entry.corpse=new UnrealCorpseVisual(body);
                ModelInstance root=entry.corpse.prepare();
                if(root==null)entry.retryAt=now+100_000_000L;
                if(root!=null){entry.corpseScale=body.isAnimal()?150*body.getAnimalSize():humanScale(root);entry.busy=true;jobs.incrementAndGet();appearance(entry,root,epoch,entry.corpseRevision,entry.corpse.creator);}
            }catch(Throwable failure){closeCorpse(entry);entry.retryAt=now+500_000_000L;if(entry.failures++%30==0)System.err.println("[heyzed] Corpse appearance: "+failure);}
        }
        if(entry.published&&entry.corpseScale>0){
            Matrix4f placement=corpseWorld(body,entry.corpseScale);
            if(entry.corpsePlacement==null||!placement.equals(entry.corpsePlacement,0.00001f)){
                entry.corpsePlacement=new Matrix4f(placement);
                PosePacket packet=new PosePacket(entry.id,epoch,++entry.sequence,now,entry.revision,placement.get(new float[16]),0,0,0,new byte[0],null,false,"",-1,new JSONObject().put("mode","stream"));
                poses.put(entry.id,packet);if(queuedPoses.add(entry.id))poseOrder.add(entry.id);
            }
        }
        return preparing;
    }
    private record Part(ModelInstance instance,JSONObject descriptor){}
    private static void completed(Appearance value){if(value.entry.generation==scope)ready.add(value);else if(value.entry.corpse!=null)ready.add(value);}
    public static boolean owns(IsoGameCharacter character){return ownedSnapshot.contains(character);}
    public static boolean interceptCull(IsoGameCharacter character,boolean wanted){if(internalCull||!managed.containsKey(character))return false;managed.put(character,wanted);return wanted;}
    @Patch(className="zombie.characters.IsoGameCharacter",methodName="allowsInvisibleAnimationSkips",warmUp=true)
    public static class AnimationVisibility {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This IsoGameCharacter character){return owns(character);}}
    @Patch(className="zombie.characters.IsoGameCharacter",methodName="setSceneCulled",warmUp=true)
    public static class SceneCull {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This IsoGameCharacter character,@Patch.Argument(0) boolean wanted){return interceptCull(character,wanted);}}
    static JSONArray joml(Matrix4f m){float[] a=new float[16];m.get(a);return new JSONArray(a);}
    static JSONArray row(Matrix4f m){return joml(new Matrix4f(m).transpose());}
    static void putMatrix(ByteBuffer b,org.lwjgl.util.vector.Matrix4f m){b.putFloat(m.m00).putFloat(m.m10).putFloat(m.m20).putFloat(m.m30).putFloat(m.m01).putFloat(m.m11).putFloat(m.m21).putFloat(m.m31).putFloat(m.m02).putFloat(m.m12).putFloat(m.m22).putFloat(m.m32).putFloat(m.m03).putFloat(m.m13).putFloat(m.m23).putFloat(m.m33);}
    private static ModelInstance model(IsoMovingObject object){if(object instanceof IsoGameCharacter c)return c.getModel();if(object instanceof BaseVehicle v&&v.getSprite()!=null&&v.getSprite().modelSlot!=null)return v.getSprite().modelSlot.model;return null;}
    private static Matrix4f world(IsoMovingObject object,ModelInstance root){
        if(object instanceof IsoDeadBody body)return corpseWorld(body,body.isAnimal()?150*body.getAnimalSize():humanScale(root));
        if(object instanceof BaseVehicle v){var p=v.jniTransform.origin;return new Matrix4f().translation((p.x+zombie.core.physics.WorldSimulation.instance.offsetX)*100,(p.z+zombie.core.physics.WorldSimulation.instance.offsetY)*100,p.y*100);}
        if(object instanceof IsoGameCharacter character&&character.isSeatedInVehicle()){
            BaseVehicle vehicle=character.getVehicle();var seat=vehicle.getPassengerLocalPos(vehicle.getSeat(character),new org.joml.Vector3f());
            if(seat!=null){var rotation=vehicle.vehicleTransform.getUnnormalizedRotation(new org.joml.Quaternionf());return world(vehicle,null).mul(basis).rotate(rotation).rotateY((float)Math.PI).translate(seat.x,seat.y,-seat.z).scale(humanScale(root)/100).mul(new Matrix4f(basis).invert());}
        }
        float angle=root.animPlayer==null?0:root.animPlayer.getRenderedAngle();float size=object instanceof IsoAnimal animal?150*animal.getAnimalSize():humanScale(root);
        return new Matrix4f().translation(object.getX()*100,object.getY()*100,object.getZ()*(float)UnrealMetrics.FLOOR_CM).scale(size)
            .mul(new Matrix4f().m11(0).m12(1).m21(1).m22(0)).rotateY(-angle).scale(-1,1,1).mul(new Matrix4f(basis).invert());
    }
    private static float humanScale(ModelInstance root){float height=root.model.mesh.maxXyz.y-root.model.mesh.minXyz.y;return height>.1f&&height<3?170/height:175.64f;}
    private static String kind(IsoMovingObject object){return object instanceof IsoDeadBody?"corpse":object instanceof IsoAnimal?"animal":object instanceof IsoPlayer?"player":object instanceof BaseVehicle?"vehicle":"zombie";}
    private static String key(ModelInstance instance){
        String p=instance.model.mesh.fullPath;if(p==null)return "";p=p.replace('\\','/').toLowerCase(Locale.ROOT);int media=p.indexOf("media/");if(media>=0)p=p.substring(media);
        String selector=instance.model.mesh.getPath().getPath();int bar=selector.indexOf('|');if(bar>=0)p+=selector.substring(bar).toLowerCase(Locale.ROOT);return p;
    }
    private static long signature(ModelInstance instance,int depth){
        if(instance==null||depth>12)return 0;long h=System.identityHashCode(instance)*31L+System.identityHashCode(instance.tex);
        h=31*h+System.identityHashCode(instance.model);
        if(instance.model!=null){h=31*h+(instance.model.isReady()?1:0);h=31*h+System.identityHashCode(instance.model.tex);if(instance.model.mesh!=null)h=31*h+instance.model.mesh.modificationCount;}
        Texture source=instance.tex!=null?instance.tex:instance.model==null?null:instance.model.tex;
        if(source instanceof SmartTexture smart)h=31*h+System.identityHashCode(smart.result);
        h=31*h+Float.floatToIntBits(instance.hue);h=31*h+Float.floatToIntBits(instance.tintR);h=31*h+Float.floatToIntBits(instance.tintG);h=31*h+Float.floatToIntBits(instance.tintB);
        if(instance instanceof VehicleModelInstance v){for(Texture texture:new Texture[]{v.textureMask,v.textureRust,v.textureLights,v.textureDamage1Overlay,v.textureDamage1Shell,v.textureDamage2Overlay,v.textureDamage2Shell})h=31*h+(texture==null?0:31L*System.identityHashCode(texture)+(texture.isReady()?1:0));h=31*h+v.painColor.hashCode();h=31*h+Float.floatToIntBits(v.textureRustA);h=31*h+Arrays.hashCode(v.textureUninstall1);h=31*h+Arrays.hashCode(v.textureUninstall2);h=31*h+Arrays.hashCode(v.textureDamage1Enables1);h=31*h+Arrays.hashCode(v.textureDamage2Enables1);h=31*h+Arrays.hashCode(v.textureDamage1Enables2);h=31*h+Arrays.hashCode(v.textureDamage2Enables2);h=31*h+Arrays.hashCode(v.textureLightsEnables1);h=31*h+Arrays.hashCode(v.textureLightsEnables2);h=31*h+Arrays.hashCode(v.matrixBlood1Enables1);h=31*h+Arrays.hashCode(v.matrixBlood1Enables2);h=31*h+Arrays.hashCode(v.matrixBlood2Enables1);h=31*h+Arrays.hashCode(v.matrixBlood2Enables2);}
        for(var child:instance.sub)h=31*h+signature(child,depth+1);return h;
    }
    private static JSONArray boneNames(ModelInstance root){
        JSONArray names=new JSONArray();if(root.animPlayer==null||!root.animPlayer.isReady())return names;
        String[] ordered=new String[root.animPlayer.getModelTransformsCount()];root.animPlayer.getSkinningBoneIndices().forEach((name,index)->{if(index<ordered.length)ordered[index]=name;});for(String name:ordered)names.put(name==null?"":name);return names;
    }
    public static boolean prepareLoadingPose(IsoPlayer player){
        if(player==null)return false;
        ModelManager.instance.update();
        if(!player.hasActiveModel()||player.isSceneCulled()){internalCull=true;try{player.setSceneCulled(false);}finally{internalCull=false;}}
        player.checkUpdateModelTextures();ModelInstance root=model(player);
        if(root==null){loadingPoseStatus="model_missing";return false;}
        if(root.animPlayer==null){loadingPoseStatus="animation_player_missing";return false;}
        if(!root.animPlayer.isReady()){loadingPoseStatus="animation_player_loading";return false;}
        var animation=root.animPlayer;
        if(animation.isBoneTransformsNeedFirstFrame()||animation.getMultiTrack().getTrackCount()==0){
            String state=player.getActionContext().getCurrentStateName();if(state==null||state.isBlank())state="idle";
            var animator=player.getAdvancedAnimator();if(animator.animSet==null)animator.init(player,player.GetAnimSetName());if(!animator.containsState(state)){loadingPoseStatus="animation_state_missing:"+state;return false;}
            List<String> children=new ArrayList<>();for(var child:player.getActionContext().getChildStates())children.add(child.getName());
            animator.setState(state,children);animator.update(0f);
            var clocks=new IdentityHashMap<zombie.core.skinnedmodel.animation.AnimationTrack,Float>();
            for(var track:animation.getMultiTrack().getTracks())clocks.put(track,track.getCurrentTimeValue());
            if(animation.isBoneTransformsNeedFirstFrame())animator.update(1f/60f);
            boolean update=animation.updateBones;animation.updateBones=true;try{animation.Update(0f);}finally{animation.updateBones=update;}
            float clockStep=0;for(var clock:clocks.entrySet())clockStep=Math.max(clockStep,Math.abs(clock.getKey().getCurrentTimeValue()-clock.getValue()));
            var deferred=animation.getDeferredMovement(new zombie.iso.Vector2(),false);
            if(clockStep>1e-6f||deferred.getLengthSquared()>1e-12f){loadingPoseStatus="animation_initialization_changed_motion:clock="+clockStep+",deferred="+deferred.getLength();return false;}
            if(!animation.isBoneTransformsNeedFirstFrame())System.out.println("[heyzed] Native initial pose ready: clip clock step="+clockStep+", deferred movement="+deferred.getLength());
        }
        boolean ready=!animation.isBoneTransformsNeedFirstFrame()&&animation.getMultiTrack().getTrackCount()>0;loadingPoseStatus=ready?"ready":"animation_first_pose_pending:tracks="+animation.getMultiTrack().getTrackCount()+",need_first="+animation.isBoneTransformsNeedFirstFrame()+",state="+player.getActionContext().getCurrentStateName();return ready;
    }
    static String crowdId(IsoZombie zombie){return entries.computeIfAbsent(zombie,Entry::new).id;}
    static void capture(IsoPlayer player,long epoch){
        if(!zombie.GameWindow.isIngameState())prepareLoadingPose(player);
        Appearance completed;while((completed=ready.poll())!=null){Entry e=completed.entry;e.busy=false;if(entries.get(e.object)!=e){closeCorpse(e);continue;}if(!e.detailWanted)continue;if(completed.error!=null){if(completed.error instanceof UnfinishedPlayerAppearance&&e.object instanceof IsoPlayer nativePlayer)nativePlayer.postUpdateModelTextures();e.retryAt=System.nanoTime()+500_000_000L;if(e.failures++%30==0)System.err.println("[heyzed] Entity appearance: "+completed.error);continue;}e.failures=0;if(events.size()>=2048)continue;if(e.object instanceof IsoDeadBody){e.corpsePlacement=null;closeCorpse(e);}e.appearance=completed.signature;e.published=true;e.localAnimation=false;e.nextBones=0;completed.state.put("revision",++e.revision);events.add(completed.state.toString());}
        long now=System.nanoTime();if(now<nextSample)return;nextSample=UnrealCaptureClock.next(nextSample,now,50_000_000L);
        UnrealCrowd.capture(player,epoch,now);
        Set<IsoMovingObject> found=Collections.newSetFromMap(new IdentityHashMap<>());
        List<IsoMovingObject> candidates=new ArrayList<>();candidates.add(player);candidates.addAll(player.getCell().getZombieList());candidates.addAll(player.getCell().getAnimals());candidates.addAll(player.getCell().getVehicles());
        for(var identified:ObjectIDType.DeadBody.getObjects())if(identified instanceof IsoDeadBody body&&body.getCell()==player.getCell()&&body.getSquare()!=null)candidates.add(body);
        Set<IsoMovingObject> nativeObjects=Collections.newSetFromMap(new IdentityHashMap<>());nativeObjects.addAll(candidates);
        IdentityHashMap<zombie.core.skinnedmodel.animation.AnimationPlayer,Entry> deceased=new IdentityHashMap<>();
        for(Entry entry:entries.values())if(entry.object instanceof IsoGameCharacter character&&!nativeObjects.contains(character)&&entry.sourceAnimation!=null&&!entry.busy&&character.isDead())deceased.put(entry.sourceAnimation,entry);
        for(var object:candidates)if(object instanceof IsoDeadBody body&&!entries.containsKey(body)&&body.animationPlayer!=null){
            Entry prior=deceased.remove(body.animationPlayer);if(prior!=null){entries.remove(prior.object);release((IsoGameCharacter)prior.object);prior.object=body;prior.detailWanted=true;prior.appearance=0;prior.localAnimation=false;entries.put(body,prior);}
        }
        candidates.sort(Comparator.comparingDouble(object->{double dx=object.getX()-player.getX(),dy=object.getY()-player.getY();return dx*dx+dy*dy;}));
        int appearanceBudget=8;long appearanceWork=0;
        for(var object:candidates){
            if(object==null)continue;
            if(object instanceof IsoAnimal animal&&(animal.getHutch()!=null||animal.getVehicle()!=null))continue;
            found.add(object);if(!(object instanceof IsoDeadBody)&&object.getCurrentSquare()==null)continue;Entry e=entries.computeIfAbsent(object,Entry::new);
            if(object instanceof IsoZombie){
                boolean wanted=UnrealCrowd.wantsDetail(e.id);
                if(!wanted){
                    if(e.detailWanted){e.detailWanted=false;e.appearance=0;e.localAnimation=false;if(e.published){events.add(new JSONObject().put("type","entity_detail_release").put("epoch",epoch).put("id",e.id).toString());e.published=false;}release((IsoGameCharacter)object);}
                    continue;
                }
                if(!e.detailWanted){e.detailWanted=true;e.appearance=0;e.nextPose=0;e.nextBones=0;}
            }
            if(object instanceof IsoDeadBody body){if(captureCorpse(e,body,epoch,now,appearanceBudget>0))--appearanceBudget;continue;}
            if(object instanceof IsoGameCharacter character){managed.putIfAbsent(character,character.isSceneCulled());internalCull=true;try{if(character.isSceneCulled()||!character.hasActiveModel())character.setSceneCulled(false);character.checkUpdateModelTextures();}finally{internalCull=false;}}
            else if(object instanceof BaseVehicle vehicle&&(vehicle.getSprite()==null||vehicle.getSprite().modelSlot==null))ModelManager.instance.addVehicle(vehicle);
            ModelInstance root=model(object);if(root==null||root.model==null||root.model.mesh==null||!root.model.mesh.isReady())continue;
            if(object instanceof IsoGameCharacter && (root.animPlayer==null||!root.animPlayer.isReady()||root.animPlayer.isBoneTransformsNeedFirstFrame()||root.animPlayer.getMultiTrack().getTrackCount()==0))continue;
            if(object instanceof BaseVehicle vehicle&&root instanceof VehicleModelInstance&&vehicle.getScript()!=null)vehicle.updateLights();
            var creator=object instanceof IsoGameCharacter c?c.getTextureCreator():null;
            long fingerprint=signature(root,0)*31+System.identityHashCode(creator);
            if(!e.busy&&now>=e.retryAt&&fingerprint!=e.appearance&&appearanceBudget>0&&appearanceWork<1_000_000L&&jobs.get()<8){e.busy=true;jobs.incrementAndGet();appearanceBudget--;long captureStart=System.nanoTime();appearance(e,root,epoch,fingerprint,creator);appearanceWork+=System.nanoTime()-captureStart;}
            boolean seated=object instanceof IsoGameCharacter character&&character.isSeatedInVehicle();
            if(!e.published||now<e.nextPose)continue;
            e.nextPose=nextSample;
            JSONObject animation=object instanceof IsoGameCharacter c?UnrealEntityAnimation.describe(c,root):new JSONObject().put("mode","stream");
            String boneAction=animation.optString("mode")+":"+animation.optString("state")+":"+animation.optString("reason")+":"+animation.optString("key");
            boolean boneDue=now>=e.nextBones||!boneAction.equals(e.boneAction);
            int boneCount=!boneDue||root.animPlayer==null||(e.localAnimation&&animation.optString("mode").equals("local")&&e.localAnimationKey.equals(animation.optString("key")))?0:root.animPlayer.getModelTransformsCount();ByteBuffer pose=ByteBuffer.allocate(boneCount*64).order(ByteOrder.LITTLE_ENDIAN);
            e.sourceAnimation=root.animPlayer;if(object instanceof IsoGameCharacter)e.corpseScale=object instanceof IsoAnimal animal?150*animal.getAnimalSize():humanScale(root);
            if(root.animPlayer!=null&&root.animPlayer.isReady())for(int i=0;i<boneCount;i++)putMatrix(pose,root.animPlayer.getModelTransformAt(i));
            if(boneCount>0){double distance=Math.hypot(object.getX()-player.getX(),object.getY()-player.getY());e.nextBones=now+(distance<15?50_000_000L:distance<35?100_000_000L:200_000_000L);e.boneAction=boneAction;}
            double dt=(now-e.at)*1e-9;boolean continuous=e.at!=0&&dt>0&&dt<.5&&Math.hypot(object.getX()-e.x,object.getY()-e.y)<4;
            Matrix4f placement=world(object,root);
            float[] placementValues=placement.get(new float[16]);
            List<Matrix4f> localMatrices=new ArrayList<>();transforms(root,null,object instanceof BaseVehicle,localMatrices,0);
            ByteBuffer locals=ByteBuffer.allocate(localMatrices.size()*64).order(ByteOrder.LITTLE_ENDIAN);
            for(var local:localMatrices){float[] values=new float[16];local.get(values);for(float value:values)locals.putFloat(value);}
            if(object instanceof BaseVehicle){long transform=31L*Arrays.hashCode(locals.array())+Arrays.hashCode(placementValues);if(transform==e.vehiclePose&&e.sequence>0&&object!=player.getVehicle()){e.at=now;e.x=object.getX();e.y=object.getY();e.z=object.getZ();continue;}e.vehiclePose=transform;}
            BaseVehicle vehicle=seated?((IsoGameCharacter)object).getVehicle():null;String vehicleId=vehicle==null?"":entries.computeIfAbsent(vehicle,Entry::new).id;
            PosePacket packet=new PosePacket(e.id,epoch,++e.sequence,now,e.revision,placementValues,continuous?(object.getX()-e.x)/dt:0,continuous?(object.getY()-e.y)/dt:0,continuous?(object.getZ()-e.z)*UnrealMetrics.FLOOR_CM/100/dt:0,pose.array(),fingerprint==e.appearance?locals.array():null,seated,vehicleId,vehicle==null?-1:vehicle.getSeat((IsoGameCharacter)object),animation);
            poses.put(e.id,packet);if(queuedPoses.add(e.id))poseOrder.add(e.id);if(object==player&&seated!=e.wasSeated)seatTransition.set(packet);
            e.wasSeated=seated;
            e.at=now;e.x=object.getX();e.y=object.getY();e.z=object.getZ();
        }
        for(var it=entries.entrySet().iterator();it.hasNext();){var item=it.next();if(found.contains(item.getKey()))continue;Entry e=item.getValue();if(e.published)events.add(new JSONObject().put("type","entity_state").put("epoch",epoch).put("id",e.id).put("revision",++e.revision).put("deleted",true).toString());poses.remove(e.id);if(!e.busy)closeCorpse(e);if(item.getKey() instanceof IsoGameCharacter c)release(c);it.remove();}
        Set<IsoGameCharacter> snapshot=Collections.newSetFromMap(new IdentityHashMap<>());snapshot.addAll(managed.keySet());ownedSnapshot=Collections.unmodifiableSet(snapshot);
        if(now>=nextStatus){nextStatus=now+2_000_000_000L;JSONObject status=new JSONObject().put("epoch",epoch).put("time_ms",System.currentTimeMillis()).put("loading_pose",loadingPoseStatus).put("crowd",UnrealCrowd.status());for(String k:List.of("player","zombie","animal","vehicle","corpse")){int source=0,nearby=0,published=0,waiting=0,failed=0;for(var o:candidates)if(kind(o).equals(k))source++;for(var e:entries.values())if(kind(e.object).equals(k)){nearby++;if(e.published)published++;if(e.busy)waiting++;if(e.failures>0)failed++;}status.put(k,new JSONObject().put("source",source).put("nearby",nearby).put("published",published).put("preparing",waiting).put("failed",failed));}String json=status.toString();encoder.submit(()->{try{Files.createDirectories(textures.getParent());Path tmp=textures.getParent().resolve("entities-status.tmp");Files.writeString(tmp,json);Files.move(tmp,textures.getParent().resolve("entities-status.json"),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);}catch(Exception ignored){}});}
    }
    private static void release(IsoGameCharacter c){Boolean culled=managed.remove(c);if(Boolean.TRUE.equals(culled)){internalCull=true;try{c.setSceneCulled(true);}finally{internalCull=false;}}}
    private static void transforms(ModelInstance instance,AnimatedModelInstanceRenderData parent,boolean vehicle,List<Matrix4f> out,int depth){
        if(depth>12||instance.model==null||instance.model.mesh==null||!instance.model.mesh.isReady())return;
        var data=new AnimatedModelInstanceRenderData();data.model=instance.model;data.modelInstance=instance;data.parent=parent;data.transformToParent(parent);
        Matrix4f local=data.xfrm;
        if(vehicle){if(instance instanceof VehicleModelInstance)local.set(((BaseVehicle)instance.object).renderTransform);else if(instance instanceof VehicleSubModelInstance sub&&sub.modelInfo!=null)local.set(sub.modelInfo.renderTransform);ModelInstanceRenderData.postMultiplyMeshTransform(local,instance.model.mesh);out.add(new Matrix4f(local));}
        else{if(parent==null&&instance.model.isStatic){ModelInstanceRenderData.postMultiplyMeshTransform(local,instance.model.mesh);local.scale(instance.scale).transpose();}out.add(new Matrix4f(local).transpose());}
        for(var child:instance.sub)transforms(child,data,vehicle,out,depth+1);
    }
    private static void parts(ModelInstance instance,AnimatedModelInstanceRenderData parent,List<Part> out,boolean vehicle,int depth){
        if(depth>12||instance.model==null||instance.model.mesh==null||!instance.model.mesh.isReady())return;
        var data=new AnimatedModelInstanceRenderData();data.model=instance.model;data.initModel(instance,parent);data.transformToParent(parent);
        Matrix4f local=data.xfrm;
        if(vehicle){if(instance instanceof VehicleModelInstance)local.set(((BaseVehicle)instance.object).renderTransform);else if(instance instanceof VehicleSubModelInstance sub&&sub.modelInfo!=null)local.set(sub.modelInfo.renderTransform);ModelInstanceRenderData.postMultiplyMeshTransform(local,instance.model.mesh);}
        else if(parent==null&&instance.model.isStatic){ModelInstanceRenderData.postMultiplyMeshTransform(local,instance.model.mesh);local.scale(instance.scale).transpose();}
        instance.renderRefCount++;if(instance.getTextureInitializer()!=null)instance.getTextureInitializer().renderMain();
        out.add(new Part(instance,new JSONObject().put("model",key(instance)).put("rigid",instance.model.isStatic).put("texture","").put("hue",instance.hue).put("local",vehicle?joml(local):row(local)).put("tint",new JSONArray(new float[]{instance.tintR,instance.tintG,instance.tintB}))));
        if(!vehicle&&instance.model.isStatic&&parent!=null){
            var attachment=parent.modelInstance.getAttachmentById(instance.attachmentNameParent);
            if(attachment==null&&instance.parentBoneName!=null)attachment=parent.modelInstance.getAttachmentById(instance.parentBoneName);
            String bone=attachment!=null?attachment.getBone():instance.parentBoneName;
            if(bone!=null)out.get(out.size()-1).descriptor.put("parent_bone",bone);
        }
        for(var child:instance.sub)parts(child,data,out,vehicle,depth+1);
    }
    private static boolean requiresPlayerBodyPixels(IsoPlayer player){
        var mask=new zombie.core.skinnedmodel.model.CharacterMask();mask.setAllVisible(true);
        var visuals=player.getItemVisuals();var group=zombie.characters.WornItems.BodyLocations.getGroup("Human");
        for(int i=visuals.size()-1;i>=0;--i){var visual=visuals.get(i);var clothing=visual.getClothingItem();if(clothing==null||!clothing.isReady()||zombie.core.skinnedmodel.population.PopTemplateManager.instance.isItemModelHidden(group,visuals,visual))continue;visual.getClothingItemCombinedMask(mask);}
        return !mask.isNothingVisible();
    }
    private static void appearance(Entry e,ModelInstance root,long epoch,long fingerprint,ModelInstanceTextureCreator creator){
        final boolean visiblePlayerBody=e.object instanceof IsoPlayer player&&requiresPlayerBodyPixels(player);
        List<Part> captured=new ArrayList<>();
        try{parts(root,null,captured,e.object instanceof BaseVehicle,0);}catch(Throwable error){for(var p:captured)ModelManager.instance.derefModelInstance(p.instance);e.busy=false;e.retryAt=System.nanoTime()+500_000_000L;jobs.decrementAndGet();if(e.failures++%30==0)System.err.println("[heyzed] Entity capture: "+error);return;}
        if(creator!=null)creator.renderRefCount++;
        JSONObject state=new JSONObject().put("type","entity_state").put("epoch",epoch).put("id",e.id).put("deleted",false).put("kind",kind(e.object)).put("bones",boneNames(root)).put("world",joml(world(e.object,root)));
        int count=root.animPlayer==null?0:root.animPlayer.getModelTransformsCount();ByteBuffer initial=ByteBuffer.allocate(count*64).order(ByteOrder.LITTLE_ENDIAN);
        if(root.animPlayer!=null&&root.animPlayer.isReady())for(int i=0;i<count;i++)putMatrix(initial,root.animPlayer.getModelTransformAt(i));
        final byte[] initialPose=initial.array();
        RenderThread.queueInvokeOnRenderContext(()->{
            try{
                if(creator!=null&&!creator.isRendered()){creator.render();if(!creator.isRendered())throw new IllegalStateException("Native character composition is not ready");}
                JSONArray descriptors=new JSONArray();List<NativeEntityTextures.Image> images=new ArrayList<>();NativeEntityTextures.Image vehicleImage=null;boolean visibleSkinnedPixels=false;
                for(var p:captured){var initializer=p.instance.getTextureInitializer();if(initializer!=null&&!initializer.isRendered())initializer.render();Texture texture=p.instance.tex!=null?p.instance.tex:p.instance.model.tex;if(texture==null)texture=Texture.getWhite();if(!texture.isReady())throw new IllegalStateException("Native entity texture not ready");
                    NativeEntityTextures.Image image;
                    if(root instanceof VehicleModelInstance v && p.instance.model.effect!=null && p.instance.model.effect.isVehicleShader()){
                        if(vehicleImage==null)vehicleImage=NativeEntityTextures.vehicle(v,NativeEntityTextures.read(v.tex!=null?v.tex:v.model.tex));image=vehicleImage;
                        p.descriptor.put("multi_uv",p.instance.model.effect.getName().contains("multiuv")).put("random_paint",!p.instance.model.effect.getName().contains("norandom"));p.descriptor.put("Lit",new JSONArray(NativeEntityTextures.zones(v.textureLightsEnables1,v.textureLightsEnables2)));
                    }else image=NativeEntityTextures.read(texture);
                    if(p.instance==root&&(e.object instanceof IsoGameCharacter||e.object instanceof IsoDeadBody)&&NativeEntityTextures.characterPlaceholder(image))throw new IllegalStateException("Native character appearance is still a placeholder");
                    if(p.instance==root&&visiblePlayerBody&&!NativeEntityTextures.hasVisiblePixels(image))throw new UnfinishedPlayerAppearance();
                    if(!p.instance.model.isStatic&&NativeEntityTextures.hasVisiblePixels(image))visibleSkinnedPixels=true;
                    descriptors.put(p.descriptor);images.add(image);}
                if(e.object instanceof IsoPlayer&&!visibleSkinnedPixels)throw new UnfinishedPlayerAppearance();
                state.put("parts",descriptors);
                encoder.submit(()->{try{state.put("pose",Base64.getEncoder().encodeToString(initialPose));Files.createDirectories(textures);for(int i=0;i<images.size();i++){var source=images.get(i);JSONObject car=NativeEntityTextures.nativeVehicle(source,textures);if(car!=null)descriptors.getJSONObject(i).put("vehicle",car);var image=NativeEntityTextures.hue(NativeEntityTextures.finish(source),(float)descriptors.getJSONObject(i).getDouble("hue"));NativeEntityTextures.save(image,textures);boolean glass=false;for(int p=3;p<image.rgba.length;p+=4)if((image.rgba[p]&255)>0&&(image.rgba[p]&255)<128){glass=true;break;}descriptors.getJSONObject(i).put("texture",NativeEntityTextures.hash(image)).put("glass",e.object instanceof BaseVehicle&&glass);}completed(new Appearance(e,fingerprint,state,null));}catch(Throwable error){completed(new Appearance(e,fingerprint,null,error));}finally{jobs.decrementAndGet();}});
            }catch(Throwable error){jobs.decrementAndGet();completed(new Appearance(e,fingerprint,null,error));}
            finally{
                for(var p:captured){if(p.instance.getTextureInitializer()!=null)p.instance.getTextureInitializer().postRender();ModelManager.instance.derefModelInstance(p.instance);}
                if(creator!=null&&--creator.renderRefCount==0&&creator.isRendered()){if(e.object instanceof IsoGameCharacter c&&c.getTextureCreator()==creator)c.setTextureCreator(null);if(e.corpse!=null&&e.corpse.creator==creator)e.corpse.creator=null;creator.postRender();}
                zombie.core.SpriteRenderer.ringBuffer.restoreBoundTextures=true;
            }
        });
    }
    static void flush(UnrealMemory memory){
        UnrealCrowd.flush(memory);
        long now=System.nanoTime();if(now>=nextPosePrune){nextPosePrune=now+1_000_000_000L;poseOrder.removeIf(id->{if(poses.containsKey(id))return false;queuedPoses.remove(id);return true;});}
        for(int i=0;i<16;i++){String event=events.peek();if(event==null||!memory.offer("entities",event))break;events.poll();}
        PosePacket transition=seatTransition.get();if(transition!=null&&memory.canOffer("entities")&&memory.offer("entities",transition.text()))seatTransition.compareAndSet(transition,null);
        int budget=32;
        while(budget>0&&memory.canOffer("poses")){
            String id=poseOrder.peek();if(id==null)break;PosePacket value=poses.get(id);
            if(value!=null&&!memory.offer("poses",value.binary()))break;
            if(value!=null){poses.remove(id,value);budget--;}
            poseOrder.poll();queuedPoses.remove(id);
            if(poses.containsKey(id)&&queuedPoses.add(id))poseOrder.add(id);
        }
    }
    static boolean animationCommand(JSONObject command,long epoch){
        if(command.optLong("generation",-1)!=epoch)return false;
        for(Entry entry:entries.values())if(entry.id.equals(command.optString("id"))&&entry.revision==command.optLong("revision",-1)){
            entry.localAnimation=command.optBoolean("enabled");entry.localAnimationKey=command.optString("clips");entry.nextPose=0;return true;
        }
        return false;
    }
    static void reconcile(long epoch){UnrealCrowd.reconcile();for(Entry e:entries.values()){e.appearance=0;e.wasSeated=false;e.localAnimation=false;}}
    static void reset(long epoch){UnrealCrowd.reset();UnrealEntityAnimation.reset();++scope;ownedSnapshot=Set.of();for(var c:new ArrayList<>(managed.keySet()))release(c);for(Entry entry:entries.values())if(!entry.busy)closeCorpse(entry);entries.clear();events.add(new JSONObject().put("type","entity_reset").put("epoch",epoch).toString());poses.clear();poseOrder.clear();queuedPoses.clear();seatTransition.set(null);Appearance pending;while((pending=ready.poll())!=null)closeCorpse(pending.entry);nextSample=0;}
    private UnrealEntities(){}
}
