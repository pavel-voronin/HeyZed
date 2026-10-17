package com.pavelvoronin.heyzed;

import java.util.BitSet;

final class UnrealInputState {
    private final BitSet desired=new BitSet(10002),effective=new BitSet(10002);
    private long owner;
    private boolean fresh;
    void lease(long source,boolean valid){
        if(source!=owner){desired.clear();owner=source;}
        fresh=source!=0&&valid;effective.clear();
        if(fresh)effective.or(desired);
    }
    void clear(){desired.clear();effective.clear();}
    void set(int code,boolean down){desired.set(code,down);if(fresh)effective.set(code,down);}
    boolean isDown(int code){return effective.get(code);}
}
