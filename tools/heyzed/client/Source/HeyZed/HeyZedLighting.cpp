#include "HeyZedLighting.h"
#include "HeyZedFurniture.h"
#include "HeyZedFixture.h"
#include "HeyZedMetrics.h"
#include "HeyZedPawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Curves/CurveFloat.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "HeyZedMeshAuthoring.h"
#include "HeyZedWorldSubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"

bool UHeyZedLightResources::PrepareOcclusionMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if (!Mesh) return false;
    FinishHeyZedMesh(Mesh); return true;
#else
    return false;
#endif
}

bool UHeyZedLightResources::PrepareFixtureMesh(UStaticMesh* Mesh,const TArray<int32>& NonShadowSlots)
{
#if WITH_EDITOR
    if (!Mesh) return false;
    for(const auto& Slot:Mesh->GetStaticMaterials())if(Slot.MaterialInterface &&
        Slot.MaterialInterface->GetBlendMode()!=BLEND_Opaque && Slot.MaterialInterface->GetBlendMode()!=BLEND_Masked)
    {
        if(Mesh->GetNaniteSettings().bEnabled)
        {
            Mesh->GetNaniteSettings().bEnabled=false;
            Mesh->Build(false);Mesh->MarkPackageDirty();
        }
        break;
    }
    FinishHeyZedMesh(Mesh);
    for (int32 LOD=0;LOD<Mesh->GetNumLODs();++LOD) for (int32 Section=0;Section<Mesh->GetNumSections(LOD);++Section)
    {
        auto Info=Mesh->GetSectionInfoMap().Get(LOD,Section);
        Info.bCastShadow=!NonShadowSlots.Contains(Info.MaterialIndex);
        Mesh->GetSectionInfoMap().Set(LOD,Section,Info);
        Mesh->GetOriginalSectionInfoMap().Set(LOD,Section,Info);
    }
    Mesh->PostEditChange(); FinishHeyZedMesh(Mesh);
    Mesh->MarkPackageDirty(); return true;
#else
    return false;
#endif
}

AHeyZedLightRegion::AHeyZedLightRegion()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Poles=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StreetFixtures"));
    Poles->SetupAttachment(RootComponent);
    Poles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Poles->SetCanEverAffectNavigation(false);
    Poles->SetCastShadow(true);
    Poles->SetCullDistances(0,18000);
    FarGlows=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DistantStreetGlow"));
    FarGlows->SetupAttachment(RootComponent); FarGlows->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FarGlows->SetCanEverAffectNavigation(false); FarGlows->SetCastShadow(false);
    FarGlows->bVisibleInRayTracing=false; FarGlows->bAffectDistanceFieldLighting=false;
    FarGlows->MinDrawDistance=18000; FarGlows->SetCullDistances(0,200000);
}
void AHeyZedLightRegion::BeginPlay()
{
    Super::BeginPlay();
    if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) HeyZed->RegisterLightRegion(this);
}
void AHeyZedLightRegion::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>()) HeyZed->UnregisterLightRegion(this);
    Super::EndPlay(Reason);
}

AHeyZedLighting::AHeyZedLighting()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    PrimaryActorTick.bCanEverTick=true;
    Sun=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun")); Sun->SetupAttachment(RootComponent);
    Moon=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon")); Moon->SetupAttachment(RootComponent);
    Sky=CreateDefaultSubobject<USkyLightComponent>(TEXT("Sky")); Sky->SetupAttachment(RootComponent);
    Atmosphere=CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere")); Atmosphere->SetupAttachment(RootComponent);
    Fog=CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("WeatherFog")); Fog->SetupAttachment(RootComponent);
    for (auto* Light : {Sun.Get(),Moon.Get()})
    {
        Light->SetMobility(EComponentMobility::Movable); Light->SetCastShadows(true);
        Light->bAllowMegaLights=false;
        Light->SetDynamicShadowDistanceMovableLight(18000);
        Light->SetAtmosphereSunLight(true);
    }
    Sun->SetAtmosphereSunLightIndex(0); Moon->SetAtmosphereSunLightIndex(1);
    Sun->SetForwardShadingPriority(1);Moon->SetForwardShadingPriority(0);
    Sun->SetLightSourceAngle(.535f); Moon->SetLightSourceAngle(.52f);
    Sun->SetRelativeRotation(FRotator(-55,-35,0)); Sun->SetIntensity(100000);
    Moon->SetIntensity(0);
    Sky->SetMobility(EComponentMobility::Movable); Sky->bRealTimeCapture=true;
    Fog->SetFogDensity(0); Fog->SetVolumetricFog(true); Fog->SetVolumetricFogDistance(12000);
}
void AHeyZedLighting::BeginPlay()
{
    Super::BeginPlay();
    CommandSession=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FSoftObjectPath LibraryPath(TEXT("/Game/HeyZed/Lighting/FixtureLibrary.FixtureLibrary"));
    const FSoftObjectPath ResourcePath(TEXT("/Game/HeyZed/Lighting/Resources.Resources"));
    ResourceLoad=UAssetManager::GetStreamableManager().RequestAsyncLoad({LibraryPath,ResourcePath},
        FStreamableDelegate::CreateWeakLambda(this,[this,LibraryPath,ResourcePath]()
        {
            FixtureLibrary=Cast<UHeyZedFixtureLibrary>(LibraryPath.ResolveObject());
            Resources=Cast<UHeyZedLightResources>(ResourcePath.ResolveObject());
            if(!FixtureLibrary || !Resources || !FixtureLibrary->Cube || !FixtureLibrary->Sphere)
            {
                FixtureLibrary=nullptr;
                UE_LOG(LogTemp,Error,TEXT("heyzed: lighting resources require a complete fixture library"));
                return;
            }
            NextRefresh=0;
            bFixturesDirty=true;bSourcesDirty=true;SourcePresentations.Reset();
            for(auto& Weak:Regions)if(auto* Region=Weak.Get())RefreshRegion(Region);
        }));
    Exposure=GetWorld()->SpawnActor<APostProcessVolume>(); Exposure->bUnbound=true;
    auto& Settings=Exposure->Settings;
    Settings.bOverride_AutoExposureMinBrightness=true; Settings.AutoExposureMinBrightness=MinimumExposureEV100;
    Settings.bOverride_AutoExposureMaxBrightness=true; Settings.AutoExposureMaxBrightness=16;
    Settings.bOverride_AutoExposureBias=true; Settings.AutoExposureBias=ExposureCompensation;
    Settings.bOverride_AutoExposureSpeedUp=true; Settings.AutoExposureSpeedUp=3;
    Settings.bOverride_AutoExposureSpeedDown=true; Settings.AutoExposureSpeedDown=1;
    Settings.bOverride_BloomIntensity=true; Settings.BloomIntensity=.1f;
    Settings.bOverride_LensFlareIntensity=true; Settings.LensFlareIntensity=0;
    Settings.bOverride_LocalExposureHighlightContrastScale=true; Settings.LocalExposureHighlightContrastScale=1;
    Settings.bOverride_LocalExposureShadowContrastScale=true; Settings.LocalExposureShadowContrastScale=1;
    auto* Highlights=NewObject<UCurveFloat>(Exposure);
    for(const auto Point:{FVector2D(-4,.8),FVector2D(4,.8),FVector2D(8,.65),FVector2D(12,.6),FVector2D(16,.6)})
        Highlights->FloatCurve.AddKey(Point.X,Point.Y);
    auto* Shadows=NewObject<UCurveFloat>(Exposure);
    for(const auto Point:{FVector2D(-4,.95),FVector2D(4,.95),FVector2D(8,.7),FVector2D(12,.6),FVector2D(16,.6)})
        Shadows->FloatCurve.AddKey(Point.X,Point.Y);
    Settings.bOverride_LocalExposureHighlightContrastCurve=true;Settings.LocalExposureHighlightContrastCurve=Highlights;
    Settings.bOverride_LocalExposureShadowContrastCurve=true;Settings.LocalExposureShadowContrastCurve=Shadows;
    Settings.bOverride_LocalExposureBlurredLuminanceBlend=true;Settings.LocalExposureBlurredLuminanceBlend=.5f;
    Settings.bOverride_LumenSceneViewDistance=true; Settings.LumenSceneViewDistance=DetailedGeometryDistance;
    Settings.bOverride_LumenMaxTraceDistance=true; Settings.LumenMaxTraceDistance=DetailedGeometryDistance;
}
void AHeyZedLighting::EndPlay(const EEndPlayReason::Type Reason)
{
    if(ResourceLoad)ResourceLoad->CancelHandle();
    ResourceLoad.Reset();Super::EndPlay(Reason);
}
void AHeyZedLighting::SetEpoch(int64 Value)
{
    if (Epoch==Value) return;
    Epoch=Value; EnvironmentRevision=-1; States.Reset(); GridPower=false; StreetNight=false;
    SourcePresentations.Reset();bSourcesDirty=true;bFixturesDirty=true;
    for (auto& Weak : Regions) if (auto* Region=Weak.Get()) RefreshRegion(Region);
    for (auto& Pair : Models) if(Pair.Value)Pair.Value->Destroy(); Models.Reset();
    for (auto& Pair : Lights) Pair.Value->DestroyComponent();
    Lights.Reset(); Presented.Reset(); VisibilityWeights.Reset(); VisibilityTargets.Reset(); NextRefresh=0;
}
void AHeyZedLighting::SetIdentity(const FString& Value)
{
    if (Identity==Value) return;
    for (auto& Pair : Models) if(Pair.Value)Pair.Value->Destroy(); Models.Reset();
    for (auto& Pair : Lights) Pair.Value->DestroyComponent();
    Lights.Reset(); Presented.Reset(); VisibilityWeights.Reset(); VisibilityTargets.Reset();
    Identity=Value;
    SourcePresentations.Reset();bSourcesDirty=true;bFixturesDirty=true;
    for (auto& Weak : Regions) if (auto* Region=Weak.Get()) Region->SetActorHiddenInGame(Identity.IsEmpty() || Identity!=Region->Identity);
    NextRefresh=0;
}
void AHeyZedLighting::RegisterRegion(AHeyZedLightRegion* Region)
{
    Regions.Add(Region);
    bFixturesDirty=true;
    Region->SetActorHiddenInGame(Identity.IsEmpty() || Identity!=Region->Identity);
    Region->Poles->SetCullDistances(0,DetailedGeometryDistance);
    Region->FarGlows->MinDrawDistance=DetailedGeometryDistance;
    Region->FarGlows->MarkRenderStateDirty();
    RefreshRegion(Region);
    NextRefresh=0;
}
void AHeyZedLighting::RefreshRegion(AHeyZedLightRegion* Region)
{
    for (int32 I=0;I<Region->Fixtures.Num();++I)
    {
        const auto State=FixtureState(Region->Fixtures[I]);
        const bool Enabled=State.Enabled && !State.Deleted;
        Region->Poles->SetCustomDataValue(I,0,Enabled?1.f:0.f,I==Region->Fixtures.Num()-1);
        Region->FarGlows->SetCustomDataValue(I,0,Enabled?1.f:0.f,I==Region->Fixtures.Num()-1);
        Region->Poles->UpdateInstanceTransform(I,FTransform(FRotator::ZeroRotator,Region->Fixtures[I].Position-Region->GetActorLocation(),FVector(State.Deleted?0.f:1.f)),false,I==Region->Fixtures.Num()-1,true);
    }
}
FHeyZedLightState AHeyZedLighting::FixtureState(const FHeyZedFixture& Fixture) const
{
    FHeyZedLightState Result; Result.Position=Fixture.Position+(Resources?Resources->Emitter:FVector(0,0,326.43));
    Result.Color=Fixture.Color; Result.Radius=0; Result.Lumens=StreetLumens; Result.Deleted=true;
    const TArray<FString> Controls=Fixture.Controls.IsEmpty()?TArray<FString>{Fixture.Id}:Fixture.Controls;
    for (const auto& Control : Controls)
    {
        int32 Separator=INDEX_NONE;
        if(FixtureLibrary&&Control.FindLastChar(TEXT(':'),Separator))
            if(const auto* Binding=FixtureLibrary->Bindings.Find(Control.Mid(Separator+1));Binding&&*Binding!=TEXT("street"))continue;
        const auto* State=States.Find(Control);
        if (State && (State->Deleted || State->Kind!=TEXT("street"))) continue;
        Result.Deleted=false;
        const bool Enabled=State?State->Enabled:GridPower && StreetNight;
        if (!Enabled) continue;
        Result.Enabled=true;
        Result.Radius=FMath::Max(Result.Radius,State?State->Radius:Fixture.Radius);
        if (State) Result.Color=State->Color;
    }
    return Result;
}
void AHeyZedLighting::UnregisterRegion(AHeyZedLightRegion* Region)
{
    Regions.Remove(Region); bFixturesDirty=true;NextRefresh=0;
}
void AHeyZedLighting::Apply(const TSharedRef<FJsonObject>& Event)
{
    if (Event->GetNumberField(TEXT("epoch"))!=Epoch) return;
    const FString Type=Event->GetStringField(TEXT("type"));
    const int64 Revision=Event->GetNumberField(TEXT("revision"));
    if (Type==TEXT("environment"))
    {
        if (Revision<=EnvironmentRevision) return;
        const bool Initial=EnvironmentRevision<0;EnvironmentRevision=Revision;
        auto Direction=[&](const TCHAR* Name)
        {
            const auto& V=Event->GetArrayField(Name);
            // The existing ephemeris uses map east +X, south +Y, up +Z.
            return FVector(V[0]->AsNumber(),V[1]->AsNumber(),V[2]->AsNumber()).GetSafeNormal();
        };
        const FVector Sunward=Direction(TEXT("sun")), Moonward=Direction(TEXT("moon"));
        const float Cloud=FMath::Clamp(Event->GetNumberField(TEXT("cloud")),0.,1.);
        const float Rain=FMath::Clamp(Event->GetNumberField(TEXT("rain")),0.,1.);
        const float Phase=FMath::Clamp(Event->GetNumberField(TEXT("phase")),0.,1.);
        const bool Power=Event->GetBoolField(TEXT("grid_power")), Night=Event->GetBoolField(TEXT("street_night"));
        if (Power!=GridPower || Night!=StreetNight)
        {
            GridPower=Power; StreetNight=Night;
            bFixturesDirty=true;
            for (auto& Weak : Regions) if (auto* Region=Weak.Get()) RefreshRegion(Region);
            NextRefresh=0;
        }
        // Sky Atmosphere computes transmittance at ground level and twilight scattering.
        // Keep its outer-space illuminant alive below the horizon through twilight.
        const float SolarAltitude=FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(float(Sunward.Z),-1.f,1.f)));
        const float SunLux=100000*FMath::SmoothStep(-18.f,-12.f,SolarAltitude)*(1-.9f*Cloud*Cloud)*(1-.2f*Rain);
        const float MoonLux=.22f*FMath::Pow(Phase,3.f)*FMath::SmoothStep(0.f,.1f,float(Moonward.Z))*(1-.95f*Cloud);
        Sky->SetIntensity(1.f+.5f*FMath::SmoothStep(0.f,.15f,float(Sunward.Z)));
        SunTarget=(-Sunward).Rotation().Quaternion();MoonTarget=(-Moonward).Rotation().Quaternion();
        SunTargetLux=SunLux;MoonTargetLux=MoonLux;
        if(Initial){Sun->SetWorldRotation(SunTarget);Moon->SetWorldRotation(MoonTarget);Sun->SetIntensity(SunLux);Moon->SetIntensity(MoonLux);}
        const bool SunDominant=Sunward.Z>0||MoonLux<=0;
        Sun->SetForwardShadingPriority(SunDominant?1:0);
        Moon->SetForwardShadingPriority(SunDominant?0:1);
        Sun->SetCastShadows(Sunward.Z>0);
        Moon->SetLightColor(FLinearColor(.74f,.82f,1)); Moon->SetCastShadows(MoonLux>.025f && Sunward.Z<0);
        Fog->SetFogDensity(.04f*FMath::Clamp(Event->GetNumberField(TEXT("fog")),0.,1.));
        Fog->SetFogHeightFalloff(.15f);
        return;
    }
    if (Type!=TEXT("light")) return;
    const FString Id=Event->GetStringField(TEXT("id")); auto& State=States.FindOrAdd(Id);
    if (Revision<=State.Revision) return; State.Revision=Revision;
    bFixturesDirty=true;
    State.Deleted=Event->GetBoolField(TEXT("deleted"));
    if (!State.Deleted)
    {
        State.Enabled=Event->GetBoolField(TEXT("enabled")); State.Kind=Event->GetStringField(TEXT("kind"));
        Event->TryGetStringField(TEXT("sprite"),State.Sprite);Event->TryGetStringField(TEXT("control"),State.Control);Event->TryGetStringField(TEXT("hand"),State.Hand);
        double Yaw=0;Event->TryGetNumberField(TEXT("yaw"),Yaw);State.Yaw=Yaw;
        Event->TryGetBoolField(TEXT("support"),State.Support);
        Event->TryGetBoolField(TEXT("interactable"),State.Interactable);
        State.Radius=FMath::Clamp(Event->GetNumberField(TEXT("radius")),0.,20.)*100;
        const auto& Color=Event->GetArrayField(TEXT("color"));
        State.Color=FLinearColor(Color[0]->AsNumber(),Color[1]->AsNumber(),Color[2]->AsNumber());
        State.Position=FVector(Event->GetNumberField(TEXT("x"))*100,Event->GetNumberField(TEXT("y"))*100,Event->GetNumberField(TEXT("height_cm")));
        State.Lumens=StreetLumens;
        double Lumens=0; if (Event->TryGetNumberField(TEXT("lumens"),Lumens)) State.Lumens=FMath::Max(0.,Lumens);
        double Cone=0; if (Event->TryGetNumberField(TEXT("cone"),Cone)) State.Outer=FMath::Clamp(Cone,0.,85.);
        const TArray<TSharedPtr<FJsonValue>>* D=nullptr;
        if (Event->TryGetArrayField(TEXT("direction"),D) && D->Num()==3) State.Direction=FVector((*D)[0]->AsNumber(),(*D)[1]->AsNumber(),(*D)[2]->AsNumber()).GetSafeNormal();
    }
    if(FixtureLibrary&&Resources&&!bSourcesDirty)PrepareSource(Id,State);
    for (auto& Weak : Regions) if (auto* Region=Weak.Get())
        for (int32 I=0;I<Region->Fixtures.Num();++I) if (Region->Fixtures[I].Id==Id || Region->Fixtures[I].Controls.Contains(Id))
        {
            RefreshRegion(Region);
            break;
        }
    NextRefresh=0;
}
void AHeyZedLighting::PublishTelemetry(const TSharedRef<FJsonObject>& Health) const
{
    auto Info=MakeShared<FJsonObject>();
    Info->SetNumberField(TEXT("environment_revision"),EnvironmentRevision);
    Info->SetNumberField(TEXT("sun_lux"),Sun->Intensity);
    Info->SetNumberField(TEXT("moon_lux"),Moon->Intensity);
    Info->SetNumberField(TEXT("sky_intensity"),Sky->Intensity);
    Info->SetBoolField(TEXT("sky_realtime"),Sky->IsRealTimeCaptureEnabled());
    Info->SetNumberField(TEXT("source_states"),States.Num());
    Info->SetNumberField(TEXT("native_local_lights"),Lights.Num());
    Info->SetNumberField(TEXT("fixture_models"),Models.Num());
    Info->SetNumberField(TEXT("streamed_light_regions"),Regions.Num());
    Info->SetBoolField(TEXT("resources_ready"),FixtureLibrary&&Resources);
    Info->SetNumberField(TEXT("exposure_min_ev100"),Exposure?Exposure->Settings.AutoExposureMinBrightness:0);
    Info->SetNumberField(TEXT("exposure_max_ev100"),Exposure?Exposure->Settings.AutoExposureMaxBrightness:0);
    Health->SetObjectField(TEXT("lighting"),Info);
}
void AHeyZedLighting::Tick(float Delta)
{
    Super::Tick(Delta);
    if(EnvironmentRevision>=0)
    {
        const float Blend=1-FMath::Exp(-FMath::Max(Delta,0.f)/.2f);
        Sun->SetWorldRotation(FQuat::Slerp(Sun->GetComponentQuat(),SunTarget,Blend));
        Moon->SetWorldRotation(FQuat::Slerp(Moon->GetComponentQuat(),MoonTarget,Blend));
        Sun->SetIntensity(FMath::FInterpTo(Sun->Intensity,SunTargetLux,Delta,5.f));
        Moon->SetIntensity(FMath::FInterpTo(Moon->Intensity,MoonTargetLux,Delta,5.f));
    }
    auto* PlayerController=GetWorld()->GetFirstPlayerController();
    const APawn* PlayerPawn=PlayerController?PlayerController->GetPawn():nullptr;
    if (FPlatformTime::Seconds()>=NextRefresh && PlayerPawn &&
        (NextRefresh==0 || !LastRefreshPosition.Equals(PlayerPawn->GetActorLocation(),.01)))
    {
        NextRefresh=FPlatformTime::Seconds()+.1; RefreshLights();
    }
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_HeldTransforms);
    if(auto* PC=GetWorld()->GetFirstPlayerController())if(auto* Avatar=PC->GetPawn<AHeyZedPawn>())
        for(const auto& Pair:States)if(!Pair.Value.Deleted&&Pair.Value.Kind==TEXT("held"))
        {
            const bool Left=Pair.Value.Hand==TEXT("left");
            const auto Rotation=PC->GetControlRotation();
            const FVector Anchor=Avatar->CameraMode==0 ? Avatar->GetPawnViewLocation()+Rotation.RotateVector(FVector(20,Left?-18:18,-25)) : Avatar->Body->GetSocketLocation(Left?TEXT("hand_l"):TEXT("hand_r"));
            if(auto* Model=Models.FindRef(Pair.Key).Get())Model->SetActorLocationAndRotation(Anchor,Rotation);
            if(auto* Light=Lights.FindRef(Pair.Key).Get())if(FixtureLibrary)
                if(const auto* Profile=FixtureLibrary->Profiles.Find(TEXT("held")))
                {Light->SetWorldLocation(Anchor+Rotation.RotateVector(Profile->Emitter));Light->SetWorldRotation(Rotation);}
        }
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_FadeLights);
    for (auto It=VisibilityWeights.CreateIterator();It;++It)
    {
        const auto* State=Presented.Find(It.Key());
        auto* Light=Lights.FindRef(It.Key()).Get();
        if (!State || !Light) {It.RemoveCurrent();continue;}
        float& Weight=It.Value();
        Weight=FMath::FInterpConstantTo(Weight,VisibilityTargets.FindRef(It.Key()),Delta,1.f/FMath::Max(.1f,StreamingFadeSeconds));
        Light->SetIntensity(State->Lumens*Weight);
        if (Weight<=0 && VisibilityTargets.FindRef(It.Key())<=0)
        {
            Light->DestroyComponent(); Lights.Remove(It.Key()); Presented.Remove(It.Key()); VisibilityTargets.Remove(It.Key()); It.RemoveCurrent();
        }
    }
}
void AHeyZedLighting::RefreshPreparedFixtures()
{
    if(!bFixturesDirty)return;
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_PrepareFixtures);
    TMap<FString,const FHeyZedFixture*> Fixtures;
    for(const auto& Weak:Regions)if(const auto* Region=Weak.Get();Region&&Region->Identity==Identity)
        for(const auto& Fixture:Region->Fixtures)Fixtures.Add(Fixture.Id,&Fixture);
    PreparedFixtures.Reset();PreparedControls.Reset();
    for(const auto& Pair:Fixtures)
    {
        const auto State=FixtureState(*Pair.Value);PreparedFixtures.Add(Pair.Key,State);
        if(State.Deleted)continue;
        PreparedControls.Add(Pair.Key);for(const auto& Id:Pair.Value->Controls)PreparedControls.Add(Id);
    }
    bFixturesDirty=false;
}
void AHeyZedLighting::PrepareSource(const FString& Id,const FHeyZedLightState& State)
{
    FSourcePresentation Prepared;Prepared.State=State;Prepared.Anchor=State.Position;Prepared.Rotation=FRotator(0,State.Yaw,0);Prepared.Model=Models.FindRef(Id);
    const auto* Binding=FixtureLibrary->Bindings.Find(State.Sprite);
    Prepared.Profile=FixtureLibrary->Profiles.Find(Binding?*Binding:State.Kind);
    if(Prepared.Profile)
    {
        if(State.Kind==TEXT("switch"))
        {
            // A procedural cube plate mounts by its back face on the canonical wall.
            // The native address is the tile centre; +X is the plate's front normal.
            const FVector Normal=Prepared.Rotation.RotateVector(FVector::ForwardVector);
            const FVector Plane=State.Position-Normal*(HeyZedMetrics::Tile*.5);
            const FVector Centre=Plane+Normal*(HeyZedMetrics::WallThickness*.5+50*Prepared.Profile->MeshScale.X)
                +FVector(0,0,Prepared.Profile->MeshOffset.Z);
            Prepared.Anchor=Centre-Prepared.Rotation.RotateVector(Prepared.Profile->MeshOffset);
        }
        Prepared.State.Position=Prepared.Anchor+Prepared.Rotation.RotateVector(Prepared.Profile->Emitter);
        Prepared.State.Direction=Prepared.Rotation.RotateVector(Prepared.Profile->Direction);
        if(State.Kind!=TEXT("held")){Prepared.State.Lumens=Prepared.Profile->Lumens;Prepared.State.Outer=Prepared.Profile->OuterAngle;}
    }
    else Prepared.State.Position=Prepared.Anchor+FVector(0,0,180);
    SourcePresentations.Add(Id,MoveTemp(Prepared));
}
void AHeyZedLighting::RefreshLights()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_RefreshLights);
    if(!FixtureLibrary || !Resources)return;
    const auto* PC=GetWorld()->GetFirstPlayerController(); const APawn* Pawn=PC?PC->GetPawn():nullptr;
    if (!Pawn) return;
    const FVector Player=Pawn->GetActorLocation();
    LastRefreshPosition=Player;
    struct FCandidate { const FString* Id; const FHeyZedLightState* State; float Distance; };
    RefreshPreparedFixtures();
    if(bSourcesDirty){SourcePresentations.Reset();for(const auto& Pair:States)PrepareSource(Pair.Key,Pair.Value);bSourcesDirty=false;}
    TArray<FCandidate> Candidates;
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_FixtureCandidates);
    for (const auto& Pair : PreparedFixtures)
    {
        const auto& State=Pair.Value;
        if (State.Deleted || !State.Enabled || State.Radius<=0) continue;
        const float Distance=FVector::Dist(Player,State.Position);
        Candidates.Add({&Pair.Key,&State,Distance});
    }
    }
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_NativeSources);
    for(auto& Pair:SourcePresentations)
    {
        auto& Cached=Pair.Value;auto& State=Cached.State;
        FVector Anchor=Cached.Anchor;FRotator Rotation=Cached.Rotation;
        const bool Held=State.Kind==TEXT("held");
        if(Held)
        {
            auto* Avatar=Cast<AHeyZedPawn>(const_cast<APawn*>(Pawn));
            if(!Avatar)continue;
            const bool Left=State.Hand==TEXT("left");Rotation=PC->GetControlRotation();
            Anchor=Avatar->CameraMode==0?Avatar->GetPawnViewLocation()+Rotation.RotateVector(FVector(20,Left?-18:18,-25)):
                Avatar->Body->GetSocketLocation(Left?TEXT("hand_l"):TEXT("hand_r"));
        }
        const float Distance=FVector::Dist(Player,Anchor);
        const bool Admitted=!State.Deleted&&!(State.Kind==TEXT("street")&&PreparedControls.Contains(Pair.Key))&&Distance-State.Radius<=LocalDistance;
        auto* Model=Cached.Model.Get();const auto* Profile=Cached.Profile;
        const auto* Native=States.Find(Pair.Key);
        const bool Replaced=!Held&&Native&&GetWorld()->GetSubsystem<UHeyZedFurnitureSubsystem>()->ReplacesTile(
            FIntVector(FMath::FloorToInt(Native->Position.X/100),FMath::FloorToInt(Native->Position.Y/100),FMath::RoundToInt(Native->Position.Z/HeyZedMetrics::Floor)),Native->Sprite);
        if(!Admitted||!Profile||Replaced)
        {
            if(Model){Model->Destroy();Models.Remove(Pair.Key);Cached.Model.Reset();Model=nullptr;}
            if(!Admitted)continue;
        }
        if(Profile&&!Replaced)
        {
            if(Model&&Cached.ModelRevision!=State.Revision&&!Held&&!Model->GetActorTransform().Equals(FTransform(Rotation,Anchor)))
            {Model->Destroy();Models.Remove(Pair.Key);Cached.Model.Reset();Model=nullptr;}
            const bool Created=!Model;
            if(Created){Model=GetWorld()->SpawnActor<AHeyZedWorldFixture>(AHeyZedWorldFixture::StaticClass(),Anchor,Rotation);Models.Add(Pair.Key,Model);Cached.Model=Model;}
            if(Created||Cached.ModelRevision!=State.Revision)
            {
                Model->Configure(State.Kind,*Profile,FixtureLibrary,State.Support);Model->Power(State.Enabled,State.Color);Cached.ModelRevision=State.Revision;
            }
            if(Held){Model->SetActorLocationAndRotation(Anchor,Rotation);State.Position=Anchor+Rotation.RotateVector(Profile->Emitter);State.Direction=Rotation.RotateVector(Profile->Direction);}
        }
        else if(Held)State.Position=Anchor+FVector(0,0,180);
        if(State.Enabled&&State.Radius>0)Candidates.Add({&Pair.Key,&State,Distance});
    }
    }
    for(auto& Pair:VisibilityTargets)Pair.Value=0;
    {
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedLighting_UpdateComponents);
    for (const auto& Candidate : Candidates)
    {
        const auto& State=*Candidate.State;const FString& Id=*Candidate.Id;
        auto* Light=Lights.FindRef(Id).Get();
        const auto* Previous=Presented.Find(Id);
        const bool ConfigurationChanged=!Previous||Previous->Position!=State.Position||Previous->Direction!=State.Direction||Previous->Color!=State.Color||Previous->Radius!=State.Radius||Previous->Outer!=State.Outer||Previous->Kind!=State.Kind;
        const bool Rect=State.Kind==TEXT("sign"),Spot=!Rect&&State.Outer>0;
        if (Light && (Rect!=Light->IsA<URectLightComponent>() || (!Rect&&Spot!=Light->IsA<USpotLightComponent>()))) { Light->DestroyComponent(); Light=nullptr; }
        const bool Created=!Light;
        if (Created)
        {
            if(Rect)Light=NewObject<URectLightComponent>(this);
            else Light=Spot ? NewObject<USpotLightComponent>(this) : NewObject<UPointLightComponent>(this);
            Light->SetupAttachment(RootComponent); Light->SetMobility(EComponentMobility::Movable);
            Light->IntensityUnits=ELightUnits::Lumens;
            Light->bAllowMegaLights=true;
            Light->MegaLightsShadowMethod=EMegaLightsShadowMethod::RayTracing;
            Light->SetCastShadows(true); Light->SetCastVolumetricShadow(true);
            Light->SetIntensity(0);
            Lights.Add(Id,Light);
        }
        if(Created||ConfigurationChanged)
        {
        Light->SetWorldLocation(State.Position); Light->SetWorldRotation(State.Direction.Rotation());
        Light->SetAttenuationRadius(State.Radius); Light->SetLightColor(State.Color);
        if (auto* RectLight=Cast<URectLightComponent>(Light)){RectLight->SetSourceWidth(112);RectLight->SetSourceHeight(32);}
        if (auto* Point=Cast<UPointLightComponent>(Light)) Point->SetSourceRadius(5);
        if (auto* Cone=Cast<USpotLightComponent>(Light)) { Cone->SetOuterConeAngle(State.Outer); Cone->SetInnerConeAngle(State.Outer*.65f); }
        }
        if(Created)Light->RegisterComponent();
        if(ConfigurationChanged||!Previous||Previous->Lumens!=State.Lumens)Presented.Add(Id,State);
        // Source power and occlusion never depend on a nearest-light rank.
        const float Target=State.Kind==TEXT("street") ? 1.f :
            1.f-FMath::SmoothStep(LocalDistance*.8f,LocalDistance,FMath::Max(0.f,Candidate.Distance-State.Radius));
        VisibilityTargets.Add(Id,Target);
        Light->SetIntensity(State.Lumens*VisibilityWeights.FindOrAdd(Id));
    }
    }
    for (auto It=Lights.CreateIterator();It;++It) if (VisibilityTargets.FindRef(It.Key())<=0)
    {
        // Native off/deletion is authoritative; an unloaded region fades out.
        const auto* State=States.Find(It.Key());
        bool Removed=State && (State->Deleted || !State->Enabled);
        if (const auto* Fixture=PreparedFixtures.Find(It.Key()))
        {
            Removed=Fixture->Deleted || !Fixture->Enabled;
        }
        if (!Removed && !Identity.IsEmpty() && VisibilityWeights.FindRef(It.Key())>0) continue;
        It.Value()->DestroyComponent(); Presented.Remove(It.Key()); VisibilityWeights.Remove(It.Key()); VisibilityTargets.Remove(It.Key()); It.RemoveCurrent();
    }
}

FString AHeyZedLighting::RequestHeldToggle()
{
    auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("action"),TEXT("held_toggle"));
    Command->SetStringField(TEXT("token"),CommandSession+FString::Printf(TEXT(":%llu"),++NextCommand));
    Command->SetNumberField(TEXT("generation"),Epoch);
    FString Text;FJsonSerializer::Serialize(Command,TJsonWriterFactory<>::Create(&Text));return Text;
}
FString AHeyZedLighting::RequestInteraction(const FVector& Position,const FVector& Forward)
{
    const FHeyZedLightState* Selected=nullptr;float Best=250;
    for(const auto& Pair:States)
    {
        const auto& State=Pair.Value;if(State.Deleted||!State.Interactable||State.Kind==TEXT("held")||State.Kind==TEXT("room")||State.Control.IsEmpty()||FMath::Abs(State.Position.Z-Position.Z)>100)continue;
        FVector Target=State.Position;
        if(FixtureLibrary)
        {
            const auto* Binding=FixtureLibrary->Bindings.Find(State.Sprite);
            if(const auto* Profile=FixtureLibrary->Profiles.Find(Binding?*Binding:State.Kind))
                Target+=FRotator(0,State.Yaw,0).RotateVector(Profile->MeshOffset);
        }
        FVector Difference=Target-Position;Difference.Z=0;
        const float Distance=Difference.Size();
        if(Distance>Best||FVector::DotProduct(Difference.GetSafeNormal(),Forward.GetSafeNormal2D())<.45f)continue;
        Best=Distance;Selected=&State;
    }
    if(!Selected)return FString();
    auto Command=MakeShared<FJsonObject>();Command->SetStringField(TEXT("action"),TEXT("light_toggle"));
    Command->SetStringField(TEXT("id"),Selected->Control);Command->SetNumberField(TEXT("generation"),Epoch);
    Command->SetStringField(TEXT("token"),CommandSession+FString::Printf(TEXT(":%llu"),++NextCommand));
    FString Text;FJsonSerializer::Serialize(Command,TJsonWriterFactory<>::Create(&Text));return Text;
}
