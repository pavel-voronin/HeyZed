#include "HeyZedTerrain.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HeyZedMetrics.h"
#include "HeyZedWorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshCompiler.h"
#include "DistanceFieldAtlas.h"
#include "MeshCardBuild.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/Linker.h"
#endif

bool UHeyZedGroundLibrary::IsAsphalt(FName Name)
{
    return Name.ToString().StartsWith(TEXT("Road_")) && Name!=TEXT("Road_04");
}
bool UHeyZedGroundLibrary::IsSoftSurface(FName Name)
{
    return Name.ToString().StartsWith(TEXT("Grass")) || Name==TEXT("Dirt") || Name==TEXT("Dirt_Grass") || Name==TEXT("Sand") || Name==TEXT("Clay") || Name==TEXT("Gravel") || Name==TEXT("Road_04");
}

bool UHeyZedGroundLibrary::PrepareMeshes()
{
#if WITH_EDITOR
    OverlayMeshes.Reset();
    TMap<FName,FHeyZedGroundShape> Shapes=Overlays;
    for (const auto& Pair : Shapes)
    {
        const FString Name=TEXT("SM_")+Pair.Key.ToString();
        const FString PackageName=TEXT("/Game/HeyZed/GroundShapes/")+Name;
        UStaticMesh* Mesh=FPackageName::DoesPackageExist(PackageName)?LoadObject<UStaticMesh>(nullptr,*(PackageName+TEXT(".")+Name)):nullptr;
        UPackage* Package=Mesh ? Mesh->GetOutermost() : CreatePackage(*PackageName);
        if (!Mesh)
        {
            Mesh=NewObject<UStaticMesh>(Package,*Name,RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Mesh);
        }
        {
            FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
            if (GDistanceFieldAsyncQueue) GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Mesh,false);
            if (GCardRepresentationAsyncQueue) GCardRepresentationAsyncQueue->BlockUntilBuildComplete(Mesh,false);
            Mesh->bDoFastBuild=false;
            Mesh->SetNumSourceModels(1);
            auto& BuildSettings=Mesh->GetSourceModel(0).BuildSettings;
            BuildSettings.bRecomputeNormals=false;
            BuildSettings.bRecomputeTangents=false;
            BuildSettings.bGenerateLightmapUVs=false;
            FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
            auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
            auto Tangents=Attributes.GetVertexInstanceTangents(); auto Signs=Attributes.GetVertexInstanceBinormalSigns();
            auto UV=Attributes.GetVertexInstanceUVs(); UV.SetNumChannels(1);
            auto Groups=Attributes.GetPolygonGroupMaterialSlotNames();
            const FPolygonGroupID Group=Description.CreatePolygonGroup(); Groups[Group]=Pair.Value.Material;
            for (int32 I=0;I+2<Pair.Value.Vertices.Num();I+=3)
            {
                TArray<FVertexInstanceID> Triangle;
                for (int32 Index : {I,I+2,I+1})
                {
                    const FVector2D V=Pair.Value.Vertices[Index];
                    const FVertexID Vertex=Description.CreateVertex(); Positions[Vertex]=FVector3f(V.X*100,V.Y*100,0);
                    const FVertexInstanceID Instance=Description.CreateVertexInstance(Vertex);
                    Normals[Instance]=FVector3f(0,0,1); Tangents[Instance]=FVector3f(1,0,0); Signs[Instance]=1;
                    UV.Set(Instance,0,FVector2f(V)); Triangle.Add(Instance);
                }
                Description.CreateTriangle(Group,Triangle);
            }
            Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials.FindRef(Pair.Value.Material),Pair.Value.Material));
            UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bUseHashAsGuid=true; Params.bFastBuild=false;
            if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) return false;
            FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
            if (GDistanceFieldAsyncQueue) GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Mesh,false);
            if (GCardRepresentationAsyncQueue) GCardRepresentationAsyncQueue->BlockUntilBuildComplete(Mesh,false);
            Package->MarkPackageDirty();
            FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
            const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
            if (!UPackage::SavePackage(Package,Mesh,*Filename,Args)) return false;
            if (GDistanceFieldAsyncQueue) GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Mesh,false);
            if (GCardRepresentationAsyncQueue) GCardRepresentationAsyncQueue->BlockUntilBuildComplete(Mesh,false);
        }
        OverlayMeshes.Add(Pair.Key,Mesh);
    }
    ResetLoaders(GetOutermost());
    return true;
#else
    return false;
#endif
}

TSharedPtr<FHeyZedPreparedGround> FHeyZedPreparedGround::Decode(const FString& Text)
{
    const double Start=FPlatformTime::Seconds();
    TSharedPtr<FJsonObject> Snapshot;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Snapshot) || !Snapshot) return nullptr;
    auto Result=Prepare(Snapshot.ToSharedRef());
    if (Result) Result->DecodeMs=(FPlatformTime::Seconds()-Start)*1000;
    return Result;
}
TSharedPtr<FHeyZedPreparedGround> FHeyZedPreparedGround::Prepare(const TSharedRef<FJsonObject>& Snapshot)
{
    auto Result=MakeShared<FHeyZedPreparedGround>();
    double Epoch=0,Revision=0; Snapshot->TryGetNumberField(TEXT("epoch"),Epoch); Snapshot->TryGetNumberField(TEXT("revision"),Revision);
    Result->Epoch=Epoch; Result->Revision=Revision;
    const TArray<TSharedPtr<FJsonValue>> *Dictionary=nullptr,*Data=nullptr;
    if (!Snapshot->TryGetArrayField(TEXT("dictionary"),Dictionary) || !Snapshot->TryGetArrayField(TEXT("cells"),Data)) return nullptr;
    TArray<FHeyZedGroundLayer> Definitions;
    for (const auto& Value : *Dictionary)
    {
        const auto Tile=Value->AsObject(); if (!Tile) return nullptr;
        FHeyZedGroundLayer Layer; Layer.Name=FName(*Tile->GetStringField(TEXT("name")));
        Layer.Material=FName(*Tile->GetStringField(TEXT("material"))); Layer.Overlay=Tile->GetBoolField(TEXT("overlay"));
        Tile->TryGetStringField(TEXT("edges"),Layer.Edges); Definitions.Add(MoveTemp(Layer));
    }
    for (const auto& Value : *Data)
    {
        const auto& Values=Value->AsArray(); if (Values.Num()<3) continue;
        FHeyZedPreparedCell Cell; Cell.Address={static_cast<int32>(Values[0]->AsNumber()),static_cast<int32>(Values[1]->AsNumber())};
        if (Values.Num()>=5) { Cell.Version=Values[3]->AsNumber(); Cell.Generation=Values[4]->AsNumber(); }
        for (const auto& Index : Values[2]->AsArray()) if (Definitions.IsValidIndex(Index->AsNumber()))
        {
            const auto& Layer=Definitions[Index->AsNumber()]; Cell.Layers.Add(Layer);
            Cell.Signature+=Layer.Name.ToString()+TEXT(":")+Layer.Material.ToString()+TEXT(":")+(Layer.Overlay?TEXT("1"):TEXT("0"))+Layer.Edges+TEXT(";");
        }
        Result->Cells.Add(MoveTemp(Cell));
    }
    if (!Result->Cells.IsEmpty()) Result->Center=Result->Cells[0].Address;
    return Result;
}

void UHeyZedGroundLibrary::Resolve(const TArray<FHeyZedGroundLayer>& Layers,TArray<FName>& Parts,FName& Base) const
{
    Parts.Reset(); Base=NAME_None;
    for (const auto& Layer : Layers)
    {
        if (!Layer.Overlay)
        {
            Base=Layer.Material.ToString().StartsWith(TEXT("Grass"))?FName(TEXT("Grass")):Layer.Material;
            if (Base.IsNone() || !Materials.Contains(Base)) Base=TEXT("Unknown");
        }
        if (const auto* Tile=Tiles.Find(Layer.Name)) Parts.Append(Tile->Parts);
        else if (!Layer.Overlay) Parts.Add(FName(*(TEXT("GroundBase_")+Base.ToString())));
    }
}
bool UHeyZedGroundLibrary::GrassAt(const TArray<FName>& Parts,FVector2D Point,FName Base) const
{
    for (int32 P=Parts.Num()-1;P>=0;--P)
    {
        const auto* Shape=Overlays.Find(Parts[P]); if (!Shape || Shape->Material==TEXT("PaintHidden")) continue;
        for (int32 I=0;I+2<Shape->Vertices.Num();I+=3)
        {
            const auto A=Shape->Vertices[I],B=Shape->Vertices[I+1],C=Shape->Vertices[I+2];
            auto Cross=[](FVector2D X,FVector2D Y){return X.X*Y.Y-X.Y*Y.X;};
            const double U=Cross(B-A,Point-A),V=Cross(C-B,Point-B),W=Cross(A-C,Point-C);
            if ((U>=-1e-8 && V>=-1e-8 && W>=-1e-8) || (U<=1e-8 && V<=1e-8 && W<=1e-8))
            {
                const auto& D=Shape->Boundary;
                return Shape->Material==TEXT("Grass") && (D.W<=0 || D.X*Point.X+D.Y*Point.Y+D.Z>=D.W);
            }
        }
    }
    return Parts.IsEmpty() && Base==TEXT("Grass");
}

namespace {
FIntPoint GrassAddress(FIntPoint Cell) { return {int32(FMath::FloorToInt(Cell.X/8.)),int32(FMath::FloorToInt(Cell.Y/8.))}; }
}
AHeyZedTerrain::AHeyZedTerrain()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); PrimaryActorTick.bCanEverTick=true;
}
void AHeyZedTerrain::Apply(const FHeyZedPreparedGround& Snapshot,const FVector& NativeOrigin)
{
    if (!Library) Library=LoadObject<UHeyZedGroundLibrary>(nullptr,TEXT("/Game/HeyZed/GroundLibrary.GroundLibrary"));
    if (!Library) return;
    GroundOrigin=NativeOrigin;
    TSet<FIntPoint> Affected; ++AppliedPatches;
    for (const auto& Data : Snapshot.Cells)
    {
        FHeyZedGroundCell& Cell=Cells.FindOrAdd(Data.Address);
        if (Snapshot.bBaked && Cell.bLive) continue;
        if (Snapshot.Epoch<Cell.SourceEpoch || (Snapshot.Epoch==Cell.SourceEpoch && Data.Version>=0 && Data.Version<=Cell.SourceRevision)) continue;
        const bool WasLive=Cell.bLive;
        if (!Snapshot.bBaked)
        {
            Cell.bLive=true;
            if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) HeyZed->OverrideBase(Data.Address);
        }
        Cell.SourceEpoch=Snapshot.Epoch; Cell.SourceRevision=Data.Version; Cell.Generation=Data.Generation;
        if (Cell.Signature==Data.Signature && WasLive) continue;
        Cell.Signature=Data.Signature; Cell.Layers=Data.Layers;
        Affected.Add(Data.Address);
    }
    BuildCells(Affected,NativeOrigin);
}
void AHeyZedTerrain::SetArchitectureFloor(FIntPoint Address,bool Claimed)
{
    if (ArchitectureFloors.Contains(Address)==Claimed) return;
    if (Claimed) ArchitectureFloors.Add(Address);else ArchitectureFloors.Remove(Address);
    if (auto* Cell=Cells.Find(Address)) Cell->RenderSignature.Reset();
    DirtyGrass.Add(Address);
    if (Library) BuildCells({Address},GroundOrigin);
}

void AHeyZedTerrain::BuildCells(const TSet<FIntPoint>& Addresses,const FVector& NativeOrigin)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedGround_BuildCells);
    struct FBatch { TArray<FTransform> Transforms; TArray<FIntPoint> Owners; TArray<float> Custom; UStaticMesh* Mesh=nullptr; UMaterialInterface* Material=nullptr; };
    TMap<FName,FBatch> Batches;
    for (FIntPoint Address : Addresses)
    {
        auto* CellPtr=Cells.Find(Address); if (!CellPtr || (!CellPtr->bLive && CellPtr->Layers.IsEmpty())) continue;
        auto& Cell=*CellPtr;
        FName Base; TArray<FName> Parts; Library->Resolve(Cell.Layers,Parts,Base);
        const FString RenderSignature=Cell.Signature;
        if (Cell.RenderSignature==RenderSignature) continue;
        for (const auto& Instance : Cell.Instances) if (UInstancedStaticMeshComponent* Mesh=Meshes.FindRef(Instance.Key)) Mesh->RemoveInstanceById(Instance.Id);
        Cell.Instances.Reset(); Tiles-=Cell.Tiles; Cell.Tiles=0;
        for (const FString& Name : Cell.Unresolved) if (--UnresolvedCounts.FindChecked(Name)==0) UnresolvedCounts.Remove(Name);
        Cell.Unresolved.Reset();
        Cell.Base=Base; Cell.Blend=Base; Cell.bBlend=false; Cell.Parts=Parts;
        if (ArchitectureFloors.Contains(Address)) {Cell.RenderSignature=RenderSignature;continue;}
        const FVector Position((Address.X-NativeOrigin.X)*100,(Address.Y-NativeOrigin.Y)*100,-NativeOrigin.Z*HeyZedMetrics::Floor);
        auto Add=[&](FName Key,UStaticMesh* Shape,UMaterialInterface* Material,const FTransform& Transform,const FVector4f& Custom,const FVector4f& EdgeData)
        {
            if (!Shape) return;
            auto& Batch=Batches.FindOrAdd(Key); Batch.Mesh=Shape; if (Material) Batch.Material=Material;
            Batch.Transforms.Add(Transform); Batch.Owners.Add(Address);
            Batch.Custom.Append({Custom.X,Custom.Y,Custom.Z,Custom.W,EdgeData.X,EdgeData.Y,EdgeData.Z,EdgeData.W});
        };
        int32 LayerIndex=0;
        for (FName Part : Parts)
        {
            const auto* Definition=Library->Overlays.Find(Part);
            UStaticMesh* Shape=Library->OverlayMeshes.FindRef(Part);
            if (!Definition || !Shape) {Cell.Unresolved.Add(Part.ToString());continue;}
            Add(Part,Shape,nullptr,FTransform(Position+FVector(0,0,LayerIndex++*.02)),FVector4f(Definition->Boundary),FVector4f(0,0,0,0));
        }
        if (!Parts.IsEmpty()) {Cell.Tiles=1; ++Tiles;}
        for (const FString& Name : Cell.Unresolved) ++UnresolvedCounts.FindOrAdd(Name);
        Cell.RenderSignature=RenderSignature; ++RebuiltCells; if (GrassChunks.Contains(GrassAddress(Address))) DirtyGrass.Add(Address);
    }
    for (auto& Pair : Batches)
    {
        auto& Mesh=Meshes.FindOrAdd(Pair.Key);
        if (!Mesh)
        {
            Mesh=NewObject<UInstancedStaticMeshComponent>(this); Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCanEverAffectNavigation(false); Mesh->SetStaticMesh(Pair.Value.Mesh); Mesh->NumCustomDataFloats=8;
            if (Pair.Value.Material) Mesh->SetMaterial(0,Pair.Value.Material);
            Mesh->RegisterComponent();
        }
        const auto Ids=Mesh->AddInstancesById(Pair.Value.Transforms,false,false);
        Mesh->SetCustomDataById(Ids,Pair.Value.Custom);
        for (int32 I=0;I<Ids.Num();++I) Cells.FindChecked(Pair.Value.Owners[I]).Instances.Add({Pair.Key,Ids[I]});
    }
    Unresolved.Reset(); for (const auto& Pair : UnresolvedCounts) Unresolved.Add(Pair.Key);
}
void AHeyZedTerrain::BaseLoaded(FIntPoint NativeCell)
{
    for (auto& Pair : Cells) if (!Pair.Value.bLive && FMath::FloorToInt(Pair.Key.X/256.)==NativeCell.X && FMath::FloorToInt(Pair.Key.Y/256.)==NativeCell.Y) {Pair.Value.Base=NAME_None;Pair.Value.Parts.Reset();}
    for (const auto& Pair : GrassChunks)
    {
        const FIntPoint Region(int32(FMath::FloorToInt(Pair.Key.X/32.)),int32(FMath::FloorToInt(Pair.Key.Y/32.)));
        if (Region!=NativeCell) continue;
        for (int32 Y=0;Y<8;++Y) for (int32 X=0;X<8;++X) DirtyGrass.Add(Pair.Key*8+FIntPoint(X,Y));
    }
}
void AHeyZedTerrain::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Library) Library=LoadObject<UHeyZedGroundLibrary>(nullptr,TEXT("/Game/HeyZed/GroundLibrary.GroundLibrary"));
    if (!Library || !Library->GrassMesh || FPlatformTime::Seconds()<NextGrass) return;
    NextGrass=FPlatformTime::Seconds()+.2;
    auto* PC=GetWorld()->GetFirstPlayerController(); if (!PC || !PC->GetPawn()) return;
    const FVector Player=PC->GetPawn()->GetActorLocation();
    const FIntPoint Center(FMath::FloorToInt((Player.X/100+GroundOrigin.X)/8),FMath::FloorToInt((Player.Y/100+GroundOrigin.Y)/8));
    for (auto It=GrassChunks.CreateIterator();It;++It)
        if (FMath::Abs(It.Key().X-Center.X)>4 || FMath::Abs(It.Key().Y-Center.Y)>4)
        {
            for (int32 Y=0;Y<8;++Y) for (int32 X=0;X<8;++X)
            {
                const FIntPoint Address=It.Key()*8+FIntPoint(X,Y);
                if (auto* Cell=Cells.Find(Address)) Cell->Grass.Reset();
                DirtyGrass.Remove(Address);
            }
            It.Value()->DestroyComponent(); It.RemoveCurrent();
        }
    TSet<FIntPoint> Candidates;
    for (int32 Y=Center.Y-4;Y<=Center.Y+4;++Y) for (int32 X=Center.X-4;X<=Center.X+4;++X)
        if (!GrassChunks.Contains({X,Y})) Candidates.Add({X,Y});
    for (FIntPoint Address : DirtyGrass) Candidates.Add(GrassAddress(Address));
    TArray<FIntPoint> Needed=Candidates.Array();
    Needed.Sort([Center](FIntPoint A,FIntPoint B){return (A-Center).SizeSquared()<(B-Center).SizeSquared();});
    int32 Built=0;
    for (FIntPoint Chunk : Needed)
    {
        if (Built>=2) break;
        auto& Component=GrassChunks.FindOrAdd(Chunk); const bool Initial=!Component;
        if (Initial)
        {
            Component=NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
            Component->SetupAttachment(RootComponent); Component->SetMobility(EComponentMobility::Movable);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetCanEverAffectNavigation(false);
            Component->SetStaticMesh(Library->GrassMesh); Component->SetMaterial(0,Library->GrassMaterial);
            Component->SetCastShadow(false); Component->SetCullDistances(1600,2400);
            Component->bAutoRebuildTreeOnInstanceChanges=false; Component->RegisterComponent();
        }
        TArray<FTransform> Instances; TArray<FIntPoint> Owners;
        for (int32 Y=0;Y<8;++Y) for (int32 X=0;X<8;++X)
        {
            const FIntPoint Address=Chunk*8+FIntPoint(X,Y);
            if (!Initial && !DirtyGrass.Contains(Address)) continue;
            DirtyGrass.Remove(Address);
            auto* Cell=Cells.Find(Address);
            if (!Cell || (!Cell->bLive && Cell->Base.IsNone()))
            {
                FHeyZedGroundCell Baked;
                if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) if (HeyZed->BaseSurface(Address,Baked))
                {
                    Cell=&Cells.FindOrAdd(Address); Cell->Base=Baked.Base; Cell->Blend=Baked.Blend;
                    Cell->Corners=Baked.Corners; Cell->Edges=Baked.Edges; Cell->bBlend=Baked.bBlend; Cell->Parts=Baked.Parts;
                }
            }
            if (!Cell) continue;
            for (FPrimitiveInstanceId Id : Cell->Grass) Component->RemoveInstanceById(Id);
            Cell->Grass.Reset();
            FRandomStream Random(HashCombine(GetTypeHash(Address.X),GetTypeHash(Address.Y)));
            for (int32 I=0;I<16;++I)
            {
                const FVector2D Point((I%4+Random.FRand())/4,(I/4+Random.FRand())/4);
                const float Coverage=!ArchitectureFloors.Contains(Address) && Library->GrassAt(Cell->Parts,Point,Cell->Base)?1.f:0.f;
                if (Coverage<.05f) continue;
                const FVector Location((Address.X+Point.X-GroundOrigin.X)*100,(Address.Y+Point.Y-GroundOrigin.Y)*100,-GroundOrigin.Z*HeyZedMetrics::Floor);
                const float Scale=Random.FRandRange(.85,1.15)*FMath::Sqrt(Coverage);
                Instances.Add(FTransform(FRotator(0,Random.FRand()*360,0),Location,FVector(Scale,Scale,Scale))); Owners.Add(Address);
            }
        }
        const auto Ids=Component->AddInstancesById(Instances,false,false);
        for (int32 I=0;I<Ids.Num();++I) Cells.FindChecked(Owners[I]).Grass.Add(Ids[I]);
        Component->BuildTreeIfOutdated(true,false); ++Built;
    }
}
