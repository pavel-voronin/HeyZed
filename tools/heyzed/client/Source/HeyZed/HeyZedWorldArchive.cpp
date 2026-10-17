#include "HeyZedWorldArchive.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

TSharedPtr<FHeyZedWorldArchive,ESPMode::ThreadSafe> FHeyZedWorldArchive::Open(const FString& Path,const FString& Identity,const FString& Recipe)
{
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
    if (!File || File->TotalSize()<20 || Identity.IsEmpty() || Recipe.IsEmpty()) return nullptr;
    File->SetByteSwapping(!PLATFORM_LITTLE_ENDIAN);
    ANSICHAR Magic[8]; File->Serialize(Magic,8);
    if (FMemory::Memcmp(Magic,"HEYZEDWLD1",8)!=0) return nullptr;
    uint32 Version=0,MetadataBytes=0,Count=0;
    *File<<Version<<MetadataBytes<<Count;
    if (Version!=1 || MetadataBytes==0 || MetadataBytes>1024*1024 || Count>2000000) return nullptr;
    const uint64 Start=20ull+MetadataBytes+28ull*Count;
    if (Start>static_cast<uint64>(File->TotalSize())) return nullptr;
    TArray<ANSICHAR> Metadata; Metadata.SetNumZeroed(MetadataBytes+1); File->Serialize(Metadata.GetData(),MetadataBytes);
    TSharedPtr<FJsonObject> Header;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Metadata.GetData())),Header) || !Header) return nullptr;
    FString Map,SourceRecipe; double PageSize=0;
    if (!Header->TryGetStringField(TEXT("map_id"),Map) || Map!=Identity || !Header->TryGetStringField(TEXT("recipe"),SourceRecipe) || SourceRecipe!=Recipe || !Header->TryGetNumberField(TEXT("page_size"),PageSize) || PageSize!=64) return nullptr;
    auto Result=MakeShared<FHeyZedWorldArchive,ESPMode::ThreadSafe>(); Result->Filename=Path;
    for (uint32 I=0;I<Count;++I)
    {
        int32 X=0,Y=0; FHeyZedWorldPage Page;
        *File<<X<<Y<<Page.Offset<<Page.Compressed<<Page.Raw<<Page.Crc;
        if (File->IsError() || !Page.Compressed || Page.Raw>32*1024*1024 || Page.Raw==0 || Page.Compressed>32*1024*1024 || Page.Offset>static_cast<uint64>(File->TotalSize())-Start || Page.Compressed>static_cast<uint64>(File->TotalSize())-Start-Page.Offset || Result->Pages.Contains({X,Y})) return nullptr;
        Page.Offset+=Start; Result->Pages.Add({X,Y},Page);
    }
    return Result;
}
TSharedPtr<FHeyZedPreparedGround> FHeyZedWorldArchive::Read(FIntPoint Address) const
{
    const auto* Page=Pages.Find(Address); if (!Page) return nullptr;
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Filename)); if (!File) return nullptr;
    File->Seek(Page->Offset);
    TArray<uint8> Compressed; Compressed.SetNumUninitialized(Page->Compressed); File->Serialize(Compressed.GetData(),Page->Compressed);
    if (File->IsError() || FCrc::MemCrc32(Compressed.GetData(),Compressed.Num())!=Page->Crc) return nullptr;
    TArray<ANSICHAR> Raw; Raw.SetNumZeroed(Page->Raw+1);
    if (!FCompression::UncompressMemory(NAME_Zlib,Raw.GetData(),Page->Raw,Compressed.GetData(),Compressed.Num())) return nullptr;
    auto Result=FHeyZedPreparedGround::Decode(UTF8_TO_TCHAR(Raw.GetData()));
    if (Result)
    {
        for (const auto& Cell : Result->Cells)
            if (FMath::FloorToInt(Cell.Address.X/64.)!=Address.X || FMath::FloorToInt(Cell.Address.Y/64.)!=Address.Y) return nullptr;
        Result->bBaked=true; Result->Epoch=0; Result->Revision=0;
    }
    return Result;
}
void FHeyZedWorldStream::Begin(const FString& Filename,const FString& Identity,const FString& Recipe)
{
    if (Filename.IsEmpty()) return;
    Status=TEXT("OPENING");
    FParse::Value(FCommandLine::Get(),TEXT("HeyZedWorldRadius="),Radius); Radius=FMath::Clamp(Radius,64,2048);
    Opening=Async(EAsyncExecution::ThreadPool,[Filename,Identity,Recipe](){ return FHeyZedWorldArchive::Open(Filename,Identity,Recipe); });
}
TArray<TSharedPtr<FHeyZedPreparedGround>> FHeyZedWorldStream::Tick(const FVector& NativePlayer)
{
    TArray<TSharedPtr<FHeyZedPreparedGround>> Ready;
    if (Opening.IsValid() && Opening.IsReady())
    {
        Archive=Opening.Consume(); Status=Archive ? TEXT("READY") : TEXT("INCOMPATIBLE");
    }
    for (auto It=Reading.CreateIterator();It;++It) if (It.Value().IsReady())
    {
        auto Page=It.Value().Get();
        if (Page) { Ready.Add(MoveTemp(Page)); ++LoadedPages; }
        else Status=TEXT("PAGE_ERROR");
        It.RemoveCurrent();
    }
    if (!Archive) return Ready;
    const FIntPoint Center(FMath::FloorToInt(NativePlayer.X/64.),FMath::FloorToInt(NativePlayer.Y/64.));
    TArray<FIntPoint> Candidates; const int32 Span=FMath::DivideAndRoundUp(Radius,64);
    for (int32 Y=Center.Y-Span;Y<=Center.Y+Span;++Y) for (int32 X=Center.X-Span;X<=Center.X+Span;++X)
        if (!Requested.Contains({X,Y}) && Archive->Pages.Contains({X,Y})) Candidates.Add({X,Y});
    Candidates.Sort([Center](FIntPoint A,FIntPoint B){return (A-Center).SizeSquared()<(B-Center).SizeSquared();});
    for (FIntPoint Address : Candidates)
    {
        if (Reading.Num()>=2) break;
        Requested.Add(Address);
        Reading.Add(Address,Async(EAsyncExecution::ThreadPool,[Source=Archive,Address](){return Source->Read(Address);}));
    }
    return Ready;
}
