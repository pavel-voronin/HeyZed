package com.pavelvoronin.heyzed;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import me.zed_0xff.zombie_buddy.Patch;
import zombie.core.Core;
import zombie.util.ByteBufferPooledObject;

public final class UnrealNativeSave {
    private static final boolean enabled=Integer.getInteger("heyzed.unreal.port",0)!=0;

    public static void writePrepared(String file,ByteBuffer data)throws IOException{
        ByteBuffer view=data.slice();
        try(var stream=new FileOutputStream(file);var channel=stream.getChannel()){
            while(view.hasRemaining())channel.write(view);
        }
    }

    public static boolean writeNative(String file,ByteBufferPooledObject data)throws IOException{
        if(!enabled)return false;
        if(Core.getInstance().isNoSave())return true;
        UnrealNativeTiming.Work timing=UnrealNativeTiming.begin("native-save-write");
        try{writePrepared(file,data.slice());}
        finally{UnrealNativeTiming.finish(timing);}
        return true;
    }

    @Patch(className="zombie.iso.ChunkSaveWorker",methodName="writeBufferToDisk",warmUp=true)
    public static class PreparedWrite {
        @Patch.OnEnter(skipOn=true)
        public static boolean enter(@Patch.Argument(0) String file,@Patch.Argument(1) ByteBufferPooledObject data)throws IOException{
            return UnrealNativeSave.writeNative(file,data);
        }
    }
    private UnrealNativeSave(){}
}
