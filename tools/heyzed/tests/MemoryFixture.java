package com.pavelvoronin.heyzed;
import java.io.*;
import java.util.concurrent.*;
import org.json.*;

/** Cross-process fixture uses the production Java memory publisher. */
public class MemoryFixture {
    public static void main(String[] args)throws Exception{
        System.setProperty("heyzed.unreal.memory",args[0]);
        LinkedBlockingQueue<String> control=new LinkedBlockingQueue<>();
        Thread input=new Thread(()->{try{var reader=new BufferedReader(new InputStreamReader(System.in));String line;while((line=reader.readLine())!=null)control.add(line);}catch(Exception ignored){}});input.setDaemon(true);input.start();
        long epoch=System.currentTimeMillis(),seq=0;String intent=null;boolean running=true;int bulk=0;
        UnrealMemory memory=new UnrealMemory(epoch);
        String world=new JSONObject().put("schema",1).put("epoch",epoch).put("revision",1).put("dictionary",new JSONArray()).put("cells",new JSONArray()).put("padding","x".repeat(60000)).toString();
        java.util.ArrayDeque<JSONObject> pending=new java.util.ArrayDeque<>();
        System.out.println("ready "+epoch);System.out.flush();
        try{
            while(running){
                String line;
                while((line=control.poll())!=null){
                    JSONObject c=new JSONObject(line);
                    switch(c.getString("channel")){
                        case "quit"->running=false;
                        case "restart"->{memory.close();memory=new UnrealMemory(++epoch);intent=null;}
                        case "bulk"->bulk+=c.getInt("count");
                        default->pending.add(c);
                    }
                }
                String next;while((next=memory.poll("input"))!=null)intent=next;
                if(intent!=null){
                    String[] f=intent.trim().split(" ");
                    memory.offer("motion","S3 "+epoch+" "+(++seq)+" "+f[1]+" "+f[2]+" "+f[3]+" 0 100.5 100.5 0 1 0 0 0 1000 87,83,65,68,340,342,67 "+System.nanoTime()+" 0 0 0");
                }
                while((next=memory.poll("commands"))!=null){
                    if(next.startsWith("R "))continue;
                    JSONObject c=new JSONObject(next);
                    pending.add(new JSONObject().put("channel","events").put("payload",new JSONObject().put("type","ack").put("token",c.getString("token")).put("accepted",false)));
                }
                if(!pending.isEmpty()){
                    JSONObject c=pending.peek();if(memory.offer(c.getString("channel"),c.getJSONObject("payload").toString()))pending.remove();
                }
                for(int i=0;i<16&&bulk>0;i++)if(memory.offer("world",world))bulk--;else break;
                Thread.sleep(4);
            }
        }finally{memory.close();}
    }
}
