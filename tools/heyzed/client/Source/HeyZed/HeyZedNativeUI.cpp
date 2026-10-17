#include "HeyZedNativeUI.h"
#include "HeyZedPlayerController.h"
#include "HeyZedWorldSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "RenderUtils.h"
#include <atomic>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "ID3D12DynamicRHI.h"
#include <wrl/client.h>
#endif

class SHeyZedNativeUI : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHeyZedNativeUI) {} SLATE_END_ARGS()
    void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
    void SetTexture(UTextureRenderTarget2D* Texture) { Brush.SetResourceObject(Texture);Brush.ImageSize=FVector2D(Texture->SizeX,Texture->SizeY); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const override
    {
        if(Brush.GetResourceObject())FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&Brush,ESlateDrawEffect::PreMultipliedAlpha|ESlateDrawEffect::NoGamma,FLinearColor::White);
        return Layer;
    }
private:
    FSlateBrush Brush;
};

struct FHeyZedNativeUIState
{
#if PLATFORM_WINDOWS
    template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
    struct FSlot { ComPtr<ID3D12Resource> Resource;ComPtr<ID3D12Fence> Fence;FTextureRHIRef Texture;HANDLE TextureHandle=nullptr,FenceHandle=nullptr;uint64 Pending=0; };
    FSlot Slots[3];
    HANDLE File=INVALID_HANDLE_VALUE,Mapping=nullptr;
    uint8* Data=nullptr;
    uint64 Generation=0;
    std::atomic<uint64> Frames{0};
    std::atomic<uint64> PresentedGeneration{0};
    std::atomic<bool> Ready{false},Failed{false};
    ~FHeyZedNativeUIState()
    {
        for(auto& Slot:Slots){if(Slot.TextureHandle)CloseHandle(Slot.TextureHandle);if(Slot.FenceHandle)CloseHandle(Slot.FenceHandle);}
        if(Data)UnmapViewOfFile(Data);if(Mapping)CloseHandle(Mapping);if(File!=INVALID_HANDLE_VALUE)CloseHandle(File);
    }
    uint64 Load(int32 Offset) const {return reinterpret_cast<const std::atomic<uint64>*>(Data+Offset)->load(std::memory_order_acquire);}
    void Store(int32 Offset,uint64 Value) {reinterpret_cast<std::atomic<uint64>*>(Data+Offset)->store(Value,std::memory_order_release);}
    int32& Int(int32 Offset) {return *reinterpret_cast<int32*>(Data+Offset);}
    bool Open()
    {
        const FString Folder=FPaths::Combine(FPlatformProcess::UserHomeDir(),TEXT(".heyzed/bridge"));IFileManager::Get().MakeDirectory(*Folder,true);
        const FString Path=FPaths::Combine(Folder,TEXT("native-ui.control"));
        File=CreateFileW(*Path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(File==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER Size;Size.QuadPart=8192;SetFilePointerEx(File,Size,nullptr,FILE_BEGIN);SetEndOfFile(File);
        Mapping=CreateFileMappingW(File,nullptr,PAGE_READWRITE,0,8192,nullptr);if(!Mapping)return false;
        Data=static_cast<uint8*>(MapViewOfFile(Mapping,FILE_MAP_ALL_ACCESS,0,0,8192));if(!Data)return false;
        FMemory::Memzero(Data,8192);Int(0)=0x31495550;Int(4)=1;Int(8)=FPlatformProcess::GetCurrentProcessId();return true;
    }
    void Create(FRHICommandListImmediate& RHICmdList,FIntPoint Size,uint64 Revision)
    {
        Ready=false;Failed=false;Store(16,0);Generation=Revision;
        auto* RHI=GetID3D12DynamicRHI();auto* Device=RHI->RHIGetDevice_NoMGPU();
        for(int32 Index=0;Index<3;++Index)
        {
            auto& Slot=Slots[Index];Slot.Texture.SafeRelease();Slot.Resource.Reset();Slot.Fence.Reset();Slot.Pending=0;
            if(Slot.TextureHandle)CloseHandle(Slot.TextureHandle);if(Slot.FenceHandle)CloseHandle(Slot.FenceHandle);Slot.TextureHandle=Slot.FenceHandle=nullptr;
            D3D12_HEAP_PROPERTIES Heap{};Heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC Desc{};Desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;Desc.Width=Size.X;Desc.Height=Size.Y;Desc.DepthOrArraySize=1;Desc.MipLevels=1;Desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;Desc.SampleDesc.Count=1;Desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            HRESULT Result=Device->CreateCommittedResource(&Heap,D3D12_HEAP_FLAG_SHARED,&Desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&Slot.Resource));
            if(SUCCEEDED(Result))Result=Device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&Slot.Fence));
            const FString Prefix=FString::Printf(TEXT("Local\\heyzed-ui-%u-%llu-%d"),FPlatformProcess::GetCurrentProcessId(),Revision,Index);
            if(SUCCEEDED(Result))Result=Device->CreateSharedHandle(Slot.Resource.Get(),nullptr,GENERIC_ALL,*(Prefix+TEXT("-texture")),&Slot.TextureHandle);
            if(SUCCEEDED(Result))Result=Device->CreateSharedHandle(Slot.Fence.Get(),nullptr,GENERIC_ALL,*(Prefix+TEXT("-fence")),&Slot.FenceHandle);
            if(FAILED(Result)){Failed=true;Int(56)=2;UE_LOG(LogTemp,Error,TEXT("heyzed native UI shared resource failed: %08x"),uint32(Result));return;}
            Slot.Texture=RHI->RHICreateTexture2DFromResource(PF_R8G8B8A8,ETextureCreateFlags::ShaderResource|ETextureCreateFlags::RenderTargetable|ETextureCreateFlags::Shared,FClearValueBinding::Transparent,Slot.Resource.Get());
            Store(256+Index*64,0);Store(264+Index*64,0);
        }
        Int(24)=Size.X;Int(28)=Size.Y;Int(56)=0;Store(16,Revision);Ready=true;
        UE_LOG(LogTemp,Display,TEXT("heyzed native UI GPU generation=%llu viewport=%dx%d"),Revision,Size.X,Size.Y);
    }
    void Copy(FRHICommandListImmediate& RHICmdList,FTextureRHIRef Destination)
    {
        if(!Ready||Failed||!Destination)return;
        int32 Latest=-1;uint64 LatestFrame=0;
        for(int32 Index=0;Index<3;++Index)
        {
            auto& Slot=Slots[Index];const int32 Offset=256+Index*64;
            if(Slot.Pending&&Slot.Fence->GetCompletedValue()>=Slot.Pending+1){Store(Offset+8,Slot.Pending);Slot.Pending=0;}
            const uint64 Value=Load(Offset);
            if(Load(Offset+24)==Generation&&!Slot.Pending&&Value!=Load(Offset+8)&&Slot.Fence->GetCompletedValue()>=Value)
            {
                const uint64 Frame=Load(Offset+16);if(Frame>=LatestFrame){LatestFrame=Frame;Latest=Index;}
            }
        }
        for(int32 Index=0;Index<3;++Index)
        {
            auto& Slot=Slots[Index];const int32 Offset=256+Index*64;const uint64 Value=Load(Offset);
            if(Load(Offset+24)!=Generation||Slot.Pending||Value==Load(Offset+8)||Slot.Fence->GetCompletedValue()<Value)continue;
            if(Index==Latest)
            {
                RHICmdList.Transition(FRHITransitionInfo(Slot.Texture,ERHIAccess::Unknown,ERHIAccess::CopySrc));
                RHICmdList.Transition(FRHITransitionInfo(Destination,ERHIAccess::Unknown,ERHIAccess::CopyDest));
                RHICmdList.CopyTexture(Slot.Texture,Destination,FRHICopyTextureInfo());
                RHICmdList.Transition(FRHITransitionInfo(Slot.Texture,ERHIAccess::CopySrc,ERHIAccess::Present));
                RHICmdList.Transition(FRHITransitionInfo(Destination,ERHIAccess::CopyDest,ERHIAccess::SRVMask));
                ++Frames;Store(64,Frames.load());PresentedGeneration=Generation;
            }
            auto Fence=Slot.Fence;
            RHICmdList.EnqueueLambda([Fence,Value](FRHICommandList& Executing){GetID3D12DynamicRHI()->RHISignalManualFence(Executing,Fence.Get(),Value+1);});Slot.Pending=Value;
        }
    }
#endif
};

class FHeyZedNativeUIInput : public IInputProcessor
{
public:
    TWeakObjectPtr<UWorld> World;
    TSharedPtr<FHeyZedNativeUIState,ESPMode::ThreadSafe> State;
    uint32 Held=0;
    double ReviewUntil=0;
    TSet<FKey> ReviewButtons;
    FVector2D ReviewPoint=FVector2D::ZeroVector;
    void Pointer(const FPointerEvent& Value)
    {
#if PLATFORM_WINDOWS
        auto* Current=World.Get();if(!Current||!Current->GetGameViewport())return;
        const auto Viewport=Current->GetGameViewport()->GetGameViewportWidget();if(!Viewport)return;
        const FGeometry& Geometry=Viewport->GetCachedGeometry();const FVector2D Local=Geometry.AbsoluteToLocal(Value.GetScreenSpacePosition());
        const FVector2D Extent=Geometry.GetLocalSize();const FIntPoint Pixels=Current->GetGameViewport()->Viewport->GetSizeXY();
        if(Extent.X>0&&Extent.Y>0){State->Int(80)=FMath::RoundToInt(Local.X*Pixels.X/Extent.X);State->Int(84)=FMath::RoundToInt(Local.Y*Pixels.Y/Extent.Y);}
#endif
    }
    virtual void Tick(float DeltaTime,FSlateApplication&,TSharedRef<ICursor>) override
    {
#if PLATFORM_WINDOWS
        auto* Current=World.Get();auto* PC=Current?Cast<AHeyZedPlayerController>(Current->GetFirstPlayerController()):nullptr;
        const auto* HeyZed=Current?Current->GetSubsystem<UHeyZedWorldSubsystem>():nullptr;
        const bool Reviewing=FPlatformTime::Seconds()<ReviewUntil;
        bool Active=PC&&HeyZed&&HeyZed->GameplayReady()&&PC->PointerReleased()&&Current->GetGameViewport()&&Current->GetGameViewport()->Viewport&&(Reviewing||Current->GetGameViewport()->Viewport->HasFocus());
        if(Active){const FIntPoint Size=Current->GetGameViewport()->Viewport->GetSizeXY();Active=State->Int(40)==Size.X&&State->Int(44)==Size.Y&&State->PresentedGeneration==State->Load(16);}
        State->Int(88)=Active?1:0;float X=0,Y=0;
        if(Active&&!Reviewing&&PC->GetMousePosition(X,Y)){State->Int(80)=FMath::RoundToInt(X);State->Int(84)=FMath::RoundToInt(Y);}
        if(!Active)Held=0;
#endif
    }
    bool OverNative() const
    {
#if PLATFORM_WINDOWS
        if(State->Int(88)!=1)return false;
        const auto* Current=World.Get();const auto* PC=Current?Cast<AHeyZedPlayerController>(Current->GetFirstPlayerController()):nullptr;
        const int32 X=State->Int(80),Y=State->Int(84);const uint64 Sequence=State->Load(104);if(Sequence&1)return false;
        const bool OverControls=PC&&PC->PointerOverHeyZedControls(float(X),float(Y));
        bool Over=false,Modal=false;
        for(int32 Index=0;Index<FMath::Clamp(State->Int(96),0,96);++Index)
        {
            const int32 Offset=4096+Index*16;
            if(X>=State->Int(Offset)&&Y>=State->Int(Offset+4)&&X<State->Int(Offset)+State->Int(Offset+8)&&Y<State->Int(Offset+4)+State->Int(Offset+12))
            {
                Over=true;
                if(State->Int(Offset)<=0&&State->Int(Offset+4)<=0&&State->Int(Offset+8)>=State->Int(24)&&State->Int(Offset+12)>=State->Int(28))Modal=true;
            }
        }
        return Over&&(!OverControls||Modal)&&State->Load(104)==Sequence;
#else
        return false;
#endif
    }
    bool Event(int32 Type,int32 Value)
    {
#if PLATFORM_WINDOWS
        const uint64 Head=State->Load(128),Tail=State->Load(136);if(Head-Tail>=128)return false;
        const int32 Offset=1024+(Head%128)*16;State->Int(Offset)=Type;State->Int(Offset+4)=Value;State->Int(Offset+8)=State->Int(80);State->Int(Offset+12)=State->Int(84);State->Store(128,Head+1);return true;
#else
        return false;
#endif
    }
    static int32 Button(FKey Key){return Key==EKeys::LeftMouseButton?0:Key==EKeys::RightMouseButton?1:Key==EKeys::MiddleMouseButton?2:-1;}
    virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& EventValue) override
    {
        Pointer(EventValue);
        const int32 Code=Button(EventValue.GetEffectingButton());if(Code<0||!OverNative())return false;
        if(!Event(1,Code|256))return false;Held|=1u<<Code;return true;
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& EventValue) override
    {
        Pointer(EventValue);
        const int32 Code=Button(EventValue.GetEffectingButton());if(Code<0||!(Held&(1u<<Code)))return false;
        Event(1,Code);Held&=~(1u<<Code);return true;
    }
    virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& App,const FPointerEvent& Value) override {return HandleMouseButtonDownEvent(App,Value);}
    virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent& Value,const FPointerEvent*) override {Pointer(Value);return OverNative()&&Event(2,FMath::RoundToInt(Value.GetWheelDelta()*120));}
};

void UHeyZedNativeUI::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
#if PLATFORM_WINDOWS
    if(!IsRHID3D12()){UE_LOG(LogTemp,Error,TEXT("heyzed native UI requires DirectX 12 on Windows"));return;}
    State=MakeShared<FHeyZedNativeUIState,ESPMode::ThreadSafe>();if(!State->Open()){State.Reset();return;}
    Input=MakeShared<FHeyZedNativeUIInput>();Input->World=GetWorld();Input->State=State;FSlateApplication::Get().RegisterInputPreProcessor(Input);
    FParse::Value(FCommandLine::Get(),TEXT("HeyZedNativeUIInput="),InputReviewPath);
    UE_LOG(LogTemp,Display,TEXT("heyzed native UI enabled: GPU texture transport, viewport-driven native resolution"));
#endif
}
void UHeyZedNativeUI::Tick(float DeltaTime)
{
#if PLATFORM_WINDOWS
    if(!State||!GetWorld()->GetGameViewport()||!GetWorld()->GetGameViewport()->Viewport)return;
    const double Now=FPlatformTime::Seconds();State->Store(32,FDateTime::UtcNow().ToUnixTimestamp()*1000+FDateTime::UtcNow().GetMillisecond());
    FIntPoint Size=GetWorld()->GetGameViewport()->Viewport->GetSizeXY();if(Size.X<320||Size.Y<240)return;
    if(Size!=DesiredSize){DesiredSize=Size;SizeChangedAt=Now;}
    if(Size!=CurrentSize&&Now-SizeChangedAt>=.2)
    {
        PendingDisplay=NewObject<UTextureRenderTarget2D>(this);PendingDisplay->ClearColor=FLinearColor::Transparent;PendingDisplay->bAutoGenerateMips=false;PendingDisplay->InitCustomFormat(Size.X,Size.Y,PF_R8G8B8A8,true);PendingDisplay->UpdateResourceImmediate(true);
        if(!Widget){Widget=SNew(SHeyZedNativeUI);GetWorld()->GetGameViewport()->AddViewportWidgetContent(Widget.ToSharedRef(),5000);}
        auto Shared=State;const uint64 Revision=FPlatformTime::Cycles64();
        PendingGeneration=Revision;
        ENQUEUE_RENDER_COMMAND(HeyZedNativeUICreate)([Shared,Size,Revision](FRHICommandListImmediate& RHICmdList){Shared->Create(RHICmdList,Size,Revision);});CurrentSize=Size;
    }
    if(PendingDisplay&&State->PresentedGeneration==PendingGeneration){Display=PendingDisplay;PendingDisplay=nullptr;Widget->SetTexture(Display);}
    if(!bActivated&&Display)
    {
        const auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();
        if(HeyZed&&HeyZed->ScenePresented())
        {
            if(auto Window=GetWorld()->GetGameViewport()->GetWindow())
            {
                if(FParse::Param(FCommandLine::Get(),TEXT("Fullscreen"))){Window->SetWindowMode(EWindowMode::WindowedFullscreen);UGameUserSettings::GetGameUserSettings()->SetFullscreenMode(EWindowMode::WindowedFullscreen);}
                Window->BringToFront(true);
            }
            bActivated=true;
        }
    }
    if((PendingDisplay||Display)&&State->Ready)
    {
        auto Shared=State;FTextureRenderTargetResource* Resource=(PendingDisplay?PendingDisplay:Display)->GameThread_GetRenderTargetResource();
        ENQUEUE_RENDER_COMMAND(HeyZedNativeUICopy)([Shared,Resource](FRHICommandListImmediate& RHICmdList){Shared->Copy(RHICmdList,Resource->GetRenderTargetTexture());});
    }
    if(!InputReviewPath.IsEmpty()&&Now>=NextInputReview)
    {
        NextInputReview=Now+.1;FString Source;TSharedPtr<FJsonObject> Command;
        if(FFileHelper::LoadFileToString(Source,*InputReviewPath)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Command)&&Command.IsValid())
        {
            double Sequence=0;FString Action;
            if(Command->TryGetNumberField(TEXT("sequence"),Sequence)&&int64(Sequence)>InputReviewSequence&&Command->TryGetStringField(TEXT("action"),Action))
            {
                InputReviewSequence=int64(Sequence);bool Handled=false;
                if(Action==TEXT("screenshot"))
                {
                    FString Name;if(Command->TryGetStringField(TEXT("name"),Name)&&Name==FPaths::GetCleanFilename(Name)){FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed"),Name),true,false);Handled=true;}
                }
                else if(Action==TEXT("backend-screenshot")){State->Int(120)=int32(InputReviewSequence);Handled=true;}
                else if(Display&&Input)
                {
                    double X=0,Y=0;Command->TryGetNumberField(TEXT("x"),X);Command->TryGetNumberField(TEXT("y"),Y);
                    auto* PC=Cast<AHeyZedPlayerController>(GetWorld()->GetFirstPlayerController());if(PC)PC->ReleasePointer();
                    Input->ReviewUntil=Now+5;Input->State->Int(80)=FMath::RoundToInt(X);Input->State->Int(84)=FMath::RoundToInt(Y);
                    const auto Viewport=GetWorld()->GetGameViewport()->GetGameViewportWidget();const FGeometry& Geometry=Viewport->GetCachedGeometry();
                    const FVector2D Position=Geometry.LocalToAbsolute(FVector2D(X*Geometry.GetLocalSize().X/Size.X,Y*Geometry.GetLocalSize().Y/Size.Y));
                    FString ButtonName;Command->TryGetStringField(TEXT("button"),ButtonName);const FKey Button=ButtonName==TEXT("right")?EKeys::RightMouseButton:EKeys::LeftMouseButton;
                    if(Action==TEXT("down"))Input->ReviewButtons.Add(Button);else if(Action==TEXT("up"))Input->ReviewButtons.Remove(Button);
                    FPointerEvent Event(0,Position,Input->ReviewPoint,Input->ReviewButtons,(Action==TEXT("move"))?EKeys::Invalid:Button,0,FModifierKeysState());
                    Input->ReviewPoint=Position;Input->State->Int(88)=1;
                    auto& App=FSlateApplication::Get();
                    if(Action==TEXT("down"))Handled=App.ProcessMouseButtonDownEvent(GetWorld()->GetGameViewport()->GetWindow()->GetNativeWindow(),Event);
                    else if(Action==TEXT("up"))Handled=App.ProcessMouseButtonUpEvent(Event);
                    else if(Action==TEXT("move"))Handled=App.ProcessMouseMoveEvent(Event);
                }
                FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"sequence\":%lld,\"handled\":%s,\"head\":%llu,\"tail\":%llu}"),InputReviewSequence,Handled?TEXT("true"):TEXT("false"),State->Load(128),State->Load(136)),*(InputReviewPath+TEXT(".result.json")));
            }
        }
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedNativeUIReview")))
    {
        const auto* HeyZed=GetWorld()->GetSubsystem<UHeyZedWorldSubsystem>();
        const bool Matched=HeyZed&&HeyZed->ScenePresented()&&!PendingDisplay&&Display&&State->Int(40)==Size.X&&State->Int(44)==Size.Y&&State->Int(56)==0;
        if(Matched&&ReviewAt==0)ReviewAt=Now+3;
        if(!Matched)ReviewAt=0;
        if(Matched&&Now>=ReviewAt&&!FScreenshotRequest::IsScreenshotRequested())
        {
            if(ReviewStep==0){ReviewInitialSize=Size;ReviewInitialMode=int32(UGameUserSettings::GetGameUserSettings()->GetFullscreenMode());FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/native-ui-initial.png")),true,false);ReviewStep=1;ReviewAt=Now+1;}
            else if(ReviewStep==1){UGameUserSettings::RequestResolutionChange(1120,800,EWindowMode::Windowed,false);ReviewStep=2;ReviewAt=Now+3;}
            else if(ReviewStep==2&&Size!=ReviewInitialSize){FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/native-ui-resized.png")),true,false);ReviewStep=3;ReviewAt=Now+1;}
            else if(ReviewStep==3){UGameUserSettings::RequestResolutionChange(ReviewInitialSize.X,ReviewInitialSize.Y,EWindowMode::Type(ReviewInitialMode),false);ReviewStep=4;ReviewAt=Now+3;}
            else if(ReviewStep==4&&Size==ReviewInitialSize){ReviewStep=5;UE_LOG(LogTemp,Display,TEXT("heyzed native UI review complete: initial, resized and restored native dimensions matched"));if(FParse::Param(FCommandLine::Get(),TEXT("HeyZedNativeUIExitAfterReview")))FPlatformMisc::RequestExit(false);}
        }
    }
    if(Now>=NextHealth)
    {
        NextHealth=Now+.5;
        const FString Health=FString::Printf(TEXT("{\"schema\":1,\"width\":%d,\"height\":%d,\"native_width\":%d,\"native_height\":%d,\"produced\":%llu,\"displayed\":%llu,\"error\":%d,\"review_step\":%d}"),CurrentSize.X,CurrentSize.Y,State->Int(40),State->Int(44),State->Load(48),State->Frames.load(),State->Int(56),ReviewStep);
        FFileHelper::SaveStringToFile(Health,*FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeyZed/native-ui-health.json")));
    }
#endif
}
void UHeyZedNativeUI::Deinitialize()
{
    if(Input&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(Input);Input.Reset();
    if(Widget&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Widget.ToSharedRef());Widget.Reset();
#if PLATFORM_WINDOWS
    if(State)State->Store(32,0);
#endif
    FlushRenderingCommands();State.Reset();Display=nullptr;PendingDisplay=nullptr;Super::Deinitialize();
}
