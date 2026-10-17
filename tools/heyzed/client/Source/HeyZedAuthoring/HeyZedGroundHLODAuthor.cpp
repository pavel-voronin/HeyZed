#include "HeyZedGroundHLODAuthor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EditorViewportClient.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "IMeshMergeUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "MaterialUtilities.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "MeshMergeModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldPartition/HLOD/Builders/HLODBuilderMeshSimplify.h"

bool UHeyZedGroundHLODAuthor::BuildSample(AActor* Source,const FString& PackageName,const FString& ReportFilename,double MinVisibleDistance)
{
    if(!Source||!Source->GetWorld()||!PackageName.StartsWith(TEXT("/Game/HeyZed/GroundHLODSamples/")))return false;
    TArray<UInstancedStaticMeshComponent*> Inputs;Source->GetComponents(Inputs);
    TMap<UInstancedStaticMeshComponent*,UMaterialInterface*> OriginalMaterials;
    ON_SCOPE_EXIT{for(const auto& Entry:OriginalMaterials)Entry.Key->SetMaterial(0,Entry.Value);};
    int64 Instances=0;for(auto* Input:Inputs)Instances+=Input->GetInstanceCount();
    if(Instances<=0||Instances>200000){UE_LOG(LogTemp,Error,TEXT("Ground HLOD sample requires 1..200000 source instances, got %lld"),Instances);return false;}
    UPackage* Package=CreatePackage(*PackageName);
    auto* Settings=NewObject<UHLODBuilderMeshSimplifySettings>();
    Settings->HLODMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineMaterials/FlattenMaterial_VT.FlattenMaterial_VT"));
    if(!Settings->HLODMaterial)return false;
    auto& Options=Settings->MeshSimplifySettings;
    Options.MaterialSettings.TextureSize=FIntPoint(1024,1024);
    Options.MaterialSettings.bNormalMap=true;Options.MaterialSettings.bRoughnessMap=true;
    Options.MaterialSettings.bOpacityMaskMap=true;
    Options.MaterialSettings.BlendMode=BLEND_Masked;
    Options.bGroupIdenticalMeshesForBaking=false;
    Options.bAllowDistanceField=false;Options.bCreateCollision=false;Options.bGenerateLightmapUVs=false;
    Options.MergeDistance=0;Options.bOverrideVoxelSize=true;Options.VoxelSize=100;
    Options.bReuseMeshLightmapUVs=false;
    TArray<UActorComponent*> Sources;
    TArray<TStrongObjectPtr<UObject>> KeepAlive;
    TMap<FString,UMaterialInterface*> MaterialCache;
    const double MaterialsStarted=FPlatformTime::Seconds();
    KeepAlive.Emplace(Settings);
    for(auto* Input:Inputs)
    {
        const int32 Stride=Input->NumCustomDataFloats;
        if(Stride!=8||Input->PerInstanceSMCustomData.Num()!=Input->GetInstanceCount()*Stride)return false;
        for(int32 I=Stride;I<Input->PerInstanceSMCustomData.Num();++I)
            if(Input->PerInstanceSMCustomData[I]!=Input->PerInstanceSMCustomData[I%Stride])
            {UE_LOG(LogTemp,Error,TEXT("Ground HLOD source batch has varying instance material values"));return false;}
        UMaterialInterface* Original=Input->GetMaterial(0);if(!Original)return false;
        OriginalMaterials.Add(Input,Original);
        FString MaterialKey=Original->GetPathName();
        for(int32 I=0;I<Stride;++I)MaterialKey+=TEXT("|")+FString::Printf(TEXT("%.9g"),Input->PerInstanceSMCustomData[I]);
        if(UMaterialInterface** Found=MaterialCache.Find(MaterialKey))
        {
            Input->SetMaterial(0,*Found);Input->HLODBatchingPolicy=EHLODBatchingPolicy::None;Sources.Add(Input);continue;
        }
        auto* Frozen=DuplicateObject<UMaterial>(Original->GetMaterial(),GetTransientPackage());
        for(UMaterialExpression* Expression:Frozen->GetExpressions())if(auto* Data=Cast<UMaterialExpressionPerInstanceCustomData>(Expression))
        {
            if(Data->DataIndex>=uint32(Stride))return false;
            Data->DefaultValue.Expression=nullptr;
            Data->ConstDefaultValue=Input->PerInstanceSMCustomData[Data->DataIndex];
        }
        Frozen->PostEditChange();
        Frozen->CacheShaders(EMaterialShaderPrecompileMode::Default);
        auto* Material=UMaterialInstanceDynamic::Create(Frozen,GetTransientPackage());
        Material->CopyMaterialUniformParameters(Original);
        Input->SetMaterial(0,Material);Input->HLODBatchingPolicy=EHLODBatchingPolicy::None;
        MaterialCache.Add(MaterialKey,Material);
        KeepAlive.Emplace(Frozen);KeepAlive.Emplace(Material);Sources.Add(Input);
    }
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    for(const auto& Entry:MaterialCache)
    {
        const auto* Resource=Entry.Value->GetMaterialResource(GMaxRHIShaderPlatform);
        if(!Resource||!Resource->HasValidGameThreadShaderMap()||!Resource->GetCompileErrors().IsEmpty())
        {
            UE_LOG(LogTemp,Error,TEXT("Ground HLOD source shader is invalid: %s, resource=%d, finalized=%d, compiler errors=%d"),*Entry.Key,Resource!=nullptr,Resource&&Resource->HasValidGameThreadShaderMap(),Resource?Resource->GetCompileErrors().Num():0);
            if(Resource)for(const auto& Error:Resource->GetCompileErrors())UE_LOG(LogTemp,Error,TEXT("%s"),*Error);
            return false;
        }
    }
    TRACE_BOOKMARK(TEXT("GroundSample.SourceMaterials.Ready"));
    const double MaterialSeconds=FPlatformTime::Seconds()-MaterialsStarted;
    const auto MaterialMemory=FPlatformMemory::GetStats();
    UE_LOG(LogTemp,Display,TEXT("Ground HLOD source materials ready: %d batches, %d unique combinations, %.2fs, private bytes %llu. Native geometry-only preassembly starts next."),Inputs.Num(),MaterialCache.Num(),MaterialSeconds,MaterialMemory.UsedVirtual);
    TArray<UPrimitiveComponent*> MergeInputs;
    TMap<UMaterialInterface*,int64> ExpectedTriangles;
    FBox SourceBounds(ForceInit);
    for(auto* Input:Inputs)
    {
        MergeInputs.Add(Input);SourceBounds+=Input->Bounds.GetBox();
        const auto* Render=Input->GetStaticMesh()->GetRenderData();
        if(!Render||Render->LODResources.IsEmpty()||Render->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()<1)return false;
        for(const auto& Section:Render->LODResources[0].Sections)
            ExpectedTriangles.FindOrAdd(Input->GetMaterial(Section.MaterialIndex))+=int64(Section.NumTriangles)*Input->GetInstanceCount();
    }
    FMeshMergingSettings MergeSettings;
    MergeSettings.LODSelectionType=EMeshLODSelectionType::SpecificLOD;MergeSettings.SpecificLOD=0;
    MergeSettings.bMergeMaterials=false;MergeSettings.bMergeEquivalentMaterials=false;
    MergeSettings.bGenerateLightMapUV=false;MergeSettings.bUseLandscapeCulling=false;
    MergeSettings.bMergePhysicsData=false;MergeSettings.bMergeMeshSockets=false;
    MergeSettings.bIncludeImposters=false;MergeSettings.bAllowDistanceField=false;
    MergeSettings.PivotType=EMeshMergePivotType::Custom;MergeSettings.CustomPivotLocation=Source->GetActorLocation();
    for(auto& UV:MergeSettings.OutputUVs)UV=EUVOutput::OutputChannel;
    TArray<UObject*> MergeAssets;FVector MergeLocation;
    const double MergeStarted=FPlatformTime::Seconds();
    TRACE_BOOKMARK(TEXT("GroundSample.Preassembly.Begin"));
    const auto& MergeUtilities=FModuleManager::LoadModuleChecked<IMeshMergeModule>(TEXT("MeshMergeUtilities")).GetUtilities();
    MergeUtilities.MergeComponentsToStaticMesh(MergeInputs,Source->GetWorld(),MergeSettings,nullptr,GetTransientPackage(),TEXT("GroundSamplePreassembly"),MergeAssets,MergeLocation,1.f,true);
    UStaticMesh* MergedMesh=nullptr;
    for(auto* Asset:MergeAssets)
    {
        Asset->ClearFlags(RF_Public|RF_Standalone);Asset->SetFlags(RF_Transient);KeepAlive.Emplace(Asset);
        if(auto* Mesh=Cast<UStaticMesh>(Asset)){if(MergedMesh)return false;MergedMesh=Mesh;}
        else {UE_LOG(LogTemp,Error,TEXT("Ground geometry-only preassembly unexpectedly generated a non-mesh asset"));return false;}
    }
    if(!MergedMesh)return false;
    FStaticMeshCompilingManager::Get().FinishCompilation({MergedMesh});
    const auto* MergedRender=MergedMesh->GetRenderData();
    if(!MergedRender||MergedRender->LODResources.IsEmpty()||MergedRender->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()<1)return false;
    TMap<UMaterialInterface*,int64> MergedTriangles;
    for(const auto& Section:MergedRender->LODResources[0].Sections)
        MergedTriangles.FindOrAdd(MergedMesh->GetMaterial(Section.MaterialIndex))+=Section.NumTriangles;
    if(MergedTriangles.Num()!=ExpectedTriangles.Num()||MergedMesh->GetStaticMaterials().Num()!=MaterialCache.Num())return false;
    for(const auto& Entry:ExpectedTriangles)
    {
        const auto* Count=MergedTriangles.Find(Entry.Key);
        if(!Count||*Count!=Entry.Value){UE_LOG(LogTemp,Error,TEXT("Ground preassembly changed material ownership or triangle counts"));return false;}
    }
    const FBox MergedBounds=MergedMesh->GetBoundingBox().ShiftBy(MergeLocation);
    if(!SourceBounds.Min.Equals(MergedBounds.Min,.25)||!SourceBounds.Max.Equals(MergedBounds.Max,.25))
    {UE_LOG(LogTemp,Error,TEXT("Ground preassembly changed world-space bounds"));return false;}
    auto* MergedComponent=NewObject<UStaticMeshComponent>(Source);
    ON_SCOPE_EXIT{MergedComponent->DestroyComponent();};
    MergedComponent->SetMobility(EComponentMobility::Static);MergedComponent->SetStaticMesh(MergedMesh);
    MergedComponent->SetWorldLocation(MergeLocation);MergedComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MergedComponent->SetCanEverAffectNavigation(false);MergedComponent->SetCastShadow(false);
    MergedComponent->HLODBatchingPolicy=EHLODBatchingPolicy::None;MergedComponent->RegisterComponent();
    KeepAlive.Emplace(MergedComponent);Sources.Reset();Sources.Add(MergedComponent);
    const double MergeSeconds=FPlatformTime::Seconds()-MergeStarted;
    TRACE_BOOKMARK(TEXT("GroundSample.Preassembly.Ready"));
    double WorldArea=0;
    const auto& SourceLOD=MergedRender->LODResources[0];
    for(int32 I=0;I+2<SourceLOD.IndexBuffer.GetNumIndices();I+=3)
    {
        const FVector A(SourceLOD.VertexBuffers.PositionVertexBuffer.VertexPosition(SourceLOD.IndexBuffer.GetIndex(I)));
        const FVector B(SourceLOD.VertexBuffers.PositionVertexBuffer.VertexPosition(SourceLOD.IndexBuffer.GetIndex(I+1)));
        const FVector C(SourceLOD.VertexBuffers.PositionVertexBuffer.VertexPosition(SourceLOD.IndexBuffer.GetIndex(I+2)));
        WorldArea+=FVector::CrossProduct(B-A,C-A).Length()*.5;
    }
    const float NativeDensity=FMaterialUtilities::ComputeRequiredTexelDensityFromDrawDistance(MinVisibleDistance,MergedMesh->GetBounds().SphereRadius);
    const int32 NativeDistanceResolution=FMaterialUtilities::GetTextureSizeFromTargetTexelDensity(WorldArea,1.,NativeDensity);
    UE_LOG(LogTemp,Display,TEXT("Ground HLOD texture policy: actual 1024, native draw-distance estimate %d, %.3f texels/m, distance %.1fcm, source area %.1fcm2"),NativeDistanceResolution,NativeDensity,MinVisibleDistance,WorldArea);
    UE_LOG(LogTemp,Display,TEXT("Ground HLOD geometry-only preassembly ready: one mesh, %d material sections, %u triangles, %.2fs. Native proxy bake/simplify starts next."),MergedRender->LODResources[0].Sections.Num(),MergedRender->LODResources[0].GetNumTriangles(),MergeSeconds);
    auto* Builder=NewObject<UHLODBuilderMeshSimplify>();Builder->SetHLODBuilderSettings(Settings);
    KeepAlive.Emplace(Builder);
    FHLODBuildContext Context{};
    Context.TargetWorld=Source->GetWorld();Context.BuildWorld=Source->GetWorld();Context.AssetsOuter=Package;
    Context.AssetsBaseName=TEXT("GroundSample");Context.MinVisibleDistance=MinVisibleDistance;
    Context.AssetsObjectFlags=RF_Public|RF_Standalone;Context.SourceComponents=Sources;
    const double Started=FPlatformTime::Seconds();
    TRACE_BOOKMARK(TEXT("GroundSample.Proxy.Begin"));
    auto Outputs=Builder->Build(Context,Sources);
    TRACE_BOOKMARK(TEXT("GroundSample.Proxy.Ready"));
    const double ProxySeconds=FPlatformTime::Seconds()-Started;
    TArray<TSharedPtr<FJsonValue>> OutputRecords;
    int64 Triangles=0,Vertices=0;int32 Meshes=0,VirtualTextures=0,ShaderErrors=0;TSet<UTexture*> Textures;
    for(auto* Output:Outputs)if(auto* Component=Cast<UStaticMeshComponent>(Output))if(UStaticMesh* Mesh=Component->GetStaticMesh())
        for(const auto& Slot:Mesh->GetStaticMaterials())if(Slot.MaterialInterface)Slot.MaterialInterface->CacheShaders(EMaterialShaderPrecompileMode::Default);
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    for(auto* Output:Outputs)if(auto* Component=Cast<UStaticMeshComponent>(Output))if(UStaticMesh* Mesh=Component->GetStaticMesh())
    {
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});++Meshes;
        auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("mesh"),Mesh->GetPathName());
        const auto Position=Component->GetComponentLocation();const auto Rotation=Component->GetComponentRotation();const auto Scale=Component->GetComponentScale();
        auto VectorValues=[](double X,double Y,double Z){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(X),MakeShared<FJsonValueNumber>(Y),MakeShared<FJsonValueNumber>(Z)};};
        Record->SetArrayField(TEXT("location"),VectorValues(Position.X,Position.Y,Position.Z));
        Record->SetArrayField(TEXT("rotation"),VectorValues(Rotation.Pitch,Rotation.Yaw,Rotation.Roll));
        Record->SetArrayField(TEXT("scale"),VectorValues(Scale.X,Scale.Y,Scale.Z));OutputRecords.Add(MakeShared<FJsonValueObject>(Record));
        if(auto* Render=Mesh->GetRenderData();Render&&Render->LODResources.Num())
        {Triangles+=Render->LODResources[0].GetNumTriangles();Vertices+=Render->LODResources[0].GetNumVertices();}
        for(const auto& Slot:Mesh->GetStaticMaterials())if(Slot.MaterialInterface)
        {
            const auto* Resource=Slot.MaterialInterface->GetMaterialResource(GMaxRHIShaderPlatform);
            if(!Resource||!Resource->HasValidGameThreadShaderMap())++ShaderErrors;
            if(Resource)ShaderErrors+=Resource->GetCompileErrors().Num();
            TArray<UTexture*> Used;Slot.MaterialInterface->GetUsedTextures(Used);
            Textures.Append(Used);
        }
    }
    TArray<TSharedPtr<FJsonValue>> TextureRecords;
    for(auto* Texture:Textures)if(Texture->GetOutermost()==Package&&Texture->VirtualTextureStreaming)
    {
        ++VirtualTextures;
        if(auto* Image=Cast<UTexture2D>(Texture))
        {
            auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("path"),Image->GetPathName());
            Record->SetNumberField(TEXT("width"),Image->GetSizeX());Record->SetNumberField(TEXT("height"),Image->GetSizeY());
            TextureRecords.Add(MakeShared<FJsonValueObject>(Record));
        }
    }
    const double Seconds=FPlatformTime::Seconds()-Started;
    const bool Valid=Meshes>0&&Triangles>0&&VirtualTextures>0&&ShaderErrors==0;
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    bool Saved=false;
    const double SaveStarted=FPlatformTime::Seconds();
    if(Valid){FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Saved=UPackage::SavePackage(Package,nullptr,*Filename,Args);}
    const double SaveSeconds=FPlatformTime::Seconds()-SaveStarted;
    TRACE_BOOKMARK(TEXT("GroundSample.Package.Saved"));
    int64 PackageBytes=0;
    for(const TCHAR* Extension:{TEXT("uasset"),TEXT("uexp"),TEXT("ubulk"),TEXT("uptnl")})
        PackageBytes+=FMath::Max(int64(0),IFileManager::Get().FileSize(*FPaths::ChangeExtension(Filename,Extension)));
    const auto Memory=FPlatformMemory::GetStats();
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("complete"),Valid&&Saved);Report->SetStringField(TEXT("representation"),TEXT("native-mesh-simplify-vt-sample"));
    Report->SetBoolField(TEXT("preview_complete"),false);Report->SetArrayField(TEXT("outputs"),OutputRecords);
    Report->SetNumberField(TEXT("texture_size"),1024);Report->SetArrayField(TEXT("virtual_texture_images"),TextureRecords);
    Report->SetNumberField(TEXT("native_distance_texture_size"),NativeDistanceResolution);
    Report->SetNumberField(TEXT("native_texels_per_meter"),NativeDensity);
    Report->SetNumberField(TEXT("source_world_area_cm2"),WorldArea);Report->SetNumberField(TEXT("shader_errors"),ShaderErrors);
    Report->SetStringField(TEXT("package"),PackageName);Report->SetNumberField(TEXT("source_instances"),Instances);
    Report->SetNumberField(TEXT("source_batches"),Inputs.Num());Report->SetNumberField(TEXT("seconds"),Seconds);
    Report->SetNumberField(TEXT("unique_source_materials"),MaterialCache.Num());
    Report->SetNumberField(TEXT("source_material_seconds"),MaterialSeconds);
    Report->SetNumberField(TEXT("source_material_peak_private_bytes"),MaterialMemory.PeakUsedVirtual);
    Report->SetNumberField(TEXT("preassembly_seconds"),MergeSeconds);
    Report->SetNumberField(TEXT("preassembly_material_sections"),MergedRender->LODResources[0].Sections.Num());
    Report->SetNumberField(TEXT("preassembly_triangles"),MergedRender->LODResources[0].GetNumTriangles());
    Report->SetBoolField(TEXT("proxy_seconds_include_export_shader_compilation"),true);
    Report->SetNumberField(TEXT("proxy_build_seconds"),ProxySeconds);
    Report->SetNumberField(TEXT("proxy_validation_seconds"),Seconds-ProxySeconds);
    Report->SetNumberField(TEXT("package_save_seconds"),SaveSeconds);
    Report->SetNumberField(TEXT("meshes"),Meshes);Report->SetNumberField(TEXT("triangles"),Triangles);Report->SetNumberField(TEXT("vertices"),Vertices);
    Report->SetNumberField(TEXT("virtual_textures"),VirtualTextures);Report->SetNumberField(TEXT("peak_physical_bytes"),Memory.PeakUsedPhysical);
    Report->SetNumberField(TEXT("peak_virtual_bytes"),Memory.PeakUsedVirtual);Report->SetNumberField(TEXT("package_bytes"),PackageBytes);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*ReportFilename);
    UE_LOG(LogTemp,Display,TEXT("Ground HLOD sample: %lld instances -> %lld triangles, %d VT, %.2fs, complete=%d"),Instances,Triangles,VirtualTextures,Seconds,Valid&&Saved);
    return Valid&&Saved;
}

void UHeyZedGroundHLODAuthor::ConfigurePreviewView(AActor* Source)
{
    if(!Source||!Source->GetWorld())return;
    UWorld* World=Source->GetWorld();World->EditorViews.SetNum(LVT_MAX);
    World->EditorViews[LVT_Perspective]=FLevelViewportInfo(Source->GetActorLocation()+FVector(-2200,-7000,28000),FRotator(-55,90,0),DEFAULT_ORTHOZOOM);
    World->EditorViews[LVT_Perspective].CamUpdated=true;
}
