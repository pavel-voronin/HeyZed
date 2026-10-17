#include "HeyZedWorldSubsystem.h"
#include "HeyZedPawn.h"
#include "HeyZedEntities.h"
#include "HeyZedLighting.h"
#include "HeyZedFurniture.h"
#include "HeyZedTerrain.h"
#include "HeyZedGameInstance.h"
#include "HeyZedPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

void UHeyZedWorldSubsystem::UpdateStartup(double Now)
{
    if(bScenePresented && (Now-LastReceive>2 || StartupEpoch!=Epoch))
    {
        bScenePresented=false;bStartupPrepared=false;StartupWarmup=0;
        if(auto* PC=Cast<AHeyZedPlayerController>(GetWorld()->GetFirstPlayerController()))PC->ReleasePointer();
    }
    if(!bScenePresented)
    {
        const auto* InitialController=GetWorld()->GetFirstPlayerController();
        const auto* Pawn=InitialController?InitialController->GetPawn<AHeyZedPawn>():nullptr;
        const FVector Position=Pawn?Pawn->GetActorLocation()*.01+Origin:FVector::ZeroVector;
        const FIntPoint Tile(FMath::FloorToInt(Position.X),FMath::FloorToInt(Position.Y));
        FHeyZedGroundCell Surface;
        const bool HasFloor=ArchitectureFloors.Contains(Tile)||BaseSurface(Tile,Surface)||(Terrain&&Terrain->HasSurface(Tile));
        FString Stage=TEXT("Waiting for PZ");
        if(LastReceive<=0)
        {
            if(BackendStartupStage==TEXT("preparing_resources"))Stage=TEXT("Preparing PZ resources");
            else if(bShared)Stage=TEXT("Loading PZ world");
        }
        else if(Now-LastReceive>2)Stage=TEXT("Waiting for PZ");
        else if(!bStartupProtocol)Stage=TEXT("PZ adapter update required");
        else if(bAdapterPreparedFailed)Stage=TEXT("PZ resource preparation failed");
        else if(!bOrigin)Stage=TEXT("Loading player state");
        else if(!HasFloor)Stage=TEXT("Loading ground");
        else if(bBackendEntered && (!Entities || !Entities->PlayerAppearanceReady()))Stage=TEXT("Loading character appearance");
        else if(bBackendEntered && (!Lighting || !Lighting->StartupReady()))Stage=TEXT("Loading lighting");
        else if(bBackendEntered && !GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->StartupReady())Stage=TEXT("Loading furniture");
        else if(auto* Partition=GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();Partition && !Partition->IsAllStreamingCompleted())Stage=TEXT("Loading world");
        else if(!QueuedConstruction.IsEmpty() || !GroundQueue.IsEmpty() || !GroundDecode.IsEmpty())Stage=TEXT("Preparing world geometry");
        else if(!bBackendEntered){Stage=TEXT("Initializing player");bStartupPrepared=true;}
        else
        {
            Stage=TEXT("Preparing scene");
            if(StartupWarmup==0)
            {
                StartupWarmup=Now;StartupWarmupFrame=GFrameCounter;
                if(auto* PC=GetWorld()->GetFirstPlayerController();PC&&PC->PlayerCameraManager)PC->PlayerCameraManager->SetGameCameraCutThisFrame();
                StartupRenderFence.BeginFence();
            }
            auto* Instance=GetWorld()->GetGameInstance<UHeyZedGameInstance>();
            if(Now-StartupWarmup>=2 && GFrameCounter-StartupWarmupFrame>=32 && StartupRenderFence.IsFenceComplete() && Instance && Instance->ExposureReady(StartupWarmup))
            {
                bStartupPrepared=true;
                if(bBackendEntered){bScenePresented=true;StartupEpoch=Epoch;StartupPresentedFrame=GFrameCounter;Stage=TEXT("Ready");}
                if(bScenePresented)UE_LOG(LogTemp,Display,TEXT("heyzed startup: scene presented epoch=%lld"),Epoch);
            }
        }
        if(Stage!=TEXT("Preparing scene") && Stage!=TEXT("Initializing player") && !bScenePresented){StartupWarmup=0;bStartupPrepared=false;}
        StartupStatus=MoveTemp(Stage);
    }
    if(bShared && bStartupProtocol && Now>=NextStartupSend)
    {
        const bool Presented=bScenePresented && GFrameCounter>StartupPresentedFrame+2;
        if(Memory.Offer(TEXT("commands"),FString::Printf(TEXT("L %llu %lld %d"),Session,Epoch,Presented?2:bStartupPrepared?1:0)))NextStartupSend=Now+.25;
    }
}
