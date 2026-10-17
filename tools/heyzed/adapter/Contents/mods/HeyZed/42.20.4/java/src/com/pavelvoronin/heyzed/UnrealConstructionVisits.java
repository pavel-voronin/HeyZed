package com.pavelvoronin.heyzed;

import java.util.*;

final class UnrealConstructionVisits {
    private static final class Entry {
        final Object owner;
        final int load;
        final Set<Long> cells=new HashSet<>();
        Entry(Object owner,int load){this.owner=owner;this.load=load;}
    }
    private final Map<Long,Entry> chunks=new HashMap<>();
    private static long chunk(int x,int y){return ((long)Math.floorDiv(x,8)<<32)|(Math.floorDiv(y,8)&0xffffffffL);}
    private static long cell(int x,int y,int z){return ((long)z<<6)|(Math.floorMod(y,8)<<3)|Math.floorMod(x,8);}
    boolean contains(int x,int y,int z,Object owner,int load){
        Entry entry=chunks.get(chunk(x,y));
        return entry!=null&&entry.owner==owner&&entry.load==load&&entry.cells.contains(cell(x,y,z));
    }
    void add(int x,int y,int z,Object owner,int load){
        long key=chunk(x,y);Entry entry=chunks.get(key);
        if(entry==null||entry.owner!=owner||entry.load!=load){entry=new Entry(owner,load);chunks.put(key,entry);}
        entry.cells.add(cell(x,y,z));
    }
    void loaded(int x,int y){chunks.remove(((long)x<<32)|(y&0xffffffffL));}
    void retain(int cx,int cy,int radius){chunks.keySet().removeIf(key->Math.abs((int)(key>>32)-cx)>radius||Math.abs((int)(long)key-cy)>radius);}
    void clear(){chunks.clear();}
}
