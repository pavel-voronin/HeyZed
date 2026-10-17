#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "HeyZedPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UHeyZedOverlay;

UCLASS()
class AHeyZedPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float Delta) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void ReleasePointer();
    UFUNCTION() void CycleCamera();
    void RefreshPointer();
    void MapMovementKeys(const TArray<int32>& Keys);
    int32 IntentMask() const;
    float ActionYaw() const;
    bool PointerReleased() const { return bReleased; }
    bool PointerOverHeyZedControls(float X,float Y) const;
    void InjectVerificationLook();
    void InjectVerificationMotion(int32 Mask);
private:
    void Look(const FInputActionValue& Value);
    void Forward(const FInputActionValue& Value);
    void Right(const FInputActionValue& Value);
    void Run(const FInputActionValue& Value);
    void Sprint(const FInputActionValue& Value);
    void Crouch(const FInputActionValue& Value);
    void Wheel(const FInputActionValue& Value);
    void Middle(const FInputActionValue& Value);
    void Click();
    TSet<int32> NativeHeldKeys;
    void ToggleDiagnostics();
    UPROPERTY() TObjectPtr<UInputMappingContext> Context;
    UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
    UPROPERTY() TObjectPtr<UHeyZedOverlay> Overlay;
    UPROPERTY() TObjectPtr<UInputAction> ForwardAction;
    UPROPERTY() TObjectPtr<UInputAction> RightAction;
    UPROPERTY() TObjectPtr<UInputAction> RunAction;
    UPROPERTY() TObjectPtr<UInputAction> SprintAction;
    UPROPERTY() TObjectPtr<UInputAction> CrouchAction;
    float ForwardValue = 0;
    float RightValue = 0;
    bool bRun = false, bSprint = false, bCrouch = false, bMiddle = false, bReleased = true;
    bool bVerificationMotionActive = false;
    bool bVerificationContextRemoved = false;
    int32 VerificationContextPriority = 0;
    bool bCaptured = false;
    bool bPointerInitialized = false;
};
