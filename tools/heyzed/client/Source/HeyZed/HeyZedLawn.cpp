#include "HeyZedTerrain.h"
#include "HeyZedMeshAuthoring.h"

#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#endif

bool UHeyZedGroundLibrary::PrepareGrass()
{
#if WITH_EDITOR
    FString Text;
    if (!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectDir(),TEXT("../.local/bermuda-lawn.json")))) return false;
    TSharedPtr<FJsonObject> Source;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Source)) return false;
    const auto& Vertices=Source->GetArrayField(TEXT("vertices"));
    const auto& Indices=Source->GetArrayField(TEXT("indices"));
    const FString Name=TEXT("SM_GrassClump"), PackageName=TEXT("/Game/HeyZed/Vegetation/")+Name;
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*(PackageName+TEXT(".")+Name));
    UPackage* Package=Mesh ? Mesh->GetOutermost() : CreatePackage(*PackageName);
    if (!Mesh)
    {
        Mesh=NewObject<UStaticMesh>(Package,*Name,RF_Public|RF_Standalone);
        FAssetRegistryModule::AssetCreated(Mesh);
    }
    FinishHeyZedMesh(Mesh);
    Mesh->bDoFastBuild=false; Mesh->bSupportRayTracing=false;
    Mesh->SetAutoComputeLODScreenSize(false); Mesh->SetNumSourceModels(3);
    FMeshDescription Descriptions[3];
    for (int32 LOD=0;LOD<3;++LOD)
    {
        auto& Description=Descriptions[LOD];
        FStaticMeshAttributes Attributes(Description); Attributes.Register();
        auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
        auto Tangents=Attributes.GetVertexInstanceTangents(); auto Signs=Attributes.GetVertexInstanceBinormalSigns();
        auto UV=Attributes.GetVertexInstanceUVs(); UV.SetNumChannels(1);
        const auto Group=Description.CreatePolygonGroup(); Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Grass");
        TArray<FVertexID> IDs; TArray<FVector3f> SourceNormals; TArray<FVector2f> Tex;
        for (const auto& Value : Vertices)
        {
            const auto& Row=Value->AsArray();
            const auto Vertex=Description.CreateVertex(); IDs.Add(Vertex);
            Positions[Vertex]=FVector3f(float(Row[0]->AsNumber()),float(Row[1]->AsNumber()),float(Row[2]->AsNumber()));
            SourceNormals.Add(FVector3f(float(Row[3]->AsNumber()),float(Row[4]->AsNumber()),float(Row[5]->AsNumber())).GetSafeNormal());
            Tex.Add(FVector2f(float(Row[6]->AsNumber()),float(Row[7]->AsNumber())));
        }
        for (int32 I=0;I+2<Indices.Num();I+=3)
        {
            const int32 A=int32(Indices[I]->AsNumber()), B=int32(Indices[I+1]->AsNumber()), C=int32(Indices[I+2]->AsNumber());
            const FVector3f E1=Positions[IDs[B]]-Positions[IDs[A]], E2=Positions[IDs[C]]-Positions[IDs[A]];
            const FVector2f D1=Tex[B]-Tex[A], D2=Tex[C]-Tex[A];
            const float Determinant=D1.X*D2.Y-D1.Y*D2.X;
            const float Reciprocal=FMath::Abs(Determinant)>1e-8f ? 1.f/Determinant : 1.f;
            const FVector3f T=(E1*D2.Y-E2*D1.Y)*Reciprocal, V=(E2*D1.X-E1*D2.X)*Reciprocal;
            TArray<FVertexInstanceID> Corners;
            for (int32 Index : {A,B,C})
            {
                const auto Instance=Description.CreateVertexInstance(IDs[Index]);
                const FVector3f N=SourceNormals[Index];
                const FVector3f Tangent=(T-N*FVector3f::DotProduct(N,T)).GetSafeNormal(SMALL_NUMBER,FVector3f(1,0,0));
                Normals[Instance]=N; Tangents[Instance]=Tangent;
                Signs[Instance]=FVector3f::DotProduct(FVector3f::CrossProduct(N,Tangent),V)<0 ? -1.f : 1.f;
                UV.Set(Instance,0,Tex[Index]); Corners.Add(Instance);
            }
            Description.CreateTriangle(Group,Corners);
        }
        auto& Model=Mesh->GetSourceModel(LOD);
        Model.ScreenSize.Default=LOD==0 ? 1.f : LOD==1 ? .10f : .045f;
        Model.BuildSettings.bRecomputeNormals=false; Model.BuildSettings.bRecomputeTangents=false;
        Model.BuildSettings.bGenerateLightmapUVs=false;
        Model.BuildSettings.DistanceFieldResolutionScale=0; Model.BuildSettings.MaxLumenMeshCards=0;
        Model.ReductionSettings.BaseLODModel=0;
        Model.ReductionSettings.PercentTriangles=LOD==0 ? 1.f : LOD==1 ? .55f : .22f;
    }
    Mesh->GetStaticMaterials().Reset(); Mesh->GetStaticMaterials().Add(FStaticMaterial(GrassMaterial,TEXT("Grass")));
    UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bUseHashAsGuid=true; Params.bFastBuild=false;
    if (!Mesh->BuildFromMeshDescriptions({&Descriptions[0],&Descriptions[1],&Descriptions[2]},Params)) return false;
    FinishHeyZedMesh(Mesh); Package->MarkPackageDirty();
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    GrassMesh=Mesh; const bool Saved=UPackage::SavePackage(Package,Mesh,*Filename,Args);
    FinishHeyZedMesh(Mesh); return Saved;
#else
    return false;
#endif
}
