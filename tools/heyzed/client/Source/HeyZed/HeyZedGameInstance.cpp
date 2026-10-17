#include "HeyZedGameInstance.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "SceneViewExtension.h"
#include "SceneView.h"

class FHeyZedLoadingView : public FSceneViewExtensionBase
{
public:
    FHeyZedLoadingView(const FAutoRegister& Register):FSceneViewExtensionBase(Register){}
    TAtomic<float> Exposure{0};
    TAtomic<uint64> Frame{0};
    virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder,FSceneView& View) override
    {
        if(View.bIsSceneCapture)return;
        Exposure.Store(View.GetLastEyeAdaptationExposure());Frame.Store(GFrameCounterRenderThread);
    }
};

void UHeyZedGameInstance::Init()
{
    Super::Init();
    LoadingView=FSceneViewExtensions::NewExtension<FHeyZedLoadingView>();
    FCoreUObjectDelegates::PreLoadMap.AddUObject(this,&UHeyZedGameInstance::BeginMapLoad);
}
void UHeyZedGameInstance::Shutdown()
{
    FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
    LoadingView.Reset();
    Super::Shutdown();
}

bool UHeyZedGameInstance::ExposureReady(double WindowStart)
{
    if(!LoadingView)return false;
    const double Now=FPlatformTime::Seconds();
    const float Value=LoadingView->Exposure.Load();
    if(!FMath::IsFinite(Value)||Value<=0 || LoadingView->Frame.Load()+3<GFrameCounter)return false;
    if(ExposureWindow!=WindowStart || ReferenceExposure<=0 || FMath::Abs(FMath::Log2(Value/ReferenceExposure))>.1f)
    {
        ExposureWindow=WindowStart;ReferenceExposure=Value;ExposureStableAt=Now;
    }
    return Now-ExposureStableAt>=.75;
}
void UHeyZedGameInstance::BeginMapLoad(const FString& Map)
{
    if(IsRunningDedicatedServer() || !GetMoviePlayer())return;
    FLoadingScreenAttributes Screen;
    Screen.bAutoCompleteWhenLoadingCompletes=true;
    Screen.bMoviesAreSkippable=false;
    Screen.WidgetLoadingScreen=SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.008f,.012f,.018f,1))
        .HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(STextBlock).Text(FText::FromString(TEXT("Loading world")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular",20))
            .ColorAndOpacity(FLinearColor(.8f,.85f,.9f,1))];
    GetMoviePlayer()->SetupLoadingScreen(Screen);
}
