#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "InstanceDataTypes.h"
#include "Components/StaticMeshComponent.h"
#include "HeyZedConstructionPlanner.h"
#include "HeyZedArchitecture.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class FJsonObject;
struct FStreamableHandle;
struct FHeyZedArchitectureMetadata;
struct FHeyZedConstructionWork;

UCLASS()
class UHeyZedArchitectureHLODSource : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    UPROPERTY() FString EnvelopeKey;
#if WITH_EDITOR
    virtual void ComputeHLODHash(FHLODHashBuilder& Hash) const override;
#endif
};

UCLASS(BlueprintType)
class UHeyZedArchitectureResources : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FName,TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY() TMap<FString,TSoftObjectPtr<UStaticMesh>> Meshes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,FString> Definitions;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,FString> SourceBindings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,FName> SurfaceBindings;
    UFUNCTION(BlueprintCallable,Category="heyzed") bool PrepareLibrary(const FString& Index,const FString& Directory);
    UFUNCTION(BlueprintCallable,Category="heyzed") static int32 RepairMeshFacing(UStaticMesh* Mesh);
    UStaticMesh* Resolve(const FString& Recipe,bool Save,const TSharedPtr<FJsonObject>& Parsed=nullptr);
    FSoftObjectPath PendingAsset(const FString& Recipe,const TSharedPtr<FJsonObject>& Parsed=nullptr) const;
    bool IsEmptyRecipe(const FString& Recipe) const;
    TSharedPtr<FJsonObject> Definition(const FString& Name);
    TSharedPtr<FJsonObject> Source(const FString& Name);
    TSharedRef<const FHeyZedConstructionCatalog,ESPMode::ThreadSafe> GetPlanningCatalog();
private:
    TMap<FString,TSharedPtr<FJsonObject>> ParsedDefinitions,ParsedSources;
    TSet<FString> EmptyRecipes;
    TSharedPtr<const FHeyZedConstructionCatalog,ESPMode::ThreadSafe> PlanningCatalog;
};

USTRUCT()
struct FHeyZedConstructionInstance
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Component;
    UPROPERTY() int32 Index=INDEX_NONE;
    UPROPERTY() FTransform Closed;
    UPROPERTY() FString Door;
    UPROPERTY() FString OwnerAddress;
    UPROPERTY() FString Signature;
};

UCLASS()
class AHeyZedArchitecture : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedArchitecture();
    virtual void PostLoad() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Covers(const FVector& Position) const;
    bool IsPresentationActive() const { return bActivationStarted&&!bEndingPlay; }
#if WITH_EDITOR
    virtual TArray<UActorComponent*> GetHLODRelevantComponents() const override;
#endif
    UFUNCTION(BlueprintCallable,Category="heyzed") bool Prepare(const FString& Input);
    UFUNCTION(BlueprintCallable,Category="heyzed") static bool ConfigureWorld(UWorld* World,float LoadingRange=20000);
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UHeyZedArchitectureResources> Resources;
    UPROPERTY(EditAnywhere) FString Description;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;
    UPROPERTY() TArray<FHeyZedConstructionInstance> SavedInstances;
    void Apply(const TSharedRef<FJsonObject>& Cell);
    bool Door(const FVector& Position,bool North,float Angle);
    bool UpdateConstruction(bool& Changed,FIntVector& ChangedCell);
    static void ApplyToLoaded(UWorld* World,const TSharedRef<FJsonObject>& Cell);
    static AHeyZedArchitecture* Find(UWorld* World,const FVector& Position);
private:
    UPROPERTY(Transient) TObjectPtr<UHeyZedArchitectureHLODSource> EnvelopeSource;
    static FString Key(int32 X,int32 Y,int32 Z);
    void Load();
    void StartMetadataPreparation();
    void FinishMetadataPreparation();
    void InstallMetadata(FHeyZedArchitectureMetadata&& Data);
    void ActivateMetadata();
    TSharedPtr<FHeyZedArchitectureMetadata> PreparedMetadata;
    FBox PendingBounds=FBox(ForceInit);
    TSet<FString> PreparedDoors;
    uint64 MetadataGeneration=0;
    bool bMetadataReady=false,bMetadataScheduled=false,bActivationStarted=false,bMetadataActivated=false;
    void Rebuild(const FString& Address,bool Save);
    void GatherParts(const FString& Address,bool Save);
    void QueueRebuild(const FString& Address,bool InvalidatePlan=true);
    TSet<FString> DirtyCells;
    TMap<FString,TArray<TSharedPtr<FStreamableHandle>>> PendingLoads;
    TMap<FString,TArray<FHeyZedConstructionPlacement>> PendingPlans;
    TSet<FSoftObjectPath> FailedAssets;
    TSharedPtr<FHeyZedConstructionWork,ESPMode::ThreadSafe> PlanningWork;
    TMap<FString,uint64> PlanVersions;
    FHeyZedConstructionSnapshot ConstructionSnapshot(const FString& Address);
    bool bEndingPlay=false;
    void DiscardUnused(const FString& OwnerAddress);
    TSet<FString> KeptParts;
    void Place(const FString& OwnerAddress,const TSharedRef<FJsonObject>& Recipe,const FVector& Position,float Yaw,const TArray<FName>& Materials,bool Save,const FString& Door=FString(),const FVector& Scale=FVector::OneVector,const FString* PreparedText=nullptr);
    TMap<FString,TSharedPtr<FJsonObject>> Cells;
    TMap<FString,TArray<FHeyZedConstructionInstance>> Instances;
    TMap<FString,UInstancedStaticMeshComponent*> Batches;
    TSet<FString> OwnedEdges,ContextEdges,OwnedFloors,Footprint,Interior;
    FVector2D Minimum=FVector2D::ZeroVector,Maximum=FVector2D::ZeroVector;
    double Floor=244.94900703430176,Thickness=12,Slab=15;
};
