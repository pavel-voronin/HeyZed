#include "HeyZedBakedRegion.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HeyZedWorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Level.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#endif

AHeyZedBakedRegion::AHeyZedBakedRegion()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent->SetMobility(EComponentMobility::Static);
    PrimaryActorTick.bCanEverTick=true;
    SetActorHiddenInGame(true);
}
bool AHeyZedBakedRegion::ConfigureWorld(UWorld* World)
{
#if WITH_EDITOR
    if (!World) return false;
    auto* Partition=UWorldPartition::CreateOrRepairWorldPartition(World->GetWorldSettings(),{},UWorldPartitionRuntimeSpatialHash::StaticClass());
    if (!Partition) return false;
    Partition->SetEnableStreaming(true);
    // Configure the engine grid, rather than scheduling or loading render pages in bridge code.
    for (TObjectIterator<UWorldPartitionRuntimeSpatialHash> It;It;++It) if (It->GetOuter()==Partition)
    {
        FArrayProperty* Property=FindFProperty<FArrayProperty>(It->GetClass(),TEXT("Grids"));
        if (!Property) return false;
        FScriptArrayHelper Grids(Property,Property->ContainerPtrToValuePtr<void>(*It));
        if (Grids.Num()==0) Grids.AddValue();
        auto* Grid=reinterpret_cast<FSpatialHashRuntimeGrid*>(Grids.GetRawPtr(0));
        Grid->GridName=TEXT("Landscape"); Grid->CellSize=25600; Grid->LoadingRange=204800;
        Grid->Origin=FVector2D::ZeroVector; Grid->bBlockOnSlowStreaming=false;
        break;
    }
    World->PersistentLevel->SetUseExternalActors(true);
    World->MarkPackageDirty(); return true;
#else
    return false;
#endif
}
bool AHeyZedBakedRegion::Author(const FString& Filename,const FString& MapIdentity)
{
#if WITH_EDITOR
    FString Text; if (!FFileHelper::LoadFileToString(Text,*Filename)) return false;
    TSharedPtr<FJsonObject> Document;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document)) return false;
    auto* Library=LoadObject<UHeyZedGroundLibrary>(nullptr,TEXT("/Game/HeyZed/GroundLibrary.GroundLibrary"));
    auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!Library || !Plane) return false;
    Cell={Document->GetIntegerField(TEXT("x")),Document->GetIntegerField(TEXT("y"))}; Identity=MapIdentity;
    SetActorLocation(FVector(Cell.X*25600.,Cell.Y*25600.,0)); SetRuntimeGrid(TEXT("Landscape")); SetIsSpatiallyLoaded(true);
    if (Document->GetIntegerField(TEXT("schema"))!=2) return false;
    Styles.Reset(); Styles.AddDefaulted();
    for (const auto& Value : Document->GetArrayField(TEXT("styles")))
    {
        if (Value->Type==EJson::Null) continue;
        const auto Source=Value->AsObject(); FHeyZedBakedStyle Style;
        Style.Base=FName(*Source->GetStringField(TEXT("base")));Style.Blend=Style.Base;
        Style.Uniform=Source->GetBoolField(TEXT("uniform"));
        for (const auto& Part : Source->GetArrayField(TEXT("parts")))
        {
            const FName Key(*Part->AsString());
            if (!Library->Overlays.Contains(Key) || !Library->OverlayMeshes.Contains(Key)) return false;
            Style.Parts.Add(Key);
        }
        Styles.Add(MoveTemp(Style));
    }
    TArray<uint8> Encoded;
    if (!FFileHelper::LoadFileToArray(Encoded,*FPaths::ChangeExtension(Filename,TEXT("grid")))) return false;
    Grid.SetNumUninitialized(65536);
    if (!FCompression::UncompressMemory(NAME_Zlib,Grid.GetData(),131072,Encoded.GetData(),Encoded.Num())) return false;
    for (uint16 Id : Grid) if (!Styles.IsValidIndex(Id)) return false;
    for (UInstancedStaticMeshComponent* Component : Surfaces) if (Component) {RemoveInstanceComponent(Component);Component->DestroyComponent();}
    Surfaces.Reset();
    int64 EncodedSize=FCompression::CompressMemoryBound(NAME_Zlib,131072);
    CompressedGrid.SetNumUninitialized(EncodedSize);
    if (!FCompression::CompressMemory(NAME_Zlib,CompressedGrid.GetData(),EncodedSize,Grid.GetData(),131072)) return false;
    CompressedGrid.SetNum(EncodedSize);
    TArray<FIntVector> Rects; TArray<FIntPoint> Sizes; TBitArray<> Visited(false,65536);
    for (int32 Y=0;Y<256;++Y) for (int32 X=0;X<256;++X)
    {
        const int32 At=Y*256+X; const uint16 Id=Grid[At]; if (!Id || Visited[At]) continue;
        int32 Width=1,Height=1;
        if (Styles[Id].Uniform)
        {
            while (X+Width<256 && !Visited[At+Width] && Grid[At+Width]==Id) ++Width;
            while (Y+Height<256)
            {
                bool Match=true;
                for (int32 I=0;I<Width;++I) if (Visited[(Y+Height)*256+X+I] || Grid[(Y+Height)*256+X+I]!=Id) { Match=false; break; }
                if (!Match) break; ++Height;
            }
        }
        for (int32 DY=0;DY<Height;++DY) for (int32 DX=0;DX<Width;++DX) Visited[(Y+DY)*256+X+DX]=true;
        Rects.Add({X,Y,Id}); Sizes.Add({Width,Height});
    }
    struct FBatch { UStaticMesh* Mesh=nullptr; UMaterialInterface* Material=nullptr; TArray<FTransform> Instances; TArray<float> Custom; };
    TMap<FName,FBatch> Batches;
    for (int32 I=0;I<Rects.Num();++I)
    {
        const auto& Rect=Rects[I];const auto& Size=Sizes[I];const auto& Style=Styles[Rect.Z];
        if (Style.Uniform)
        {
            const FName Key(*(TEXT("UniformSurface_")+Style.Base.ToString()));
            auto& Batch=Batches.FindOrAdd(Key);Batch.Mesh=Plane;Batch.Material=Library->Materials.FindRef(Style.Base);
            const double X=Rect.X*100,Y=Rect.Y*100,W=Size.X,H=Size.Y;
            Batch.Instances.Add(FTransform(FQuat::Identity,FVector(X+W*50,Y+H*50,0),FVector(W,H,1)));
            Batch.Custom.Append({0,0,0,0,0,0,0,0});
            continue;
        }
        int32 Layer=0;
        for (FName Key : Style.Parts)
        {
            const auto& Definition=Library->Overlays.FindChecked(Key);
            auto& Batch=Batches.FindOrAdd(Key);Batch.Mesh=Library->OverlayMeshes.FindChecked(Key);
            Batch.Instances.Add(FTransform(FVector(Rect.X*100,Rect.Y*100,Layer++*.02)));
            const auto D=Definition.Boundary;Batch.Custom.Append({float(D.X),float(D.Y),float(D.Z),float(D.W),0,0,0,0});
        }
    }
    for (auto& Pair : Batches)
    {
        auto* Component=NewObject<UInstancedStaticMeshComponent>(this,NAME_None,RF_Transactional);
        Component->SetupAttachment(RootComponent); Component->SetMobility(EComponentMobility::Static);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false); Component->SetStaticMesh(Pair.Value.Mesh); Component->NumCustomDataFloats=8;
        if (Pair.Value.Material) Component->SetMaterial(0,Pair.Value.Material);
        AddInstanceComponent(Component); Component->RegisterComponent();
        const auto Ids=Component->AddInstancesById(Pair.Value.Instances,false,false); Component->SetCustomDataById(Ids,Pair.Value.Custom);
        Surfaces.Add(Component);
    }
    MarkPackageDirty(); return true;
#else
    return false;
#endif
}
bool AHeyZedBakedRegion::ApplyArchitectureMask(const FString& Filename)
{
#if WITH_EDITOR
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes,*Filename) || Bytes.Num()!=8192) return false;
    ArchitectureMask=MoveTemp(Bytes); MarkPackageDirty(); return true;
#else
    return false;
#endif
}
void AHeyZedBakedRegion::BeginPlay()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedGround_Activate);
    Super::BeginPlay();
    Grid.SetNumUninitialized(65536);
    if (!FCompression::UncompressMemory(NAME_Zlib,Grid.GetData(),Grid.Num()*sizeof(uint16),CompressedGrid.GetData(),CompressedGrid.Num())) { Grid.Reset(); return; }
    MaskBytes.Init(255,65536);
    if (ArchitectureMask.Num()==8192) for (int32 I=0;I<65536;++I) if (ArchitectureMask[I>>3]&(1u<<(I&7))) MaskBytes[I]=0;
    Mask=UTexture2D::CreateTransient(256,256,PF_G8);
    Mask->SRGB=false; Mask->Filter=TF_Nearest; Mask->AddressX=TA_Clamp; Mask->AddressY=TA_Clamp;
    auto& Mip=Mask->GetPlatformData()->Mips[0]; void* MaskData=Mip.BulkData.Lock(LOCK_READ_WRITE); FMemory::Memcpy(MaskData,MaskBytes.GetData(),65536); Mip.BulkData.Unlock(); Mask->UpdateResource();
    for (int32 Index=0;Index<Surfaces.Num();++Index)
    {
        UInstancedStaticMeshComponent* Component=Surfaces[Index];
        auto* Material=Component->CreateDynamicMaterialInstance(0);
        Material->SetTextureParameterValue(TEXT("BakedMask"),Mask);
        Material->SetScalarParameterValue(TEXT("BakedMaskEnabled"),1);
        Material->SetVectorParameterValue(TEXT("BakedRegion"),FLinearColor(Cell.X*256,Cell.Y*256,0,0));
    }
    if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) HeyZed->RegisterBase(this);
}
void AHeyZedBakedRegion::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) HeyZed->UnregisterBase(this);
    Super::EndPlay(Reason);
}
void AHeyZedBakedRegion::Activate(bool Enabled) { SetActorHiddenInGame(!Enabled); }
void AHeyZedBakedRegion::Override(FIntPoint Address)
{
    const FIntPoint Local=Address-Cell*256;
    if (Local.X<0 || Local.Y<0 || Local.X>=256 || Local.Y>=256 || MaskBytes.IsEmpty()) return;
    const int32 Index=Local.Y*256+Local.X;
    if (MaskBytes[Index]) { MaskBytes[Index]=0; DirtyMask=true; }
}
void AHeyZedBakedRegion::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!DirtyMask || !Mask || !Mask->GetResource()) return;
    DirtyMask=false;
    auto* Pixels=new uint8[65536]; FMemory::Memcpy(Pixels,MaskBytes.GetData(),65536);
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,256,256);
    Mask->UpdateTextureRegions(0,1,Region,256,1,Pixels,[](uint8* Data,const FUpdateTextureRegion2D* Regions){delete[] Data;delete Regions;});
}
bool AHeyZedBakedRegion::Surface(FIntPoint Address,FHeyZedGroundCell& Out) const
{
    const FIntPoint Local=Address-Cell*256;
    if (Local.X<0 || Local.Y<0 || Local.X>=256 || Local.Y>=256 || Grid.IsEmpty()) return false;
    const int32 At=Local.Y*256+Local.X;
    if (ArchitectureMask.Num()==8192 && (ArchitectureMask[At>>3]&(1u<<(At&7)))) return false;
    const uint16 Index=Grid[At]; if (!Styles.IsValidIndex(Index) || !Index) return false;
    const auto& Style=Styles[Index]; Out.Base=Style.Base; Out.Blend=Style.Blend;Out.Parts=Style.Parts;
    Out.Corners=Style.Corners; Out.Edges=Style.Edges; Out.bBlend=Style.Base!=Style.Blend;
    Out.bBlend=Out.Base!=Out.Blend;
    return true;
}
