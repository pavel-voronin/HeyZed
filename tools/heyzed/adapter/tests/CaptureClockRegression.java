package com.pavelvoronin.heyzed;

public final class CaptureClockRegression {
    private static void check(boolean value,String name){if(!value)throw new AssertionError(name);}
    public static void main(String[] args){
        long period=50_000_000L,deadline=0;int captures=0,oldCaptures=0;long oldDeadline=0;
        for(int frame=0;frame<600;frame++){
            long now=1_000_000_000L+frame*1_000_000_000L/60+(frame%7)*200_000L;
            if(now>=deadline){captures++;deadline=UnrealCaptureClock.next(deadline,now,period);check(deadline>now,"no repeated capture in one step");}
            if(now>=oldDeadline){oldCaptures++;oldDeadline=now+period;}
        }
        check(captures==200,"20 Hz remains phase aligned on a 60 Hz native loop");
        check(oldCaptures<captures,"regression reproduces drift from scheduling after late frames");
        long resumed=UnrealCaptureClock.next(1_050_000_000L,3_071_000_000L,period);
        check(resumed==3_100_000_000L,"skip missed deadlines without a catch-up burst");
        check(UnrealCaptureClock.next(resumed,3_100_000_000L,period)==3_150_000_000L,"resume original cadence after a stall");
        System.out.println("PASS: 60 Hz native steps produce "+captures+" phase-aligned captures vs "+oldCaptures+" drifting captures; stall recovery bounded");
    }
}
