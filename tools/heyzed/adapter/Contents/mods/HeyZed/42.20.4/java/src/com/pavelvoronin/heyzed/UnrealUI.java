package com.pavelvoronin.heyzed;

import java.nio.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.lang.invoke.*;
import me.zed_0xff.zombie_buddy.Patch;
import org.lwjgl.opengl.*;
import org.lwjgl.system.MemoryStack;
import zombie.core.Core;
import zombie.ui.UIManager;

public final class UnrealUI {
    private static final VarHandle LONG=MethodHandles.byteBufferViewVarHandle(long[].class,ByteOrder.LITTLE_ENDIAN);
    private static MappedByteBuffer control;
    private static FileChannel channel;
    private static long nextOpen,generation,frames,failedGeneration;
    private static final int[] textures=new int[3],memories=new int[3],semaphores=new int[3],fbos=new int[3];
    private static int width,height,readFbo;
    private static boolean failed,configured;
    private static int mouseX,mouseY,wheel;
    private static final java.util.BitSet buttons=new java.util.BitSet(8);
    private static long load(int offset){return (long)LONG.getAcquire(control,offset);}
    private static void store(int offset,long value){LONG.setRelease(control,offset,value);}
    private static boolean connected(){
        if(Integer.getInteger("heyzed.unreal.port",0)==0||!System.getProperty("os.name","").startsWith("Windows"))return false;
        if(control==null&&System.currentTimeMillis()>=nextOpen){
            nextOpen=System.currentTimeMillis()+1000;
            try{
                Path root=Path.of(System.getProperty("heyzed.unreal.memory",System.getProperty("user.home")+"/.heyzed/bridge"));
                Path path=root.resolve("native-ui.control");
                if(!Files.isRegularFile(path)||Files.size(path)!=8192)return false;
                channel=FileChannel.open(path,StandardOpenOption.READ,StandardOpenOption.WRITE);
                control=channel.map(FileChannel.MapMode.READ_WRITE,0,8192);control.order(ByteOrder.LITTLE_ENDIAN);
            }catch(Exception error){System.err.println("[heyzed] Native UI control: "+error);return false;}
        }
        return control!=null&&control.getInt(0)==0x31495550&&control.getInt(4)==1&&load(16)!=0&&Math.abs(System.currentTimeMillis()-load(32))<3000;
    }
    public static boolean hasConsumer(){return connected();}
    public static void prepare(){
        if(!zombie.GameWindow.isIngameState()||zombie.Lua.LuaManager.env==null)return;
        if(!connected())return;
        if(failed&&load(16)!=failedGeneration)failed=false;
        if(failed)return;
        int requestedWidth=control.getInt(24),requestedHeight=control.getInt(28);
        if(requestedWidth<320||requestedHeight<240||requestedWidth>7680||requestedHeight>4320)return;
        Core core=Core.getInstance();
        if(core.getScreenWidth()!=requestedWidth||core.getScreenHeight()!=requestedHeight){
            zombie.core.opengl.RenderThread.invokeOnRenderContext(()->org.lwjgl.glfw.GLFW.glfwSetWindowSize(org.lwjglx.opengl.Display.getWindow(),requestedWidth,requestedHeight));
            core.setScreenSize(requestedWidth,requestedHeight);
            System.out.println("[heyzed] Native UI viewport="+requestedWidth+"x"+requestedHeight);
        }
        if(!configured){zombie.core.opengl.RenderThread.invokeOnRenderContext(()->core.setOptionUIFBO(true));core.setOptionUIRenderFPS(60);configured=true;}
        control.putInt(40,core.getScreenWidth());control.putInt(44,core.getScreenHeight());store(72,System.currentTimeMillis());
    }
    private static boolean inputActive(){return connected()&&control.getInt(88)==1&&!failed;}
    public static int pointerX(int physical){return inputActive()?mouseX:physical;}
    public static int pointerY(int physical){return inputActive()?Core.getInstance().getScreenHeight()-mouseY-1:physical;}
    public static int pointerWheel(int physical){return inputActive()?wheel:physical;}
    public static boolean pointerButton(int button,boolean physical){return inputActive()?button>=0&&buttons.get(button):physical;}
    public static void input(){
        wheel=0;
        if(!inputActive()){buttons.clear();if(control!=null)store(136,load(128));return;}
        mouseX=control.getInt(80);mouseY=control.getInt(84);java.util.BitSet seen=new java.util.BitSet(8);
        long tail=load(136),head=load(128);
        while(tail<head){
            int offset=1024+(int)(tail%128)*16,type=control.getInt(offset),value=control.getInt(offset+4);
            int button=value&255;
            if(type==1&&seen.get(button))break;
            mouseX=control.getInt(offset+8);mouseY=control.getInt(offset+12);
            if(type==1&&button<8){buttons.set(button,(value&256)!=0);seen.set(button);}
            if(type==2)wheel+=value;
            store(136,++tail);
        }
    }
    public static void rectangles(){
        if(!connected()||failed)return;
        long sequence=load(104);store(104,sequence+1);int count=0;
        for(var element:UIManager.getUI()){
            if(count>=96)break;
            if(element==UIManager.getToolTip()||!element.isVisible()||element.getWidth()<=0||element.getHeight()<=0)continue;
            int offset=4096+count*16;
            if(element.isCapture()){control.putInt(offset,0);control.putInt(offset+4,0);control.putInt(offset+8,Core.getInstance().getScreenWidth());control.putInt(offset+12,Core.getInstance().getScreenHeight());++count;continue;}
            control.putInt(offset,(int)Math.floor(element.getX()));control.putInt(offset+4,(int)Math.floor(element.getY()));
            control.putInt(offset+8,(int)Math.ceil(element.getWidth()));control.putInt(offset+12,(int)Math.ceil(element.getMaxDrawHeight()<0?element.getHeight():Math.min(element.getHeight(),element.getMaxDrawHeight())));++count;
        }
        control.putInt(96,count);store(104,sequence+2);
    }
    private static String name(long revision,int slot,String suffix){return "Local\\heyzed-ui-"+control.getInt(8)+"-"+Long.toUnsignedString(revision)+"-"+slot+suffix;}
    private static void check(String phase){int error=GL11.glGetError();if(error!=GL11.GL_NO_ERROR)throw new IllegalStateException(phase+" GL="+Integer.toHexString(error));}
    private static void release(){
        for(int i=0;i<3;i++){
            if(fbos[i]!=0)GL30.glDeleteFramebuffers(fbos[i]);if(textures[i]!=0)GL11.glDeleteTextures(textures[i]);
            if(semaphores[i]!=0)EXTSemaphore.glDeleteSemaphoresEXT(semaphores[i]);if(memories[i]!=0)EXTMemoryObject.glDeleteMemoryObjectsEXT(memories[i]);
            fbos[i]=textures[i]=semaphores[i]=memories[i]=0;
        }
        if(readFbo!=0)GL30.glDeleteFramebuffers(readFbo);readFbo=0;generation=0;
    }
    private static void open(long revision){
        release();var caps=GL.getCapabilities();
        if(!caps.GL_EXT_memory_object_win32||!caps.GL_EXT_semaphore_win32)throw new IllegalStateException("External GPU memory/semaphore extensions unavailable");
        width=control.getInt(24);height=control.getInt(28);
        try(MemoryStack stack=MemoryStack.stackPush()){
            for(int i=0;i<3;i++){
                memories[i]=EXTMemoryObject.glCreateMemoryObjectsEXT();
                EXTMemoryObject.glMemoryObjectParameterivEXT(memories[i],0x9581,stack.ints(GL11.GL_TRUE));
                EXTMemoryObjectWin32.glImportMemoryWin32NameEXT(memories[i],0,0x958A,org.lwjgl.system.MemoryUtil.memAddress(stack.UTF16(name(revision,i,"-texture"))));check("import texture");
                textures[i]=GL11.glGenTextures();GL11.glBindTexture(GL11.GL_TEXTURE_2D,textures[i]);
                GL11.glTexParameteri(GL11.GL_TEXTURE_2D,GL11.GL_TEXTURE_MIN_FILTER,GL11.GL_NEAREST);GL11.glTexParameteri(GL11.GL_TEXTURE_2D,GL11.GL_TEXTURE_MAG_FILTER,GL11.GL_NEAREST);
                EXTMemoryObject.glTexStorageMem2DEXT(GL11.GL_TEXTURE_2D,1,GL11.GL_RGBA8,width,height,memories[i],0);check("shared storage");
                fbos[i]=GL30.glGenFramebuffers();GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,fbos[i]);
                GL30.glFramebufferTexture2D(GL30.GL_DRAW_FRAMEBUFFER,GL30.GL_COLOR_ATTACHMENT0,GL11.GL_TEXTURE_2D,textures[i],0);
                if(GL30.glCheckFramebufferStatus(GL30.GL_DRAW_FRAMEBUFFER)!=GL30.GL_FRAMEBUFFER_COMPLETE)throw new IllegalStateException("Shared UI framebuffer incomplete");
                semaphores[i]=EXTSemaphore.glGenSemaphoresEXT();
                EXTSemaphoreWin32.glImportSemaphoreWin32NameEXT(semaphores[i],0x9594,org.lwjgl.system.MemoryUtil.memAddress(stack.UTF16(name(revision,i,"-fence"))));check("import fence");
            }
        }
        for(int i=0;i<3;i++){store(256+i*64,0);store(280+i*64,revision);}
        readFbo=GL30.glGenFramebuffers();generation=revision;
        System.out.println("[heyzed] Native UI GPU generation="+generation+" size="+width+"x"+height+" GL="+GL11.glGetString(GL11.GL_RENDERER));
    }
    public static void publish(){
        if(!connected()){if(generation!=0)release();return;}
        captureBackend();
        if(failed||UIManager.uiFbo==null||!UIManager.uiTextureContentsValid)return;
        int oldRead=GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING),oldDraw=GL11.glGetInteger(GL30.GL_DRAW_FRAMEBUFFER_BINDING);
        int oldTexture=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);boolean scissor=GL11.glIsEnabled(GL11.GL_SCISSOR_TEST);
        try{
            long revision=load(16);if(revision!=generation)open(revision);
            var source=UIManager.uiFbo.getTexture();
            if(source.getWidth()!=width||source.getHeight()!=height||control.getInt(40)!=width||control.getInt(44)!=height)return;
            for(int i=0;i<3;i++){
                int offset=256+i*64;long ready=load(offset),ack=load(offset+8);if(ready!=ack)continue;
                long value=ready==0?1:ready+2;
                try(MemoryStack stack=MemoryStack.stackPush()){
                    if(ack!=0){EXTSemaphore.glSemaphoreParameterui64EXT(semaphores[i],0x9595,ack+1);EXTSemaphore.glWaitSemaphoreEXT(semaphores[i],stack.ints(),stack.ints(textures[i]),stack.ints(0x958D));}
                    GL11.glDisable(GL11.GL_SCISSOR_TEST);GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,readFbo);
                    GL30.glFramebufferTexture2D(GL30.GL_READ_FRAMEBUFFER,GL30.GL_COLOR_ATTACHMENT0,GL11.GL_TEXTURE_2D,source.getID(),0);
                    GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,fbos[i]);
                    GL30.glBlitFramebuffer(0,0,width,height,0,height,width,0,GL11.GL_COLOR_BUFFER_BIT,GL11.GL_NEAREST);
                    EXTSemaphore.glSemaphoreParameterui64EXT(semaphores[i],0x9595,value);
                    EXTSemaphore.glSignalSemaphoreEXT(semaphores[i],stack.ints(),stack.ints(textures[i]),stack.ints(0x958D));GL11.glFlush();check("publish UI framebuffer");
                }
                if(load(16)!=generation)return;
                control.putLong(offset+16,++frames);store(offset+24,generation);store(offset,value);store(48,frames);return;
            }
        }catch(Throwable error){failed=true;failedGeneration=load(16);control.putInt(56,1);System.err.println("[heyzed] Native UI GPU failed: "+error);error.printStackTrace();}
        finally{
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,oldRead);GL30.glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,oldDraw);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,oldTexture);if(scissor)GL11.glEnable(GL11.GL_SCISSOR_TEST);else GL11.glDisable(GL11.GL_SCISSOR_TEST);
        }
    }
    private static void captureBackend(){
        int request=control.getInt(120);if(request<=0||request==Math.abs(control.getInt(124)))return;
        String output=System.getProperty("modstudio.scenario.output","");if(output.isEmpty())return;
        int read=GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING),buffer=GL11.glGetInteger(GL11.GL_READ_BUFFER),pack=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT);
        try{
            Path folder=Path.of(output,"unreal-ui");Files.createDirectories(folder);
            int w=Core.getInstance().getScreenWidth(),h=Core.getInstance().getScreenHeight();
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,0);GL11.glReadBuffer(GL11.GL_FRONT);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);
            try(var image=zombie.core.utils.NativeImage.allocate(w,h);var stream=Files.newOutputStream(folder.resolve("backend-window.png"))){
                GL11.glReadPixels(0,0,w,h,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,image.pixels());
                if(!image.writePng(stream,true))throw new IllegalStateException("Backend capture write failed");
            }
            control.putInt(124,request);
        }catch(Throwable error){control.putInt(124,-request);System.err.println("[heyzed] Native backend capture: "+error);}
        finally{GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,read);GL11.glReadBuffer(buffer);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,pack);}
    }
    @Patch(className="zombie.core.Core",methodName="StartFrameUI",warmUp=true)
    public static class Prepare{@Patch.OnEnter public static void enter(){UnrealUI.prepare();}}
    @Patch(className="zombie.core.SpriteRenderer",methodName="postRender",warmUp=true)
    public static class Publish{@Patch.OnExit public static void exit(){UnrealUI.publish();}}
    @Patch(className="zombie.input.Mouse",methodName="update",warmUp=true)
    public static class Input{@Patch.OnEnter public static void enter(){UnrealUI.input();}}
    @Patch(className="zombie.ui.UIManager",methodName="update",warmUp=true)
    public static class Rectangles{@Patch.OnExit public static void exit(){UnrealUI.rectangles();}}
    @Patch(className="zombie.input.MouseState",methodName="getX",warmUp=true)
    public static class MouseX{@Patch.OnExit public static void exit(@Patch.Return(readOnly=false) int value){value=UnrealUI.pointerX(value);}}
    @Patch(className="zombie.input.MouseState",methodName="getY",warmUp=true)
    public static class MouseY{@Patch.OnExit public static void exit(@Patch.Return(readOnly=false) int value){value=UnrealUI.pointerY(value);}}
    @Patch(className="zombie.input.MouseState",methodName="getDWheel",warmUp=true)
    public static class Wheel{@Patch.OnExit public static void exit(@Patch.Return(readOnly=false) int value){value=UnrealUI.pointerWheel(value);}}
    @Patch(className="zombie.input.MouseState",methodName="isButtonDown",warmUp=true)
    public static class MouseButton{@Patch.OnExit public static void exit(@Patch.Argument(0) int button,@Patch.Return(readOnly=false) boolean value){value=UnrealUI.pointerButton(button,value);}}
    private UnrealUI(){}
}
