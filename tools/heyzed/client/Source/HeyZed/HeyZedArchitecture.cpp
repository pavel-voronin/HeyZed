#include "HeyZedArchitecture.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HeyZedWorldSubsystem.h"
#include "HeyZedMetrics.h"
#include "HeyZedConstructionSolid.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "Tasks/Pipe.h"
#include "Tasks/Task.h"
#include "Tasks/TaskConcurrencyLimiter.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/SecureHash.h"
#if WITH_EDITOR
#include "HeyZedMeshAuthoring.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODHashBuilder.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#endif

struct FHeyZedArchitectureMetadata
{
    bool Valid=false;
    FVector2D Minimum=FVector2D::ZeroVector,Maximum=FVector2D::ZeroVector;
    double Floor=244.94900703430176,Thickness=12,Slab=15;
    TMap<FString,TSharedPtr<FJsonObject>> Cells;
    TSet<FString> OwnedEdges,ContextEdges,OwnedFloors,Footprint,Interior;
};


struct FHeyZedConstructionWork
{
    FString Address;
    uint64 Version=0;
    FHeyZedConstructionSnapshot Snapshot;
    TArray<FHeyZedConstructionPlacement> Parts;
};

namespace {
TAutoConsoleVariable<int32> ConstructionWorkerCount(TEXT("heyzed.Architecture.PlanWorkers"),2,TEXT("Maximum concurrent construction plans; read when the first plan starts."));
UE::Tasks::FTaskConcurrencyLimiter& ConstructionPlans()
{
    static UE::Tasks::FTaskConcurrencyLimiter Limiter(FMath::Clamp(ConstructionWorkerCount.GetValueOnGameThread(),1,8),UE::Tasks::ETaskPriority::BackgroundNormal);
    return Limiter;
}
UE::Tasks::FPipe ArchitectureMetadataPipe(TEXT("heyzed architecture metadata"));
TSharedRef<FJsonObject> Read(const FString& Text)
{
    TSharedPtr<FJsonObject> Value;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Value) || !Value) return MakeShared<FJsonObject>();
    return Value.ToSharedRef();
}
FString Write(const TSharedRef<FJsonObject>& Value)
{
    FString Text; FJsonSerializer::Serialize(Value,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
}
bool Equal(const TSharedRef<FJsonObject>& A,const TSharedRef<FJsonObject>& B)
{
    return FJsonValue::CompareEqual(FJsonValueObject(A),FJsonValueObject(B));
}
bool OnlyAttachmentsChanged(const TSharedRef<FJsonObject>& A,const TSharedRef<FJsonObject>& B)
{
    auto Structure=[](const TSharedRef<FJsonObject>& Cell)
    {
        auto Copy=Read(Write(Cell));
        for(const auto& W:Copy->GetArrayField(TEXT("walls")))W->AsObject()->RemoveField(TEXT("attachments"));
        return Copy;
    };
    return Equal(Structure(A),Structure(B));
}

}

namespace {
int32 AlignConstructionFacing(FMeshDescription& Description)
{
    FStaticMeshAttributes Attributes(Description);
    const auto Positions=Attributes.GetVertexPositions();const auto Normals=Attributes.GetVertexInstanceNormals();
    TArray<FPolygonID> Reverse;
    for(const auto Polygon:Description.Polygons().GetElementIDs())
        for(const auto Triangle:Description.GetPolygonTriangles(Polygon))
        {
            const auto Corners=Description.GetTriangleVertexInstances(Triangle);
            const auto A=Positions[Description.GetVertexInstanceVertex(Corners[0])];
            const auto B=Positions[Description.GetVertexInstanceVertex(Corners[1])];
            const auto C=Positions[Description.GetVertexInstanceVertex(Corners[2])];
            const auto Front=FVector3f::CrossProduct(C-A,B-A).GetSafeNormal();
            const auto Normal=(Normals[Corners[0]]+Normals[Corners[1]]+Normals[Corners[2]]).GetSafeNormal();
            if(Front.IsNearlyZero()||Normal.IsNearlyZero())continue;
            if(FVector3f::DotProduct(Front,Normal)<-.5f)Reverse.Add(Polygon);
            break;
        }
    for(const auto Polygon:Reverse)Description.ReversePolygonFacing(Polygon);
    return Reverse.Num();
}
}

int32 UHeyZedArchitectureResources::RepairMeshFacing(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if(!Mesh||!Mesh->GetPathName().StartsWith(TEXT("/Game/HeyZed/Architecture/Meshes/")))return -1;
    FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
    auto* Description=Mesh->GetMeshDescription(0);if(!Description)return -1;
    const int32 Count=AlignConstructionFacing(*Description);
    if(Count)
    {
        Mesh->CommitMeshDescription(0);Mesh->Build(false);FinishHeyZedMesh(Mesh);Mesh->MarkPackageDirty();
        UE_LOG(LogTemp,Display,TEXT("heyzed construction facing: %s, %d polygons repaired"),*Mesh->GetPathName(),Count);
    }
    return Count;
#else
    return -1;
#endif
}

TSharedPtr<FJsonObject> UHeyZedArchitectureResources::Definition(const FString& Name)
{
    if(auto* Cached=ParsedDefinitions.Find(Name))return *Cached;
    const auto* Text=Definitions.Find(Name);if(!Text)return nullptr;
    auto Parsed=Read(*Text);ParsedDefinitions.Add(Name,Parsed);return Parsed;
}
TSharedPtr<FJsonObject> UHeyZedArchitectureResources::Source(const FString& Name)
{
    if(auto* Cached=ParsedSources.Find(Name))return *Cached;
    const auto* Text=SourceBindings.Find(Name);if(!Text)return nullptr;
    auto Parsed=Read(*Text);ParsedSources.Add(Name,Parsed);return Parsed;
}
bool UHeyZedArchitectureResources::PrepareLibrary(const FString& Index,const FString& Directory)
{
#if WITH_EDITOR
    const auto Library=Read(Index);const TSharedPtr<FJsonObject>* Entries=nullptr;
    if(!Library->TryGetObjectField(TEXT("meshes"),Entries))return false;
    int32 Count=0;
    for(const auto& Entry:(*Entries)->Values)
    {
        FString Text;if(!FFileHelper::LoadFileToString(Text,*FPaths::Combine(Directory,Entry.Value->AsObject()->GetStringField(TEXT("file")))))return false;
        UStaticMesh* Mesh=Resolve(Text,true);if(Mesh)Meshes.Add(FString(Entry.Key),Mesh);
        if(++Count%100==0)UE_LOG(LogTemp,Display,TEXT("heyzed construction library: %d / %d"),Count,(*Entries)->Values.Num());
    }
    PlanningCatalog.Reset();Definitions.Reset();ParsedDefinitions.Reset();
    for(const auto& Entry:Library->GetObjectField(TEXT("definitions"))->Values)Definitions.Add(FString(Entry.Key),Write(Entry.Value->AsObject().ToSharedRef()));
    MarkPackageDirty();return true;
#else
    return false;
#endif
}

FSoftObjectPath UHeyZedArchitectureResources::PendingAsset(const FString& Recipe,const TSharedPtr<FJsonObject>& Parsed) const
{
    const auto Value=Parsed?Parsed.ToSharedRef():Read(Recipe);FString Alias;
    const FString Key=Value->TryGetStringField(TEXT("mesh"),Alias)?Alias:FMD5::HashAnsiString(*(TEXT("architecture-v1:")+Recipe));
    const auto* Found=Meshes.Find(Key);
    if(!Found||Found->IsNull()||Found->Get())return {};
    const auto Path=Found->ToSoftObjectPath();
    return Path.IsAsset()?Path:FSoftObjectPath();
}

bool UHeyZedArchitectureResources::IsEmptyRecipe(const FString& Recipe) const
{
    return EmptyRecipes.Contains(FMD5::HashAnsiString(*(TEXT("architecture-v1:")+Recipe)));
}

UStaticMesh* UHeyZedArchitectureResources::Resolve(const FString& Recipe,bool Save,const TSharedPtr<FJsonObject>& Parsed)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedConstruction_Resolve);
    const FString Hash=FMD5::HashAnsiString(*(TEXT("architecture-v1:")+Recipe));
    if(EmptyRecipes.Contains(Hash))return nullptr;
    if (auto* Found=Meshes.Find(Hash))
    {
        UStaticMesh* Existing=Save?Found->LoadSynchronous():Found->Get();
        if(!Existing&&!Save&&Found->ToSoftObjectPath().IsAsset())return nullptr;
        if (Existing && Existing->GetNumTriangles(0)>0) return Existing;
        Meshes.Remove(Hash);if(Existing)return nullptr;
    }
    const auto Value=Parsed?Parsed.ToSharedRef():Read(Recipe);FString Alias;
    if(Value->TryGetStringField(TEXT("mesh"),Alias))
    {
        auto* Found=Meshes.Find(Alias);return Found?(Save?Found->LoadSynchronous():Found->Get()):nullptr;
    }
    const FString Name=TEXT("SM_")+Hash,PackageName=TEXT("/Game/HeyZed/Architecture/Meshes/")+Name;
    const FString ObjectPath=PackageName+TEXT(".")+Name;
    UStaticMesh* Mesh=FindObject<UStaticMesh>(nullptr,*ObjectPath);
#if WITH_EDITOR
    if(!Mesh&&FPackageName::DoesPackageExist(PackageName))Mesh=LoadObject<UStaticMesh>(nullptr,*ObjectPath);
#endif
    if (Mesh) { if (Mesh->GetNumTriangles(0)==0) return nullptr;Meshes.Add(Hash,Mesh);if(Save)MarkPackageDirty();return Mesh; }
    UPackage* Package=nullptr;
#if WITH_EDITOR
    if (Save) Package=CreatePackage(*PackageName);
#endif
    Mesh=NewObject<UStaticMesh>(Package?static_cast<UObject*>(Package):this,Package?FName(*Name):NAME_None,Package?RF_Public|RF_Standalone:RF_Transient);
    FMeshDescription Description; FStaticMeshAttributes Attributes(Description); Attributes.Register();
    auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
    auto Tangents=Attributes.GetVertexInstanceTangents(); auto Signs=Attributes.GetVertexInstanceBinormalSigns();
    auto UV=Attributes.GetVertexInstanceUVs(); UV.SetNumChannels(1);
    FPolygonGroupID Groups[3];
    for (int32 I=0;I<3;++I) { Groups[I]=Description.CreatePolygonGroup(); Attributes.GetPolygonGroupMaterialSlotNames()[Groups[I]]=FName(*FString::FromInt(I)); }
    auto Triangle=[&](const FVector3f& A,const FVector3f& B,const FVector3f& C,int32 Material)
    {
        const auto Normal=FVector3f::CrossProduct(B-A,C-A).GetSafeNormal();
        if (Normal.IsNearlyZero()) return;
        TArray<FVertexInstanceID> Corners;
        for (const auto P : {A,B,C})
        {
            const auto Vertex=Description.CreateVertex();Positions[Vertex]=P;
            const auto Instance=Description.CreateVertexInstance(Vertex);Normals[Instance]=Normal;
            const FVector3f Tangent=FMath::Abs(Normal.X)>.5f?FVector3f(0,1,0):FVector3f(1,0,0);
            const FVector3f Bitangent=FMath::Abs(Normal.Z)>.5f?FVector3f(0,1,0):FVector3f(0,0,1);
            Tangents[Instance]=Tangent;Signs[Instance]=FVector3f::DotProduct(FVector3f::CrossProduct(Normal,Tangent),Bitangent)>0?1:-1;
            const FVector2f Tex=FMath::Abs(Normal.Z)>.5f?FVector2f(P.X,P.Y):FMath::Abs(Normal.Y)>.5f?FVector2f(P.X,P.Z):FVector2f(P.Y,P.Z);
            UV.Set(Instance,0,Tex/100);Corners.Add(Instance);
        }
        Description.CreateTriangle(Groups[Material],Corners);
    };
    auto Solid=[&](const TArray<FVector2D>& Polygon,double Low,double High)
    {
        if (Polygon.Num()<3 || High-Low<1.e-5) return;
        for (int32 I=1;I+1<Polygon.Num();++I)
        {
            auto V=[&](int32 K,double Z){return FVector3f(Polygon[K].X,Polygon[K].Y,Z);};
            Triangle(V(0,High),V(I,High),V(I+1,High),2); Triangle(V(0,Low),V(I+1,Low),V(I,Low),2);
        }
        for (int32 I=0;I<Polygon.Num();++I)
        {
            const auto P=Polygon[I],Q=Polygon[(I+1)%Polygon.Num()];
            const FVector3f A(P.X,P.Y,Low),B(Q.X,Q.Y,Low),C(Q.X,Q.Y,High),D(P.X,P.Y,High);
            const int32 Material=FMath::IsNearlyEqual(P.Y,Q.Y,1.e-5) ? (Q.X>P.X?0:1) : 2;
            Triangle(A,B,C,Material);Triangle(A,C,D,Material);
        }
    };
    if(Value->HasField(TEXT("vertices")))
    {
        const auto& Vertices=Value->GetArrayField(TEXT("vertices"));const auto& Indices=Value->GetArrayField(TEXT("indices"));
        const TArray<TSharedPtr<FJsonValue>>* SourceNormals=nullptr;const TArray<TSharedPtr<FJsonValue>>* SourceUV=nullptr;
        Value->TryGetArrayField(TEXT("normals"),SourceNormals);Value->TryGetArrayField(TEXT("uv"),SourceUV);
        TArray<FVertexID> IDs;for(int32 I=0;I+2<Vertices.Num();I+=3){auto ID=Description.CreateVertex();Positions[ID]=FVector3f(Vertices[I]->AsNumber(),Vertices[I+1]->AsNumber(),Vertices[I+2]->AsNumber());IDs.Add(ID);}
        for(int32 I=0;I+2<Indices.Num();I+=3)
        {
            TArray<FVertexInstanceID> Corners;int32 K[3]={int32(Indices[I]->AsNumber()),int32(Indices[I+1]->AsNumber()),int32(Indices[I+2]->AsNumber())};
            if(!IDs.IsValidIndex(K[0])||!IDs.IsValidIndex(K[1])||!IDs.IsValidIndex(K[2]))continue;
            const auto FaceNormal=FVector3f::CrossProduct(Positions[IDs[K[1]]]-Positions[IDs[K[0]]],Positions[IDs[K[2]]]-Positions[IDs[K[0]]]).GetSafeNormal();if(FaceNormal.IsNearlyZero())continue;
            for(auto Index:K){auto Corner=Description.CreateVertexInstance(IDs[Index]);auto N=FaceNormal;if(SourceNormals&&SourceNormals->Num()>Index*3+2)N=FVector3f((*SourceNormals)[Index*3]->AsNumber(),(*SourceNormals)[Index*3+1]->AsNumber(),(*SourceNormals)[Index*3+2]->AsNumber()).GetSafeNormal();Normals[Corner]=N;Tangents[Corner]=FVector3f::CrossProduct(FMath::Abs(N.Z)>.9f?FVector3f(0,1,0):FVector3f(0,0,1),N).GetSafeNormal();Signs[Corner]=1;UV.Set(Corner,0,SourceUV&&SourceUV->Num()>Index*2+1?FVector2f((*SourceUV)[Index*2]->AsNumber(),(*SourceUV)[Index*2+1]->AsNumber()):FVector2f::ZeroVector);Corners.Add(Corner);}
            Description.CreateTriangle(Groups[0],Corners);
        }
    }
    else if(Value->HasField(TEXT("solids")))
    {
        for(const auto& Piece:HeyZedConstruction::Read(Value->GetArrayField(TEXT("solids"))))for(const auto& Face:Piece)
        {
            const auto N=HeyZedConstruction::Normal(Face);const int32 Material=FMath::Abs(N.Y)>.999?(N.Y<0?0:1):2;
            for(int32 I=1;I+1<Face.Num();++I)Triangle(FVector3f(Face[0]),FVector3f(Face[I]),FVector3f(Face[I+1]),Material);
        }
    }
    else {
    TArray<FVector2D> Polygon;
    for (const auto& P : Value->GetArrayField(TEXT("polygon"))) { const auto& XY=P->AsArray();Polygon.Add({XY[0]->AsNumber(),XY[1]->AsNumber()}); }
    const double Low=Value->GetNumberField(TEXT("bottom")),High=Value->GetNumberField(TEXT("top"));
    struct FPiece { TArray<FVector2D> Polygon;double Low,High; };
    TArray<FPiece> Pieces;Pieces.Add({Polygon,Low,High});
    const TArray<TSharedPtr<FJsonValue>>* Holes=nullptr;
    if (Value->TryGetArrayField(TEXT("holes"),Holes)) for (const auto& Hole : *Holes)
    {
        const auto& H=Hole->AsArray();const double L=H[0]->AsNumber(),V0=H[1]->AsNumber(),B=H[2]->AsNumber(),R=H[3]->AsNumber(),V1=H[4]->AsNumber(),T=H[5]->AsNumber();
        TArray<FPiece> Next;
        auto Add=[&](TArray<FVector2D> Ring,double Z0,double Z1)
        {
            double Area=0;for(int32 I=0;I<Ring.Num();++I) {const auto P=Ring[I],Q=Ring[(I+1)%Ring.Num()];Area+=P.X*Q.Y-P.Y*Q.X;}
            if (Area>1.e-7 && Z1-Z0>1.e-7) Next.Add({MoveTemp(Ring),Z0,Z1});
        };
        for (const auto& Piece : Pieces)
        {
            if (T<=Piece.Low || B>=Piece.High) { Next.Add(Piece);continue; }
            Add(HeyZedConstruction::Clip2D(Piece.Polygon,-1,0,-L),Piece.Low,Piece.High);
            Add(HeyZedConstruction::Clip2D(Piece.Polygon,1,0,R),Piece.Low,Piece.High);
            const auto Middle=HeyZedConstruction::Clip2D(HeyZedConstruction::Clip2D(Piece.Polygon,1,0,L),-1,0,-R);
            Add(HeyZedConstruction::Clip2D(Middle,0,-1,-V0),Piece.Low,Piece.High);Add(HeyZedConstruction::Clip2D(Middle,0,1,V1),Piece.Low,Piece.High);
            const auto Interior=HeyZedConstruction::Clip2D(HeyZedConstruction::Clip2D(Middle,0,1,V0),0,-1,-V1);
            Add(Interior,Piece.Low,FMath::Min(B,Piece.High));Add(Interior,FMath::Max(T,Piece.Low),Piece.High);
        }
        Pieces=MoveTemp(Next);
    }
    for (const auto& Piece : Pieces) Solid(Piece.Polygon,Piece.Low,Piece.High);
    }
    if (Description.Triangles().Num()==0) {EmptyRecipes.Add(Hash);Mesh->ClearFlags(RF_Public|RF_Standalone);return nullptr;}
    AlignConstructionFacing(Description);
    double SurfaceAreas[3]={},UVAreas[3]={};
    for(const auto ID:Description.Triangles().GetElementIDs())
    {
        const int32 Group=Description.GetTrianglePolygonGroup(ID).GetValue();
        const auto Corners=Description.GetTriangleVertexInstances(ID);
        const auto A=Positions[Description.GetVertexInstanceVertex(Corners[0])];
        const auto B=Positions[Description.GetVertexInstanceVertex(Corners[1])];
        const auto C=Positions[Description.GetVertexInstanceVertex(Corners[2])];
        const auto U=UV.Get(Corners[1],0)-UV.Get(Corners[0],0),V=UV.Get(Corners[2],0)-UV.Get(Corners[0],0);
        SurfaceAreas[Group]+=FVector3f::CrossProduct(B-A,C-A).Size();
        UVAreas[Group]+=FMath::Abs(U.X*V.Y-U.Y*V.X);
    }
    for (int32 I=0;I<3;++I)
    {
        FStaticMaterial Slot(nullptr,FName(*FString::FromInt(I)));
        Slot.UVChannelData=FMeshUVChannelInfo(UVAreas[I]>UE_SMALL_NUMBER?FMath::Sqrt(SurfaceAreas[I]/UVAreas[I]):0.f);
        Mesh->GetStaticMaterials().Add(Slot);
    }
    UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bUseHashAsGuid=true;Params.bFastBuild=true;
#if WITH_EDITOR
    if (Save)
    {
        Params.bFastBuild=false;Mesh->GetNaniteSettings().bEnabled=!Value->HasField(TEXT("glass"));
        Mesh->SetNumSourceModels(1);auto& Build=Mesh->GetSourceModel(0).BuildSettings;
        Build.bRecomputeNormals=false;Build.bRecomputeTangents=Value->HasField(TEXT("vertices"));Build.bGenerateLightmapUVs=false;
    }
#else
    Params.bCommitMeshDescription=false;
#endif
    if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) return nullptr;
#if WITH_EDITOR
    if (Save)
    {
        FinishHeyZedMesh(Mesh);FAssetRegistryModule::AssetCreated(Mesh);Package->MarkPackageDirty();
        const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if (!UPackage::SavePackage(Package,Mesh,*Filename,Args)) return nullptr;
    }
#endif
    Meshes.Add(Hash,Mesh);if(Save)MarkPackageDirty();return Mesh;
}

AHeyZedArchitecture::AHeyZedArchitecture()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Architecture"));RootComponent->SetMobility(EComponentMobility::Static);PrimaryActorTick.bCanEverTick=false;
}
#if WITH_EDITOR
void UHeyZedArchitectureHLODSource::ComputeHLODHash(FHLODHashBuilder& Hash) const
{
    Hash.HashField(EnvelopeKey,TEXT("BuildingEnvelope"));
}

TArray<UActorComponent*> AHeyZedArchitecture::GetHLODRelevantComponents() const
{
    const auto Data=Read(Description);
    if(!Data->HasField(TEXT("bounds"))||!Data->HasField(TEXT("metric")))return {};
    const auto& Bounds=Data->GetArrayField(TEXT("bounds"));
    const auto Metric=Data->GetObjectField(TEXT("metric"));
    double Base=0,Levels=1;Data->TryGetNumberField(TEXT("minLevel"),Base);Data->TryGetNumberField(TEXT("levels"),Levels);
    const double FloorHeight=Metric->GetNumberField(TEXT("floorHeight"))*100;
    const double HalfWall=Metric->GetNumberField(TEXT("wallThickness"))*50,SlabDepth=Metric->GetNumberField(TEXT("slabDepth"))*100;
    const FBox Volume(FVector(Bounds[0]->AsNumber()*100-HalfWall,Bounds[1]->AsNumber()*100-HalfWall,FMath::Max(-SlabDepth,Base*FloorHeight-SlabDepth)),
                      FVector(Bounds[2]->AsNumber()*100+HalfWall,Bounds[3]->AsNumber()*100+HalfWall,(Base+Levels)*FloorHeight-SlabDepth));
    if(Volume.GetSize().GetMin()<=0)return {};
    auto* Mutable=const_cast<AHeyZedArchitecture*>(this);
    if(!EnvelopeSource)
    {
        Mutable->EnvelopeSource=NewObject<UHeyZedArchitectureHLODSource>(Mutable,NAME_None,RF_Transient);
        EnvelopeSource->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        EnvelopeSource->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        EnvelopeSource->SetCanEverAffectNavigation(false);
    }
    auto Key=MakeShared<FJsonObject>();
    Key->SetArrayField(TEXT("bounds"),Bounds);Key->SetNumberField(TEXT("base"),Base);Key->SetNumberField(TEXT("levels"),Levels);
    Key->SetNumberField(TEXT("floor"),Metric->GetNumberField(TEXT("floorHeight")));
    Key->SetNumberField(TEXT("slab"),Metric->GetNumberField(TEXT("slabDepth")));
    Key->SetNumberField(TEXT("wall"),Metric->GetNumberField(TEXT("wallThickness")));
    FString Finish;Data->TryGetStringField(TEXT("exteriorFinish"),Finish);Key->SetStringField(TEXT("finish"),Finish);
    EnvelopeSource->EnvelopeKey=Write(Key);
    EnvelopeSource->SetWorldTransform(FTransform(FQuat::Identity,Volume.GetCenter(),Volume.GetSize()/100));
    EnvelopeSource->UpdateBounds();
    return {EnvelopeSource};
}
#endif
FString AHeyZedArchitecture::Key(int32 X,int32 Y,int32 Z) { return FString::Printf(TEXT("%d,%d,%d"),X,Y,Z); }
namespace {
TUniquePtr<FHeyZedArchitectureMetadata> PrepareArchitectureMetadata(const FString& Description)
{
    auto Data=MakeUnique<FHeyZedArchitectureMetadata>();
    const auto Value=[&](){TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_ParseDescription);return Read(Description);}();
    if(!Value->HasField(TEXT("cells")))return Data;
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_IndexDescription);
    auto Key=[](int32 X,int32 Y,int32 Z){return FString::Printf(TEXT("%d,%d,%d"),X,Y,Z);};
    const auto& Bounds=Value->GetArrayField(TEXT("bounds"));Data->Minimum={Bounds[0]->AsNumber(),Bounds[1]->AsNumber()};Data->Maximum={Bounds[2]->AsNumber(),Bounds[3]->AsNumber()};
    const auto Metric=Value->GetObjectField(TEXT("metric"));Data->Floor=Metric->GetNumberField(TEXT("floorHeight"))*100;Data->Thickness=Metric->GetNumberField(TEXT("wallThickness"))*100;Data->Slab=Metric->GetNumberField(TEXT("slabDepth"))*100;
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(Value->TryGetArrayField(TEXT("footprint"),Values))for(const auto& V:*Values){const auto& P=V->AsArray();Data->Footprint.Add(Key(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()));}
    if(Value->TryGetArrayField(TEXT("interior"),Values))for(const auto& V:*Values){const auto& P=V->AsArray();Data->Interior.Add(Key(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()));}
    for(const auto& Record:Value->GetArrayField(TEXT("cells")))
    {
        const auto Cell=Record->AsObject();const auto& P=Cell->GetArrayField(TEXT("address"));const FString Address=Key(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber());Data->Cells.Add(Address,Cell);
        FString Source;if(Cell->TryGetStringField(TEXT("floor"),Source)&&!Source.IsEmpty())Data->OwnedFloors.Add(Address);
        for(const auto& W:Cell->GetArrayField(TEXT("walls"))){const auto Wall=W->AsObject();bool Context=false;Wall->TryGetBoolField(TEXT("contextOnly"),Context);(Context?Data->ContextEdges:Data->OwnedEdges).Add(Address+TEXT(":")+Wall->GetStringField(TEXT("axis")));}
    }
    Data->Valid=true;return Data;
}
}
void AHeyZedArchitecture::InstallMetadata(FHeyZedArchitectureMetadata&& Data)
{
    Minimum=Data.Minimum;Maximum=Data.Maximum;Floor=Data.Floor;Thickness=Data.Thickness;Slab=Data.Slab;
    Cells=MoveTemp(Data.Cells);OwnedEdges=MoveTemp(Data.OwnedEdges);ContextEdges=MoveTemp(Data.ContextEdges);OwnedFloors=MoveTemp(Data.OwnedFloors);Footprint=MoveTemp(Data.Footprint);Interior=MoveTemp(Data.Interior);
    for(UInstancedStaticMeshComponent* Component:Components)if(Component&&Component->GetStaticMesh())Batches.Add(Component->GetName(),Component);
    bMetadataReady=true;
}
void AHeyZedArchitecture::Load()
{
    ++MetadataGeneration;bMetadataScheduled=false;PreparedMetadata.Reset();
    auto Data=PrepareArchitectureMetadata(Description);if(Data->Valid)InstallMetadata(MoveTemp(*Data));
}
void AHeyZedArchitecture::PostLoad()
{
    Super::PostLoad();
    if(GetWorld()&&GetWorld()->IsGameWorld())StartMetadataPreparation();
}
void AHeyZedArchitecture::StartMetadataPreparation()
{
    if(bEndingPlay||bMetadataReady||bMetadataScheduled||Description.IsEmpty())return;
    bMetadataScheduled=true;const uint64 Generation=++MetadataGeneration;
    const TWeakObjectPtr<AHeyZedArchitecture> ActorRef(this);
    ArchitectureMetadataPipe.Launch(TEXT("HeyZedArchitecture_PrepareMetadata"),[ActorRef,Generation,Text=Description]()
    {
        auto Data=PrepareArchitectureMetadata(Text);
        AsyncTask(ENamedThreads::GameThread,[ActorRef,Generation,Data=MoveTemp(Data)]() mutable
        {
            auto* Actor=ActorRef.Get();if(!Actor||Actor->bEndingPlay||Actor->MetadataGeneration!=Generation)return;
            if(!Data->Valid){UE_LOG(LogTemp,Error,TEXT("heyzed architecture metadata invalid: %s; authored parts retained"),*Actor->GetName());return;}
            Actor->PreparedMetadata=MakeShareable(Data.Release());
            if(Actor->bActivationStarted)Actor->FinishMetadataPreparation();
        });
    },UE::Tasks::ETaskPriority::BackgroundLow);
}
void AHeyZedArchitecture::FinishMetadataPreparation()
{
    if(bEndingPlay||bMetadataReady||!PreparedMetadata)return;
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_CommitMetadata);
    InstallMetadata(MoveTemp(*PreparedMetadata));PreparedMetadata.Reset();ActivateMetadata();
}
void AHeyZedArchitecture::ActivateMetadata()
{
    if(bEndingPlay||!bActivationStarted||!bMetadataReady||bMetadataActivated)return;
    bMetadataActivated=true;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_ClaimFloors);
        for(const auto& Pair:Cells)
        {
            const auto& P=Pair.Value->GetArrayField(TEXT("address"));FString Source;
            if(P[2]->AsNumber()==0&&Pair.Value->TryGetStringField(TEXT("floor"),Source)&&!Source.IsEmpty())
                if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->ClaimArchitectureFloor({int32(P[0]->AsNumber()),int32(P[1]->AsNumber())},true,this);
        }
    }
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()){HeyZed->RestoreArchitecture(this);HeyZed->ArchitectureChanged(this);}
}
void AHeyZedArchitecture::BeginPlay()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Activate);
    Super::BeginPlay();bEndingPlay=false;bMetadataActivated=false;
    PendingBounds=GetComponentsBoundingBox(false);PreparedDoors.Reset();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_IndexInstances);
        if(Instances.IsEmpty())for(const auto& Instance:SavedInstances)Instances.FindOrAdd(Instance.OwnerAddress).Add(Instance);
        for(const auto& Instance:SavedInstances)if(!Instance.Door.IsEmpty())PreparedDoors.Add(Instance.Door);
    }
    bActivationStarted=true;
    if(bMetadataReady)ActivateMetadata();else if(PreparedMetadata)FinishMetadataPreparation();else
    {
        if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->ArchitectureChanged(this);
        StartMetadataPreparation();
    }
}
void AHeyZedArchitecture::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay=true;bActivationStarted=false;bMetadataActivated=false;bMetadataScheduled=false;++MetadataGeneration;PreparedMetadata.Reset();
    for(auto& Pair:PendingLoads)for(auto& Handle:Pair.Value)if(Handle)Handle->CancelHandle();
    PendingLoads.Reset();PendingPlans.Reset();DirtyCells.Reset();PlanningWork.Reset();PlanVersions.Reset();
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->ArchitectureChanged(this);
    Super::EndPlay(Reason);
}
bool AHeyZedArchitecture::Covers(const FVector& Position) const
{
    if(!bMetadataReady)return PendingBounds.IsValid&&PendingBounds.ExpandBy(100).IsInsideXY(Position);
    return Position.X>=Minimum.X*100-100 && Position.X<=(Maximum.X+1)*100 && Position.Y>=Minimum.Y*100-100 && Position.Y<=(Maximum.Y+1)*100;
}
bool AHeyZedArchitecture::Prepare(const FString& Input)
{
#if WITH_EDITOR
    if(!ConfigureWorld(GetWorld()))return false;
    SetRuntimeGrid(TEXT("Architecture"));SetIsSpatiallyLoaded(true);
    // HLOD is explicit per representation; never simplify the prepared ground map implicitly.
    if (auto* Partition=GetWorld()->GetWorldPartition())if(Partition->GetDefaultHLODLayer())
    { Partition->SetDefaultHLODLayer(nullptr);GetWorld()->MarkPackageDirty(); }
#endif
    Load();const auto Previous=Cells;const double OldFloor=Floor,OldThickness=Thickness,OldSlab=Slab;
    if(Instances.IsEmpty())for(const auto& Instance:SavedInstances)Instances.FindOrAdd(Instance.OwnerAddress).Add(Instance);
    Description=Input;Load();if (!Resources || Cells.IsEmpty()) return false;
    TSet<FString> Dirty;
    auto Touch=[&](const TSharedPtr<FJsonObject>& Cell)
    {
        const auto& P=Cell->GetArrayField(TEXT("address"));const int32 X=P[0]->AsNumber(),Y=P[1]->AsNumber(),Z=P[2]->AsNumber();
        auto Around=[&](int32 XX,int32 YY){for(int32 DZ=-1;DZ<=1;++DZ)for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)Dirty.Add(Key(XX+DX,YY+DY,Z+DZ));};Around(X,Y);
        for(auto& W:Cell->GetArrayField(TEXT("walls")))
        {
            auto Wall=W->AsObject();double Span=1;Wall->TryGetNumberField(TEXT("assemblyCells"),Span);
            const bool North=Wall->GetStringField(TEXT("axis"))==TEXT("N");for(int32 I=1;I<Span;++I)Around(X+(North?I:0),Y+(North?0:I));
            const TArray<TSharedPtr<FJsonValue>>* Anchor=nullptr;if(Wall->TryGetArrayField(TEXT("assemblyAnchor"),Anchor)&&Anchor->Num()>=2)Around((*Anchor)[0]->AsNumber(),(*Anchor)[1]->AsNumber());
        }
    };
    const bool MetricChanged=OldFloor!=Floor||OldThickness!=Thickness||OldSlab!=Slab;
    for(const auto& Pair:Cells){const auto* Old=Previous.Find(Pair.Key);if(MetricChanged||!Old||!Equal(Old->ToSharedRef(),Pair.Value.ToSharedRef())){if(!MetricChanged&&Old&&OnlyAttachmentsChanged(Old->ToSharedRef(),Pair.Value.ToSharedRef()))Dirty.Add(Pair.Key);else {Touch(Pair.Value);if(Old)Touch(*Old);}}}
    for(const auto& Pair:Previous)if(!Cells.Contains(Pair.Key))Touch(Pair.Value);
    for(const auto& Address:Dirty)Rebuild(Address,true);
    SavedInstances.Reset();for(const auto& Pair:Instances)SavedInstances.Append(Pair.Value);
    MarkPackageDirty();return true;
}
bool AHeyZedArchitecture::ConfigureWorld(UWorld* World,float LoadingRange)
{
#if WITH_EDITOR
    if(!World||!World->GetWorldPartition())return false;
    auto* Partition=World->GetWorldPartition();
    for(TObjectIterator<UWorldPartitionRuntimeSpatialHash> It;It;++It)if(It->GetOuter()==Partition)
    {
        auto* Property=FindFProperty<FArrayProperty>(It->GetClass(),TEXT("Grids"));if(!Property)return false;
        FScriptArrayHelper Grids(Property,Property->ContainerPtrToValuePtr<void>(*It));int32 Index=INDEX_NONE;
        for(int32 I=0;I<Grids.Num();++I)if(reinterpret_cast<FSpatialHashRuntimeGrid*>(Grids.GetRawPtr(I))->GridName==TEXT("Architecture")){Index=I;break;}
        if(Index==INDEX_NONE)Index=Grids.AddValue();auto* Grid=reinterpret_cast<FSpatialHashRuntimeGrid*>(Grids.GetRawPtr(Index));
        if(Grid->GridName==TEXT("Architecture")&&Grid->CellSize==12800&&Grid->LoadingRange==LoadingRange)return true;
        Grid->GridName=TEXT("Architecture");Grid->CellSize=12800;Grid->LoadingRange=LoadingRange;Grid->Origin=FVector2D::ZeroVector;Grid->bBlockOnSlowStreaming=false;
        World->MarkPackageDirty();return true;
    }
#endif
    return false;
}
void AHeyZedArchitecture::Place(const FString& OwnerAddress,const TSharedRef<FJsonObject>& Recipe,const FVector& Position,float Yaw,const TArray<FName>& Materials,bool Save,const FString& DoorId,const FVector& Scale,const FString* PreparedText)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Place);
    FString GeneratedText;const FString& Text=PreparedText?*PreparedText:(GeneratedText=Write(Recipe));
    const FString Signature=FMD5::HashAnsiString(*(Text+Position.ToString()+Scale.ToString()+FString::SanitizeFloat(Yaw)+FString::JoinBy(Materials,TEXT("|"),[](FName Name){return Name.ToString();})+DoorId));
    KeptParts.Add(Signature);
    float FloorDirt=0,WallDirt=0;
    if(auto* Cell=Cells.Find(OwnerAddress)){const TArray<TSharedPtr<FJsonValue>>* Grime=nullptr;if((*Cell)->TryGetArrayField(TEXT("grime"),Grime))for(auto& Source:*Grime){const FString Name=Source->AsString();if(Name.Contains(TEXT("floor")))FloorDirt=1;else WallDirt=1;}}
    auto Dirt=[&](UInstancedStaticMeshComponent* Component,int32 Index)
    {
        for(int32 Slot=2;Slot<=3;++Slot)
        {
            const float Value=Slot==2?FloorDirt:WallDirt;
            const int32 Offset=Index*Component->NumCustomDataFloats+Slot;
            if(Component->PerInstanceSMCustomData.IsValidIndex(Offset)&&Component->PerInstanceSMCustomData[Offset]==Value)continue;
            Component->SetCustomDataValue(Index,Slot,Value,false);
        }
    };
    if (const auto* Existing=Instances.Find(OwnerAddress)) for (const auto& Part : *Existing) if (Part.Signature==Signature) {Dirt(Part.Component,Part.Index);return;}
    auto* Mesh=Resources->Resolve(Text,Save,Recipe);if (!Mesh) return;
    FString Batch=FMD5::HashAnsiString(*(Text+FString::JoinBy(Materials,TEXT("|"),[](FName Name){return Name.ToString();})));
    Batch=TEXT("Construction_")+Batch+(DoorId.IsEmpty()?TEXT("_static"):TEXT("_moving"))+(Scale.GetMin()<0?TEXT("_mirrored"):TEXT(""));
    auto*& Component=Batches.FindOrAdd(Batch);
    if (!Component)
    {
        Component=NewObject<UInstancedStaticMeshComponent>(this,*Batch,RF_Transactional);Component->SetupAttachment(RootComponent);
        Component->SetMobility(DoorId.IsEmpty()?EComponentMobility::Static:EComponentMobility::Movable);Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCanEverAffectNavigation(false);
        Component->SetStaticMesh(Mesh);Component->SetCastShadow(true);Component->NumCustomDataFloats=4;
        for (int32 I=0;I<3;++I)
        {
            UMaterialInterface* Material=Resources->Materials.FindRef(Materials[FMath::Min(I,Materials.Num()-1)]);
            if(!Material)Material=Resources->Materials.FindRef(TEXT("Plaster"));
            Component->SetMaterial(I,Material);
        }
        AddInstanceComponent(Component);Component->RegisterComponent();Components.Add(Component);
    }
    const FTransform Transform(FRotator(0,Yaw,0),Position-GetActorLocation(),Scale);
    FTransform Initial=Transform;
    if(!DoorId.IsEmpty())if(const auto* Existing=Instances.Find(OwnerAddress))for(const auto& Part:*Existing)if(Part.Door==DoorId&&Part.Component)
    {
        FTransform Current;if(Part.Component->GetInstanceTransform(Part.Index,Current,false)){Initial.SetRotation(Current.GetRotation()*Part.Closed.GetRotation().Inverse()*Transform.GetRotation());break;}
    }
    FHeyZedConstructionInstance Instance;Instance.Component=Component;Instance.Index=Component->AddInstance(Initial);Instance.Closed=Transform;Instance.Door=DoorId;Instance.OwnerAddress=OwnerAddress;Instance.Signature=Signature;
    double Bottom=0,Top=0;Recipe->TryGetNumberField(TEXT("bottom"),Bottom);Recipe->TryGetNumberField(TEXT("top"),Top);
    const bool FloorUV=Bottom<0 && Top==0;
    bool LocalUV=false;Recipe->TryGetBoolField(TEXT("localUV"),LocalUV);
    Component->SetCustomDataValue(Instance.Index,0,DoorId.IsEmpty()&&!LocalUV?(FloorUV?Position.X:(Yaw==0?Position.X:Position.Y))/100:0,false);
    Component->SetCustomDataValue(Instance.Index,1,DoorId.IsEmpty()&&!LocalUV?(FloorUV?Position.Y:Position.Z)/100:0,false);
    Dirt(Component,Instance.Index);
    Instances.FindOrAdd(OwnerAddress).Add(Instance);if (Save) SavedInstances.Add(Instance);
}
void AHeyZedArchitecture::DiscardUnused(const FString& OwnerAddress)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_DiscardUnused);
    auto* Parts=Instances.Find(OwnerAddress);if (!Parts) return;
    for (int32 I=Parts->Num()-1;I>=0;--I) if (!KeptParts.Contains((*Parts)[I].Signature))
    {
        const auto Removed=(*Parts)[I];Parts->RemoveAt(I);
        if (!Removed.Component) continue;
        const bool Swap=Removed.Component->SupportsRemoveSwap();const int32 Last=Removed.Component->GetInstanceCount()-1;
        Removed.Component->RemoveInstance(Removed.Index);
        for (auto& Pair : Instances) for (auto& Other : Pair.Value) if (Other.Component==Removed.Component)
        {
            if (Swap && Other.Index==Last) Other.Index=Removed.Index;
            else if (!Swap && Other.Index>Removed.Index) --Other.Index;
        }
    }
}

TSharedRef<const FHeyZedConstructionCatalog,ESPMode::ThreadSafe> UHeyZedArchitectureResources::GetPlanningCatalog()
{
    check(IsInGameThread());
    if(!PlanningCatalog)
    {
        auto Data=MakeShared<FHeyZedConstructionCatalog,ESPMode::ThreadSafe>();
        Data->Definitions=Definitions;Data->SourceBindings=SourceBindings;Data->SurfaceBindings=SurfaceBindings;
        for(const auto& Pair:Materials)Data->Materials.Add(Pair.Key);
        PlanningCatalog=Data;
    }
    return PlanningCatalog.ToSharedRef();
}
FHeyZedConstructionSnapshot AHeyZedArchitecture::ConstructionSnapshot(const FString& Address)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedConstruction_Snapshot);
    FHeyZedConstructionSnapshot Snapshot;
    Snapshot.Catalog=Resources->GetPlanningCatalog();Snapshot.Floor=Floor;Snapshot.Thickness=Thickness;Snapshot.Slab=Slab;
    TArray<FString> Parts;Address.ParseIntoArray(Parts,TEXT(","));check(Parts.Num()==3);
    const int32 X=FCString::Atoi(*Parts[0]),Y=FCString::Atoi(*Parts[1]),Z=FCString::Atoi(*Parts[2]);
    TSet<FString> Keys;
    for(int32 DZ=-1;DZ<=1;++DZ)for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)Keys.Add(Key(X+DX,Y+DY,Z+DZ));
    for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)if(const auto* Cell=Cells.Find(Key(X+DX,Y+DY,Z)))
        for(const auto& Wall:(*Cell)->GetArrayField(TEXT("walls")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Anchor=nullptr;
            if(Wall->AsObject()->TryGetArrayField(TEXT("assemblyAnchor"),Anchor)&&Anchor->Num()>=2)Keys.Add(Key((*Anchor)[0]->AsNumber(),(*Anchor)[1]->AsNumber(),Z));
        }
    for(const auto& K:Keys)
    {
        if(const auto* Cell=Cells.Find(K))Snapshot.Cells.Add(K,Write((*Cell).ToSharedRef()));
        if(Interior.Contains(K))Snapshot.Interior.Add(K);
        if(OwnedFloors.Contains(K))Snapshot.OwnedFloors.Add(K);
    }
    return Snapshot;
}
void AHeyZedArchitecture::GatherParts(const FString& Address,bool Save)
{
    KeptParts.Reset();
    if(Resources)
    {
        const auto Snapshot=ConstructionSnapshot(Address);FHeyZedConstructionPlanner Planner(Snapshot);
        for(const auto& Part:Planner.Plan(Address))Place(Address,Part.Recipe.ToSharedRef(),Part.Position,Part.Yaw,Part.Materials,Save,Part.Door,Part.Scale,&Part.RecipeText);
    }
    DiscardUnused(Address);
}

void AHeyZedArchitecture::QueueRebuild(const FString& Address,bool InvalidatePlan)
{
    if(bEndingPlay)return;
    if(InvalidatePlan){PendingPlans.Remove(Address);++PlanVersions.FindOrAdd(Address);}
    DirtyCells.Add(Address);
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->QueueArchitectureUpdate(this);
}
void AHeyZedArchitecture::Rebuild(const FString& Address,bool Save)
{
    if(Save){GatherParts(Address,true);return;}
    QueueRebuild(Address);
}
bool AHeyZedArchitecture::UpdateConstruction(bool& Changed,FIntVector& ChangedCell)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Update);
    Changed=false;
    if(bEndingPlay||PlanningWork||DirtyCells.IsEmpty()||!Resources)return false;
    auto It=DirtyCells.CreateIterator();const FString Address=*It;It.RemoveCurrent();
    if(const auto* Loads=PendingLoads.Find(Address))for(const auto& Handle:*Loads)if(Handle&&!Handle->HasLoadCompleted())return !DirtyCells.IsEmpty();
    if(!PendingPlans.Contains(Address))
    {
        auto Work=MakeShared<FHeyZedConstructionWork,ESPMode::ThreadSafe>();
        Work->Address=Address;Work->Version=PlanVersions.FindRef(Address);
        Work->Snapshot=ConstructionSnapshot(Address);PlanningWork=Work;
        TWeakObjectPtr<AHeyZedArchitecture> Weak(this);
        ConstructionPlans().Push(TEXT("heyzed construction plan"),[Work,Weak](uint32)
        {
            {FHeyZedConstructionPlanner Planner(Work->Snapshot);Work->Parts=Planner.Plan(Work->Address);}
            Work->Snapshot={};
            AsyncTask(ENamedThreads::GameThread,[Work,Weak]()
            {
                auto* Building=Weak.Get();
                if(!Building||Building->bEndingPlay||Building->PlanningWork!=Work)return;
                Building->PlanningWork.Reset();
                if(Building->PlanVersions.FindRef(Work->Address)==Work->Version)
                {
                    Building->PendingPlans.Add(Work->Address,MoveTemp(Work->Parts));
                    Building->DirtyCells.Add(Work->Address);
                }
                else Building->DirtyCells.Add(Work->Address);
                if(auto* HeyZed=Building->GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->QueueArchitectureUpdate(Building);
            });
        });
        return false;
    }
    const auto& Parts=PendingPlans.FindChecked(Address);
    TSet<FSoftObjectPath> Missing;
    for(const auto& Part:Parts)
    {
        const auto Path=Resources->PendingAsset(Part.RecipeText,Part.Recipe);
        if(Path.IsValid())Missing.Add(Path);
    }
    if(!Missing.IsEmpty())
    {
        for(const auto& Path:Missing)if(FailedAssets.Contains(Path)){PendingPlans.Remove(Address);return !DirtyCells.IsEmpty();}
        const auto Paths=Missing.Array();
        auto Handle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
            FStreamableDelegate::CreateWeakLambda(this,[this,Address,Paths]()
            {
                if(bEndingPlay)return;
                bool Failed=false;
                for(const auto& Path:Paths)if(!Path.ResolveObject()){Failed=true;FailedAssets.Add(Path);UE_LOG(LogTemp,Error,TEXT("heyzed construction asset failed: %s cell=%s; previous parts retained"),*Path.ToString(),*Address);}
                if(Failed){PendingLoads.Remove(Address);PendingPlans.Remove(Address);}else QueueRebuild(Address,false);
            }),FStreamableManager::DefaultAsyncLoadPriority,false,true,TEXT("heyzed addressed construction"));
        if(Handle){PendingLoads.FindOrAdd(Address).Add(Handle);Handle->StartStalledHandle();}
        else {for(const auto& Path:Paths)FailedAssets.Add(Path);PendingPlans.Remove(Address);UE_LOG(LogTemp,Error,TEXT("heyzed construction async request failed: %s; previous parts retained"),*Address);}
        return !DirtyCells.IsEmpty();
    }
    TArray<TStrongObjectPtr<UStaticMesh>> ReadyMeshes;
    for(const auto& Part:Parts)
    {
        const FString& Recipe=Part.RecipeText;
        auto* Mesh=Resources->Resolve(Recipe,false,Part.Recipe);
        if(!Mesh&&Resources->IsEmptyRecipe(Recipe))continue;
        if(!Mesh){UE_LOG(LogTemp,Error,TEXT("heyzed construction mesh unavailable: %s recipe=%s; previous parts retained"),*Address,*Recipe.Left(512));PendingLoads.Remove(Address);PendingPlans.Remove(Address);return !DirtyCells.IsEmpty();}
        ReadyMeshes.Emplace(Mesh);
    }
    KeptParts.Reset();
    for(const auto& Part:Parts)if(!Resources->IsEmptyRecipe(Part.RecipeText))Place(Address,Part.Recipe.ToSharedRef(),Part.Position,Part.Yaw,Part.Materials,false,Part.Door,Part.Scale,&Part.RecipeText);
    DiscardUnused(Address);
    if(const auto* Cell=Cells.Find(Address))
    {
        const auto& P=(*Cell)->GetArrayField(TEXT("address"));FString Source;
        if(P[2]->AsNumber()==0)if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())
            HeyZed->ClaimArchitectureFloor({int32(P[0]->AsNumber()),int32(P[1]->AsNumber())},(*Cell)->TryGetStringField(TEXT("floor"),Source)&&!Source.IsEmpty(),this);
    }
    TArray<FString> Coordinates;Address.ParseIntoArray(Coordinates,TEXT(","));
    check(Coordinates.Num()==3);ChangedCell=FIntVector(FCString::Atoi(*Coordinates[0]),FCString::Atoi(*Coordinates[1]),FCString::Atoi(*Coordinates[2]));
    PendingLoads.Remove(Address);PendingPlans.Remove(Address);Changed=true;
    return !DirtyCells.IsEmpty();
}
void AHeyZedArchitecture::Apply(const TSharedRef<FJsonObject>& Cell)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedArchitecture_Apply);
    if(!bMetadataReady)return;
    const auto& P=Cell->GetArrayField(TEXT("address"));const int32 X=P[0]->AsNumber(),Y=P[1]->AsNumber(),Z=P[2]->AsNumber();
    if (X<Minimum.X-1 || X>Maximum.X+1 || Y<Minimum.Y-1 || Y>Maximum.Y+1) return;
    const FString Address=Key(X,Y,Z);const auto* Old=Cells.Find(Address);
    if (Old && Equal(Old->ToSharedRef(),Cell)) return;
    auto Local=Read(Write(Cell));FString FloorSource;
    if(Local->TryGetStringField(TEXT("floor"),FloorSource)&&!FloorSource.IsEmpty()&&!OwnedFloors.Contains(Address)&&!Footprint.Contains(Address))Local->SetStringField(TEXT("floor"),TEXT(""));
    for(auto& W:Local->GetArrayField(TEXT("walls")))
    {
        const auto Wall=W->AsObject();const FString Edge=Address+TEXT(":")+Wall->GetStringField(TEXT("axis"));
        if(ContextEdges.Contains(Edge))Wall->SetBoolField(TEXT("contextOnly"),true);
        else if(OwnedEdges.Contains(Edge))Wall->SetBoolField(TEXT("contextOnly"),false);
        else {const bool North=Wall->GetStringField(TEXT("axis"))==TEXT("N");const bool Within=Footprint.Contains(Address)||Footprint.Contains(Key(X-(North?0:1),Y-(North?1:0),Z));Wall->SetBoolField(TEXT("contextOnly"),!Within);}
    }
    if(Old&&!Local->HasField(TEXT("renderRecipe"))&&(*Old)->HasField(TEXT("renderRecipe")))Local->SetField(TEXT("renderRecipe"),(*Old)->Values[TEXT("renderRecipe")]);
    if(Old&&Equal(Old->ToSharedRef(),Local))return;
    static int32 DifferenceExamples=0;
    if(Old&&DifferenceExamples<4&&FParse::Param(FCommandLine::Get(),TEXT("HeyZedRenderDiagnostics")))
    {
        ++DifferenceExamples;
        UE_LOG(LogTemp,Display,TEXT("heyzed construction difference %s before=%s after=%s"),*Address,*Write(Old->ToSharedRef()).Left(8192),*Write(Local).Left(8192));
    }
    const bool AttachmentsOnly=Old&&OnlyAttachmentsChanged(Old->ToSharedRef(),Local);
    Cells.Add(Address,Local);
    if(Local->TryGetStringField(TEXT("floor"),FloorSource)&&!FloorSource.IsEmpty())OwnedFloors.Add(Address);else OwnedFloors.Remove(Address);
    if(AttachmentsOnly){Rebuild(Address,false);return;}
    for (int32 DY=-1;DY<=1;++DY) for (int32 DX=-1;DX<=1;++DX) Rebuild(Key(X+DX,Y+DY,Z),false);
    for (int32 DY=-1;DY<=1;++DY) for (int32 DX=-1;DX<=1;++DX) Rebuild(Key(X+DX,Y+DY,Z-1),false);
    for (int32 DY=-1;DY<=1;++DY) for (int32 DX=-1;DX<=1;++DX) Rebuild(Key(X+DX,Y+DY,Z+1),false);
}
void AHeyZedArchitecture::ApplyToLoaded(UWorld* World,const TSharedRef<FJsonObject>& Cell)
{
    for(TActorIterator<AHeyZedArchitecture> It(World);It;++It)It->Apply(Cell);
}
bool AHeyZedArchitecture::Door(const FVector& Position,bool North,float Angle)
{
    const int32 X=FMath::RoundToInt(Position.X/100),Y=FMath::RoundToInt(Position.Y/100);
    if(!bMetadataReady)return PreparedDoors.Contains(Key(X,Y,FMath::RoundToInt(Position.Z/Floor))+(North?TEXT(":N"):TEXT(":W")));
    if(X<Minimum.X-1||X>Maximum.X+1||Y<Minimum.Y-1||Y>Maximum.Y+1)return false;
    const FString Address=Key(X,Y,FMath::RoundToInt(Position.Z/Floor));
    bool Represented=false,Mirror=false;FString AssemblyOwner=Address;
    if(const auto* Cell=Cells.Find(Address))for(auto& W:(*Cell)->GetArrayField(TEXT("walls")))
    {
        auto Wall=W->AsObject();if(Wall->GetStringField(TEXT("axis"))!=(North?TEXT("N"):TEXT("W")))continue;
        bool Context=false,Present=false;Wall->TryGetBoolField(TEXT("contextOnly"),Context);Wall->TryGetBoolField(TEXT("door"),Present);
        Represented=!Context&&Present;Wall->TryGetBoolField(TEXT("mirrorU"),Mirror);
        const TArray<TSharedPtr<FJsonValue>>* Anchor=nullptr;if(Wall->TryGetArrayField(TEXT("assemblyAnchor"),Anchor)&&Anchor->Num()>=3)AssemblyOwner=Key((*Anchor)[0]->AsNumber(),(*Anchor)[1]->AsNumber(),(*Anchor)[2]->AsNumber());
    }
    struct FSwing{FVector Pivot;double Scale;};TArray<FSwing> Swings;
    if(const auto* Cell=Cells.Find(AssemblyOwner))for(auto& W:(*Cell)->GetArrayField(TEXT("walls")))
    {
        auto Wall=W->AsObject();if(Wall->GetStringField(TEXT("axis"))!=(North?TEXT("N"):TEXT("W")))continue;
        Wall->TryGetBoolField(TEXT("mirrorU"),Mirror);FString Type;Wall->TryGetStringField(TEXT("insert"),Type);auto Definition=Resources->Definition(Type);if(!Definition)continue;
        const auto& AddressValues=(*Cell)->GetArrayField(TEXT("address"));const FVector Base(AddressValues[0]->AsNumber()*100,AddressValues[1]->AsNumber()*100,AddressValues[2]->AsNumber()*Floor);
        const TArray<TSharedPtr<FJsonValue>>* Models=nullptr;if(!Definition->GetObjectField(TEXT("states"))->TryGetArrayField(TEXT("open"),Models))continue;
        double Count=1;Definition->TryGetNumberField(TEXT("cells"),Count);
        for(auto& Model:*Models){const TSharedPtr<FJsonObject>* Motion=nullptr;if(!Model->AsObject()->TryGetObjectField(TEXT("motion"),Motion))continue;const auto& V=(*Motion)->GetArrayField(TEXT("pivot"));FVector P(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber());if(Mirror)P.X=Count*100-P.X;Swings.Add({Base+FRotator(0,North?0:90,0).RotateVector(P)-GetActorLocation(),(*Motion)->GetNumberField(TEXT("angle"))/90});}
    }
    const auto* Parts=Instances.Find(AssemblyOwner);if (!Parts) return Represented;bool Found=false;
    for (const auto& Part : *Parts) if (Part.Door==Address+(North?TEXT(":N"):TEXT(":W")))
    {
        double Scale=Mirror?-1:1;for(const auto& Swing:Swings)if(Swing.Pivot.Equals(Part.Closed.GetTranslation(),.01)){Scale*=Swing.Scale;break;}
        FTransform Transform=Part.Closed;Transform.SetRotation(Part.Closed.GetRotation()*FQuat(FVector::UpVector,FMath::DegreesToRadians(Angle*Scale)));
        FTransform Current;if(Part.Component->GetInstanceTransform(Part.Index,Current,false)&&!Current.Equals(Transform,1.e-5))Part.Component->UpdateInstanceTransform(Part.Index,Transform,false,false);Found=true;
    }
    return Found||Represented;
}
AHeyZedArchitecture* AHeyZedArchitecture::Find(UWorld* World,const FVector& Position)
{
    for (TActorIterator<AHeyZedArchitecture> It(World);It;++It) if(It->Covers(Position))return *It;
    return nullptr;
}
