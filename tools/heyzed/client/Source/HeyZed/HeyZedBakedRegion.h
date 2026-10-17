#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeyZedTerrain.h"
#include "HeyZedBakedRegion.generated.h"

class UTexture2D;
class UInstancedStaticMeshComponent;

USTRUCT()
struct FHeyZedBakedStyle
{
    GENERATED_BODY()
    UPROPERTY() TArray<FName> Parts;
    UPROPERTY() bool Uniform=false;
    UPROPERTY() FName Base;
    UPROPERTY() FName Blend;
    UPROPERTY() FVector4f Corners=FVector4f(0,0,0,0);
    UPROPERTY() FVector4f Edges=FVector4f(0,0,0,0);
};

// Serialized native World Partition actors own the static base; the bridge owns only overrides.
UCLASS()
class AHeyZedBakedRegion : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedBakedRegion();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Delta) override;
    UFUNCTION(BlueprintCallable, Category="heyzed") bool Author(const FString& Filename,const FString& MapIdentity);
    UFUNCTION(BlueprintCallable, Category="heyzed") static bool ConfigureWorld(UWorld* World);
    UFUNCTION(BlueprintCallable, Category="heyzed") bool ApplyArchitectureMask(const FString& Filename);
    bool Surface(FIntPoint Address,FHeyZedGroundCell& Out) const;
    void Override(FIntPoint Address);
    void Activate(bool Enabled);
    UPROPERTY() FIntPoint Cell;
    UPROPERTY() FString Identity;
private:
    UPROPERTY() TArray<FHeyZedBakedStyle> Styles;
    UPROPERTY() TArray<uint8> CompressedGrid;
    UPROPERTY() TArray<uint8> ArchitectureMask;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Surfaces;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Mask;
    TArray<uint16> Grid;
    TArray<uint8> MaskBytes;
    bool DirtyMask=false;
};
