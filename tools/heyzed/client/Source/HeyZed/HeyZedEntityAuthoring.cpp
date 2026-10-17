#include "HeyZedEntities.h"
#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "MeshUtilities.h"
#include "LODUtilities.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "HeyZedMeshAuthoring.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HeyZedMotion.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#include "RetargetEditor/IKRetargeterController.h"

namespace {
FMatrix NativeBasis(){FMatrix B=FMatrix::Identity;B.M[0][0]=-100;B.M[1][1]=0;B.M[1][2]=100;B.M[2][1]=100;B.M[2][2]=0;return B;}
FMatrix NativeMatrix(const TArray<TSharedPtr<FJsonValue>>& A){FMatrix M;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)M.M[R][C]=A[R*4+C]->AsNumber();return NativeBasis().Inverse()*M*NativeBasis();}
FVector3f NativeVector(const TArray<TSharedPtr<FJsonValue>>& A,int32 I,float Scale){return FVector3f(-A[I]->AsNumber()*Scale,A[I+2]->AsNumber()*Scale,A[I+1]->AsNumber()*Scale);}
int32 FacingCorner(const TArray<TSharedPtr<FJsonValue>>& P,const TArray<TSharedPtr<FJsonValue>>& N,const TArray<TSharedPtr<FJsonValue>>& Indices,int32 Face,int32 Corner){int32 A=Indices[Face]->AsNumber(),B=Indices[Face+1]->AsNumber(),C=Indices[Face+2]->AsNumber();auto Front=FVector3f::CrossProduct(NativeVector(P,C*3,1)-NativeVector(P,A*3,1),NativeVector(P,B*3,1)-NativeVector(P,A*3,1));auto Normal=NativeVector(N,A*3,1)+NativeVector(N,B*3,1)+NativeVector(N,C*3,1);return FVector3f::DotProduct(Front,Normal)<0&&Corner>0?3-Corner:Corner;}
}
#endif

bool UHeyZedEntityResources::PreparePlayerRetargeters()
{
#if WITH_EDITOR
    auto* Motion=LoadObject<UHeyZedMotionLibrary>(nullptr,TEXT("/Game/HeyZed/Animation/Library.Library"));if(!Motion||!Motion->Mesh)return false;
    auto Save=[](UObject* Asset){FAssetRegistryModule::AssetCreated(Asset);Asset->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);};
    auto Rig=[&](const FString& Name,USkeletalMesh* Mesh,bool Native){const FString Path=TEXT("/Game/HeyZed/Entities/Player/")+Name;auto* Asset=LoadObject<UIKRigDefinition>(nullptr,*(Path+TEXT(".")+Name));if(!Asset)Asset=NewObject<UIKRigDefinition>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);auto* C=UIKRigController::GetController(Asset);C->SetSkeletalMesh(Mesh);const auto OldChains=C->GetRetargetChains();for(const auto& Chain:OldChains)C->RemoveRetargetChain(Chain.ChainName);C->SetRetargetRoot(Native?TEXT("Bip01_Pelvis"):TEXT("pelvis"));
        const TCHAR* Names[]={TEXT("Spine"),TEXT("Head"),TEXT("LeftArm"),TEXT("RightArm"),TEXT("LeftLeg"),TEXT("RightLeg")};
        const TCHAR* SourceStart[]={TEXT("spine_01"),TEXT("neck_01"),TEXT("clavicle_l"),TEXT("clavicle_r"),TEXT("thigh_l"),TEXT("thigh_r")};const TCHAR* SourceEnd[]={TEXT("spine_03"),TEXT("head"),TEXT("hand_l"),TEXT("hand_r"),TEXT("foot_l"),TEXT("foot_r")};
        const TCHAR* TargetStart[]={TEXT("Bip01_Spine"),TEXT("Bip01_Neck"),TEXT("Bip01_L_Clavicle"),TEXT("Bip01_R_Clavicle"),TEXT("Bip01_L_Thigh"),TEXT("Bip01_R_Thigh")};const TCHAR* TargetEnd[]={TEXT("Bip01_Spine1"),TEXT("Bip01_Head"),TEXT("Bip01_L_Hand"),TEXT("Bip01_R_Hand"),TEXT("Bip01_L_Foot"),TEXT("Bip01_R_Foot")};
        for(int32 I=0;I<6;++I)C->AddRetargetChain(Names[I],Native?TargetStart[I]:SourceStart[I],Native?TargetEnd[I]:SourceEnd[I],NAME_None);return Save(Asset)?Asset:nullptr;};
    auto* Source=Rig(TEXT("IK_MotionSource"),Motion->Mesh,false);if(!Source)return false;
    for(const FString Sex:{FString(TEXT("male")),FString(TEXT("female"))}){const FString Key=TEXT("media/models_x/skinned/")+Sex+TEXT("body.x");auto Model=Models.FindRef(Key);auto* TargetMesh=Cast<USkeletalMesh>(Model.LoadSynchronous());if(!TargetMesh)return false;auto* Target=Rig(TEXT("IK_")+Sex,TargetMesh,true);if(!Target)return false;const FString Name=TEXT("RTG_Motion_")+Sex,Path=TEXT("/Game/HeyZed/Entities/Player/")+Name;auto* Asset=LoadObject<UIKRetargeter>(nullptr,*(Path+TEXT(".")+Name));if(!Asset)Asset=NewObject<UIKRetargeter>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);auto* C=UIKRetargeterController::GetController(Asset);C->SetIKRig(ERetargetSourceOrTarget::Source,Source);C->SetIKRig(ERetargetSourceOrTarget::Target,Target);C->SetPreviewMesh(ERetargetSourceOrTarget::Source,Motion->Mesh);C->SetPreviewMesh(ERetargetSourceOrTarget::Target,TargetMesh);C->RemoveAllOps();C->AddDefaultOps();C->AutoMapChains(EAutoMapChainType::Exact,true);C->ResetRetargetPose(C->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target),{},ERetargetSourceOrTarget::Target);C->SetRotationOffsetForRetargetPoseBone(TEXT("Dummy01"),FQuat(FVector::UpVector,PI),ERetargetSourceOrTarget::Target);C->AutoAlignBones({TEXT("Bip01_L_Clavicle"),TEXT("Bip01_L_UpperArm"),TEXT("Bip01_L_Forearm"),TEXT("Bip01_R_Clavicle"),TEXT("Bip01_R_UpperArm"),TEXT("Bip01_R_Forearm"),TEXT("Bip01_L_Thigh"),TEXT("Bip01_L_Calf"),TEXT("Bip01_R_Thigh"),TEXT("Bip01_R_Calf")},ERetargetAutoAlignMethod::ChainToChain,ERetargetSourceOrTarget::Target);if(!Save(Asset))return false;PlayerRetargeters.Add(Key,Asset);}
    MarkPackageDirty();return true;
#else
    return false;
#endif
}

bool UHeyZedEntityResources::RegisterNativeModels(const FString& Json)
{
#if WITH_EDITOR
    TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!Data)return false;
    for(auto& Pair:Data->Values){FString Path;if(!Pair.Value->TryGetString(Path)||!Path.StartsWith(TEXT("/Game/HeyZed/Entities/Meshes/")))return false;Models.Add(FString(Pair.Key),TSoftObjectPtr<UObject>(FSoftObjectPath(Path)));}
    MarkPackageDirty();return true;
#else
    return false;
#endif
}

UObject* UHeyZedEntityResources::PrepareNativeModel(const FString& Json,const FString& AssetPath)
{
#if WITH_EDITOR
    if(auto* Existing=LoadObject<UObject>(nullptr,*(AssetPath+TEXT(".")+FPackageName::GetShortName(AssetPath))))return Existing;
    TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!Data)return nullptr;
    const auto& Positions=Data->GetArrayField(TEXT("positions"));const auto& Normals=Data->GetArrayField(TEXT("normals"));const auto& UV=Data->GetArrayField(TEXT("uv"));const auto& Indices=Data->GetArrayField(TEXT("indices"));
    if(Positions.Num()%3 || UV.Num()!=Positions.Num()/3*2 || Normals.Num()!=Positions.Num() || Indices.Num()%3 || Indices.IsEmpty())return nullptr;
    auto* Package=CreatePackage(*AssetPath);UObject* Result=nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Bones=nullptr;
    if(Data->TryGetArrayField(TEXT("bones"),Bones)&&!Bones->IsEmpty())
    {
        const TArray<TSharedPtr<FJsonValue>>* UV2=nullptr;Data->TryGetArrayField(TEXT("uv2"),UV2);
        FSkeletalMeshImportData Import;Import.NumTexCoords=UV2?2:1;Import.bHasNormals=true;Import.bHasTangents=false;
        Import.Materials.AddDefaulted();Import.Materials[0].MaterialImportName=TEXT("NativeSurface");
        TArray<FMatrix> Bind;
        for(int32 I=0;I<Bones->Num();++I){auto Bone=(*Bones)[I]->AsObject();Bind.Add(NativeMatrix(Bone->GetArrayField(TEXT("inverse_bind"))).Inverse());}
        for(int32 I=0;I<Bones->Num();++I)
        {
            auto Source=(*Bones)[I]->AsObject();const int32 Parent=Source->GetIntegerField(TEXT("parent"));if(Parent>=I || Parent < -1)return nullptr;
            SkeletalMeshImportData::FBone Bone{};Bone.Name=Source->GetStringField(TEXT("name"));Bone.ParentIndex=Parent;
            Bone.BonePos.Transform=FTransform3f(FTransform(Parent<0?Bind[I]:Bind[I]*Bind[Parent].Inverse()));Import.RefBonesBinary.Add(Bone);
        }
        const auto& Joints=Data->GetArrayField(TEXT("joints"));const auto& Weights=Data->GetArrayField(TEXT("weights"));
        for(int32 I=0;I<Positions.Num()/3;++I)
        {
            Import.Points.Add(NativeVector(Positions,I*3,100));Import.PointToRawMap.Add(I);
            for(int32 J=0;J<4;++J)if(Weights[I*4+J]->AsNumber()>0){SkeletalMeshImportData::FRawBoneInfluence Influence;Influence.VertexIndex=I;Influence.BoneIndex=Joints[I*4+J]->AsNumber();Influence.Weight=Weights[I*4+J]->AsNumber();if(Influence.BoneIndex<0||Influence.BoneIndex>=Bones->Num())return nullptr;Import.Influences.Add(Influence);}
        }
        for(int32 I=0;I<Indices.Num();I+=3)
        {
            SkeletalMeshImportData::FTriangle Face{};Face.SmoothingGroups=1;
            for(int32 J=0;J<3;++J){int32 V=Indices[I+FacingCorner(Positions,Normals,Indices,I,J)]->AsNumber();if(!Import.Points.IsValidIndex(V))return nullptr;SkeletalMeshImportData::FVertex W;W.VertexIndex=V;W.Color=FColor::White;W.UVs[0]=FVector2f(UV[V*2]->AsNumber(),UV[V*2+1]->AsNumber());if(UV2)W.UVs[1]=FVector2f((*UV2)[V*2]->AsNumber(),(*UV2)[V*2+1]->AsNumber());Face.WedgeIndex[J]=Import.Wedges.Add(W);Face.TangentZ[J]=NativeVector(Normals,V*3,1).GetSafeNormal();}
            Import.Faces.Add(Face);
        }
        auto* Mesh=NewObject<USkeletalMesh>(Package,*FPackageName::GetShortName(AssetPath),RF_Public|RF_Standalone);
        Mesh->PreEditChange(nullptr);Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());Mesh->GetMaterials().Add(FSkeletalMaterial());
        int32 Depth=0;if(!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(nullptr,Mesh->GetRefSkeleton(),Depth,Import))return nullptr;
        SkeletalMeshImportUtils::ProcessImportMeshInfluences(Import,AssetPath);
        auto& Info=Mesh->AddLODInfo();
        Info.ReductionSettings.TerminationCriterion=SMTC_NumOfTriangles;
        Info.ReductionSettings.NumOfTrianglesPercentage=1.f;Info.ReductionSettings.NumOfVertPercentage=1.f;
        Info.ReductionSettings.MaxNumOfTriangles=MAX_uint32;Info.ReductionSettings.MaxNumOfVerts=MAX_uint32;
        Info.ReductionSettings.MaxNumOfTrianglesPercentage=MAX_uint32;Info.ReductionSettings.MaxNumOfVertsPercentage=MAX_uint32;
        Info.ReductionSettings.MaxDeviationPercentage=0.f;Info.bHasBeenSimplified=false;
        Info.BuildSettings.bRecomputeNormals=false;Info.BuildSettings.bRecomputeTangents=true;Info.BuildSettings.bUseMikkTSpace=true;
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        Mesh->SaveLODImportedData(0,Import);
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
        TArray<FVector3f> P;TArray<SkeletalMeshImportData::FMeshWedge> W;TArray<SkeletalMeshImportData::FMeshFace> F;TArray<SkeletalMeshImportData::FVertInfluence> Inf;TArray<int32> Map;
        Import.CopyLODImportData(P,W,F,Inf,Map);
        IMeshUtilities::MeshBuildOptions Options;Options.bComputeNormals=false;Options.bComputeTangents=true;Options.bUseMikkTSpace=true;
        auto& LOD=Mesh->GetImportedModel()->LODModels[0];LOD.NumTexCoords=Import.NumTexCoords;
        if(!FModuleManager::LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities")).BuildSkeletalMesh(LOD,AssetPath,Mesh->GetRefSkeleton(),Inf,W,F,P,Map,Options))return nullptr;
        Mesh->GetImportedModel()->InlineReductionCacheDatas.AddDefaulted();Mesh->GetImportedModel()->InlineReductionCacheDatas[0].SetCacheGeometryInfo(LOD);
        FBox Bounds(ForceInit);for(auto Point:P)Bounds+=FVector(Point);Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));Mesh->CalculateInvRefMatrices();
        auto* Skeleton=NewObject<USkeleton>(Package,*(FPackageName::GetShortName(AssetPath)+TEXT("_Skeleton")),RF_Public|RF_Standalone);Skeleton->MergeAllBonesToBoneTree(Mesh);Mesh->SetSkeleton(Skeleton);FAssetRegistryModule::AssetCreated(Skeleton);
        Mesh->PostEditChange();
        const TArray<TSharedPtr<FJsonValue>>* Ratios=nullptr;
        if(Data->TryGetArrayField(TEXT("lod_ratios"),Ratios)&&!Ratios->IsEmpty()){
            for(int32 Level=0;Level<Ratios->Num();++Level){auto& Reduced=Mesh->AddLODInfo();Reduced.ReductionSettings.BaseLOD=0;Reduced.ReductionSettings.TerminationCriterion=SMTC_NumOfTriangles;Reduced.ReductionSettings.NumOfTrianglesPercentage=FMath::Clamp(float((*Ratios)[Level]->AsNumber()),.05f,1.f);Reduced.ScreenSize.Default=Level==0?.04f:.012f;}
            if(!FLODUtilities::RegenerateLOD(Mesh,GetTargetPlatformManagerRef().GetRunningTargetPlatform(),Ratios->Num()+1,true,false))return nullptr;
        }
        Result=Mesh;
    }
    else
    {
        auto* Mesh=NewObject<UStaticMesh>(Package,*FPackageName::GetShortName(AssetPath),RF_Public|RF_Standalone);
        const TArray<TSharedPtr<FJsonValue>>* UV2=nullptr;Data->TryGetArrayField(TEXT("uv2"),UV2);
        FMeshDescription Description;FStaticMeshAttributes Attributes(Description);Attributes.Register();auto Pos=Attributes.GetVertexPositions();auto Normal=Attributes.GetVertexInstanceNormals();auto Tex=Attributes.GetVertexInstanceUVs();Tex.SetNumChannels(UV2?2:1);const auto Group=Description.CreatePolygonGroup();
        TArray<FVertexID> Vertices;for(int32 I=0;I<Positions.Num();I+=3){auto V=Description.CreateVertex();Pos[V]=NativeVector(Positions,I,100);Vertices.Add(V);}
        for(int32 I=0;I<Indices.Num();I+=3){TArray<FVertexInstanceID> Corners;for(int32 J=0;J<3;++J){int32 V=Indices[I+FacingCorner(Positions,Normals,Indices,I,J)]->AsNumber();if(!Vertices.IsValidIndex(V))return nullptr;auto C=Description.CreateVertexInstance(Vertices[V]);Normal[C]=NativeVector(Normals,V*3,1).GetSafeNormal();Tex.Set(C,0,FVector2f(UV[V*2]->AsNumber(),UV[V*2+1]->AsNumber()));if(UV2)Tex.Set(C,1,FVector2f((*UV2)[V*2]->AsNumber(),(*UV2)[V*2+1]->AsNumber()));Corners.Add(C);}Description.CreateTriangle(Group,Corners);}
        Mesh->GetStaticMaterials().Add(FStaticMaterial());Mesh->SetNumSourceModels(1);Mesh->GetSourceModel(0).BuildSettings.bRecomputeNormals=false;Mesh->GetSourceModel(0).BuildSettings.bRecomputeTangents=true;Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs=false;
        UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bFastBuild=false;Params.bUseHashAsGuid=true;if(!Mesh->BuildFromMeshDescriptions({&Description},Params))return nullptr;FinishHeyZedMesh(Mesh);Result=Mesh;
    }
    Result->MarkPackageDirty();FAssetRegistryModule::AssetCreated(Result);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString Filename=FPackageName::LongPackageNameToFilename(AssetPath,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    return UPackage::SavePackage(Package,Result,*Filename,Args)?Result:nullptr;
#else
    return nullptr;
#endif
}
