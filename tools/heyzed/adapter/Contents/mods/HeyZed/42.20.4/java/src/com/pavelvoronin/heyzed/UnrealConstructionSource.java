package com.pavelvoronin.heyzed;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.GZIPInputStream;
import org.json.*;

/** Prepared immutable facts only; this worker never reads a live game object. */
final class UnrealConstructionSource {
    record Address(int x,int y,int z) {String key(){return x+","+y+","+z;}}
    record Area(Map<String,JSONObject> cells,Set<String> footprint,List<Address> order) {}
    private static final ExecutorService worker=Executors.newSingleThreadExecutor(r->{Thread t=new Thread(r,"heyzed construction source");t.setDaemon(true);t.setPriority(Thread.NORM_PRIORITY-1);return t;});
    static final CompletableFuture<UnrealConstructionSource> current=CompletableFuture.supplyAsync(()->{
        try {
            Path pointer=Path.of(System.getProperty("user.home"),".heyzed","architecture","current.json");
            if(!Files.isRegularFile(pointer))return null;
            JSONObject descriptor=new JSONObject(Files.readString(pointer));
            return new UnrealConstructionSource(Path.of(descriptor.getString("path")));
        }catch(Exception error){System.err.println("[heyzed] Construction source unavailable: "+error);return null;}
    },worker);
    private final JSONObject manifest,bindings,attachmentBindings;
    private final Map<String,Path> buildings=new HashMap<>();
    private final ConcurrentMap<String,Area> areas=new ConcurrentHashMap<>();
    private final Set<String> loading=ConcurrentHashMap.newKeySet();
    private final Area legacy;
    private volatile Set<String> wanted=Set.of();
    private int lastX=Integer.MIN_VALUE,lastY=Integer.MIN_VALUE;
    private volatile List<Address> nearby=List.of();
    private final AtomicLong generation=new AtomicLong();
    private record CandidateQuery(long sequence,int x,int y,List<List<Address>> areas) {}
    private volatile CandidateQuery candidateQuery;
    private final AtomicBoolean candidateScheduled=new AtomicBoolean();
    private volatile long candidatePublished;
    private long candidateSequence,nearbyGeneration=-1;
    private int candidateChunkX=Integer.MIN_VALUE,candidateChunkY=Integer.MIN_VALUE;

    private UnrealConstructionSource(Path path)throws IOException {
        manifest=new JSONObject(Files.readString(path));
        if(manifest.optInt("schema")==2&&manifest.has("regions")){
            legacy=null;
            for(Object item:manifest.getJSONArray("buildings")){JSONObject b=(JSONObject)item;buildings.put(b.getString("id"),Path.of(b.getString("file")));}
            JSONObject definitions=new JSONObject(Files.readString(path.getParent().getParent().resolve("definitions.json")));
            bindings=definitions.getJSONObject("sources");attachmentBindings=definitions.optJSONObject("attachmentBindings");
        }else {
            legacy=area(List.of(manifest),null);bindings=new JSONObject();attachmentBindings=null;
        }
    }
    private static Area area(List<JSONObject> snapshots,String region){
        Map<String,JSONObject> cells=new HashMap<>();Set<String> footprint=new HashSet<>();
        int rx=0,ry=0;if(region!=null){String[] p=region.split(",");rx=Integer.parseInt(p[0]);ry=Integer.parseInt(p[1]);}
        for(JSONObject snapshot:snapshots){
            for(Object item:snapshot.getJSONArray("footprint")){JSONArray p=(JSONArray)item;int x=p.getInt(0),y=p.getInt(1);if(region==null||(Math.floorDiv(x,256)==rx&&Math.floorDiv(y,256)==ry))footprint.add(x+","+y+","+p.getInt(2));}
            for(Object item:snapshot.getJSONArray("cells")){
                JSONObject incoming=(JSONObject)item;JSONArray p=incoming.getJSONArray("address");int x=p.getInt(0),y=p.getInt(1),z=p.getInt(2);
                if(region!=null&&(Math.floorDiv(x,256)!=rx||Math.floorDiv(y,256)!=ry))continue;
                String key=x+","+y+","+z;JSONObject existing=cells.get(key);
                if(existing==null){cells.put(key,new JSONObject(incoming,JSONObject.getNames(incoming)));continue;}
                if(!incoming.isNull("floor")&&!incoming.optString("floor").isEmpty())existing.put("floor",incoming.get("floor"));
                Map<String,JSONObject> walls=new LinkedHashMap<>();
                for(Object wall:existing.getJSONArray("walls")){JSONObject w=(JSONObject)wall;walls.put(w.getString("axis"),w);}
                for(Object wall:incoming.getJSONArray("walls")){JSONObject w=(JSONObject)wall;JSONObject old=walls.get(w.getString("axis"));if(old==null||old.optBoolean("contextOnly"))walls.put(w.getString("axis"),w);}
                existing.put("walls",new JSONArray(walls.values()));
            }
        }
        List<Address> order=new ArrayList<>();for(JSONObject cell:cells.values()){JSONArray p=cell.getJSONArray("address");order.add(new Address(p.getInt(0),p.getInt(1),p.getInt(2)));}
        order.sort(Comparator.comparingInt(Address::x).thenComparingInt(Address::y).thenComparingInt(Address::z));
        return new Area(Map.copyOf(cells),Set.copyOf(footprint),List.copyOf(order));
    }
    void request(int x,int y){
        if(legacy!=null)return;
        // Keep the view stable within a native chunk, not at every sub-tile step.
        int cx=Math.floorDiv(x,8),cy=Math.floorDiv(y,8);if(cx==lastX&&cy==lastY)return;lastX=cx;lastY=cy;
        Set<String> next=new LinkedHashSet<>();
        for(int ry=Math.floorDiv(y-96,256);ry<=Math.floorDiv(y+96,256);ry++)for(int rx=Math.floorDiv(x-96,256);rx<=Math.floorDiv(x+96,256);rx++)next.add(rx+","+ry);
        wanted=Set.copyOf(next);areas.keySet().removeIf(k->!next.contains(k));
        for(String key:next)if(!areas.containsKey(key)&&loading.add(key))worker.execute(()->{
            try {
                JSONArray ids=manifest.getJSONObject("regions").optJSONArray(key);List<JSONObject> data=new ArrayList<>();
                if(ids!=null)for(Object id:ids){Path file=buildings.get(id.toString());try(Reader in=new BufferedReader(new InputStreamReader(new GZIPInputStream(Files.newInputStream(file)),StandardCharsets.UTF_8))){data.add(new JSONObject(new JSONTokener(in)));}}
                Area ready=area(data,key);if(wanted.contains(key)){areas.put(key,ready);generation.incrementAndGet();}
            }catch(Exception error){System.err.println("[heyzed] Construction region "+key+": "+error);}
            finally {loading.remove(key);}
        });
    }
    private Area at(int x,int y){return legacy!=null?legacy:areas.get(Math.floorDiv(x,256)+","+Math.floorDiv(y,256));}
    boolean ready(int x,int y){return at(x,y)!=null;}
    JSONObject cell(int x,int y,int z){Area area=at(x,y);return area==null?null:area.cells.get(x+","+y+","+z);}
    boolean floorOwner(int x,int y,int z){Area area=at(x,y);return area!=null&&area.footprint.contains(x+","+y+","+z);}
    JSONObject binding(String name){return bindings.optJSONObject(name);}
    JSONObject attachment(String name){return attachmentBindings==null?null:attachmentBindings.optJSONObject(name);}
    List<Address> candidates(int x,int y){
        if(legacy!=null)return legacy.order;
        int cx=Math.floorDiv(x,8),cy=Math.floorDiv(y,8);long areaGeneration=generation.get();
        if(candidateQuery==null||cx!=candidateChunkX||cy!=candidateChunkY||nearbyGeneration!=areaGeneration){
            candidateChunkX=cx;candidateChunkY=cy;nearbyGeneration=areaGeneration;
            List<List<Address>> orders=new ArrayList<>();for(Area area:areas.values())orders.add(area.order);
            candidateQuery=new CandidateQuery(++candidateSequence,x,y,List.copyOf(orders));
        }
        CandidateQuery query=candidateQuery;
        if(candidatePublished!=query.sequence&&candidateScheduled.compareAndSet(false,true))worker.execute(()->{
            try{
                List<Address> order=new ArrayList<>();
                for(List<Address> addresses:query.areas)for(Address address:addresses)
                    if(Math.abs(address.x-query.x)<=104&&Math.abs(address.y-query.y)<=104)order.add(address);
                order.sort(Comparator.comparingLong(a->{long dx=(long)a.x-query.x,dy=(long)a.y-query.y;return dx*dx+dy*dy;}));
                if(candidateQuery==query){nearby=List.copyOf(order);candidatePublished=query.sequence;}
            }finally{candidateScheduled.set(false);}
        });
        return nearby;
    }
}
