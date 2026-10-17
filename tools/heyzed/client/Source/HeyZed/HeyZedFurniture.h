#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "InstanceDataTypes.h"
#include "HeyZedFurniture.generated.h"
class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class FJsonObject;
struct FStreamableHandle;
struct FHeyZedFurnitureBinding {FString Mesh,State;FTransform Placement;};
struct FHeyZedFurnitureFact {FString Id,Tile,State;};
UCLASS(BlueprintType)
class UHeyZedFurnitureLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,TSoftObjectPtr<UStaticMesh>> Meshes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString BindingDescription;
    const TArray<FHeyZedFurnitureBinding>* FindBindings(const FString& Tile);
    UFUNCTION(BlueprintCallable,Category="heyzed") bool PrepareMesh(const FString& Name,const FString& Source,const TArray<UMaterialInterface*>& Materials);
    UFUNCTION(BlueprintCallable,Category="heyzed") void RegisterMesh(const FString& Name,const FString& Path);
private:
    TMap<FString,TArray<FHeyZedFurnitureBinding>> Bindings;
    bool bBindingsParsed=false;
};
UCLASS()
class AHeyZedFurnitureRegion : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedFurnitureRegion();
    void Initialize(FIntPoint Address,UHeyZedFurnitureLibrary* Resources);
    void SetCell(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    bool Ready() const;
    bool Empty() const {return Cells.IsEmpty();}
    void Diagnostics(const TSharedRef<FJsonObject>& Out) const;
    int32 PendingLoads() const {return Loading.Num();}
private:
    struct FReference {FString Mesh;FPrimitiveInstanceId Id;FString Owner;FTransform World;};
    UPROPERTY() TObjectPtr<UHeyZedFurnitureLibrary> Library;
    FIntPoint Region;
    TMap<FIntVector,TArray<FHeyZedFurnitureFact>> Cells;
    TMap<FIntVector,TArray<FReference>> Instances;
    TSet<FIntVector> Dirty;
    TSet<FString> Failed;
    TMap<FString,TSharedPtr<FStreamableHandle>> Loading;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UInstancedStaticMeshComponent>> Groups;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UStaticMesh>> Resident;
    void Request(const FString& Mesh);
    bool Replace(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts);
};
UCLASS()
class UHeyZedFurnitureSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override {return Type==EWorldType::Game||Type==EWorldType::PIE;}
    void Configure(int64 InEpoch,const FString& InIdentity,const FVector& InOrigin);
    void Apply(const TSharedRef<FJsonObject>& Event);
    TSharedRef<FJsonObject> Diagnostics() const;
    bool StartupReady() const;
    bool ReplacesTile(FIntVector Address,const FString& Tile) const;
    double WorkDeadline();
    FVector Origin=FVector::ZeroVector;
private:
    using FCells=TMap<FIntVector,TArray<FHeyZedFurnitureFact>>;
    struct FChunk {int64 Generation=0,Revision=-1,PendingGeneration=0,PendingRevision=-1;int32 NextPart=0;FCells Cells,Pending;};
    int64 Epoch=0,LastRevision=-1;
    FString Identity;
    bool bSourceReady=false,bFailed=false,bLibraryAttempted=false;
    uint64 WorkFrame=MAX_uint64;
    double FrameDeadline=0;
    UPROPERTY() TObjectPtr<UHeyZedFurnitureLibrary> Library;
    UPROPERTY() TMap<FIntPoint,TObjectPtr<AHeyZedFurnitureRegion>> Regions;
    TSharedPtr<FStreamableHandle> LibraryLoad;
    TMap<FIntPoint,FChunk> Chunks;
    TMap<int64,TArray<TSharedRef<FJsonObject>>> Deferred;
    void Reset();
    void Deliver(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts);
    void RemoveChunk(FIntPoint Key);
};
