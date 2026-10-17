#include "HeyZedMemory.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <atomic>

namespace {
static_assert(std::atomic<uint64>::is_always_lock_free,"Shared publication requires lock-free 64-bit atomics");
constexpr uint64 RingSlots = 128, RingSlotBytes = 65536, RingHeaderBytes = 4096, RingMappingBytes = RingHeaderBytes+RingSlots*RingSlotBytes;
uint64 Load(const uint8* P) { return reinterpret_cast<const std::atomic<uint64>*>(P)->load(std::memory_order_acquire); }
void Store(uint8* P,uint64 V) { reinterpret_cast<std::atomic<uint64>*>(P)->store(V,std::memory_order_release); }
}
FHeyZedRing::~FHeyZedRing()
{
#if PLATFORM_WINDOWS
    if (Data) UnmapViewOfFile(Data);
    if (Mapping) CloseHandle(Mapping);
    if (File && File!=INVALID_HANDLE_VALUE) CloseHandle(File);
#else
    if (Data) munmap(Data,RingMappingBytes);
    if (Descriptor>=0) close(Descriptor);
#endif
}
bool FHeyZedRing::Open(const FString& Path)
{
#if PLATFORM_WINDOWS
    File = CreateFileW(*Path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (File==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER Size; if (!GetFileSizeEx(File,&Size) || Size.QuadPart!=RingMappingBytes) return false;
    Mapping = CreateFileMappingW(File,nullptr,PAGE_READWRITE,0,0,nullptr);
    if (Mapping) Data = static_cast<uint8*>(MapViewOfFile(Mapping,FILE_MAP_ALL_ACCESS,0,0,RingMappingBytes));
#else
    Descriptor = open(TCHAR_TO_UTF8(*Path),O_RDWR);
    struct stat Info;
    if (Descriptor<0 || fstat(Descriptor,&Info)!=0 || Info.st_size!=RingMappingBytes) return false;
    void* Address = mmap(nullptr,RingMappingBytes,PROT_READ|PROT_WRITE,MAP_SHARED,Descriptor,0);
    if (Address!=MAP_FAILED) Data = static_cast<uint8*>(Address);
#endif
    if (!Data) return false;
    const uint32* Layout=reinterpret_cast<const uint32*>(Data+128);
    return Layout[0]==0x314d4a50 && Layout[1]==1 && Layout[2]==RingSlots && Layout[3]==RingSlotBytes;
}
bool FHeyZedRing::Offer(const FString& Text)
{
    if (!Data) return false;
    FTCHARToUTF8 Bytes(*Text);
    if (Bytes.Length()>RingSlotBytes-16) return false;
    const uint64 Head = Load(Data),Tail = Load(Data+64);
    if (Head-Tail>=RingSlots) return false;
    uint8* Dest = Data+RingHeaderBytes+(Head%RingSlots)*RingSlotBytes;
    const uint32 Size = Bytes.Length(); FMemory::Memcpy(Dest,&Size,4);
    const int64 Timestamp=FDateTime::UtcNow().ToUnixTimestamp()*1000+FDateTime::UtcNow().GetMillisecond(); FMemory::Memcpy(Dest+8,&Timestamp,8);
    FMemory::Memcpy(Dest+16,Bytes.Get(),Size); Store(Data,Head+1); return true;
}
bool FHeyZedRing::Poll(FString& Text,bool Latest)
{
    if (!Data) return false;
    const uint64 Tail = Load(Data+64),Head = Load(Data);
    if (Tail>=Head) return false;
    uint8* Source = Data+RingHeaderBytes+((Latest ? Head-1 : Tail)%RingSlots)*RingSlotBytes;
    uint32 Size; FMemory::Memcpy(&Size,Source,4);
    if (Size>RingSlotBytes-16) return false;
    int64 Published; FMemory::Memcpy(&Published,Source+8,8);
    const auto Now=FDateTime::UtcNow(); LastWaitMs=FMath::Max<int64>(0,Now.ToUnixTimestamp()*1000+Now.GetMillisecond()-Published);
    FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Source+16),Size);
    Text = FString(Convert.Length(),Convert.Get());
    Store(Data+64,Latest ? Head : Tail+1); return true;
}
bool FHeyZedRing::PollBytes(TArray<uint8>& Bytes)
{
    if(!Data)return false;
    const uint64 Tail=Load(Data+64),Head=Load(Data);
    if(Tail>=Head)return false;
    const uint8* Source=Data+RingHeaderBytes+(Tail%RingSlots)*RingSlotBytes;
    uint32 Size=0;FMemory::Memcpy(&Size,Source,4);
    if(Size>RingSlotBytes-16)return false;
    int64 Published=0;FMemory::Memcpy(&Published,Source+8,8);
    const auto Now=FDateTime::UtcNow();LastWaitMs=FMath::Max<int64>(0,Now.ToUnixTimestamp()*1000+Now.GetMillisecond()-Published);
    Bytes.SetNumUninitialized(Size);if(Size)FMemory::Memcpy(Bytes.GetData(),Source+16,Size);
    Store(Data+64,Tail+1);return true;
}
uint64 FHeyZedRing::Depth() const { return Data ? Load(Data)-Load(Data+64) : 0; }
bool FHeyZedMemory::Refresh()
{
    const double Now = FPlatformTime::Seconds();
    if (Now<NextCheck) return Ready();
    NextCheck = Now+.25;
    FString Root = FPaths::Combine(FPlatformProcess::UserHomeDir(),TEXT(".heyzed/bridge"));
    FParse::Value(FCommandLine::Get(),TEXT("HeyZedMemoryRoot="),Root);
    FString NewGeneration;
    if (!FFileHelper::LoadFileToString(NewGeneration,*FPaths::Combine(Root,TEXT("current")))) return Ready();
    NewGeneration.TrimStartAndEndInline();
    if (!ConsumerLease && ConsumerDescriptor<0)
    {
        const FString LeasePath=FPaths::Combine(Root,TEXT("consumer.lock"));
#if PLATFORM_WINDOWS
        HANDLE Lease=CreateFileW(*LeasePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (Lease==INVALID_HANDLE_VALUE) return false;
        ConsumerLease=Lease;
#else
        const int Lease=open(TCHAR_TO_UTF8(*LeasePath),O_CREAT|O_RDWR,0600);
        if (Lease<0) return false;
        if (flock(Lease,LOCK_EX|LOCK_NB)!=0) { close(Lease); return false; }
        ConsumerDescriptor=Lease;
#endif
    }
    if (NewGeneration==Generation || NewGeneration.IsEmpty() || !NewGeneration.IsNumeric()) return Ready();
    TMap<FString,TUniquePtr<FHeyZedRing>> NewRings;
    for (const TCHAR* Name : {TEXT("motion"),TEXT("events"),TEXT("world"),TEXT("input"),TEXT("commands")})
    {
        auto Ring = MakeUnique<FHeyZedRing>();
        if (!Ring->Open(FPaths::Combine(Root,NewGeneration,FString(Name)+TEXT(".ring")))) return Ready();
        NewRings.Add(Name,MoveTemp(Ring));
    }
    for (const TCHAR* Name : {TEXT("entities"),TEXT("poses")})
    {
        const FString Path=FPaths::Combine(Root,NewGeneration,FString(Name)+TEXT(".ring"));
        if (!FPaths::FileExists(Path)) continue;
        auto Ring=MakeUnique<FHeyZedRing>(); if (Ring->Open(Path)) NewRings.Add(Name,MoveTemp(Ring));
    }
    Rings = MoveTemp(NewRings); Generation = MoveTemp(NewGeneration); return true;
}
bool FHeyZedMemory::Offer(const FString& Name,const FString& Text) { auto* Ring = Rings.Find(Name); return Ring && (*Ring)->Offer(Text); }
bool FHeyZedMemory::Poll(const FString& Name,FString& Text) { auto* Ring = Rings.Find(Name); return Ring && (*Ring)->Poll(Text); }
bool FHeyZedMemory::PollBytes(const FString& Name,TArray<uint8>& Bytes) { auto* Ring=Rings.Find(Name); return Ring&&(*Ring)->PollBytes(Bytes); }
uint64 FHeyZedMemory::Depth(const FString& Name) const { const auto* Ring = Rings.Find(Name); return Ring ? (*Ring)->Depth() : 0; }

double FHeyZedMemory::WaitMs(const FString& Name) const { const auto* Ring=Rings.Find(Name); return Ring ? (*Ring)->LastWaitMs : 0; }

uint64 FHeyZedRing::Consumed() const { return Data ? Load(Data+64) : 0; }
uint64 FHeyZedMemory::Consumed(const FString& Name) const { const auto* Ring=Rings.Find(Name); return Ring ? (*Ring)->Consumed() : 0; }

bool FHeyZedMemory::PollLatest(const FString& Name,FString& Text) { auto* Ring=Rings.Find(Name); return Ring && (*Ring)->Poll(Text,true); }

FHeyZedMemory::~FHeyZedMemory()
{
#if PLATFORM_WINDOWS
    if (ConsumerLease) CloseHandle(ConsumerLease);
#else
    if (ConsumerDescriptor>=0) { flock(ConsumerDescriptor,LOCK_UN); close(ConsumerDescriptor); }
#endif
}
