#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeyZedCrowd.generated.h"
class UHeyZedEntityResources;
class USkeletalMeshComponent;
class FJsonObject;
USTRUCT()
struct FHeyZedCrowdView
{
    GENERATED_BODY()
    double SourceTime=0,PreviousTime=0,Received=0,Interval=.2;
    FTransform Previous,Target;
    FString Action;
    int32 Variant=0;
    bool Detail=false,Virtual=false,HeightKnown=true;
    FTransform BlendFrom;
    double BlendStarted=-1;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
};
UCLASS()
class UHeyZedCrowdComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeyZedCrowdComponent();
    void Initialize(UHeyZedEntityResources* Value);
    void SetEpoch(int64 Value);
    void Apply(const TSharedRef<FJsonObject>& Event);
    void Present(float Delta);
    bool HasProxy(const FString& Id) const;
    bool WantsDetail(const FString& Id) const;
    bool ShowProxy(const FString& Id,bool DetailReady) const;
    void AppendHealth(FJsonObject& Health) const;
    void DetailReleased(const FString& Id,const FTransform& Transform);
private:
    USkeletalMeshComponent* Leader(const FString& Action,int32 Permutation);
    UPROPERTY() TObjectPtr<UHeyZedEntityResources> Resources;
    UPROPERTY() TMap<FString,FHeyZedCrowdView> Views;
    UPROPERTY() TMap<FString,TObjectPtr<USkeletalMeshComponent>> Leaders;
    int64 Epoch=0;
    bool bResourcesReady=false;
    double NextReadiness=0;
    int32 RealCount=0,VirtualCount=0,VisibleCount=0,MissingActions=0;
    int64 VirtualSamples=0,VirtualProxyCreations=0;
    int32 PeakVirtualViews=0,PeakVisibleVirtualViews=0;
};
