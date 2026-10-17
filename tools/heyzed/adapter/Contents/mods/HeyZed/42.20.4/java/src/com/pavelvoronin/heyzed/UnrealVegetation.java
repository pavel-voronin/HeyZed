package com.pavelvoronin.heyzed;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import org.json.*;
import zombie.iso.IsoGridSquare;
import zombie.iso.IsoObject;
import zombie.iso.objects.IsoTree;

/** Native facts only; rendering, LOD and placeholder species stay in Unreal. */
final class UnrealVegetation {
    private record Cell(int x,int y,long revision,String plants) {}
    private static final Map<Long,String> previous=new HashMap<>();
    private static final ConcurrentHashMap<Long,Cell> pending=new ConcurrentHashMap<>();
    private static long revision;
    private static volatile long epoch;

    static void square(IsoGridSquare square,long generation){
        if(square.getZ()!=0)return;
        epoch=generation;
        JSONArray plants=new JSONArray();
        var objects=square.getObjects();
        for(int i=0;i<objects.size();i++){
            IsoObject object=objects.get(i);var sprite=object.getSprite();if(sprite==null)continue;
            String name=sprite.getName();if(name==null)name="native-unknown";
            var properties=sprite.getProperties();
            boolean tree=object instanceof IsoTree;
            boolean shrub=properties.has("Bush")||name.startsWith("e_newbushes")||name.startsWith("f_bushes_")
                ||properties.has("Movement")&&properties.get("Movement").startsWith("Hedge");
            if(!tree&&!shrub)continue;
            int stage=tree?Math.clamp(((IsoTree)object).getSize(),1,8):1;
            String family=name.toLowerCase(java.util.Locale.ROOT);
            int kind=tree?(family.contains("pine")||family.contains("hemlock")||family.contains("cedar")?2:0):1;
            plants.put(new JSONArray().put(kind).put(stage).put(name).put(square.getZ()));
        }
        int x=square.getX(),y=square.getY();long key=((long)x<<32)|(y&0xffffffffL);
        String state=plants.toString();
        if(state.equals(previous.get(key)))return;
        if(pending.size()>=8192&&!pending.containsKey(key))return;
        previous.put(key,state);pending.put(key,new Cell(x,y,++revision,state));
    }

    static void reconcile(){previous.clear();}

    static void flush(UnrealMemory memory){
        if(pending.isEmpty())return;
        List<Map.Entry<Long,Cell>> batch=new ArrayList<>();JSONArray cells=new JSONArray();
        for(var entry:pending.entrySet()){
            var cell=entry.getValue();batch.add(Map.entry(entry.getKey(),cell));
            cells.put(new JSONArray().put(cell.x).put(cell.y).put(cell.revision).put(new JSONArray(cell.plants)));
            if(batch.size()>=64)break;
        }
        String block="V1 "+new JSONObject().put("epoch",epoch).put("cells",cells);
        if(memory.offer("world",block))for(var entry:batch)pending.remove(entry.getKey(),entry.getValue());
    }
    private UnrealVegetation(){}
}
