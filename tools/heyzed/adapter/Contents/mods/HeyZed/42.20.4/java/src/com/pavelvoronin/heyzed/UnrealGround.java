package com.pavelvoronin.heyzed;

import java.util.*;
import org.json.*;
import zombie.characters.IsoPlayer;
import zombie.iso.IsoObject;
import zombie.iso.sprite.IsoSprite;

final class UnrealGround {
    private static long revision;
    private static final java.util.concurrent.ConcurrentLinkedQueue<Long> dirty=new java.util.concurrent.ConcurrentLinkedQueue<>();
    private static final Set<Long> rescanChunks=java.util.concurrent.ConcurrentHashMap.newKeySet();
    private static final Set<Long> queued=java.util.concurrent.ConcurrentHashMap.newKeySet();
    private static final Map<Long,List<Tile>> previous=new HashMap<>();
    private static final Map<Long,Long> versions=new HashMap<>(),generations=new HashMap<>();
    private static final Map<Long,Object> chunks=new HashMap<>();
    private static final Set<Long> completeChunks=new HashSet<>();
    private static final Map<Long,Long> retry=new HashMap<>();
    private static long address(int x,int y){return ((long)x<<32)|(y&0xffffffffL);}
    static void dirty(int x,int y){
        long key=address(x,y);
        if(queued.size()>=8192){rescanChunks.add(address(Math.floorDiv(x,8),Math.floorDiv(y,8)));return;}
        if(queued.add(key))dirty.add(key);
    }
    record Tile(String name,String material,boolean overlay,String edges) {
        JSONObject json(){return new JSONObject().put("name",name).put("material",material).put("overlay",overlay).put("edges",edges);}
    }
    static List<Tile> stateLayers(List<Tile> layers){
        // PZ derives directional floor attachments from neighboring floor materials.
        // Unreal derives its own border presentation from those same authoritative floors.
        boolean derived=false;for(Tile tile:layers)if(tile.overlay&&!tile.edges.isEmpty()){derived=true;break;}
        if(!derived)return layers;
        ArrayList<Tile> state=new ArrayList<>();
        for(Tile tile:layers)if(!tile.overlay||tile.edges.isEmpty())state.add(tile);
        return List.copyOf(state);
    }
    record Cell(int x,int y,List<Tile> layers,long version,long generation) {}
    record Block(long epoch,long revision,int x,int y,List<Cell> cells) {
        String encode(){
            Map<Tile,Integer> indices=new LinkedHashMap<>();JSONArray dictionary=new JSONArray(),data=new JSONArray();
            for(Cell cell:cells){JSONArray layers=new JSONArray();for(Tile tile:cell.layers){
                Integer index=indices.get(tile);if(index==null){index=indices.size();indices.put(tile,index);dictionary.put(tile.json());}layers.put(index);
            }data.put(new JSONArray().put(cell.x).put(cell.y).put(layers).put(cell.version).put(cell.generation));}
            return new JSONObject().put("schema",2).put("epoch",epoch).put("revision",revision).put("block_x",x).put("block_y",y).put("dictionary",dictionary).put("cells",data).toString();
        }
    }
    private static float lastX,lastY;
    private static double aheadX,aheadY;
    private static int centerX=Integer.MIN_VALUE,centerY;
    private static List<Long> order=List.of();
    static List<Block> snapshot(IsoPlayer player,long epoch) {
        var cell=player.getCell();if(cell==null)return List.of();
        long now=System.nanoTime();
        int cx=Math.floorDiv((int)Math.floor(player.getX()),8),cy=Math.floorDiv((int)Math.floor(player.getY()),8);
        double dx=player.getX()-lastX,dy=player.getY()-lastY;lastX=player.getX();lastY=player.getY();
        double length=Math.hypot(dx,dy);
        if(length>.001&&length<4){aheadX=dx/length*2;aheadY=dy/length*2;}
        if(cx!=centerX||cy!=centerY||order.isEmpty()){
            centerX=cx;centerY=cy;ArrayList<Long> next=new ArrayList<>();
            for(int y=cy-12;y<=cy+12;y++)for(int x=cx-12;x<=cx+12;x++)next.add(address(x,y));
            final double ax=aheadX,ay=aheadY;
            next.sort(Comparator.comparingDouble(key->{double x=(int)(key>>32)-cx,y=(int)(long)key-cy;return x*x+y*y-.75*(x*ax+y*ay);}));
            order=next;
        }
        ArrayList<Block> blocks=new ArrayList<>();
        Map<Long,List<Cell>> changes=new LinkedHashMap<>();
        for(int n=0;n<64;n++){
            Long key=dirty.poll();if(key==null)break;queued.remove(key);
            int x=(int)(key>>32),y=(int)(long)key;
            if(Math.abs(Math.floorDiv(x,8)-cx)>12||Math.abs(Math.floorDiv(y,8)-cy)>12)continue;
            if(!UnrealBake.ready(x,y)){dirty(x,y);continue;}
            Cell captured=capture(player,x,y,epoch,false);
            if(captured!=null)changes.computeIfAbsent(address(Math.floorDiv(x,8),Math.floorDiv(y,8)),k->new ArrayList<>()).add(captured);
        }
        for(var entry:changes.entrySet())blocks.add(new Block(epoch,++revision,(int)(entry.getKey()>>32),(int)(long)entry.getKey(),List.copyOf(entry.getValue())));
        for(long key:rescanChunks){rescanChunks.remove(key);completeChunks.remove(key);retry.remove(key);}
        int capturedChunks=0;
        for(long key:order){
            if(capturedChunks>=8)break;
            if(now<retry.getOrDefault(key,0L))continue;
            int x=(int)(key>>32),y=(int)key;
            if(!UnrealBake.ready(x*8,y*8)){retry.put(key,now+100_000_000L);continue;}
            Object chunk=cell.getChunkForGridSquare(x*8,y*8,0);
            if(chunk==null){chunks.remove(key);completeChunks.remove(key);retry.put(key,now+100_000_000L);continue;}
            boolean fresh=chunks.get(key)!=chunk;
            if(!fresh&&completeChunks.contains(key))continue;
            List<Cell> data=new ArrayList<>();boolean complete=true;
            for(int oy=0;oy<8;oy++)for(int ox=0;ox<8;ox++){
                if(cell.getGridSquare(x*8+ox,y*8+oy,0)==null){complete=false;continue;}
                Cell captured=capture(player,x*8+ox,y*8+oy,epoch,fresh);
                if(captured!=null)data.add(captured);
            }
            ++capturedChunks;
            chunks.put(key,chunk);
            if(complete){completeChunks.add(key);retry.remove(key);}
            else {completeChunks.remove(key);retry.put(key,now+100_000_000L);}
            if(!data.isEmpty())blocks.add(new Block(epoch,++revision,x,y,List.copyOf(data)));
        }
        // Bound retained native chunk references; per-cell versions remain monotonic across re-entry.
        chunks.keySet().removeIf(key->Math.abs((int)(key>>32)-cx)>14||Math.abs((int)(long)key-cy)>14);
        completeChunks.removeIf(key->!chunks.containsKey(key));
        retry.keySet().removeIf(key->Math.abs((int)(key>>32)-cx)>14||Math.abs((int)(long)key-cy)>14);
        return blocks;
    }
    private static Cell capture(IsoPlayer player,int x,int y,long epoch,boolean force){
        var square=player.getCell().getGridSquare(x,y,0);if(square==null)return null;
        UnrealObjects.square(square,epoch);
        ArrayList<Tile> layers=new ArrayList<>();
        IsoObject floor=square.getFloor();
        if(floor!=null){add(floor.getSprite(),false,layers);attachments(floor,layers);}
        var objects=square.getObjects();
        for(int i=0;i<objects.size();i++){
            IsoObject object=objects.get(i);if(object==floor||object.getSprite()==null)continue;
            var properties=object.getSprite().getProperties();
            if(properties.has("FloorOverlay")||properties.has("attachedFloor")){add(object.getSprite(),true,layers);attachments(object,layers);}
        }
        var state=stateLayers(layers);
        long key=address(x,y);List<Tile> old=previous.get(key);
        var baseline=UnrealBake.cell(x,y);
        if(old==null&&baseline!=null&&state.equals(baseline))return null;
        if(!force&&state.equals(old))return null;
        previous.put(key,List.copyOf(state));long version=versions.merge(key,1L,Long::sum);
        long generation=generations.computeIfAbsent(key,k->1L);
        if(old!=null&&old.isEmpty()&&!layers.isEmpty()){generation++;generations.put(key,generation);}
        return new Cell(x,y,List.copyOf(layers),version,generation);
    }
    private static void attachments(IsoObject object,List<Tile> layers){
        Set<IsoSprite> seen=Collections.newSetFromMap(new IdentityHashMap<>());
        seen.add(object.getSprite());
        var attached=object.getAttachedAnimSprite();
        if(attached!=null)for(var instance:attached){
            IsoSprite sprite=instance.parentSprite;
            if(sprite!=null&&seen.add(sprite))add(sprite,true,layers);
        }
        IsoSprite overlay=object.getOverlaySprite();
        if(overlay!=null&&seen.add(overlay))add(overlay,true,layers);
    }

    private static void add(IsoSprite sprite,boolean overlay,List<Tile> layers) {
        if(sprite==null||sprite.getName()==null)return;
        var properties=sprite.getProperties();
        String name=sprite.getName(),material=properties.get("FloorMaterial");
        if(material==null)material=properties.get("FootstepMaterial");
        if(material==null)material="";
        StringBuilder edges=new StringBuilder();
        for(String edge:new String[]{"N","E","S","W"})if(properties.has("FloorAttachment"+edge))edges.append(edge);
        layers.add(new Tile(name,material,overlay,edges.toString()));
    }
    static void loaded(int x,int y){long key=address(x,y);chunks.remove(key);completeChunks.remove(key);retry.remove(key);}
    static void reconcile(IsoPlayer player){chunks.clear();completeChunks.clear();retry.clear();}
    static void reset(){chunks.clear();completeChunks.clear();retry.clear();previous.clear();order=List.of();queued.clear();dirty.clear();rescanChunks.clear();}
    private UnrealGround(){}
}
