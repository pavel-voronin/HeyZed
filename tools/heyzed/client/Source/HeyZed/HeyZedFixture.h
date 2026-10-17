#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "HeyZedFixture.generated.h"
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextRenderComponent;

USTRUCT(BlueprintType)
struct FHeyZedFixtureProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector MeshOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector MeshScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FRotator MeshRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Emitter=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Direction=FVector(0,0,-1);
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Lumens=600;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float OuterAngle=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool Bulb=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool Sign=false;
};
UCLASS(BlueprintType)
class UHeyZedFixtureLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,FHeyZedFixtureProfile> Profiles;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TMap<FString,FString> Bindings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> EmissionMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> HousingMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> SupportMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Cube;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Sphere;
};
UCLASS()
class AHeyZedWorldFixture : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedWorldFixture();
    void Configure(const FString& Kind,const FHeyZedFixtureProfile& Profile,UHeyZedFixtureLibrary* Library,bool Support);
    void Power(bool Enabled,const FLinearColor& Color);
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Emissions;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
    FString Signature;
    bool bPowerInitialized=false, bEnabled=false;
    FLinearColor LastColor=FLinearColor::Black;
};
