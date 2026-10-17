#include "HeyZedEntityPose.h"
#include "Serialization/MemoryReader.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool ReadHeyZedEntityPose(const TArray<uint8>& Bytes,FHeyZedEntityPose& Pose,FString& Error)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_ReadPosePacket);
    constexpr int32 HeaderBytes=152;
    if(Bytes.Num()<HeaderBytes){Error=TEXT("Pose packet header is incomplete");return false;}
    FMemoryReaderView Reader(FMemoryView(Bytes.GetData(),Bytes.Num()));
    Reader.SetByteSwapping(!PLATFORM_LITTLE_ENDIAN);
    uint32 Magic=0,Version=0,Flags=0;
    Reader<<Magic<<Version<<Flags;
    if(Magic!=0x31504542||(Version!=1&&Version!=2)||Flags>1){Error=TEXT("Unsupported pose packet format; update adapter and client together");return false;}
    FHeyZedEntityPose Value;
    Value.Seated=(Flags&1)!=0;
    int32 IdBytes=0,VehicleBytes=0,BoneCount=0,LocalCount=0;
    Reader<<Value.Epoch<<Value.Sequence<<Value.SampleNs<<Value.AppearanceRevision;
    Reader<<IdBytes<<VehicleBytes<<Value.Seat<<BoneCount<<LocalCount;
    int32 AnimationBytes=0;if(Version==2){if(Bytes.Num()<HeaderBytes+4){Error=TEXT("Animation header is incomplete");return false;}Reader<<AnimationBytes;}
    const int64 MatrixBytes=(int64(BoneCount)+LocalCount)*64;
    if(IdBytes<=0||VehicleBytes<0||BoneCount<0||LocalCount<0||AnimationBytes<0||int64(HeaderBytes)+(Version==2?4:0)+IdBytes+VehicleBytes+AnimationBytes+MatrixBytes!=Bytes.Num())
    {Error=TEXT("Pose packet sizes are inconsistent");return false;}
    FMatrix World;
    for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column)
    {
        float Number=0;Reader<<Number;
        if(!FMath::IsFinite(Number)){Error=TEXT("Pose world matrix is not finite");return false;}
        World.M[Row][Column]=Number;
    }
    Reader<<Value.Velocity.X<<Value.Velocity.Y<<Value.Velocity.Z;
    if(Value.Velocity.ContainsNaN()){Error=TEXT("Pose velocity is not finite");return false;}
    Value.World=FTransform(World);Value.Velocity*=100;
    auto String=[&](int32 Length)
    {
        const auto* Source=Bytes.GetData()+Reader.Tell();
        FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Source),Length);
        FString Result(Converted.Length(),Converted.Get());Reader.Seek(Reader.Tell()+Length);return Result;
    };
    Value.Id=String(IdBytes);Value.Vehicle=String(VehicleBytes);
    Value.Bones.SetNumUninitialized(BoneCount*64);Reader.Serialize(Value.Bones.GetData(),Value.Bones.Num());
    Value.Locals.SetNumUninitialized(LocalCount*64);Reader.Serialize(Value.Locals.GetData(),Value.Locals.Num());
    if(AnimationBytes)Value.Animation=String(AnimationBytes);
    if(Reader.IsError()||Reader.Tell()!=Bytes.Num()||Value.World.ContainsNaN())
    {Error=TEXT("Pose packet could not be read");return false;}
    Pose=MoveTemp(Value);return true;
}
