package com.pavelvoronin.heyzed;

import zombie.characters.IsoPlayer;
import zombie.iso.IsoGridSquare;
import zombie.iso.IsoRoomLight;
import zombie.iso.objects.IsoLightSwitch;
import zombie.iso.sprite.IsoSpriteManager;

final class UnrealLightingShowcase {
    private static boolean prepared;
    static void prepare(IsoPlayer player){
        if(prepared||!Boolean.getBoolean("heyzed.bridge.lightingShowcase")||!zombie.core.Core.getInstance().isNoSave())return;
        IsoRoomLight chosen=null;double best=Double.MAX_VALUE;boolean south=false;
        for(var light:player.getCell().roomLights){
            if(light.z!=0||light.width<4||light.height<3||light.room==null)continue;
            double distance=Math.hypot(light.x+light.width*.5-player.getX(),light.y+light.height*.5-player.getY());
            if(distance>=best||distance>=40)continue;
            var east=player.getCell().getGridSquare(light.x+light.width,light.y+light.height/2,0);
            var southSquare=player.getCell().getGridSquare(light.x+light.width/2,light.y+light.height,0);
            boolean eastOutside=east!=null&&east.getRoom()==null&&east.getFloor()!=null;
            boolean southOutside=southSquare!=null&&southSquare.getRoom()==null&&southSquare.getFloor()!=null;
            if(!eastOutside&&!southOutside)continue;
            chosen=light;best=distance;south=!eastOutside;
        }
        if(chosen==null)return;
        int x=chosen.x+chosen.width/2,y=chosen.y+chosen.height/2;
        IsoGridSquare inside=player.getCell().getGridSquare(x,y,0);
        IsoGridSquare sign=player.getCell().getGridSquare(south?x:chosen.x+chosen.width,south?chosen.y+chosen.height:y,0);
        IsoGridSquare street=null;
        for(int distance=3;distance<=8&&street==null;++distance){
            var candidate=player.getCell().getGridSquare(south?x+2:chosen.x+chosen.width+distance,south?chosen.y+chosen.height+distance:y+2,0);
            if(candidate!=null&&candidate.getRoom()==null&&candidate.getFloor()!=null&&candidate.isFree(false))street=candidate;
        }
        if(inside==null||inside.getRoom()!=chosen.room||sign==null||street==null)return;
        // This opt-in fixture exists only in a fresh manual NoSave world.
        prepared=true;
        var support=new zombie.iso.IsoObject(player.getCell(),inside,"furniture_tables_high_01_0");
        inside.AddTileObject(support);
        if(chosen.room.lightSwitches.isEmpty()){
            var switchSquare=player.getCell().getGridSquare(chosen.x+1,chosen.y,0);
            if(switchSquare!=null&&switchSquare.getRoom()==chosen.room)
                switchSquare.AddSpecialObject(new IsoLightSwitch(player.getCell(),switchSquare,IsoSpriteManager.instance.getSprite("lighting_indoor_01_0"),-1));
        }
        IsoLightSwitch table=add(inside,"lighting_indoor_01_11");
        table.getModData().rawset("heyzedPreviewTable",Boolean.TRUE);
        add(sign,south?"advertising_02_1":"advertising_02_0");
        add(street,"lighting_outdoor_01_11");
        for(Object object:chosen.room.lightSwitches.toArray())if(object instanceof IsoLightSwitch lamp)lamp.setActive(true);
        var torch=player.getInventory().AddItem("Base.HandTorch");
        if(torch!=null){player.setSecondaryHandItem(torch);torch.setActivated(true);}
        var spawn=player.getCell().getFreeTile(chosen.room.def);
        if(spawn!=null)player.teleportTo(spawn.x,spawn.y,spawn.z);
        prepared=true;
        System.out.println("[heyzed] Manual lighting interior prepared at "+x+","+y+"; native switches, table lamp, outdoor sign and streetlamp; handheld torch equipped.");
    }
    private static IsoLightSwitch add(IsoGridSquare square,String sprite){
        var lamp=new IsoLightSwitch(square.getCell(),square,IsoSpriteManager.instance.getSprite(sprite),-1);
        lamp.createLightSource();square.AddSpecialObject(lamp);
        lamp.setActive(true);return lamp;
    }
    private UnrealLightingShowcase(){}
}
