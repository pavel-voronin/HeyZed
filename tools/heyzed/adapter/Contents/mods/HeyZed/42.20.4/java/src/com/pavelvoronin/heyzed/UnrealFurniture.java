package com.pavelvoronin.heyzed;
import java.nio.file.*;
import java.lang.ref.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;
import zombie.iso.*;
import zombie.characters.IsoPlayer;
final class UnrealFurniture {
    record Address(int x,int y,int z) {}
    record Key(int x,int y) {}
    record Fact(long id,String tile,String state) {}
    record Row(Address address,Fact fact) {}
    record Job(String op,long epoch,Key chunk,long generation,long revision,List<Row> rows,Address address,int chunks) {}
    private record Signal(WeakReference<IsoChunk> chunk,Key key,short loadId,boolean loaded) {}
    private record Handle(long id,short loadId) {}
    private static final class Identity extends WeakReference<IsoObject> {
        final int hash;
        Identity(IsoObject object,ReferenceQueue<IsoObject> queue){super(object,queue);hash=System.identityHashCode(object);}
        public int hashCode(){return hash;}
        public boolean equals(Object value){return this==value||value instanceof Identity other&&get()!=null&&get()==other.get();}
    }
    private static final class Chunk {
        final Key key;final WeakReference<IsoChunk> nativeChunk;final short loadId;final long generation;
        final Map<Address,List<Fact>> cells=new HashMap<>();int cursor,limit;boolean sent;Job snapshot;
        Chunk(IsoChunk chunk){key=new Key(chunk.wx,chunk.wy);nativeChunk=new WeakReference<>(chunk);loadId=chunk.getLoadID();generation=++nextGeneration;cursor=Math.max(-32,chunk.getMinLevel())*64;limit=(Math.min(31,chunk.getMaxLevel())+1)*64;}
    }
    private static volatile Set<String> tiles;
    private static boolean initialized,bootstrapped,initialReady;
    private static volatile boolean requestedReset=true;
    private static long epoch,revision,nextId,nextGeneration,nextAudit;
    private static final Map<Key,Chunk> chunks=new HashMap<>();
    private static final ArrayDeque<Chunk> scans=new ArrayDeque<>();
    private static final ReferenceQueue<IsoObject> collected=new ReferenceQueue<>();
    private static final Map<Identity,Handle> identities=new HashMap<>();
    private static final ArrayBlockingQueue<Signal> signals=new ArrayBlockingQueue<>(4096);
    private static final Set<Address> dirty=ConcurrentHashMap.newKeySet();
    private static final ArrayBlockingQueue<Job> jobs=new ArrayBlockingQueue<>(64);
    private static final ArrayBlockingQueue<String> ready=new ArrayBlockingQueue<>(16);
    private static Encoder encoder;
    static void reconcile(){requestedReset=true;}
    static void reset(){requestedReset=true;}
    static void loaded(IsoChunk chunk){signal(chunk,true);}
    static void unloaded(IsoChunk chunk){signal(chunk,false);}
    private static void signal(IsoChunk chunk,boolean loaded){
        if(!signals.offer(new Signal(new WeakReference<>(chunk),new Key(chunk.wx,chunk.wy),chunk.getLoadID(),loaded)))requestedReset=true;
    }
    static void dirty(IsoGridSquare square){
        if(square==null)return;
        if(dirty.size()>=16384){requestedReset=true;return;}
        dirty.add(new Address(square.getX(),square.getY(),square.getZ()));
    }
    private static List<Fact> capture(IsoGridSquare square,short loadId){
        var facts=new ArrayList<Fact>();var objects=square==null?null:square.getObjects();
        for(int i=0;objects!=null&&i<objects.size();i++){
            IsoObject object=objects.get(i);var sprite=object.getSprite();if(sprite==null||sprite.getName()==null)continue;
            String tile=sprite.getName();if(!tiles.contains(tile))tile=tile.replace("_on_","_");if(!tiles.contains(tile))continue;
            Identity lookup=new Identity(object,null);Handle handle=identities.get(lookup);
            if(handle==null||handle.loadId!=loadId){handle=new Handle(++nextId,loadId);identities.put(new Identity(object,collected),handle);}
            facts.add(new Fact(handle.id,tile,"closed"));
        }
        return List.copyOf(facts);
    }
    private static List<Row> rows(Map<Address,List<Fact>> cells){
        var result=new ArrayList<Row>();for(var cell:cells.entrySet())for(var fact:cell.getValue())result.add(new Row(cell.getKey(),fact));return List.copyOf(result);
    }
    private static void observe(IsoChunk chunk){
        if(chunk==null||!chunk.loaded)return;Key key=new Key(chunk.wx,chunk.wy);Chunk old=chunks.get(key);
        if(old!=null&&old.nativeChunk.get()==chunk&&old.loadId==chunk.getLoadID())return;
        Chunk current=new Chunk(chunk);chunks.put(key,current);scans.add(current);
    }
    static void tick(IsoPlayer player,long generation){
        if(tiles==null)return;
        var map=player.getCell().getChunkMap(player.getPlayerNum());if(map==null)return;
        if(epoch!=generation){epoch=generation;requestedReset=true;}
        if(requestedReset){
            if(!jobs.offer(new Job("reset",epoch,null,0,revision+1,List.of(),null,0)))return;
            ++revision;requestedReset=false;chunks.clear();scans.clear();dirty.clear();signals.clear();identities.clear();bootstrapped=false;initialReady=false;nextAudit=0;
        }
        long now=System.nanoTime();
        if(!bootstrapped||now>=nextAudit){
            var present=new HashSet<Key>();var initial=new ArrayList<IsoChunk>();
            for(int y=0;y<IsoChunkMap.chunkGridWidth;y++)for(int x=0;x<IsoChunkMap.chunkGridWidth;x++){
                var chunk=map.getChunk(x,y);if(chunk!=null&&chunk.loaded){present.add(new Key(chunk.wx,chunk.wy));initial.add(chunk);}
            }
            if(!bootstrapped)initial.sort(Comparator.comparingDouble(c->Math.abs(c.wx*8-player.getX())+Math.abs(c.wy*8-player.getY())));
            for(var chunk:initial)observe(chunk);
            boolean retry=false;
            for(var iterator=chunks.entrySet().iterator();iterator.hasNext();){
                var entry=iterator.next();if(present.contains(entry.getKey()))continue;
                Chunk gone=entry.getValue();
                if(jobs.offer(new Job("unload",epoch,entry.getKey(),gone.generation,revision+1,List.of(),null,0))){++revision;iterator.remove();}else retry=true;
            }
            bootstrapped=!initial.isEmpty();nextAudit=retry?now:now+500_000_000L;
        }
        long deadline=System.nanoTime()+1_000_000;
        for(int i=0;i<64&&jobs.remainingCapacity()>0;i++){
            Signal event=signals.poll();if(event==null)break;
            IsoChunk nativeChunk=event.chunk.get();Chunk old=chunks.get(event.key);
            if(event.loaded){
                var current=map.getChunkForGridSquare(event.key.x*8,event.key.y*8);
                if(nativeChunk!=null&&current==nativeChunk&&nativeChunk.getLoadID()==event.loadId)observe(nativeChunk);
            }else if(old!=null&&old.nativeChunk.get()==nativeChunk&&old.loadId==event.loadId){
                jobs.add(new Job("unload",epoch,event.key,old.generation,++revision,List.of(),null,0));chunks.remove(event.key);
            }
            if(System.nanoTime()>=deadline)break;
        }
        Reference<? extends IsoObject> expired;for(int n=0;n<256&&(expired=collected.poll())!=null;n++)identities.remove(expired);
        for(int n=0;n<512&&System.nanoTime()<deadline&&jobs.remainingCapacity()>0;n++){
            Chunk chunk=scans.peek();if(chunk==null)break;
            IsoChunk owner=chunk.nativeChunk.get();
            if(chunks.get(chunk.key)!=chunk){scans.poll();continue;}
            if(owner==null||!owner.loaded||owner.getLoadID()!=chunk.loadId){
                jobs.add(new Job("unload",epoch,chunk.key,chunk.generation,++revision,List.of(),null,0));chunks.remove(chunk.key);scans.poll();continue;
            }
            if(chunk.cursor>=chunk.limit){
                if(chunk.snapshot==null)chunk.snapshot=new Job("snapshot",epoch,chunk.key,chunk.generation,++revision,rows(chunk.cells),null,0);
                if(jobs.offer(chunk.snapshot)){chunk.snapshot=null;chunk.sent=true;scans.poll();}else break;
                continue;
            }
            int z=Math.floorDiv(chunk.cursor,64),xy=Math.floorMod(chunk.cursor++,64);
            Address address=new Address(chunk.key.x*8+xy/8,chunk.key.y*8+xy%8,z);
            List<Fact> facts=capture(player.getCell().getGridSquare(address.x,address.y,address.z),chunk.loadId);
            if(!facts.isEmpty())chunk.cells.put(address,facts);
        }
        for(int n=0;n<128&&System.nanoTime()<deadline&&jobs.remainingCapacity()>0;n++){
            var iterator=dirty.iterator();if(!iterator.hasNext())break;Address address=iterator.next();dirty.remove(address);
            Chunk chunk=chunks.get(new Key(Math.floorDiv(address.x,8),Math.floorDiv(address.y,8)));
            if(chunk==null)continue;if(!chunk.sent){dirty.add(address);break;}
            IsoChunk owner=chunk.nativeChunk.get();if(owner==null||!owner.loaded||owner.getLoadID()!=chunk.loadId)continue;
            List<Fact> facts=capture(player.getCell().getGridSquare(address.x,address.y,address.z),chunk.loadId);
            if(facts.equals(chunk.cells.getOrDefault(address,List.of())))continue;
            if(facts.isEmpty())chunk.cells.remove(address);else chunk.cells.put(address,facts);
            var changed=new ArrayList<Row>();for(var fact:facts)changed.add(new Row(address,fact));
            jobs.add(new Job("change",epoch,chunk.key,chunk.generation,++revision,List.copyOf(changed),address,0));
        }
        if(!initialReady&&bootstrapped&&scans.isEmpty()&&signals.isEmpty()&&jobs.remainingCapacity()>0){
            jobs.add(new Job("ready",epoch,null,0,++revision,List.of(),null,chunks.size()));initialReady=true;
        }
    }
    static final class Encoder {
        final Job job;int cursor,part;boolean finished;
        Encoder(Job source){job=source;}
        String next(){
            int count=Math.min(128,job.rows.size()-cursor);
            while(true){
                var event=new JSONObject().put("type","furniture").put("schema",3).put("epoch",job.epoch).put("revision",job.revision).put("op",job.op);
                if(job.chunk!=null)event.put("chunk",new JSONArray().put(job.chunk.x).put(job.chunk.y)).put("generation",job.generation);
                if(job.op.equals("ready"))event.put("chunks",job.chunks);
                boolean data=job.op.equals("snapshot")||job.op.equals("change");
                if(data){
                    var objects=new JSONArray();for(int i=0;i<count;i++){
                        Row row=job.rows.get(cursor+i);Address a=row.address;Fact f=row.fact;
                        objects.put(new JSONArray().put(a.x).put(a.y).put(a.z).put(Long.toString(f.id)).put(f.tile).put(f.state));
                    }
                    event.put("objects",objects).put("part",part).put("last",cursor+count==job.rows.size());
                    if(job.address!=null)event.put("address",new JSONArray().put(job.address.x).put(job.address.y).put(job.address.z));
                }
                String text=event.toString();
                if(text.getBytes(java.nio.charset.StandardCharsets.UTF_8).length<=60000){cursor+=count;++part;finished=!data||cursor==job.rows.size();return text;}
                if(count<=1)throw new IllegalArgumentException("Furniture object exceeds transport slot");count/=2;
            }
        }
    }
    static void prepare(){
        if(!initialized){
            initialized=true;Path input=Path.of(System.getProperty("user.home"),".heyzed","furniture","current.json");
            try{
                var data=new JSONObject(Files.readString(input));if(data.getInt("schema")!=3)throw new IllegalArgumentException("Furniture model catalogue update required");
                var supported=new HashSet<String>();for(Object name:data.getJSONArray("tiles"))supported.add(name.toString());tiles=Set.copyOf(supported);
                System.out.println("[heyzed] Native-chunk furniture catalogue ready: "+tiles.size()+" tile bindings");
            }catch(Exception error){throw new IllegalStateException("Furniture model catalogue failed",error);}
        }
        if(ready.remainingCapacity()==0)return;
        if(encoder==null){Job job=jobs.poll();if(job==null)return;encoder=new Encoder(job);}
        ready.add(encoder.next());if(encoder.finished)encoder=null;
    }
    static void flush(UnrealMemory memory){String event=ready.peek();if(event!=null&&memory.offer("events",event))ready.remove(event);}
    private UnrealFurniture(){}
}
