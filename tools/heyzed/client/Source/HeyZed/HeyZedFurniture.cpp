#include "HeyZedFurniture.h"
#include "HeyZedMetrics.h"
#include "HeyZedLighting.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
namespace {
FVector Vector(const TSharedPtr<FJsonObject>& Value,const TCHAR* Key,FVector Default)
{
    const TArray<TSharedPtr<FJsonValue>>* Row=nullptr;
    if(!Value->TryGetArrayField(Key,Row)||Row->Num()!=3)return Default;
    return FVector((*Row)[0]->AsNumber(),(*Row)[1]->AsNumber(),(*Row)[2]->AsNumber());
}
}

const TArray<FHeyZedFurnitureBinding>* UHeyZedFurnitureLibrary::FindBindings(const FString& Tile)
{
    if(!bBindingsParsed)
    {
        bBindingsParsed=true;TSharedPtr<FJsonObject> Data;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BindingDescription),Data))
        for(const auto& Pair:Data->Values)for(const auto& Item:Pair.Value->AsArray())
        {
            auto B=Item->AsObject();const auto P=B->GetObjectField(TEXT("placement"));
            const FVector Rotation=Vector(P,TEXT("rotation"),FVector::ZeroVector);
            const FQuat Q=FQuat(FVector::UpVector,FMath::DegreesToRadians(Rotation.Z))*FQuat(FVector(0,1,0),FMath::DegreesToRadians(Rotation.Y))*FQuat(FVector(1,0,0),FMath::DegreesToRadians(Rotation.X));
            Bindings.FindOrAdd(FString(Pair.Key)).Add({B->GetStringField(TEXT("mesh")),B->GetStringField(TEXT("state")),FTransform(Q,Vector(P,TEXT("position"),FVector::ZeroVector)*100,Vector(P,TEXT("scale"),FVector::OneVector))});
        }
    }
    return Bindings.Find(Tile);
}

void UHeyZedFurnitureLibrary::RegisterMesh(const FString& Name,const FString& Path)
{
    Meshes.Add(Name,TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path)));MarkPackageDirty();
}


void UHeyZedFurnitureSubsystem::Reset()
{
    for(auto& Pair:Regions)if(Pair.Value)Pair.Value->Destroy();
    Regions.Reset();Chunks.Reset();LastRevision=-1;bSourceReady=false;bFailed=false;
}
void UHeyZedFurnitureSubsystem::Configure(int64 InEpoch,const FString& InIdentity,const FVector& InOrigin)
{
    if(Epoch!=InEpoch||Identity!=InIdentity)Reset();
    Epoch=InEpoch;Identity=InIdentity;Origin=InOrigin;
    if(!bLibraryAttempted)
    {
        bLibraryAttempted=true;const FSoftObjectPath Path(TEXT("/Game/HeyZed/Furniture/Library_world.Library_world"));
        TWeakObjectPtr<UHeyZedFurnitureSubsystem> Weak(this);
        LibraryLoad=UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateLambda([Weak,Path]()
        {
            if(auto* Self=Weak.Get())
            {
                Self->Library=Cast<UHeyZedFurnitureLibrary>(Path.ResolveObject());Self->LibraryLoad.Reset();
                if(!Self->Library){Self->bFailed=true;UE_LOG(LogTemp,Error,TEXT("heyzed furniture model library missing"));return;}
                for(const auto& Pair:Self->Chunks)for(const auto& Cell:Pair.Value.Cells)Self->Deliver(Cell.Key,Cell.Value);
                for(TActorIterator<AHeyZedLighting> It(Self->GetWorld());It;++It)It->RefreshObjectRepresentations();
            }
        }));
    }
    if(auto* Pending=Deferred.Find(Epoch))
    {
        auto Events=MoveTemp(*Pending);Deferred.Remove(Epoch);for(const auto& Event:Events)Apply(Event);
    }
}
double UHeyZedFurnitureSubsystem::WorkDeadline()
{
    if(WorkFrame!=GFrameCounter){WorkFrame=GFrameCounter;FrameDeadline=FPlatformTime::Seconds()+.002;}
    return FrameDeadline;
}
bool UHeyZedFurnitureSubsystem::ReplacesTile(FIntVector Address,const FString& Tile) const
{
    return Library&&(Library->FindBindings(Tile)||Library->FindBindings(Tile.Replace(TEXT("_on_"),TEXT("_"))));
}
void UHeyZedFurnitureSubsystem::Deliver(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts)
{
    if(!Library)return;const FIntPoint Key(FMath::FloorToInt(double(Address.X)/64),FMath::FloorToInt(double(Address.Y)/64));
    auto* Region=Regions.FindRef(Key).Get();
    if(!Region&&!Facts.IsEmpty())
    {
        Region=GetWorld()->SpawnActor<AHeyZedFurnitureRegion>();Region->Initialize(Key,Library);Regions.Add(Key,Region);
    }
    if(Region){Region->SetCell(Address,Facts);if(Region->Empty()){Region->Destroy();Regions.Remove(Key);}}
}
void UHeyZedFurnitureSubsystem::RemoveChunk(FIntPoint Key)
{
    if(auto* Chunk=Chunks.Find(Key))for(const auto& Cell:Chunk->Cells)Deliver(Cell.Key,{});
    Chunks.Remove(Key);
    const FIntPoint RegionKey(FMath::FloorToInt(double(Key.X)/8),FMath::FloorToInt(double(Key.Y)/8));
    bool Retained=false;
    for(const auto& Pair:Chunks)if(FMath::FloorToInt(double(Pair.Key.X)/8)==RegionKey.X&&FMath::FloorToInt(double(Pair.Key.Y)/8)==RegionKey.Y&&!Pair.Value.Cells.IsEmpty()){Retained=true;break;}
    if(!Retained)if(auto* Region=Regions.FindRef(RegionKey).Get()){Region->Destroy();Regions.Remove(RegionKey);}
}
void UHeyZedFurnitureSubsystem::Apply(const TSharedRef<FJsonObject>& Event)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedFurniture_Receive);
    if(Event->GetNumberField(TEXT("schema"))!=3)return;
    const int64 IncomingEpoch=Event->GetNumberField(TEXT("epoch"));
    if(IncomingEpoch!=Epoch)
    {
        if(!Deferred.Contains(IncomingEpoch)&&Deferred.Num()>=2)Deferred.Remove(Deferred.CreateConstIterator().Key());
        auto& Waiting=Deferred.FindOrAdd(IncomingEpoch);
        if(Waiting.Num()<256)Waiting.Add(Event);else{bFailed=true;UE_LOG(LogTemp,Error,TEXT("heyzed furniture deferred stream overflow"));}
        return;
    }
    const int64 Revision=Event->GetNumberField(TEXT("revision"));if(Revision<LastRevision)return;
    const FString Op=Event->GetStringField(TEXT("op"));
    if(Op==TEXT("reset")){Reset();LastRevision=Revision;return;}
    LastRevision=Revision;
    if(Op==TEXT("ready"))
    {
        bSourceReady=Chunks.Num()==int32(Event->GetNumberField(TEXT("chunks")));
        for(const auto& Pair:Chunks)if(Pair.Value.PendingRevision>Pair.Value.Revision)bSourceReady=false;
        if(!bSourceReady){bFailed=true;UE_LOG(LogTemp,Error,TEXT("heyzed furniture initial chunk count mismatch"));}
        return;
    }
    const auto& Position=Event->GetArrayField(TEXT("chunk"));if(Position.Num()!=2)return;
    const FIntPoint Key(Position[0]->AsNumber(),Position[1]->AsNumber());
    if(Op==TEXT("unload")){RemoveChunk(Key);return;}
    auto& Chunk=Chunks.FindOrAdd(Key);const int64 Generation=Event->GetNumberField(TEXT("generation"));
    if(Generation<Chunk.Generation)return;
    FCells Parsed;
    for(const auto& Value:Event->GetArrayField(TEXT("objects")))
    {
        const auto& Row=Value->AsArray();if(Row.Num()!=6){bFailed=true;return;}
        const FIntVector Address(Row[0]->AsNumber(),Row[1]->AsNumber(),Row[2]->AsNumber());
        if(FMath::FloorToInt(double(Address.X)/8)!=Key.X||FMath::FloorToInt(double(Address.Y)/8)!=Key.Y||Address.Z< -32||Address.Z>31){bFailed=true;return;}
        Parsed.FindOrAdd(Address).Add({Row[3]->AsString(),Row[4]->AsString(),Row[5]->AsString()});
    }
    if(Op==TEXT("snapshot")||Op==TEXT("change"))
    {
        const bool Full=Op==TEXT("snapshot");
        if(!Full&&Generation!=Chunk.Generation)return;
        FIntVector Changed;
        if(!Full)
        {
            const auto& A=Event->GetArrayField(TEXT("address"));if(A.Num()!=3){bFailed=true;return;}
            Changed=FIntVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
            if(FMath::FloorToInt(double(Changed.X)/8)!=Key.X||FMath::FloorToInt(double(Changed.Y)/8)!=Key.Y){bFailed=true;return;}
            for(const auto& Cell:Parsed)if(Cell.Key!=Changed){bFailed=true;return;}
        }
        const int32 Part=Event->GetNumberField(TEXT("part"));
        if(Part==0){if(Revision<=Chunk.Revision)return;Chunk.Pending.Reset();Chunk.NextPart=0;Chunk.PendingGeneration=Generation;Chunk.PendingRevision=Revision;}
        if(Chunk.PendingGeneration!=Generation||Chunk.PendingRevision!=Revision||Part!=Chunk.NextPart){bFailed=true;return;}
        ++Chunk.NextPart;
        for(auto& Cell:Parsed)Chunk.Pending.FindOrAdd(Cell.Key).Append(MoveTemp(Cell.Value));
        if(Event->GetBoolField(TEXT("last")))
        {
            if(Full)
            {
                for(const auto& Cell:Chunk.Cells)if(!Chunk.Pending.Contains(Cell.Key))Deliver(Cell.Key,{});
                Chunk.Cells=MoveTemp(Chunk.Pending);
                for(const auto& Cell:Chunk.Cells)Deliver(Cell.Key,Cell.Value);
            }
            else
            {
                auto Facts=Chunk.Pending.FindRef(Changed);Chunk.Pending.Reset();
                if(Facts.IsEmpty())Chunk.Cells.Remove(Changed);else Chunk.Cells.Add(Changed,Facts);
                Deliver(Changed,Facts);
            }
            Chunk.Generation=Generation;Chunk.Revision=Revision;
        }
    }
}
bool UHeyZedFurnitureSubsystem::StartupReady() const
{
    if(!Library||!bSourceReady||bFailed)return false;
    for(const auto& Pair:Regions)if(!Pair.Value->Ready())return false;return true;
}
TSharedRef<FJsonObject> UHeyZedFurnitureSubsystem::Diagnostics() const
{
    auto Out=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& Pair:Regions){auto Row=MakeShared<FJsonObject>();Pair.Value->Diagnostics(Row);Rows.Add(MakeShared<FJsonValueObject>(Row));}
    Out->SetArrayField(TEXT("regions"),Rows);Out->SetNumberField(TEXT("loaded_chunks"),Chunks.Num());Out->SetBoolField(TEXT("source_ready"),bSourceReady);
    Out->SetBoolField(TEXT("failed"),bFailed);Out->SetBoolField(TEXT("ready"),StartupReady());return Out;
}
AHeyZedFurnitureRegion::AHeyZedFurnitureRegion()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Furniture"));RootComponent->SetMobility(EComponentMobility::Movable);
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
}
void AHeyZedFurnitureRegion::Initialize(FIntPoint Address,UHeyZedFurnitureLibrary* Resources)
{
    Region=Address;Library=Resources;SetActorLocation(FVector(Address.X*6400.,Address.Y*6400.,0));
}
void AHeyZedFurnitureRegion::SetCell(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts)
{
    if(Facts.IsEmpty())Cells.Remove(Address);else Cells.Add(Address,Facts);
    Dirty.Add(Address);SetActorTickEnabled(true);
}
void AHeyZedFurnitureRegion::Request(const FString& Mesh)
{
    if(Loading.Contains(Mesh)||Failed.Contains(Mesh))return;
    int32 Outstanding=0;for(TActorIterator<AHeyZedFurnitureRegion> It(GetWorld());It;++It)Outstanding+=It->PendingLoads();if(Outstanding>=8)return;
    const auto* Ref=Library->Meshes.Find(Mesh);if(!Ref||Ref->IsNull()){Failed.Add(Mesh);UE_LOG(LogTemp,Error,TEXT("heyzed furniture missing mesh %s"),*Mesh);return;}
    TWeakObjectPtr<AHeyZedFurnitureRegion> Weak(this);const FSoftObjectPath Path=Ref->ToSoftObjectPath();
    auto Handle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateLambda([Weak,Mesh,Path]()
    {
        if(auto* Self=Weak.Get())
        {
            if(auto* Asset=Cast<UStaticMesh>(Path.ResolveObject()))Self->Resident.Add(Mesh,Asset);
            else{Self->Failed.Add(Mesh);UE_LOG(LogTemp,Error,TEXT("heyzed furniture load failed %s"),*Mesh);}
            Self->Loading.Remove(Mesh);Self->SetActorTickEnabled(true);
        }
    }),FStreamableManager::DefaultAsyncLoadPriority,false,true,TEXT("heyzed chunk furniture"));
    if(Handle){Loading.Add(Mesh,Handle);Handle->StartStalledHandle();}else Failed.Add(Mesh);
}
bool AHeyZedFurnitureRegion::Replace(FIntVector Address,const TArray<FHeyZedFurnitureFact>& Facts)
{
    struct FDesired {FString Owner;const FHeyZedFurnitureBinding* Binding;};
    TArray<FDesired> Selected;
    for(const auto& Fact:Facts)
    {
        const auto* Options=Library->FindBindings(Fact.Tile);if(!Options)continue;
        const FHeyZedFurnitureBinding* Choice=Options->FindByPredicate([&](const FHeyZedFurnitureBinding& B){return B.State==Fact.State;});
        if(!Choice)Choice=Options->FindByPredicate([](const FHeyZedFurnitureBinding& B){return B.State==TEXT("*");});
        if(!Choice)continue;
        Selected.Add({Fact.Id,Choice});
    }
    bool Ready=true;const auto Old=Instances.FindRef(Address);TSet<int32> Kept;TArray<FReference> Next;
    const FVector Origin=GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->Origin;
    for(const auto& Desired:Selected)
    {
        const auto* Choice=Desired.Binding;
        FTransform World=Choice->Placement;
        World.AddToTranslation(FVector((Address.X-Origin.X)*100,(Address.Y-Origin.Y)*100,(Address.Z-Origin.Z)*HeyZedMetrics::Floor));
        const int32 Existing=Old.IndexOfByPredicate([&](const FReference& Ref){return Ref.Owner==Desired.Owner&&Ref.Mesh==Choice->Mesh&&Ref.World.Equals(World);});
        if(Existing!=INDEX_NONE){Next.Add(Old[Existing]);Kept.Add(Existing);continue;}
        if(!Resident.Contains(Choice->Mesh)&&!Failed.Contains(Choice->Mesh)){Request(Choice->Mesh);Ready=false;continue;}
        UStaticMesh* Mesh=Resident.FindRef(Choice->Mesh);if(!Mesh)continue;
        auto* Group=Groups.FindRef(Choice->Mesh).Get();
        if(!Group)
        {
            Group=NewObject<UInstancedStaticMeshComponent>(this);Group->SetupAttachment(RootComponent);
            Group->SetMobility(EComponentMobility::Movable);Group->SetStaticMesh(Mesh);
            Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);Group->SetCanEverAffectNavigation(false);
            Group->SetCastShadow(true);Group->RegisterComponent();AddInstanceComponent(Group);Groups.Add(Choice->Mesh,Group);
        }
        const FPrimitiveInstanceId Id=Group->AddInstanceById(World,true);
        Next.Add({Choice->Mesh,Id,Desired.Owner,World});
    }
    for(int32 I=0;I<Old.Num();++I)if(!Kept.Contains(I))if(auto* Group=Groups.FindRef(Old[I].Mesh).Get())Group->RemoveInstanceById(Old[I].Id);
    if(Next.IsEmpty())Instances.Remove(Address);else Instances.Add(Address,MoveTemp(Next));
    return Ready;
}


void AHeyZedFurnitureRegion::Tick(float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedFurniture_Apply);
    Super::Tick(Delta);const double Deadline=GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->WorkDeadline();
    if(FPlatformTime::Seconds()>=Deadline)return;TArray<FIntVector> Completed;
    for(auto Address:Dirty)
    {
        if(Replace(Address,Cells.FindRef(Address)))Completed.Add(Address);
        if(FPlatformTime::Seconds()>=Deadline)break;
    }
    for(auto Address:Completed)Dirty.Remove(Address);
    for(auto It=Groups.CreateIterator();It;++It)if(It.Value()->GetInstanceCount()==0){Resident.Remove(It.Key());It.Value()->DestroyComponent();It.RemoveCurrent();}
    if(Dirty.IsEmpty())
    {
        for(auto It=Resident.CreateIterator();It;++It)if(!Groups.Contains(It.Key()))It.RemoveCurrent();
        if(Loading.IsEmpty())SetActorTickEnabled(false);
    }
}
bool AHeyZedFurnitureRegion::Ready() const{return Dirty.IsEmpty()&&Loading.IsEmpty()&&Failed.IsEmpty();}
void AHeyZedFurnitureRegion::Diagnostics(const TSharedRef<FJsonObject>& Out) const
{
    int32 Count=0;for(const auto& Pair:Groups)Count+=Pair.Value->GetInstanceCount();
    Out->SetNumberField(TEXT("x"),Region.X);Out->SetNumberField(TEXT("y"),Region.Y);Out->SetNumberField(TEXT("instances"),Count);
    Out->SetNumberField(TEXT("groups"),Groups.Num());Out->SetNumberField(TEXT("resident_meshes"),Resident.Num());
    Out->SetNumberField(TEXT("loading_meshes"),Loading.Num());Out->SetNumberField(TEXT("failed_meshes"),Failed.Num());
    Out->SetNumberField(TEXT("pending_squares"),Dirty.Num());Out->SetBoolField(TEXT("ready"),Ready());
}
void AHeyZedFurnitureRegion::EndPlay(const EEndPlayReason::Type Reason)
{
    for(auto& Pair:Loading)if(Pair.Value)Pair.Value->CancelHandle();Loading.Reset();
    for(auto& Pair:Groups)if(Pair.Value)Pair.Value->DestroyComponent();
    Groups.Reset();Resident.Reset();Instances.Reset();Cells.Reset();Dirty.Reset();Failed.Reset();SetActorTickEnabled(false);Super::EndPlay(Reason);
}
