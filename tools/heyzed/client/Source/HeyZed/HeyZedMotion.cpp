#include "HeyZedMotion.h"
#include "HeyZedPawn.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSequence.h"
#include "Engine/AssetUserData.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "AnimGraphNode_Root.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "Retargeter/IKRetargeter.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/SavePackage.h"
#endif

struct FHeyZedMotionProxy : FAnimInstanceProxy
{
    FAnimNode_MotionMatching* Matching = nullptr;
    FAnimNode_PoseSearchHistoryCollector* History = nullptr;
    struct FPoint { double Time; FTransform Transform; };
    TArray<FPoint> Past;
    FName Group;
    float Speed = 0;
    float VisualScale = 1;
    FVector Velocity = FVector::ZeroVector;
    bool bMoving = false;
    int32 LastGait = 1;
    bool bWasFirstPerson = false;
    mutable double NextDiagnostic = 0;

    explicit FHeyZedMotionProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        if (const auto* Interface=IAnimClassInterface::GetFromClass(Instance->GetClass()))
            for (const auto* Property:Interface->GetAnimNodeProperties())
            {
                if(Property->Struct==FAnimNode_MotionMatching::StaticStruct())Matching=Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Instance);
                if(Property->Struct==FAnimNode_PoseSearchHistoryCollector::StaticStruct())History=Property->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(Instance);
            }
    }

    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);
        checkf(Matching && History,TEXT("heyzed: the compiled ABP_HeyZedMotion AnimGraph is required"));
        auto* Animation = CastChecked<UHeyZedMotionInstance>(Instance);
        auto* Pawn = Cast<AHeyZedPawn>(Instance->TryGetPawnOwner());
        if (!Pawn || !Animation->Library) return;
        const float Blend = 1.f-FMath::Exp(-12.f*FMath::Min(Delta,.1f));
        Velocity = FMath::Lerp(Velocity,Pawn->PoseVelocity,Blend);
        Speed = Velocity.Size2D();
        bMoving = Speed > (bMoving ? 5.f : 18.f);
        const bool Moving = bMoving;
        if (Pawn->Gait > 0) LastGait = Pawn->Gait;
        const bool FirstPerson=Pawn->CameraMode==0;
        if (FirstPerson!=bWasFirstPerson) {Past.Reset();bWasFirstPerson=FirstPerson;}
        int32 VisualGait=LastGait;
        if (FirstPerson && Moving)
        {
            const FVector Facing=FRotator(0,Pawn->MovementYaw(),0).Vector();
            const float Forward=FVector::DotProduct(Velocity,Facing);
            const float Side=FVector::CrossProduct(Facing,Velocity).Z;
            if (Forward < -1.f) VisualGait=1;
            else if (FMath::Abs(Side)>Forward+1.f) VisualGait=FMath::Min(VisualGait,2);
        }
        const bool Turning = FMath::Abs(Pawn->NativeTurnRate) > 8;
        Group = Pawn->bCrouched ? (Moving ? (FirstPerson?TEXT("FPCrouch"):TEXT("Crouch")) : Turning && !FirstPerson ? TEXT("CrouchTurn") : TEXT("CrouchIdle"))
            : !Moving ? (Turning && !FirstPerson ? TEXT("Turn") : TEXT("Idle"))
            : FirstPerson ? (VisualGait==3?TEXT("FPSprint"):VisualGait==2?TEXT("FPRun"):TEXT("FPWalk"))
            : LastGait == 3 ? TEXT("Sprint") : LastGait == 2 ? TEXT("Run") : TEXT("Walk");
        if (auto* Database = Animation->Library->Databases.Find(Group))
            Matching->SetDatabaseToSearch(Database->Get(),EPoseSearchInterruptMode::InterruptOnDatabaseChange);
        const FTransform Transform = Pawn->Body->GetComponentTransform();
        VisualScale=FMath::Max(.01f,float(FMath::Abs(Transform.GetScale3D().Z)));
        const double Now = FPlatformTime::Seconds();
        if (!Past.IsEmpty() && FVector::DistSquared(Past.Last().Transform.GetLocation(),Transform.GetLocation()) > FMath::Square(400.)) Past.Reset();
        Past.Add({Now,Transform});
        Past.RemoveAll([Now](const FPoint& P){ return P.Time < Now-.8; });
        History->TransformTrajectory.Samples.Reset();
        for (float Time : {-.6f,-.4f,-.2f,0.f,.2f,.4f,.6f,.8f,1.f})
        {
            FTransformTrajectorySample Sample; Sample.TimeInSeconds = Time;
            const float Prediction = Time > 0 && Pawn->Gait == 0 ? (1.f-FMath::Exp(-8.f*Time))/8.f : Time;
            Sample.Position = Transform.GetLocation()+Velocity*Prediction;
            Sample.Facing = Transform.GetRotation();
            if (Time < 0 && !Past.IsEmpty())
            {
                const double Wanted = Now+Time;
                const FPoint* Before = &Past[0]; const FPoint* After = Before;
                for (const auto& P : Past) { After = &P; if (P.Time >= Wanted) break; Before = &P; }
                const float Alpha = After->Time > Before->Time ? FMath::Clamp((Wanted-Before->Time)/(After->Time-Before->Time),0.,1.) : 0;
                Sample.Position = FMath::Lerp(Before->Transform.GetLocation(),After->Transform.GetLocation(),Alpha);
                Sample.Facing = FQuat::Slerp(Before->Transform.GetRotation(),After->Transform.GetRotation(),Alpha);
            }
            if (Time > 0)
            {
                const float CurrentFacing = Transform.Rotator().Yaw+90;
                const float FutureFacing = FirstPerson ? Pawn->MovementYaw() : FMath::FixedTurn(CurrentFacing,Pawn->DesiredFacingYaw,FMath::Min(FMath::Abs(Pawn->NativeTurnRate),540.f)*Time);
                Sample.Facing = FQuat(FVector::UpVector,FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(CurrentFacing,FutureFacing)))*Transform.GetRotation();
            }
            // FTransformTrajectory deliberately discards component scale. Query the
            // original animation's units, retaining the actual world-space anchor.
            Sample.Position=Transform.GetLocation()+(Sample.Position-Transform.GetLocation())/VisualScale;
            History->TransformTrajectory.Samples.Add(Sample);
        }
        Animation->SelectedGroup = Group;
    }
    virtual void PostUpdate(UAnimInstance* Instance) const override
    {
        FAnimInstanceProxy::PostUpdate(Instance);
        auto* Animation = CastChecked<UHeyZedMotionInstance>(Instance);
        const auto& Result = Matching->GetMotionMatchingState().SearchResult;
        Animation->bMatchingValid = Result.SelectedAnim != nullptr;
        const auto* Asset = Result.SelectedAnim.Get();
        Animation->SelectedClip = Asset ? Asset->GetName() : TEXT("None");
        const float RootSpeed = Matching->GetEstimatedFutureRootMotionVelocity().Size2D();
        const float DisplayedRootSpeed=RootSpeed*VisualScale*Result.WantedPlayRate;
        Animation->StrideRatio = DisplayedRootSpeed > 10 ? Speed/DisplayedRootSpeed : 1;
        Animation->PlaybackRate = Result.WantedPlayRate;
        if (FParse::Param(FCommandLine::Get(),TEXT("HeyZedLocomotionDiagnostics")) && FPlatformTime::Seconds() >= NextDiagnostic)
        {
            NextDiagnostic=FPlatformTime::Seconds()+.05;
            if (const auto* Pawn=Cast<AHeyZedPawn>(Instance->TryGetPawnOwner()))
                UE_LOG(LogTemp,Display,TEXT("heyzed locomotion frame=%llu native=%.3f visible=%.3f query=%.3f gait=%d group=%s clip=%s time=%.4f continue=%d loop=%d rate=%.3f x=%.3f y=%.3f"),
                    GFrameCounter,Pawn->NativeVelocity.Size2D(),Pawn->PoseVelocity.Size2D(),Speed,Pawn->Gait,*Group.ToString(),*Animation->SelectedClip,
                    Result.SelectedTime,Result.bIsContinuingPoseSearch,Result.bLoop,Result.WantedPlayRate,Pawn->GetActorLocation().X,Pawn->GetActorLocation().Y);
        }

    }
};

UHeyZedMotionInstance::UHeyZedMotionInstance() { SetRootMotionMode(ERootMotionMode::IgnoreRootMotion); }
void UHeyZedMotionInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    Library = LoadObject<UHeyZedMotionLibrary>(nullptr,TEXT("/Game/HeyZed/Animation/Library.Library"));
}
FAnimInstanceProxy* UHeyZedMotionInstance::CreateAnimInstanceProxy() { return new FHeyZedMotionProxy(this); }
void UHeyZedMotionInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete static_cast<FHeyZedMotionProxy*>(Proxy); }

bool UHeyZedMotionAuthoring::PrepareGraph()
{
#if WITH_EDITOR
    auto* Library=LoadObject<UHeyZedMotionLibrary>(nullptr,TEXT("/Game/HeyZed/Animation/Library.Library"));
    if(!Library || !Library->Mesh)return false;
    const FString PackageName=TEXT("/Game/HeyZed/Animation/ABP_HeyZedMotion");
    auto* Blueprint=LoadObject<UAnimBlueprint>(nullptr,*(PackageName+TEXT(".ABP_HeyZedMotion")),nullptr,LOAD_NoWarn|LOAD_Quiet);
    if(!Blueprint)
    {
        auto* Factory=NewObject<UAnimBlueprintFactory>();Factory->ParentClass=UHeyZedMotionInstance::StaticClass();
        Factory->TargetSkeleton=Library->Mesh->GetSkeleton();Factory->PreviewSkeletalMesh=Library->Mesh;
        Blueprint=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*PackageName),TEXT("ABP_HeyZedMotion"),RF_Public|RF_Standalone,nullptr,GWarn));
        if(!Blueprint)return false;
        FAssetRegistryModule::AssetCreated(Blueprint);
    }
    UEdGraph* Graph=nullptr;
    for(UEdGraph* Candidate:Blueprint->FunctionGraphs)if(Candidate->GetFName()==TEXT("AnimGraph")){Graph=Candidate;break;}
    if(!Graph)return false;
    UAnimGraphNode_Root* Root=nullptr;
    for(int32 I=Graph->Nodes.Num()-1;I>=0;--I)
        if(auto* Candidate=Cast<UAnimGraphNode_Root>(Graph->Nodes[I]))Root=Candidate;
        else Graph->RemoveNode(Graph->Nodes[I]);
    if(!Root)return false;
    FGraphNodeCreator<UAnimGraphNode_MotionMatching> MatchingCreator(*Graph);
    auto* Matching=MatchingCreator.CreateNode();Matching->NodePosX=-600;
    auto* MatchingProperty=FindFProperty<FStructProperty>(Matching->GetClass(),TEXT("Node"));
    auto* MatchingData=MatchingProperty->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Matching);
    auto* Rate=FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("PlayRate"));
    auto* Blend=FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(),TEXT("BlendTime"));
    auto* Database=FindFProperty<FObjectPropertyBase>(FAnimNode_MotionMatching::StaticStruct(),TEXT("Database"));
    if(!Rate || !Blend || !Database)return false;
    *Rate->ContainerPtrToValuePtr<FFloatInterval>(MatchingData)=FFloatInterval(.8f,1.2f);
    Blend->SetPropertyValue_InContainer(MatchingData,.25f);
    Database->SetObjectPropertyValue_InContainer(MatchingData,Library->Databases.FindRef(TEXT("Idle")));
    MatchingCreator.Finalize();
    FGraphNodeCreator<UAnimGraphNode_PoseSearchHistoryCollector> HistoryCreator(*Graph);
    auto* History=HistoryCreator.CreateNode();History->NodePosX=-300;
    auto* HistoryProperty=FindFProperty<FStructProperty>(History->GetClass(),TEXT("Node"));
    auto* HistoryData=HistoryProperty->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(History);
    HistoryData->PoseCount=32;HistoryData->SamplingInterval=1.f/30;
    HistoryCreator.Finalize();
    const auto* Schema=Graph->GetSchema();
    auto* MatchingOutput=Matching->FindPin(TEXT("Pose"),EGPD_Output);
    auto* HistoryInput=History->FindPin(TEXT("Source"),EGPD_Input);
    auto* HistoryOutput=History->FindPin(TEXT("Pose"),EGPD_Output);
    auto* RootInput=Root->FindPin(TEXT("Result"),EGPD_Input);
    if(!MatchingOutput || !HistoryInput || !HistoryOutput || !RootInput)return false;
    if(!Schema->TryCreateConnection(MatchingOutput,HistoryInput) || !Schema->TryCreateConnection(HistoryOutput,RootInput))return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)return false;
    Blueprint->MarkPackageDirty();
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args);
#else
    return false;
#endif
}

FString UHeyZedMotionAuthoring::BuildLibrary(const FString& ManifestPath)
{
#if WITH_EDITOR
    FString Text; TSharedPtr<FJsonObject> Manifest;
    if (!FFileHelper::LoadFileToString(Text,*ManifestPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Manifest)) return TEXT("Missing sample manifest");
    auto Save = [](UObject* Asset)
    {
        UPackage* Package = Asset->GetOutermost(); Package->MarkPackageDirty();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public|RF_Standalone; Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Package,Asset,*Filename,Args);
    };
    auto Create = [](UClass* Class,const FString& Name) -> UObject*
    {
        const FString PackageName = TEXT("/Game/HeyZed/Animation/")+Name;
        if (UObject* Loaded = StaticLoadObject(Class,nullptr,*(PackageName+TEXT(".")+Name))) return Loaded;
        UPackage* Package = CreatePackage(*PackageName);
        if (UObject* Existing = StaticFindObject(Class,Package,*Name)) return Existing;
        UObject* Asset = NewObject<UObject>(Package,Class,*Name,RF_Public|RF_Standalone);
        FAssetRegistryModule::AssetCreated(Asset); return Asset;
    };
    auto* Library = CastChecked<UHeyZedMotionLibrary>(Create(UHeyZedMotionLibrary::StaticClass(),TEXT("Library")));
    auto* SourceMesh = LoadObject<USkeletalMesh>(nullptr,*Manifest->GetStringField(TEXT("mesh")));
    Library->Mesh = LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin"));
    auto* Retargeter = LoadObject<UIKRetargeter>(nullptr,TEXT("/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin.RTG_UEFN_to_UE4_Mannequin"));
    if (!SourceMesh || !Retargeter) return TEXT("Missing Epic retarget resources");
    FIKRetargetBatchOperationInputs Retarget;
    Retarget.SourceMesh=SourceMesh; Retarget.TargetMesh=Library->Mesh; Retarget.IKRetargetAsset=Retargeter;
    Retarget.TargetPath=TEXT("/Game/HeyZed/Animation/UE4"); Retarget.bIncludeReferencedAssets=false;
    Retarget.bOverwriteExistingFiles=true;
    TSet<FString> Sources;
    for (const auto& Group : Manifest->GetObjectField(TEXT("groups"))->Values)
        for (const auto& Path : Group.Value->AsArray()) Sources.Add(Path->AsString());
    for (const FString& Path : Sources)
    {
        if (Path.Contains(TEXT("Transition_"))) continue;
        const FString Destination=Retarget.TargetPath+TEXT("/")+FPackageName::GetShortName(Path);
        if (!FPackageName::DoesPackageExist(Destination))
            if (auto* Sequence=LoadObject<UAnimSequence>(nullptr,*Path)) Retarget.AssetsToRetarget.Add(FAssetData(Sequence));
    }
    if (!Retarget.AssetsToRetarget.IsEmpty()) UIKRetargetBatchOperation::RunBatchRetarget(Retarget);
    if (!Library->Mesh) return TEXT("Missing Epic mannequin");
    Library->Mesh->SetPostProcessAnimBlueprint(nullptr);
    Save(Library->Mesh);
    auto* Schema = CastChecked<UPoseSearchSchema>(Create(UPoseSearchSchema::StaticClass(),TEXT("Schema_UE4")));
    if (Schema->GetRoledSkeletons().IsEmpty()) { Schema->AddSkeleton(Library->Mesh->GetSkeleton()); Schema->AddDefaultChannels(); }
    if (!Save(Schema)) return TEXT("Cannot save pose schema");
    for (const auto& Group : Manifest->GetObjectField(TEXT("groups"))->Values)
    {
        const FString GroupName(*Group.Key);
        auto* Database = CastChecked<UPoseSearchDatabase>(Create(UPoseSearchDatabase::StaticClass(),TEXT("DB_")+GroupName));
        Database->Schema = Schema;
        while (Database->GetNumAnimationAssets()) Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets()-1);
        for (const auto& Path : Group.Value->AsArray())
        {
            const FString Original=Path->AsString();
            if (Original.Contains(TEXT("Transition_"))) continue;
            if ((GroupName==TEXT("Idle") || GroupName==TEXT("CrouchIdle")) && !Original.Contains(TEXT("Idle_Loop"))) continue;
            auto* Sequence = LoadObject<UAnimSequence>(nullptr,*(Retarget.TargetPath+TEXT("/")+FPackageName::GetShortName(Original)));
            if (!Sequence) return TEXT("Missing clip ")+Path->AsString();
            Sequence->bEnableRootMotion = true;
            Sequence->Notifies.Reset();
            Sequence->RemoveUserDataOfClass(UAssetUserData::StaticClass());
            if (!Save(Sequence)) return TEXT("Cannot save prepared clip");
            FPoseSearchDatabaseAnimationAsset Entry; Entry.AnimAsset = Sequence; Database->AddAnimationAsset(Entry);
        }
        Database->PostEditChange();
        using namespace UE::PoseSearch;
        if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,ERequestAsyncBuildFlag::NewRequest|ERequestAsyncBuildFlag::WaitForCompletion) != EAsyncBuildIndexResult::Success) return TEXT("Pose index failed: ")+GroupName;
        if (!Save(Database)) return TEXT("Cannot save pose index");
        Library->Databases.Add(FName(*GroupName),Database);
    }
    if (!Save(Library)) return TEXT("Cannot save locomotion library");
    if (!PrepareGraph()) return TEXT("Cannot compile locomotion AnimGraph");
    return TEXT("complete");
#else
    return TEXT("Editor authoring required");
#endif
}
