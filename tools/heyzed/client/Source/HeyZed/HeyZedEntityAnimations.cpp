#include "HeyZedEntities.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UHeyZedEntityResources::RegisterNativeAnimations(const FString& Json)
{
    TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||!Data)return false;
    TMap<FString,TSoftObjectPtr<UAnimSequence>> Prepared;
    for(const auto& Pair:Data->Values){const FString Path=Pair.Value->AsString();if(!Path.StartsWith(TEXT("/Game/HeyZed/Entities/Animations/")))return false;Prepared.Add(FString(Pair.Key).ToLower(),TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Path)));}
    NativeAnimations=MoveTemp(Prepared);MarkPackageDirty();return true;
}
#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/SavePackage.h"

namespace {
struct FNativeAnimationKey { double Time; FTransform Transform; };
FTransform AnimationTransform(const TSharedRef<FJsonObject>& Key)
{
    const auto& Values=Key->GetArrayField(TEXT("matrix"));
    check(Values.Num()==16);
    FMatrix Matrix,Basis=FMatrix::Identity;
    Basis.M[0][0]=-100;Basis.M[1][1]=0;Basis.M[1][2]=100;Basis.M[2][1]=100;Basis.M[2][2]=0;
    for(int32 Row=0;Row<4;++Row)for(int32 Column=0;Column<4;++Column)Matrix.M[Row][Column]=Values[Row*4+Column]->AsNumber();
    return FTransform(Basis.Inverse()*Matrix*Basis);
}
}
#endif

UObject* UHeyZedEntityResources::PrepareNativeAnimation(const FString& Json,const FString& ClipName,USkeletalMesh* Mesh,const FString& AssetPath)
{
#if WITH_EDITOR
    if(!Mesh||!Mesh->GetSkeleton()||!AssetPath.StartsWith(TEXT("/Game/HeyZed/Entities/Animations/")))return nullptr;
    if(auto* Existing=LoadObject<UAnimSequence>(nullptr,*(AssetPath+TEXT(".")+FPackageName::GetShortName(AssetPath))))return Existing;
    TSharedPtr<FJsonObject> Source;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Source)||!Source||Source->GetIntegerField(TEXT("format"))!=2)return nullptr;
    TSharedPtr<FJsonObject> Clip;
    for(const auto& Value:Source->GetArrayField(TEXT("clips")))if(Value->AsObject()->GetStringField(TEXT("name"))==ClipName){Clip=Value->AsObject();break;}
    if(!Clip)return nullptr;
    const double Duration=Clip->GetNumberField(TEXT("duration"));
    if(!FMath::IsFinite(Duration)||Duration<=0||Duration>600)return nullptr;
    const int32 FrameCount=FMath::Max(1,FMath::RoundToInt(Duration*60));
    TMap<FName,TArray<FNativeAnimationKey>> Tracks;
    for(const auto& Pair:Clip->GetObjectField(TEXT("tracks"))->Values)
    {
        const FName Name(*Pair.Key);
        if(Mesh->GetRefSkeleton().FindBoneIndex(Name)==INDEX_NONE){UE_LOG(LogTemp,Warning,TEXT("heyzed animation %s: source-only bone %s"),*ClipName,*Pair.Key);continue;}
        auto& Keys=Tracks.Add(Name);
        for(const auto& Value:Pair.Value->AsArray())
        {
            const auto Key=Value->AsObject();const double Time=Key->GetNumberField(TEXT("time"));
            if(!FMath::IsFinite(Time)||Time<0||Time>Duration+1.e-4||Key->GetArrayField(TEXT("matrix")).Num()!=16)return nullptr;
            const FTransform Transform=AnimationTransform(Key.ToSharedRef());
            if(Transform.ContainsNaN())return nullptr;
            Keys.Add({Time,Transform});
        }
        if(Keys.IsEmpty())return nullptr;
        Keys.Sort([](const auto& A,const auto& B){return A.Time<B.Time;});
    }
    if(Tracks.IsEmpty())return nullptr;
    auto* Package=CreatePackage(*AssetPath);
    auto* Animation=NewObject<UAnimSequence>(Package,*FPackageName::GetShortName(AssetPath),RF_Public|RF_Standalone);
    Animation->SetSkeleton(Mesh->GetSkeleton());Animation->SetPreviewMesh(Mesh);
    Animation->bEnableRootMotion=false;
    auto& Controller=Animation->GetController();Controller.InitializeModel();
    {
    Controller.OpenBracket(NSLOCTEXT("heyzed","NativeAnimationImport","Import source animation"),false);
    ON_SCOPE_EXIT { Controller.CloseBracket(false); };
    Controller.SetFrameRate(FFrameRate(60,1),false);Controller.SetNumberOfFrames(FFrameNumber(FrameCount),false);
    for(const auto& Pair:Tracks)
    {
        TArray<FVector3f> Positions,Scales;TArray<FQuat4f> Rotations;
        Positions.Reserve(FrameCount+1);Scales.Reserve(FrameCount+1);Rotations.Reserve(FrameCount+1);
        int32 Cursor=0;const auto& Keys=Pair.Value;
        for(int32 Frame=0;Frame<=FrameCount;++Frame)
        {
            const double Time=Duration*Frame/FrameCount;
            while(Cursor+1<Keys.Num()&&Keys[Cursor+1].Time<=Time)++Cursor;
            FTransform Transform=Keys[Cursor].Transform;
            if(Cursor+1<Keys.Num())
            {
                const auto& Next=Keys[Cursor+1];const double Span=Next.Time-Keys[Cursor].Time;
                Transform.Blend(Keys[Cursor].Transform,Next.Transform,Span>SMALL_NUMBER?FMath::Clamp((Time-Keys[Cursor].Time)/Span,0.,1.):1.);
            }
            Positions.Add(FVector3f(Transform.GetTranslation()));Scales.Add(FVector3f(Transform.GetScale3D()));Rotations.Add(FQuat4f(Transform.GetRotation()));
        }
        if(!Controller.AddBoneCurve(Pair.Key,false)||!Controller.SetBoneTrackKeys(Pair.Key,Positions,Rotations,Scales,false))return nullptr;
    }
    Controller.NotifyPopulated();
    }
    Animation->MarkPackageDirty();Animation->PostEditChange();FAssetRegistryModule::AssetCreated(Animation);
    const FString Filename=FPackageName::LongPackageNameToFilename(AssetPath,FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Package,Animation,*Filename,Args)?Animation:nullptr;
#else
    return nullptr;
#endif
}
