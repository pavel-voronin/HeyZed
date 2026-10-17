package com.pavelvoronin.heyzed;

import java.lang.reflect.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import org.json.*;

public final class PosePacketRegression {
    private static final class Animation extends JSONObject {
        final AtomicInteger serializations=new AtomicInteger();
        Animation(JSONObject source){super(source,JSONObject.getNames(source));}
        @Override public String toString(){
            if(!Thread.currentThread().getName().equals("pose-test-encoder"))throw new AssertionError("Animation serialized on the capture thread");
            serializations.incrementAndGet();return super.toString();
        }
    }
    public static void main(String[] args)throws Exception{
        var type=Class.forName("com.pavelvoronin.heyzed.UnrealEntities$PosePacket");
        var constructor=type.getDeclaredConstructors()[0];constructor.setAccessible(true);
        var binary=type.getDeclaredMethod("binary");binary.setAccessible(true);var text=type.getDeclaredMethod("text");text.setAccessible(true);
        JSONObject description=new JSONObject().put("mode","local").put("key","walk\nrun").put("tracks",new JSONArray().put(new JSONObject().put("phase",.25).put("mask",new JSONArray(List.of(1,.5,0)))));
        Animation animation=new Animation(description);float[] world=new float[16];for(int i=0;i<16;i++)world[i]=i*.125f;byte[] bones=new byte[64],locals=new byte[128];Arrays.fill(bones,(byte)7);Arrays.fill(locals,(byte)11);
        Object packet=constructor.newInstance("42",10L,20L,30L,40L,world,1.25,2.5,3.75,bones,locals,true,"car",0,animation);
        if(animation.serializations.get()!=0)throw new AssertionError("Capture performed serialization");
        try(var worker=Executors.newSingleThreadExecutor(r->new Thread(r,"pose-test-encoder"))){
            byte[] actual=worker.submit(()->(byte[])binary.invoke(packet)).get();byte[] id="42".getBytes(StandardCharsets.UTF_8),car="car".getBytes(StandardCharsets.UTF_8),action=description.toString().getBytes(StandardCharsets.UTF_8);
            ByteBuffer expected=ByteBuffer.allocate(156+id.length+car.length+bones.length+locals.length+action.length).order(ByteOrder.LITTLE_ENDIAN);
            expected.putInt(0x31504542).putInt(2).putInt(1).putLong(10).putLong(20).putLong(30).putLong(40).putInt(id.length).putInt(car.length).putInt(0).putInt(1).putInt(2).putInt(action.length);
            for(float value:world)expected.putFloat(value);expected.putDouble(1.25).putDouble(2.5).putDouble(3.75).put(id).put(car).put(bones).put(locals).put(action);
            if(!Arrays.equals(actual,expected.array()))throw new AssertionError("Binary pose protocol changed");
            if(worker.submit(()->(byte[])binary.invoke(packet)).get()!=actual||animation.serializations.get()!=1)throw new AssertionError("Retry repeated serialization");
            String json=worker.submit(()->(String)text.invoke(packet)).get();if(!new JSONObject(json).getJSONObject("animation").similar(description))throw new AssertionError("Text transition lost animation facts");
        }
        System.out.println("PASS: capture does not serialize animation; worker binary bytes match protocol v2; retries and text transitions retain facts");
    }
}
