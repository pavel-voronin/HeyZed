#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeyZedNativeUI.generated.h"

class UTextureRenderTarget2D;
class SHeyZedNativeUI;
class FHeyZedNativeUIInput;
struct FHeyZedNativeUIState;

UCLASS()
class UHeyZedNativeUI : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHeyZedNativeUI,STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game; }
private:
    TSharedPtr<FHeyZedNativeUIState,ESPMode::ThreadSafe> State;
    TSharedPtr<SHeyZedNativeUI> Widget;
    TSharedPtr<FHeyZedNativeUIInput> Input;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Display;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PendingDisplay;
    uint64 PendingGeneration=0;
    FIntPoint DesiredSize=FIntPoint::ZeroValue,CurrentSize=FIntPoint::ZeroValue;
    double SizeChangedAt=0,NextHealth=0;
    int32 ReviewStep=0;
    double ReviewAt=0;
    FIntPoint ReviewInitialSize=FIntPoint::ZeroValue;
    int32 ReviewInitialMode=2;
    FString InputReviewPath;
    int64 InputReviewSequence=0;
    double NextInputReview=0;
    bool bActivated=false;
};
