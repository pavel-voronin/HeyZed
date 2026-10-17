package com.pavelvoronin.heyzed;

import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.zip.InflaterInputStream;
import org.json.*;

/** Immutable source baselines are read off the game and transport threads. */
final class UnrealBake {
    private record Region(List<List<UnrealGround.Tile>> palette,char[] cells) {}
    private static final ConcurrentHashMap<Long,Region> cache=new ConcurrentHashMap<>();
    private static final Set<Long> requested=ConcurrentHashMap.newKeySet();
    private static final BlockingQueue<Long> requests=new ArrayBlockingQueue<>(128);
    private static final ArrayDeque<Long> order=new ArrayDeque<>();
    private static volatile boolean initializing=true;
    private static volatile String identity="";
    private static Path root;
    private static boolean started;
    private static long address(int x,int y){return ((long)x<<32)|(y&0xffffffffL);}
    static String identity(){return identity;}

    static void begin(){
        if(started)return;started=true;
        List<String> sources=new ArrayList<>();
        int layers=0;
        for(var map:zombie.iso.IsoLot.MapFiles){
            layers++;
            for(var h:map.infoHeaders.values())sources.add(h.absoluteFilePath);
        }
        final int sourceLayers=layers;
        Thread reader=new Thread(()->load(List.copyOf(sources),sourceLayers),"heyzed-unreal-baseline");
        reader.setDaemon(true);reader.start();
    }
    private static void load(List<String> sources,int sourceLayers){
        try{
            Path descriptor=Path.of(System.getProperty("heyzed.unreal.world",System.getProperty("user.home")+"/.heyzed/world/current.json"));
            JSONObject manifest=new JSONObject(Files.readString(descriptor));
            if(manifest.getInt("version")!=1||sourceLayers!=1)throw new IOException("Map layer set differs from the baked base");
            Set<String> nativeHeaders=new HashSet<>();
            for(String source:sources)nativeHeaders.add(Path.of(source).toAbsolutePath().normalize().toString().toLowerCase(Locale.ROOT));
            JSONArray files=manifest.getJSONArray("source_files");Set<String> expected=new HashSet<>();
            for(int n=0;n<files.length();n++){
                JSONArray file=files.getJSONArray(n);
                for(int offset:new int[]{0,3}){
                    Path path=Path.of(file.getString(offset));
                    if(Files.size(path)!=file.getLong(offset+1)||Files.getLastModifiedTime(path).toMillis()!=file.getLong(offset+2))throw new IOException("Installed map source changed");
                }
                expected.add(Path.of(file.getString(0)).toAbsolutePath().normalize().toString().toLowerCase(Locale.ROOT));
            }
            if(!nativeHeaders.equals(expected))throw new IOException("Native map sources differ from the baked base");
            root=Path.of(manifest.getString("folder"));identity=manifest.getString("identity");initializing=false;
            System.out.println("[heyzed] Unreal baked floor base ready: "+files.length()+" native cells");
            while(true){
                long key=requests.take();
                try{
                    Region region=read((int)(key>>32),(int)key);
                    cache.put(key,region);order.addLast(key);
                    while(order.size()>128)cache.remove(order.removeFirst());
                }catch(IOException|RuntimeException failure){
                    // A broken baseline cannot authorize delta-only ground delivery.
                    identity="";cache.clear();
                    System.err.println("[heyzed] Unreal baked base disabled: "+failure);
                    break;
                }finally{requested.remove(key);}
            }
        }catch(Exception failure){
            identity="";initializing=false;
            System.err.println("[heyzed] Unreal baked base unavailable: "+failure.getMessage());
        }
    }
    private static Region read(int x,int y)throws IOException{
        Path source=root.resolve(x+"_"+y+".pzf");
        if(!Files.exists(source))return new Region(List.of(List.of()),new char[65536]);
        try(var input=new DataInputStream(new InflaterInputStream(Files.newInputStream(source)))){
            if(input.readInt()!=0x505A4246||input.readInt()!=1||input.readInt()!=x||input.readInt()!=y)throw new IOException("Invalid baked floor header");
            int size=input.readInt();if(size<2||size>16*1024*1024)throw new IOException("Invalid baked palette length");
            byte[] text=input.readNBytes(size);if(text.length!=size)throw new EOFException();
            JSONArray definitions=new JSONArray(new String(text,StandardCharsets.UTF_8));
            List<List<UnrealGround.Tile>> palette=new ArrayList<>();
            for(int n=0;n<definitions.length();n++){
                List<UnrealGround.Tile> layers=new ArrayList<>();JSONArray row=definitions.getJSONArray(n);
                for(int t=0;t<row.length();t++){
                    JSONObject tile=row.getJSONObject(t);
                    layers.add(new UnrealGround.Tile(tile.getString("name"),tile.getString("material"),tile.getBoolean("overlay"),tile.getString("edges")));
                }
                palette.add(UnrealGround.stateLayers(List.copyOf(layers)));
            }
            char[] cells=new char[65536];
            for(int n=0;n<cells.length;n++){
                cells[n]=input.readChar();
                if(cells[n]>=palette.size())throw new IOException("Invalid baked palette index");
            }
            if(input.read()!=-1)throw new IOException("Unexpected baked floor payload");
            return new Region(List.copyOf(palette),cells);
        }
    }
    static boolean ready(int x,int y){
        if(initializing)return false;
        if(identity.isEmpty())return true;
        long key=address(Math.floorDiv(x,256),Math.floorDiv(y,256));
        if(cache.containsKey(key))return true;
        if(requested.add(key)&&!requests.offer(key))requested.remove(key);
        return false;
    }
    static List<UnrealGround.Tile> cell(int x,int y){
        if(identity.isEmpty())return null;
        Region region=cache.get(address(Math.floorDiv(x,256),Math.floorDiv(y,256)));
        if(region==null)return null;
        return region.palette.get(region.cells[Math.floorMod(y,256)*256+Math.floorMod(x,256)]);
    }
    private UnrealBake(){}
}
