package com.pavelvoronin.heyzed;

import me.zed_0xff.zombie_buddy.Patch;

public final class UnrealSimulationResources {
    private static zombie.iso.IsoWorld preparedWorld;

    public static void prepare(zombie.iso.IsoWorld world) {
        if(Integer.getInteger("heyzed.unreal.port",0)==0 || world==preparedWorld
                || zombie.Lua.LuaManager.env==null
                || !(zombie.Lua.LuaManager.env.rawget("AnimalDefinitions") instanceof se.krka.kahlua.vm.KahluaTable))return;
        UnrealStartup.stage("preparing_resources");
        long started=System.nanoTime();
        java.util.Set<String> names=new java.util.LinkedHashSet<>();
        names.add("player");names.add("zombie");
        for(var definition:zombie.characters.animals.AnimalDefinitions.getAnimalDefsArray())
            if(definition.animset!=null&&!definition.animset.isBlank())names.add(definition.animset);
        for(String name:names){
            zombie.core.skinnedmodel.advancedanimation.AnimationSet.GetAnimationSet(name,false);
            zombie.characters.action.ActionGroup.getActionGroup(name);
        }
        preparedWorld=world;
        UnrealStartup.stage("loading_world");
        System.out.println("[heyzed] Native simulation resources prepared: "+names.size()+" animation/action sets in "+((System.nanoTime()-started)/1_000_000)+" ms");
    }

    @Patch(className="zombie.iso.IsoWorld",methodName="init",warmUp=true)
    public static class WorldPrepared {
        @Patch.OnExit public static void exit(@Patch.This zombie.iso.IsoWorld world){UnrealSimulationResources.prepare(world);}
    }

    private UnrealSimulationResources(){}
}