package com.pavelvoronin.heyzed;

import zombie.iso.Vector2;

public final class UnrealHeyZedRegression {
    public static void main(String[] arguments) {
        for(float yaw:new float[]{-135,0,90,180,-90}) {
            for(float[] keys:new float[][]{{0,-1},{0,1},{-1,0},{1,0},{0,0}}) {
                Vector2 vector=new Vector2(keys[0]*1.95f,keys[1]*1.95f);
                float length=vector.getLength();
                UnrealHeyZed.cameraSpace(vector,yaw);
                if(Math.abs(vector.getLength()-length)>0.0001f)throw new AssertionError("Native movement speed changed");
                double worldX=(vector.x+vector.y)*Math.sqrt(.5),worldY=(vector.y-vector.x)*Math.sqrt(.5);
                double angle=Math.toRadians(yaw),forward=-keys[1],right=keys[0];
                double expectedX=(Math.cos(angle)*forward-Math.sin(angle)*right)*1.95;
                double expectedY=(Math.sin(angle)*forward+Math.cos(angle)*right)*1.95;
                if(Math.hypot(worldX-expectedX,worldY-expectedY)>0.0001)throw new AssertionError("Input differs from Unreal camera axes at "+yaw);
            }
        }
        System.out.println("PASS: native camera-space input preserves direction and vanilla vector magnitude at five headings");
    }
}
