package com.pavelvoronin.heyzed;

import jdk.jfr.Category;
import jdk.jfr.Event;
import jdk.jfr.EventType;
import jdk.jfr.Label;
import jdk.jfr.Name;
import jdk.jfr.StackTrace;
import jdk.jfr.Threshold;
import me.zed_0xff.zombie_buddy.Patch;

public final class UnrealNativeTiming {
    @Name("heyzed.NativeWork") @Label("Native backend work")
    @Category("heyzed") @Threshold("5 ms") @StackTrace(false)
    public static final class Work extends Event {
        public String stage;
    }

    private static final EventType workType=EventType.getEventType(Work.class);

    public static Work begin(String stage){
        if(Integer.getInteger("heyzed.unreal.port",0)==0||!workType.isEnabled())return null;
        Work event=new Work();
        if(!event.isEnabled())return null;
        event.stage=stage;event.begin();return event;
    }
    public static void finish(Work event){if(event!=null){event.end();event.commit();}}

    @Patch(className="zombie.iso.IsoMetaGrid",methodName="saveToBufferMap",warmUp=true)
    public static class MetaSave {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("metagrid-save");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.GameWindow",methodName="frameStep",warmUp=true)
    public static class Frame {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("frame-step");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.GameWindow",methodName="logic",warmUp=true)
    public static class Logic {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("logic");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.iso.IsoMetaGrid",methodName="savePart",warmUp=true)
    public static class MetaPart {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event,@Patch.Argument(1) int part){event=UnrealNativeTiming.begin(part==0?"metagrid-rooms-buildings":"metagrid-global-state");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.iso.IsoMetaGrid",methodName="saveZone",warmUp=true)
    public static class MetaZones {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("metagrid-zones");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.iso.IsoMetaGrid",methodName="saveAnimalZones",warmUp=true)
    public static class MetaAnimals {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("metagrid-animals");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.iso.IsoChunk",methodName="loadInMainThread",warmUp=true)
    public static class ChunkAdmission {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("chunk-admission");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.iso.IsoChunkMap",methodName="processAllLoadGridSquare",warmUp=true)
    public static class ChunkQueue {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("chunk-load-queue");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.inventory.ItemPickerJava",methodName="fillContainer",warmUp=true)
    public static class Loot {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("container-fill");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    @Patch(className="zombie.core.skinnedmodel.ModelManager",methodName="Add",warmUp=true)
    public static class ModelAdd {
        @Patch.OnEnter public static void enter(@Patch.Local("heyzedTiming") Work event){event=UnrealNativeTiming.begin("model-add");}
        @Patch.OnExit(onThrowable=Throwable.class) public static void exit(@Patch.Local("heyzedTiming") Work event){UnrealNativeTiming.finish(event);}
    }
    private UnrealNativeTiming(){}
}
