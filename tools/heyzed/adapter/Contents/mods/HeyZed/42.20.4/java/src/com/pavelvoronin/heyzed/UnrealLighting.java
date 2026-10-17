package com.pavelvoronin.heyzed;

import java.util.*;
import org.json.*;
import zombie.characters.IsoPlayer;
import zombie.iso.objects.IsoLightSwitch;
import zombie.iso.SpriteDetails.IsoFlagType;

/** Native gameplay facts only; Unreal owns photometry, fixtures and presentation. */
final class UnrealLighting {
    private static final Map<String,JSONObject> previous=new HashMap<>();
    private static final Map<String,Set<String>> squareLights=new HashMap<>();
    private static long nextSample,nextHeldSample,revision;
    private static final Map<String,java.lang.ref.WeakReference<IsoLightSwitch>> controls=new HashMap<>();
    private static final Set<String> held=new HashSet<>();
    private static JSONObject environment;
    static void reconcile(){previous.clear();squareLights.clear();controls.clear();held.clear();environment=null;nextSample=nextHeldSample=0;}
    static String identity(int x,int y,int z,String sprite){return x+":"+y+":"+z+":"+sprite.replace("_on_","_");}
    static void square(zombie.iso.IsoGridSquare square,long epoch){
        String prefix=square.x+":"+square.y+":"+square.z+":";
        Set<String> known=squareLights.get(prefix);if(known==null||known.isEmpty())return;
        Set<String> present=new HashSet<>();
        var objects=square.getObjects();
        for(int index=0;index<objects.size();index++)if(objects.get(index) instanceof IsoLightSwitch lamp&&lamp.getSprite()!=null)
            present.add(identity(square.x,square.y,square.z,lamp.getSprite().getName()));
        for(var iterator=known.iterator();iterator.hasNext();){String id=iterator.next();if(present.contains(id))continue;
            iterator.remove();controls.remove(id);
            previous.put(id,new JSONObject().put("id",id).put("deleted",true));UnrealObjects.events.add(new JSONObject().put("type","light").put("epoch",epoch).put("id",id)
                .put("revision",++revision).put("deleted",true).toString());
        }
        if(known.isEmpty())squareLights.remove(prefix);
    }
    static void capture(IsoPlayer player,long epoch){
        try{UnrealLightingShowcase.prepare(player);}catch(Exception error){System.err.println("[heyzed] Manual lighting setup failed: "+error);}
        long now=System.nanoTime();
        if(now>=nextHeldSample){nextHeldSample=now+50_000_000L;captureHeld(player,epoch);}
        if(now<nextSample)return;nextSample=now+200_000_000L;
        var time=zombie.GameTime.getInstance();var climate=zombie.iso.weather.ClimateManager.getInstance();
        var celestial=UnrealCelestial.at(time.getYear(),time.getMonth()+1,time.getDayPlusOne(),time.getTimeOfDay());
        var date=java.time.LocalDate.of(time.getYear(),time.getMonth()+1,time.getDayPlusOne());
        var season=climate.getSeason();
        var fallback=new UnrealSolarCycle.Day(6,18);
        var schedule=season==null?new UnrealSolarCycle.Schedule(fallback,fallback,fallback):UnrealSolarCycle.schedule(season,date);
        celestial=UnrealSolarCycle.at(celestial,time.getTimeOfDay(),schedule,time.isEndlessDay(),time.isEndlessNight());
        var state=new JSONObject().put("type","environment").put("epoch",epoch)
            .put("sun",new JSONArray(new float[]{celestial.sun().x,celestial.sun().y,celestial.sun().z}))
            .put("moon",new JSONArray(new float[]{celestial.moon().x,celestial.moon().y,celestial.moon().z}))
            .put("phase",celestial.illuminated()).put("cloud",climate.getCloudIntensity())
            .put("daylight",climate.getDayLightStrength())
            .put("rain",climate.getPrecipitationIntensity()).put("fog",climate.getFogIntensity())
            .put("grid_power",zombie.iso.IsoWorld.instance.isHydroPowerOn()).put("street_night",time.getNight()>=.5f);
        if(environment==null||!state.similar(environment)){environment=state;UnrealObjects.events.add(new JSONObject(state,JSONObject.getNames(state)).put("revision",++revision).toString());}
        Set<IsoLightSwitch> lamps=Collections.newSetFromMap(new IdentityHashMap<>());
        for(var entry:player.getCell().getLamppostPositions().toArray())if(entry instanceof zombie.iso.IsoLightSource source&&source.getSwitches()!=null)
            for(var value:source.getSwitches().toArray())if(value instanceof IsoLightSwitch lamp)lamps.add(lamp);
        for(var roomLight:player.getCell().roomLights){
            if(roomLight.room!=null)for(Object object:roomLight.room.lightSwitches.toArray())
                if(object instanceof IsoLightSwitch lamp)lamps.add(lamp);
            if(Math.hypot(roomLight.x+roomLight.width*.5-player.getX(),roomLight.y+roomLight.height*.5-player.getY())>100)continue;
            var square=player.getCell().getGridSquare(roomLight.x,roomLight.y,roomLight.z);if(square==null)continue;
            String id="room:"+roomLight.x+":"+roomLight.y+":"+roomLight.z+":"+roomLight.width+":"+roomLight.height;
            String control="";
            if(roomLight.room!=null)for(Object value:roomLight.room.lightSwitches.toArray())if(value instanceof IsoLightSwitch lamp&&lamp.getSprite()!=null){
                control=identity(lamp.getSquare().x,lamp.getSquare().y,lamp.getSquare().z,lamp.getSprite().getName());
                controls.put(control,new java.lang.ref.WeakReference<>(lamp));break;
            }
            boolean enabled=roomLight.room!=null&&roomLight.room.def.lightsActive&&(square.haveElectricity()||square.hasGridPower());
            publish(new JSONObject().put("id",id).put("kind","room").put("control",control).put("sprite","room")
                .put("enabled",enabled).put("deleted",false)
                .put("x",roomLight.x+roomLight.width*.5).put("y",roomLight.y+roomLight.height*.5)
                .put("height_cm",(roomLight.z+1)*UnrealMetrics.FLOOR_CM-15)
                .put("radius",Math.min(20,Math.hypot(roomLight.width,roomLight.height)*.5+5))
                .put("color",new JSONArray(new float[]{1,.82f,.62f})),epoch);
        }
        for(var lamp:lamps){
            var square=lamp.getSquare();var sprite=lamp.getSprite();if(square==null||sprite==null)continue;
            String id=identity(square.x,square.y,square.z,sprite.getName());
            controls.put(id,new java.lang.ref.WeakReference<>(lamp));
            if(lamp.lightRoom){
                publish(new JSONObject().put("id",id).put("kind","switch").put("sprite",sprite.getName()).put("control",id).put("interactable",Math.abs(player.getZ()-lamp.getZ())<.1&&Math.hypot(player.getX()-lamp.getX(),player.getY()-lamp.getY())<=2.5&&player.CanSee(lamp))
                    .put("enabled",lamp.isActivated()).put("deleted",lamp.getObjectIndex()<0).put("radius",0)
                    .put("x",square.x+.5).put("y",square.y+.5).put("height_cm",square.z*UnrealMetrics.FLOOR_CM)
                    .put("yaw",sprite.getProperties().has(IsoFlagType.attachedN)?90:
                        sprite.getProperties().has(IsoFlagType.attachedS)?-90:
                        sprite.getProperties().has(IsoFlagType.attachedE)?180:0)
                    .put("color",new JSONArray(new float[]{1,1,1})),epoch);continue;
            }
            var props=sprite.getProperties();String name=sprite.getName();
            String custom=props.get("CustomName"),group=props.get("GroupName");
            String label=((custom==null?"":custom)+" "+(group==null?"":group)).toLowerCase(Locale.ROOT);
            String kind=label.contains("sign")||name.startsWith("advertising_")?"sign":
                props.has("IsTableTop")?"table":name.startsWith("lighting_outdoor_")||name.startsWith("appliances_com_01_")?"street":"wall";
            int radius=0;float r=lamp.getPrimaryR(),g=lamp.getPrimaryG(),b=lamp.getPrimaryB();boolean active=false;
            for(var light:lamp.getLights()){radius=Math.max(radius,Math.min(20,light.radius));active|=light.active;}
            String facing=props.get("Facing");double yaw="N".equals(facing)?-90:"W".equals(facing)?180:"S".equals(facing)?90:0;
            publish(new JSONObject().put("id",id).put("kind",kind).put("sprite",name.replace("_on_","_")).put("control",id).put("interactable",Math.abs(player.getZ()-lamp.getZ())<.1&&Math.hypot(player.getX()-lamp.getX(),player.getY()-lamp.getY())<=2.5&&player.CanSee(lamp))
                .put("deleted",lamp.getObjectIndex()<0).put("enabled",lamp.isActivated()&&lamp.canSwitchLight()&&(!lamp.streetLight||(time.getNight()>=.5f&&lamp.hasGridPower())))
                .put("radius",radius).put("x",square.x+.5).put("y",square.y+.5)
                .put("height_cm",square.z*UnrealMetrics.FLOOR_CM).put("yaw",yaw)
                .put("support",Boolean.TRUE.equals(lamp.getModData().rawget("heyzedPreviewTable")))
                .put("color",new JSONArray(new float[]{Math.clamp(r*2,0,1),Math.clamp(g*2,0,1),Math.clamp(b*2,0,1)})),epoch);
        }
    }
    private static void publish(JSONObject state,long epoch){
        String id=state.getString("id");
        if(!id.startsWith("held:")&&!id.startsWith("room:")){
            int first=id.indexOf(':'),second=id.indexOf(':',first+1),third=id.indexOf(':',second+1);
            if(third>=0&&!state.optBoolean("deleted"))squareLights.computeIfAbsent(id.substring(0,third+1),key->new HashSet<>()).add(id);
        }
        JSONObject old=previous.get(id);
        if(old==null||!state.similar(old)){
            previous.put(id,state);
            UnrealObjects.events.add(new JSONObject(state,JSONObject.getNames(state)).put("type","light").put("epoch",epoch).put("revision",++revision).toString());
        }
    }
    private static void captureHeld(IsoPlayer player,long epoch){
        var items=player.getActiveLightItems(new ArrayList<zombie.inventory.InventoryItem>());
        for(var item:new zombie.inventory.InventoryItem[]{player.getPrimaryHandItem(),player.getSecondaryHandItem()})
            if(item!=null&&item.getLightStrength()>0&&!items.contains(item))items.add(item);
        Set<String> present=new HashSet<>();
        for(var item:items){
            String id="held:"+item.getID();present.add(id);
            publish(new JSONObject().put("id",id).put("kind","held").put("sprite",item.getFullType()).put("hand",item==player.getSecondaryHandItem()?"left":item==player.getPrimaryHandItem()?"right":"attached")
                .put("enabled",item.isEmittingLight()).put("deleted",false).put("x",0).put("y",0).put("height_cm",0)
                .put("radius",Math.min(20,item.getLightDistance())).put("lumens",Math.max(0,item.getLightStrength())*700)
                .put("cone",item.isTorchCone()?Math.toDegrees(Math.acos(Math.clamp(item.getTorchDot(),.1f,.99f))):0)
                .put("color",new JSONArray(new float[]{1,.94f,.82f})),epoch);
        }
        for(String id:held)if(!present.contains(id))publish(new JSONObject().put("id",id).put("deleted",true),epoch);
        held.clear();held.addAll(present);
    }
    static boolean command(JSONObject command,IsoPlayer player,long epoch){
        if(command.optLong("generation",-1)!=epoch)return false;
        if(command.optString("action").equals("held_toggle")){
            for(var item:new zombie.inventory.InventoryItem[]{player.getSecondaryHandItem(),player.getPrimaryHandItem()})
                if(item!=null&&item.getLightStrength()>0&&item.canBeActivated()){
                    item.setActivated(!item.isActivated());nextHeldSample=0;return true;
                }
            return false;
        }
        var reference=controls.get(command.optString("id"));var lamp=reference==null?null:reference.get();
        if(lamp==null||lamp.getObjectIndex()<0||Math.hypot(player.getX()-lamp.getX(),player.getY()-lamp.getY())>2.5||
            Math.abs(player.getZ()-lamp.getZ())>.1||!player.CanSee(lamp))return false;
        boolean before=lamp.isActivated();lamp.setActive(!before);nextSample=0;
        return lamp.isActivated()!=before;
    }

    private UnrealLighting(){}
}
