#include "HeyZedCrowd.h"
#include "HeyZedEntities.h"
#include "HeyZedWorldSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "HeyZedMetrics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Dom/JsonObject.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UHeyZedCrowdComponent::UHeyZedCrowdComponent(){PrimaryComponentTick.bCanEverTick=false;}
bool UHeyZedWorldSubsystem::SendCrowdReadiness(bool Enabled)
{
    if(!bShared)return false;
    auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("action"),TEXT("entity_crowd_ready"));Command->SetStringField(TEXT("token"),FGuid::NewGuid().ToString(EGuidFormats::Digits));Command->SetNumberField(TEXT("generation"),Epoch);Command->SetNumberField(TEXT("schema"),1);Command->SetBoolField(TEXT("enabled"),Enabled);
    FString Text;FJsonSerializer::Serialize(Command,TJsonWriterFactory<>::Create(&Text));return Memory.Offer(TEXT("commands"),Text);
}
void UHeyZedCrowdComponent::Initialize(UHeyZedEntityResources* Value)
{
    Resources=Value;bResourcesReady=Value&&Value->CrowdMesh&&Value->CrowdMaterials.Num()==4;
    if(bResourcesReady)for(const TCHAR* State:{TEXT("idle"),TEXT("walk"),TEXT("run"),TEXT("attack"),TEXT("sit"),TEXT("climb"),TEXT("crawl"),TEXT("crawl_idle")})if(!Value->CrowdAnimations.FindRef(State))bResourcesReady=false;
}
void UHeyZedCrowdComponent::SetEpoch(int64 Value)
{
    if(Epoch==Value)return;Epoch=Value;NextReadiness=0;VirtualSamples=VirtualProxyCreations=0;PeakVirtualViews=PeakVisibleVirtualViews=0;
    for(auto& Pair:Views)if(Pair.Value.Mesh)Pair.Value.Mesh->DestroyComponent();Views.Reset();
    for(auto& Pair:Leaders)if(Pair.Value)Pair.Value->DestroyComponent();Leaders.Reset();
}
void UHeyZedCrowdComponent::Apply(const TSharedRef<FJsonObject>& Event)
{
    if(Event->GetNumberField(TEXT("epoch"))!=Epoch||Event->GetIntegerField(TEXT("schema"))!=1)return;
    const TArray<TSharedPtr<FJsonValue>>* Removed=nullptr;if(Event->TryGetArrayField(TEXT("removed"),Removed))for(const auto& Value:*Removed){const FString Id=Value->AsString();if(auto* View=Views.Find(Id);View&&View->Mesh)View->Mesh->DestroyComponent();Views.Remove(Id);}
    const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;if(!Event->TryGetArrayField(TEXT("points"),Points))return;
    for(const auto& Value:*Points){
        const auto& Row=Value->AsArray();if(Row.Num()!=13)continue;
        const FString Id=Row[0]->AsString();const double Time=Row[1]->AsNumber()*1.e-9;
        auto& View=Views.FindOrAdd(Id);if(Time<=View.SourceTime)continue;
        const FVector Position(Row[2]->AsNumber()*100,Row[3]->AsNumber()*100,Row[4]->AsNumber()*HeyZedMetrics::Floor);
        const FTransform Target(FRotator(0,Row[7]->AsNumber()+90,0),Position,FVector(Resources?Resources->CrowdScale:1.7564f));
        if(Position.ContainsNaN())continue;
        const bool Continuous=View.SourceTime>0&&Time-View.SourceTime<3&&FVector::DistSquared(View.Target.GetLocation(),Position)<FMath::Square(400.f);
        View.Previous=Continuous?View.Target:Target;View.PreviousTime=Continuous?View.SourceTime:Time;
        if(Continuous)View.Interval=FMath::Clamp(Time-View.SourceTime,.05,1.5);
        View.Target=Target;View.SourceTime=Time;View.Received=FPlatformTime::Seconds();View.Action=Row[8]->AsString();View.Variant=Row[9]->AsNumber();View.Detail=Row[10]->AsBool();View.HeightKnown=Row[11]->AsBool();View.Virtual=Row[12]->AsBool();
        if(View.Virtual)++VirtualSamples;
    }
}
bool UHeyZedCrowdComponent::HasProxy(const FString& Id) const{return bResourcesReady&&Views.Contains(Id);}
bool UHeyZedCrowdComponent::WantsDetail(const FString& Id) const{const auto* View=Views.Find(Id);return !View||View->Detail;}
bool UHeyZedCrowdComponent::ShowProxy(const FString& Id,bool DetailReady) const{const auto* View=Views.Find(Id);return HasProxy(Id)&&(!View->Detail||!DetailReady);}
USkeletalMeshComponent* UHeyZedCrowdComponent::Leader(const FString& Action,int32 Permutation)
{
    if(!Resources||!Resources->CrowdMesh)return nullptr;
    auto* Clip=Resources->CrowdAnimations.FindRef(Action).Get();if(!Clip){Clip=Resources->CrowdAnimations.FindRef(TEXT("idle")).Get();if(!Clip)return nullptr;}
    const FString Key=Action+Clip->GetPathName()+FString::FromInt(Permutation);if(auto* Existing=Leaders.FindRef(Key).Get())return Existing;
    auto* Mesh=NewObject<USkeletalMeshComponent>(GetOwner());Mesh->SetupAttachment(GetOwner()->GetRootComponent());Mesh->SetSkeletalMeshAsset(Resources->CrowdMesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetVisibility(false);Mesh->SetCastShadow(false);Mesh->SetVisibleInRayTracing(false);
    Mesh->SetForcedLOD(1);Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Mesh->RegisterComponent();Mesh->SetComponentTickInterval(.05f);
    Mesh->PlayAnimation(Clip,true);Mesh->SetPlayRate(Action==TEXT("walk_slow")?.65f:1.f);Mesh->SetPosition(Clip->GetPlayLength()*Permutation/3.f,false);Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();if(Action==TEXT("idle")||Action==TEXT("sit")||Action==TEXT("crawl_idle"))Mesh->SetComponentTickEnabled(false);
    Leaders.Add(Key,Mesh);return Mesh;
}
void UHeyZedCrowdComponent::Present(float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedCrowd_Present);
    const double Now=FPlatformTime::Seconds();
    if(Now>=NextReadiness)if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();HeyZed&&HeyZed->SendCrowdReadiness(bResourcesReady))NextReadiness=Now+1;
    RealCount=VirtualCount=VisibleCount=MissingActions=0;if(!bResourcesReady)return;
    auto* Entities=Cast<AHeyZedEntities>(GetOwner());
    int32 VisibleVirtual=0;
    for(auto& Pair:Views){auto& View=Pair.Value;if(View.Virtual)++VirtualCount;else ++RealCount;
        const bool Detailed=Entities&&Entities->DetailReady(Pair.Key);const bool Visible=!View.Detail||!Detailed;
        if(!Visible){if(View.Mesh&&View.Mesh->IsVisible()){if(Entities)Entities->BeginCrowdHandoff(Pair.Key,View.Mesh->GetComponentTransform());View.Mesh->SetVisibility(false);}continue;}
        FString Action=View.Action;if(Now-View.Received>FMath::Max(.6,View.Interval*2.5)&&(Action==TEXT("walk")||Action==TEXT("walk_slow")||Action==TEXT("run")))Action=TEXT("idle");
        if(!Resources->CrowdAnimations.Contains(Action))++MissingActions;
        auto* Pose=Leader(Action,int32(GetTypeHash(Pair.Key)%3));if(!Pose)continue;
        const bool CreatingProxy=!View.Mesh;
        if(!View.Mesh){auto* Mesh=NewObject<USkeletalMeshComponent>(GetOwner());Mesh->SetupAttachment(GetOwner()->GetRootComponent());Mesh->SetSkeletalMeshAsset(Resources->CrowdMesh);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetBoundsScale(1.5f);Mesh->bUseBoundsFromLeaderPoseComponent=false;Mesh->SetLeaderPoseComponent(Pose,true,false);Mesh->RegisterComponent();Mesh->SetComponentTickEnabled(false);if(!Resources->CrowdMaterials.IsEmpty())Mesh->SetMaterial(0,Resources->CrowdMaterials[uint32(View.Variant)%Resources->CrowdMaterials.Num()]);View.Mesh=Mesh;}
        if(View.Virtual){++VisibleVirtual;if(CreatingProxy)++VirtualProxyCreations;}
        if(View.Mesh->LeaderPoseComponent.Get()!=Pose)View.Mesh->SetLeaderPoseComponent(Pose,true,false);
        const double Clock=FMath::Min(View.SourceTime,View.SourceTime+Now-View.Received-View.Interval);
        const float Alpha=View.SourceTime>View.PreviousTime?FMath::Clamp(float((Clock-View.PreviousTime)/(View.SourceTime-View.PreviousTime)),0.f,1.f):1.f;
        FTransform Transform;Transform.Blend(View.Previous,View.Target,Alpha);if(View.BlendStarted>=0&&Now-View.BlendStarted<.15){FTransform Blended;Blended.Blend(View.BlendFrom,Transform,float(FMath::Clamp((Now-View.BlendStarted)/.15,0.,1.)));Transform=Blended;}if(!View.Mesh->GetComponentTransform().Equals(Transform,.001f))View.Mesh->SetWorldTransform(Transform);View.Mesh->SetCastShadow(true);if(!View.Mesh->IsVisible())View.Mesh->SetVisibility(true);++VisibleCount;
    }
    PeakVirtualViews=FMath::Max(PeakVirtualViews,VirtualCount);PeakVisibleVirtualViews=FMath::Max(PeakVisibleVirtualViews,VisibleVirtual);
}
void UHeyZedCrowdComponent::DetailReleased(const FString& Id,const FTransform& Transform){if(auto* View=Views.Find(Id)){View->BlendFrom=Transform;View->BlendStarted=FPlatformTime::Seconds();}}
void UHeyZedCrowdComponent::AppendHealth(FJsonObject& Health) const
{
    Health.SetNumberField(TEXT("crowd_real"),RealCount);Health.SetNumberField(TEXT("crowd_virtual_xy"),VirtualCount);Health.SetNumberField(TEXT("crowd_visible"),VisibleCount);Health.SetNumberField(TEXT("crowd_pose_leaders"),Leaders.Num());Health.SetNumberField(TEXT("crowd_missing_actions"),MissingActions);Health.SetBoolField(TEXT("crowd_resources_ready"),bResourcesReady);
    Health.SetNumberField(TEXT("crowd_virtual_samples"),VirtualSamples);Health.SetNumberField(TEXT("crowd_virtual_proxy_creations"),VirtualProxyCreations);Health.SetNumberField(TEXT("crowd_peak_virtual_views"),PeakVirtualViews);Health.SetNumberField(TEXT("crowd_peak_visible_virtual_views"),PeakVisibleVirtualViews);
}
