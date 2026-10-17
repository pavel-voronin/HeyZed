#include "HeyZedEntities.h"
#include "HeyZedWorldSubsystem.h"
#include "HeyZedPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationAsset.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UHeyZedWorldSubsystem::SendEntityAnimation(const FString& Id,int64 Revision,bool Enabled,const FString& Clips)
{
    if(!bShared)return false;
    auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("action"),TEXT("entity_animation"));
    Command->SetStringField(TEXT("token"),FGuid::NewGuid().ToString(EGuidFormats::Digits));Command->SetStringField(TEXT("id"),Id);
    Command->SetNumberField(TEXT("generation"),Epoch);Command->SetNumberField(TEXT("revision"),Revision);Command->SetBoolField(TEXT("enabled"),Enabled);
    Command->SetStringField(TEXT("clips"),Clips);
    FString Text;FJsonSerializer::Serialize(Command,TJsonWriterFactory<>::Create(&Text));return Memory.Offer(TEXT("commands"),Text);
}

void AHeyZedEntities::AcknowledgeLocomotion(FHeyZedEntity& Entity,bool Enabled)
{
    FString Clips;if(Entity.AnimationState)Entity.AnimationState->TryGetStringField(TEXT("key"),Clips);
    if(Entity.LocalAnimationAcknowledged==Enabled&&(!Enabled||Entity.AcknowledgedAnimationKey==Clips))return;
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();HeyZed&&HeyZed->SendEntityAnimation(Entity.Id,Entity.Revision,Enabled,Clips)){Entity.LocalAnimationAcknowledged=Enabled;Entity.AcknowledgedAnimationKey=Clips;}
}

bool AHeyZedEntities::EvaluateLocomotion(FHeyZedEntity& Entity,TArray<FTransform>& Pose,float Delta,bool SourceSample)
{
    if(!Entity.AnimationState||Entity.Seated||Entity.AnimationState->GetStringField(TEXT("mode"))!=TEXT("local"))return false;
    auto Tracks=Entity.AnimationState->GetArrayField(TEXT("tracks"));if(Tracks.IsEmpty()||Tracks.Num()>32)return false;
    if(!SourceSample&&Entity.Kind==TEXT("player"))if(auto* Pawn=Cast<AHeyZedPawn>(UGameplayStatics::GetPlayerPawn(this,0));Pawn&&Pawn->CameraMode==0&&Pawn->NativeVelocity.SizeSquared2D()>25)
    {
        const FRotator View(0,Pawn->MovementYaw(),0);const FVector Velocity=View.UnrotateVector(Pawn->NativeVelocity);const double Angle=FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y,Velocity.X));
        const FString Replacement=FMath::Abs(Angle)>135?TEXT("bob_walkbwdnoaim1hand"):FMath::Abs(Angle)>45?(Angle<0?TEXT("bob_strafe_left"):TEXT("bob_strafe_right")):TEXT("");
        if(!Replacement.IsEmpty())for(auto& Value:Tracks){const auto Source=Value->AsObject();if(Source->HasField(TEXT("mask")))continue;const FString Name=Source->GetStringField(TEXT("clip"));if(!Name.StartsWith(TEXT("bob_")))continue;auto Track=MakeShared<FJsonObject>(*Source);Track->SetStringField(TEXT("clip"),Replacement);Track->SetNumberField(TEXT("id"),-int32(GetTypeHash(Replacement)&0x3fffffff)-Source->GetIntegerField(TEXT("id")));Value=MakeShared<FJsonValueObject>(Track);}
    }
    auto Required=Tracks;if(Entity.Kind==TEXT("player"))for(const TCHAR* Name:{TEXT("bob_walkbwdnoaim1hand"),TEXT("bob_strafe_left"),TEXT("bob_strafe_right")}){auto Clip=MakeShared<FJsonObject>();Clip->SetStringField(TEXT("clip"),Name);Required.Add(MakeShared<FJsonValueObject>(Clip));}
    for(const auto& Value:Required)
    {
        const FString Key=Value->AsObject()->GetStringField(TEXT("clip"));
        if(ResidentAnimations.Contains(Key))continue;
        const auto* Resource=Resources->NativeAnimations.Find(Key);
        if(!Resource){if(!FailedAnimations.Contains(Key)){FailedAnimations.Add(Key);UE_LOG(LogTemp,Warning,TEXT("heyzed native locomotion is not prepared: %s"),*Key);}AcknowledgeLocomotion(Entity,false);return false;}if(FailedAnimations.Contains(Key)){AcknowledgeLocomotion(Entity,false);return false;}
        if(auto* Clip=Resource->Get()){ResidentAnimations.Add(Key,Clip);continue;}
        if(!LoadingAnimations.Contains(Key))
        {
            LoadingAnimations.Add(Key);TWeakObjectPtr<AHeyZedEntities> Weak=this;const FSoftObjectPath Path=Resource->ToSoftObjectPath();
            UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,FStreamableDelegate::CreateLambda([Weak,Key,Path](){if(!Weak.IsValid())return;Weak->LoadingAnimations.Remove(Key);if(auto* Clip=Cast<UAnimSequence>(Path.ResolveObject()))Weak->ResidentAnimations.Add(Key,Clip);else{Weak->FailedAnimations.Add(Key);UE_LOG(LogTemp,Error,TEXT("heyzed locomotion clip failed: %s"),*Key);}}));
        }
        return false;
    }
    const double Now=FPlatformTime::Seconds();const int32 Count=Entity.BoneNames.Num();if(!Count)return false;
    TArray<FTransform> Local;Local.Init(FTransform::Identity,Count);TArray<float> Remaining;Remaining.Init(1.f,Count);TArray<float> Total;Total.Init(0.f,Count);
    for(const auto& Value:Tracks)
    {
        const auto Track=Value->AsObject();auto* Clip=ResidentAnimations.FindRef(Track->GetStringField(TEXT("clip"))).Get();if(!Clip||!Clip->GetSkeleton())return false;
        const auto& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();const int32 Id=Track->GetIntegerField(TEXT("id"));
        const double Duration=Clip->GetPlayLength(),SourceDuration=Track->GetNumberField(TEXT("duration"));const double Ratio=Duration/FMath::Max(SourceDuration,.001);const double Rate=Track->GetNumberField(TEXT("rate"))*Ratio;if(Duration<=0||!FMath::IsFinite(Rate))return false;
        const bool Loop=Track->GetBoolField(TEXT("loop"));
        const double Clock=Entity.PresentationSample>0?Entity.PresentationSample:Entity.NativeSample;
        const double AnimationDelta=Entity.AnimationSample>0?FMath::Clamp(Clock-Entity.AnimationSample,0.,.25):0.;
        double Target=Track->GetNumberField(TEXT("phase"))*Ratio+(SourceSample?0:Rate*FMath::Clamp(Clock-Entity.NativeSample,-.75,.12));
        auto Wrap=[Duration,Loop](double Time){return Loop?FMath::Fmod(FMath::Fmod(Time,Duration)+Duration,Duration):FMath::Clamp(Time,0.,Duration);};Target=Wrap(Target);
        double Phase=Target;if(!SourceSample&&Now-Entity.AnimationEvaluated<.25){if(const double* Previous=Entity.AnimationPhases.Find(Id))Phase=Wrap(*Previous+Rate*AnimationDelta);}
        double Error=Target-Phase;if(Loop){if(Error>Duration*.5)Error-=Duration;if(Error<-Duration*.5)Error+=Duration;}
        Phase=Wrap(Phase+Error*(1-FMath::Exp(-AnimationDelta*18)));
        if(!SourceSample)Entity.AnimationPhases.Add(Id,Phase);
        const FAnimExtractContext Context(Phase,false);const float Weight=FMath::Clamp(float(Track->GetNumberField(TEXT("weight"))),0.f,1.f);
        const TArray<TSharedPtr<FJsonValue>>* Mask=nullptr;Track->TryGetArrayField(TEXT("mask"),Mask);
        const FName Deferred(*Track->GetStringField(TEXT("deferred")));const FString Axis=Track->GetStringField(TEXT("axis"));
        for(int32 Bone=0;Bone<Count;++Bone)
        {
            const int32 Index=Ref.FindBoneIndex(Entity.BoneNames[Bone]);if(Index==INDEX_NONE||Remaining[Bone]<=.001f)continue;
            const float Raw=Weight*(Mask&&Mask->IsValidIndex(Bone)?float((*Mask)[Bone]->AsNumber()):1.f);const float Contribution=FMath::Min(Raw,Remaining[Bone]);if(Contribution<=.001f)continue;
            FTransform Atom;Clip->GetBoneTransform(Atom,FSkeletonPoseBoneIndex(Index),Context,false);
            Atom.SetScale3D(FVector::OneVector);
            if(Entity.BoneNames[Bone]==Deferred){auto P=Atom.GetTranslation();if(Axis==TEXT("Y")){P.X=0;P.Y*=2;}else{P.X=0;P.Z=0;}Atom.SetTranslation(P);}
            if(Total[Bone]==0)Local[Bone]=Atom;else{FTransform Blended;Blended.Blend(Local[Bone],Atom,Contribution/(Total[Bone]+Contribution));Local[Bone]=Blended;}
            Total[Bone]+=Contribution;Remaining[Bone]=FMath::Max(0.f,Remaining[Bone]-Raw);
        }
    }
    const auto* First=ResidentAnimations.FindRef(Tracks[0]->AsObject()->GetStringField(TEXT("clip"))).Get();const auto& Ref=First->GetSkeleton()->GetReferenceSkeleton();
    Pose.SetNum(Count);
    TSet<int32> Built;
    TFunction<bool(int32)> Build=[&](int32 Bone){if(Built.Contains(Bone))return true;const int32 RefIndex=Ref.FindBoneIndex(Entity.BoneNames[Bone]);if(RefIndex==INDEX_NONE)return false;const int32 ParentIndex=Ref.GetParentIndex(RefIndex);const int32* Parent=ParentIndex>=0?Entity.BoneIndices.Find(Ref.GetBoneName(ParentIndex)):nullptr;if(Parent&&!Build(*Parent))return false;FTransform Atom=Total[Bone]>.001f?Local[Bone]:Ref.GetRefBonePose()[RefIndex];Pose[Bone]=Parent?Atom*Pose[*Parent]:Atom;Built.Add(Bone);return true;};
    for(int32 Bone=0;Bone<Count;++Bone)if(!Build(Bone))return false;
    if(!SourceSample){Entity.AnimationEvaluated=Now;Entity.AnimationSample=Entity.PresentationSample;AcknowledgeLocomotion(Entity,!FParse::Param(FCommandLine::Get(),TEXT("HeyZedLocomotionAudit")));}return true;
}

bool AHeyZedEntities::EvaluateEntityPose(FHeyZedEntity& Entity,TArray<FTransform>& Pose,float Delta)
{
    const double Now=FPlatformTime::Seconds();const bool Local=EvaluateLocomotion(Entity,Pose,Delta);
    if(Local&&FParse::Param(FCommandLine::Get(),TEXT("HeyZedLocomotionAudit"))&&Entity.TargetPose.Num()==Pose.Num())
    {
        static TMap<FString,int64> Sequences;static TMap<FString,int32> Counts;
        const FString Key=Entity.Kind+TEXT("/")+Entity.AnimationState->GetStringField(TEXT("state"));
        if(Sequences.FindRef(Entity.Id)!=Entity.Sequence&&Counts.FindRef(Key)<10){Sequences.Add(Entity.Id,Entity.Sequence);TArray<FTransform> SourcePose;if(EvaluateLocomotion(Entity,SourcePose,0,true)){
            double Maximum=0,Feet=0;for(int32 Bone=0;Bone<SourcePose.Num();++Bone){const double Error=FVector::Distance(SourcePose[Bone].GetLocation(),Entity.TargetPose[Bone].GetLocation());Maximum=FMath::Max(Maximum,Error);if(Entity.BoneNames[Bone].ToString().Contains(TEXT("Foot")))Feet=FMath::Max(Feet,Error);}auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("kind"),Entity.Kind);Result->SetStringField(TEXT("id"),Entity.Id);Result->SetNumberField(TEXT("max_position_error_cm"),Maximum);Result->SetNumberField(TEXT("foot_position_error_cm"),Feet);Result->SetObjectField(TEXT("animation"),Entity.AnimationState);FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));FFileHelper::SaveStringToFile(Text+TEXT("\n"),*FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/locomotion-audit.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);++Counts.FindOrAdd(Key);
        }}
    }
    if(!Local)
    {
        AcknowledgeLocomotion(Entity,false);
        if(Entity.TargetPose.Num()!=Entity.BoneNames.Num()||Entity.TargetPose.IsEmpty())return false;
        const float Alpha=FMath::Clamp(float((Now-Entity.BoneReceived)/Entity.BlendDuration),0.f,1.f);Pose.SetNum(Entity.TargetPose.Num());
        for(int32 Bone=0;Bone<Pose.Num();++Bone)if(Entity.PreviousPose.IsValidIndex(Bone))Pose[Bone].Blend(Entity.PreviousPose[Bone],Entity.TargetPose[Bone],Alpha);else Pose[Bone]=Entity.TargetPose[Bone];
    }
    if(Local!=Entity.UsingLocalAnimation){Entity.AnimationBlendFrom=Entity.PresentedPose;Entity.AnimationModeChanged=Now;Entity.UsingLocalAnimation=Local;Entity.EvaluatedSequence=MIN_int64;}
    const float Alpha=Local?FMath::Clamp(float((Now-Entity.AnimationModeChanged)/.12),0.f,1.f):1.f;
    if(Alpha<1&&Entity.AnimationBlendFrom.Num()==Pose.Num())for(int32 Bone=0;Bone<Pose.Num();++Bone){FTransform Blended;Blended.Blend(Entity.AnimationBlendFrom[Bone],Pose[Bone],Alpha);Pose[Bone]=Blended;}
    Entity.PresentedPose=Pose;return true;
}
