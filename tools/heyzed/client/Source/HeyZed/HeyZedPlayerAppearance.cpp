#include "HeyZedEntities.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HeyZedPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HeyZedMetrics.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

namespace {
TArray<TSharedPtr<FJsonValue>> AuditMatrix(const FMatrix& Matrix)
{
    TArray<TSharedPtr<FJsonValue>> Values;for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column)Values.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Column]));return Values;
}
void AuditPlayerAppearance(const FHeyZedEntity& Entity,const AHeyZedPawn& Pawn)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("HeyZedPlayerAppearanceAudit")))return;
    static int32 Samples=0;static double Next=0;const double Now=FPlatformTime::Seconds();if(Samples>=3||Now<Next)return;Next=Now+2;
    auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("id"),Entity.Kind);Data->SetNumberField(TEXT("frame"),GFrameCounter);Data->SetArrayField(TEXT("root"),AuditMatrix(Entity.Root->GetComponentTransform().ToMatrixWithScale()));
    TArray<TSharedPtr<FJsonValue>> Parts;
    for(const auto& Part:Entity.Parts)if(auto* Mesh=Cast<UPoseableMeshComponent>(Part.Mesh))
    {
        const auto* Asset=Mesh->GetSkinnedAsset();const auto& Ref=Asset->GetRefSkeleton();const auto& Current=Mesh->GetComponentSpaceTransforms();const auto& Inverse=Asset->GetRefBasesInvMatrix();auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("key"),Part.Key);Item->SetStringField(TEXT("asset"),Asset->GetPathName());Item->SetStringField(TEXT("texture"),Part.Texture);Item->SetArrayField(TEXT("component"),AuditMatrix(Mesh->GetComponentTransform().ToMatrixWithScale()));
        if(Part.Material){UTexture* Texture=nullptr;Part.Material->GetTextureParameterValue(TEXT("NativeDiffuse"),Texture);Item->SetStringField(TEXT("material"),Part.Material->GetPathName());Item->SetStringField(TEXT("material_parent"),Part.Material->Parent?Part.Material->Parent->GetPathName():TEXT(""));Item->SetStringField(TEXT("bound_texture"),Texture?Texture->GetPathName():TEXT(""));}
        TArray<TSharedPtr<FJsonValue>> Bones;
        for(int32 Index=0;Index<Ref.GetNum();++Index){auto Bone=MakeShared<FJsonObject>();Bone->SetStringField(TEXT("name"),Ref.GetBoneName(Index).ToString());Bone->SetNumberField(TEXT("parent"),Ref.GetParentIndex(Index));Bone->SetArrayField(TEXT("ref_local"),AuditMatrix(Ref.GetRefBonePose()[Index].ToMatrixWithScale()));if(Current.IsValidIndex(Index))Bone->SetArrayField(TEXT("posed_global"),AuditMatrix(Current[Index].ToMatrixWithScale()));if(Inverse.IsValidIndex(Index))Bone->SetArrayField(TEXT("inverse_bind"),AuditMatrix(FMatrix(Inverse[Index])));Bones.Add(MakeShared<FJsonValueObject>(Bone));}
        Item->SetArrayField(TEXT("bones"),Bones);
        if(Samples==0)if(const auto* Render=Asset->GetResourceForRendering();Render&&!Render->LODRenderData.IsEmpty())
        {
            const auto& LOD=Render->LODRenderData[0];const auto* Weights=LOD.GetSkinWeightVertexBuffer();
            if(LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()&&LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordData()&&Weights&&Weights->GetDataVertexBuffer()->GetWeightData())
            {
                TArray<TSharedPtr<FJsonValue>> Vertices;
                for(const auto& Section:LOD.RenderSections)for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
                {
                    const auto P=LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex);const auto UV=LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Vertex,0);auto Value=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Position,Texcoord,Influences;
                    for(float Number:{P.X,P.Y,P.Z})Position.Add(MakeShared<FJsonValueNumber>(Number));for(float Number:{UV.X,UV.Y})Texcoord.Add(MakeShared<FJsonValueNumber>(Number));
                    for(uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)if(const auto Weight=Weights->GetBoneWeight(Vertex,Influence)){const uint32 Bone=Weights->GetBoneIndex(Vertex,Influence);auto Entry=MakeShared<FJsonObject>();Entry->SetStringField(TEXT("bone"),Section.BoneMap.IsValidIndex(Bone)?Ref.GetBoneName(Section.BoneMap[Bone]).ToString():TEXT("INVALID"));Entry->SetNumberField(TEXT("weight"),Weight/65535.);Influences.Add(MakeShared<FJsonValueObject>(Entry));}
                    Value->SetNumberField(TEXT("index"),Vertex);Value->SetArrayField(TEXT("position"),Position);Value->SetArrayField(TEXT("uv"),Texcoord);Value->SetArrayField(TEXT("influences"),Influences);Vertices.Add(MakeShared<FJsonValueObject>(Value));
                }
                Item->SetArrayField(TEXT("vertices"),Vertices);
            }
        }
        Parts.Add(MakeShared<FJsonValueObject>(Item));
    }
    Data->SetArrayField(TEXT("parts"),Parts);FString Text;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Text));const FString Folder=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed"));IFileManager::Get().MakeDirectory(*Folder,true);FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Folder,FString::Printf(TEXT("player-appearance-%d.json"),Samples++)));
}
}

bool AHeyZedEntities::PosePlayer(FHeyZedEntity& Entity,float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_PlayerRetarget);
    auto* Pawn=Cast<AHeyZedPawn>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!Pawn)return false;
    SetOwner(Pawn);AddTickPrerequisiteActor(Pawn);
    bool AttachedSeat=false;
    if(Entity.Seated&&Entity.Root)if(auto* Vehicle=Entities.Find(Entity.Vehicle);Vehicle&&!Vehicle->Parts.IsEmpty()&&Vehicle->Parts[0].Mesh){
        auto* Anchor=Vehicle->Parts[0].Mesh.Get();
        const bool Bracketed=!Vehicle->RootSamples.IsEmpty()&&Vehicle->RootSamples[0].Time<=Entity.NativeSample&&Vehicle->RootSamples.Last().Time>=Entity.NativeSample;
        if(Bracketed){
            FTransform NativeRoot;TArray<FTransform> NativeLocals;HeyZedSampleEntityRoot(Vehicle->RootSamples,Entity.NativeSample,NativeRoot,NativeLocals);
            if(!NativeLocals.IsEmpty()){
                const FTransform NativeAnchor=NativeLocals[0]*NativeRoot;
                if(Entity.Root->GetAttachParent()!=Anchor)Entity.Root->AttachToComponent(Anchor,FAttachmentTransformRules::KeepWorldTransform);
                Entity.Root->SetRelativeTransform(Entity.Target.GetRelativeTransform(NativeAnchor));
            }
        }
        if(Entity.Root->GetAttachParent()==Anchor){
            if(!Pawn->bNativeSeated){const FVector Eye=Pawn->GetPawnViewLocation();Pawn->NativeSeatTransform=Entity.Root->GetComponentTransform();Pawn->NativeSeatEyeLocation=Eye;}
            if(Pawn->Arm->GetAttachParent()!=Entity.Root)Pawn->Arm->AttachToComponent(Entity.Root,FAttachmentTransformRules::KeepRelativeTransform);
            Pawn->Arm->AddTickPrerequisiteActor(this);Pawn->bNativeSeated=true;AttachedSeat=true;
        }
    }
    if(!AttachedSeat){
        if(Entity.Root&&Entity.Root->GetAttachParent()!=RootComponent)Entity.Root->AttachToComponent(RootComponent,FAttachmentTransformRules::KeepWorldTransform);
        if(Pawn->Arm->GetAttachParent()!=Pawn->GetRootComponent())Pawn->Arm->AttachToComponent(Pawn->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
        Pawn->Arm->RemoveTickPrerequisiteActor(this);Pawn->bNativeSeated=false;
    }
    if(Entity.Parts.IsEmpty())return false;
    auto* Target=Cast<UPoseableMeshComponent>(Entity.Parts[0].Mesh);
    if(!Target)return false;
    auto* TargetMesh=Cast<USkeletalMesh>(Target->GetSkinnedAsset());
    if(!TargetMesh)return false;
    const auto& NativeRef=TargetMesh->GetRefSkeleton();
    TArray<FTransform> Evaluated,NativePose;
    if(!EvaluateEntityPose(Entity,Evaluated,Delta))return false;
    NativePose.SetNum(NativeRef.GetNum());
    for(int32 Bone=0;Bone<NativeRef.GetNum();++Bone){const int32* Index=Entity.BoneIndices.Find(NativeRef.GetBoneName(Bone));if(!Index||!Evaluated.IsValidIndex(*Index))return false;NativePose[Bone]=Evaluated[*Index];}
    const TArray<FTransform>* Output=&NativePose;
    if(!AttachedSeat){const FQuat Rotation=Pawn->CameraMode==0?FRotator(0,Pawn->MovementYaw()+90.f,0).Quaternion():Entity.PresentationRoot.GetRotation();Entity.Root->SetWorldTransform(FTransform(Rotation,Pawn->GetActorLocation(),Entity.Target.GetScale3D()));}
    if(Output->Num()!=NativeRef.GetNum())return false;
    TArray<FTransform> Pose=*Output;
    TArray<FTransform> Bind;Bind.SetNum(NativeRef.GetNum());
    for(int32 Bone=0;Bone<Bind.Num();++Bone){const int32 Parent=NativeRef.GetParentIndex(Bone);Bind[Bone]=Parent<0?NativeRef.GetRefBonePose()[Bone]:NativeRef.GetRefBonePose()[Bone]*Bind[Parent];}
    const int32 NativeHead=NativeRef.FindBoneIndex(TEXT("Bip01_Head"));
    Pawn->bNativeAppearance=true;
    if(AttachedSeat){Pawn->NativeSeatTransform=Entity.Root->GetComponentTransform();const int32 Head=NativeRef.FindBoneIndex(TEXT("Bip01_Head"));Pawn->NativeSeatEyeLocation=Head!=INDEX_NONE?Pawn->NativeSeatTransform.TransformPosition(Pose[Head].GetLocation()):Entity.Root->GetComponentLocation();}
    static const bool DiagnoseVehicle=FParse::Param(FCommandLine::Get(),TEXT("HeyZedVehicleProbe"));
    if(Entity.Seated&&DiagnoseVehicle){
        const auto* Vehicle=Entities.Find(Entity.Vehicle);
        UE_LOG(LogTemp,Display,TEXT("heyzed vehicle movement t=%.6f source=%.6f time=%.6f target=%.4f,%.4f visible=%.4f,%.4f vehicle=%.4f,%.4f seat=%d"),
            FPlatformTime::Seconds(),Entity.NativeSample,Entity.PresentationSample,Entity.Target.GetLocation().X,Entity.Target.GetLocation().Y,
            Entity.PresentationRoot.GetLocation().X,Entity.PresentationRoot.GetLocation().Y,Vehicle?Vehicle->PresentationRoot.GetLocation().X:0.,Vehicle?Vehicle->PresentationRoot.GetLocation().Y:0.,Entity.Seat);
    }
    const bool FirstPerson=Pawn->CameraMode==0;
    FVector ViewOffset=FVector::ZeroVector;
    if(FirstPerson&&!Entity.Seated&&NativeHead!=INDEX_NONE)
    {
        const FTransform Root=Entity.Root->GetComponentTransform();
        const FVector ActualHead=Root.TransformPosition(Pose[NativeHead].GetLocation());
        const float ReferenceHeadHeight=Root.TransformVector(Bind[NativeHead].GetLocation()).Z;
        const float EyeAboveHead=Pawn->StandingEyeHeight-ReferenceHeadHeight;
        const FVector Forward=FRotator(0,Pawn->MovementYaw(),0).Vector();
        const FVector DesiredHead=Pawn->GetPawnViewLocation()-Forward*Pawn->EyeForwardOffset-FVector(0,0,EyeAboveHead);
        ViewOffset=DesiredHead-ActualHead;
    }
    for(auto& Part:Entity.Parts)
    {
        if(!Part.Mesh)continue;
        Part.Mesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
        Part.Mesh->SetOwnerNoSee(FirstPerson);Part.Mesh->SetCastShadow(true);Part.Mesh->SetCastHiddenShadow(true);
        if(Part.Rigid){FTransform Local=Part.Local;if(const int32* Bone=Entity.BoneIndices.Find(FName(*Part.AttachmentBone));Bone&&Evaluated.IsValidIndex(*Bone))Local=Part.AttachmentOffset*Evaluated[*Bone];Part.Mesh->SetRelativeTransform(Local);if(Part.Glass)Part.Glass->SetRelativeTransform(Local);continue;}
        auto* Mesh=Cast<UPoseableMeshComponent>(Part.Mesh);
        if(!Mesh)continue;
        if(!Part.ViewMesh&&FirstPerson)
        {
            auto* View=NewObject<UPoseableMeshComponent>(this);View->SetSkinnedAssetAndUpdate(Mesh->GetSkinnedAsset());View->SetupAttachment(Entity.Root);View->SetCollisionEnabled(ECollisionEnabled::NoCollision);View->SetOnlyOwnerSee(true);View->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);View->SetCastShadow(false);View->RegisterComponent();View->PrimaryComponentTick.SetTickFunctionEnable(false);Part.ViewMesh=View;
        }
        if(Part.ViewMesh){Part.ViewMesh->SetMaterial(0,Part.Material);Part.ViewMesh->SetHiddenInGame(!FirstPerson);}
        for(auto* Component:{Mesh,Cast<UPoseableMeshComponent>(Part.ViewMesh)})
        {
            if(!Component||(Component==Part.ViewMesh&&!FirstPerson))continue;Component->SetRelativeTransform(Part.Local);
            const auto& Ref=Component->GetSkinnedAsset()->GetRefSkeleton();TArray<FTransform> Global;Global.SetNum(Ref.GetNum());
            if(Component->BoneSpaceTransforms.Num()!=Ref.GetNum())continue;
            for(int32 Bone=0;Bone<Ref.GetNum();++Bone){const int32 Parent=Ref.GetParentIndex(Bone),Native=NativeRef.FindBoneIndex(Ref.GetBoneName(Bone));Global[Bone]=Native!=INDEX_NONE?Pose[Native]:Parent<0?Ref.GetRefBonePose()[Bone]:Ref.GetRefBonePose()[Bone]*Global[Parent];Component->BoneSpaceTransforms[Bone]=Parent<0?Global[Bone]:Global[Bone].GetRelativeTransform(Global[Parent]);}
            if(Component==Part.ViewMesh){const int32 Head=Ref.FindBoneIndex(TEXT("Bip01_Head"));if(Head!=INDEX_NONE)Component->BoneSpaceTransforms[Head].SetScale3D(FVector::ZeroVector);}
            if(Component==Part.ViewMesh&&FirstPerson&&!Entity.Seated){FTransform World=Component->GetComponentTransform();World.AddToTranslation(ViewOffset);Component->SetWorldTransform(World);}
            Component->MarkRefreshTransformDirty();Component->RefreshBoneTransforms();
        }
    }
    Entity.PoseInitialized=true;
    AuditPlayerAppearance(Entity,*Pawn);
    return true;
}
