#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "GameFramework/HUD.h"
#include "HeyZedOverlay.generated.h"

class UTextBlock;
class UBorder;
class UVerticalBox;

UCLASS()
class UHeyZedButton : public UButton
{
    GENERATED_BODY()
public:
    UHeyZedButton() { InitIsFocusable(false); }
};

UCLASS()
class UHeyZedOverlay : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    void UpdateLabels();
    void ToggleDiagnostics();
    bool PointerOverControls() const;
    bool PointerOverControls(float X,float Y) const;
private:
    UFUNCTION() void Cycle();
    UFUNCTION() void Settings();
    UFUNCTION() void Torch();
    UFUNCTION() void NearDown();
    UFUNCTION() void NearUp();
    UFUNCTION() void FarDown();
    UFUNCTION() void FarUp();
    UPROPERTY() TObjectPtr<UBorder> LoadingPanel;
    UPROPERTY() TObjectPtr<UTextBlock> LoadingLabel;
    UPROPERTY() TObjectPtr<UTextBlock> CameraLabel;
    UPROPERTY() TObjectPtr<UTextBlock> NearLabel;
    UPROPERTY() TObjectPtr<UTextBlock> FarLabel;
    UPROPERTY() TObjectPtr<UTextBlock> MetricsLabel;
    UPROPERTY() TObjectPtr<UVerticalBox> Panel;
    UPROPERTY() TObjectPtr<UBorder> MetricsPanel;
    bool bDiagnostics = true;
    bool bSettings = false;
};

UCLASS()
class AHeyZedHud : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
