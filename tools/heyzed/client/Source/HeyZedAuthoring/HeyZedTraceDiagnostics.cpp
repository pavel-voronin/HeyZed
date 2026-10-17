#include "HeyZedTraceDiagnostics.h"
#include "HAL/PlatformProcess.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Frames.h"
#include "Misc/FileHelper.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Containers/Tables.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/ModuleService.h"

bool UHeyZedTraceDiagnostics::Export(const FString& TraceFilename,const FString& ResponseFilename,double TimeoutSeconds,bool TimingOnly)
{
    auto& Insights=FModuleManager::LoadModuleChecked<IUnrealInsightsModule>(TEXT("TraceInsights"));
    FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices")).GetModuleService()->SetModuleEnabled(TEXT("TraceModule_Memory"),!TimingOnly);
    Insights.SetAutoQuit(false);
    Insights.StartAnalysisForTraceFile(*TraceFilename);
    auto Session=Insights.GetAnalysisSession();
    if(!Session)return false;
    const double Deadline=FPlatformTime::Seconds()+FMath::Clamp(TimeoutSeconds,1.,1800.);
    while(!Session->IsAnalysisComplete())
    {
        if(FPlatformTime::Seconds()>Deadline){Session->Stop(true);return false;}
        FPlatformProcess::Sleep(.05f);
    }
    UE_LOG(LogTemp,Display,TEXT("heyzed native trace analysis completed: %s"),*TraceFilename);
    return Insights.Exec(*(TEXT("@=")+ResponseFilename),*GLog);
}

bool UHeyZedTraceDiagnostics::Execute(const FString& Command)
{
    auto& Insights=FModuleManager::LoadModuleChecked<IUnrealInsightsModule>(TEXT("TraceInsights"));
    auto Session=Insights.GetAnalysisSession();
    return Session&&Session->IsAnalysisComplete()&&Insights.Exec(*Command,*GLog);
}

bool UHeyZedTraceDiagnostics::ExportStatistics(const FString& OutputFilename,const FString& ThreadPattern,double StartSeconds,double EndSeconds,bool Gpu)
{
    auto& Insights=FModuleManager::LoadModuleChecked<IUnrealInsightsModule>(TEXT("TraceInsights"));
    auto Session=Insights.GetAnalysisSession();
    if(!Session||!Session->IsAnalysisComplete()||StartSeconds<0||EndSeconds<=StartSeconds)return false;
    TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>> Table;
    {
        TraceServices::FAnalysisSessionReadScope Scope(*Session);
        const auto* Provider=TraceServices::ReadTimingProfilerProvider(*Session);
        if(!Provider)return false;
        TSet<uint32> Ids;
        if(Gpu)Provider->EnumerateGpuQueues([&](const TraceServices::FGpuQueueInfo& Queue){if(Queue.GetDisplayName().MatchesWildcard(ThreadPattern))Ids.Add(Queue.Id);});
        else TraceServices::ReadThreadProvider(*Session).EnumerateThreads([&](const TraceServices::FThreadInfo& Thread){if(FString(Thread.Name).MatchesWildcard(ThreadPattern))Ids.Add(Thread.Id);});
        if(Ids.IsEmpty())return false;
        TraceServices::FCreateAggregationParams Params;
        Params.IntervalStart=StartSeconds;Params.IntervalEnd=EndSeconds;
        Params.CpuThreadFilter=[Ids,Gpu](uint32 Id){return !Gpu&&Ids.Contains(Id);};
        Params.GpuQueueFilter=[Ids,Gpu](uint32 Id){return Gpu&&Ids.Contains(Id);};
        Table.Reset(Provider->CreateAggregation(Params));
    }
    return Table&&TraceServices::Table2Csv(*Table,*OutputFilename);
}

double UHeyZedTraceDiagnostics::BookmarkTime(const FString& Text)
{
    auto Session=FModuleManager::LoadModuleChecked<IUnrealInsightsModule>(TEXT("TraceInsights")).GetAnalysisSession();
    if(!Session||!Session->IsAnalysisComplete())return -1;
    TraceServices::FAnalysisSessionReadScope Scope(*Session);
    double Result=-1;
    TraceServices::ReadBookmarkProvider(*Session).EnumerateBookmarks(0,Session->GetDurationSeconds(),[&](const TraceServices::FBookmark& Bookmark){if(Result<0&&Text==Bookmark.Text)Result=Bookmark.Time;});
    return Result;
}

bool UHeyZedTraceDiagnostics::ExportFrames(const FString& OutputFilename,double StartSeconds,double EndSeconds)
{
    auto Session=FModuleManager::LoadModuleChecked<IUnrealInsightsModule>(TEXT("TraceInsights")).GetAnalysisSession();
    if(!Session||!Session->IsAnalysisComplete()||EndSeconds<=StartSeconds)return false;
    TraceServices::FAnalysisSessionReadScope Scope(*Session);
    FString Csv=TEXT("Index,StartTime,EndTime\n");
    TraceServices::ReadFrameProvider(*Session).EnumerateFrames(TraceFrameType_Game,StartSeconds,EndSeconds,[&](const TraceServices::FFrame& Frame)
    {
        if(Frame.StartTime>=StartSeconds&&Frame.EndTime<=EndSeconds&&FMath::IsFinite(Frame.EndTime))Csv+=FString::Printf(TEXT("%llu,%.9f,%.9f\n"),Frame.Index,Frame.StartTime,Frame.EndTime);
    });
    return FFileHelper::SaveStringToFile(Csv,*OutputFilename,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
