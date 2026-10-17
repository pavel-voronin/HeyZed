package com.pavelvoronin.heyzed;

final class UnrealLoadingTransition {
    private Object pending;
    boolean pending(Object state){return pending==state;}
    boolean complete(Object state,boolean continued,boolean ready){
        if(pending==state || continued){
            pending=state;
            return ready;
        }
        return false;
    }
    void reset(){pending=null;}
}
