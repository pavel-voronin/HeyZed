#pragma once
#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeyZedMemory.h"
#include "HeyZedTerrain.h"
#include "HeyZedWorldArchive.h"
#include "Async/Future.h"
#include "RenderCommandFence.h"
#include "HeyZedWorldSubsystem.generated.h"

class FSocket;
class FInternetAddr;
class FJsonObject;
class AHeyZedTerrain;
class AHeyZedScene;
class AHeyZedEntities;
class AHeyZedLighting;
class AHeyZedLightRegion;
class AHeyZedBakedRegion;
class AHeyZedArchitecture;

UCLASS()
class UHeyZedWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHeyZedWorldSubsystem, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type == EWorldType::Game || Type == EWorldType::PIE; }
    void RegisterLightRegion(AHeyZedLightRegion* Region);
    void UnregisterLightRegion(AHeyZedLightRegion* Region);
    void InteractDoor();
    void ToggleHeldLight();
    void SendNativeKey(int32 Code,bool Down);
    bool SendCrowdReadiness(bool Enabled);
    bool SendEntityAnimation(const FString& Id,int64 Revision,bool Enabled,const FString& Clips);
    bool ScenePresented() const {return bScenePresented;}
    bool GameplayReady() const {return bScenePresented && bStartupProtocol && !bBackendLoading;}
    const FString& LoadingStatus() const {return StartupStatus;}
    const FString& TelemetryText() const { return MetricsText; }
    void RegisterBase(AHeyZedBakedRegion* Region);
    void UnregisterBase(AHeyZedBakedRegion* Region);
    bool BaseSurface(FIntPoint Address,FHeyZedGroundCell& Out) const;
    void OverrideBase(FIntPoint Address);
    void ClaimArchitectureFloor(FIntPoint Address,bool Claimed,AHeyZedArchitecture* Owner);
    TSet<FIntPoint> ArchitectureFloors;
    TMap<FIntPoint,TWeakObjectPtr<AHeyZedArchitecture>> ArchitectureFloorOwners;
    void RestoreArchitecture(AHeyZedArchitecture* Building);
    void ArchitectureChanged(AHeyZedArchitecture* Building);
    void QueueArchitectureUpdate(AHeyZedArchitecture* Building);
private:
    void UpdateStartup(double Now);
    bool bScenePresented=false,bStartupPrepared=false,bStartupProtocol=false,bBackendLoading=true,bBackendEntered=false;
    int64 StartupEpoch=0;
    double StartupWarmup=0,NextStartupSend=0;
    uint64 StartupWarmupFrame=0,StartupPresentedFrame=0;
    FString StartupStatus=TEXT("Waiting for PZ"),BackendStartupStage;
    FRenderCommandFence StartupRenderFence;
    void PublishTelemetry(double Now, const TSharedRef<FJsonObject>& Health);
    void PublishRenderTelemetry(const TSharedRef<FJsonObject>& Health);
    bool bRenderMemoryLogged = false;
    TSharedPtr<FJsonObject> RenderMapDiagnostics;
    TSharedPtr<FJsonObject> RenderResourceDiagnostics;
    double TickWorkMs = 0;
    struct FArchitectureChange {int64 Epoch=0,Revision=-1;TSharedPtr<FJsonObject> Cell;};
    TMap<FString,FArchitectureChange> ArchitectureChanges;
    void ReceiveArchitecture(const TSharedRef<FJsonObject>& Block);
    TQueue<TWeakObjectPtr<AHeyZedArchitecture>> ConstructionQueue;
    TSet<TWeakObjectPtr<AHeyZedArchitecture>> QueuedConstruction;
    double ConstructionUpdateMs=0;
    FHeyZedMemory Memory;
    bool bShared = false, bDatagramFixture = false;
    FString MemoryGeneration;
    FHeyZedWorldStream WorldBase;
    bool bBaseStarted = false;
    FString BaseIdentity;
    TMap<FIntPoint,TWeakObjectPtr<AHeyZedBakedRegion>> BaseRegions;
    TMap<FIntPoint,TSet<FIntPoint>> BaseOverrides;
    TArray<TSharedPtr<FHeyZedPreparedGround>> GroundQueue;
    TArray<TFuture<TSharedPtr<FHeyZedPreparedGround>>> GroundDecode;
    void ReceiveState(const FString& Packet);
    void ReceiveGround(const uint8* Bytes, int32 Length);
    FSocket* Socket = nullptr;
    TSharedPtr<FInternetAddr> Backend;
    uint64 Session = 0, Sequence = 0;
    uint64 NativeKeySequence = 0;
    TArray<FString> PendingNativeKeys;
    int64 Epoch = 0, StateSequence = -1, GroundRevision = -1;
    double NextSend = 0, LastReceive = 0, NextHealth = 0;
    struct FSample { double Time, Value; };
    struct FTraffic { double Time; int32 Bytes; bool Sent; };
    TArray<FSample> FrameTimes, AckTimes, InputAges, StateGaps;
    TArray<FTraffic> Traffic;
    double LastTick = 0, GroundDecodeMs = 0, GroundApplyMs = 0;
    TArray<FSample> NativeIntervals;
    int64 LastNativeSample = 0;
    uint64 InvalidPosePackets=0;
    double MotionApplyMs = 0;
    uint64 LastAck = 0, SkippedStates = 0, ReceivedStates = 0;
    int32 SendErrors = 0;
    FString MetricsText = TEXT("HeyZed: waiting");
    FVector Origin = FVector::ZeroVector;
    bool bOrigin = false;
    UPROPERTY() TObjectPtr<AHeyZedLighting> Lighting;
    TSet<TWeakObjectPtr<AHeyZedLightRegion>> LightRegions;
    UPROPERTY() TObjectPtr<AHeyZedTerrain> Terrain;
    UPROPERTY() TObjectPtr<AHeyZedScene> Scene;
    UPROPERTY() TObjectPtr<AHeyZedEntities> Entities;
    double LiveTestStarted = 0;
    int32 LiveTestStage = -1;
    double NextCommandRetry = 0;
    double ExtractUs = 0, PublishUs = 0, EventApplyMs = 0;
    double AdapterObjectsUs=0,AdapterLightingUs=0,AdapterEntitiesUs=0,AdapterGroundUs=0,AdapterArchitectureUs=0;
    double AdapterInputExpirations=0,AdapterInputStepAgeUs=0,AdapterControlGapMaxUs=0,AdapterPreparedWorkMaxUs=0;
    bool bAdapterPreparedFailed=false;
    FString BindingSignature;
    int32 TestPhase = 0;
    bool bTestCapturePending = false;
    double NextTest = 0;
    int32 MotionCaptureStage = -1;
    bool bMotionMoved = false, bMotionCaptured = false;
    double MotionCaptureAt = 0;
};
