#include "HeyZedGroundRegionAuthor.h"
#include "Dom/JsonObject.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/SavePackage.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Slate/SceneViewport.h"

bool UHeyZedGroundRegionAuthor::Build(const FString& Filename,const FString& PackageName,const TArray<FString>& MaterialNames,const TArray<UMaterialInterface*>& MaterialVariants,const FString& ReportFilename)
{
    if(!PackageName.StartsWith(TEXT("/Game/HeyZed/GroundRegions/Samples/"))||MaterialNames.IsEmpty())return false;
    FString Text;if(!FFileHelper::LoadFileToString(Text,*Filename))return false;
    TSharedPtr<FJsonObject> Data;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data||Data->GetIntegerField(TEXT("schema"))!=1)return false;
    const auto& Levels=Data->GetArrayField(TEXT("levels"));
    if(Levels.Num()!=3||MaterialVariants.Num()!=Levels.Num()*MaterialNames.Num())return false;
    TSet<UMaterial*> Roots;
    for(auto* Material:MaterialVariants){if(!Material)return false;Roots.Add(Material->GetMaterial());}
    for(auto* Material:Roots)Material->CacheShaders(EMaterialShaderPrecompileMode::Default);
    for(auto* Material:MaterialVariants)Material->CacheShaders(EMaterialShaderPrecompileMode::Default);
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    for(auto* Material:MaterialVariants)
    {
        const auto* Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);
        if(!Resource||!Resource->HasValidGameThreadShaderMap()||!Resource->GetCompileErrors().IsEmpty())return false;
    }
    const double Started=FPlatformTime::Seconds();
    UPackage* Package=CreatePackage(*PackageName);
    TArray<TSharedPtr<FJsonValue>> Results;
    for(int32 Level=0;Level<Levels.Num();++Level)
    {
        const auto Document=Levels[Level]->AsObject();
        const FString Name=TEXT("Ground_")+Document->GetStringField(TEXT("name"));
        auto* Mesh=NewObject<UStaticMesh>(Package,*Name,RF_Public|RF_Standalone);
        Mesh->SetNumSourceModels(1);auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
        Settings.bRecomputeNormals=false;Settings.bRecomputeTangents=false;Settings.bGenerateLightmapUVs=false;
        Settings.bRemoveDegenerates=false;Settings.bUseFullPrecisionUVs=true;Settings.DistanceFieldResolutionScale=0;
        FMeshDescription Geometry;FStaticMeshAttributes Attributes(Geometry);Attributes.Register();
        auto Positions=Attributes.GetVertexPositions();auto Normals=Attributes.GetVertexInstanceNormals();
        auto Tangents=Attributes.GetVertexInstanceTangents();auto Signs=Attributes.GetVertexInstanceBinormalSigns();
        auto Colors=Attributes.GetVertexInstanceColors();auto UV=Attributes.GetVertexInstanceUVs();UV.SetNumChannels(1);
        auto Names=Attributes.GetPolygonGroupMaterialSlotNames();
        int32 Triangles=0;double SourceArea=0;
        for(const auto& Value:Document->GetArrayField(TEXT("surfaces")))
        {
            const auto Surface=Value->AsObject();const FString Material=Surface->GetStringField(TEXT("material"));
            const int32 Index=MaterialNames.IndexOfByKey(Material);if(Index==INDEX_NONE)return false;
            const auto Group=Geometry.CreatePolygonGroup();Names[Group]=FName(*Material);
            Mesh->GetStaticMaterials().Add(FStaticMaterial(MaterialVariants[Level*MaterialNames.Num()+Index],FName(*Material)));
            const auto& Vertices=Surface->GetArrayField(TEXT("vertices"));if(Vertices.Num()%3)return false;
            for(int32 I=0;I<Vertices.Num();I+=3)
            {
                TArray<FVertexInstanceID> Triangle;
                TArray<FVector3d> Points;
                for(int32 J:{0,2,1})
                {
                    const auto& Vertex=Vertices[I+J]->AsArray();if(Vertex.Num()!=6)return false;
                    const double X=Vertex[0]->AsNumber(),Y=Vertex[1]->AsNumber(),Z=Vertex[2]->AsNumber();
                    if(!FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(Z)||X<-.00001||Y<-.00001||X>256.00001||Y>256.00001||FMath::Abs(Z)>1e-9)return false;
                    FVector4f Gates(0,0,0,1);
                    for(int32 Gate=0;Gate<3;++Gate){const double GateValue=Vertex[Gate+3]->AsNumber();if(!FMath::IsFinite(GateValue)||GateValue<0||GateValue>1)return false;Gates[Gate]=GateValue;}
                    const auto V=Geometry.CreateVertex();Positions[V]=FVector3f(X*100,Y*100,Z*100);
                    Points.Add(FVector3d(Positions[V]));
                    const auto Instance=Geometry.CreateVertexInstance(V);Normals[Instance]=FVector3f(0,0,1);
                    Tangents[Instance]=FVector3f(1,0,0);Signs[Instance]=1;Colors[Instance]=Gates;
                    UV.Set(Instance,0,FVector2f(X,Y));Triangle.Add(Instance);
                }
                SourceArea+=FMath::Abs(FVector3d::CrossProduct(Points[1]-Points[0],Points[2]-Points[0]).Z)*.00005;
                Geometry.CreateTriangle(Group,Triangle);++Triangles;
            }
        }
        UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bFastBuild=false;Params.bUseHashAsGuid=true;
        if(!Mesh->BuildFromMeshDescriptions({&Geometry},Params))return false;
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        const auto* Render=Mesh->GetRenderData();if(!Render||Render->LODResources.IsEmpty())return false;
        const auto& LOD=Render->LODResources[0];const auto Indices=LOD.IndexBuffer.GetArrayView();double RenderArea=0;
        for(int32 I=0;I<Indices.Num();I+=3){
            const FVector3d A(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I]));
            const FVector3d B(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+1]));
            const FVector3d C(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+2]));
            RenderArea+=FMath::Abs(FVector3d::CrossProduct(B-A,C-A).Z)*.00005;
        }
        UE_LOG(LogTemp,Display,TEXT("Structural %s: input %d, rendered %u triangles, source %.9f, rendered %.9f square meters"),*Name,Triangles,LOD.GetNumTriangles(),SourceArea,RenderArea);
        if(FMath::Abs(SourceArea-RenderArea)>FMath::Max(.0001,SourceArea*1e-8))return false;
        auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("name"),Document->GetStringField(TEXT("name")));
        Result->SetStringField(TEXT("mesh"),Mesh->GetPathName());Result->SetNumberField(TEXT("triangles"),LOD.GetNumTriangles());
        Result->SetNumberField(TEXT("input_triangles"),Triangles);Result->SetNumberField(TEXT("source_area_m2"),SourceArea);Result->SetNumberField(TEXT("render_area_m2"),RenderArea);
        Result->SetNumberField(TEXT("material_sections"),Render->LODResources[0].Sections.Num());Results.Add(MakeShared<FJsonValueObject>(Result));
        FResourceSizeEx ResourceSize(EResourceSizeMode::Exclusive);Mesh->GetResourceSizeEx(ResourceSize);
        Result->SetNumberField(TEXT("mesh_resource_bytes"),ResourceSize.GetTotalMemoryBytes());
    }
    const FString Target=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(Package,nullptr,*Target,Save))return false;
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("complete"),true);Report->SetArrayField(TEXT("levels"),Results);
    Report->SetNumberField(TEXT("native_mesh_seconds"),FPlatformTime::Seconds()-Started);
    Report->SetNumberField(TEXT("package_bytes"),IFileManager::Get().FileSize(*Target));
    TSet<UTexture*> Textures;
    for(auto* Material:MaterialVariants){TArray<UTexture*> Used;Material->GetUsedTextures(Used);Textures.Append(Used);}
    uint64 TextureBytes=0;
    for(auto* Texture:Textures){FResourceSizeEx Size(EResourceSizeMode::Exclusive);Texture->GetResourceSizeEx(Size);TextureBytes+=Size.GetTotalMemoryBytes();}
    Report->SetNumberField(TEXT("shared_texture_resource_bytes"),double(TextureBytes));
    Report->SetNumberField(TEXT("shared_texture_count"),Textures.Num());
    const auto Memory=FPlatformMemory::GetStats();Report->SetNumberField(TEXT("peak_private_bytes"),Memory.PeakUsedVirtual);
    Report->SetNumberField(TEXT("peak_physical_bytes"),Memory.PeakUsedPhysical);
    Text.Reset();FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    return FFileHelper::SaveStringToFile(Text,*ReportFilename);
}

FString UHeyZedGroundRegionAuthor::FrameMetrics()
{
    auto Report=MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("gpu_ms"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    Report->SetNumberField(TEXT("game_ms"),FPlatformTime::ToMilliseconds(GGameThreadTime));
    Report->SetNumberField(TEXT("render_ms"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Report->SetNumberField(TEXT("rhi_ms"),FPlatformTime::ToMilliseconds(GRHIThreadTime));
    Report->SetNumberField(TEXT("draw_calls"),GNumDrawCallsRHI[0]);
    Report->SetNumberField(TEXT("primitives"),GNumPrimitivesDrawnRHI[0]);
    if(GEditor&&GEditor->GetActiveViewport()){
        const auto Size=GEditor->GetActiveViewport()->GetSizeXY();
        Report->SetNumberField(TEXT("viewport_width"),Size.X);Report->SetNumberField(TEXT("viewport_height"),Size.Y);
        Report->SetNumberField(TEXT("render_screen_percentage"),static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient())->GetPreviewScreenPercentage());
    }
    const auto Memory=FPlatformMemory::GetStats();
    Report->SetNumberField(TEXT("private_bytes"),Memory.UsedVirtual);
    Report->SetNumberField(TEXT("physical_bytes"),Memory.UsedPhysical);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));return Text;
}

void UHeyZedGroundRegionAuthor::ConfigureRenderViewport(int32 Width,int32 Height)
{
    if(!GEditor||!GEditor->GetActiveViewport()||Width<0||Height<0)return;
    auto* Viewport=static_cast<FSceneViewport*>(GEditor->GetActiveViewport());
    Viewport->SetFixedViewportSize(Width,Height);
    static_cast<FEditorViewportClient*>(Viewport->GetClient())->SetRealtime(true);
    static_cast<FEditorViewportClient*>(Viewport->GetClient())->SetPreviewingScreenPercentage(true);
    static_cast<FEditorViewportClient*>(Viewport->GetClient())->SetPreviewScreenPercentage(100);
}

void UHeyZedGroundRegionAuthor::ConfigurePreview(UWorld* World,FVector CameraLocation,FRotator CameraRotation)
{
    if(!World||World->WorldType!=EWorldType::Editor)return;
    World->EditorViews.SetNum(LVT_MAX);World->EditorViews[LVT_Perspective]=FLevelViewportInfo(CameraLocation,CameraRotation,DEFAULT_ORTHOZOOM);
    World->EditorViews[LVT_Perspective].CamUpdated=true;
}
