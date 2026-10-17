package com.pavelvoronin.heyzed;

import java.nio.file.*;
import java.lang.reflect.Field;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;
import zombie.characters.IsoPlayer;
import zombie.iso.*;
import zombie.iso.objects.*;
import zombie.iso.sprite.IsoSprite;
import zombie.iso.SpriteDetails.IsoFlagType;
import zombie.iso.SpriteDetails.IsoObjectType;

final class UnrealArchitecture {
    private static final ConcurrentLinkedQueue<UnrealConstructionSource.Address> dirty=new ConcurrentLinkedQueue<>();
    private static final Set<String> queued=ConcurrentHashMap.newKeySet();
    private static final ConcurrentLinkedQueue<UnrealConstructionSource.Address> discovery=new ConcurrentLinkedQueue<>();
    private static final Set<String> discovered=ConcurrentHashMap.newKeySet();
    private record Block(long epoch,long revision,JSONObject cell) {}
    private static final ConcurrentLinkedQueue<Block> pending=new ConcurrentLinkedQueue<>();
    private static final Map<String,String> observed=new HashMap<>();
    private static final Set<String> reconcileAddresses=new HashSet<>();
    private static final Field doorClosed=closedField(IsoDoor.class),windowClosed=closedField(IsoWindow.class);
    private static final Set<Class<?>> unavailableSprites=new HashSet<>();
    private static Field closedField(Class<?> owner){
        try{Field field=owner.getDeclaredField("closedSprite");if(field.trySetAccessible())return field;}
        catch(ReflectiveOperationException error){System.err.println("[heyzed] Native construction identity unavailable: "+owner.getName()+".closedSprite: "+error);}
        return null;
    }
    private static IsoSprite constructionSprite(IsoObject object){
        Field field=object instanceof IsoDoor?doorClosed:object instanceof IsoWindow?windowClosed:null;
        if(!(object instanceof IsoDoor)&&!(object instanceof IsoWindow))return object.getSprite();
        try{if(field!=null&&field.get(object) instanceof IsoSprite sprite)return sprite;}
        catch(IllegalAccessException error){System.err.println("[heyzed] Native construction identity read failed: "+error);}
        if(unavailableSprites.add(object.getClass()))System.err.println("[heyzed] Construction updates retained while native closed sprite is unavailable: "+object.getClass().getName());
        return null;
    }
    private record RescanLevel(int x,int y,int z) {}
    private static final Set<RescanLevel> rescans=ConcurrentHashMap.newKeySet();
    private static RescanLevel activeRescan;
    private static int rescanOffset;
    private static final UnrealConstructionVisits visits=new UnrealConstructionVisits();
    private static List<UnrealConstructionSource.Address> candidateView=List.of();
    private static boolean discoverAgain;
    private static int visitX=Integer.MIN_VALUE,visitY;
    private static int cursor;
    private static long revision;
    private static String key(int x,int y,int z){return x+","+y+","+z;}
    static void reset(){pending.clear();observed.clear();reconcileAddresses.clear();dirty.clear();queued.clear();discovery.clear();discovered.clear();rescans.clear();activeRescan=null;visits.clear();candidateView=List.of();discoverAgain=false;visitX=Integer.MIN_VALUE;cursor=0;}
    static void dirty(int x,int y,int z){
        String key=key(x,y,z);
        if(queued.size()>=8192){rescans.add(new RescanLevel(Math.floorDiv(x,8),Math.floorDiv(y,8),z));return;}
        if(queued.add(key))dirty.add(new UnrealConstructionSource.Address(x,y,z));
    }
    static void loaded(int x,int y){visits.loaded(x,y);discoverAgain=true;}
    private static UnrealConstructionSource.Address rescan(){
        if(activeRescan==null){var iterator=rescans.iterator();if(!iterator.hasNext())return null;activeRescan=iterator.next();rescans.remove(activeRescan);rescanOffset=0;}
        RescanLevel region=activeRescan;int offset=rescanOffset++;
        if(rescanOffset==64)activeRescan=null;
        return new UnrealConstructionSource.Address(region.x*8+offset%8,region.y*8+offset/8,region.z);
    }
    static void observe(int x,int y,int z){
        UnrealConstructionSource source=UnrealConstructionSource.current.getNow(null);if(source==null||source.cell(x,y,z)==null)return;
        String key=key(x,y,z);if(discovered.size()<8192&&discovered.add(key))discovery.add(new UnrealConstructionSource.Address(x,y,z));
    }
    static void reconcile(){visits.clear();discoverAgain=true;reconcileAddresses.addAll(observed.keySet());for(String key:observed.keySet()){String[] p=key.split(",");dirty(Integer.parseInt(p[0]),Integer.parseInt(p[1]),Integer.parseInt(p[2]));}cursor=0;}
    static void flush(UnrealMemory memory){for(int i=0;i<4;i++){Block block=pending.peek();if(block==null)break;String encoded="A1 "+new JSONObject().put("type","architecture").put("epoch",block.epoch).put("revision",block.revision).put("cell",block.cell).toString();if(!memory.offer("world",encoded))break;pending.poll();}}
    private static String signature(JSONObject cell){
        StringBuilder out=new StringBuilder(cell.optString("floor",""));
        JSONObject stairs=cell.optJSONObject("stairs");if(stairs!=null)out.append('|').append(stairs.getString("axis")).append(stairs.getInt("part"));
        Map<String,JSONObject> ordered=new TreeMap<>();for(Object value:cell.getJSONArray("walls")){JSONObject wall=(JSONObject)value;ordered.put(wall.getString("axis"),wall);}
        for(JSONObject wall:ordered.values()){
            ArrayList<String> names=new ArrayList<>();for(Object n:wall.getJSONArray("sources"))names.add(n.toString());Collections.sort(names);String state=wall.optString("state");
            out.append('|').append(wall.getString("axis")).append(wall.optBoolean("wall")).append(wall.getString("opening")).append(wall.getBoolean("door")).append(wall.getBoolean("window")).append(wall.optBoolean("glassRemoved",state.equals("glassRemoved"))).append(!wall.optBoolean("door")&&wall.optBoolean("open",state.equals("open"))).append(names);
            out.append(wall.optBoolean("broken",state.equals("broken"))).append(wall.has("attachments")?wall.getJSONArray("attachments").toString():"[]");
        }
        ArrayList<String> grime=new ArrayList<>();JSONArray overlays=cell.optJSONArray("grime");if(overlays!=null)for(Object name:overlays)grime.add(name.toString());Collections.sort(grime);out.append(grime);
        return out.toString();
    }
    static void tick(IsoPlayer player,long epoch){
        if(!UnrealConstructionSource.current.isDone()||pending.size()>=64)return;UnrealConstructionSource base=UnrealConstructionSource.current.getNow(null);if(base==null)return;
        int px=(int)Math.floor(player.getX()),py=(int)Math.floor(player.getY());base.request(px,py);
        List<UnrealConstructionSource.Address> candidates=base.candidates(px,py);int sampled=0;
        if(candidateView!=candidates||discoverAgain){candidateView=candidates;cursor=0;discoverAgain=false;}
        int cx=Math.floorDiv(px,8),cy=Math.floorDiv(py,8);
        if(cx!=visitX||cy!=visitY){visitX=cx;visitY=cy;visits.retain(cx,cy,14);}
        for(int n=0;n<128&&sampled<16;n++){
            UnrealConstructionSource.Address point=dirty.poll();boolean changed=point!=null;
            if(point!=null)queued.remove(point.key());
            else {
                point=rescan();changed=point!=null;
                if(point==null){point=discovery.poll();if(point!=null)discovered.remove(point.key());}
                if(point==null){if(cursor>=candidates.size())break;point=candidates.get(cursor++);}
            }
            int x=point.x(),y=point.y(),z=point.z();if(Math.abs(x-px)>104||Math.abs(y-py)>104)continue;
            if(!base.ready(x,y)){dirty(x,y,z);continue;}
            IsoGridSquare square=player.getCell().getGridSquare(x,y,z);if(square==null)continue;
            IsoChunk chunk=square.getChunk();if(chunk==null)continue;
            if(!changed&&visits.contains(x,y,z,chunk,chunk.getLoadID()))continue;
            ++sampled;JSONObject original=base.cell(x,y,z);
            JSONObject cell=capture(square,original,base.floorOwner(x,y,z),epoch,base);
            if(cell==null){dirty(x,y,z);continue;}
            visits.add(x,y,z,chunk,chunk.getLoadID());
            String value=signature(cell),old=observed.get(key(x,y,z));
            String initial=original==null?signature(new JSONObject().put("floor","").put("walls",new JSONArray())):signature(original);
            boolean force=reconcileAddresses.remove(key(x,y,z));
            if(!force&&old==null&&initial.equals(value))continue;
            if(!force&&value.equals(old))continue;
            observed.put(key(x,y,z),value);
            pending.add(new Block(epoch,++revision,cell));
        }
    }
    private static JSONObject capture(IsoGridSquare square,JSONObject original,boolean floorOwner,long epoch,UnrealConstructionSource base){
        int x=square.getX(),y=square.getY(),z=square.getZ();
        JSONObject result=new JSONObject().put("address",new JSONArray().put(x).put(y).put(z));
        IsoObject floor=square.getFloor();
        result.put("floor",floorOwner&&floor!=null&&floor.getSprite()!=null?floor.getSprite().getName():"");
        LinkedHashMap<String,JSONObject> walls=new LinkedHashMap<>();JSONObject stairs=null;Set<String> grime=new LinkedHashSet<>();
        var objects=square.getObjects();
        for(int objectIndex=0;objectIndex<objects.size();objectIndex++){
            IsoObject object=objects.get(objectIndex);UnrealObjects.observe(object,epoch);
            var visible=object.getSprite();if(visible==null||visible.getName()==null)continue;
            grime(visible,grime);grime(object.getOverlaySprite(),grime);var attached=object.getAttachedAnimSprite();if(attached!=null)for(var instance:attached)grime(instance.parentSprite,grime);
            IsoSprite canonical=constructionSprite(object);if(canonical==null)return null;
            ArrayList<IsoSprite> sources=new ArrayList<>();sources.add(canonical);
            if(attached!=null)for(var instance:attached){var binding=instance.parentSprite==null?null:base.binding(instance.parentSprite.getName());if(binding!=null&&"boundary".equals(binding.optString("archetype")))sources.add(instance.parentSprite);}
            for(IsoSprite sprite:sources){
            var p=sprite.getProperties();String name=sprite.getName();if(name==null)continue;boolean primary=sprite==canonical;
            if(name.startsWith("roofs_")||name.startsWith("rooftop_")||name.contains("_roofs_"))continue;
            JSONObject sourceBinding=base.binding(name);
            for(boolean north:new boolean[]{true,false}){
                String axis=north?"N":"W";
                boolean door=primary&&object instanceof IsoDoor d&&d.getNorth()==north;
                boolean window=primary&&object instanceof IsoWindow w&&w.getNorth()==north;
                boolean boundBoundary=sourceBinding!=null&&"boundary".equals(sourceBinding.optString("archetype"))&&axis.equals(sourceBinding.optString("orientation"));
                boolean structure=boundBoundary||p.has(north?IsoFlagType.WallN:IsoFlagType.WallW)||p.has(IsoFlagType.WallNW)||p.has(north?IsoFlagType.DoorWallN:IsoFlagType.DoorWallW)||p.has(north?IsoFlagType.WindowN:IsoFlagType.WindowW);
                JSONObject boundOpening=boundBoundary?sourceBinding.optJSONObject("opening"):null;
                String opening=door||p.has(north?IsoFlagType.DoorWallN:IsoFlagType.DoorWallW)?"door":window||p.has(north?IsoFlagType.WindowN:IsoFlagType.WindowW)?"window":boundOpening==null?"":boundOpening.optString("archetype");
                if(structure||!opening.isEmpty()){
                    JSONObject wall=walls.computeIfAbsent(axis,k->new JSONObject().put("axis",axis).put("sources",new JSONArray()).put("opening","").put("wall",false).put("door",false).put("window",false));
                    String constructionName=p.has(IsoFlagType.WallNW)?p.get(north?"CornerNorthWall":"CornerWestWall"):name;if(constructionName==null||constructionName.isEmpty())constructionName=name;
                    wall.getJSONArray("sources").put(constructionName);if(structure)wall.put("wall",true);
                    if(!opening.isEmpty())wall.put("opening",opening);
                    if(door)wall.put("door",true).put("doorGlass","Glass".equals(p.get("Material2"))||"Glass".equals(p.get("MaterialType")));if(window){IsoWindow w=(IsoWindow)object;wall.put("window",true).put("glassRemoved",w.isGlassRemoved()).put("broken",w.isSmashed()).put("open",w.IsOpen());}
                    JSONObject binding=base.binding(constructionName);
                    if(binding!=null){
                        if(door||window){wall.put("insert",binding.optString("type"));JSONObject evidence=binding.optJSONObject("nativeEvidence");if(evidence!=null)wall.put("mirrorU",evidence.optBoolean("mirrorU")).put("state",evidence.optString("spriteState","closed"));}
                        if(binding.has("opening"))wall.put("openingConfig",binding.get("opening"));
                        if("boundary".equals(binding.optString("archetype")))wall.put("boundary",binding.optString("type","boundary.wall"));
                    }
                    if(door){IsoDoor d=(IsoDoor)object;wall.put("open",d.IsOpen()).put("state",d.IsOpen()?"open":"closed");}
                }
                IsoObjectType[] flags=north?new IsoObjectType[]{IsoObjectType.stairsBN,IsoObjectType.stairsMN,IsoObjectType.stairsTN}:new IsoObjectType[]{IsoObjectType.stairsBW,IsoObjectType.stairsMW,IsoObjectType.stairsTW};
                for(int i=0;i<3;i++)if(primary&&object.getType()==flags[i])stairs=new JSONObject().put("axis",axis).put("part",i).put("source",name);
            }
            }
        }
        JSONArray array=new JSONArray();
        for(String axis:new String[]{"N","W"}){
            JSONObject wall=walls.get(axis);if(wall==null)continue;
            if(original!=null)for(Object value:original.getJSONArray("walls")){
                JSONObject known=(JSONObject)value;if(axis.equals(known.getString("axis"))){
                    for(String field:new String[]{"surfaceA","surfaceB","boundary","assemblyContinuation","assemblyAnchor","assemblyCells","contextOnly","fixedState"})if(known.has(field))wall.put(field,known.get(field));
                    if(!wall.has("openingConfig")&&known.has("openingConfig"))wall.put("openingConfig",known.get("openingConfig"));
                    if(known.optInt("assemblyCells",1)>1){if(known.has("insert"))wall.put("insert",known.get("insert"));if(known.has("openingConfig"))wall.put("openingConfig",known.get("openingConfig"));}
                    if(known.has("attachments"))wall.put("attachments",attachments(square,axis,known,base));
                }
            }
            boolean storefront=false;for(Object name:wall.getJSONArray("sources"))if(name.toString().startsWith("walls_commercial_01_")||name.toString().startsWith("walls_commercial_02_"))storefront=true;
            wall.put("openingType",wall.getString("opening").isEmpty()?"":storefront&&wall.getString("opening").equals("window")?"window.storefront":wall.getString("opening")+".standard");array.put(wall);
            if(!wall.has("attachments"))wall.put("attachments",attachments(square,axis,new JSONObject().put("attachments",new JSONArray()),base));
        }
        result.put("walls",array).put("grime",new JSONArray(grime));if(stairs!=null)result.put("stairs",stairs);
        return result;
    }
    private static void grime(zombie.iso.sprite.IsoSprite sprite,Set<String> result){
        if(sprite==null||sprite.getName()==null)return;String name=sprite.getName();
        if(name.startsWith("overlay_grime_")||name.startsWith("overlay_wall_")||name.startsWith("floors_burnt_")||name.startsWith("walls_burnt_"))result.add(name);
    }
    private static JSONArray attachments(IsoGridSquare host,String axis,JSONObject known,UnrealConstructionSource base){
        JSONArray result=new JSONArray();Map<String,JSONObject> found=new LinkedHashMap<>();Set<String> names=new HashSet<>();
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
            IsoGridSquare square=host.getCell().getGridSquare(host.getX()+dx,host.getY()+dy,host.getZ());if(square==null)continue;
            var objects=square.getObjects();
            for(int i=0;i<objects.size();i++){
                IsoObject object=objects.get(i);if(object.getSprite()==null)continue;String name=object.getSprite().getName();names.add(name);JSONObject binding=base.attachment(name);if(binding==null)continue;
                String direction=binding.getString("direction");
                if(object instanceof IsoCurtain curtain){
                    if(curtain.getType()==IsoObjectType.curtainN)direction="N";else if(curtain.getType()==IsoObjectType.curtainS)direction="S";
                    else if(curtain.getType()==IsoObjectType.curtainW)direction="W";else if(curtain.getType()==IsoObjectType.curtainE)direction="E";
                }
                int x=square.getX()+(direction.equals("E")?1:0),y=square.getY()+(direction.equals("S")?1:0);
                String edge=direction.equals("W")||direction.equals("E")?"W":"N";if(x!=host.getX()||y!=host.getY()||!edge.equals(axis))continue;
                String side=direction.equals("N")||direction.equals("W")?"outside":"inside",family=binding.getString("family");
                boolean opened=object instanceof IsoCurtain curtain?curtain.IsOpen():binding.optBoolean("open");
                found.put(family+":"+side,new JSONObject().put("family",family).put("side",side).put("state",opened?"open":"closed").put("definition","attachment."+family).put("sources",new JSONArray().put(name)));
                if(object instanceof IsoCurtain||binding.optString("category").equals("covering")){
                    JSONObject covering=found.remove(family+":"+side);String edgeSide=side.equals("outside")?"A":"B";
                    covering.remove("side");covering.put("edgeSide",edgeSide).put("prototypeNormal",-1).put("mountCell",new JSONArray().put(square.getX()).put(square.getY()).put(square.getZ()));
                    found.put(family+":"+edgeSide,covering);
                }
            }
        }
        for(Object value:known.getJSONArray("attachments")){
            JSONObject old=(JSONObject)value,current=found.remove(old.optString("family")+":"+old.optString("edgeSide",old.optString("side")));
            if(current==null&&old.has("side"))current=found.remove(old.optString("family")+":"+(old.optString("side").equals("outside")?"A":"B"));
            if(current!=null){JSONObject copy=new JSONObject(old.toString());copy.put("state",current.getString("state"));if(current.has("mountCell")){
                if(!copy.has("prototypeNormal"))copy.put("prototypeNormal",old.has("side")?(old.optString("side").equals("outside")?1:-1):-1);
                copy.remove("side");copy.put("edgeSide",current.getString("edgeSide")).put("mountCell",current.getJSONArray("mountCell"));
            }result.put(copy);}
            else if(old.optString("family").equals("sheet-rope")){for(Object name:old.getJSONArray("sources"))if(names.contains(name.toString())){result.put(new JSONObject(old.toString()));break;}}
        }
        for(JSONObject attachment:found.values())result.put(attachment);return result;
    }
}
