#include "HeyZedPlayerController.h"
#include "HeyZedPawn.h"
#include "HeyZedOverlay.h"
#include "HeyZedWorldSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "InputKeyEventArgs.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

namespace {
int32 NativeCode(FKey Key)
{
    if(Key==EKeys::I)return 0;
    const FString Name=Key.ToString();
    if(Name.Len()==1&&((Name[0]>='A'&&Name[0]<='Z')||(Name[0]>='0'&&Name[0]<='9')))return Name[0];
    if(Name.StartsWith(TEXT("F"))&&Name.Len()<=3&&Name.Len()>1&&Name.Mid(1).IsNumeric()){const int32 Number=FCString::Atoi(*Name.Mid(1));return Number>=1&&Number<=25?289+Number:0;}
    const TPair<FKey,int32> Codes[]={{EKeys::Zero,48},{EKeys::One,49},{EKeys::Two,50},{EKeys::Three,51},{EKeys::Four,52},{EKeys::Five,53},{EKeys::Six,54},{EKeys::Seven,55},{EKeys::Eight,56},{EKeys::Nine,57},{EKeys::SpaceBar,32},{EKeys::Enter,257},{EKeys::BackSpace,259},{EKeys::Delete,261},{EKeys::Insert,260},
        {EKeys::Right,262},{EKeys::Left,263},{EKeys::Down,264},{EKeys::Up,265},{EKeys::PageUp,266},{EKeys::PageDown,267},{EKeys::Home,268},{EKeys::End,269},
        {EKeys::LeftShift,340},{EKeys::LeftControl,341},{EKeys::LeftAlt,342},{EKeys::RightShift,344},{EKeys::RightControl,345},{EKeys::RightAlt,346},
        {EKeys::Hyphen,45},{EKeys::Equals,61},{EKeys::Tilde,96},{EKeys::NumPadZero,320},{EKeys::NumPadOne,321},{EKeys::NumPadTwo,322},{EKeys::NumPadThree,323},{EKeys::NumPadFour,324},{EKeys::NumPadFive,325},{EKeys::NumPadSix,326},{EKeys::NumPadSeven,327},{EKeys::NumPadEight,328},{EKeys::NumPadNine,329},{EKeys::Decimal,330},{EKeys::Multiply,332},{EKeys::Subtract,333},{EKeys::Add,334},{EKeys::Comma,44},{EKeys::Period,46},{EKeys::Slash,47},{EKeys::Semicolon,59},{EKeys::Quote,39},{EKeys::LeftBracket,91},{EKeys::RightBracket,93},{EKeys::Backslash,92},
        {EKeys::LeftMouseButton,10000},{EKeys::RightMouseButton,10001}};
    for(const auto& Pair:Codes)if(Pair.Key==Key)return Pair.Value;
    return 0;
}
}

void AHeyZedPlayerController::BeginPlay()
{
    Super::BeginPlay();
    Overlay = CreateWidget<UHeyZedOverlay>(this, UHeyZedOverlay::StaticClass());
    Overlay->AddToViewport(10000);
    RefreshPointer();
}

void AHeyZedPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    auto* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
    Context = NewObject<UInputMappingContext>(this);
    auto Make = [this](EInputActionValueType Type)
    {
        auto* Action = NewObject<UInputAction>(this); Action->ValueType = Type;
        Actions.Add(Action); return Action;
    };
    auto* LookAction = Make(EInputActionValueType::Axis2D);
    Context->MapKey(LookAction, EKeys::Mouse2D);
    Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHeyZedPlayerController::Look);
    ForwardAction = Make(EInputActionValueType::Axis1D);
    RightAction = Make(EInputActionValueType::Axis1D);
    RunAction = Make(EInputActionValueType::Boolean);
    SprintAction = Make(EInputActionValueType::Boolean);
    CrouchAction = Make(EInputActionValueType::Boolean);
    for (ETriggerEvent Event : {ETriggerEvent::Triggered, ETriggerEvent::Completed, ETriggerEvent::Canceled})
    {
        Input->BindAction(ForwardAction, Event, this, &AHeyZedPlayerController::Forward);
        Input->BindAction(RightAction, Event, this, &AHeyZedPlayerController::Right);
        Input->BindAction(RunAction, Event, this, &AHeyZedPlayerController::Run);
        Input->BindAction(SprintAction, Event, this, &AHeyZedPlayerController::Sprint);
        Input->BindAction(CrouchAction, Event, this, &AHeyZedPlayerController::Crouch);
    }
    auto BindButton = [&](FKey Key, auto Method)
    {
        auto* Action = Make(EInputActionValueType::Boolean); Context->MapKey(Action, Key);
        Input->BindAction(Action, ETriggerEvent::Started, this, Method);
    };
    BindButton(EKeys::I, &AHeyZedPlayerController::ToggleDiagnostics);
    BindButton(EKeys::Divide, &AHeyZedPlayerController::CycleCamera);
    BindButton(EKeys::Tab, &AHeyZedPlayerController::ReleasePointer);
    BindButton(EKeys::Escape, &AHeyZedPlayerController::ReleasePointer);
    BindButton(EKeys::LeftMouseButton, &AHeyZedPlayerController::Click);
    auto* MiddleAction = Make(EInputActionValueType::Boolean);
    Context->MapKey(MiddleAction, EKeys::MiddleMouseButton);
    for (ETriggerEvent Event : {ETriggerEvent::Started, ETriggerEvent::Completed, ETriggerEvent::Canceled})
        Input->BindAction(MiddleAction, Event, this, &AHeyZedPlayerController::Middle);
    auto* ZoomAction = Make(EInputActionValueType::Axis1D);
    Context->MapKey(ZoomAction, EKeys::MouseWheelAxis);
    Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &AHeyZedPlayerController::Wheel);
    MapMovementKeys({87, 83, 65, 68, 160, 164});
    if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
        Subsystem->AddMappingContext(Context, 0);
}

void AHeyZedPlayerController::MapMovementKeys(const TArray<int32>& Keys)
{
    if (!Context || (Keys.Num() != 6 && Keys.Num() != 7)) return;
    for (UInputAction* Action : {ForwardAction.Get(), RightAction.Get(), RunAction.Get(), SprintAction.Get(), CrouchAction.Get()}) Context->UnmapAllKeysFromAction(Action);
    auto Key = [](int32 Code) -> FKey
    {
        if (Code >= 65 && Code <= 90) return FKey(FName(*FString::Chr(Code)));
        if(Code>=48&&Code<=57){const FKey Digits[]={EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};return Digits[Code-48];}
        switch (Code)
        {
            case 160: case 340: return EKeys::LeftShift; case 161: case 344: return EKeys::RightShift;
            case 162: case 341: return EKeys::LeftControl; case 163: case 345: return EKeys::RightControl;
            case 164: case 342: return EKeys::LeftAlt; case 165: case 346: return EKeys::RightAlt;
            case 32: return EKeys::SpaceBar; case 38: return EKeys::Up; case 40: return EKeys::Down;
            case 37: case 263: return EKeys::Left; case 39: case 262: return EKeys::Right;
            case 265: return EKeys::Up; case 264: return EKeys::Down;
            default: return EKeys::Invalid;
        }
    };
    for (int32 Index = 0; Index < Keys.Num(); ++Index)
    {
        const FKey Binding = Key(Keys[Index]);
        if (!Binding.IsValid()) continue;
        UInputAction* Action = Index < 2 ? ForwardAction.Get() : Index < 4 ? RightAction.Get() : Index == 4 ? RunAction.Get() : Index == 5 ? SprintAction.Get() : CrouchAction.Get();
        auto& Mapping = Context->MapKey(Action, Binding);
        if (Index == 1 || Index == 2) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
    }
    if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer())) Subsystem->RequestRebuildControlMappings();
}

void AHeyZedPlayerController::Look(const FInputActionValue& Value)
{
    auto* Avatar = GetPawn<AHeyZedPawn>();
    if (!Avatar || bReleased) return;
    const FVector2D Mouse = Value.Get<FVector2D>();
    FRotator Rotation = GetControlRotation();
    Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + Mouse.X * .12);
    Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch + Mouse.Y * .12), Avatar->CameraMode==0?-75.:-85., 85.);
    SetControlRotation(Rotation);
}
void AHeyZedPlayerController::Forward(const FInputActionValue& Value) { ForwardValue = Value.Get<float>(); }
void AHeyZedPlayerController::Right(const FInputActionValue& Value) { RightValue = Value.Get<float>(); }
void AHeyZedPlayerController::Run(const FInputActionValue& Value) { bRun = Value.Get<bool>(); }
void AHeyZedPlayerController::Sprint(const FInputActionValue& Value) { bSprint = Value.Get<bool>(); }
void AHeyZedPlayerController::Crouch(const FInputActionValue& Value) { bCrouch = Value.Get<bool>(); }
void AHeyZedPlayerController::Wheel(const FInputActionValue& Value) { if (auto* Avatar = GetPawn<AHeyZedPawn>(); Avatar && (!Overlay || !Overlay->PointerOverControls())) Avatar->Zoom(Value.Get<float>()); }
void AHeyZedPlayerController::Middle(const FInputActionValue& Value) { bMiddle = !bReleased && Value.Get<bool>(); RefreshPointer(); }

void AHeyZedPlayerController::ReleasePointer()
{
    bVerificationMotionActive = false;
    if(bVerificationContextRemoved){
        if(auto* Subsystem=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))Subsystem->AddMappingContext(Context,VerificationContextPriority);
        bVerificationContextRemoved=false;
    }
    NativeHeldKeys.Reset();
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->SendNativeKey(-1,false);
    bReleased = true; bMiddle = false; ForwardValue = RightValue = 0; bRun = bSprint = bCrouch = false;
    RefreshPointer();
}

bool AHeyZedPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    const bool Down=Params.Event==IE_Pressed;
    const int32 Code=NativeCode(Params.Key);
    const bool OverControls=Params.Key.IsMouseButton()&&Overlay&&Overlay->PointerOverControls();
    const auto* StartupHeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();
    if(StartupHeyZed&&StartupHeyZed->GameplayReady()&&Code&&((Down&&!bReleased&&!OverControls)||(Params.Event==IE_Released&&NativeHeldKeys.Contains(Code))))
    {
        if(Down)NativeHeldKeys.Add(Code);else NativeHeldKeys.Remove(Code);
        if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>())HeyZed->SendNativeKey(Code,Down);
    }
    return Super::InputKey(Params);
}
void AHeyZedPlayerController::ToggleDiagnostics() { if (Overlay) Overlay->ToggleDiagnostics(); }
bool AHeyZedPlayerController::PointerOverHeyZedControls(float X,float Y) const {return Overlay&&Overlay->PointerOverControls(X,Y);}

void AHeyZedPlayerController::Click()
{
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();!HeyZed||!HeyZed->GameplayReady())return;
    if (Overlay && Overlay->PointerOverControls()) return;
    bReleased = false; RefreshPointer();
}

void AHeyZedPlayerController::CycleCamera() { if (auto* Avatar = GetPawn<AHeyZedPawn>()) Avatar->CycleCamera(); bMiddle = false; RefreshPointer(); }

void AHeyZedPlayerController::RefreshPointer()
{
    const auto* Avatar = GetPawn<AHeyZedPawn>();
    const auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();
    const bool Capture = !bReleased && Avatar && HeyZed && HeyZed->GameplayReady();
    if (bPointerInitialized && bCaptured == Capture) return;
    bPointerInitialized = true; bCaptured = Capture; bShowMouseCursor = !Capture;
    if (Capture)
    {
        FInputModeGameOnly Mode; Mode.SetConsumeCaptureMouseDown(true); SetInputMode(Mode);
    }
    else
    {
        FInputModeGameAndUI Mode; Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode);
    }
}

int32 AHeyZedPlayerController::IntentMask() const
{
    if(auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();!HeyZed||!HeyZed->GameplayReady())return 0;
    if (bReleased) return 0;
    const int32 Movement=(ForwardValue > .1 ? 1 : ForwardValue < -.1 ? 2 : 0) | (RightValue < -.1 ? 4 : RightValue > .1 ? 8 : 0);
    bool Run=bRun, Sprint=bSprint;
    if (const auto* Avatar=GetPawn<AHeyZedPawn>(); Avatar && Avatar->CameraMode==0)
    {
        const int32 Forward=(Movement&1 ? 1 : 0)-(Movement&2 ? 1 : 0);
        const int32 Right=(Movement&8 ? 1 : 0)-(Movement&4 ? 1 : 0);
        Sprint=bSprint && Forward>0 && FMath::Abs(Right)<=Forward;
        Run=bRun && Movement!=0 && Forward>=0;
    }
    return Movement | (Run || Sprint ? 16 : 0) | (Sprint ? 32 : 0) | (bCrouch ? 64 : 0);
}

float AHeyZedPlayerController::ActionYaw() const
{
    return GetControlRotation().Yaw;
}

void AHeyZedPlayerController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    if(!bVerificationMotionActive&&!bReleased&&GetLocalPlayer()&&GetLocalPlayer()->ViewportClient&&GetLocalPlayer()->ViewportClient->Viewport&&!GetLocalPlayer()->ViewportClient->Viewport->HasFocus())ReleasePointer();
    if (Overlay) Overlay->UpdateLabels();
}

void AHeyZedPlayerController::InjectVerificationLook()
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeyZedCameraTest")) || Actions.IsEmpty()) return;
    // Use Unreal's input test path; do not capture the background test window's OS pointer.
    bReleased = false;
    if (auto* Input = Cast<UEnhancedPlayerInput>(PlayerInput)) Input->InjectInputForAction(Actions[0], FInputActionValue(FVector2D(750, -125)));
}

void AHeyZedPlayerController::InjectVerificationMotion(int32 Mask)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("HeyZedLiveMotionTest")) && !FParse::Param(FCommandLine::Get(),TEXT("HeyZedStreetProbe"))) return;
    if(!bVerificationMotionActive){
        if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedProbeFirstPerson")))CycleCamera();
        if(auto* Subsystem=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer())){
            bVerificationContextRemoved=Subsystem->HasMappingContext(Context,VerificationContextPriority);
            if(bVerificationContextRemoved){FModifyContextOptions Options;Options.bForceImmediately=true;Subsystem->RemoveMappingContext(Context,Options);}
        }
        if(PlayerInput)PlayerInput->FlushPressedKeys();
        ForwardValue=RightValue=0;bRun=bSprint=bCrouch=false;
    }
    bVerificationMotionActive = true;
    bReleased=false;
    SetControlRotation(FRotator(0,0,0));
    if (auto* Input=Cast<UEnhancedPlayerInput>(PlayerInput))
    {
        Input->InjectInputForAction(ForwardAction,FInputActionValue(Mask&1 ? 1.f : Mask&2 ? -1.f : 0.f));
        Input->InjectInputForAction(RightAction,FInputActionValue(0.f));
        Input->InjectInputForAction(RunAction,FInputActionValue((Mask&16)!=0));
        Input->InjectInputForAction(SprintAction,FInputActionValue((Mask&32)!=0));
        Input->InjectInputForAction(CrouchAction,FInputActionValue((Mask&64)!=0));
    }
}
