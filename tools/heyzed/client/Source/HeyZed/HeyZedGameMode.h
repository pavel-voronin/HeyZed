#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/GameViewportClient.h"
#include "HeyZedGameMode.generated.h"

UCLASS()
class AHeyZedGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AHeyZedGameMode();
};

UCLASS()
class UHeyZedViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual void LostFocus(FViewport* InViewport) override;
};
