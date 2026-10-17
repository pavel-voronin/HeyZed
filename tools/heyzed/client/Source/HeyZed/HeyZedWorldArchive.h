#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "HeyZedTerrain.h"

struct FHeyZedWorldPage
{
    uint64 Offset=0;
    uint32 Compressed=0, Raw=0, Crc=0;
};
class FHeyZedWorldArchive
{
public:
    static TSharedPtr<FHeyZedWorldArchive,ESPMode::ThreadSafe> Open(const FString& Path,const FString& Identity,const FString& Recipe);
    TSharedPtr<FHeyZedPreparedGround> Read(FIntPoint Page) const;
    TMap<FIntPoint,FHeyZedWorldPage> Pages;
private:
    FString Filename;
};
class FHeyZedWorldStream
{
public:
    void Begin(const FString& Filename,const FString& Identity,const FString& Recipe);
    TArray<TSharedPtr<FHeyZedPreparedGround>> Tick(const FVector& NativePlayer);
    FString Status=TEXT("NONE");
    uint64 LoadedPages=0;
private:
    TFuture<TSharedPtr<FHeyZedWorldArchive,ESPMode::ThreadSafe>> Opening;
    TSharedPtr<FHeyZedWorldArchive,ESPMode::ThreadSafe> Archive;
    TMap<FIntPoint,TFuture<TSharedPtr<FHeyZedPreparedGround>>> Reading;
    TSet<FIntPoint> Requested;
    int32 Radius=256;
};
