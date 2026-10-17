#include "HeyZedOverlay.h"
#include "HeyZedPawn.h"
#include "HeyZedPlayerController.h"
#include "HeyZedWorldSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/Canvas.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UHeyZedOverlay::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;
    auto* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* ButtonSlot = Root->AddChildToCanvas(Buttons);
    ButtonSlot->SetAnchors(FAnchors(1,1));ButtonSlot->SetAlignment(FVector2D(1,1));
    ButtonSlot->SetPosition(FVector2D(-4,-4)); ButtonSlot->SetAutoSize(true);
    auto AddButton = [&](UPanelWidget* Parent, const TCHAR* Text, FName Handler)
    {
        auto* Button = WidgetTree->ConstructWidget<UHeyZedButton>();
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        Button->SetBackgroundColor(FLinearColor(.04f, .05f, .06f, .95f));
        Button->AddChild(Label); Parent->AddChild(Button);
        FScriptDelegate Delegate; Delegate.BindUFunction(this, Handler); Button->OnClicked.Add(Delegate);
        return Label;
    };
    CameraLabel = AddButton(Buttons, TEXT("3"), FName(TEXT("Cycle")));
    AddButton(Buttons, TEXT("..."), FName(TEXT("Settings")));
    AddButton(Buttons, TEXT("Torch"), FName(TEXT("Torch")));
    Panel = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* PanelSlot = Root->AddChildToCanvas(Panel);
    PanelSlot->SetAnchors(FAnchors(1,1));PanelSlot->SetAlignment(FVector2D(1,1));
    PanelSlot->SetPosition(FVector2D(-4,-44)); PanelSlot->SetAutoSize(true);
    auto Row = [&](const TCHAR* Name, FName Decrease, FName Increase)
    {
        auto* Box = WidgetTree->ConstructWidget<UHorizontalBox>(); Panel->AddChild(Box);
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Box->AddChild(Label);
        AddButton(Box, TEXT(" - "), Decrease); AddButton(Box, TEXT(" + "), Increase);
        Label->SetText(FText::FromString(Name)); return Label;
    };
    NearLabel = Row(TEXT("Near"), FName(TEXT("NearDown")), FName(TEXT("NearUp")));
    FarLabel = Row(TEXT("Far"), FName(TEXT("FarDown")), FName(TEXT("FarUp")));
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    MetricsPanel = WidgetTree->ConstructWidget<UBorder>();
    MetricsPanel->SetBrushColor(FLinearColor(0,0,0,.8f));
    MetricsPanel->SetPadding(FMargin(3));
    MetricsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* MetricsSlot = Root->AddChildToCanvas(MetricsPanel);
    MetricsSlot->SetAnchors(FAnchors(0,1)); MetricsSlot->SetAlignment(FVector2D(0,1));
    MetricsSlot->SetPosition(FVector2D(4,-4)); MetricsSlot->SetAutoSize(true); MetricsSlot->SetZOrder(10000);
    MetricsLabel = WidgetTree->ConstructWidget<UTextBlock>();
    auto MetricsFont = MetricsLabel->GetFont(); MetricsFont.Size = 6; MetricsLabel->SetFont(MetricsFont);
    MetricsLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    MetricsPanel->AddChild(MetricsLabel);
    LoadingPanel=WidgetTree->ConstructWidget<UBorder>();
    LoadingPanel->SetBrush(*FCoreStyle::Get().GetBrush("WhiteBrush"));
    LoadingPanel->SetBrushColor(FLinearColor(.008f,.012f,.018f,1));
    LoadingPanel->SetHorizontalAlignment(HAlign_Center);LoadingPanel->SetVerticalAlignment(VAlign_Center);
    auto* LoadingSlot=Root->AddChildToCanvas(LoadingPanel);
    LoadingSlot->SetAnchors(FAnchors(0,0,1,1));LoadingSlot->SetOffsets(FMargin(0));LoadingSlot->SetZOrder(20000);
    LoadingLabel=WidgetTree->ConstructWidget<UTextBlock>();
    LoadingLabel->SetText(FText::FromString(TEXT("Waiting for PZ")));
    auto LoadingFont=LoadingLabel->GetFont();LoadingFont.Size=20;LoadingLabel->SetFont(LoadingFont);
    LoadingLabel->SetColorAndOpacity(FSlateColor(FLinearColor(.8f,.85f,.9f,1)));
    LoadingPanel->SetContent(LoadingLabel);
    return Super::RebuildWidget();
}

bool UHeyZedOverlay::PointerOverControls() const
{
    float X = 0, Y = 0;
    return GetOwningPlayer()->GetMousePosition(X,Y)&&PointerOverControls(X,Y);
}
bool UHeyZedOverlay::PointerOverControls(float X,float Y) const
{
    int32 Width=0,Height=0;GetOwningPlayer()->GetViewportSize(Width,Height);
    const float Scale=UWidgetLayoutLibrary::GetViewportScale(this);
    return X>Width-240*Scale&&Y>Height-(bSettings?150:44)*Scale;
}

void UHeyZedOverlay::UpdateLabels()
{
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();HeyZed&&LoadingPanel)
    {
        LoadingPanel->SetVisibility(HeyZed->ScenePresented()?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
        LoadingLabel->SetText(FText::FromString(HeyZed->LoadingStatus()));
    }
    auto* Pawn = GetOwningPlayerPawn<AHeyZedPawn>();
    if (!Pawn || !Pawn->Preferences || !CameraLabel) return;
    const TCHAR* Names[] = {TEXT("1"), TEXT("3")};
    CameraLabel->SetText(FText::FromString(Names[FMath::Clamp(Pawn->CameraMode,0,1)]));
    NearLabel->SetText(FText::FromString(FString::Printf(TEXT("Near %.0f cm"), Pawn->Preferences->Near)));
    FarLabel->SetText(FText::FromString(FString::Printf(TEXT("Far %.0f cm"), Pawn->Preferences->Far)));
    if (MetricsLabel && bDiagnostics)
        if (auto* HeyZed = GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())
        {
            auto Font = MetricsLabel->GetFont();
            const int32 Size = FMath::CeilToInt(5.5f/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this)));
            if (Font.Size != Size) { Font.Size = Size; MetricsLabel->SetFont(Font); }
            const FString& Text = HeyZed->TelemetryText();
            if (!MetricsLabel->GetText().ToString().Equals(Text)) MetricsLabel->SetText(FText::FromString(Text));
        }
}

void UHeyZedOverlay::Cycle() { if (auto* PC = Cast<AHeyZedPlayerController>(GetOwningPlayer())) PC->CycleCamera(); }
void UHeyZedOverlay::Settings() { bSettings = !bSettings; Panel->SetVisibility(bSettings ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
void UHeyZedOverlay::NearDown() { if (auto* Pawn = GetOwningPlayerPawn<AHeyZedPawn>()) Pawn->ChangeLimit(false, -5); }
void UHeyZedOverlay::NearUp() { if (auto* Pawn = GetOwningPlayerPawn<AHeyZedPawn>()) Pawn->ChangeLimit(false, 5); }
void UHeyZedOverlay::FarDown() { if (auto* Pawn = GetOwningPlayerPawn<AHeyZedPawn>()) Pawn->ChangeLimit(true, -15); }
void UHeyZedOverlay::FarUp() { if (auto* Pawn = GetOwningPlayerPawn<AHeyZedPawn>()) Pawn->ChangeLimit(true, 15); }

void AHeyZedHud::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();HeyZed&&!HeyZed->ScenePresented())return;
    auto* PC = Cast<AHeyZedPlayerController>(PlayerOwner);
    auto* Pawn = PC ? PC->GetPawn<AHeyZedPawn>() : nullptr;
    if (!Pawn) return;
    float X = Canvas->SizeX * .5f, Y = Canvas->SizeY * .5f;
    if (PC->PointerReleased())
    {
        if (!PC->GetMousePosition(X, Y)) return;
    }
    auto Stroke = [&](float A, float B, float C, float D)
    {
        DrawLine(X+A, Y+B, X+C, Y+D, FLinearColor::Black, 4);
        DrawLine(X+A, Y+B, X+C, Y+D, FLinearColor::White, 1.5);
    };
    if (PC->PointerReleased())
    {
        Stroke(0, 0, 0, 16); Stroke(0, 0, 11, 11); Stroke(0, 16, 4, 11); Stroke(4, 11, 11, 11);
    }
    else
    {
        Stroke(-10, 0, -4, 0); Stroke(4, 0, 10, 0); Stroke(0, -10, 0, -4); Stroke(0, 4, 0, 10);
    }
}

void UHeyZedOverlay::ToggleDiagnostics()
{
    bDiagnostics = !bDiagnostics;
    if (MetricsPanel) MetricsPanel->SetVisibility(bDiagnostics ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UHeyZedOverlay::Torch()
{
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->ToggleHeldLight();
}
