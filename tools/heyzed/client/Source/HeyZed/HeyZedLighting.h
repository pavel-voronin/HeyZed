#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "HeyZedLighting.generated.h"

class FJsonObject;
struct FStreamableHandle;
class UHeyZedFixtureLibrary;
class AHeyZedWorldFixture;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class ULocalLightComponent;
class UStaticMesh;
class UMaterialInterface;
class APostProcessVolume;
class UInstancedStaticMeshComponent;
struct FHeyZedFixtureProfile;

USTRUCT(BlueprintType)
struct FHeyZedFixture
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FString> Controls;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Position=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Radius=1300;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FLinearColor Color=FLinearColor(1,.63f,.3f);
};

UCLASS(BlueprintType)
class UHeyZedLightResources : public UDataAsset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="heyzed") static bool PrepareFixtureMesh(UStaticMesh* Mesh,const TArray<int32>& NonShadowSlots);
    UFUNCTION(BlueprintCallable,Category="heyzed") static bool PrepareOcclusionMesh(UStaticMesh* Mesh);
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> StreetLamp;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> BulbMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> FarGlowMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 BulbSlot=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Emitter=FVector(0,0,326.43);
};

UCLASS()
class AHeyZedLightRegion : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedLightRegion();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere) FString Identity;
    UPROPERTY(EditAnywhere) TArray<FHeyZedFixture> Fixtures;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Poles;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FarGlows;
};

USTRUCT()
struct FHeyZedLightState
{
    GENERATED_BODY()
    FVector Position=FVector::ZeroVector;
    FVector Direction=FVector(0,0,-1);
    FLinearColor Color=FLinearColor::White;
    float Radius=1000, Lumens=1800, Outer=0;
    int64 Revision=-1;
    bool Enabled=false, Deleted=false;
    FString Kind=TEXT("street"), Sprite, Control, Hand=TEXT("right");
    float Yaw=0;
    bool Support=false, Interactable=false;
};

UCLASS(Config=Game)
class AHeyZedLighting : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedLighting();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    bool StartupReady() const {return EnvironmentRevision>=0 && Resources && FixtureLibrary;}
    void RefreshObjectRepresentations(){NextRefresh=0;}
    void Apply(const TSharedRef<FJsonObject>& Event);
    void RegisterRegion(AHeyZedLightRegion* Region);
    void UnregisterRegion(AHeyZedLightRegion* Region);
    void SetIdentity(const FString& Value);
    void SetEpoch(int64 Value);
    FString RequestInteraction(const FVector& Position,const FVector& Forward);
    FString RequestHeldToggle();
    void PublishTelemetry(const TSharedRef<FJsonObject>& Health) const;
    UPROPERTY(Config,EditAnywhere) float LocalDistance=12000;
    UPROPERTY(Config,EditAnywhere) float DetailedGeometryDistance=18000;
    UPROPERTY(Config,EditAnywhere) float StreetLumens=1800;
    UPROPERTY(Config,EditAnywhere) float ExposureCompensation=-.5f;
    UPROPERTY(Config,EditAnywhere) float MinimumExposureEV100=-2;
    UPROPERTY(Config,EditAnywhere,meta=(ClampMin="0.1")) float StreamingFadeSeconds=.8f;
    UPROPERTY() TObjectPtr<UHeyZedLightResources> Resources;
    UPROPERTY() TObjectPtr<UHeyZedFixtureLibrary> FixtureLibrary;
private:
    struct FSourcePresentation
    {
        FHeyZedLightState State;
        FVector Anchor=FVector::ZeroVector;
        FRotator Rotation=FRotator::ZeroRotator;
        const FHeyZedFixtureProfile* Profile=nullptr;
        TWeakObjectPtr<AHeyZedWorldFixture> Model;
        int64 ModelRevision=MIN_int64;
    };
    void RefreshLights();
    void RefreshPreparedFixtures();
    void PrepareSource(const FString& Id,const FHeyZedLightState& State);
    void RefreshRegion(AHeyZedLightRegion* Region);
    FHeyZedLightState FixtureState(const FHeyZedFixture& Fixture) const;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Moon;
    UPROPERTY() TObjectPtr<USkyLightComponent> Sky;
    UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
    UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
    UPROPERTY() TObjectPtr<APostProcessVolume> Exposure;
    UPROPERTY() TMap<FString,TObjectPtr<ULocalLightComponent>> Lights;
    UPROPERTY() TMap<FString,TObjectPtr<AHeyZedWorldFixture>> Models;
    TMap<FString,FHeyZedLightState> Presented;
    uint64 NextCommand=0;
    FString CommandSession;
    TMap<FString,float> VisibilityWeights;
    TMap<FString,float> VisibilityTargets;
    TMap<FString,FHeyZedLightState> States;
    TMap<FString,FSourcePresentation> SourcePresentations;
    TMap<FString,FHeyZedLightState> PreparedFixtures;
    TSet<FString> PreparedControls;
    bool bFixturesDirty=true,bSourcesDirty=true;
    TSet<TWeakObjectPtr<AHeyZedLightRegion>> Regions;
    FString Identity;
    int64 Epoch=0, EnvironmentRevision=-1;
    double NextRefresh=0;
    FVector LastRefreshPosition=FVector::ZeroVector;
    bool GridPower=false, StreetNight=false;
    FQuat SunTarget=FQuat::Identity,MoonTarget=FQuat::Identity;
    float SunTargetLux=0,MoonTargetLux=0;
    TSharedPtr<FStreamableHandle> ResourceLoad;
};
