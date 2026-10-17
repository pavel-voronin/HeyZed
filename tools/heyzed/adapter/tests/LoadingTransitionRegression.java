package com.pavelvoronin.heyzed;

public final class LoadingTransitionRegression {
    private static void check(boolean ok,String text){if(!ok)throw new AssertionError(text);}
    public static void main(String[] args){
        var gate=new UnrealLoadingTransition();Object state=new Object();int nativeTransitions=0;
        check(!gate.complete(state,false,false),"ordinary native loading remains active");
        check(!gate.pending(state),"do not suppress preparation before native completion");
        for(int frame=0;frame<500;frame++){
            boolean completed=false;
            if(!gate.pending(state)){nativeTransitions++;completed=true;}
            check(!gate.complete(state,completed,false),"wait for frontend without entering gameplay");
        }
        check(nativeTransitions==1,"native transition and its sound execute exactly once");
        check(gate.pending(state)&&gate.complete(state,false,true),"release cached transition once frontend is ready");
        check(gate.pending(state)&&gate.complete(state,false,true),"repeat observation cannot replay native side effects");
        check(!gate.pending(new Object()),"different loading state is independent");
        gate.reset();check(!gate.pending(state),"reset admits a new native load");
        check(gate.complete(state,true,true),"ready frontend preserves immediate native transition");
        System.out.println("PASS: 500 readiness waits execute one native completion; native preparation, release and reload preserved");
    }
}
