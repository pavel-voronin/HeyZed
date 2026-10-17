#pragma once
#include "CoreMinimal.h"
#include "HeyZedEntitySample.h"
#include "HeyZedEntityPose.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "HeyZedEntities.generated.h"
class UMaterialInterface;
class UTexture2D;
class UMaterialInstanceDynamic;
class USceneComponent;
class UMeshComponent;
class FJsonObject;
class USkeletalMesh;
class UIKRetargeter;
class UAnimSequence;
class UHeyZedCrowdComponent;
struct FIKRetargetProcessor;

UCLASS(BlueprintType)
class UHeyZedEntityResources : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,TSoftObjectPtr<UObject>> Models;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> GlassMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> VehicleMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> VehicleGlassMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,TObjectPtr<UIKRetargeter>> PlayerRetargeters;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,TSoftObjectPtr<UAnimSequence>> NativeAnimations;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<USkeletalMesh> CrowdMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<TObjectPtr<UMaterialInterface>> CrowdMaterials;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,TObjectPtr<UAnimSequence>> CrowdAnimations;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float CrowdScale=1.7564f;
    UFUNCTION(BlueprintCallable,Category="heyzed") bool RegisterNativeAnimations(const FString& Json);
    UFUNCTION(BlueprintCallable,Category="heyzed") bool PreparePlayerRetargeters();
    UFUNCTION(BlueprintCallable,Category="heyzed") bool RegisterNativeModels(const FString& Json);
    UFUNCTION(BlueprintPure,Category="heyzed") int32 NativeModelCount() const {return Models.Num();}
    UFUNCTION(BlueprintCallable,Category="heyzed") static UObject* PrepareNativeModel(const FString& Json,const FString& AssetPath);
    UFUNCTION(BlueprintCallable,Category="heyzed") static UObject* PrepareNativeAnimation(const FString& Json,const FString& ClipName,USkeletalMesh* Mesh,const FString& AssetPath);
};

USTRUCT()
struct FHeyZedEntityPart
{
    GENERATED_BODY()
    FString Key,Texture,AttachmentBone;
    FTransform AttachmentOffset;
    FTransform Local,PreviousLocal;
    FLinearColor Tint=FLinearColor::White;
    bool HasGlass=false,Rigid=false;
    TSharedPtr<FJsonObject> Vehicle;
    bool MultiUV=false,RandomPaint=true;
    TArray<float> Lit;
    UPROPERTY() TObjectPtr<UMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMeshComponent> Glass;
    UPROPERTY() TObjectPtr<UMeshComponent> ViewMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlassMaterial;
};

USTRUCT()
struct FHeyZedEntity
{
    GENERATED_BODY()
    int64 Revision=-1,Sequence=-1;
    double Received=0,PoseReceived=0,NativeSample=0,BlendDuration=.05;
    double PresentationSample=0,AnimationSample=0,RootInterval=.05,RootJitter=0;
    FTransform CrowdBlendFrom;
    double CrowdBlendStarted=-1;
    double BoneSample=0,BoneReceived=0;
    FTransform PresentationRoot;
    TArray<FTransform> PresentationVehicleLocals;
    TArray<FHeyZedEntityRootSample> RootSamples;
    int64 EvaluatedSequence=-1,OffscreenRootSequence=MIN_int64;
    FString Id,Kind,Vehicle;
    int32 Seat=-1;
    bool Seated=false,PoseInitialized=false;
    bool LocalAnimationAcknowledged=false,UsingLocalAnimation=false;
    FString AcknowledgedAnimationKey;
    TSharedPtr<FJsonObject> AnimationState;
    TMap<int32,double> AnimationPhases;
    double AnimationReceived=0,AnimationEvaluated=0,AnimationModeChanged=0;
    TArray<FTransform> PresentedPose,AnimationBlendFrom;
    FTransform Target;
    FVector Velocity=FVector::ZeroVector;
    TArray<FName> BoneNames;
    TMap<FName,int32> BoneIndices;
    TArray<FTransform> PreviousPose,TargetPose;
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TArray<FHeyZedEntityPart> Parts;
    UPROPERTY() TArray<FHeyZedEntityPart> PendingParts;
};

UCLASS()
class AHeyZedEntities : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedEntities();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void SetEpoch(int64 Value);
    void Apply(const TSharedRef<FJsonObject>& Event);
    void ApplyPose(FHeyZedEntityPose&& Pose);
    uint64 Poses=0,Changes=0,MissingModels=0;
    uint64 PoseVisibilitySkipped=0,PoseVisibilityRefreshed=0,RootPoseUpdates=0,RootPoseSkipped=0;
    int32 Count() const {return Entities.Num();}
    int32 ReadyCount() const;
    bool PlayerAppearanceReady() const;
    bool DetailReady(const FString& Id) const;
    void BeginCrowdHandoff(const FString& Id,const FTransform& Transform);
    int32 KindCount(const FString& Kind,bool Ready=false) const;
    int32 RenderedCount(const FString& Kind) const;
    void AppendResourceHealth(FJsonObject& Health) const;
private:
    void Remove(const FString& Id);
    void Resolve(FHeyZedEntity& Entity);
    bool RequestTexture(const FString& Hash,bool Linear=false);
    void ConfigureMaterial(UMaterialInstanceDynamic* Material,const FHeyZedEntityPart& Part);
    bool PosePlayer(FHeyZedEntity& Entity,float Delta);
    bool EvaluateLocomotion(FHeyZedEntity& Entity,TArray<FTransform>& Pose,float Delta,bool SourceSample=false);
    bool EvaluateEntityPose(FHeyZedEntity& Entity,TArray<FTransform>& Pose,float Delta);
    void UpdatePresentation(FHeyZedEntity& Entity,double Now);
    void AcknowledgeLocomotion(FHeyZedEntity& Entity,bool Enabled);
    TMap<FString,TSharedPtr<FIKRetargetProcessor>> PlayerProcessors;
    UPROPERTY() TObjectPtr<UHeyZedEntityResources> Resources;
    UPROPERTY() TObjectPtr<UHeyZedCrowdComponent> Crowd;
    TSet<FString> PendingDetailRelease;
    UPROPERTY() TMap<FString,FHeyZedEntity> Entities;
    UPROPERTY() TMap<FString,TObjectPtr<UTexture2D>> Textures;
    UPROPERTY() TMap<FString,TObjectPtr<UObject>> ResidentModels;
    UPROPERTY() TMap<FString,TObjectPtr<UAnimSequence>> ResidentAnimations;
    TSet<FString> LoadingAnimations,FailedAnimations;
    uint64 FailedModelLoads=0;
    TMap<FString,FHeyZedEntityPose> PendingPoses;
    TSet<FString> LoadingTextures,LoadingModels;
    TMap<FString,int64> Tombstones;
    FString TextureDirectory;
    int64 Epoch=0;
    double NextTexturePrune=0;
    double NextModelPrune=0;
};
