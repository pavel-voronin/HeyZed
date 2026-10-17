package com.pavelvoronin.heyzed;

import java.lang.reflect.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.concurrent.locks.ReentrantLock;
import me.zed_0xff.zombie_buddy.Patch;
import zombie.MapCollisionData;
import zombie.popman.ZombiePopulationRenderer;

/** Coordinate-only native population inspection; no UI buffers are submitted. */
public final class UnrealPopulationView {
    private static ZombiePopulationRenderer observer;
    private static Method nativeView,nativeOption;
    private static boolean moving,standing;
    private static boolean settingsKnown=true;
    private static final ReentrantLock optionsLock=new ReentrantLock();
    private static volatile Thread samplingThread;
    private static float[] output;
    private static int count,seen;
    private static float centerX,centerY;
    private static double rangeSquared;
    static int snapshot(float x,float y,double range,float[] destination)throws Exception {
        if(observer==null){
            String hash=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(System.getProperty("user.dir"),"PZPopMan64.dll"))));
            if(!hash.equals("5b2e3ef5d383c5e5e6fecf49afd514b196617925c1a0e3623bf0e7917a010ec6"))throw new IllegalStateException("Unreviewed native population callback ABI");
            observer=new ZombiePopulationRenderer();nativeView=ZombiePopulationRenderer.class.getDeclaredMethod("n_render",float.class,int.class,int.class,float.class,float.class,int.class,int.class);nativeView.setAccessible(true);nativeOption=ZombiePopulationRenderer.class.getDeclaredMethod("n_setDebugOption",String.class,String.class);nativeOption.setAccessible(true);
        }
        centerX=x;centerY=y;rangeSquared=range*range;output=destination;count=seen=0;
        int size=(int)Math.ceil(range*2);
        synchronized(MapCollisionData.instance.renderLock){
            optionsLock.lock();
            try{
                if(!settingsKnown)throw new IllegalStateException("Native population debug options are unknown");
                samplingThread=Thread.currentThread();
                try{nativeOption.invoke(observer,"Zombies.Standing","true");nativeOption.invoke(observer,"Zombies.Moving","true");nativeView.invoke(observer,1.f,0,0,x,y,size,size);}
                finally{try{nativeOption.invoke(observer,"Zombies.Standing",Boolean.toString(standing));}finally{nativeOption.invoke(observer,"Zombies.Moving",Boolean.toString(moving));}}
            }finally{samplingThread=null;output=null;optionsLock.unlock();}
        }
        return count;
    }
    static int seen(){return seen;}
    public static boolean captures(ZombiePopulationRenderer source){return source==observer&&Thread.currentThread()==samplingThread;}
    public static void beginOptions(){optionsLock.lock();}
    public static void optionsChanged(ZombiePopulationRenderer source,Throwable failure){try{if(failure!=null)settingsKnown=false;else if(source!=observer){standing=source.getBoolean("Zombies.Standing");moving=source.getBoolean("Zombies.Moving");}}finally{optionsLock.unlock();}}
    public static boolean point(ZombiePopulationRenderer source,float x,float y){
        if(!captures(source))return false;++seen;
        if(Float.isFinite(x)&&Float.isFinite(y)&&(x-centerX)*(x-centerX)+(y-centerY)*(y-centerY)<=rangeSquared&&count<output.length/2){output[count*2]=x;output[count*2+1]=y;++count;}
        return true;
    }
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderZombie",warmUp=true)
    public static class Zombie {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source,@Patch.Argument(0) float x,@Patch.Argument(1) float y){return UnrealPopulationView.point(source,x,y);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderRect",warmUp=true)
    public static class Rectangle {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="outlineRect",warmUp=true)
    public static class Outline {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderLine",warmUp=true)
    public static class Line {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderCircle",warmUp=true)
    public static class Circle {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderString",warmUp=true)
    public static class Text {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderCellInfo",warmUp=true)
    public static class Cell {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="renderVehicle",warmUp=true)
    public static class Vehicle {@Patch.OnEnter(skipOn=true) public static boolean enter(@Patch.This ZombiePopulationRenderer source){return UnrealPopulationView.captures(source);}}
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="load",warmUp=true)
    public static class OptionsLoaded {
        @Patch.OnEnter public static void enter(){UnrealPopulationView.beginOptions();}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.This ZombiePopulationRenderer source,@Patch.Thrown Throwable failure){UnrealPopulationView.optionsChanged(source,failure);}
    }
    @Patch(className="zombie.popman.ZombiePopulationRenderer",methodName="save",warmUp=true)
    public static class OptionsSaved {
        @Patch.OnEnter public static void enter(){UnrealPopulationView.beginOptions();}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.This ZombiePopulationRenderer source,@Patch.Thrown Throwable failure){UnrealPopulationView.optionsChanged(source,failure);}
    }
    private UnrealPopulationView(){}
}
