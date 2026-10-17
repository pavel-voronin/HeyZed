#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeyZedTraceDiagnostics.generated.h"

UCLASS()
class UHeyZedTraceDiagnostics : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool Export(const FString& TraceFilename,const FString& ResponseFilename,double TimeoutSeconds=300,bool TimingOnly=true);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool Execute(const FString& Command);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool ExportStatistics(const FString& OutputFilename,const FString& ThreadPattern,double StartSeconds=0,double EndSeconds=36000,bool Gpu=false);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static double BookmarkTime(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="heyzed")
    static bool ExportFrames(const FString& OutputFilename,double StartSeconds=0,double EndSeconds=36000);
};
