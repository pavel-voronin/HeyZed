#include "HeyZedFirstPerson.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"

struct FHeyZedFirstPersonProxy : FAnimInstanceProxy
{
    FAnimNode_CopyPoseFromMesh Copy;
    explicit FHeyZedFirstPersonProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        Copy.bUseAttachedParent=true;
        Copy.bCopyCurves=true;
    }
    virtual FAnimNode_Base* GetCustomRootNode() override { return &Copy; }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override { Nodes.Add(&Copy); }
};

FAnimInstanceProxy* UHeyZedFirstPersonInstance::CreateAnimInstanceProxy() { return new FHeyZedFirstPersonProxy(this); }
void UHeyZedFirstPersonInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete static_cast<FHeyZedFirstPersonProxy*>(Proxy); }
