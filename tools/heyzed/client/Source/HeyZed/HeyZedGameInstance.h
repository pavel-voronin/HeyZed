#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "HeyZedGameInstance.generated.h"
class FHeyZedLoadingView;

UCLASS()
class UHeyZedGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void Shutdown() override;
    bool ExposureReady(double WindowStart);
private:
    void BeginMapLoad(const FString& Map);
    TSharedPtr<FHeyZedLoadingView,ESPMode::ThreadSafe> LoadingView;
    double ExposureWindow=0,ExposureStableAt=0;
    float ReferenceExposure=0;
};
