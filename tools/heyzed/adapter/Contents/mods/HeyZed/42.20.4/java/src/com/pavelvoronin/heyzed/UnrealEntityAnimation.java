package com.pavelvoronin.heyzed;

import java.lang.reflect.Field;
import java.util.*;
import org.json.*;
import zombie.GameTime;
import zombie.characters.IsoGameCharacter;
import zombie.core.skinnedmodel.advancedanimation.AnimLayer;
import zombie.core.skinnedmodel.animation.*;
import zombie.core.skinnedmodel.model.ModelInstance;

/** Presentation facts only: native gameplay and animation evaluation remain untouched. */
final class UnrealEntityAnimation {
    private static final Set<String> STATES=Set.of("movement","run","sprint","walk","walktoward","walktoward-network","followwall","pathfind","idle","headlook","maskingleft","maskingright","slowidleblend","strafe");
    private static final LiveAnimationTrackEntries live=new LiveAnimationTrackEntries();
    private record Clock(int id,double at,float phase){}
    private static final Map<AnimationTrack,Clock> clocks=new WeakHashMap<>();
    private static final Field AXIS;
    static {try{AXIS=AnimationTrack.class.getDeclaredField("deferredBoneAxis");AXIS.setAccessible(true);}catch(Exception error){throw new ExceptionInInitializerError(error);}}
    static JSONObject describe(IsoGameCharacter character,ModelInstance model){
        var player=model.animPlayer;
        JSONObject result=new JSONObject().put("mode","stream").put("reason","state");
        String state=character.getActionContext().getCurrentStateName();result.put("state",state);
        if(state==null||!STATES.contains(state.toLowerCase(Locale.ROOT))||character.isSeatedInVehicle()||character.isDead()||character.isAiming())return result;
        if(Math.abs(player.getTwistAngle())>.035f||Math.abs(player.getShoulderTwistAngle())>.035f)return result.put("reason","turn_pose");
        live.setTracks(player.getMultiTrack().getTracks(),.001f,false);
        if(live.count()==0)return result.put("reason","no_tracks");
        JSONArray tracks=new JSONArray();
        try {
            for(int index=live.count()-1;index>=0;--index){
                var entry=live.get(index);var track=entry.getTrack();
                String layer=AnimLayer.getCurrentStateName(track.animLayer);
                if(layer==null||!STATES.contains(layer.toLowerCase(Locale.ROOT))||track.isRagdoll()||track.getUseDeferredRotation()||!track.hasClip())return result.put("reason","interaction_track");
                JSONArray mask=null;boolean masked=false;
                for(int bone=0;bone<player.getModelTransformsCount();++bone){float weight=track.getBoneWeight(bone);if(!Float.isFinite(weight))throw new IllegalArgumentException("Non-finite native bone weight");if(Math.abs(weight-1)>.0001f){masked=true;break;}}
                if(masked){mask=new JSONArray();for(int bone=0;bone<player.getModelTransformsCount();++bone)mask.put(track.getBoneWeight(bone));}
                int deferred=track.getDeferredMovementBoneIdx();String deferredName="";
                if(deferred>=0)for(var item:player.getSkinningBoneIndices().entrySet())if(item.getValue()==deferred){deferredName=item.getKey();break;}
                float phase=track.getCurrentAnimationTime();double at=System.nanoTime()*1e-9;double rate=track.isPlaying?track.getSpeedDelta()*(track.reverse?-1:1):0;
                Clock previous=clocks.put(track,new Clock(track.getId(),at,phase));
                if(previous!=null&&previous.id==track.getId()&&at-previous.at>.01&&at-previous.at<2){double advance=phase-previous.phase;if(track.isLooping()){if(!track.reverse&&advance<0)advance+=track.getDuration();if(track.reverse&&advance>0)advance-=track.getDuration();}double measured=advance/(at-previous.at);if(Double.isFinite(measured)&&Math.abs(measured)<20)rate=measured;}
                var item=new JSONObject().put("clip",track.getClip().name.toLowerCase(Locale.ROOT)).put("id",track.getId())
                    .put("phase",phase).put("duration",track.getDuration()).put("rate",rate)
                    .put("weight",entry.getBlendWeight()).put("loop",track.isLooping()).put("deferred",deferredName).put("axis",String.valueOf(AXIS.get(track)));
                if(masked)item.put("mask",mask);
                tracks.put(item);
            }
        }catch(Exception failure){return result.put("reason","unsupported_track");}
        Set<String> clips=new TreeSet<>();for(int index=0;index<tracks.length();++index)clips.add(tracks.getJSONObject(index).getString("clip"));
        return result.put("mode","local").put("reason","").put("tracks",tracks).put("key",String.join("\n",clips));
    }
    static void reset(){clocks.clear();live.clear();}
    private UnrealEntityAnimation(){}
}
