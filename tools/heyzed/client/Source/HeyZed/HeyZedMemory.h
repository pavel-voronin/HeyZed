#pragma once
#include "CoreMinimal.h"

// SPSC immutable published slots, retained until the reader has copied them.
class FHeyZedRing
{
public:
    FHeyZedRing() = default;
    ~FHeyZedRing();
    bool Open(const FString& Path);
    bool Offer(const FString& Text);
    bool Poll(FString& Text,bool Latest=false);
    bool PollBytes(TArray<uint8>& Bytes);
    uint64 Depth() const;
    uint64 Consumed() const;
    double LastWaitMs = 0;
private:
    uint8* Data = nullptr;
    void* File = nullptr;
    void* Mapping = nullptr;
    int Descriptor = -1;
};

class FHeyZedMemory
{
public:
    ~FHeyZedMemory();
    bool Refresh();
    bool Ready() const { return !Generation.IsEmpty(); }
    bool Offer(const FString& Name,const FString& Text);
    bool Poll(const FString& Name,FString& Text);
    bool PollBytes(const FString& Name,TArray<uint8>& Bytes);
    bool PollLatest(const FString& Name,FString& Text);
    uint64 Depth(const FString& Name) const;
    uint64 Consumed(const FString& Name) const;
    double WaitMs(const FString& Name) const;
    FString Generation;
private:
    void* ConsumerLease = nullptr;
    int ConsumerDescriptor = -1;
    TMap<FString,TUniquePtr<FHeyZedRing>> Rings;
    double NextCheck = 0;
};
