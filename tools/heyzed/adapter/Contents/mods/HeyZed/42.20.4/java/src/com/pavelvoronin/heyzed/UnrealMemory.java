package com.pavelvoronin.heyzed;

import java.io.*;
import java.nio.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.lang.invoke.*;
import java.util.*;

/** Portable file-backed shared memory. One producer and one consumer per ring. */
final class UnrealMemory implements AutoCloseable {
    static final int SLOTS=128, SLOT=65536, HEADER=4096;
    static final VarHandle LONG=MethodHandles.byteBufferViewVarHandle(long[].class,ByteOrder.LITTLE_ENDIAN);
    final Path root;
    final Map<String,Ring> rings=new HashMap<>();
    static final class Ring implements AutoCloseable {
        final FileChannel channel;
        final MappedByteBuffer map;
        Ring(Path path,boolean create) throws IOException {
            channel=FileChannel.open(path,StandardOpenOption.CREATE,StandardOpenOption.READ,StandardOpenOption.WRITE);
            if(create){channel.truncate(0);channel.position(HEADER+(long)SLOTS*SLOT-1);channel.write(ByteBuffer.wrap(new byte[]{0}));}
            map=channel.map(FileChannel.MapMode.READ_WRITE,0,HEADER+(long)SLOTS*SLOT);map.order(ByteOrder.LITTLE_ENDIAN);
            if(create){map.putInt(128,0x314d4a50);map.putInt(132,1);map.putInt(136,SLOTS);map.putInt(140,SLOT);}
            if(map.getInt(128)!=0x314d4a50||map.getInt(132)!=1||map.getInt(136)!=SLOTS||map.getInt(140)!=SLOT)throw new IOException("Incompatible shared ring");
        }
        boolean canOffer(){return (long)LONG.getAcquire(map,0)-(long)LONG.getAcquire(map,64)<SLOTS;}
        boolean offer(String text) {return canOffer()&&offer(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
        boolean offer(byte[] data) {
            long head=(long)LONG.getAcquire(map,0),tail=(long)LONG.getAcquire(map,64);
            if(head-tail>=SLOTS)return false;
            if(data.length>SLOT-16)throw new IllegalArgumentException("Shared block exceeds slot");
            int offset=HEADER+(int)(head%SLOTS)*SLOT;
            map.putInt(offset,data.length);map.putLong(offset+8,System.currentTimeMillis());
            ByteBuffer view=map.duplicate();view.position(offset+16);view.put(data);
            LONG.setRelease(map,0,head+1);return true;
        }
        String poll() {
            long tail=(long)LONG.getAcquire(map,64),head=(long)LONG.getAcquire(map,0);
            if(tail>=head)return null;
            int offset=HEADER+(int)(tail%SLOTS)*SLOT,length=map.getInt(offset);
            if(length<0||length>SLOT-16)throw new IllegalStateException("Invalid shared block");
            byte[] data=new byte[length];ByteBuffer view=map.duplicate();view.position(offset+16);view.get(data);
            LONG.setRelease(map,64,tail+1);
            return new String(data,java.nio.charset.StandardCharsets.UTF_8);
        }
        public void close()throws IOException{channel.close();}
    }
    UnrealMemory(long epoch) throws IOException {
        root=Path.of(System.getProperty("heyzed.unreal.memory",System.getProperty("user.home")+"/.heyzed/bridge"));
        Path generation=root.resolve(Long.toString(epoch));Files.createDirectories(generation);
        for(String name:List.of("motion","events","world","input","commands","entities","poses"))rings.put(name,new Ring(generation.resolve(name+".ring"),true));
        Path pending=root.resolve("current.tmp");Files.writeString(pending,Long.toString(epoch));
        Files.move(pending,root.resolve("current"),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
    }
    boolean canOffer(String name){return rings.get(name).canOffer();}
    boolean offer(String name,String value){return rings.get(name).offer(value);}
    boolean offer(String name,byte[] value){return rings.get(name).offer(value);}
    String poll(String name){return rings.get(name).poll();}
    public void close()throws IOException{for(Ring ring:rings.values())ring.close();}
}
