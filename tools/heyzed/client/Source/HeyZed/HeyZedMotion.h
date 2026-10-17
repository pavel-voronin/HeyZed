#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeyZedMotion.generated.h"

class UPoseSearchDatabase;
class USkeletalMesh;

UCLASS()
class UHeyZedMotionLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditAnywhere) TMap<FName,TObjectPtr<UPoseSearchDatabase>> Databases;
};

UCLASS(Transient, Blueprintable)
class UHeyZedMotionInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UHeyZedMotionInstance();
    virtual void NativeInitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
    UPROPERTY() TObjectPtr<UHeyZedMotionLibrary> Library;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") FString SelectedClip;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") FName SelectedGroup;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float StrideRatio = 1;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float PlaybackRate = 1;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bMatchingValid = false;
};

UCLASS()
class UHeyZedMotionAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="heyzed") static FString BuildLibrary(const FString& ManifestPath);
    UFUNCTION(BlueprintCallable, Category="heyzed") static bool PrepareGraph();
};
