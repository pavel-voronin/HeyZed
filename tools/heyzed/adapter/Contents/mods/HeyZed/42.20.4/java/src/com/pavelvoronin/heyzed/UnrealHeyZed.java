package com.pavelvoronin.heyzed;

import java.net.*;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicReference;
import java.util.zip.DeflaterOutputStream;
import me.zed_0xff.zombie_buddy.Patch;
import zombie.characters.IsoPlayer;
import zombie.characters.IsoGameCharacter;
import zombie.characters.component.CharacterInputComponent;

/** Opt-in, loopback-only experiment. Game objects stay on the game thread. */
public final class UnrealHeyZed {
    private static final int port=Integer.getInteger("heyzed.unreal.port",0);
    private static final long epoch=System.currentTimeMillis();
    private record Intent(long session,long sequence,long echo,int mask,long received,float yaw,float aim,int mode,boolean capture) {}
    private static final AtomicReference<Intent> incoming=new AtomicReference<>();
    private static final AtomicReference<String> outgoing=new AtomicReference<>();
    private static final AtomicReference<String> capture=new AtomicReference<>();
    private static final java.util.concurrent.ConcurrentLinkedQueue<UnrealGround.Block> ground=new java.util.concurrent.ConcurrentLinkedQueue<>();
    private static final AtomicReference<String> entityMotion=new AtomicReference<>();
    private static final AtomicReference<String> reconcile=new AtomicReference<>();
    private static Intent applied;
    private static int mask;
    private static long sequence;
    private static volatile IsoPlayer player;
    private static boolean started,previousPause;
    private static String bindings="";
    private static long bindingsAt;
    private static long motionAt,logicAt,previousLogicAt;
    private static double stepSeconds,previousStepSeconds;
    private static boolean lastDeferredMotion;
    private static boolean stepPending;
    private static volatile long extractionUs,publishUs,objectsUs,lightingUs,entitiesUs,groundUs,architectureUs;
    private static float motionX,motionY;
    private static volatile long inputExpirations,inputStepAgeUs,controlGapMaxUs,preparedWorkMaxUs;
    private static volatile boolean preparedFailed;

    public static void begin() {
        if(port==0)return;
        previousLogicAt=logicAt;logicAt=System.nanoTime();stepPending=false;
        previousStepSeconds=stepSeconds;stepSeconds=zombie.GameTime.getInstance().getRealworldSecondsSinceLastUpdate();
        if(!started){
            started=true;
            UnrealPreviewQuality.apply();
            previousPause=zombie.core.Core.getInstance().getOptionPauseOnFocusloss();
            zombie.core.Core.getInstance().setOptionPauseOnFocusloss(false);
            Thread worker=new Thread(UnrealHeyZed::network,"heyzed-unreal-loopback");worker.setDaemon(true);worker.start();
        }
        UnrealPreviewQuality.applyWorld();
        player=IsoPlayer.getInstance();
        UnrealStartup.poll();
        if(player!=null)UnrealBake.begin();
        long now=System.nanoTime();
        if(player!=null&&now>=bindingsAt){
            bindingsAt=now+1_000_000_000L;
            StringBuilder keys=new StringBuilder();
            for(String name:new String[]{"Forward","Backward","Left","Right","Run","Sprint","Crouch"}){
                if(!keys.isEmpty())keys.append(',');
                keys.append(org.lwjglx.input.KeyCodes.toGlfwKey(zombie.core.Core.getInstance().getKey(name)));
            }
            bindings=keys.toString();
        }
        Intent pending=incoming.get();
        applied=pending;
        long age=pending==null?Long.MAX_VALUE:now-pending.received;
        inputStepAgeUs=pending==null?-1:Math.max(0,age/1000);
        if(player!=null&&pending!=null&&age>=250_000_000L&&pending.mask!=0&&mask!=0)++inputExpirations;
        mask=!UnrealStartup.waiting()&&player!=null&&pending!=null&&age<250_000_000L?pending.mask:0;
    }
    public static void logicCompleted(){stepPending=port!=0&&player!=null&&(zombie.GameWindow.isIngameState()||UnrealStartup.snapshotAllowed());}
    public static boolean startupPeer(long session,long world){Intent intent=incoming.get();return world==epoch&&intent!=null&&intent.session==session;}
    public static void end() {
        if(port==0||!stepPending)return;
        UnrealNativeTiming.Work timing=UnrealNativeTiming.begin("adapter-capture");
        try {
        stepPending=false;
        if(player==null)return;
        if(player.getVehicle()==null&&applied!=null&&applied.mode==0&&System.nanoTime()-applied.received<250_000_000L&&!player.getIgnoreMovement()&&!player.isDead()&&!player.isRunning()&&!player.isSprinting()){
            double angle=Math.toRadians(applied.aim);
            player.setForwardDirection((float)Math.cos(angle),(float)Math.sin(angle));
        }
        String name=capture.getAndSet(null);
        if(name!=null&&zombie.core.Core.getInstance().isNoSave()){
            try{Class.forName("me.pavel.modstudio.ScenarioHost").getMethod("screenshot",String.class).invoke(null,name);}
            catch(Exception failure){System.err.println("[heyzed] Unreal bridge capture failed: "+failure);}
        }
        long ack=applied==null?0:applied.sequence,echo=applied==null?0:applied.echo,session=applied==null?0:applied.session;
        int mode=player.isPlayerMoving()?(player.isSprinting()?3:player.isRunning()?2:1):0;
        long applyUs=applied==null?0:(System.nanoTime()-applied.received)/1000;
        boolean deferredMotion=player.getVehicle()==null&&!player.isRagdollSimulationActive();
        long motionNow=deferredMotion&&previousLogicAt!=0?previousLogicAt:logicAt;
        float x=player.getX(),y=player.getY();
        double dt=deferredMotion&&previousStepSeconds>0?previousStepSeconds:stepSeconds;
        double elapsed=(motionNow-motionAt)*1e-9;
        boolean continuous=motionAt!=0&&deferredMotion==lastDeferredMotion&&elapsed>0&&elapsed<.5&&dt>0&&Double.isFinite(dt)&&Math.hypot(x-motionX,y-motionY)<4;
        double vx=continuous?(x-motionX)/dt:0,vy=continuous?(y-motionY)/dt:0;
        motionAt=motionNow;motionX=x;motionY=y;lastDeferredMotion=deferredMotion;
        outgoing.set("S3 "+epoch+" "+(++sequence)+" "+session+" "+ack+" "+echo+" "+player.getPlayerNum()+" "+x+" "+y+" "+player.getZ()+" "+player.getForwardDirectionX()+" "+player.getForwardDirectionY()+" "+mode+" "+mask+" "+applyUs+" "+bindings+" "+motionNow+" "+vx+" "+vy+" "+(player.isSneaking()?1:0));
        long phase=System.nanoTime();
        UnrealObjects.execute(player,epoch);objectsUs=(System.nanoTime()-phase)/1000;
        phase=System.nanoTime();UnrealLighting.capture(player,epoch);lightingUs=(System.nanoTime()-phase)/1000;
        if(reconcile.getAndSet(null)!=null){UnrealGround.reconcile(player);UnrealObjects.reconcile();UnrealVegetation.reconcile();UnrealFurniture.reconcile();UnrealLighting.reconcile();UnrealArchitecture.reconcile();UnrealEntities.reconcile(epoch);}
        phase=System.nanoTime();
        if(zombie.GameWindow.isIngameState()&&UnrealStartup.waiting())UnrealEntities.prepareLoadingPose(player);
        UnrealEntities.capture(player,epoch);entitiesUs=(System.nanoTime()-phase)/1000;
        long extractionStart=System.nanoTime();
        if(ground.size()<32)ground.addAll(UnrealGround.snapshot(player,epoch));
        groundUs=(System.nanoTime()-extractionStart)/1000;
        phase=System.nanoTime();UnrealArchitecture.tick(player,epoch);architectureUs=(System.nanoTime()-phase)/1000;
        UnrealFurniture.tick(player,epoch);
        extractionUs=(System.nanoTime()-extractionStart)/1000;
        }finally{UnrealNativeTiming.finish(timing);}
    }
    public static boolean owns(CharacterInputComponent input){return port!=0&&player!=null&&input.tryGetECSOwnerEntityAs(IsoGameCharacter.class)==player;}
    public static boolean down(CharacterInputComponent input,int bit){return owns(input)&&(mask&(1<<bit))!=0;}
    public static void movement(CharacterInputComponent input,zombie.iso.Vector2 vector){
        if(!owns(input)||player.getVehicle()!=null||applied==null||System.nanoTime()-applied.received>=250_000_000L)return;
        cameraSpace(vector,applied.yaw);
    }
    public static void aim(IsoPlayer source,zombie.iso.Vector2 vector){
        Intent intent=incoming.get();
        if(intent==null||source!=player||source.getVehicle()!=null||vector==null||keyboardSession()==0||!source.isAnyAimKeyDown())return;
        double angle=Math.toRadians(intent.aim);vector.set((float)Math.cos(angle),(float)Math.sin(angle));
    }
    static void cameraSpace(zombie.iso.Vector2 vector,float yaw){
        float forward=-vector.y,side=vector.x;
        double angle=Math.toRadians(yaw),c=Math.cos(angle),s=Math.sin(angle);
        double x=c*forward-s*side,y=s*forward+c*side;
        vector.set((float)((x-y)*Math.sqrt(.5)),(float)((x+y)*Math.sqrt(.5)));
    }
    static long keyboardOwner(){
        Intent intent=incoming.get();
        return port!=0&&player!=null&&intent!=null&&intent.capture&&!UnrealStartup.waiting()?intent.session:0;
    }
    static long keyboardSession(){
        Intent intent=incoming.get();
        return intent!=null&&System.nanoTime()-intent.received<250_000_000L?keyboardOwner():0;
    }
    public static void leave(){if(port!=0&&started){mask=0;motionAt=0;stepPending=false;UnrealStartup.reset();player=null;UnrealInput.reset();incoming.set(null);outgoing.set(null);ground.clear();UnrealGround.reset();UnrealArchitecture.reset();UnrealFurniture.reset();UnrealEntities.reset(epoch);zombie.core.Core.getInstance().setOptionPauseOnFocusloss(previousPause);}}

    private static void acceptMemory(String text){
        try{
            String[] f=text.trim().split(" ");
            if(f.length!=9||(!f[0].equals("I2")&&!f[0].equals("I3")))return;
            long session=Long.parseLong(f[1]),seq=Long.parseLong(f[2]),echo=Long.parseLong(f[3]);
            int keys=Integer.parseInt(f[4]),mode=Integer.parseInt(f[7]);
            float yaw=Float.parseFloat(f[5]),aim=Float.parseFloat(f[6]);
            if(keys<0||keys>127||mode<0||mode>2||!Float.isFinite(yaw)||!Float.isFinite(aim)||Math.abs(yaw)>360||Math.abs(aim)>360)return;
            Intent old=incoming.get();long now=System.nanoTime();
            if(old==null||old.session==session&&seq>old.sequence||now-old.received>250_000_000L)
                incoming.set(new Intent(session,seq,echo,keys,now,yaw,aim,mode,f[0].equals("I3")&&f[8].equals("1")));
        }catch(NumberFormatException ignored){}
    }

    private static void network(){
        try(UnrealMemory memory=new UnrealMemory(epoch); DatagramChannel socket=DatagramChannel.open()){
            socket.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"),port));
            socket.configureBlocking(false);
            byte[] bytes=new byte[512];ByteBuffer packet=ByteBuffer.wrap(bytes);
            AtomicReference<byte[]> preparedGround=new AtomicReference<>();
            Thread prepared=new Thread(()->prepared(memory,preparedGround),"heyzed-unreal-prepared");
            prepared.setDaemon(true);prepared.start();
            try {
            SocketAddress peer=null;String last=null,lastShared=null;
            byte[] groundPacket=null;long nextGroundSend=0,lastControl=System.nanoTime(),lastBootstrap=0;
            while(true){
                long controlNow=System.nanoTime();controlGapMaxUs=Math.max(controlGapMaxUs,(controlNow-lastControl)/1000);lastControl=controlNow;
                String input;
                while((input=memory.poll("input"))!=null)acceptMemory(input);
                while((input=memory.poll("commands"))!=null){
                    if(input.startsWith("L "))UnrealStartup.accept(input);
                    else if(input.startsWith("K ")){if(!UnrealStartup.waiting())UnrealInput.accept(input);}
                    else if(input.startsWith("R "))reconcile.set(input);
                    else if(UnrealObjects.commands.size()<256)UnrealObjects.commands.add(input);
                }
                String sharedState=outgoing.get();
                if(sharedState==null && controlNow-lastBootstrap>250_000_000L){memory.offer("motion",UnrealStartup.status());lastBootstrap=controlNow;}
                if(sharedState!=null&&sharedState!=lastShared){
                    org.json.JSONObject frame=new org.json.JSONObject().put("type","frame").put("player",sharedState).put("extract_us",extractionUs).put("publish_us",publishUs).put("world_id",UnrealBake.identity()).put("objects_us",objectsUs).put("lighting_us",lightingUs).put("entities_us",entitiesUs).put("ground_us",groundUs).put("architecture_us",architectureUs).put("input_expirations",inputExpirations).put("input_step_age_us",inputStepAgeUs).put("control_gap_max_us",controlGapMaxUs).put("prepared_work_max_us",preparedWorkMaxUs).put("prepared_failed",preparedFailed).put("startup_schema",3).put("startup_loading",UnrealStartup.waiting()).put("startup_entered",UnrealStartup.entered());
                    String entity=entityMotion.get();
                    if(entity!=null)frame.put("motion",new org.json.JSONObject(entity));
                    if(memory.offer("motion",frame.toString()))lastShared=sharedState;
                }
                for(int delivered=0;delivered<32;delivered++){
                    String event=UnrealObjects.events.peek();
                    if(event==null||!memory.offer("events",event))break;
                    UnrealObjects.events.poll();
                }
                UnrealFurniture.flush(memory);

                try{
                    packet.clear();SocketAddress sender=socket.receive(packet);
                    if(sender instanceof InetSocketAddress address && address.getAddress().isLoopbackAddress()){
                    String[] fields=new String(bytes,0,packet.position(),StandardCharsets.US_ASCII).trim().split(" ");
                    boolean modern=fields.length==9&&(fields[0].equals("I2")||fields[0].equals("I3"));
                    if(modern||(fields.length==5||fields.length==6)&&fields[0].equals("I")){
                        long session=Long.parseLong(fields[1]),seq=Long.parseLong(fields[2]),echo=Long.parseLong(fields[3]);int keys=Integer.parseInt(fields[4]);
                        float yaw=modern?Float.parseFloat(fields[5]):-135,aim=modern?Float.parseFloat(fields[6]):-135;
                        int mode=modern?Integer.parseInt(fields[7]):1;
                        if(!Float.isFinite(yaw)||!Float.isFinite(aim)||Math.abs(yaw)>360||Math.abs(aim)>360||mode<0||mode>2)continue;
                        Intent previous=incoming.get();long now=System.nanoTime();
                        boolean same=previous!=null&&previous.session==session&&sender.equals(peer);
                        if(seq>0&&keys>=0&&keys<128&&(previous==null||same&&seq>previous.sequence||now-previous.received>250_000_000L)){
                            incoming.set(new Intent(session,seq,echo,keys,now,yaw,aim,mode,fields[0].equals("I3")&&fields[8].equals("1")));peer=sender;
                            String name=fields[0].equals("I3")?"-":modern?fields[8]:fields.length==6?fields[5]:"-";
                            if(!name.equals("-")&&name.matches("[a-z0-9_-]{1,60}"))capture.set(name);
                        }
                    }
                    }
                }catch(NumberFormatException ignored){}
                String state=outgoing.get();
                if(peer!=null&&state!=null&&state!=last){byte[] data=state.getBytes(StandardCharsets.US_ASCII);socket.send(ByteBuffer.wrap(data),peer);last=state;}
                byte[] current=preparedGround.get();
                if(current!=groundPacket){groundPacket=current;nextGroundSend=0;}
                long now=System.nanoTime();
                if(peer!=null&&groundPacket!=null&&now>=nextGroundSend){socket.send(ByteBuffer.wrap(groundPacket),peer);nextGroundSend=now+1_000_000_000L;}
                java.util.concurrent.locks.LockSupport.parkNanos(1_000_000L);
            }
            } finally {prepared.interrupt();prepared.join();}
        }catch(Exception failure){System.err.println("[heyzed] Unreal bridge stopped: "+failure);incoming.set(null);}
    }

    private static void prepared(UnrealMemory memory,AtomicReference<byte[]> preparedGround){
        try {
            while(!Thread.currentThread().isInterrupted()){
                long start=System.nanoTime();
                UnrealEntities.flush(memory);
                UnrealArchitecture.flush(memory);
                UnrealVegetation.flush(memory);
                UnrealFurniture.prepare();
                UnrealGround.Block pendingBlock=ground.peek();
                if(pendingBlock!=null&&memory.canOffer("world")){
                    String block=pendingBlock.encode();
                    long publishStart=System.nanoTime();
                    if(memory.offer("world",block)){
                        ground.poll();
                        byte[] raw=block.getBytes(StandardCharsets.UTF_8);
                        ByteArrayOutputStream compressed=new ByteArrayOutputStream();
                        try(DeflaterOutputStream zip=new DeflaterOutputStream(compressed)){zip.write(raw);}
                        byte[] packet=ByteBuffer.allocate(8+compressed.size()).putInt(0x504A4731).putInt(raw.length).put(compressed.toByteArray()).array();
                        if(packet.length<=60000)preparedGround.set(packet);
                        else System.err.println("[heyzed] Unreal ground snapshot exceeds local datagram bound");
                    }
                    publishUs=(System.nanoTime()-publishStart)/1000;
                }
                preparedWorkMaxUs=Math.max(preparedWorkMaxUs,(System.nanoTime()-start)/1000);
                Thread.sleep(4);
            }
        } catch(InterruptedException stopped){Thread.currentThread().interrupt();}
        catch(Exception failure){preparedFailed=true;System.err.println("[heyzed] Unreal prepared-data delivery stopped: "+failure);}
    }

    @Patch(className="zombie.GameWindow",methodName="logic",warmUp=true)
    public static class Tick {
        @Patch.OnEnter public static void enter(){UnrealHeyZed.begin();}
        @Patch.OnExit public static void exit(){UnrealHeyZed.logicCompleted();}
    }
    @Patch(className="zombie.iso.IsoWorld",methodName="FinishAnimation",warmUp=true)
    public static class FinishedAnimation {
        @Patch.OnExit public static void exit(){UnrealHeyZed.end();}
    }
    @Patch(className="zombie.gameStates.IngameState",methodName="exit",warmUp=true)
    public static class Leave {@Patch.OnEnter public static void enter(){UnrealHeyZed.leave();}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isForwardKeyDown",warmUp=true)
    public static class Forward {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,0);}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isBackwardKeyDown",warmUp=true)
    public static class Backward {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,1);}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isLeftKeyDown",warmUp=true)
    public static class Left {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,2);}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isRightKeyDown",warmUp=true)
    public static class Right {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,3);}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isRunButtonDown",warmUp=true)
    public static class Run {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=(UnrealHeyZed.down(input,4)||UnrealHeyZed.down(input,5))&&input.isAllowRun();}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isSprintButtonDown",warmUp=true)
    public static class Sprint {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,5)&&input.isAllowSprint();}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="isCrouchButtonDown",warmUp=true)
    public static class Crouch {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return(readOnly=false) boolean value){if(UnrealHeyZed.owns(input))value=UnrealHeyZed.down(input,6);}}
    @Patch(className="zombie.characters.component.CharacterInputComponent",methodName="getInputMoveVector",warmUp=true)
    public static class Movement {@Patch.OnExit public static void exit(@Patch.This CharacterInputComponent input,@Patch.Return zombie.iso.Vector2 value){UnrealHeyZed.movement(input,value);}}
    @Patch(className="zombie.iso.objects.IsoDoor",methodName="ToggleDoor",warmUp=true)
    public static class DoorChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoDoor door){UnrealHeyZed.markDoor(door);}}
    @Patch(className="zombie.iso.objects.IsoDoor",methodName="ToggleDoorActual",warmUp=true)
    public static class DoorActualChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoDoor door){UnrealHeyZed.markDoor(door);}}
    @Patch(className="zombie.iso.objects.IsoDoor",methodName="setOpen",warmUp=true)
    public static class DoorSetChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoDoor door){UnrealHeyZed.markDoor(door);}}
    @Patch(className="zombie.iso.objects.IsoDoor",methodName="syncIsoObject",warmUp=true)
    public static class DoorReplicated {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoDoor door){UnrealHeyZed.markDoor(door);}}
    public static void markDoor(zombie.iso.objects.IsoDoor door){if(port!=0){UnrealObjects.dirty.add(door);dirtySquare(door.getSquare());}}
    public static void dirtySquare(zombie.iso.IsoGridSquare square){
        if(port!=0&&player!=null&&square!=null)UnrealFurniture.dirty(square);
        if(port!=0&&player!=null&&square!=null&&Math.abs(square.getX()-player.getX())<104&&Math.abs(square.getY()-player.getY())<104)
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)UnrealArchitecture.dirty(square.getX()+dx,square.getY()+dy,square.getZ());
        if(port!=0&&player!=null&&square!=null&&square.getZ()==0&&Math.abs(square.getX()-player.getX())<104&&Math.abs(square.getY()-player.getY())<104)UnrealGround.dirty(square.getX(),square.getY());
    }
    @Patch(className="zombie.iso.objects.IsoWindow",methodName="ToggleWindow",warmUp=true)
    public static class WindowChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoWindow window){UnrealHeyZed.dirtySquare(window.getSquare());}}
    @Patch(className="zombie.iso.objects.IsoWindow",methodName="setSmashed",warmUp=true)
    public static class WindowSmashed {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoWindow window){UnrealHeyZed.dirtySquare(window.getSquare());}}
    @Patch(className="zombie.iso.objects.IsoWindow",methodName="setGlassRemoved",warmUp=true)
    public static class WindowGlassRemoved {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoWindow window){UnrealHeyZed.dirtySquare(window.getSquare());}}
    @Patch(className="zombie.iso.objects.IsoCurtain",methodName="ToggleDoor",warmUp=true)
    public static class CurtainChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoCurtain curtain){UnrealHeyZed.dirtySquare(curtain.getSquare());}}
    @Patch(className="zombie.iso.objects.IsoCurtain",methodName="ToggleDoorSilent",warmUp=true)
    public static class CurtainChangedSilent {@Patch.OnExit public static void exit(@Patch.This zombie.iso.objects.IsoCurtain curtain){UnrealHeyZed.dirtySquare(curtain.getSquare());}}
    @Patch(className="zombie.iso.IsoGridSquare",methodName="RemoveTileObject",warmUp=true)
    public static class RemovedObject {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoGridSquare square){UnrealHeyZed.dirtySquare(square);}}
    @Patch(className="zombie.iso.IsoGridSquare",methodName="AddTileObject",warmUp=true)
    public static class AddedObject {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoGridSquare square){UnrealHeyZed.dirtySquare(square);}}
    @Patch(className="zombie.iso.IsoGridSquare",methodName="AddSpecialObject",warmUp=true)
    public static class AddedSpecialObject {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoGridSquare square){UnrealHeyZed.dirtySquare(square);}}
    @Patch(className="zombie.characters.IsoPlayer",methodName="getAimVector",warmUp=true)
    public static class PlayerAim {@Patch.OnExit public static void exit(@Patch.This IsoPlayer source,@Patch.Return zombie.iso.Vector2 value){UnrealHeyZed.aim(source,value);}}
    @Patch(className="zombie.characters.IsoPlayer",methodName="calculateAimVector",warmUp=true)
    public static class PlayerActionAim {@Patch.OnExit public static void exit(@Patch.This IsoPlayer source,@Patch.Return zombie.iso.Vector2 value){UnrealHeyZed.aim(source,value);}}
    public static void groundLoaded(zombie.iso.IsoChunk chunk){if(port!=0){UnrealGround.loaded(chunk.wx,chunk.wy);UnrealArchitecture.loaded(chunk.wx,chunk.wy);UnrealFurniture.loaded(chunk);}}
    @Patch(className="zombie.iso.IsoChunk",methodName="removeFromWorld",warmUp=true)
    public static class FurnitureChunkUnloaded {@Patch.OnEnter public static void enter(@Patch.This zombie.iso.IsoChunk chunk){if(port!=0)UnrealFurniture.unloaded(chunk);}}
    @Patch(className="zombie.iso.IsoChunk",methodName="loadInMainThread",warmUp=true)
    public static class GroundChunkLoaded {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoChunk chunk){UnrealHeyZed.groundLoaded(chunk);}}
    @Patch(className="zombie.iso.IsoObject",methodName="setSprite",warmUp=true)
    public static class GroundSpriteChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}}
    @Patch(className="zombie.iso.IsoObject",methodName="setOverlaySprite",warmUp=true)
    public static class GroundOverlayChanged {@Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}}
    private UnrealHeyZed(){}
}
