package com.pavelvoronin.heyzed;

import me.zed_0xff.zombie_buddy.Patch;

public final class UnrealStartup {
    private static volatile boolean loading=true;
    private static volatile long readySession,readyEpoch,readyAt;
    private static volatile int readyLevel;
    private static volatile String stage="loading_world";
    public static void stage(String value){stage=value;}
    public static String status(){return new org.json.JSONObject().put("type","startup").put("stage",stage).toString();}
    public static void accept(String text){
        try{
            String[] f=text.split(" ");
            if(f.length!=4||!f[0].equals("L"))return;
            readySession=Long.parseLong(f[1]);readyEpoch=Long.parseLong(f[2]);
            int level=Integer.parseInt(f[3]);if(level<0||level>2)return;
            readyLevel=level;readyAt=level>0?System.nanoTime():0;
        }catch(NumberFormatException ignored){}
    }
    private static final UnrealLoadingTransition transition=new UnrealLoadingTransition();
    private static java.lang.reflect.Field forceDone;
    private static volatile int resumeSpeed=-1;
    public static boolean snapshotAllowed(){
        Thread loader=zombie.gameStates.GameLoadingState.loader;
        return loader!=null&&!loader.isAlive()&&!zombie.iso.WorldStreamer.instance.isBusy()
                &&!zombie.core.skinnedmodel.ModelManager.instance.isLoadingAnimations();
    }
    public static boolean waiting(){return enabled()&&(loading||resumeSpeed>=0);}
    public static boolean entered(){return !loading;}
    public static void poll(){
        if(!enabled()||loading)return;
        var controls=zombie.ui.UIManager.getSpeedControls();
        if(controls==null)return;
        if(!ready(2)){
            if(resumeSpeed<0)resumeSpeed=controls.getCurrentGameSpeed();
            if(controls.getCurrentGameSpeed()!=0)controls.SetCurrentGameSpeed(0);
        }
        else if(ready(2)&&resumeSpeed>=0){if(controls.getCurrentGameSpeed()==0)controls.SetCurrentGameSpeed(resumeSpeed);resumeSpeed=-1;}
    }
    private static boolean enabled(){return Integer.getInteger("heyzed.unreal.port",0)!=0&&!zombie.network.GameClient.client;}
    private static boolean ready(int level){return readyLevel>=level&&readyAt!=0&&System.nanoTime()-readyAt<2_000_000_000L&&UnrealHeyZed.startupPeer(readySession,readyEpoch);}
    public static boolean beforeLoading(zombie.gameStates.GameLoadingState state){
        if(!enabled())return false;
        if(transition.pending(state))return true;
        if(!ready(1))return false;
        try{
            if(forceDone==null){forceDone=zombie.gameStates.GameLoadingState.class.getDeclaredField("forceDone");forceDone.setAccessible(true);}
            forceDone.setBoolean(state,true);
        }catch(ReflectiveOperationException error){throw new IllegalStateException("heyzed native loading handoff unavailable",error);}
        return false;
    }
    public static zombie.gameStates.GameStateMachine.StateAction afterLoading(zombie.gameStates.GameLoadingState state,zombie.gameStates.GameStateMachine.StateAction result){
        if(!enabled())return result;
        if(transition.pending(state)||result==zombie.gameStates.GameStateMachine.StateAction.Continue)
            return transition.complete(state,result==zombie.gameStates.GameStateMachine.StateAction.Continue,ready(1))
                    ?zombie.gameStates.GameStateMachine.StateAction.Continue:zombie.gameStates.GameStateMachine.StateAction.Remain;
        return result;
    }
    public static void reset(){transition.reset();readyAt=0;loading=true;resumeSpeed=-1;stage="loading_world";}
    public static void started(){if(enabled()){loading=false;poll();System.out.println("[heyzed] Native gameplay initialized; first simulation tick awaits final Unreal readiness");}}
    @Patch(className="zombie.gameStates.GameLoadingState",methodName="update",warmUp=true)
    public static class LoadingGate {
        @Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This zombie.gameStates.GameLoadingState state){return UnrealStartup.beforeLoading(state);}
        @Patch.OnExit public static void exit(@Patch.This zombie.gameStates.GameLoadingState state,@Patch.Return(readOnly=false) zombie.gameStates.GameStateMachine.StateAction result){result=UnrealStartup.afterLoading(state,result);}
    }
    @Patch(className="zombie.gameStates.IngameState",methodName="enter",warmUp=true)
    public static class Started {@Patch.OnExit public static void exit(){UnrealStartup.started();}}
    private UnrealStartup(){}
}
