#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SaveGame.h"
#include "HeyZedPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class USkeletalMeshComponent;

UCLASS()
class UHeyZedPreferences : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) float Near = 45;
    UPROPERTY(SaveGame) float Far = 300;
    UPROPERTY(SaveGame) float Distance = 140;
    UPROPERTY(SaveGame) float Fov = 95;
    UPROPERTY(SaveGame) float SideNear = 38;
    UPROPERTY(SaveGame) float SideFar = 48;
    UPROPERTY(SaveGame) float UpNear = 8;
    UPROPERTY(SaveGame) float UpFar = 12;
    UPROPERTY(SaveGame) float BodyClearance = 55;
};

UCLASS()
class AHeyZedPawn : public APawn
{
    GENERATED_BODY()
public:
    AHeyZedPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual FVector GetPawnViewLocation() const override;
    void Adopt(const FVector& Position, float Yaw, int32 MovementMode, const FVector& Velocity = FVector::ZeroVector, float TurnRate = 0, bool Crouched = false, bool ConfirmedMotion = false);
    virtual FVector GetVelocity() const override { return PoseVelocity; }
    void CycleCamera();
    void Zoom(float Steps);
    void ChangeLimit(bool FarLimit, float Delta);
    void SavePreferences();
    float MovementYaw() const;
    int32 CameraMode = 1;
    UPROPERTY(EditDefaultsOnly, Category="Camera", meta=(ClampMin="0")) float StandingEyeHeight = 160.f;
    UPROPERTY(EditDefaultsOnly, Category="Camera", meta=(ClampMin="0")) float CrouchedEyeHeight = 105.f;
    UPROPERTY(EditDefaultsOnly, Category="Camera", meta=(ClampMin="0")) float EyeHeightBlendSpeed = 12.f;
    UPROPERTY(EditDefaultsOnly, Category="Camera", meta=(ClampMin="0")) float EyeForwardOffset = 18.f;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") FVector NativeVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") FVector PoseVelocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float ActualSpeed = 0;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float NativeTurnRate = 0;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float DesiredFacingYaw = 0;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") int32 Gait = 0;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bCrouched = false;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool bConfirmedMotion = false;
    UPROPERTY() TObjectPtr<UHeyZedPreferences> Preferences;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> FirstPersonBody;
    bool bNativeAppearance=false;
    bool bNativeSeated=false;
    FTransform NativeSeatTransform;
    FVector NativeSeatEyeLocation=FVector::ZeroVector;
private:
    FVector Target = FVector::ZeroVector;
    float TargetYaw = 0;
    double LastPoseState = 0;
    bool bEyeHeightInitialized = false;
    bool bFirstPersonTravelInitialized=false;
    FVector FirstPersonTravel=FVector::ZeroVector,FirstPersonTravelVelocity=FVector::ZeroVector;
    FRotator PerspectiveLook = FRotator(-15, -135, 0);
};
