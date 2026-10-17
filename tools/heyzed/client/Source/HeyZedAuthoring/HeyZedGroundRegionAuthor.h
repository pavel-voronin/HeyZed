#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeyZedGroundRegionAuthor.generated.h"

class UMaterialInterface;

UCLASS()
class UHeyZedGroundRegionAuthor : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool Build(const FString& Filename,const FString& PackageName,const TArray<FString>& MaterialNames,const TArray<UMaterialInterface*>& MaterialVariants,const FString& ReportFilename);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static void ConfigurePreview(UWorld* World,FVector CameraLocation,FRotator CameraRotation);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static FString FrameMetrics();
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static void ConfigureRenderViewport(int32 Width,int32 Height);
};
