#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "HeyZedVegetation.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class FJsonObject;
class FJsonValue;

struct FHeyZedPlant
{
    int32 Kind=0,Stage=1,Z=0;
    uint32 Variant=0;
};
struct FHeyZedPlantSquare
{
    FIntPoint Address;
    TArray<FHeyZedPlant> Plants;
};
struct FHeyZedVegetationBaseline {TArray<FHeyZedPlantSquare> Cells;};

UCLASS()
class UHeyZedVegetationLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TMap<FString,TObjectPtr<UStaticMesh>> Meshes;
    UFUNCTION(CallInEditor,BlueprintCallable) bool PrepareMesh(const FString& Name,const FString& Source,const TArray<UMaterialInterface*>& Materials);
};

UCLASS()
class AHeyZedVegetationRegion : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedVegetationRegion();
    void Setup(FIntPoint Address,UHeyZedVegetationLibrary* Resources);
    void Replace(FIntPoint Address,const TArray<FHeyZedPlant>& Plants,const FVector& Origin);
    void Finish();
    FIntPoint Cell;
private:
    struct FReference {int32 Kind,Index;};
    TMap<FIntPoint,TArray<FReference>> Squares;
    TArray<int32> Free[3];
    UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Groups;
    bool Dirty=false;
};

UCLASS()
class UHeyZedVegetationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHeyZedVegetationSubsystem,STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override {return Type==EWorldType::Game || Type==EWorldType::PIE;}
    void Configure(int64 Epoch,const FString& Identity,const FVector& Origin);
    void Apply(const TSharedRef<FJsonObject>& Block);
private:
    struct FChange {int64 Revision=0;TArray<FHeyZedPlant> Plants;};
    struct FLoad {FIntPoint Address;TFuture<TSharedPtr<FHeyZedVegetationBaseline>> Future;};
    struct FPending {FIntPoint Address;TSharedPtr<FHeyZedVegetationBaseline> Data;int32 Cursor=0;};
    int64 Epoch=0;
    FString Identity,Folder;
    FVector Origin=FVector::ZeroVector;
    FIntPoint Center=FIntPoint(MAX_int32,MAX_int32);
    bool bColdAdmission=false;
    UPROPERTY() TObjectPtr<UHeyZedVegetationLibrary> Library;
    bool bResourceAttempted=false;
    UPROPERTY() TMap<FIntPoint,TObjectPtr<AHeyZedVegetationRegion>> Regions;
    TMap<FIntPoint,FChange> Changes;
    TArray<FIntPoint> Dirty;
    TSet<FIntPoint> Queued;
    TArray<FLoad> Loads;
    TArray<FPending> Pending;
    TSet<FIntPoint> Requested;
    AHeyZedVegetationRegion* Region(FIntPoint Address);
};
