package com.pavelvoronin.heyzed;

public final class InputLeaseRegression {
    private static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    public static void main(String[] args){
        UnrealInputState state=new UnrealInputState();
        state.lease(1,true);state.set(87,true);require(state.isDown(87),"Accelerator press lost");
        state.lease(1,false);require(!state.isDown(87),"Stale input remained effective");
        state.lease(1,true);require(state.isDown(87),"Same-session held accelerator did not recover");
        state.lease(1,false);state.set(87,false);state.lease(1,true);require(!state.isDown(87),"Release during a stall was lost");
        state.set(87,true);state.lease(0,false);state.lease(1,true);require(!state.isDown(87),"Focus release restored an old key");
        state.set(87,true);state.lease(2,true);require(!state.isDown(87),"Another session inherited input");
        state.set(87,true);state.clear();state.lease(2,true);require(!state.isDown(87),"Explicit reset restored input");
        System.out.println("PASS: expired leases release effective keys, same-owner recovery retains held intent, explicit release and session changes clear it");
    }
}
