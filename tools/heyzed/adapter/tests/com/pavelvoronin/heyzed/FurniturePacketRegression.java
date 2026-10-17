package com.pavelvoronin.heyzed;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.json.*;
import static com.pavelvoronin.heyzed.UnrealFurniture.*;

public final class FurniturePacketRegression {
    static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    static List<JSONObject> encode(Job job){
        var encoder=new Encoder(job);var parts=new ArrayList<JSONObject>();
        do{
            String text=encoder.next();check(text.getBytes(StandardCharsets.UTF_8).length<=60000,"Transport slot overflow");
            parts.add(new JSONObject(text));
        }while(!encoder.finished);
        return parts;
    }
    static void multipart(String op,String tile){
        var address=new Address(-1,-2,3);var rows=new ArrayList<Row>();
        for(int i=0;i<401;i++)rows.add(new Row(address,new Fact(Long.MAX_VALUE-i,tile,"closed")));
        var packets=encode(new Job(op,123,new Key(-1,-1),81,900,List.copyOf(rows),op.equals("change")?address:null,0));
        check(packets.size()>1,"Expected multipart stream");int count=0;
        for(int i=0;i<packets.size();i++){
            var p=packets.get(i);check(p.getInt("schema")==3&&p.getLong("epoch")==123&&p.getLong("revision")==900&&p.getLong("generation")==81,"Version identity lost");
            check(p.getInt("part")==i&&p.getBoolean("last")== (i==packets.size()-1),"Multipart boundary incorrect");
            if(op.equals("change"))check(p.getJSONArray("address").getInt(0)==-1,"Change address lost");
            for(Object value:p.getJSONArray("objects")){
                var row=(JSONArray)value;check(row.getInt(0)==-1&&row.getInt(1)==-2&&row.getInt(2)==3,"Address changed");
                check(row.getString(3).equals(Long.toString(Long.MAX_VALUE-count)),"Object identity precision lost");
                check(row.getString(4).equals(tile),"UTF8 tile changed");count++;
            }
        }
        check(count==401,"Rows dropped or duplicated");
    }
    public static void main(String[] args){
        multipart("snapshot","chair");multipart("change","\u0441\u0442\u0443\u043b".repeat(200));
        var empty=encode(new Job("change",1,new Key(2,3),4,5,List.of(),new Address(16,24,-1),0));
        check(empty.size()==1&&empty.getFirst().getJSONArray("objects").isEmpty()&&empty.getFirst().getBoolean("last"),"Deletion lost");
        var blank=encode(new Job("snapshot",1,new Key(2,3),4,6,List.of(),null,0));
        check(blank.size()==1&&blank.getFirst().getJSONArray("objects").isEmpty(),"Empty native chunk lost");
        for(String op:List.of("reset","unload","ready")){
            var packet=encode(new Job(op,7,op.equals("unload")?new Key(-1,9):null,8,9,List.of(),null,391)).getFirst();
            check(!packet.has("objects")&&!packet.has("part"),"Control event carries placement data");
            if(op.equals("ready"))check(packet.getInt("chunks")==391,"Initial chunk count lost");
            if(op.equals("unload"))check(packet.getLong("generation")==8,"Unload generation lost");
        }
        System.out.println("Furniture packet regression: multipart snapshots/changes, UTF8 limits, precise IDs, empty chunks, deletions and lifecycle controls passed");
    }
}
