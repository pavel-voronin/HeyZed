#include "HeyZedPawn.h"
#include "HeyZedMetrics.h"
#include "HeyZedMotion.h"
#include "HeyZedFirstPerson.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static FString CameraSlot()
{
    return (FParse::Param(FCommandLine::Get(), TEXT("HeyZedCameraTest")) || FParse::Param(FCommandLine::Get(),TEXT("HeyZedMotionTest")) || FParse::Param(FCommandLine::Get(),TEXT("HeyZedInteractionTest")) || FParse::Param(FCommandLine::Get(),TEXT("HeyZedLiveMotionTest"))) ? TEXT("heyzed-camera-test") : TEXT("heyzed-camera");
}

AHeyZedPawn::AHeyZedPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    BaseEyeHeight = StandingEyeHeight;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Character"));
    Body->PrimaryComponentTick.AddPrerequisite(this,PrimaryActorTick);
    Body->SetupAttachment(RootComponent);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCastShadow(true);
    Body->bCastDynamicShadow=true;
    Body->bCastHiddenShadow=true;
    FirstPersonBody=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonBody"));
    FirstPersonBody->SetupAttachment(Body);
    FirstPersonBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonBody->SetOnlyOwnerSee(true);
    FirstPersonBody->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    FirstPersonBody->SetHiddenInGame(true);
    FirstPersonBody->PrimaryComponentTick.AddPrerequisite(Body,Body->PrimaryComponentTick);
    Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->bUsePawnControlRotation = true;
    Arm->bDoCollisionTest = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
    Camera->bConstrainAspectRatio = false;
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
}

void AHeyZedPawn::BeginPlay()
{
    Super::BeginPlay();
    Preferences = Cast<UHeyZedPreferences>(UGameplayStatics::LoadGameFromSlot(CameraSlot(), 0));
    if (!Preferences)
    {
        Preferences = Cast<UHeyZedPreferences>(UGameplayStatics::CreateSaveGameObject(UHeyZedPreferences::StaticClass()));
        FString Properties;
        if (FFileHelper::LoadFileToString(Properties, *(FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE")), TEXT("Zomboid/Lua/HEYZED.properties")))))
        {
            TArray<FString> Lines; Properties.ParseIntoArrayLines(Lines);
            for (const FString& Line : Lines)
            {
                FString Key, Value;
                if (!Line.Split(TEXT("="), &Key, &Value)) continue;
                if (Key == TEXT("cameraNear")) Preferences->Near = FCString::Atof(*Value);
                if (Key == TEXT("cameraFar")) Preferences->Far = FCString::Atof(*Value);
                if (Key == TEXT("firstPersonFov")) Preferences->Fov = FCString::Atof(*Value);
                if (Key == TEXT("cameraNearSide")) Preferences->SideNear = FCString::Atof(*Value);
                if (Key == TEXT("cameraFarSide")) Preferences->SideFar = FCString::Atof(*Value);
                if (Key == TEXT("cameraNearUp")) Preferences->UpNear = FCString::Atof(*Value);
                if (Key == TEXT("cameraFarUp")) Preferences->UpFar = FCString::Atof(*Value);
                if (Key == TEXT("cameraBodyClearance")) Preferences->BodyClearance = FCString::Atof(*Value);
            }
        }
    }
    Preferences->Near = FMath::Clamp(Preferences->Near, 20.f, 2995.f);
    Preferences->Far = FMath::Clamp(Preferences->Far, Preferences->Near + 5, 3000.f);
    Preferences->Fov = FMath::Clamp(Preferences->Fov, 60.f, 120.f);
    Body->SetHiddenInGame(true);
    FirstPersonBody->SetHiddenInGame(true);
    if (Controller) Controller->SetControlRotation(PerspectiveLook);
    Zoom(0);
}

void AHeyZedPawn::Adopt(const FVector& Position, float Yaw, int32 MovementMode, const FVector& Velocity, float TurnRate, bool Crouched, bool ConfirmedMotion)
{
    Target = Position; TargetYaw = Yaw;
    NativeVelocity = Velocity; Gait = FMath::Clamp(MovementMode,0,3);
    bCrouched = Crouched; bConfirmedMotion = ConfirmedMotion;
    if (!bEyeHeightInitialized)
    {
        BaseEyeHeight = bCrouched ? CrouchedEyeHeight : StandingEyeHeight;
        bEyeHeightInitialized = true;
    }
    LastPoseState = FPlatformTime::Seconds();
}

void AHeyZedPawn::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Preferences) return;
    const float Alpha = 1 - FMath::Exp(-18 * FMath::Min(Delta, .1f));
    const FVector Before = GetActorLocation();
    const bool Teleport = FVector::DistSquared(Before, Target) > FMath::Square(400.);
    const double StateAge=FMath::Max(0.,FPlatformTime::Seconds()-LastPoseState);
    SetActorLocation(Teleport ? Target : FMath::Lerp(Before, Target, Alpha));
    PoseVelocity = !Teleport && Delta > SMALL_NUMBER ? (GetActorLocation()-Before)/Delta : FVector::ZeroVector;
    static const bool DiagnoseMotion=FParse::Param(FCommandLine::Get(),TEXT("HeyZedLocomotionDiagnostics"));
    if(DiagnoseMotion)
        UE_LOG(LogTemp,Display,TEXT("heyzed movement t=%.6f dt=%.6f source=%.6f age=%.6f target=%.4f,%.4f visible=%.4f,%.4f native_speed=%.4f visible_speed=%.4f confirmed=%d gait=%d teleport=%d"),
            FPlatformTime::Seconds(),Delta,LastPoseState,StateAge,Target.X,Target.Y,GetActorLocation().X,GetActorLocation().Y,NativeVelocity.Size2D(),PoseVelocity.Size2D(),bConfirmedMotion,Gait,Teleport);
    ActualSpeed = PoseVelocity.Size2D();
    if (FPlatformTime::Seconds()-LastPoseState > .25) { NativeVelocity = FVector::ZeroVector; Gait = 0; NativeTurnRate = 0; }
    const float Yaw = Body->GetRelativeRotation().Yaw;
    const float NextYaw = CameraMode==0 ? FRotator::NormalizeAxis(MovementYaw()-90.f) : FMath::FixedTurn(Yaw,TargetYaw,360.f*Delta);
    const float TurnSample = Delta > SMALL_NUMBER ? FMath::FindDeltaAngleDegrees(Yaw,NextYaw)/Delta : 0;
    NativeTurnRate = FMath::Lerp(NativeTurnRate,TurnSample,Alpha);
    Body->SetRelativeRotation(FRotator(0,NextYaw,0));
    const float Direction = ActualSpeed > 1 ? FMath::FindDeltaAngleDegrees(Body->GetRelativeRotation().Yaw+90, PoseVelocity.Rotation().Yaw) : 0;
    if (auto* Animation = Body->GetSingleNodeInstance()) Animation->SetBlendSpacePosition(FVector(ActualSpeed, Direction, 0));
    if(CameraMode==0&&!bNativeSeated)
    {
        const FVector Velocity=FPlatformTime::Seconds()-LastPoseState<.25?NativeVelocity:FVector::ZeroVector;
        if(!bFirstPersonTravelInitialized||Teleport){FirstPersonTravel=Target;FirstPersonTravelVelocity=Velocity;bFirstPersonTravelInitialized=true;}
        FirstPersonTravelVelocity=FMath::VInterpTo(FirstPersonTravelVelocity,Velocity,Delta,8.f);
        const FVector Goal=Target+FirstPersonTravelVelocity*FMath::Clamp(FPlatformTime::Seconds()-LastPoseState,0.,.08);
        FirstPersonTravel+=FirstPersonTravelVelocity*FMath::Min(Delta,.05f)+(Goal-FirstPersonTravel)*(1-FMath::Exp(-8.f*FMath::Min(Delta,.05f)));
        FirstPersonTravel=Goal+(FirstPersonTravel-Goal).GetClampedToMaxSize(15.f);
    }
    else bFirstPersonTravelInitialized=false;
    const float TargetEyeHeight = bCrouched ? CrouchedEyeHeight : StandingEyeHeight;
    const float EyeBlend = 1.f-FMath::Exp(-EyeHeightBlendSpeed*FMath::Max(Delta,0.f));
    BaseEyeHeight = Teleport ? TargetEyeHeight : FMath::Lerp(BaseEyeHeight,TargetEyeHeight,EyeBlend);
    Arm->SetRelativeLocation(FVector(0, 0, CameraMode == 0 ? BaseEyeHeight : 115));
    Arm->TargetArmLength = CameraMode == 0 ? 0 : Preferences->Distance;
    Arm->SocketOffset = FVector::ZeroVector;
    if (CameraMode == 1)
    {
        const float Blend = FMath::Clamp((Preferences->Distance-Preferences->Near)/(Preferences->Far-Preferences->Near), 0.f, 1.f);
        const float Side = FMath::Lerp(Preferences->SideNear, Preferences->SideFar, Blend);
        Arm->SocketOffset = FVector(0, Side, FMath::Lerp(Preferences->UpNear, Preferences->UpFar, Blend));
        const float Clearance = FMath::Sqrt(FMath::Max(0.f, FMath::Square(Preferences->BodyClearance)-FMath::Square(Side)));
        Arm->TargetArmLength = FMath::Max(Arm->TargetArmLength, Clearance);
        const FRotator ViewRotation=Controller?Controller->GetControlRotation():PerspectiveLook;
        const FVector Offset=ViewRotation.RotateVector(FVector(-Arm->TargetArmLength,0,0)+Arm->SocketOffset);
        const float AvailableHeight=FMath::Max(0.f,Arm->GetRelativeLocation().Z-Arm->ProbeSize);
        if(Offset.Z<-AvailableHeight)
        {
            const float Retraction=AvailableHeight/-Offset.Z;
            Arm->TargetArmLength*=Retraction;
            Arm->SocketOffset*=Retraction;
        }
    }
    Body->SetHiddenInGame(true);
    Body->bCastHiddenShadow=!bNativeAppearance;
    const bool FirstPerson=CameraMode==0;
    const auto BodyType=EFirstPersonPrimitiveType::None;
    Body->SetOwnerNoSee(FirstPerson);
    Body->SetCastHiddenShadow(!bNativeAppearance);
    if (Body->FirstPersonPrimitiveType!=BodyType) Body->SetFirstPersonPrimitiveType(BodyType);
    FirstPersonBody->SetHiddenInGame(true);
    Camera->SetFieldOfView(Preferences->Fov);
    Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
}

void AHeyZedPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Super::CalcCamera(DeltaTime, OutResult);
    if (CameraMode != 0 || !Body) return;
    OutResult.Location = GetPawnViewLocation();
    OutResult.Rotation = Controller ? Controller->GetControlRotation() : PerspectiveLook;
    OutResult.PerspectiveNearClipPlane = 2.f;
    OutResult.bUseFirstPersonParameters=true;
    OutResult.FirstPersonFOV=Preferences->Fov;
    OutResult.FirstPersonScale=1.f;
}

FVector AHeyZedPawn::GetPawnViewLocation() const
{
    if(bNativeSeated&&Arm&&Arm->GetAttachParent())return Arm->GetAttachParent()->GetComponentTransform().TransformPosition(NativeSeatTransform.InverseTransformPosition(NativeSeatEyeLocation));
    // Eye position belongs to the gameplay stance, independent of locomotion bones.
    const FVector Forward = FRotator(0,MovementYaw(),0).Vector();
    const FVector Location=CameraMode==0&&bFirstPersonTravelInitialized?FirstPersonTravel:GetActorLocation();
    return Location+FVector(0,0,BaseEyeHeight)+Forward*EyeForwardOffset;
}

void AHeyZedPawn::CycleCamera()
{
    if (Controller)
    {
        PerspectiveLook=Controller->GetControlRotation();
        CameraMode=CameraMode==0?1:0;
        bFirstPersonTravelInitialized=false;
        PerspectiveLook.Pitch=FMath::Clamp(FRotator::NormalizeAxis(PerspectiveLook.Pitch),CameraMode==0?-75.f:-85.f,85.f);
        Controller->SetControlRotation(PerspectiveLook);
    }
}

void AHeyZedPawn::Zoom(float Steps)
{
    if (!Preferences) return;
    if (CameraMode == 1) Preferences->Distance = FMath::Clamp(Preferences->Distance - Steps * 15, Preferences->Near, Preferences->Far);
    SavePreferences();
}

void AHeyZedPawn::ChangeLimit(bool FarLimit, float Delta)
{
    if (FarLimit) Preferences->Far = FMath::Clamp(Preferences->Far + Delta, Preferences->Near + 5, 3000.f);
    else Preferences->Near = FMath::Clamp(Preferences->Near + Delta, 20.f, Preferences->Far - 5);
    Preferences->Distance = FMath::Clamp(Preferences->Distance, Preferences->Near, Preferences->Far);
    SavePreferences();
}

void AHeyZedPawn::SavePreferences() { UGameplayStatics::SaveGameToSlot(Preferences, CameraSlot(), 0); }
float AHeyZedPawn::MovementYaw() const { return Controller ? Controller->GetControlRotation().Yaw : -135.f; }
