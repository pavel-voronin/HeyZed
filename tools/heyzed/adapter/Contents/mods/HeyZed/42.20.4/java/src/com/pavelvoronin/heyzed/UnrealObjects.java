package com.pavelvoronin.heyzed;

import java.util.*;
import java.util.concurrent.*;
import org.json.*;
import zombie.iso.*;
import zombie.iso.objects.IsoDoor;
import zombie.characters.*;
import zombie.vehicles.BaseVehicle;

/** Confirmed facts only. No presentation state or graphical preparation lives here. */
final class UnrealObjects {
    private static final IdentityHashMap<IsoObject,Entry> objects=new IdentityHashMap<>();
    private static final Map<Long,Entry> ids=new HashMap<>();
    private record SquareAddress(int x,int y,int z){}
    private static final Map<SquareAddress,Set<IsoObject>> squares=new HashMap<>();
    static final ConcurrentLinkedQueue<IsoDoor> dirty=new ConcurrentLinkedQueue<>();
    static final ConcurrentLinkedQueue<String> events=new ConcurrentLinkedQueue<>(),commands=new ConcurrentLinkedQueue<>();
    private static final LinkedHashMap<String,String> results=new LinkedHashMap<>();
    private static long nextId,eventSeq;
    private static int motionCursor;
    private static final class Entry {
        final long id=++nextId; final IsoObject object; long revision; String signature="";
        double x,y,yaw;long sampled;
        Entry(IsoObject o){object=o;}
    }
    private static Entry entry(IsoObject object){return objects.computeIfAbsent(object,o->{Entry e=new Entry(o);ids.put(e.id,e);return e;});}
    static void observe(IsoObject object,long epoch){
        if(!(object instanceof IsoDoor door)||door.getSquare()==null)return;
        Entry e=entry(door);
        String signature=door.IsOpen()+":"+door.getNorth();
        if(signature.equals(e.signature))return;
        e.signature=signature;
        events.add(new JSONObject().put("type","object").put("epoch",epoch).put("sequence",++eventSeq)
            .put("id",Long.toString(e.id)).put("generation",epoch).put("revision",++e.revision)
            .put("kind","door").put("x",door.getX()).put("y",door.getY()).put("z",door.getZ())
            .put("open",door.IsOpen()).put("north",door.getNorth()).put("deleted",false).toString());
    }
    static void square(IsoGridSquare square,long epoch){
        UnrealLighting.square(square,epoch);
        UnrealVegetation.square(square,epoch);
        UnrealArchitecture.observe(square.getX(),square.getY(),square.getZ());
        SquareAddress address=new SquareAddress(square.getX(),square.getY(),square.getZ());
        Set<IsoObject> found=Collections.newSetFromMap(new IdentityHashMap<>());
        var list=square.getObjects();
        for(int i=0;i<list.size();i++)if(list.get(i) instanceof IsoDoor door){found.add(door);observe(door,epoch);}
        Set<IsoObject> old=found.isEmpty()?squares.remove(address):squares.put(address,found);
        if(old!=null)for(IsoObject object:old)if(!found.contains(object)){
            Entry e=objects.remove(object);if(e==null)continue;ids.remove(e.id);
            events.add(new JSONObject().put("type","object").put("epoch",epoch).put("sequence",++eventSeq)
                .put("id",Long.toString(e.id)).put("generation",epoch).put("revision",++e.revision).put("deleted",true).toString());
        }
    }
    static void execute(IsoPlayer player,long epoch){
        IsoDoor changed;while((changed=dirty.poll())!=null)if(changed.getSquare()!=null&&(objects.containsKey(changed)||Math.hypot(changed.getX()-player.getX(),changed.getY()-player.getY())<64))observe(changed,epoch);
        for(int i=0;i<16;i++){
            String text=commands.poll();if(text==null)break;
            try{
                JSONObject c=new JSONObject(text);String token=c.getString("token");
                if(results.containsKey(token)){events.add(results.get(token));continue;}
                if(c.optString("action").startsWith("light_")||c.optString("action").equals("held_toggle")){
                    boolean accepted=UnrealLighting.command(c,player,epoch);
                    String result=new JSONObject().put("type","ack").put("token",token).put("accepted",accepted).toString();
                    results.put(token,result);events.add(result);
                    while(results.size()>4096)results.remove(results.keySet().iterator().next());
                    continue;
                }
                if(c.optString("action").equals("entity_crowd_ready")){
                    boolean accepted=UnrealCrowd.command(c,epoch);
                    String result=new JSONObject().put("type","ack").put("token",token).put("accepted",accepted).toString();
                    results.put(token,result);events.add(result);while(results.size()>4096)results.remove(results.keySet().iterator().next());continue;
                }
                if(c.optString("action").equals("entity_animation")){
                    boolean accepted=UnrealEntities.animationCommand(c,epoch);
                    String result=new JSONObject().put("type","ack").put("token",token).put("accepted",accepted).toString();
                    results.put(token,result);events.add(result);continue;
                }
                Entry e=ids.get(Long.parseLong(c.getString("id")));
                boolean accepted=false;
                if(e!=null&&c.getLong("generation")==epoch&&e.revision==c.getLong("revision")&&e.object instanceof IsoDoor door&&door.getSquare()!=null&&door.getObjectIndex()>=0&&Math.hypot(player.getX()-door.getX(),player.getY()-door.getY())<2.5&&Math.abs(player.getZ()-door.getZ())<.1){
                    boolean wanted=c.getBoolean("open");
                    if(door.IsOpen()!=wanted)door.ToggleDoor(player);
                    accepted=door.IsOpen()==wanted;observe(door,epoch);
                }
                String result=new JSONObject().put("type","ack").put("token",token).put("accepted",accepted).toString();
                results.put(token,result);events.add(result);
                while(results.size()>4096)results.remove(results.keySet().iterator().next());
            }catch(Exception error){System.err.println("[heyzed] Invalid bridge command: "+error.getClass().getSimpleName());}
        }
    }
    static String motion(IsoPlayer player,long epoch){
        JSONArray entities=new JSONArray();long now=System.nanoTime();
        java.util.ArrayList<Object> moving=new java.util.ArrayList<>(player.getCell().getVehicles());
        java.util.ArrayList<Object> others=new java.util.ArrayList<>(player.getCell().getObjectList());
        if(!others.isEmpty()){
            int start=motionCursor%others.size();
            for(int n=0;n<others.size();n++)if(!(others.get((start+n)%others.size()) instanceof BaseVehicle))moving.add(others.get((start+n)%others.size()));
            motionCursor=(start+48)%others.size();
        }
        for(Object candidate:moving){
            if(!(candidate instanceof IsoMovingObject object)||object==player)continue;
            if(Math.hypot(object.getX()-player.getX(),object.getY()-player.getY())>64)continue;
            Entry e=entry(object);
            double yaw=object instanceof IsoGameCharacter character?Math.atan2(character.getForwardDirectionY(),character.getForwardDirectionX()):0;
            if(object instanceof BaseVehicle vehicle){org.joml.Vector3f forward=vehicle.getForwardVector(new org.joml.Vector3f());yaw=Math.atan2(forward.z,forward.x);}
            double dt=(now-e.sampled)*1e-9;
            boolean continuous=e.sampled!=0&&dt>0&&dt<.5&&Math.hypot(object.getX()-e.x,object.getY()-e.y)<10;
            entities.put(new JSONObject().put("id",Long.toString(e.id)).put("generation",epoch).put("revision",++e.revision)
                .put("kind",object instanceof BaseVehicle?"vehicle":object instanceof IsoGameCharacter?"character":"moving")
                .put("x",object.getX()).put("y",object.getY()).put("z",object.getZ()).put("yaw",yaw)
                .put("vx",continuous?(object.getX()-e.x)/dt:0).put("vy",continuous?(object.getY()-e.y)/dt:0)
                .put("turn",continuous?Math.atan2(Math.sin(yaw-e.yaw),Math.cos(yaw-e.yaw))/dt:0));
            e.x=object.getX();e.y=object.getY();e.yaw=yaw;e.sampled=now;
            if(entities.length()>=64)break;
        }
        return new JSONObject().put("type","motion").put("epoch",epoch).put("sample_ns",now).put("entities",entities).toString();
    }
    static void reconcile(){for(Entry e:objects.values())e.signature="";}
    private UnrealObjects(){}
}
