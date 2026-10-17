#include "HeyZedEntities.h"
#include "HeyZedVehicleMotion.h"
#include "HeyZedCrowd.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Algo/AllOf.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"

namespace {
TAutoConsoleVariable<int32> CVarNativePoseVisibility(TEXT("heyzed.Entities.NativePoseVisibility"),2,TEXT("Native recent-render admission: 0 disabled, 1 pose uploads, 2 also coalesces unseen root transforms."));
FMatrix Basis(){FMatrix B=FMatrix::Identity;B.M[0][0]=-100;B.M[1][1]=0;B.M[1][2]=100;B.M[2][1]=100;B.M[2][2]=0;return B;}
FMatrix Matrix(const TArray<TSharedPtr<FJsonValue>>& A,bool Native){FMatrix M=FMatrix::Identity;if(A.Num()!=16)return M;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)M.M[R][C]=A[R*4+C]->AsNumber();return Native?Basis().Inverse()*M*Basis():M;}
FString VehicleMapField(const TCHAR* Name){return FString(Name)==TEXT("Paint")?TEXT("NativePaintMap"):FString(Name)==TEXT("Rust")?TEXT("NativeRustMap"):Name;}
bool TextureKey(const FString& Key){return Key.Len()==64 && Algo::AllOf(Key,[](TCHAR C){return FChar::IsHexDigit(C);});}
bool DecodeNativeMatrices(const TArray<uint8>& Raw,int32 Count,TArray<FTransform>& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_DecodeMatrices);
    if(Count<=0||Raw.Num()!=int64(Count)*64)return false;
#if PLATFORM_LITTLE_ENDIAN
    const float* Values=reinterpret_cast<const float*>(Raw.GetData());
#else
    TArray<float> NativeValues;NativeValues.SetNumUninitialized(Raw.Num()/4);
    FMemoryReaderView Reader(FMemoryView(Raw.GetData(),Raw.Num()));Reader.SetByteSwapping(true);
    for(float& Number:NativeValues)Reader<<Number;
    const float* Values=NativeValues.GetData();
#endif
    Out.Reset(Count);
    for(int32 Bone=0;Bone<Count;++Bone)
    {
        FMatrix BoneMatrix;
        for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column)BoneMatrix.M[Row][Column]=Values[Bone*16+Row*4+Column];
        Out.Add(FTransform(Basis().Inverse()*BoneMatrix*Basis()));
    }
    return true;
}
bool DecodeNativePose(const FString& Encoded,int32 Count,TArray<FTransform>& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_DecodePose);
    TArray<uint8> Raw;return FBase64::Decode(Encoded,Raw)&&DecodeNativeMatrices(Raw,Count,Out);
}

}

AHeyZedEntities::AHeyZedEntities(){RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Entities"));PrimaryActorTick.bCanEverTick=true;Crowd=CreateDefaultSubobject<UHeyZedCrowdComponent>(TEXT("Crowd"));}
void AHeyZedEntities::BeginPlay(){Super::BeginPlay();Resources=LoadObject<UHeyZedEntityResources>(nullptr,TEXT("/Game/HeyZed/Entities/Resources.Resources"));Crowd->Initialize(Resources);TextureDirectory=FPaths::Combine(FPlatformProcess::UserHomeDir(),TEXT(".heyzed/bridge/entity-textures"));}
int32 AHeyZedEntities::ReadyCount() const{int32 Count=0;for(auto& Pair:Entities)if(Pair.Value.Parts.ContainsByPredicate([](const FHeyZedEntityPart& Part){return Part.Mesh!=nullptr;}))++Count;return Count;}
int32 AHeyZedEntities::KindCount(const FString& Kind,bool Ready) const{int32 Count=0;for(auto& Pair:Entities)if(Pair.Value.Kind==Kind&&(!Ready||Pair.Value.Parts.ContainsByPredicate([](const FHeyZedEntityPart& Part){return Part.Mesh!=nullptr;})))++Count;return Count;}
int32 AHeyZedEntities::RenderedCount(const FString& Kind) const{int32 Count=0;const float Now=GetWorld()->GetTimeSeconds();for(auto& Pair:Entities)if(Pair.Value.Kind==Kind&&Pair.Value.Parts.ContainsByPredicate([Now](const FHeyZedEntityPart& Part){return Part.Mesh&&Part.Mesh->GetLastRenderTimeOnScreen()>0&&Now-Part.Mesh->GetLastRenderTimeOnScreen()<.5f;}))++Count;return Count;}
bool AHeyZedEntities::PlayerAppearanceReady() const
{
    for(const auto& Pair:Entities)if(Pair.Value.Kind==TEXT("player")){
        const auto& Entity=Pair.Value;if(!Entity.PendingParts.IsEmpty()||Entity.Parts.IsEmpty()||!Entity.PoseInitialized)return false;
        for(const auto& Part:Entity.Parts){const auto* Texture=Textures.FindRef(Part.Texture).Get();if(!Part.Mesh||!Part.Material||!Texture||!Texture->GetResource()||!Texture->GetResource()->IsInitialized())return false;}return true;
    }
    return false;
}
void AHeyZedEntities::BeginCrowdHandoff(const FString& Id,const FTransform& Transform){if(auto* Entity=Entities.Find(Id);Entity&&Entity->Root){Entity->CrowdBlendFrom=Transform;Entity->CrowdBlendStarted=FPlatformTime::Seconds();Entity->Root->SetWorldTransform(Transform);}}
bool AHeyZedEntities::DetailReady(const FString& Id) const{const auto* Entity=Entities.Find(Id);return Entity&&Entity->PoseInitialized&&!Entity->Parts.IsEmpty()&&Entity->PendingParts.IsEmpty();}
void AHeyZedEntities::AppendResourceHealth(FJsonObject& Health) const
{
    Crowd->AppendHealth(Health);
    auto Animation=MakeShared<FJsonObject>();Animation->SetNumberField(TEXT("clips"),Resources?Resources->NativeAnimations.Num():0);Animation->SetNumberField(TEXT("loaded"),ResidentAnimations.Num());Animation->SetNumberField(TEXT("loading"),LoadingAnimations.Num());Animation->SetNumberField(TEXT("failed"),FailedAnimations.Num());
    for(const TCHAR* Kind:{TEXT("player"),TEXT("zombie"),TEXT("animal")}){int32 Local=0,Stream=0,Eligible=0;TSharedPtr<FJsonObject> Example=MakeShared<FJsonObject>();for(const auto& Pair:Entities)if(Pair.Value.Kind==Kind){const auto& Entity=Pair.Value;if(Entity.UsingLocalAnimation)++Local;else ++Stream;if(Entity.AnimationState){if(Entity.AnimationState->GetStringField(TEXT("mode"))==TEXT("local"))++Eligible;if(Example->Values.IsEmpty())Example=Entity.AnimationState;}}auto Counts=MakeShared<FJsonObject>();Counts->SetNumberField(TEXT("local"),Local);Counts->SetNumberField(TEXT("streamed"),Stream);Counts->SetNumberField(TEXT("eligible"),Eligible);Counts->SetObjectField(TEXT("example"),Example);Animation->SetObjectField(Kind,Counts);}Health.SetObjectField(TEXT("entity_animation"),Animation);Health.SetBoolField(TEXT("player_appearance_ready"),PlayerAppearanceReady());
    Health.SetNumberField(TEXT("entity_model_cache"),ResidentModels.Num());Health.SetNumberField(TEXT("entity_texture_cache"),Textures.Num());Health.SetNumberField(TEXT("entity_model_requests"),LoadingModels.Num());Health.SetNumberField(TEXT("entity_texture_requests"),LoadingTextures.Num());Health.SetNumberField(TEXT("entity_failed_model_loads"),FailedModelLoads);
    for(const FString Kind:{FString(TEXT("vehicle")),FString(TEXT("animal"))}){int32 Models=0,Images=0;for(const auto& Pair:Entities)if(Pair.Value.Kind==Kind)for(const auto& Part:Pair.Value.PendingParts){const auto* Model=Resources?Resources->Models.Find(Part.Key):nullptr;if(!Model||!Model->Get())++Models;if(Part.Vehicle.IsValid()){for(const TCHAR* Name:{TEXT("Paint"),TEXT("Mask"),TEXT("Rust"),TEXT("Lamps"),TEXT("BloodMask"),TEXT("Damage1"),TEXT("Blood"),TEXT("Damage2")}){FString Hash;if(Part.Vehicle->TryGetStringField(VehicleMapField(Name),Hash)&&!Textures.Contains(Hash+(FString(Name)==TEXT("Mask")?TEXT("_mask"):TEXT(""))))++Images;}}else if(!Textures.Contains(Part.Texture))++Images;}Health.SetNumberField(TEXT("pending_models_")+Kind,Models);Health.SetNumberField(TEXT("pending_textures_")+Kind,Images);}
}
void AHeyZedEntities::Remove(const FString& Id){if(auto* Entity=Entities.Find(Id)){for(auto& Part:Entity->Parts){if(Part.Mesh)Part.Mesh->DestroyComponent();if(Part.Glass)Part.Glass->DestroyComponent();if(Part.ViewMesh)Part.ViewMesh->DestroyComponent();}if(Entity->Root)Entity->Root->DestroyComponent();Entities.Remove(Id);}PendingPoses.Remove(Id);}
void AHeyZedEntities::SetEpoch(int64 Value){if(Epoch==Value)return;TArray<FString> Keys;Entities.GetKeys(Keys);for(auto& Id:Keys)Remove(Id);PendingPoses.Reset();Tombstones.Reset();PendingDetailRelease.Reset();Epoch=Value;Crowd->SetEpoch(Value);}
void AHeyZedEntities::Apply(const TSharedRef<FJsonObject>& Event)
{
    if(Event->GetNumberField(TEXT("epoch"))!=Epoch)return;
    const FString Type=Event->GetStringField(TEXT("type"));
    if(Type==TEXT("entity_crowd")){Crowd->Apply(Event);return;}
    if(Type==TEXT("entity_detail_release")){PendingDetailRelease.Add(Event->GetStringField(TEXT("id")));return;}
    if(Type==TEXT("entity_reset")){const int64 Previous=Epoch;SetEpoch(-1);SetEpoch(Previous);return;}
    const FString Id=Event->GetStringField(TEXT("id"));
    if(Type==TEXT("entity_pose"))
    {
        FHeyZedEntityPose Pose;Pose.Epoch=Epoch;Pose.Id=Id;
        Pose.Sequence=Event->GetNumberField(TEXT("sequence"));Pose.SampleNs=Event->GetNumberField(TEXT("sample_ns"));Pose.AppearanceRevision=Event->GetNumberField(TEXT("appearance_revision"));
        if(const auto* Entity=Entities.Find(Id)){Pose.Seated=Entity->Seated;Pose.Vehicle=Entity->Vehicle;Pose.Seat=Entity->Seat;}
        Event->TryGetBoolField(TEXT("seated"),Pose.Seated);Event->TryGetStringField(TEXT("vehicle"),Pose.Vehicle);double Seat=-1;if(Event->TryGetNumberField(TEXT("seat"),Seat))Pose.Seat=int32(Seat);
        Pose.World=FTransform(Matrix(Event->GetArrayField(TEXT("world")),false));
        const auto& Velocity=Event->GetArrayField(TEXT("velocity"));Pose.Velocity=FVector(Velocity[0]->AsNumber()*100,Velocity[1]->AsNumber()*100,Velocity[2]->AsNumber()*100);
        FString Encoded;if(Event->TryGetStringField(TEXT("pose"),Encoded))FBase64::Decode(Encoded,Pose.Bones);
        if(Event->TryGetStringField(TEXT("locals"),Encoded))FBase64::Decode(Encoded,Pose.Locals);
        const TSharedPtr<FJsonObject>* Animation=nullptr;if(Event->TryGetObjectField(TEXT("animation"),Animation))FJsonSerializer::Serialize(Animation->ToSharedRef(),TJsonWriterFactory<>::Create(&Pose.Animation));
        ApplyPose(MoveTemp(Pose));return;
    }
    if(Type!=TEXT("entity_state"))return;
    PendingDetailRelease.Remove(Id);
    const int64 Revision=Event->GetNumberField(TEXT("revision"));if(Revision<=Tombstones.FindRef(Id))return;
    if(Event->GetBoolField(TEXT("deleted"))){Remove(Id);Tombstones.Add(Id,Revision);++Changes;return;}
    auto& Entity=Entities.FindOrAdd(Id);if(Revision<=Entity.Revision)return;Entity.Revision=Revision;Entity.Id=Id;Entity.LocalAnimationAcknowledged=false;Entity.Kind=Event->GetStringField(TEXT("kind"));Entity.Received=FPlatformTime::Seconds();
    if(!Entity.Root){Entity.Root=NewObject<USceneComponent>(this);Entity.Root->SetupAttachment(RootComponent);Entity.Root->SetMobility(EComponentMobility::Movable);Entity.Root->RegisterComponent();Entity.Target=FTransform(Matrix(Event->GetArrayField(TEXT("world")),false));Entity.Root->SetWorldTransform(Entity.Target);}
    if(Entity.Kind==TEXT("corpse")){Entity.CrowdBlendStarted=-1;Entity.AnimationState.Reset();Entity.UsingLocalAnimation=false;Entity.AnimationPhases.Reset();Entity.AnimationSample=0;Entity.RootSamples.Reset();Entity.PresentationSample=0;}
    const auto PreviousBones=Entity.BoneNames;
    Entity.BoneNames.Reset();Entity.BoneIndices.Reset();for(auto& Bone:Event->GetArrayField(TEXT("bones"))){FName Name(*Bone->AsString());Entity.BoneIndices.Add(Name,Entity.BoneNames.Add(Name));}
    FString InitialPose;if((Entity.Kind==TEXT("corpse")||Entity.TargetPose.IsEmpty()||PreviousBones!=Entity.BoneNames)&&Event->TryGetStringField(TEXT("pose"),InitialPose)&&DecodeNativePose(InitialPose,Entity.BoneNames.Num(),Entity.TargetPose)){Entity.PreviousPose=Entity.TargetPose;Entity.PoseReceived=FPlatformTime::Seconds();Entity.BoneReceived=Entity.PoseReceived;Entity.BoneSample=0;Entity.NativeSample=0;Entity.EvaluatedSequence=MIN_int64;}
    TArray<FTransform> AppearancePose;FString Captured;if(Event->TryGetStringField(TEXT("pose"),Captured))DecodeNativePose(Captured,Entity.BoneNames.Num(),AppearancePose);
    Entity.PendingParts.Reset();
    for(auto& Value:Event->GetArrayField(TEXT("parts"))){auto P=Value->AsObject();FHeyZedEntityPart Part;Part.Key=P->GetStringField(TEXT("model"));Part.Texture=P->GetStringField(TEXT("texture"));P->TryGetBoolField(TEXT("glass"),Part.HasGlass);P->TryGetBoolField(TEXT("rigid"),Part.Rigid);
        const TSharedPtr<FJsonObject>* Vehicle=nullptr;if(P->TryGetObjectField(TEXT("vehicle"),Vehicle)){Part.Vehicle=*Vehicle;P->TryGetBoolField(TEXT("multi_uv"),Part.MultiUV);P->TryGetBoolField(TEXT("random_paint"),Part.RandomPaint);const TArray<TSharedPtr<FJsonValue>>* Lit=nullptr;if(P->TryGetArrayField(TEXT("Lit"),Lit))for(auto& V:*Lit)Part.Lit.Add(V->AsNumber());}Part.Local=FTransform(Matrix(P->GetArrayField(TEXT("local")),true));Part.PreviousLocal=Part.Local;P->TryGetStringField(TEXT("parent_bone"),Part.AttachmentBone);if(const int32* Bone=Entity.BoneIndices.Find(FName(*Part.AttachmentBone));Bone&&AppearancePose.IsValidIndex(*Bone))Part.AttachmentOffset=Part.Local.GetRelativeTransform(AppearancePose[*Bone]);const auto& T=P->GetArrayField(TEXT("tint"));Part.Tint=FLinearColor(T[0]->AsNumber(),T[1]->AsNumber(),T[2]->AsNumber());Entity.PendingParts.Add(MoveTemp(Part));}
    Resolve(Entity);++Changes;
    if(auto* Pending=PendingPoses.Find(Id)){auto Pose=MoveTemp(*Pending);PendingPoses.Remove(Id);ApplyPose(MoveTemp(Pose));}
}
void AHeyZedEntities::ApplyPose(FHeyZedEntityPose&& Pose)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_ApplyPose);
    if(Pose.Epoch!=Epoch)return;
    auto* Entity=Entities.Find(Pose.Id);
    if(!Entity)
    {
        if(Tombstones.Contains(Pose.Id))return;
        if(const auto* Previous=PendingPoses.Find(Pose.Id);Previous&&Previous->Sequence>=Pose.Sequence)return;
        FString Id=Pose.Id;PendingPoses.Add(MoveTemp(Id),MoveTemp(Pose));return;
    }
    if(Pose.Sequence<=Entity->Sequence)return;Entity->Sequence=Pose.Sequence;
    const bool SeatChanged=Entity->Seated!=Pose.Seated||Entity->Vehicle!=Pose.Vehicle||Entity->Seat!=Pose.Seat;
    Entity->Seated=Pose.Seated;Entity->Vehicle=MoveTemp(Pose.Vehicle);Entity->Seat=Pose.Seat;
    if(SeatChanged)Entity->NativeSample=0;
    const double Now=FPlatformTime::Seconds();const float PreviousAlpha=FMath::Clamp(float((Now-Entity->BoneReceived)/Entity->BlendDuration),0.f,1.f);
    if(!Pose.Bones.IsEmpty()&&Entity->PreviousPose.Num()==Entity->TargetPose.Num())for(int32 Index=0;Index<Entity->PreviousPose.Num();++Index)
    {FTransform Blended;Blended.Blend(Entity->PreviousPose[Index],Entity->TargetPose[Index],PreviousAlpha);Entity->PreviousPose[Index]=Blended;}
    const double Sample=Pose.SampleNs*1.e-9;
    if(Entity->NativeSample>0){const double Interval=Sample-Entity->NativeSample;if(Interval>0&&Interval<.5){Entity->RootInterval=FMath::Lerp(Entity->RootInterval,Interval,.15);Entity->RootJitter=FMath::Lerp(Entity->RootJitter,FMath::Abs((Now-Entity->PoseReceived)-Interval),.15);}}
    const bool RootDiscontinuity=SeatChanged||Entity->RootSamples.IsEmpty()||HeyZedEntityRootDiscontinuity(Entity->RootSamples.Last(),Sample,Pose.World,FMath::Max(Entity->Velocity.Size(),Pose.Velocity.Size()),Entity->Kind==TEXT("vehicle")||Entity->Seated);
    Entity->NativeSample=Sample;Entity->PoseReceived=Now;
    Entity->Target=Pose.World;Entity->Velocity=Pose.Velocity;
    if(RootDiscontinuity){Entity->RootSamples.Reset();Entity->PresentationSample=0;Entity->AnimationSample=0;}
    FHeyZedEntityRootSample RootSample;RootSample.Time=Sample;RootSample.Transform=Pose.World;
    if(Entity->Kind==TEXT("vehicle")&&Pose.AppearanceRevision==Entity->Revision){
        const auto& Parts=Entity->PendingParts.IsEmpty()?Entity->Parts:Entity->PendingParts;
        if(!DecodeNativeMatrices(Pose.Locals,Parts.Num(),RootSample.VehicleLocals)&&!Entity->RootSamples.IsEmpty())
            RootSample.VehicleLocals=Entity->RootSamples.Last().VehicleLocals;
    }
    Entity->RootSamples.Add(MoveTemp(RootSample));if(Entity->RootSamples.Num()>16)Entity->RootSamples.RemoveAt(0,Entity->RootSamples.Num()-16,EAllowShrinking::No);
    Entity->AnimationState.Reset();
    if(!Pose.Animation.IsEmpty())FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Pose.Animation),Entity->AnimationState);
    Entity->AnimationReceived=Now;
    const bool Teleport=RootDiscontinuity;
    if(Teleport&&Entity->Root)Entity->Root->SetWorldTransform(Entity->Target);
    if(Pose.AppearanceRevision==Entity->Revision)
    {
        if(Entity->UsingLocalAnimation&&!Pose.Bones.IsEmpty()&&Entity->PresentedPose.Num()==Entity->BoneNames.Num())Entity->PreviousPose=Entity->PresentedPose;
        if(DecodeNativeMatrices(Pose.Bones,Entity->BoneNames.Num(),Entity->TargetPose)){
            Entity->BlendDuration=Entity->BoneSample>0?FMath::Clamp(Sample-Entity->BoneSample,.025,.25):.05;Entity->BoneSample=Sample;Entity->BoneReceived=Now;
            if(Teleport||SeatChanged||Entity->PreviousPose.Num()!=Entity->TargetPose.Num())Entity->PreviousPose=Entity->TargetPose;
        }
        auto& Parts=Entity->PendingParts.IsEmpty()?Entity->Parts:Entity->PendingParts;TArray<FTransform> Locals;
        if(DecodeNativeMatrices(Pose.Locals,Parts.Num(),Locals))for(int32 Index=0;Index<Parts.Num();++Index)
        {Parts[Index].PreviousLocal=Parts[Index].Local;Parts[Index].Local=Locals[Index];}
    }
    ++Poses;
}
bool AHeyZedEntities::RequestTexture(const FString& Hash,bool Linear)
{
    const FString Key=Hash+(Linear?TEXT("_mask"):TEXT(""));
    if(auto* Texture=Textures.FindRef(Key).Get())return Texture->GetResource()&&Texture->GetResource()->IsInitialized();
    if(!TextureKey(Hash)||LoadingTextures.Contains(Key)||LoadingTextures.Num()>=4)return false;
    const FString Path=FPaths::Combine(TextureDirectory,Hash+TEXT(".png"));
    if(!FPaths::FileExists(Path))return false;
    LoadingTextures.Add(Key);TWeakObjectPtr<AHeyZedEntities> Weak(this);
                    Async(EAsyncExecution::ThreadPool,[Path,Key,Weak,Linear]()
                    {
                        struct FMip {int32 Width,Height;TArray64<uint8> Bytes;};
                        TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_TexturePreparation);
                        TArray<FMip> Mips;TArray<uint8> Compressed;FImage Image,BGRA;
                        if(FFileHelper::LoadFileToArray(Compressed,*Path)&&FImageUtils::DecompressImage(Compressed.GetData(),Compressed.Num(),Image)&&Image.SizeX<=8192&&Image.SizeY<=8192)
                        {
                            Image.CopyTo(BGRA,ERawImageFormat::BGRA8,EGammaSpace::sRGB);
                            Mips.Add({BGRA.SizeX,BGRA.SizeY,MoveTemp(BGRA.RawData)});
                            while(Mips.Last().Width>1||Mips.Last().Height>1)
                            {
                                const auto& Source=Mips.Last();FMip Dest;Dest.Width=FMath::Max(1,Source.Width/2);Dest.Height=FMath::Max(1,Source.Height/2);Dest.Bytes.SetNumUninitialized(int64(Dest.Width)*Dest.Height*4);
                                const EGammaSpace Gamma=Linear?EGammaSpace::Linear:EGammaSpace::sRGB;
                                const FImageView SourceView(const_cast<uint8*>(Source.Bytes.GetData()),Source.Width,Source.Height,1,ERawImageFormat::BGRA8,Gamma);
                                const FImageView DestinationView(Dest.Bytes.GetData(),Dest.Width,Dest.Height,1,ERawImageFormat::BGRA8,Gamma);
                                FImageCore::ResizeImage(SourceView,DestinationView,FImageCore::EResizeImageFilter::Box);
                                Mips.Add(MoveTemp(Dest));
                            }
                        }
                        AsyncTask(ENamedThreads::GameThread,[Weak,Key,Linear,Mips=MoveTemp(Mips)]() mutable
                        {
                            TRACE_CPUPROFILER_EVENT_SCOPE(HeyZedEntities_TextureCommit);
                            auto* Self=Weak.Get();if(!Self)return;Self->LoadingTextures.Remove(Key);if(Mips.IsEmpty())return;
                            auto* Texture=NewObject<UTexture2D>(GetTransientPackage());auto* Data=new FTexturePlatformData();Data->SizeX=Mips[0].Width;Data->SizeY=Mips[0].Height;Data->PixelFormat=PF_B8G8R8A8;
                            for(auto& Source:Mips){auto* Mip=new FTexture2DMipMap();Mip->SizeX=Source.Width;Mip->SizeY=Source.Height;Mip->SizeZ=1;Mip->BulkData.Lock(LOCK_READ_WRITE);void* Target=Mip->BulkData.Realloc(Source.Bytes.Num());FMemory::Memcpy(Target,Source.Bytes.GetData(),Source.Bytes.Num());Mip->BulkData.Unlock();Data->Mips.Add(Mip);}
                            Texture->SetPlatformData(Data);Texture->SRGB=!Linear;Texture->NeverStream=true;Texture->Filter=TF_Default;Texture->LODGroup=TEXTUREGROUP_Character;
                            Texture->UpdateResource();Self->Textures.Add(Key,Texture);
                        });
                    });
    return false;
}

void AHeyZedEntities::Resolve(FHeyZedEntity& Entity)
{
    if(!Resources || Entity.PendingParts.IsEmpty())return;
    bool Ready=true;
    for(auto& Part:Entity.PendingParts)
    {
        auto* Model=Resources->Models.Find(Part.Key);
        if(!Model){if(!LoadingModels.Contains(Part.Key)){LoadingModels.Add(Part.Key);++MissingModels;UE_LOG(LogTemp,Warning,TEXT("heyzed entity model is not prepared: %s"),*Part.Key);}Ready=false;continue;}
        if(!Model->Get())
        {
            Ready=false;
            if(!LoadingModels.Contains(Part.Key)){LoadingModels.Add(Part.Key);FString Key=Part.Key;TWeakObjectPtr<AHeyZedEntities> Weak(this);UAssetManager::GetStreamableManager().RequestAsyncLoad(Model->ToSoftObjectPath(),FStreamableDelegate::CreateLambda([Weak,Key](){if(auto* Self=Weak.Get()){Self->LoadingModels.Remove(Key);if(auto* Loaded=Self->Resources->Models.FindRef(Key).Get())Self->ResidentModels.Add(Key,Loaded);else{++Self->FailedModelLoads;UE_LOG(LogTemp,Warning,TEXT("heyzed entity asset failed to load: %s"),*Key);}for(auto& Pair:Self->Entities)Self->Resolve(Pair.Value);}}));}
        }
        else ResidentModels.Add(Part.Key,Model->Get());
        if(Part.Vehicle.IsValid()&&Resources->VehicleMaterial)
        {
            for(const TCHAR* Name:{TEXT("Paint"),TEXT("Mask"),TEXT("Rust"),TEXT("Lamps"),TEXT("BloodMask"),TEXT("Damage1"),TEXT("Blood"),TEXT("Damage2")})
            {FString Hash;if(Part.Vehicle->TryGetStringField(VehicleMapField(Name),Hash)&&!RequestTexture(Hash,FString(Name)==TEXT("Mask")))Ready=false;}
        }
        else if(!RequestTexture(Part.Texture))Ready=false;
    }
    if(!Ready)return;
    TArray<FHeyZedEntityPart> Previous=MoveTemp(Entity.Parts);
    Entity.Parts=MoveTemp(Entity.PendingParts);Entity.PendingParts.Reset();Entity.EvaluatedSequence=MIN_int64;Entity.OffscreenRootSequence=MIN_int64;Entity.PoseInitialized=false;
    UPoseableMeshComponent* Leader=nullptr;
    for(auto& Part:Entity.Parts)
    {
        auto* Model=Resources->Models.FindRef(Part.Key).Get();
        for(auto& Old:Previous)if(Old.Key==Part.Key&&Old.Mesh)
        {
            const auto* Skin=Cast<UPoseableMeshComponent>(Old.Mesh);const auto* Static=Cast<UStaticMeshComponent>(Old.Mesh);
            if((Skin&&Skin->GetSkinnedAsset()==Model)||(Static&&Static->GetStaticMesh()==Model))
            {
                Part.Mesh=Old.Mesh;Old.Mesh=nullptr;Part.Material=Old.Material;Part.ViewMesh=Old.ViewMesh;Old.ViewMesh=nullptr;
                Part.PreviousLocal=Part.Mesh->GetRelativeTransform();
                if(Part.HasGlass){Part.Glass=Old.Glass;Old.Glass=nullptr;Part.GlassMaterial=Old.GlassMaterial;}
                break;
            }
        }
        if(!Part.Mesh)
        {
            if(auto* Skeletal=Cast<USkeletalMesh>(Model)){auto* Mesh=NewObject<UPoseableMeshComponent>(this);Mesh->SetSkinnedAssetAndUpdate(Skeletal);Part.Mesh=Mesh;}
            else if(auto* Static=Cast<UStaticMesh>(Model)){auto* Mesh=NewObject<UStaticMeshComponent>(this);Mesh->SetStaticMesh(Static);Part.Mesh=Mesh;}
        }
        if(!Part.Mesh)continue;
        if(auto* Skin=Cast<UPoseableMeshComponent>(Part.Mesh)){Skin->PrimaryComponentTick.bStartWithTickEnabled=false;Skin->PrimaryComponentTick.SetTickFunctionEnable(false);if(!Part.Rigid&&Entity.Kind!=TEXT("player")){if(!Leader)Leader=Skin;else Skin->SetLeaderPoseComponent(Leader,true,false);}}
        if(!Part.Mesh->IsRegistered())Part.Mesh->SetupAttachment(Entity.Root);Part.Mesh->SetMobility(EComponentMobility::Movable);Part.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part.Mesh->SetCanEverAffectNavigation(false);Part.Mesh->SetCastShadow(true);Part.Mesh->SetBoundsScale(2);Part.Mesh->SetRelativeTransform(Part.PreviousLocal);if(!Part.Mesh->IsRegistered())Part.Mesh->RegisterComponent();if(auto* Skin=Cast<UPoseableMeshComponent>(Part.Mesh))Skin->SetComponentTickEnabled(false);
        if(Part.Rigid)if(auto* Skin=Cast<UPoseableMeshComponent>(Part.Mesh)){Skin->BoneSpaceTransforms=Skin->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();Skin->MarkRefreshTransformDirty();Skin->RefreshBoneTransforms();}
        UMaterialInterface* Parent=Part.Vehicle.IsValid()&&Resources->VehicleMaterial?Resources->VehicleMaterial.Get():Resources->Material.Get();if(!Part.Material||Part.Material->Parent!=Parent)Part.Material=UMaterialInstanceDynamic::Create(Parent,this);ConfigureMaterial(Part.Material,Part);for(int32 Slot=0;Slot<Part.Mesh->GetNumMaterials();++Slot)Part.Mesh->SetMaterial(Slot,Part.Material);
        if(Part.HasGlass && Resources->GlassMaterial)
        {
            if(!Part.Glass){if(auto* Skeletal=Cast<USkeletalMesh>(Model)){auto* Glass=NewObject<UPoseableMeshComponent>(this);Glass->SetSkinnedAssetAndUpdate(Skeletal);Part.Glass=Glass;}
            else if(auto* Static=Cast<UStaticMesh>(Model)){auto* Glass=NewObject<UStaticMeshComponent>(this);Glass->SetStaticMesh(Static);Part.Glass=Glass;}}
            if(auto* GlassSkin=Cast<UPoseableMeshComponent>(Part.Glass))
            {
                GlassSkin->PrimaryComponentTick.bStartWithTickEnabled=false;GlassSkin->PrimaryComponentTick.SetTickFunctionEnable(false);
                if(auto* OpaqueSkin=Cast<UPoseableMeshComponent>(Part.Mesh))GlassSkin->SetLeaderPoseComponent(OpaqueSkin,true,false);
            }
            if(Part.Glass){if(!Part.Glass->IsRegistered())Part.Glass->SetupAttachment(Entity.Root);Part.Glass->SetMobility(EComponentMobility::Movable);Part.Glass->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part.Glass->SetCanEverAffectNavigation(false);Part.Glass->SetCastShadow(false);Part.Glass->SetRelativeTransform(Part.Local);if(!Part.Glass->IsRegistered())Part.Glass->RegisterComponent();if(auto* Skin=Cast<UPoseableMeshComponent>(Part.Glass))Skin->SetComponentTickEnabled(false);Part.GlassMaterial=UMaterialInstanceDynamic::Create(Part.Vehicle.IsValid()&&Resources->VehicleGlassMaterial?Resources->VehicleGlassMaterial.Get():Resources->GlassMaterial.Get(),this);ConfigureMaterial(Part.GlassMaterial,Part);for(int32 Slot=0;Slot<Part.Glass->GetNumMaterials();++Slot)Part.Glass->SetMaterial(Slot,Part.GlassMaterial);}
        }
    }
    for(auto& Old:Previous){if(Old.Mesh)Old.Mesh->DestroyComponent();if(Old.Glass)Old.Glass->DestroyComponent();if(Old.ViewMesh)Old.ViewMesh->DestroyComponent();}
}
void AHeyZedEntities::ConfigureMaterial(UMaterialInstanceDynamic* Material,const FHeyZedEntityPart& Part)
{
    if(!Material)return;
    Material->SetVectorParameterValue(TEXT("Tint"),Part.Tint);
    if(!Part.Vehicle.IsValid()||!Resources->VehicleMaterial){Material->SetTextureParameterValue(TEXT("NativeDiffuse"),Textures.FindRef(Part.Texture));return;}
    auto& V=Part.Vehicle;
    for(const TCHAR* Name:{TEXT("Paint"),TEXT("Mask"),TEXT("Rust"),TEXT("Lamps"),TEXT("BloodMask"),TEXT("Damage1"),TEXT("Blood"),TEXT("Damage2")}){FString Hash;if(V->TryGetStringField(VehicleMapField(Name),Hash))Material->SetTextureParameterValue(FName(Name),Textures.FindRef(Hash+(FString(Name)==TEXT("Mask")?TEXT("_mask"):TEXT(""))));}
    const auto& Paint=V->GetArrayField(TEXT("paint"));Material->SetVectorParameterValue(TEXT("PaintHSV"),FLinearColor(Paint[0]->AsNumber(),Paint[1]->AsNumber(),Paint[2]->AsNumber()));
    Material->SetScalarParameterValue(TEXT("RustAmount"),V->GetNumberField(TEXT("rust")));
    Material->SetScalarParameterValue(TEXT("HasMask"),V->HasField(TEXT("Mask"))?1:0);
    Material->SetScalarParameterValue(TEXT("MultiUV"),Part.MultiUV?1:0);Material->SetScalarParameterValue(TEXT("RandomPaint"),Part.RandomPaint?1:0);
    for(const TCHAR* Name:{TEXT("Removed"),TEXT("Damage1Enabled"),TEXT("Damage2Enabled"),TEXT("BloodAmount"),TEXT("BloodEnabled"),TEXT("Lit")})
    {
        const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;if(FString(Name)!=TEXT("Lit"))V->TryGetArrayField(Name,Array);
        for(int32 I=0;I<7;++I){FLinearColor Value(0,0,0,0);float* Channels=&Value.R;for(int32 J=0;J<4;++J){const int32 Index=I*4+J;if(Array&&Array->IsValidIndex(Index))Channels[J]=(*Array)[Index]->AsNumber();else if(FString(Name)==TEXT("Lit")&&Part.Lit.IsValidIndex(Index))Channels[J]=Part.Lit[Index];}Material->SetVectorParameterValue(FName(FString(Name)+FString::FromInt(I)),Value);}
    }
}

void AHeyZedEntities::UpdatePresentation(FHeyZedEntity& Entity,double Now)
{
    if(Entity.RootSamples.IsEmpty()){Entity.PresentationRoot=Entity.Target;return;}
    const auto& Samples=Entity.RootSamples;
    const double Age=FMath::Max(0.,Now-Entity.PoseReceived);
    if(Entity.Kind==TEXT("player")&&!Entity.Seated){
        Entity.PresentationSample=FMath::Max(Entity.PresentationSample,Entity.NativeSample+FMath::Min(Age,FMath::Clamp(Entity.RootInterval*1.5,.05,.12)));
        Entity.PresentationRoot=Entity.Target;
        if(Entity.Root)Entity.PresentationRoot.SetRotation(FQuat::Slerp(Entity.Root->GetComponentQuat(),Entity.Target.GetRotation(),1-FMath::Exp(-GetWorld()->GetDeltaSeconds()*24)));
        return;
    }
    const double Horizon=FMath::Clamp(Entity.RootInterval+Entity.RootJitter*2,.04,.20);
    double Requested=Entity.NativeSample+Age-Horizon;
    if(Entity.Seated)if(const auto* Vehicle=Entities.Find(Entity.Vehicle);Vehicle&&Vehicle->PresentationSample>0)
        Requested=Vehicle->PresentationSample;
    const double Time=FMath::Clamp(FMath::Max(Entity.PresentationSample,Requested),Samples[0].Time,Samples.Last().Time);
    Entity.PresentationSample=Time;
    HeyZedSampleEntityRoot(Samples,Time,Entity.PresentationRoot,Entity.PresentationVehicleLocals);
}
void AHeyZedEntities::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=FPlatformTime::Seconds();
    for(auto It=PendingDetailRelease.CreateIterator();It;++It)if(Crowd->HasProxy(*It)){if(auto* Entity=Entities.Find(*It);Entity&&Entity->Root)Crowd->DetailReleased(*It,Entity->Root->GetComponentTransform());Remove(*It);It.RemoveCurrent();}
    if(Now>=NextModelPrune){NextModelPrune=Now+1;TSet<FString> Used;for(const auto& Pair:Entities)for(const auto* Parts:{&Pair.Value.Parts,&Pair.Value.PendingParts})for(const auto& Part:*Parts)Used.Add(Part.Key);for(auto It=ResidentModels.CreateIterator();It;++It)if(!Used.Contains(It.Key()))It.RemoveCurrent();}
    if(Now>=NextTexturePrune){NextTexturePrune=Now+1;TSet<FString> Used;for(auto& Pair:Entities){for(auto* Parts:{&Pair.Value.Parts,&Pair.Value.PendingParts})for(auto& Part:*Parts){Used.Add(Part.Texture);if(Part.Vehicle.IsValid())for(const TCHAR* Name:{TEXT("Paint"),TEXT("Mask"),TEXT("Rust"),TEXT("Lamps"),TEXT("BloodMask"),TEXT("Damage1"),TEXT("Blood"),TEXT("Damage2")}){FString Hash;if(Part.Vehicle->TryGetStringField(VehicleMapField(Name),Hash))Used.Add(Hash+(FString(Name)==TEXT("Mask")?TEXT("_mask"):TEXT("")));}}}for(auto It=Textures.CreateIterator();It;++It)if(!Used.Contains(It.Key()))It.RemoveCurrent();}
    TSet<FString> OccupiedVehicles;for(const auto& Pair:Entities)if(Pair.Value.Kind==TEXT("player")&&Pair.Value.Seated)OccupiedVehicles.Add(Pair.Value.Vehicle);
    for(auto& Pair:Entities)if(Pair.Value.Kind==TEXT("vehicle"))UpdatePresentation(Pair.Value,Now);
    for(auto& Pair:Entities)
    {
        auto& Entity=Pair.Value;Resolve(Entity);if(!Entity.Root)continue;if(Entity.Kind!=TEXT("vehicle"))UpdatePresentation(Entity,Now);if(Entity.Kind==TEXT("zombie")){const bool Proxy=Crowd->ShowProxy(Entity.Id,DetailReady(Entity.Id));if(Entity.Root->IsVisible()==Proxy)Entity.Root->SetVisibility(!Proxy,true);if(Proxy&&!Crowd->WantsDetail(Entity.Id))continue;}if(Entity.Kind==TEXT("player")){const bool Visible=PosePlayer(Entity,Delta);if(Entity.Root->IsVisible()!=Visible)Entity.Root->SetVisibility(Visible,true);continue;}
        const int32 VisibilityPolicy=CVarNativePoseVisibility.GetValueOnGameThread();
        bool RecentlyRendered=true;
        if(VisibilityPolicy!=0&&Entity.PoseInitialized)
        {
            RecentlyRendered=OccupiedVehicles.Contains(Entity.Id);
            for(const auto& Part:Entity.Parts)
                for(const auto* Component:{Part.Mesh.Get(),Part.Glass.Get()})if(Component&&Component->WasRecentlyRendered(1.f))RecentlyRendered=true;
        }
        if(VisibilityPolicy>=2&&!RecentlyRendered)
        {
            if(Entity.OffscreenRootSequence!=Entity.Sequence)
            {
                Entity.OffscreenRootSequence=Entity.Sequence;
                if(!Entity.Root->GetComponentTransform().Equals(Entity.Target,.001)){Entity.Root->SetWorldTransform(Entity.Target);++RootPoseUpdates;}
            }
            else ++RootPoseSkipped;
            Entity.EvaluatedSequence=MIN_int64;++PoseVisibilitySkipped;continue;
        }
        const auto Current=Entity.Root->GetComponentTransform();
        FTransform RootPose=Entity.PresentationRoot;if(Entity.Kind==TEXT("zombie")&&Entity.CrowdBlendStarted>=0&&Now-Entity.CrowdBlendStarted<.15)RootPose.Blend(Entity.CrowdBlendFrom,Entity.PresentationRoot,float(FMath::Clamp((Now-Entity.CrowdBlendStarted)/.15,0.,1.)));if(!Current.Equals(RootPose,.001)){Entity.Root->SetWorldTransform(RootPose);++RootPoseUpdates;}
        if(!RecentlyRendered){Entity.EvaluatedSequence=MIN_int64;++PoseVisibilitySkipped;continue;}
        const float PoseAlpha=FMath::Clamp(float((Now-Entity.PoseReceived)/Entity.BlendDuration),0.f,1.f);
        if(Entity.Kind!=TEXT("vehicle")&&PoseAlpha>=1&&Entity.EvaluatedSequence==Entity.Sequence&&(!Entity.AnimationState.IsValid()||Entity.Kind==TEXT("corpse")))continue;
        if(PoseAlpha>=1)Entity.EvaluatedSequence=Entity.Sequence;
        for(int32 Index=0;Index<Entity.Parts.Num();++Index){auto& Part=Entity.Parts[Index];FTransform Local;
            if(Entity.Kind==TEXT("vehicle")&&Entity.PresentationVehicleLocals.IsValidIndex(Index))Local=Entity.PresentationVehicleLocals[Index];
            else Local.Blend(Part.PreviousLocal,Part.Local,PoseAlpha);
            if(Part.Mesh)Part.Mesh->SetRelativeTransform(Local);if(Part.Glass)Part.Glass->SetRelativeTransform(Local);
        }
        if(Entity.Kind==TEXT("vehicle")&&Entity.BoneNames.IsEmpty()){Entity.PoseInitialized=true;++PoseVisibilityRefreshed;continue;}
        TArray<FTransform> BlendedPose;if(!EvaluateEntityPose(Entity,BlendedPose,Delta))continue;
        for(auto& Part:Entity.Parts)if(Part.Rigid)if(const int32* Bone=Entity.BoneIndices.Find(FName(*Part.AttachmentBone));Bone&&BlendedPose.IsValidIndex(*Bone)){const FTransform Local=Part.AttachmentOffset*BlendedPose[*Bone];if(Part.Mesh)Part.Mesh->SetRelativeTransform(Local);if(Part.Glass)Part.Glass->SetRelativeTransform(Local);}

        for(auto& Part:Entity.Parts)if(!Part.Rigid)for(auto* Component:{Part.Mesh.Get(),Part.Glass.Get()})if(auto* Mesh=Cast<UPoseableMeshComponent>(Component))
        {
            if(Mesh->LeaderPoseComponent.IsValid())continue;
            const auto& Ref=Mesh->GetSkinnedAsset()->GetRefSkeleton();TArray<FTransform> ComponentPose,BindPose;ComponentPose.SetNum(Ref.GetNum());BindPose.SetNum(Ref.GetNum());
            if(Mesh->BoneSpaceTransforms.Num()!=Ref.GetNum())continue;
            for(int32 Bone=0;Bone<Ref.GetNum();++Bone){const int32 Parent=Ref.GetParentIndex(Bone);BindPose[Bone]=Parent<0?Ref.GetRefBonePose()[Bone]:Ref.GetRefBonePose()[Bone]*BindPose[Parent];if(const auto* Native=Entity.BoneIndices.Find(Ref.GetBoneName(Bone)))ComponentPose[Bone]=BlendedPose[*Native];else ComponentPose[Bone]=BindPose[Bone];Mesh->BoneSpaceTransforms[Bone]=Parent<0?ComponentPose[Bone]:ComponentPose[Bone].GetRelativeTransform(ComponentPose[Parent]);}
            Mesh->MarkRefreshTransformDirty();Mesh->RefreshBoneTransforms();Mesh->RefreshFollowerComponents();
        }
        Entity.PoseInitialized=true;if(Entity.Kind==TEXT("zombie")&&!Entity.Root->IsVisible())Entity.Root->SetVisibility(true,true);++PoseVisibilityRefreshed;
    }
    Crowd->Present(Delta);
}
