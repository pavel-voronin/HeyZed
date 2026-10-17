#pragma once
#include "WorldPartition/HLOD/HLODBuilder.h"
#include "HeyZedArchitectureHLOD.generated.h"

UCLASS()
class UHeyZedArchitectureHLODSettings : public UHLODBuilderSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> BoxMesh;
    UPROPERTY(EditAnywhere) TMap<FString,TSoftObjectPtr<UMaterialInterface>> ExteriorMaterials;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UMaterialInterface> DefaultMaterial;
    virtual void ComputeHLODHash(FHLODHashBuilder& Hash) const override;
    virtual bool IsReusingSourceMaterials() const override { return true; }
};

UCLASS()
class UHeyZedArchitectureHLOD : public UHLODBuilder
{
    GENERATED_BODY()
public:
    virtual TSubclassOf<UHLODBuilderSettings> GetSettingsClass() const override;
    virtual TArray<UActorComponent*> Build(const FHLODBuildContext& Context, const TArray<UActorComponent*>& Sources) const override;
    virtual bool RequiresWarmup() const override { return false; }
protected:
    virtual bool ShouldIgnoreBatchingPolicy() const override { return true; }
};
