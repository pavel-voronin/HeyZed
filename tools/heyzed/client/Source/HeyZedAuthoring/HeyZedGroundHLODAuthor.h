#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeyZedGroundHLODAuthor.generated.h"

UCLASS()
class UHeyZedGroundHLODAuthor : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool BuildSample(AActor* Source,const FString& PackageName,const FString& ReportFilename,double MinVisibleDistance=38400);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static void ConfigurePreviewView(AActor* Source);
};
