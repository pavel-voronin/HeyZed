#pragma once
#include "CoreMinimal.h"

struct FHeyZedEntityPose
{
    int64 Epoch=0,Sequence=0,SampleNs=0,AppearanceRevision=0;
    FString Id,Vehicle,Animation;
    int32 Seat=-1;
    bool Seated=false;
    FTransform World=FTransform::Identity;
    FVector Velocity=FVector::ZeroVector;
    TArray<uint8> Bones,Locals;
};

bool ReadHeyZedEntityPose(const TArray<uint8>& Bytes,FHeyZedEntityPose& Pose,FString& Error);
