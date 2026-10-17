package com.pavelvoronin.heyzed;

import java.nio.ByteBuffer;
import java.nio.file.*;
import java.util.Arrays;
import zombie.util.ByteBufferPool;

public final class NativeSaveRegression {
    public static void main(String[] args)throws Exception{
        Path root=Path.of(args[0]);Files.createDirectories(root);
        ByteBufferPool pool=new ByteBufferPool();
        int cases=0;
        for(int length:new int[]{0,1,8191,8192,65536,1048576,6291456}){
            byte[] expected=new byte[length];for(int i=0;i<length;i++)expected[i]=(byte)(i*71+length);
            pool.allocate(37).put(new byte[37]);
            var source=pool.allocate(length);source.put(expected);
            byte[] nativeBytes=new byte[source.capacity()];
            for(int i=0;i<nativeBytes.length;i++)nativeBytes[i]=source.get(i);
            Path file=root.resolve("native-buffer-"+length+".bin");Files.write(file,new byte[length+97]);
            int position=source.position();
            UnrealNativeSave.writePrepared(file.toString(),source.slice());
            if(!Arrays.equals(nativeBytes,Files.readAllBytes(file)))throw new AssertionError("Native slice bytes or file truncation differ: "+length);
            if(position!=source.position())throw new AssertionError("Native pool position changed");
            cases++;
        }
        for(boolean direct:new boolean[]{false,true}){
            ByteBuffer source=direct?ByteBuffer.allocateDirect(511):ByteBuffer.allocate(511);
            for(int i=0;i<source.capacity();i++)source.put(i,(byte)i);
            source.position(31);source.limit(479);
            byte[] expected=new byte[source.remaining()];source.duplicate().get(expected);
            Path file=root.resolve("slice-"+direct+".bin");
            UnrealNativeSave.writePrepared(file.toString(),source);
            if(!Arrays.equals(expected,Files.readAllBytes(file))||source.position()!=31||source.limit()!=479)throw new AssertionError("Input view changed");
            cases++;
        }
        try{UnrealNativeSave.writePrepared(root.toString(),ByteBuffer.allocate(1));throw new AssertionError("IO failure suppressed");}
        catch(java.io.IOException expected){}
        System.out.println("PASS: "+cases+" native pooled/heap/direct slices, exact bytes, truncation, ownership and IO failure propagation");
    }
}
