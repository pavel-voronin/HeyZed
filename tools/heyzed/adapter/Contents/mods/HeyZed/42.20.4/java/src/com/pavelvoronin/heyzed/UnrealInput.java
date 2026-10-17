package com.pavelvoronin.heyzed;

import java.util.ArrayDeque;
import java.util.BitSet;
import me.zed_0xff.zombie_buddy.Patch;

/** Remote transitions enter the ordinary PZ keyboard/mouse state updates. */
public final class UnrealInput {
    private record Transition(long session,long sequence,int code,boolean down) {}
    private static final ArrayDeque<Transition> pending=new ArrayDeque<>();
    private static final UnrealInputState keys=new UnrealInputState();
    private static final BitSet seen=new BitSet(10002);
    private static long receivedSession,receivedSequence;
    private static boolean active;

    static synchronized void accept(String text){
        try{
            String[] fields=text.split(" ");
            if(fields.length!=5)return;
            long source=Long.parseLong(fields[1]),sequence=Long.parseLong(fields[2]);
            int code=Integer.parseInt(fields[3]),down=Integer.parseInt(fields[4]);
            if(source==0||sequence<=0||(code!=-1&&(code<32||code>348)&&code!=10000&&code!=10001)||(down!=0&&down!=1))return;
            if(source!=UnrealHeyZed.keyboardOwner())return;
            if(source==receivedSession&&sequence<=receivedSequence)return;
            if(source!=receivedSession){pending.clear();receivedSession=source;}
            receivedSequence=sequence;
            if(pending.size()>=256){pending.clear();pending.add(new Transition(source,sequence,-1,false));}
            pending.add(new Transition(source,sequence,code,down==1));
        }catch(NumberFormatException ignored){}
    }
    public static synchronized void update(){
        long current=UnrealHeyZed.keyboardSession();
        active=current!=0;
        long owner=UnrealHeyZed.keyboardOwner();keys.lease(owner,active);
        if(!active){if(owner==0)pending.clear();return;}
        seen.clear();
        for(int count=0;count<256&&!pending.isEmpty();count++){
            Transition key=pending.peek();
            if(key.session!=current){pending.remove();continue;}
            // A tap must survive at least one native input update before its release.
            if(key.code>=0&&seen.get(key.code))break;
            pending.remove();
            if(key.code==-1){keys.clear();continue;}
            seen.set(key.code);keys.set(key.code,key.down);
        }
    }
    static synchronized void reset(){pending.clear();keys.lease(0,false);active=false;receivedSession=receivedSequence=0;}

    public static boolean keyboard(int key,boolean physical){return active?keys.isDown(Math.max(0,org.lwjglx.input.KeyCodes.toGlfwKey(key))):physical;}
    public static boolean mouse(int button,boolean physical){return active?button>=0&&button<=1&&keys.isDown(10000+button):physical;}

    @Patch(className="zombie.input.GameKeyboard",methodName="update",warmUp=true)
    public static class Update {@Patch.OnEnter public static void enter(){UnrealInput.update();}}
    @Patch(className="zombie.input.KeyboardState",methodName="isKeyDown",warmUp=true)
    public static class Keyboard {
        @Patch.OnExit public static void exit(@Patch.Argument(0) int key,@Patch.Return(readOnly=false) boolean value){
            value=UnrealInput.keyboard(key,value);
        }
    }
    @Patch(className="zombie.input.MouseState",methodName="isButtonDown",warmUp=true)
    public static class Mouse {
        @Patch.OnExit public static void exit(@Patch.Argument(0) int button,@Patch.Return(readOnly=false) boolean value){
            value=UnrealInput.mouse(button,value);
        }
    }
    private UnrealInput(){}
}
