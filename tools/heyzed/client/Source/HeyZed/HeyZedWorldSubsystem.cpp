#include "HeyZedWorldSubsystem.h"
#include "HeyZedVegetation.h"
#include "HeyZedFurniture.h"
#include "HeyZedMetrics.h"
#include "HeyZedArchitecture.h"
#include "HeyZedPawn.h"
#include "HeyZedMotion.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HeyZedPlayerController.h"
#include "HeyZedTerrain.h"
#include "HeyZedBakedRegion.h"
#include "HeyZedScene.h"
#include "HeyZedEntities.h"
#include "HeyZedLighting.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "Sockets.h"
#include "HAL/FileManager.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Parse.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Async/Async.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWindow.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "Misc/ScopeExit.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#endif

void UHeyZedWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FDynamicRHI::EnableIdealGPUCaptureOptions(false);
    int32 ArchitectureRange=0;
    if(!UWorldPartitionSubsystem::GetOverrideLoadingRange(TEXT("Architecture"),ArchitectureRange))UWorldPartitionSubsystem::SetOverrideLoadingRange(TEXT("Architecture"),6400);
    Session = (static_cast<uint64>(GetTypeHash(FGuid::NewGuid())) << 30 | GetTypeHash(FGuid::NewGuid())) & ((1ull << 62)-1);
    Socket = FUdpSocketBuilder(TEXT("heyzed backend")).AsNonBlocking().AsReusable().BoundToAddress(FIPv4Address::InternalLoopback).BoundToPort(0).WithReceiveBufferSize(4*1024*1024);
    Backend = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    bool Valid = false; Backend->SetIp(TEXT("127.0.0.1"), Valid); int32 Port = 28761; FParse::Value(FCommandLine::Get(), TEXT("HeyZedPort="), Port); Backend->SetPort(FMath::Clamp(Port, 1024, 65535)); bDatagramFixture=Port!=28761;
    IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HeyZed")), true);
    if (!Socket) UE_LOG(LogTemp, Error, TEXT("heyzed: could not create loopback transport"));
}

void UHeyZedWorldSubsystem::Deinitialize()
{
    if (Socket)
    {
        const FString Release = FString::Printf(TEXT("I2 %llu %llu 0 0 -135 -135 1 -"), Session, ++Sequence);
        FTCHARToUTF8 Bytes(*Release); int32 Sent = 0;
        if (bShared) Memory.Offer(TEXT("input"),Release);
        else if (bDatagramFixture) Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, *Backend);
        Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket); Socket = nullptr;
    }
    Super::Deinitialize();
}

namespace {
FIntPoint BaseRegionAddress(FIntPoint Address)
{
    return {int32(FMath::FloorToInt(Address.X/256.)),int32(FMath::FloorToInt(Address.Y/256.))};
}
}
void UHeyZedWorldSubsystem::RegisterBase(AHeyZedBakedRegion* Region)
{
    BaseRegions.Add(Region->Cell,Region);
    Region->Activate(!BaseIdentity.IsEmpty() && Region->Identity==BaseIdentity);
    if (const auto* Overrides=BaseOverrides.Find(Region->Cell)) for (FIntPoint Address : *Overrides) Region->Override(Address);
    if (Terrain) Terrain->BaseLoaded(Region->Cell);
}
void UHeyZedWorldSubsystem::UnregisterBase(AHeyZedBakedRegion* Region)
{
    if (auto* Found=BaseRegions.Find(Region->Cell)) if (Found->Get()==Region) BaseRegions.Remove(Region->Cell);
}
bool UHeyZedWorldSubsystem::BaseSurface(FIntPoint Address,FHeyZedGroundCell& Out) const
{
    const auto* Found=BaseRegions.Find(BaseRegionAddress(Address));
    const auto* Region=Found?Found->Get():nullptr;
    return Region && !BaseIdentity.IsEmpty() && Region->Identity==BaseIdentity && Region->Surface(Address,Out);
}
void UHeyZedWorldSubsystem::OverrideBase(FIntPoint Address)
{
    const FIntPoint Key=BaseRegionAddress(Address);
    BaseOverrides.FindOrAdd(Key).Add(Address);
    if (auto* Found=BaseRegions.Find(Key)) if (auto* Region=Found->Get()) Region->Override(Address);
}

void UHeyZedWorldSubsystem::ClaimArchitectureFloor(FIntPoint Address,bool Claimed,AHeyZedArchitecture* Owner)
{
    if(!Owner)return;
    if(Claimed){ArchitectureFloorOwners.Add(Address,Owner);ArchitectureFloors.Add(Address);OverrideBase(Address);}
    else
    {
        const auto* Existing=ArchitectureFloorOwners.Find(Address);
        if(!Existing||Existing->Get()!=Owner)return;
        ArchitectureFloorOwners.Remove(Address);ArchitectureFloors.Remove(Address);
    }
    if (Terrain) Terrain->SetArchitectureFloor(Address,Claimed);
}

void UHeyZedWorldSubsystem::SendNativeKey(int32 Code,bool Down)
{
    if(Code==-1)PendingNativeKeys.Reset();
    else if(PendingNativeKeys.Num()>=128)
    {
        PendingNativeKeys.Reset();
        PendingNativeKeys.Add(FString::Printf(TEXT("K %llu %llu -1 0"),Session,++NativeKeySequence));
    }
    PendingNativeKeys.Add(FString::Printf(TEXT("K %llu %llu %d %d"),Session,++NativeKeySequence,Code,Down?1:0));
}

void UHeyZedWorldSubsystem::ReceiveArchitecture(const TSharedRef<FJsonObject>& Block)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedWorld_ReceiveArchitecture);
    const auto Cell=Block->GetObjectField(TEXT("cell"));const auto& P=Cell->GetArrayField(TEXT("address"));
    const FString Address=FString::Printf(TEXT("%.0f,%.0f,%.0f"),P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());
    const int64 Revision=Block->GetNumberField(TEXT("revision"));auto& Change=ArchitectureChanges.FindOrAdd(Address);
    if (Change.Epoch==Epoch && Revision<=Change.Revision) return;
    Change.Epoch=Epoch;Change.Revision=Revision;Change.Cell=Cell;
    AHeyZedArchitecture::ApplyToLoaded(GetWorld(),Cell.ToSharedRef());
}
void UHeyZedWorldSubsystem::QueueArchitectureUpdate(AHeyZedArchitecture* Building)
{
    if(!IsValid(Building)||QueuedConstruction.Contains(Building))return;
    QueuedConstruction.Add(Building);ConstructionQueue.Enqueue(Building);
}
void UHeyZedWorldSubsystem::ArchitectureChanged(AHeyZedArchitecture* Building)
{
    if(IsValid(Scene))Scene->ArchitectureChanged(Building);
}
void UHeyZedWorldSubsystem::RestoreArchitecture(AHeyZedArchitecture* Building)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Restore);
    for (const auto& Pair : ArchitectureChanges) if (Pair.Value.Epoch==Epoch && Pair.Value.Cell) Building->Apply(Pair.Value.Cell.ToSharedRef());
}

void UHeyZedWorldSubsystem::ReceiveState(const FString& Packet)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedWorld_ReceiveState);
    TArray<FString> Fields; Packet.ParseIntoArrayWS(Fields);
    const bool CompactMotion = Fields.Num()==20 && Fields[0]==TEXT("S3");
    const bool MotionState = CompactMotion || (Fields.Num() == 22 && Fields[0] == TEXT("S2"));
    if ((!MotionState && (Fields.Num() != 16 || Fields[0] != TEXT("S"))) || FCString::Strtoui64(*Fields[3], nullptr, 10) != Session) return;
    const int64 NewEpoch = FCString::Atoi64(*Fields[1]), NewSequence = FCString::Atoi64(*Fields[2]);
    if (Epoch == NewEpoch && NewSequence <= StateSequence) return;
    const double ReceivedAt = FPlatformTime::Seconds();
    const int64 NativeSample=CompactMotion ? FCString::Atoi64(*Fields[16]) : 0;
    if (Epoch==NewEpoch && NativeSample>LastNativeSample && LastNativeSample>0 && NewSequence>StateSequence && StateSequence>=0)
        NativeIntervals.Add({ReceivedAt,(NativeSample-LastNativeSample)*1e-6/(NewSequence-StateSequence)});
    else if (Epoch!=NewEpoch) NativeIntervals.Reset();
    LastNativeSample=NativeSample;
    if (Epoch == NewEpoch && StateSequence >= 0)
    {
        SkippedStates += FMath::Max<int64>(0, NewSequence-StateSequence-1);
        StateGaps.Add({ReceivedAt, (ReceivedAt-LastReceive)*1000});
    }
    else
    {
        LastAck = SkippedStates = ReceivedStates = 0;
        AckTimes.Reset(); InputAges.Reset(); StateGaps.Reset();
    }
    ++ReceivedStates;
    const uint64 Ack = FCString::Strtoui64(*Fields[4], nullptr, 10);
    const uint64 Echo = FCString::Strtoui64(*Fields[5], nullptr, 10);
    if (Ack > LastAck && Ack <= Sequence && Echo > 0)
    {
        const double Rtt = (ReceivedAt-static_cast<double>(Echo)*1e-9)*1000;
        if (Rtt >= 0) AckTimes.Add({ReceivedAt, Rtt});
        InputAges.Add({ReceivedAt, FMath::Max(0., FCString::Atod(*Fields[14])*.001)});
        LastAck = Ack;
    }
    const FVector Position(FCString::Atod(*Fields[7]), FCString::Atod(*Fields[8]), FCString::Atod(*Fields[9]));
    if (!bOrigin)
    {
        Origin = FVector::ZeroVector; bOrigin = true; GroundRevision = -1;
        NextTest = FPlatformTime::Seconds()+10;

    }
    Epoch = NewEpoch; if (Lighting) Lighting->SetEpoch(Epoch); StateSequence = NewSequence; LastReceive = ReceivedAt;
    if (auto* PC = Cast<AHeyZedPlayerController>(GetWorld()->GetFirstPlayerController()))
    {
        if (auto* Pawn = PC->GetPawn<AHeyZedPawn>())
        {
            const FVector Local((Position.X-Origin.X)*100, (Position.Y-Origin.Y)*100, (Position.Z-Origin.Z)*HeyZedMetrics::Floor);
            const float Facing = FMath::Atan2(FCString::Atof(*Fields[11]), FCString::Atof(*Fields[10]));
            const float Yaw = FMath::RadiansToDegrees(Facing) - 90;
            const FVector Velocity = MotionState ? FVector(FCString::Atod(*Fields[17])*100,FCString::Atod(*Fields[18])*100,0) : FVector::ZeroVector;
            Pawn->DesiredFacingYaw = FMath::RadiansToDegrees(FMath::Atan2(FCString::Atof(*Fields[11]),FCString::Atof(*Fields[10])));
            Pawn->Adopt(Local, Yaw, FCString::Atoi(*Fields[12]), Velocity,
                0,
                MotionState && FCString::Atoi(*Fields[CompactMotion ? 19 : 21]) != 0, MotionState);
        }
        if (BindingSignature != Fields[15])
        {
            BindingSignature = Fields[15]; TArray<FString> Names; BindingSignature.ParseIntoArray(Names, TEXT(","));
            TArray<int32> Keys;
            for (const FString& Name : Names)
            {
                int32 Code = FCString::Atoi(*Name);
                switch (Code)
                {
                    case 340: Code=160; break; case 344: Code=161; break;
                    case 341: Code=162; break; case 345: Code=163; break;
                    case 342: Code=164; break; case 346: Code=165; break;
                    case 265: Code=38; break; case 264: Code=40; break; case 263: Code=37; break; case 262: Code=39; break;
                }
                Keys.Add(Code);
            }
            PC->MapMovementKeys(Keys);
        }
    }
}

void UHeyZedWorldSubsystem::ReceiveGround(const uint8* Bytes, int32 Length)
{
    if (Length < 9 || Bytes[0]!='P' || Bytes[1]!='J' || Bytes[2]!='G' || Bytes[3]!='1') return;
    const uint32 Size = static_cast<uint32>(Bytes[4])<<24 | static_cast<uint32>(Bytes[5])<<16 | static_cast<uint32>(Bytes[6])<<8 | Bytes[7];
    if (Size == 0 || Size > 8*1024*1024) return;
    TArray<uint8> Raw; Raw.SetNumUninitialized(Size+1);
    if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Size, Bytes+8, Length-8)) return;
    Raw[Size] = 0;
    const FString Text = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Raw.GetData()));
    TSharedPtr<FJsonObject> Snapshot;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Snapshot) || !Snapshot) return;
    if ((Snapshot->GetIntegerField(TEXT("schema")) != 1 && Snapshot->GetIntegerField(TEXT("schema")) != 2) || Snapshot->GetNumberField(TEXT("epoch")) != Epoch) return;
    const int64 Revision = static_cast<int64>(Snapshot->GetNumberField(TEXT("revision")));
    if (Revision <= GroundRevision) return;
    GroundRevision = Revision; GroundQueue.Add(FHeyZedPreparedGround::Prepare(Snapshot.ToSharedRef()));
}

void UHeyZedWorldSubsystem::Tick(float Delta)
{
    const double WorkStart=FPlatformTime::Seconds();
    ON_SCOPE_EXIT {TickWorkMs=(FPlatformTime::Seconds()-WorkStart)*1000;};
    auto* PC = Cast<AHeyZedPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Pawn = PC ? PC->GetPawn<AHeyZedPawn>() : nullptr;
    if (!Socket || !Pawn) return;
    if (!Lighting)
    {
        Lighting=GetWorld()->SpawnActor<AHeyZedLighting>();
        Lighting->SetEpoch(Epoch); Lighting->SetIdentity(BaseIdentity);
        for (auto& Weak : LightRegions) if (auto* Region=Weak.Get()) Lighting->RegisterRegion(Region);
    }
    const double Now = FPlatformTime::Seconds();
    if (LastTick > 0) FrameTimes.Add({Now, (Now-LastTick)*1000});
    LastTick = Now;
    int32 HeyZedPort = 28761; FParse::Value(FCommandLine::Get(),TEXT("HeyZedPort="),HeyZedPort);
    bShared = HeyZedPort==28761 && Memory.Refresh();
    if (bShared && MemoryGeneration!=Memory.Generation)
    {
        MemoryGeneration = Memory.Generation;
        LastReceive=0;BackendStartupStage.Reset();
        bScenePresented=false;bStartupPrepared=false;bStartupProtocol=false;bBackendLoading=true;bBackendEntered=false;StartupWarmup=0;
        PendingNativeKeys.Reset();SendNativeKey(-1,false);
        GroundQueue.Reset(); GroundDecode.Reset(); GroundRevision=-1;
        Memory.Offer(TEXT("commands"),FString::Printf(TEXT("R %llu"),Session));
    }
    if (GameplayReady() && bOrigin && FParse::Param(FCommandLine::Get(),TEXT("HeyZedStreetProbe")))
    {
        bool StreamingReady=true;
        for(auto* Level:GetWorld()->GetStreamingLevels())if(Level && Level->ShouldBeLoaded()&&!Level->IsLevelVisible())StreamingReady=false;
        const bool Ready=StreamingReady&&Now-LastReceive<.15;
        if(LiveTestStarted>Now&&!StreamingReady)LiveTestStarted=0;
        if(LiveTestStarted==0 && Ready){double Warmup=10;FParse::Value(FCommandLine::Get(),TEXT("HeyZedStreetProbeWarmupSeconds="),Warmup);LiveTestStarted=Now+FMath::Clamp(Warmup,10.,300.);}
        if(LiveTestStarted!=0)
        {
            double Elapsed=Now-LiveTestStarted;
            double Duration=4;FParse::Value(FCommandLine::Get(),TEXT("HeyZedStreetProbeSeconds="),Duration);Duration=FMath::Clamp(Duration,1.,12.);
            const bool VehicleProbe=FParse::Param(FCommandLine::Get(),TEXT("HeyZedVehicleProbe"));
            double Approach=14,DriveSeconds=3;FParse::Value(FCommandLine::Get(),TEXT("HeyZedVehicleApproachSeconds="),Approach);FParse::Value(FCommandLine::Get(),TEXT("HeyZedVehicleDriveSeconds="),DriveSeconds);
            Approach=FMath::Clamp(Approach,0.,30.);DriveSeconds=FMath::Clamp(DriveSeconds,1.,6.);
            double GoalX=0,GoalY=0;const bool HasGoal=VehicleProbe&&FParse::Value(FCommandLine::Get(),TEXT("HeyZedVehicleTargetX="),GoalX)&&FParse::Value(FCommandLine::Get(),TEXT("HeyZedVehicleTargetY="),GoalY);
            const FVector NativePosition=Origin+Pawn->GetActorLocation()/100.;
            if(HasGoal&&Elapsed>=0&&Elapsed<Approach&&FVector2D(NativePosition.X-GoalX,NativePosition.Y-GoalY).Size()<.7){TestPhase=1;LiveTestStarted=Now-Approach;Elapsed=Approach;}
            if(VehicleProbe&&Elapsed>=0&&LiveTestStage<=0&&Pawn->bNativeSeated){TestPhase=1;LiveTestStarted=Now-Approach-2.15;Elapsed=Approach+2.15;}
            const int32 FinalStage=VehicleProbe?6:4;
            int32 Stage=VehicleProbe?(Elapsed<0?0:Elapsed<Approach?1:Elapsed<Approach+.15?2:Elapsed<Approach+2.15?3:Elapsed<Approach+2.15+DriveSeconds?4:Elapsed<Approach+5.15+DriveSeconds?5:6):(Elapsed<0?0:Elapsed<Duration?1:Elapsed<Duration+1?2:Elapsed<Duration*2+1?3:4);
            if(HasGoal&&Elapsed>=Approach&&TestPhase==0)Stage=FinalStage;
            if(Stage==1&&LiveTestStage==0&&FParse::Param(FCommandLine::Get(),TEXT("HeyZedProbeActivateWindow")))
                if(GEngine&&GEngine->GameViewport)if(auto Window=GEngine->GameViewport->GetWindow())Window->BringToFront(true);
            if(LiveTestStage<FinalStage)PC->InjectVerificationMotion(Stage==1?17:!VehicleProbe&&Stage==3?18:0);
            if(HasGoal&&Stage==1)PC->SetControlRotation(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(GoalY-NativePosition.Y,GoalX-NativePosition.X)),0));
            if(VehicleProbe&&Stage!=LiveTestStage){
                SendNativeKey(-1,false);
                if(Stage==2)SendNativeKey(69,true);
                if(Stage==4&&Pawn->bNativeSeated){TArray<FString> Keys;BindingSignature.ParseIntoArray(Keys,TEXT(","));if(!Keys.IsEmpty())SendNativeKey(FCString::Atoi(*Keys[0]),true);}
                if(Stage==5&&Pawn->bNativeSeated)SendNativeKey(32,true);
                UE_LOG(LogTemp,Display,TEXT("heyzed vehicle probe stage=%d seated=%d target_speed=%.3f"),Stage,Pawn->bNativeSeated,Pawn->NativeVelocity.Size2D());
            }
            if(Stage!=LiveTestStage){TRACE_BOOKMARK(TEXT("HeyZedStreetProbe stage=%d"),Stage);UE_LOG(LogTemp,Display,TEXT("heyzed street probe stage=%d"),Stage);}
            if(Stage==FinalStage && LiveTestStage!=FinalStage)
            {
                PC->ReleasePointer();
                if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedStopTraceAfterProbe")))PC->ConsoleCommand(TEXT("Trace.Stop"),false);
                if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedReviewFrame")))FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/profile-review.png")),true,false);
                if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedExitAfterProbe")))
                {
                    FTimerHandle ExitHandle;
                    GetWorld()->GetTimerManager().SetTimer(ExitHandle,[](){FPlatformMisc::RequestExit(false);},2.f,false);
                }
            }
            LiveTestStage=Stage;
        }
    }
    else if (bOrigin && FParse::Param(FCommandLine::Get(),TEXT("HeyZedLiveMotionTest")))
    {
        if (LiveTestStarted==0) LiveTestStarted=Now;
        struct FStep { double Duration; int32 Mask; };
        const FStep Steps[]={{2,0},{2,1},{.8,0},{2,17},{.8,0},{2,34},{.8,0},{2,33},{.8,0},{.15,64},{2,1},{.15,64},{2,0}};
        double Remaining=Now-LiveTestStarted; int32 Mask=0; LiveTestStage=UE_ARRAY_COUNT(Steps);
        for (int32 I=0;I<UE_ARRAY_COUNT(Steps);++I) { if (Remaining<Steps[I].Duration) { Mask=Steps[I].Mask; LiveTestStage=I; break; } Remaining-=Steps[I].Duration; }
        PC->InjectVerificationMotion(Mask);
    }
    if (Now >= NextSend && (bShared || HeyZedPort!=28761))
    {
        const int32 Intent=PC->IntentMask();
        // The existing adapter's mode 0 forces walking facing to aim. Moving in
        // first person must instead keep vanilla directional facing in PZ.
        const int32 NativeFacingMode=Pawn->CameraMode==0 && (Intent&15)!=0 ? 1 : Pawn->CameraMode;
        const FString Packet = FString::Printf(TEXT("I3 %llu %llu %llu %d %.5f %.5f %d %d"), Session, ++Sequence,
            static_cast<uint64>(Now*1e9), Intent, Pawn->MovementYaw(), PC->ActionYaw(), NativeFacingMode,PC->PointerReleased()?0:1);
        FTCHARToUTF8 Bytes(*Packet); int32 Sent = 0;
        if (bShared ? Memory.Offer(TEXT("input"),Packet) : Socket->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Sent, *Backend)) Traffic.Add({Now, bShared ? Bytes.Length() : Sent, true});
        else ++SendErrors;
        NextSend = Now + 1./60;
    }
    if(bShared)while(!PendingNativeKeys.IsEmpty())
    {
        if(!Memory.Offer(TEXT("commands"),PendingNativeKeys[0]))break;
        PendingNativeKeys.RemoveAt(0);
    }
    uint8 Buffer[65536];
    auto Peer = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    for (int32 Count = 0; Count < 256; ++Count)
    {
        int32 Length = 0;
        if (!Socket->RecvFrom(Buffer, sizeof(Buffer)-1, Length, *Peer) || Length <= 0) break;
        if (Peer->GetPort() != Backend->GetPort() || Peer->ToString(false) != TEXT("127.0.0.1")) continue;
        Traffic.Add({FPlatformTime::Seconds(), Length, false});
        if (Length > 4 && Buffer[0]=='P')
        {
            const double Start = FPlatformTime::Seconds(); ReceiveGround(Buffer, Length);
            GroundDecodeMs = (FPlatformTime::Seconds()-Start)*1000;
        }
        else { Buffer[Length] = 0; ReceiveState(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer))); }
    }
    if (bShared)
    {
        if (!Scene) Scene=GetWorld()->SpawnActor<AHeyZedScene>();
        if (!Entities) Entities=GetWorld()->SpawnActor<AHeyZedEntities>();
        FString Packet,Latest;
        const double MotionStart=FPlatformTime::Seconds();
        if (Memory.PollLatest(TEXT("motion"),Packet))
        {
            if (Packet.StartsWith(TEXT("S"))) Latest=MoveTemp(Packet);
            else
            {
                TSharedPtr<FJsonObject> Frame;
                if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Packet),Frame) && Frame.IsValid() && Frame->GetStringField(TEXT("type"))==TEXT("startup"))
                    Frame->TryGetStringField(TEXT("stage"),BackendStartupStage);
                else if(Frame.IsValid() && Frame->GetStringField(TEXT("type"))==TEXT("frame"))
                {
                    double StartupSchema=0;Frame->TryGetNumberField(TEXT("startup_schema"),StartupSchema);bStartupProtocol=StartupSchema==3;
                    Frame->TryGetBoolField(TEXT("startup_loading"),bBackendLoading);
                    bool Entered=false;Frame->TryGetBoolField(TEXT("startup_entered"),Entered);
                    if(Entered&&!bBackendEntered){bStartupPrepared=false;StartupWarmup=0;}bBackendEntered=Entered;
                    Latest=Frame->GetStringField(TEXT("player"));
                    FString Identity; Frame->TryGetStringField(TEXT("world_id"),Identity);
                    if (Identity!=BaseIdentity)
                    {
                        BaseIdentity=Identity; if (Lighting) Lighting->SetIdentity(Identity);
                        for (auto& Pair : BaseRegions) if (auto* Region=Pair.Value.Get())
                        {
                            Region->Activate(!BaseIdentity.IsEmpty() && Region->Identity==BaseIdentity);
                            if (Terrain) Terrain->BaseLoaded(Region->Cell);
                        }
                    }
                    ExtractUs=Frame->GetNumberField(TEXT("extract_us")); PublishUs=Frame->GetNumberField(TEXT("publish_us"));
                    Frame->TryGetNumberField(TEXT("objects_us"),AdapterObjectsUs);Frame->TryGetNumberField(TEXT("lighting_us"),AdapterLightingUs);
                    Frame->TryGetNumberField(TEXT("entities_us"),AdapterEntitiesUs);Frame->TryGetNumberField(TEXT("ground_us"),AdapterGroundUs);Frame->TryGetNumberField(TEXT("architecture_us"),AdapterArchitectureUs);
                    Frame->TryGetNumberField(TEXT("input_expirations"),AdapterInputExpirations);Frame->TryGetNumberField(TEXT("input_step_age_us"),AdapterInputStepAgeUs);
                    Frame->TryGetNumberField(TEXT("control_gap_max_us"),AdapterControlGapMaxUs);Frame->TryGetNumberField(TEXT("prepared_work_max_us"),AdapterPreparedWorkMaxUs);Frame->TryGetBoolField(TEXT("prepared_failed"),bAdapterPreparedFailed);
                    const TSharedPtr<FJsonObject>* Motion=nullptr;
                    if (bOrigin && Frame->TryGetObjectField(TEXT("motion"),Motion)) Scene->Apply(Motion->ToSharedRef(),Origin);
                }
            }
        }
        if (!Latest.IsEmpty()) { Traffic.Add({Now,Latest.Len(),false}); ReceiveState(Latest); }
        MotionApplyMs=(FPlatformTime::Seconds()-MotionStart)*1000;
        if(Epoch==FCString::Atoi64(*Memory.Generation))
        {
            Entities->SetEpoch(Epoch);
            const double EntityStart=FPlatformTime::Seconds();
            while(FPlatformTime::Seconds()-EntityStart<.002 && Memory.Poll(TEXT("entities"),Packet))
            {
                Packet.ReplaceInline(TEXT("\"Paint\":"),TEXT("\"NativePaintMap\":"),ESearchCase::CaseSensitive);
                Packet.ReplaceInline(TEXT("\"Rust\":"),TEXT("\"NativeRustMap\":"),ESearchCase::CaseSensitive);
                TSharedPtr<FJsonObject> Event;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Packet),Event)&&Event)Entities->Apply(Event.ToSharedRef());
                Traffic.Add({Now,FTCHARToUTF8(*Packet).Length(),false});
            }
            {
            TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_PoseBatch);
            const double PoseStart=FPlatformTime::Seconds();TArray<uint8> PoseBytes;
            while(FPlatformTime::Seconds()-PoseStart<.002 && Memory.PollBytes(TEXT("poses"),PoseBytes))
            {
                FHeyZedEntityPose Pose;FString Error;
                if(ReadHeyZedEntityPose(PoseBytes,Pose,Error))Entities->ApplyPose(MoveTemp(Pose));
                else if(++InvalidPosePackets<=3)UE_LOG(LogTemp,Error,TEXT("heyzed pose packet rejected: %s"),*Error);
                Traffic.Add({Now,PoseBytes.Num(),false});
            }
            }
        }
        GetWorld()->GetSubsystem<UHeyZedVegetationSubsystem>()->Configure(Epoch,BaseIdentity,Origin);
        GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->Configure(Epoch,BaseIdentity,Origin);
        const double EventStart=FPlatformTime::Seconds();
        while (bOrigin && FPlatformTime::Seconds()-EventStart<.001 && Memory.Poll(TEXT("events"),Packet))
        {
            TSharedPtr<FJsonObject> Event;
            if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Packet),Event))
            {
                FString Type; Event->TryGetStringField(TEXT("type"),Type);
                if ((Type==TEXT("environment") || Type==TEXT("light")) && Lighting) Lighting->Apply(Event.ToSharedRef());
                else if(Type==TEXT("furniture"))GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->Apply(Event.ToSharedRef());
                else Scene->Apply(Event.ToSharedRef(),Origin);
            }
        }
        EventApplyMs=(FPlatformTime::Seconds()-EventStart)*1000;
        if (Now>=NextCommandRetry)
        {
            for (const auto& Command : Scene->RetryCommands()) Memory.Offer(TEXT("commands"),Command);
            NextCommandRetry=Now+.1;
        }
        if (FParse::Param(FCommandLine::Get(),TEXT("HeyZedInteractionTest")) && Scene->AppliedEvents>0 && Now>=NextTest)
        {
            InteractDoor(); NextTest=Now+5;
        }
        while (bOrigin && GroundDecode.Num()+GroundQueue.Num()<32 && Memory.Poll(TEXT("world"),Packet))
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedWorld_ReceivePreparedBlock);
            if (Packet.StartsWith(TEXT("V1 ")))
            {
                TSharedPtr<FJsonObject> Block;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Packet.Mid(3)),Block))
                    GetWorld()->GetSubsystem<UHeyZedVegetationSubsystem>()->Apply(Block.ToSharedRef());
                continue;
            }
            if (Packet.StartsWith(TEXT("A1 ")))
            {
                TSharedPtr<FJsonObject> Block;
                if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Packet.Mid(3)),Block) && Block->GetNumberField(TEXT("epoch"))==Epoch)
                {
                    ReceiveArchitecture(Block.ToSharedRef());
                }
                continue;
            }
            GroundDecode.Add(Async(EAsyncExecution::ThreadPool,[Text=MoveTemp(Packet)](){ return FHeyZedPreparedGround::Decode(Text); }));
        }
    }
    for (int32 I=GroundDecode.Num()-1;I>=0;--I) if (GroundDecode[I].IsReady())
    {
        auto Prepared=GroundDecode[I].Get(); GroundDecode.RemoveAtSwap(I);
        if (Prepared && Prepared->Epoch==Epoch)
        {
            GroundDecodeMs=Prepared->DecodeMs; GroundRevision=FMath::Max(GroundRevision,Prepared->Revision);
            GroundQueue.Add(MoveTemp(Prepared));
        }
    }
    if (bOrigin)
    {
        if (!Terrain)
        {
            Terrain=GetWorld()->SpawnActor<AHeyZedTerrain>();
            for (FIntPoint Address : ArchitectureFloors) Terrain->SetArchitectureFloor(Address,true);
        }
        if (!bBaseStarted)
        {
            bBaseStarted=true;
            FString Pack,Identity;
            FParse::Value(FCommandLine::Get(),TEXT("HeyZedWorldPack="),Pack);
            FParse::Value(FCommandLine::Get(),TEXT("HeyZedWorldIdentity="),Identity);
            if (!Pack.IsEmpty())
                if (auto* Library=LoadObject<UHeyZedGroundLibrary>(nullptr,TEXT("/Game/HeyZed/GroundLibrary.GroundLibrary"))) WorldBase.Begin(Pack,Identity,Library->Recipe);
        }
        const FVector NativePlayer(Origin.X+Pawn->GetActorLocation().X/100,Origin.Y+Pawn->GetActorLocation().Y/100,Origin.Z);
        GroundQueue.Append(WorldBase.Tick(NativePlayer));
    }
    if (!GroundQueue.IsEmpty() && bOrigin)
    {
        if (!Terrain)
        {
            Terrain=GetWorld()->SpawnActor<AHeyZedTerrain>();
            for (FIntPoint Address : ArchitectureFloors) Terrain->SetArchitectureFloor(Address,true);
        }
        const FVector Player=Pawn->GetActorLocation();
        GroundQueue.Sort([&](const auto& A,const auto& B)
        {
            auto Priority=[&](const auto& Block)
            {
                const FVector Position((Block->Center.X-Origin.X)*100,(Block->Center.Y-Origin.Y)*100,0);
                return FVector::DistSquared2D(Position,Player);
            };
            return Priority(A)<Priority(B);
        });
        const double Start=FPlatformTime::Seconds();
        do
        {
            auto Patch=GroundQueue[0]; GroundQueue.RemoveAt(0);
            if (Patch && (Patch->bBaked || Patch->Epoch==Epoch)) Terrain->Apply(*Patch,Origin);
        } while (!GroundQueue.IsEmpty() && FPlatformTime::Seconds()-Start<.002);
        GroundApplyMs=(FPlatformTime::Seconds()-Start)*1000;
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_QueuedUpdates);
        const double Started=FPlatformTime::Seconds();TWeakObjectPtr<AHeyZedArchitecture> Entry;
        TSet<FIntVector> ChangedCells;
        while(FPlatformTime::Seconds()-Started<.002&&ConstructionQueue.Dequeue(Entry))
        {
            QueuedConstruction.Remove(Entry);
            if(auto* Building=Entry.Get())
            {
                bool Changed=false;FIntVector ChangedCell;
                if(Building->UpdateConstruction(Changed,ChangedCell))QueueArchitectureUpdate(Building);
                if(Changed)ChangedCells.Add(ChangedCell);
            }
        }
        if(IsValid(Scene))for(const auto& Changed:ChangedCells)Scene->ConstructionChanged(Changed);
        ConstructionUpdateMs=(FPlatformTime::Seconds()-Started)*1000;
    }
    UpdateStartup(Now);
    if (Now >= NextHealth)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedDiagnostics_HealthSnapshot);
        NextHealth = Now+.25;
        auto Health = MakeShared<FJsonObject>();
        Health->SetStringField(TEXT("startup_stage"),StartupStatus);
        Health->SetBoolField(TEXT("scene_presented"),bScenePresented);
        Health->SetBoolField(TEXT("startup_loading"),bBackendLoading);
        Health->SetObjectField(TEXT("furniture"),GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->Diagnostics());
        Health->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
        Health->SetNumberField(TEXT("state_seq"), StateSequence);
        Health->SetNumberField(TEXT("state_age"), LastReceive > 0 ? FMath::Max(0., FPlatformTime::Seconds()-LastReceive) : -1);
        PublishTelemetry(FPlatformTime::Seconds(), Health);
        PublishRenderTelemetry(Health);
        Health->SetNumberField(TEXT("live_test_stage"),LiveTestStage);
        Health->SetNumberField(TEXT("construction_queue_buildings"),QueuedConstruction.Num());
        Health->SetNumberField(TEXT("construction_update_ms"),ConstructionUpdateMs);
        Health->SetNumberField(TEXT("camera_mode"), Pawn->CameraMode);
        Health->SetNumberField(TEXT("camera_yaw"), Pawn->MovementYaw());
        Health->SetNumberField(TEXT("camera_pitch"), PC->GetControlRotation().Pitch);
        Health->SetBoolField(TEXT("pointer_released"), PC->PointerReleased());
        auto Locomotion = MakeShared<FJsonObject>();
        Locomotion->SetNumberField(TEXT("actor_z"),Pawn->GetActorLocation().Z);
        const FVector FootL=Pawn->Body->GetBoneLocation(TEXT("foot_l")),FootR=Pawn->Body->GetBoneLocation(TEXT("foot_r"));
        Locomotion->SetNumberField(TEXT("foot_l_z"),FootL.Z); Locomotion->SetNumberField(TEXT("foot_r_z"),FootR.Z);
        Locomotion->SetBoolField(TEXT("confirmed_motion"), Pawn->bConfirmedMotion);
        Locomotion->SetNumberField(TEXT("native_speed_cm_s"), Pawn->NativeVelocity.Size2D());
        Locomotion->SetNumberField(TEXT("visible_speed_cm_s"), Pawn->ActualSpeed);
        Locomotion->SetNumberField(TEXT("native_turn_deg_s"), Pawn->NativeTurnRate);
        Locomotion->SetNumberField(TEXT("gait"), Pawn->Gait);
        Locomotion->SetBoolField(TEXT("crouched"), Pawn->bCrouched);
        if (const auto* Animation = Cast<UHeyZedMotionInstance>(Pawn->Body->GetAnimInstance()))
        {
            Locomotion->SetBoolField(TEXT("motion_matching"),Animation->bMatchingValid);
            Locomotion->SetStringField(TEXT("animation_group"),Animation->SelectedGroup.ToString());
            Locomotion->SetStringField(TEXT("animation_clip"),Animation->SelectedClip);
            Locomotion->SetNumberField(TEXT("stride_ratio"),Animation->StrideRatio);
            Locomotion->SetNumberField(TEXT("playback_rate"),Animation->PlaybackRate);
            Locomotion->SetBoolField(TEXT("foot_correction"),false);
        }
        Health->SetObjectField(TEXT("locomotion"), Locomotion);
        TArray<TSharedPtr<FJsonValue>> Presented;
        if (Scene) for (const auto& Pair : Scene->Objects)
        {
            const auto& Object=Pair.Value;
            if(!(Presented.Num()<64&&FVector::DistSquared(Object.Confirmed,Pawn->GetActorLocation())<4000000))continue;
            auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("id"),Object.Id); Row->SetNumberField(TEXT("generation"),Object.Generation);
            Row->SetNumberField(TEXT("revision"),Object.Revision); Row->SetStringField(TEXT("kind"),Object.Kind);
            Row->SetBoolField(TEXT("open"),Object.Open); Row->SetBoolField(TEXT("pending"),Object.Pending);
            Row->SetBoolField(TEXT("deleted"),Object.Deleted); Row->SetNumberField(TEXT("visual_yaw"),Object.VisualYaw);
            Row->SetNumberField(TEXT("local_effect"),Object.LocalEffect);
            Row->SetNumberField(TEXT("mesh_id"),Object.Mesh ? Object.Mesh->GetUniqueID() : 0);
            Row->SetNumberField(TEXT("x"),Object.Confirmed.X*.01+Origin.X); Row->SetNumberField(TEXT("y"),Object.Confirmed.Y*.01+Origin.Y);
            Row->SetNumberField(TEXT("z"),FMath::RoundToInt(Object.Confirmed.Z/HeyZedMetrics::Floor+Origin.Z));
            Row->SetBoolField(TEXT("north"),FMath::IsNearlyZero(Object.BaseYaw));
            Presented.Add(MakeShared<FJsonValueObject>(Row));
        }
        Health->SetArrayField(TEXT("presented_objects"),Presented);
        Health->SetNumberField(TEXT("objects"),Scene ? Scene->Objects.Num() : 0);
        Health->SetNumberField(TEXT("object_events"),Scene ? Scene->AppliedEvents : 0);
        Health->SetNumberField(TEXT("door_owner_searches"),Scene ? Scene->DoorOwnerSearches : 0);
        Health->SetNumberField(TEXT("door_pose_applications"),Scene ? Scene->DoorPoseApplications : 0);
        Health->SetNumberField(TEXT("entity_motion"),Entities ? Entities->Poses : 0);
        Health->SetNumberField(TEXT("entity_pose_visibility_skipped"),Entities ? Entities->PoseVisibilitySkipped : 0);
        Health->SetNumberField(TEXT("entity_pose_visibility_refreshed"),Entities ? Entities->PoseVisibilityRefreshed : 0);
        Health->SetNumberField(TEXT("entity_root_updates"),Entities ? Entities->RootPoseUpdates : 0);
        Health->SetNumberField(TEXT("entity_root_visibility_skipped"),Entities ? Entities->RootPoseSkipped : 0);
        Health->SetNumberField(TEXT("native_entities"),Entities ? Entities->Count() : 0);
        Health->SetNumberField(TEXT("visible_entities"),Entities ? Entities->ReadyCount() : 0);
        Health->SetNumberField(TEXT("entity_changes"),Entities ? Entities->Changes : 0);
        Health->SetNumberField(TEXT("entity_missing_models"),Entities ? Entities->MissingModels : 0);
        for(const TCHAR* Kind:{TEXT("player"),TEXT("zombie"),TEXT("animal"),TEXT("vehicle")}){Health->SetNumberField(FString(TEXT("entities_"))+Kind,Entities?Entities->KindCount(Kind):0);Health->SetNumberField(FString(TEXT("ready_"))+Kind,Entities?Entities->KindCount(Kind,true):0);Health->SetNumberField(FString(TEXT("onscreen_"))+Kind,Entities?Entities->RenderedCount(Kind):0);}
        Health->SetBoolField(TEXT("player_native_appearance"),Pawn->bNativeAppearance);
        if(Entities)Entities->AppendResourceHealth(*Health);
        Health->SetNumberField(TEXT("entity_state_queue"),Memory.Depth(TEXT("entities")));
        Health->SetNumberField(TEXT("entity_pose_queue"),Memory.Depth(TEXT("poses")));
        Health->SetNumberField(TEXT("entity_pose_queue_wait_ms"),Memory.WaitMs(TEXT("poses")));
        Health->SetStringField(TEXT("entity_pose_format"),TEXT("matrix-f32le-v1"));
        Health->SetNumberField(TEXT("entity_invalid_pose_packets"),InvalidPosePackets);
        Health->SetNumberField(TEXT("command_ack_ms"),Scene ? Scene->AckMs : 0);
        Health->SetStringField(TEXT("transport"),bShared ? TEXT("shared_memory") : bDatagramFixture ? TEXT("udp_fixture") : TEXT("shared_memory_waiting"));
        Health->SetNumberField(TEXT("pz_ground_extract_us"),ExtractUs);
        Health->SetNumberField(TEXT("memory_publish_us"),PublishUs);
        Health->SetNumberField(TEXT("pz_input_expirations"),AdapterInputExpirations);Health->SetNumberField(TEXT("pz_input_step_age_us"),AdapterInputStepAgeUs);
        Health->SetNumberField(TEXT("pz_control_gap_max_us"),AdapterControlGapMaxUs);Health->SetNumberField(TEXT("pz_prepared_work_max_us"),AdapterPreparedWorkMaxUs);Health->SetBoolField(TEXT("pz_prepared_failed"),bAdapterPreparedFailed);
        Health->SetNumberField(TEXT("pz_objects_us"),AdapterObjectsUs);Health->SetNumberField(TEXT("pz_lighting_us"),AdapterLightingUs);
        Health->SetNumberField(TEXT("pz_entities_us"),AdapterEntitiesUs);Health->SetNumberField(TEXT("pz_ground_us"),AdapterGroundUs);Health->SetNumberField(TEXT("pz_architecture_us"),AdapterArchitectureUs);
        Health->SetNumberField(TEXT("event_apply_ms"),EventApplyMs);
        Health->SetNumberField(TEXT("motion_queue_wait_ms"),Memory.WaitMs(TEXT("motion")));
        Health->SetNumberField(TEXT("event_queue_wait_ms"),Memory.WaitMs(TEXT("events")));
        Health->SetNumberField(TEXT("world_queue_wait_ms"),Memory.WaitMs(TEXT("world")));
        Health->SetNumberField(TEXT("world_blocks_consumed"),Memory.Consumed(TEXT("world")));
        Health->SetNumberField(TEXT("world_queue"),Memory.Depth(TEXT("world")));
        Health->SetNumberField(TEXT("rebuilt_cells"),Terrain ? Terrain->RebuiltCells : 0);
        Health->SetNumberField(TEXT("ground_revision"), GroundRevision);
        Health->SetStringField(TEXT("world_base_status"),WorldBase.Status);
        Health->SetStringField(TEXT("baked_world_identity"),BaseIdentity);
        Health->SetNumberField(TEXT("baked_regions_resident"),BaseRegions.Num());
        Health->SetNumberField(TEXT("baked_override_regions"),BaseOverrides.Num());
        Health->SetNumberField(TEXT("world_base_pages"),WorldBase.LoadedPages);
        if (WorldBase.Status!=TEXT("NONE")) MetricsText+=FString::Printf(TEXT("\nWorld base %s | pages %llu"),*WorldBase.Status,WorldBase.LoadedPages);
        MetricsText += FString::Printf(TEXT("\n%s | queues M/E/W %llu/%llu/%llu\nQueue wait M/E/W %.0f/%.0f/%.0f ms | cmd %.1f ms"),bShared?TEXT("Shared memory"):bDatagramFixture?TEXT("UDP fixture"):TEXT("Shared memory waiting"),Memory.Depth(TEXT("motion")),Memory.Depth(TEXT("events")),Memory.Depth(TEXT("world")),Memory.WaitMs(TEXT("motion")),Memory.WaitMs(TEXT("events")),Memory.WaitMs(TEXT("world")),Scene?Scene->AckMs:0);
        MetricsText += FString::Printf(TEXT("\nGround extract/publish %.0f/%.0f us | events %.2f ms"),ExtractUs,PublishUs,EventApplyMs);
        Health->SetNumberField(TEXT("ground_tiles"), Terrain ? Terrain->Tiles : 0);
        Health->SetNumberField(TEXT("unresolved"), Terrain ? Terrain->Unresolved.Num() : 0);
        TArray<TSharedPtr<FJsonValue>> Unknown;
        if (Terrain) for (const FString& Name : Terrain->Unresolved) Unknown.Add(MakeShared<FJsonValueString>(Name));
        Health->SetArrayField(TEXT("unresolved_sources"), Unknown);
        int32 Width=0, Height=0; PC->GetViewportSize(Width,Height);
        Health->SetNumberField(TEXT("width"), Width); Health->SetNumberField(TEXT("height"), Height);
        FString Json; FJsonSerializer::Serialize(Health, TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HeyZed/health.json")));
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("HeyZedMotionTest")) && bOrigin)
    {
        const int32 Stage = Pawn->bCrouched ? 4 : FMath::Abs(Pawn->NativeTurnRate)>8 && Pawn->Gait==0 ? 5 : Pawn->Gait>0 ? Pawn->Gait : bMotionMoved ? 6 : 0;
        if (Pawn->Gait>0) bMotionMoved = true;
        if (Stage != MotionCaptureStage) { MotionCaptureStage = Stage; MotionCaptureAt = Now+1.2; bMotionCaptured = false; }
        bool Ready = true;
#if WITH_EDITOR
        Ready = !GShaderCompilingManager || !GShaderCompilingManager->IsCompiling();
#endif
        if (!bMotionCaptured && Now >= MotionCaptureAt && Ready)
        {
            const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(),FString::Printf(TEXT("HeyZed/motion-%d.png"),Stage));
            FScreenshotRequest::RequestScreenshot(Path,true,false); bMotionCaptured = true;
        }
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("HeyZedCameraTest")) && bOrigin && Now >= NextTest && TestPhase < 5)
    {
        if (!bTestCapturePending)
        {
            if (TestPhase == 1) PC->CycleCamera();
            if (TestPhase == 3) PC->CycleCamera();
            bTestCapturePending = true;
            NextTest = Now+1;
            return;
        }
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { NextTest = Now+1; return; }
#endif
        const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("HeyZed/camera-%d.png"), TestPhase));
        FScreenshotRequest::RequestScreenshot(Path, true, false);
        if (TestPhase == 3)
        {
            FTimerHandle InputHandle;
            TWeakObjectPtr<AHeyZedPlayerController> WeakController = PC;
            GetWorld()->GetTimerManager().SetTimer(InputHandle, [WeakController]() { if (WeakController.IsValid()) WeakController->InjectVerificationLook(); }, .5f, false);
        }
        bTestCapturePending = false;
        NextTest = Now+2;
        ++TestPhase;
    }
}

void UHeyZedWorldSubsystem::PublishRenderTelemetry(const TSharedRef<FJsonObject>& Health)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedDiagnostics_RenderSnapshot);
    const bool DetailedInventory=FParse::Param(FCommandLine::Get(),TEXT("HeyZedResourceInventory"));
    if(Lighting)Lighting->PublishTelemetry(Health);
    auto Render=MakeShared<FJsonObject>();
    Render->SetBoolField(TEXT("application_active"),FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive());
    Render->SetNumberField(TEXT("game_ms"),FPlatformTime::ToMilliseconds(GGameThreadTime));
    Render->SetNumberField(TEXT("render_ms"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Render->SetNumberField(TEXT("rhi_ms"),FPlatformTime::ToMilliseconds(GRHIThreadTime));
    Render->SetNumberField(TEXT("gpu_ms"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    Render->SetNumberField(TEXT("bridge_tick_ms"),TickWorkMs);
    Render->SetNumberField(TEXT("object_tick_ms"),Scene?Scene->TickWorkMs:0);
    Render->SetBoolField(TEXT("object_tick_enabled"),Scene&&Scene->IsActorTickEnabled());
    Render->SetNumberField(TEXT("active_objects"),Scene?Scene->ActiveObjectCount():0);
    Render->SetNumberField(TEXT("draw_calls"),GNumDrawCallsRHI[0]);
    Render->SetNumberField(TEXT("primitives"),GNumPrimitivesDrawnRHI[0]);
    FRHIMemoryStats DeviceMemory;RHIGetMemoryStats(DeviceMemory);
    Render->SetBoolField(TEXT("gpu_memory_stats_valid"),DeviceMemory.IsValid());
    if(DeviceMemory.IsValid())
    {
        Render->SetNumberField(TEXT("gpu_local_budget_mib"),DeviceMemory.BudgetLocal/1048576.);
        Render->SetNumberField(TEXT("gpu_local_used_mib"),DeviceMemory.UsedLocal/1048576.);
        Render->SetNumberField(TEXT("gpu_local_demoted_mib"),DeviceMemory.DemotedLocal/1048576.);
        Render->SetNumberField(TEXT("gpu_system_used_mib"),DeviceMemory.UsedSystem/1048576.);
    }
    FTextureMemoryStats MemoryStats;RHIGetTextureMemoryStats(MemoryStats);
    Render->SetNumberField(TEXT("streaming_texture_mib"),MemoryStats.StreamingMemorySize/1048576.);
    Render->SetNumberField(TEXT("nonstreaming_texture_mib"),MemoryStats.NonStreamingMemorySize/1048576.);
    Render->SetNumberField(TEXT("texture_pool_mib"),MemoryStats.TexturePoolSize/1048576.);
    int32 Loaded=0,Visible=0,Requested=0;
    for(auto* Level:GetWorld()->GetStreamingLevels())if(Level)
    {Loaded+=Level->IsLevelLoaded();Visible+=Level->IsLevelVisible();Requested+=Level->ShouldBeLoaded();}
    Render->SetNumberField(TEXT("streaming_levels_loaded"),Loaded);
    Render->SetNumberField(TEXT("streaming_levels_visible"),Visible);
    Render->SetNumberField(TEXT("streaming_levels_requested"),Requested);
    int32 Buildings=0,Components=0,EmptyComponents=0,Instances=0;
    TSet<FString> MissingUVMetadata;
    TArray<TPair<double,TSharedPtr<FJsonObject>>> Nearby;
    const auto* PC=GetWorld()->GetFirstPlayerController();const APawn* Pawn=PC?PC->GetPawn():nullptr;
    const FVector Position=Pawn?Pawn->GetActorLocation():FVector::ZeroVector;
    Render->SetNumberField(TEXT("player_x_cm"),Position.X);Render->SetNumberField(TEXT("player_y_cm"),Position.Y);
    TArray<TSharedPtr<FJsonValue>> SourceInfo;
    TArray<FWorldPartitionStreamingSource> Sources;
    if(PC)PC->GetStreamingSources(Sources);
    for(const auto& Source:Sources)
    {
        auto Info=MakeShared<FJsonObject>();Info->SetStringField(TEXT("name"),Source.Name.ToString());
        Info->SetNumberField(TEXT("x_cm"),Source.Location.X);Info->SetNumberField(TEXT("y_cm"),Source.Location.Y);Info->SetNumberField(TEXT("z_cm"),Source.Location.Z);
        SourceInfo.Add(MakeShared<FJsonValueObject>(Info));
    }
    Render->SetArrayField(TEXT("streaming_sources"),SourceInfo);
    for(TActorIterator<AHeyZedArchitecture> It(GetWorld());It;++It)
    {
        ++Buildings;
        for(UInstancedStaticMeshComponent* Component:It->Components)if(Component)
        {
            ++Components;const int32 Count=Component->GetInstanceCount();Instances+=Count;if(Count==0)++EmptyComponents;
            if(DetailedInventory)if(UStaticMesh* Mesh=Component->GetStaticMesh())for(const auto& Slot:Mesh->GetStaticMaterials())
                if(!Slot.UVChannelData.bInitialized)MissingUVMetadata.Add(Mesh->GetPathName());
        }
        if(!DetailedInventory)continue;
        const double Distance=FMath::Sqrt(It->GetComponentsBoundingBox(true).ComputeSquaredDistanceToPoint(Position))/100.;
        auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("actor"),It->GetName());
        Entry->SetNumberField(TEXT("distance_m"),Distance);Entry->SetNumberField(TEXT("components"),It->Components.Num());
        Entry->SetNumberField(TEXT("origin_x_cm"),It->GetActorLocation().X);Entry->SetNumberField(TEXT("origin_y_cm"),It->GetActorLocation().Y);
        Nearby.Emplace(Distance,Entry);
    }
    Nearby.Sort([](const auto& A,const auto& B){return A.Key<B.Key;});
    TArray<TSharedPtr<FJsonValue>> Entries;
    for(int32 I=0;I<FMath::Min(8,Nearby.Num());++I)Entries.Add(MakeShared<FJsonValueObject>(Nearby[I].Value));
    if(DetailedInventory)Render->SetArrayField(TEXT("nearest_detailed_buildings"),Entries);
    Render->SetNumberField(TEXT("detailed_buildings"),Buildings);
    Render->SetNumberField(TEXT("construction_components"),Components);
    Render->SetNumberField(TEXT("construction_empty_components"),EmptyComponents);
    int32 ArchitectureRange=-1;UWorldPartitionSubsystem::GetOverrideLoadingRange(TEXT("Architecture"),ArchitectureRange);
    Render->SetNumberField(TEXT("architecture_loading_range_override_cm"),ArchitectureRange);
    Render->SetNumberField(TEXT("construction_instances"),Instances);
    Render->SetBoolField(TEXT("resource_inventory_requested"),DetailedInventory);
    if(DetailedInventory)Render->SetNumberField(TEXT("construction_missing_uv_metadata"),MissingUVMetadata.Num());
    TArray<TSharedPtr<FJsonValue>> MissingUVExamples;
    for(const auto& Path:MissingUVMetadata){MissingUVExamples.Add(MakeShared<FJsonValueString>(Path));if(MissingUVExamples.Num()==8)break;}
    if(DetailedInventory)Render->SetArrayField(TEXT("construction_missing_uv_examples"),MissingUVExamples);
#if WITH_EDITOR
    if(!RenderMapDiagnostics && bOrigin && GetWorld()->GetTimeSeconds()>5 && DetailedInventory)
    {
        RenderMapDiagnostics=MakeShared<FJsonObject>();
        if(auto* Partition=GetWorld()->GetWorldPartition())
        {
            TArray<TSharedPtr<FJsonValue>> Grids;
            if(const auto* Hash=Cast<UWorldPartitionRuntimeSpatialHash>(Partition->RuntimeHash))
                Hash->ForEachStreamingGrid([&](const FSpatialHashStreamingGrid& Grid)
                {
                    auto Info=MakeShared<FJsonObject>();Info->SetStringField(TEXT("name"),Grid.GridName.ToString());
                    Info->SetNumberField(TEXT("cell_cm"),Grid.CellSize);Info->SetNumberField(TEXT("range_cm"),Grid.GetLoadingRange());
                    Grids.Add(MakeShared<FJsonValueObject>(Info));
                });
            RenderMapDiagnostics->SetArrayField(TEXT("grids"),Grids);
            TArray<TPair<double,TSharedPtr<FJsonObject>>> Descriptors;
            FWorldPartitionHelpers::ForEachActorDescInstance<AHeyZedArchitecture>(Partition,[&](const FWorldPartitionActorDescInstance* Desc)
            {
                const FBox Bounds=Desc->GetRuntimeBounds();const double Distance=FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Position))/100.;
                auto Info=MakeShared<FJsonObject>();Info->SetStringField(TEXT("label"),Desc->GetActorLabel().ToString());
                Info->SetStringField(TEXT("grid"),Desc->GetRuntimeGrid().ToString());Info->SetNumberField(TEXT("distance_m"),Distance);
                Info->SetStringField(TEXT("bounds_cm"),Bounds.ToString());Info->SetBoolField(TEXT("spatial"),Desc->GetIsSpatiallyLoaded());
                Descriptors.Emplace(Distance,Info);return true;
            });
            Descriptors.Sort([](const auto& A,const auto& B){return A.Key<B.Key;});
            TArray<TSharedPtr<FJsonValue>> Near;
            for(int32 I=0;I<FMath::Min(12,Descriptors.Num());++I)Near.Add(MakeShared<FJsonValueObject>(Descriptors[I].Value));
            RenderMapDiagnostics->SetNumberField(TEXT("building_descriptors"),Descriptors.Num());
            RenderMapDiagnostics->SetArrayField(TEXT("nearest_descriptors_at_connection"),Near);
        }
    }
    if(RenderMapDiagnostics)Render->SetObjectField(TEXT("map"),RenderMapDiagnostics);
#endif
    if(RenderResourceDiagnostics)Render->SetObjectField(TEXT("resource_inventory"),RenderResourceDiagnostics);
    const auto Settings=MakeShared<FJsonObject>();
    for(const TCHAR* Name:{TEXT("r.HZBOcclusion"),TEXT("r.NumBufferedOcclusionQueries"),TEXT("r.RHICmdWidth"),TEXT("r.RHICmd.ParallelTranslate.MaxCommandsPerTranslate"),TEXT("r.RHICmd.ParallelTranslate.CombineSingleAndParallel"),TEXT("r.RDG.ParallelExecute"),TEXT("r.SkinCache.Mode"),TEXT("r.ShowMaterialDrawEvents"),TEXT("r.ScreenPercentage"),TEXT("heyzed.Entities.NativePoseVisibility"),TEXT("sg.GlobalIlluminationQuality"),TEXT("sg.ReflectionQuality"),TEXT("sg.ShadowQuality"),TEXT("sg.AntiAliasingQuality"),TEXT("sg.EffectsQuality"),TEXT("sg.PostProcessQuality")})
        if(const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name))Settings->SetNumberField(Name,Variable->GetFloat());
    Render->SetObjectField(TEXT("runtime_settings"),Settings);
    Health->SetObjectField(TEXT("render"),Render);
    if(!bRenderMemoryLogged && Visible>=Requested && (!Lighting || (Lighting->FixtureLibrary && Lighting->Resources)) &&
        GetWorld()->GetTimeSeconds()>60 && DetailedInventory)
    {
        bRenderMemoryLogged=true;
        GEngine->Exec(GetWorld(),TEXT("rhi.DumpMemory"));
        GEngine->Exec(GetWorld(),TEXT("rhi.DumpResourceMemory 40"));
        RenderResourceDiagnostics=MakeShared<FJsonObject>();
        auto Classes=MakeShared<FJsonObject>();
        for(TObjectIterator<UInstancedStaticMeshComponent> It;It;++It)
            if(It->GetWorld()==GetWorld() && It->IsRegistered() && It->GetOwner())
            {
                const FString Class=It->GetOwner()->GetClass()->GetName();
                double Count=0;Classes->TryGetNumberField(Class,Count);
                Classes->SetNumberField(Class,Count+It->GetInstanceCount());
            }
        int64 RootBytes=0,StreamBytes=0,RootPages=0,StreamPages=0;int32 NaniteMeshes=0;
        for(TObjectIterator<UStaticMesh> It;It;++It)
            if(const auto* Data=It->GetRenderData();Data && Data->NaniteResourcesPtr && Data->NaniteResourcesPtr->NumRootPages>0)
            {
                ++NaniteMeshes;const auto& Nanite=*Data->NaniteResourcesPtr;
                RootPages+=Nanite.NumRootPages;StreamPages+=FMath::Max(0,Nanite.PageStreamingStates.Num()-int32(Nanite.NumRootPages));
                for(int32 Page=0;Page<Nanite.PageStreamingStates.Num();++Page)
                    if(Page<int32(Nanite.NumRootPages))RootBytes+=Nanite.PageStreamingStates[Page].BulkSize;
                    else StreamBytes+=Nanite.PageStreamingStates[Page].BulkSize;
            }
        RenderResourceDiagnostics->SetObjectField(TEXT("registered_instances_by_actor_class"),Classes);
        RenderResourceDiagnostics->SetNumberField(TEXT("loaded_nanite_meshes"),NaniteMeshes);
        RenderResourceDiagnostics->SetNumberField(TEXT("nanite_encoded_root_mib"),RootBytes/1048576.);
        RenderResourceDiagnostics->SetNumberField(TEXT("nanite_encoded_streamable_mib"),StreamBytes/1048576.);
        RenderResourceDiagnostics->SetNumberField(TEXT("nanite_root_page_capacity_mib"),RootPages*(NANITE_ROOT_PAGE_GPU_SIZE/1048576.));
        RenderResourceDiagnostics->SetNumberField(TEXT("nanite_streamable_page_capacity_mib"),StreamPages*(NANITE_STREAMING_PAGE_GPU_SIZE/1048576.));
        for(const TCHAR* Name:{TEXT("r.OneFrameThreadLag"),TEXT("r.GTSyncType"),TEXT("r.Nanite.Streaming.StreamingPoolSize"),TEXT("r.HZBOcclusion"),TEXT("r.NumBufferedOcclusionQueries")})
            if(const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name))RenderResourceDiagnostics->SetNumberField(Name,Variable->GetInt());
    }
}

void UHeyZedWorldSubsystem::PublishTelemetry(double Now, const TSharedRef<FJsonObject>& Health)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedDiagnostics_TransportSnapshot);
    constexpr double Window = 2.;
    auto Summarize = [Now](TArray<FSample>& Samples)
    {
        Samples.RemoveAll([Now](const FSample& S) { return S.Time < Now-2.; });
        if (Samples.Num() > 4096) Samples.RemoveAt(0, Samples.Num()-4096);
        TArray<double> Sorted;
        double Sum = 0;
        for (const auto& S : Samples) { Sum += S.Value; Sorted.Add(S.Value); }
        Sorted.Sort();
        return FVector(Sorted.IsEmpty() ? -1 : Sum/Sorted.Num(),
            Sorted.IsEmpty() ? -1 : Sorted[FMath::Clamp(FMath::CeilToInt(Sorted.Num()*.95)-1,0,Sorted.Num()-1)],
            Sorted.IsEmpty() ? -1 : Sorted.Last());
    };
    const FVector Frame = Summarize(FrameTimes), Ack = Summarize(AckTimes), Input = Summarize(InputAges), Gap = Summarize(StateGaps);
    Traffic.RemoveAll([Now](const FTraffic& T) { return T.Time < Now-Window; });
    if (Traffic.Num() > 4096) Traffic.RemoveAt(0, Traffic.Num()-4096);
    int32 Tx=0, Rx=0; int64 TxBytes=0, RxBytes=0;
    for (const auto& T : Traffic) { if (T.Sent) { ++Tx; TxBytes+=T.Bytes; } else { ++Rx; RxBytes+=T.Bytes; } }
    const double Span = Traffic.IsEmpty() ? Window : FMath::Min(Window, FMath::Max(.25, Now-Traffic[0].Time));
    const double Age = LastReceive > 0 ? FMath::Max(0.,Now-LastReceive)*1000 : -1;
    const double Fps = Frame.X > 0 ? 1000/Frame.X : 0;
    const double StateHz = Gap.X > 0 ? 1000/Gap.X : 0;
    const TCHAR* Status = LastReceive <= 0 ? TEXT("WAITING") : Age > 250 ? TEXT("STALE") : TEXT("LIVE");
    auto Number = [](double Value) { return Value < 0 ? FString(TEXT("--")) : FString::Printf(TEXT("%.1f"), Value); };
    MetricsText = FString::Printf(TEXT("HeyZed %s | 2 s\nUE %.0f FPS | frame %s / %s / %s ms\nAck RTT %s / %s / %s ms\nPZ input age %s / %s ms | state age %s ms\nState %.0f Hz | gap p95/max %s / %s ms\nLocal TX/RX %.0f / %.0f Hz | %.1f / %.1f KiB/s\nSkipped states %llu | send errors %d\nGround decode/build %.1f / %.1f ms"),
        Status, Fps, *Number(Frame.X), *Number(Frame.Y), *Number(Frame.Z),
        *Number(Ack.X), *Number(Ack.Y), *Number(Ack.Z), *Number(Input.X), *Number(Input.Y), *Number(Age),
        StateHz, *Number(Gap.Y), *Number(Gap.Z), Tx/Span, Rx/Span, TxBytes/Span/1024, RxBytes/Span/1024,
        SkippedStates, SendErrors, GroundDecodeMs, GroundApplyMs);
    if (auto* Pawn = Cast<AHeyZedPawn>(GetWorld()->GetFirstPlayerController()->GetPawn()))
        if (auto* Animation = Cast<UHeyZedMotionInstance>(Pawn->Body->GetAnimInstance()))
            MetricsText += FString::Printf(TEXT("\nMM %s | play %.2fx | %s"),*Animation->SelectedGroup.ToString(),Animation->PlaybackRate,Animation->bMatchingValid ? TEXT("READY") : TEXT("WAITING"));
    NativeIntervals.RemoveAll([Now,Window](const FSample& S){return S.Time<Now-Window;});
    double NativeMean=0; for (const auto& S : NativeIntervals) NativeMean+=S.Value;
    NativeMean=NativeIntervals.IsEmpty() ? -1 : NativeMean/NativeIntervals.Num();
    MetricsText+=FString::Printf(TEXT("\nPZ step interval avg %s ms | state queue %.0f ms\nUE state apply %.2f ms | motion filter 56 ms"),*Number(NativeMean),Memory.WaitMs(TEXT("motion")),MotionApplyMs);
    auto Metrics = MakeShared<FJsonObject>();
    Metrics->SetStringField(TEXT("status"), Status);
    Metrics->SetNumberField(TEXT("window_seconds"), Window);
    auto Distribution = [&Metrics](const TCHAR* Name, const FVector& Values)
    {
        auto Object = MakeShared<FJsonObject>(); Object->SetNumberField(TEXT("mean"), Values.X);
        Object->SetNumberField(TEXT("p95"), Values.Y); Object->SetNumberField(TEXT("max"), Values.Z);
        Metrics->SetObjectField(Name, Object);
    };
    Distribution(TEXT("frame_ms"), Frame); Distribution(TEXT("ack_rtt_ms"), Ack);
    Distribution(TEXT("pz_input_age_ms"), Input); Distribution(TEXT("state_gap_ms"), Gap);
    Metrics->SetNumberField(TEXT("fps"), Fps); Metrics->SetNumberField(TEXT("state_hz"), StateHz);
    Metrics->SetNumberField(TEXT("state_age_ms"), Age);
    Metrics->SetNumberField(TEXT("tx_hz"), Tx/Span); Metrics->SetNumberField(TEXT("rx_hz"), Rx/Span);
    Metrics->SetNumberField(TEXT("tx_kib_s"), TxBytes/Span/1024); Metrics->SetNumberField(TEXT("rx_kib_s"), RxBytes/Span/1024);
    Metrics->SetNumberField(TEXT("skipped_states_total"), static_cast<double>(SkippedStates));
    Metrics->SetNumberField(TEXT("received_states_total"), static_cast<double>(ReceivedStates));
    Metrics->SetNumberField(TEXT("unacked_inputs"), static_cast<double>(Sequence-LastAck));
    Metrics->SetNumberField(TEXT("send_errors_total"), SendErrors);
    Metrics->SetNumberField(TEXT("ground_decode_ms"), GroundDecodeMs); Metrics->SetNumberField(TEXT("ground_build_ms"), GroundApplyMs);
    Metrics->SetNumberField(TEXT("pose_smoothing_tau_ms"), 1000./18);
    Metrics->SetNumberField(TEXT("pz_step_interval_estimate_ms"),NativeMean);
    Metrics->SetNumberField(TEXT("ue_state_apply_ms"),MotionApplyMs);
    Metrics->SetNumberField(TEXT("motion_queue_age_ms"),Memory.WaitMs(TEXT("motion")));
    Health->SetObjectField(TEXT("metrics"), Metrics);
}

void UHeyZedWorldSubsystem::InteractDoor()
{
    if (!bShared || !Scene) return;
    auto* PC=GetWorld()->GetFirstPlayerController(); if (!PC || !PC->GetPawn()) return;
    FString Command=Lighting?Lighting->RequestInteraction(PC->GetPawn()->GetActorLocation(),PC->GetControlRotation().Vector()):FString();
    if(Command.IsEmpty())Command=Scene->RequestNearestDoor(PC->GetPawn()->GetActorLocation());
    if (!Command.IsEmpty()) Memory.Offer(TEXT("commands"),Command);
}

void UHeyZedWorldSubsystem::RegisterLightRegion(AHeyZedLightRegion* Region)
{
    LightRegions.Add(Region); if (Lighting) Lighting->RegisterRegion(Region);
}
void UHeyZedWorldSubsystem::UnregisterLightRegion(AHeyZedLightRegion* Region)
{
    LightRegions.Remove(Region); if (Lighting) Lighting->UnregisterRegion(Region);
}

void UHeyZedWorldSubsystem::ToggleHeldLight()
{
    if(bShared&&Lighting)Memory.Offer(TEXT("commands"),Lighting->RequestHeldToggle());
}
