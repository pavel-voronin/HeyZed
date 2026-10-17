#include "HeyZedFurniture.h"
#if WITH_EDITOR
#include "HeyZedMeshAuthoring.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#endif

bool UHeyZedFurnitureLibrary::PrepareMesh(const FString& Name,const FString& Source,const TArray<UMaterialInterface*>& Materials)
{
#if WITH_EDITOR
    FString Text;TSharedPtr<FJsonObject> Data;
    if(!FFileHelper::LoadFileToString(Text,*Source)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data))return false;
    const auto& Vertices=Data->GetArrayField(TEXT("vertices"));const auto& Groups=Data->GetArrayField(TEXT("groups"));
    if(Vertices.IsEmpty()||Materials.Num()!=Groups.Num())return false;
    const FString PackageName=TEXT("/Game/HeyZed/Furniture/Meshes/SM_")+Name,AssetName=FPackageName::GetShortName(PackageName);
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*(PackageName+TEXT(".")+AssetName));
    if(!Mesh){Mesh=NewObject<UStaticMesh>(CreatePackage(*PackageName),*AssetName,RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Mesh);}
    FinishHeyZedMesh(Mesh);FMeshDescription Description;FStaticMeshAttributes Attributes(Description);Attributes.Register();
    auto Positions=Attributes.GetVertexPositions();auto Normals=Attributes.GetVertexInstanceNormals();auto UV=Attributes.GetVertexInstanceUVs();UV.SetNumChannels(1);
    TArray<FVertexID> IDs;
    for(const auto& Item:Vertices)
    {
        const auto& Row=Item->AsArray();if(Row.Num()!=8)return false;
        auto V=Description.CreateVertex();Positions[V]=FVector3f(Row[0]->AsNumber(),Row[1]->AsNumber(),Row[2]->AsNumber());IDs.Add(V);
    }
    Mesh->GetStaticMaterials().Reset();
    for(int32 I=0;I<Groups.Num();++I)
    {
        const FName Slot(*FString::Printf(TEXT("Part%d"),I));auto Group=Description.CreatePolygonGroup();
        Attributes.GetPolygonGroupMaterialSlotNames()[Group]=Slot;Mesh->GetStaticMaterials().Add(FStaticMaterial(Materials[I],Slot));
        const auto& Indices=Groups[I]->AsArray();if(Indices.Num()%3)return false;
        for(int32 J=0;J<Indices.Num();J+=3)
        {
            TArray<FVertexInstanceID> Corners;
            for(int32 K=0;K<3;++K)
            {
                const int32 Index=Indices[J+K]->AsNumber();if(!IDs.IsValidIndex(Index))return false;
                const auto& Row=Vertices[Index]->AsArray();auto Corner=Description.CreateVertexInstance(IDs[Index]);
                Normals[Corner]=FVector3f(Row[3]->AsNumber(),Row[4]->AsNumber(),Row[5]->AsNumber()).GetSafeNormal();
                UV.Set(Corner,0,FVector2f(Row[6]->AsNumber(),Row[7]->AsNumber()));Corners.Add(Corner);
            }
            Description.CreateTriangle(Group,Corners);
        }
    }
    Mesh->SetNumSourceModels(1);auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals=false;Settings.bRecomputeTangents=true;Settings.bGenerateLightmapUVs=false;
    Mesh->bSupportRayTracing=true;auto& Nanite=Mesh->GetNaniteSettings();Nanite.bEnabled=true;
    Nanite.FallbackTarget=ENaniteFallbackTarget::RelativeError;Nanite.FallbackRelativeError=0;Nanite.FallbackPercentTriangles=1;
    UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bFastBuild=false;Params.bUseHashAsGuid=true;
    if(!Mesh->BuildFromMeshDescriptions({&Description},Params))return false;
    FinishHeyZedMesh(Mesh);Mesh->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    if(!UPackage::SavePackage(Mesh->GetOutermost(),Mesh,*Filename,Args))return false;
    Meshes.Add(Name,TSoftObjectPtr<UStaticMesh>(Mesh));MarkPackageDirty();return true;
#else
    return false;
#endif
}
