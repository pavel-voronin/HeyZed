package com.pavelvoronin.heyzed;

import java.nio.file.*;
import java.util.*;
import java.util.zip.GZIPOutputStream;
import org.json.*;

public final class ConstructionSourceRegression {
    private static Path area(Path root,String name,int start,int count)throws Exception{
        JSONArray cells=new JSONArray();
        for(int x=start;x<start+count;x++)for(int y=start;y<start+count;y++)
            cells.put(new JSONObject().put("address",new JSONArray(List.of(x,y,0))).put("walls",new JSONArray()));
        Path file=root.resolve(name+".json.gz");
        try(var out=new GZIPOutputStream(Files.newOutputStream(file))){out.write(new JSONObject().put("cells",cells).put("footprint",new JSONArray()).toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));}
        return file;
    }
    private static void await(UnrealConstructionSource source,int x,int y,int count)throws Exception{
        long deadline=System.nanoTime()+10_000_000_000L;
        while(System.nanoTime()<deadline){
            List<UnrealConstructionSource.Address> addresses=source.candidates(x,y);
            if(addresses.size()==count&&addresses.stream().allMatch(a->Math.abs(a.x()-x)<=104&&Math.abs(a.y()-y)<=104)){
                long previous=-1;
                for(var a:addresses){long dx=(long)a.x()-x,dy=(long)a.y()-y,distance=dx*dx+dy*dy;if(distance<previous)throw new AssertionError("Unsorted candidate publication");previous=distance;}
                return;
            }
            Thread.sleep(2);
        }
        throw new AssertionError("Latest candidate view was not published");
    }
    private static void overlapOwnership()throws Exception{
        JSONObject context=new JSONObject().put("axis","N").put("contextOnly",true).put("sources",new JSONArray(List.of("context")));
        JSONObject original=new JSONObject().put("address",new JSONArray(List.of(1,1,0))).put("floor","old").put("walls",new JSONArray(List.of(context)));
        JSONObject authoritative=new JSONObject().put("axis","N").put("sources",new JSONArray(List.of("wall")));
        JSONObject incoming=new JSONObject().put("address",new JSONArray(List.of(1,1,0))).put("floor","new").put("walls",new JSONArray(List.of(authoritative)));
        var method=UnrealConstructionSource.class.getDeclaredMethod("area",List.class,String.class);method.setAccessible(true);
        var area=(UnrealConstructionSource.Area)method.invoke(null,List.of(new JSONObject().put("cells",new JSONArray(List.of(original))).put("footprint",new JSONArray()),new JSONObject().put("cells",new JSONArray(List.of(incoming))).put("footprint",new JSONArray())),null);
        JSONObject result=area.cells().get("1,1,0");
        if(result==original||!result.getString("floor").equals("new")||!result.getJSONArray("walls").getJSONObject(0).getJSONArray("sources").getString(0).equals("wall"))throw new AssertionError("Overlapping construction facts were not merged");
        if(!original.getString("floor").equals("old")||!original.getJSONArray("walls").getJSONObject(0).getBoolean("contextOnly"))throw new AssertionError("Area assembly mutated an input snapshot");
    }
    public static void main(String[] args)throws Exception{
        overlapOwnership();
        Path root=Path.of(args[0]).toAbsolutePath();Files.createDirectories(root);
        System.setProperty("user.home",root.resolve("empty-home").toString());
        Path manifestDir=root.resolve("source/maps");Files.createDirectories(manifestDir);
        Path near=area(root,"near",0,41),far=area(root,"far",500,21);
        Files.writeString(root.resolve("source/definitions.json"),new JSONObject().put("sources",new JSONObject()).toString());
        JSONObject regions=new JSONObject().put("0,0",new JSONArray(List.of("near"))).put("1,1",new JSONArray(List.of("far"))).put("1,2",new JSONArray(List.of("far"))).put("2,1",new JSONArray(List.of("far"))).put("2,2",new JSONArray(List.of("far")));
        JSONArray buildings=new JSONArray().put(new JSONObject().put("id","near").put("file",near.toString())).put(new JSONObject().put("id","far").put("file",far.toString()));
        Path manifest=manifestDir.resolve("manifest.json");Files.writeString(manifest,new JSONObject().put("schema",2).put("regions",regions).put("buildings",buildings).toString());
        var constructor=UnrealConstructionSource.class.getDeclaredConstructor(Path.class);constructor.setAccessible(true);
        UnrealConstructionSource source=constructor.newInstance(manifest);
        source.request(4,4);await(source,4,4,41*41);
        for(int i=0;i<20;i++){int at=i%2==0?500:4;source.request(at,at);source.candidates(at,at);}
        source.request(500,500);await(source,500,500,21*21);
        if(source.cell(4,4,0)!=null)throw new AssertionError("Unwanted region retained");
        if(source.cell(500,500,0)==null)throw new AssertionError("Requested region lost");
        source.request(4,4);await(source,4,4,41*41);
        System.out.println("PASS: asynchronous candidate ordering, rapid request replacement and region reload, overlapping facts and input ownership");
    }
}
