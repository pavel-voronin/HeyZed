#include "HeyZedVegetation.h"
#include "HeyZedMetrics.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"

namespace {
FIntPoint RegionKey(FIntPoint P) {return {FMath::FloorToInt32(P.X/256.),FMath::FloorToInt32(P.Y/256.)};}
bool Nearby(FIntPoint P,FIntPoint Center) {return FMath::Abs(P.X-Center.X)<=1 && FMath::Abs(P.Y-Center.Y)<=1;}
}

AHeyZedVegetationRegion::AHeyZedVegetationRegion()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VegetationRoot")));
}

void AHeyZedVegetationRegion::Setup(FIntPoint Address,UHeyZedVegetationLibrary* Resources)
{
    Cell=Address;
    for(const FString Kind : {TEXT("Tree"),TEXT("Shrub"),TEXT("Pine")})
    {
        auto* Group=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
        Group->SetupAttachment(GetRootComponent());Group->SetMobility(EComponentMobility::Movable);
        Group->SetStaticMesh(Resources->Meshes.FindRef(Kind));Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Group->SetCanEverAffectNavigation(false);Group->bAutoRebuildTreeOnInstanceChanges=false;
        Group->SetCullDistances(Kind!=TEXT("Shrub")?22000:8000,Kind!=TEXT("Shrub")?25000:12000);
        Group->SetCastShadow(true);Group->RegisterComponent();AddInstanceComponent(Group);Groups.Add(Group);
    }
}

void AHeyZedVegetationRegion::Replace(FIntPoint Address,const TArray<FHeyZedPlant>& Plants,const FVector& Origin)
{
    if(Plants.IsEmpty() && !Squares.Contains(Address))return;
    auto& Old=Squares.FindOrAdd(Address);
    for(const auto& Ref : Old)
    {
        Groups[Ref.Kind]->UpdateInstanceTransform(Ref.Index,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),false,false,true);
        Free[Ref.Kind].Add(Ref.Index);
    }
    Old.Reset();
    for(const auto& Plant : Plants)
    {
        const int32 Kind=FMath::Clamp(Plant.Kind,0,2),Stage=FMath::Clamp(Plant.Stage,1,8);
        const uint32 Hash=HashCombineFast(GetTypeHash(Address),uint32(Kind));
        // Placeholder growth proportions are presentation only; native stage remains source data.
        const float Scale=Kind!=1 ? (.12f+Stage*Stage*.026f)*(Kind==2?.65f:1.f) : (.65f+(Hash%43)/100.f);
        const FVector Position((Address.X+.5-Origin.X)*100,(Address.Y+.5-Origin.Y)*100,(Plant.Z-Origin.Z)*HeyZedMetrics::Floor);
        const FTransform Transform(FRotator(0,Hash%360,0),Position,FVector(Scale));
        int32 Index;
        if(Free[Kind].Num()) {Index=Free[Kind].Pop(EAllowShrinking::No);Groups[Kind]->UpdateInstanceTransform(Index,Transform,false,false,true);}
        else Index=Groups[Kind]->AddInstance(Transform,false);
        Old.Add({Kind,Index});
    }
    if(Old.IsEmpty()) Squares.Remove(Address);
    Dirty=true;
}

void AHeyZedVegetationRegion::Finish()
{
    if(!Dirty)return;
    for(auto& Group : Groups) {Group->BuildTreeIfOutdated(true,false);Group->MarkRenderStateDirty();}
    Dirty=false;
}

void UHeyZedVegetationSubsystem::Configure(int64 InEpoch,const FString& InIdentity,const FVector& InOrigin)
{
    if(Identity!=InIdentity)
    {
        for(auto& Pair : Regions)if(Pair.Value)Pair.Value->Destroy();
        Regions.Reset();Changes.Reset();Dirty.Reset();Queued.Reset();Loads.Reset();Pending.Reset();Requested.Reset();
        Center=FIntPoint(MAX_int32,MAX_int32);Identity=InIdentity;
        Folder=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/Vegetation"),Identity);
    }
    // A transport reconnect keeps the prepared world and existing addressed visual facts.
    if(Epoch!=InEpoch) {for(auto& Pair : Changes)Pair.Value.Revision=0;Epoch=InEpoch;}
    Origin=InOrigin;
}

void UHeyZedVegetationSubsystem::Apply(const TSharedRef<FJsonObject>& Block)
{
    if(int64(Block->GetNumberField(TEXT("epoch")))!=Epoch)return;
    const auto& Cells=Block->GetArrayField(TEXT("cells"));
    for(const auto& Value : Cells)
    {
        const auto& Row=Value->AsArray();if(Row.Num()!=4)continue;
        FIntPoint Address(Row[0]->AsNumber(),Row[1]->AsNumber());const int64 Revision=Row[2]->AsNumber();
        auto& Change=Changes.FindOrAdd(Address);if(Revision<=Change.Revision)continue;
        Change.Revision=Revision;Change.Plants.Reset();
        for(const auto& Item : Row[3]->AsArray())
        {
            const auto& Plant=Item->AsArray();if(Plant.Num()!=4)continue;
            const FTCHARToUTF8 Name(*Plant[2]->AsString());
            Change.Plants.Add({int32(Plant[0]->AsNumber()),int32(Plant[1]->AsNumber()),int32(Plant[3]->AsNumber()),FCrc::MemCrc32(Name.Get(),Name.Length())});
        }
        if(!Queued.Contains(Address)) {Queued.Add(Address);Dirty.Add(Address);}
    }
}

AHeyZedVegetationRegion* UHeyZedVegetationSubsystem::Region(FIntPoint Address)
{
    auto& Actor=Regions.FindOrAdd(Address);
    if(!Actor) {Actor=GetWorld()->SpawnActor<AHeyZedVegetationRegion>();Actor->Setup(Address,Library);}
    return Actor;
}

void UHeyZedVegetationSubsystem::Tick(float Delta)
{
    auto* PC=GetWorld()->GetFirstPlayerController();APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(!Pawn || Identity.IsEmpty())return;
    if(!Library && !bResourceAttempted)
    {
        bResourceAttempted=true;
        Library=LoadObject<UHeyZedVegetationLibrary>(nullptr,TEXT("/Game/HeyZed/Vegetation/WoodyLibrary.WoodyLibrary"));
    }
    if(!Library || Library->Meshes.Num()<3)return;
    const FVector P=Pawn->GetActorLocation()/100+Origin;
    const FIntPoint Next(FMath::FloorToInt32(P.X/256.),FMath::FloorToInt32(P.Y/256.));
    if(Next!=Center)
    {
        bColdAdmission=bColdAdmission || Center.X==MAX_int32 || FMath::Abs(double(Next.X)-Center.X)>1 || FMath::Abs(double(Next.Y)-Center.Y)>1;
        Center=Next;
        for(auto It=Regions.CreateIterator();It;++It)if(!Nearby(It.Key(),Center)) {It.Value()->Destroy();Requested.Remove(It.Key());It.RemoveCurrent();}
        for(int32 Y=-1;Y<=1;++Y)for(int32 X=-1;X<=1;++X)
        {
            const FIntPoint Key=Center+FIntPoint(X,Y);if(Requested.Contains(Key))continue;Requested.Add(Key);
            const FString Path=FPaths::Combine(Folder,FString::Printf(TEXT("%d_%d.pzvg"),Key.X,Key.Y));
            Loads.Add({Key,Async(EAsyncExecution::ThreadPool,[Path,Key,P](){
                TArray<uint8> Bytes;TSharedPtr<FHeyZedVegetationBaseline> Data;
                if(!FFileHelper::LoadFileToArray(Bytes,*Path) || Bytes.Num()<12 || FMemory::Memcmp(Bytes.GetData(),"PZVG",4)!=0)return Data;
                uint32 Version,Count;FMemory::Memcpy(&Version,Bytes.GetData()+4,4);FMemory::Memcpy(&Count,Bytes.GetData()+8,4);
                if(Version!=1 || int64(Count)*8+12!=Bytes.Num())return Data;
                Data=MakeShared<FHeyZedVegetationBaseline>();TMap<FIntPoint,int32> Squares;
                for(uint32 I=0;I<Count;++I)
                {
                    const uint8* Row=Bytes.GetData()+12+I*8;FIntPoint Address(Key.X*256+Row[0],Key.Y*256+Row[1]);
                    uint32 Variant;FMemory::Memcpy(&Variant,Row+4,4);
                    int32* Index=Squares.Find(Address);
                    if(!Index){const int32 NextIndex=Data->Cells.Add({Address,{}});Index=&Squares.Add(Address,NextIndex);}
                    Data->Cells[*Index].Plants.Add({Row[2],Row[3],0,Variant});
                }
                Data->Cells.Sort([P](const FHeyZedPlantSquare& A,const FHeyZedPlantSquare& B){
                    return FMath::Square(A.Address.X-P.X)+FMath::Square(A.Address.Y-P.Y)<FMath::Square(B.Address.X-P.X)+FMath::Square(B.Address.Y-P.Y);
                });
                return Data;
            })});
        }
        for(const auto& Pair : Changes)if(Nearby(RegionKey(Pair.Key),Center) && !Queued.Contains(Pair.Key)) {Queued.Add(Pair.Key);Dirty.Add(Pair.Key);}
    }
    for(int32 I=Loads.Num()-1;I>=0;--I)if(Loads[I].Future.IsReady())
    {
        auto Data=Loads[I].Future.Get();if(Data && Nearby(Loads[I].Address,Center))Pending.Add({Loads[I].Address,Data,0});
        else if(!Nearby(Loads[I].Address,Center))Requested.Remove(Loads[I].Address);
        Loads.RemoveAtSwap(I);
    }
    const double End=FPlatformTime::Seconds()+(bColdAdmission?.025:.0015);
    TSet<AHeyZedVegetationRegion*> Touched;
    Pending.Sort([P](const FPending& A,const FPending& B){
        const auto Distance=[P](FIntPoint R){return FMath::Square(FMath::Max(FMath::Abs(R.X*256+128-P.X)-128,0.))+FMath::Square(FMath::Max(FMath::Abs(R.Y*256+128-P.Y)-128,0.));};
        return Distance(A.Address)>Distance(B.Address);
    });
    // Addressed live corrections take precedence over initial background placement.
    while(!Dirty.IsEmpty() && FPlatformTime::Seconds()<End)
    {
        const FIntPoint Address=Dirty.Pop(EAllowShrinking::No);Queued.Remove(Address);if(!Nearby(RegionKey(Address),Center))continue;
        auto* Actor=Region(RegionKey(Address));Actor->Replace(Address,Changes[Address].Plants,Origin);Touched.Add(Actor);
    }
    while(!Pending.IsEmpty() && FPlatformTime::Seconds()<End)
    {
        auto& Work=Pending.Last();if(!Nearby(Work.Address,Center)){Pending.Pop();continue;}
        auto* Actor=Region(Work.Address);const auto& Cells=Work.Data->Cells;Touched.Add(Actor);
        for(int32 Count=0;Work.Cursor<Cells.Num() && Count<32 && FPlatformTime::Seconds()<End;++Count,++Work.Cursor)
        {
            const auto& Square=Cells[Work.Cursor];
            if(!Changes.Contains(Square.Address))Actor->Replace(Square.Address,Square.Plants,Origin);
        }
        if(Work.Cursor==Cells.Num())Pending.Pop();
    }
    for(auto* Actor : Touched)Actor->Finish();
    if(Loads.IsEmpty() && Pending.IsEmpty())bColdAdmission=false;
}
