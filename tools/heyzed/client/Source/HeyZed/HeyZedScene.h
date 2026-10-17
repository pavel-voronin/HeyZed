#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeyZedScene.generated.h"
class FJsonObject;
class UStaticMeshComponent;
class AHeyZedArchitecture;

USTRUCT()
struct FHeyZedPresentedObject
{
    GENERATED_BODY()
    FString Id, Kind, Command;
    int64 Generation = 0, Revision = -1;
    FVector Confirmed = FVector::ZeroVector, Velocity = FVector::ZeroVector;
    float ConfirmedYaw = 0, VisualYaw = 0, BaseYaw = 0, LocalEffect = 0;
    double SampleAt = 0, CommandAt = 0;
    bool Open = false, Pending = false, DesiredOpen = false, Deleted = false;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
    TArray<TWeakObjectPtr<AHeyZedArchitecture>> DoorBuildings;
    bool bDoorOwnersResolved=false;
};

UCLASS()
class AHeyZedScene : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedScene();
    virtual void Tick(float Delta) override;
    void Apply(const TSharedRef<FJsonObject>& Event,const FVector& Origin);
    FString RequestNearestDoor(const FVector& LocalPosition);
    TArray<FString> RetryCommands();
    void ArchitectureChanged(AHeyZedArchitecture* Building);
    void ConstructionChanged(FIntVector Address);
    int32 ActiveObjectCount() const { return ActiveObjects.Num(); }
    UPROPERTY() TMap<FString,FHeyZedPresentedObject> Objects;
    uint64 AppliedEvents = 0, AppliedMotion = 0;
    uint64 DoorOwnerSearches=0,DoorPoseApplications=0;
    double AckMs = 0;
    double TickWorkMs = 0;
private:
    void EnsureMesh(FHeyZedPresentedObject& Object);
    void ResolveDoorOwners(FHeyZedPresentedObject& Object);
    void Wake(const FString& Key);
    TSet<FString> ActiveObjects;
    TMap<FString,FString> SourceOwners;
    uint64 NextCommand = 0;
    FString Session;
};
