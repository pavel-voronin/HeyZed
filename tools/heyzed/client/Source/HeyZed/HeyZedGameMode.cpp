#include "HeyZedGameMode.h"
#include "HeyZedPawn.h"
#include "HeyZedPlayerController.h"
#include "HeyZedOverlay.h"
#include "Engine/World.h"

AHeyZedGameMode::AHeyZedGameMode()
{
    DefaultPawnClass = AHeyZedPawn::StaticClass();
    PlayerControllerClass = AHeyZedPlayerController::StaticClass();
    HUDClass = AHeyZedHud::StaticClass();
}

void UHeyZedViewportClient::LostFocus(FViewport* InViewport)
{
    Super::LostFocus(InViewport);
    if (GetWorld()) if (auto* PC = Cast<AHeyZedPlayerController>(GetWorld()->GetFirstPlayerController())) PC->ReleasePointer();
}
