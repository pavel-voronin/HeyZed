package com.pavelvoronin.heyzed;

import zombie.core.skinnedmodel.DeadBodyAtlas;
import zombie.core.skinnedmodel.ModelManager;
import zombie.core.skinnedmodel.advancedanimation.AnimatedModel;
import zombie.core.skinnedmodel.model.ModelInstance;
import zombie.core.skinnedmodel.model.ModelInstanceTextureCreator;
import zombie.iso.Vector2;
import zombie.iso.objects.IsoDeadBody;

final class UnrealCorpseVisual {
    final AnimatedModel model=new AnimatedModel();
    final DeadBodyAtlas.BodyParams params=new DeadBodyAtlas.BodyParams();
    ModelInstanceTextureCreator creator;
    UnrealCorpseVisual(IsoDeadBody body){
        params.init(body);
        model.setAnimate(false);model.setAnimSetName(params.animSetName);model.setState(params.stateName);
        model.setPrimaryHandModelName(params.primaryHandItem);model.setSecondaryHandModelName(params.secondaryHandItem);
        model.setAttachedModelNames(params.attachedModelNames);
        model.setVariable("FallOnFront",params.fallOnFront);model.setVariable("KilledByFall",params.killedByFall);
        params.variables.forEach((key,value)->model.setVariable(key,value));
        model.setModelData(params.baseVisual,params.itemVisuals);
        model.setAngle(new Vector2().setLengthAndDirection(params.angle,1));
        model.setTrackTime(params.trackTime);model.setGrappleable(params.grappleable);
    }
    ModelInstance prepare(){
        model.update();
        var animation=model.getAnimationPlayer();
        if(!animation.isReady()||!model.isReadyToRender())return null;
        if(params.diedBoneTransforms!=null&&params.diedBoneTransforms.length>0){
            if(animation.isBoneCountMismatched(params.diedBoneTransforms))throw new IllegalStateException("Native corpse bone count mismatch");
            animation.stopAll();var track=animation.play("DeathPose",true,true,-1f);
            if(track==null)return null;
            track.setBlendWeight(1);track.initRagdollTransforms(params.diedBoneTransforms);
            animation.Update(0f);
        }
        if(animation.isBoneTransformsNeedFirstFrame())return null;
        if(creator==null){creator=ModelInstanceTextureCreator.alloc();creator.init(params.baseVisual,params.itemVisuals,model.getModelInstance());}
        return model.getModelInstance();
    }
    void close(){
        if(creator!=null){creator.postRender();creator=null;}
        ModelManager.instance.resetModelInstanceRecurse(model.getModelInstance(),model);
        model.releaseAnimationPlayer();params.Reset();
    }
}
