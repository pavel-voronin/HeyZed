#include "HeyZedScene.h"
#include "HeyZedMetrics.h"
#include "HeyZedArchitecture.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ScopeExit.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

AHeyZedScene::AHeyZedScene()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    Session = FGuid::NewGuid().ToString(EGuidFormats::Digits);
}
void AHeyZedScene::Wake(const FString& Key)
{
    ActiveObjects.Add(Key);SetActorTickEnabled(true);
}
void AHeyZedScene::ResolveDoorOwners(FHeyZedPresentedObject& Object)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedDoors_ResolveOwners);
    ++DoorOwnerSearches;Object.DoorBuildings.Reset();Object.bDoorOwnersResolved=true;
    for(TActorIterator<AHeyZedArchitecture> It(GetWorld());It;++It)
        if(It->IsPresentationActive()&&It->Covers(Object.Confirmed))Object.DoorBuildings.Add(*It);
}
void AHeyZedScene::ArchitectureChanged(AHeyZedArchitecture* Building)
{
    if(!Building)return;
    for(auto& Pair:Objects)
    {
        auto& Object=Pair.Value;if(Object.Deleted||Object.Kind!=TEXT("door"))continue;
        const bool Candidate=Building->IsPresentationActive()&&Building->Covers(Object.Confirmed);
        const int32 Removed=Object.DoorBuildings.RemoveAll([&](const auto& BuildingRef){return !BuildingRef.IsValid()||(BuildingRef.Get()==Building&&!Candidate);});
        if(Candidate)Object.DoorBuildings.AddUnique(Building);
        if(Candidate||Removed>0)Wake(Pair.Key);
    }
}
void AHeyZedScene::ConstructionChanged(FIntVector Address)
{
    for(const auto& Pair:Objects)
    {
        const auto& Object=Pair.Value;
        if(!Object.Deleted && Object.Kind==TEXT("door") && FMath::Abs(Object.Confirmed.X-Address.X*100.)<=100.01 &&
           FMath::Abs(Object.Confirmed.Y-Address.Y*100.)<=100.01 && FMath::RoundToInt(Object.Confirmed.Z/HeyZedMetrics::Floor)==Address.Z)Wake(Pair.Key);
    }
}
void AHeyZedScene::EnsureMesh(FHeyZedPresentedObject& Object)
{
    if (Object.Mesh) return;
    Object.Mesh = NewObject<UStaticMeshComponent>(this);
    Object.Mesh->SetupAttachment(RootComponent); Object.Mesh->RegisterComponent();
    Object.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Object.Mesh->SetCastShadow(true);
    Object.Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Object.Mesh->SetMobility(EComponentMobility::Movable);
    Object.Mesh->SetRelativeScale3D(Object.Kind==TEXT("door") ? FVector(1,.045,2) : Object.Kind==TEXT("vehicle") ? FVector(4,1.8,1.3) : FVector(.4,.4,1.8));
}
void AHeyZedScene::Apply(const TSharedRef<FJsonObject>& Event,const FVector& Origin)
{
    const FString Type = Event->GetStringField(TEXT("type"));
    if (Type==TEXT("motion"))
    {
        for (const auto& Value : Event->GetArrayField(TEXT("entities")))
        {
            auto Motion = Value->AsObject();
            auto Object = MakeShared<FJsonObject>(*Motion);
            Object->SetStringField(TEXT("type"),TEXT("entity"));
            Apply(Object,Origin);
        }
        return;
    }
    if (Type==TEXT("ack"))
    {
        const FString Token = Event->GetStringField(TEXT("token"));
        for (auto& Pair : Objects) if (Pair.Value.Command==Token)
        {
            auto& Object = Pair.Value;
            AckMs = (FPlatformTime::Seconds()-Object.CommandAt)*1000;
            Object.Pending = false; Object.Command.Reset();
            Wake(Pair.Key);
        }
        return;
    }
    if (Type!=TEXT("object") && Type!=TEXT("entity")) return;
    const FString Id = Event->GetStringField(TEXT("id"));
    const int64 Generation = Event->GetNumberField(TEXT("generation")), Revision = Event->GetNumberField(TEXT("revision"));
    const FString Key = FString::Printf(TEXT("%lld:"),Generation)+Id;
    auto& Object = Objects.FindOrAdd(Key);
    if (Revision<=Object.Revision) return;
    Object.Id = Id; Object.Generation=Generation; Object.Revision=Revision;
    bool Deleted = false; Event->TryGetBoolField(TEXT("deleted"),Deleted);
    if (Deleted)
    {
        Object.Deleted=true; Object.Pending=false; Object.Command.Reset();Object.DoorBuildings.Reset();Object.bDoorOwnersResolved=false;
        ActiveObjects.Remove(Key);SetActorTickEnabled(!ActiveObjects.IsEmpty());
        if (Object.Mesh) { Object.Mesh->DestroyComponent(); Object.Mesh=nullptr; }
        ++AppliedEvents; return;
    }
    Object.Deleted=false;
    Object.Kind = Event->GetStringField(TEXT("kind"));
    const FVector PreviousPosition=Object.Confirmed;
    Object.Confirmed = FVector((Event->GetNumberField(TEXT("x"))-Origin.X)*100,(Event->GetNumberField(TEXT("y"))-Origin.Y)*100,(Event->GetNumberField(TEXT("z"))-Origin.Z)*HeyZedMetrics::Floor);
    if (Type==TEXT("object"))
    {
        if(!PreviousPosition.Equals(Object.Confirmed,.01))Object.bDoorOwnersResolved=false;
        const FString Address=FString::Printf(TEXT("door:%.0f:%.0f:%.0f:%d"),Event->GetNumberField(TEXT("x")),Event->GetNumberField(TEXT("y")),Event->GetNumberField(TEXT("z")),Event->GetBoolField(TEXT("north"))?1:0);
        if (const FString* Previous=SourceOwners.Find(Address); Previous && *Previous!=Key)
        {
            if (auto* Old=Objects.Find(*Previous))
            {
                if (Old->Generation>Generation) return;
                if (!Old->Deleted)
                {
                    Object.Mesh=Old->Mesh; Object.VisualYaw=Old->VisualYaw; Object.LocalEffect=Old->LocalEffect;
                    Old->Mesh=nullptr; Old->Deleted=true; Old->Pending=false; Old->Command.Reset();
                    ActiveObjects.Remove(*Previous);
                }
            }
        }
        SourceOwners.Add(Address,Key);
        Object.Open = Event->GetBoolField(TEXT("open")); Object.BaseYaw=Event->GetBoolField(TEXT("north")) ? 0 : 90;
        if(!Object.bDoorOwnersResolved)ResolveDoorOwners(Object);
        // Confirmations never overwrite the current visual angle or visual-only effects.
        ++AppliedEvents;
    }
    else
    {
        Object.Velocity = FVector(Event->GetNumberField(TEXT("vx"))*100,Event->GetNumberField(TEXT("vy"))*100,0);
        Object.ConfirmedYaw=FMath::RadiansToDegrees(Event->GetNumberField(TEXT("yaw")));
        ++AppliedMotion;
    }
    Object.SampleAt=FPlatformTime::Seconds();
    Wake(Key);
    const bool Fresh = !Object.Mesh;
    EnsureMesh(Object);
    if (Fresh) Object.Mesh->SetRelativeLocation(Object.Confirmed+FVector(0,0,Object.Kind==TEXT("door")?100:90));
}
void AHeyZedScene::Tick(float Delta)
{
    const double WorkStart=FPlatformTime::Seconds();
    ON_SCOPE_EXIT {TickWorkMs=(FPlatformTime::Seconds()-WorkStart)*1000;};
    Super::Tick(Delta);
    const double Now=FPlatformTime::Seconds();
    for (auto Active=ActiveObjects.CreateIterator();Active;++Active)
    {
        auto* Found=Objects.Find(*Active);
        if(!Found || Found->Deleted || !Found->Mesh){Active.RemoveCurrent();continue;}
        auto& Object=*Found;
        if (Object.Pending && Now-Object.CommandAt>2) { Object.Pending=false; Object.Command.Reset(); }
        if (Object.Kind==TEXT("door"))
        {
            const float Target=(Object.Pending ? Object.DesiredOpen : Object.Open) ? 90 : 0;
            Object.VisualYaw=FMath::FixedTurn(Object.VisualYaw,Target,240*Delta);
            bool Represented=false;
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedDoors_ApplyPose);
                for(const auto& BuildingRef:Object.DoorBuildings)if(auto* Building=BuildingRef.Get();Building&&Building->IsPresentationActive())
                {++DoorPoseApplications;Represented|=Building->Door(Object.Confirmed,FMath::IsNearlyZero(Object.BaseYaw),Object.VisualYaw);}
            }
            Object.Mesh->SetHiddenInGame(Represented);
            if(!Represented)
            {
                const FRotator Rotation(0,Object.BaseYaw+Object.VisualYaw,0);
                Object.Mesh->SetRelativeRotation(Rotation);
                Object.Mesh->SetRelativeLocation(Object.Confirmed+Rotation.RotateVector(FVector(50,0,100)));
            }
            if(!Object.Pending && FMath::IsNearlyEqual(Object.VisualYaw,Target))Active.RemoveCurrent();
        }
        else
        {
            const FVector Prediction = Object.Confirmed+Object.Velocity*FMath::Clamp(Now-Object.SampleAt,0.,.05)+FVector(0,0,90);
            Object.Mesh->SetRelativeLocation(FMath::VInterpTo(Object.Mesh->GetRelativeLocation(),Prediction,Delta,25));
            Object.VisualYaw=FMath::FixedTurn(Object.VisualYaw,Object.ConfirmedYaw,540*Delta);
            Object.Mesh->SetRelativeRotation(FRotator(0,Object.VisualYaw,0));
        }
    }
    SetActorTickEnabled(!ActiveObjects.IsEmpty());
}
FString AHeyZedScene::RequestNearestDoor(const FVector& LocalPosition)
{
    FHeyZedPresentedObject* Best=nullptr; double Distance=250*250;
    for (auto& Pair : Objects)
    {
        auto& Object=Pair.Value;
        const double D=FVector::DistSquared(LocalPosition,Object.Confirmed);
        if (Object.Kind==TEXT("door") && !Object.Deleted && !Object.Pending && D<Distance) { Distance=D; Best=&Object; }
    }
    if (!Best) return {};
    Best->Command=Session+FString::Printf(TEXT("-%llu"),++NextCommand); Best->Pending=true;
    Best->DesiredOpen=!Best->Open; Best->CommandAt=FPlatformTime::Seconds();
    for(const auto& Pair:Objects)if(&Pair.Value==Best){Wake(Pair.Key);break;}
    const auto Commands=RetryCommands();
    for (const auto& Command : Commands) if (Command.Contains(Best->Command)) return Command;
    return {};
}
TArray<FString> AHeyZedScene::RetryCommands()
{
    TArray<FString> Result;
    for (const auto& Pair : Objects)
    {
        const auto& Object=Pair.Value; if (!Object.Pending) continue;
        auto Command=MakeShared<FJsonObject>();
        Command->SetStringField(TEXT("token"),Object.Command); Command->SetStringField(TEXT("id"),Object.Id);
        Command->SetNumberField(TEXT("generation"),Object.Generation); Command->SetNumberField(TEXT("revision"),Object.Revision);
        Command->SetBoolField(TEXT("open"),Object.DesiredOpen);
        FString Text; FJsonSerializer::Serialize(Command,TJsonWriterFactory<>::Create(&Text)); Result.Add(Text);
    }
    return Result;
}
