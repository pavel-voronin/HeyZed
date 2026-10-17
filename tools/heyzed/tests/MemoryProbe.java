package com.pavelvoronin.heyzed;
import java.io.*;
import java.nio.channels.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;
/** Native-backend probe; no OS keyboard or mouse input. */
public class MemoryProbe {
 public static void main(String[] args)throws Exception{
  Path root=Path.of(args[0]),generation=root.resolve(Files.readString(root.resolve("current")).trim());
  try(FileChannel lease=FileChannel.open(root.resolve("consumer.lock"),StandardOpenOption.CREATE,StandardOpenOption.READ,StandardOpenOption.WRITE);FileLock lock=lease.tryLock()){
   if(lock==null)throw new IOException("HeyZed consumer is already active");
   Map<String,UnrealMemory.Ring> rings=new HashMap<>();
   for(String name:List.of("input","commands","motion","world","events"))rings.put(name,new UnrealMemory.Ring(generation.resolve(name+".ring"),false));
   LinkedBlockingQueue<String> input=new LinkedBlockingQueue<>();
   Thread reader=new Thread(()->{try{var in=new BufferedReader(new InputStreamReader(System.in));String s;while((s=in.readLine())!=null)input.add(s);input.add("quit");}catch(Exception ignored){input.add("quit");}});reader.setDaemon(true);reader.start();
   rings.get("commands").offer("R probe");
   try{
    while(true){
     String s;
     while((s=input.poll())!=null){if(s.equals("quit"))return;rings.get("input").offer(s);}
     String latest=null;
     while((s=rings.get("motion").poll())!=null){
      if(s.startsWith("S"))latest=s;
      else{JSONObject frame=new JSONObject(s);if(frame.optString("type").equals("frame"))latest=frame.getString("player");}
     }
     if(latest!=null)System.out.println(new JSONObject().put("channel","motion").put("payload",latest));
     for(int i=0;i<4&&(s=rings.get("world").poll())!=null;i++)System.out.println(new JSONObject().put("channel","world").put("payload",new JSONObject(s)));
     while(rings.get("events").poll()!=null){}
     System.out.flush();Thread.sleep(4);
    }
   }finally{for(var ring:rings.values())ring.close();}
  }
 }
}
