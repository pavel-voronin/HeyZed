package com.pavelvoronin.heyzed;

import me.zed_0xff.zombie_buddy.Patch;
import zombie.iso.IsoObject;

public final class UnrealConstructionChanges {
    @Patch(className="zombie.iso.IsoObject",methodName="setAttachedAnimSprite",warmUp=true)
    public static class Replace {
        @Patch.OnExit public static void exit(@Patch.This IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}
    }
    @Patch(className="zombie.iso.IsoObject",methodName="addAttachedAnimSpriteInstance",warmUp=true)
    public static class Add {
        @Patch.OnExit public static void exit(@Patch.This IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}
    }
    @Patch(className="zombie.iso.IsoObject",methodName="RemoveAttachedAnims",warmUp=true)
    public static class Clear {
        @Patch.OnExit public static void exit(@Patch.This IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}
    }
    @Patch(className="zombie.iso.IsoObject",methodName="RemoveAttachedAnim",warmUp=true)
    public static class Remove {
        @Patch.OnExit public static void exit(@Patch.This IsoObject object){UnrealHeyZed.dirtySquare(object.getSquare());}
    }
    private UnrealConstructionChanges(){}
}
