#include "HeyZedArchitectureHLOD.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/HLOD/HLODHashBuilder.h"
#include "WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, HeyZedAuthoring);

namespace
{
const FName EnvelopeTag(TEXT("heyzed.ArchitectureEnvelope"));
struct FEnvelope { FBox Bounds=FBox(ForceInit); FString Finish; };

// Authoring actors are immutable for a commandlet pass; no detailed geometry is cached.
const FEnvelope* Envelope(const AActor* Actor)
{
    static TMap<TWeakObjectPtr<const AActor>,FEnvelope> Cache;
    if(!Actor)return nullptr;
    if(auto* Found=Cache.Find(Actor))return Found;
    const auto* Property=FindFProperty<FStrProperty>(Actor->GetClass(),TEXT("Description"));
    if(!Property)return nullptr;
    TSharedPtr<FJsonObject> Data;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Property->GetPropertyValue_InContainer(Actor)),Data)||!Data)return nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Bounds=nullptr;
    const TSharedPtr<FJsonObject>* Metric=nullptr;
    if(!Data->TryGetArrayField(TEXT("bounds"),Bounds)||Bounds->Num()!=4||!Data->TryGetObjectField(TEXT("metric"),Metric))return nullptr;
    const double Floor=(*Metric)->GetNumberField(TEXT("floorHeight"))*100;
    const double HalfWall=(*Metric)->GetNumberField(TEXT("wallThickness"))*50;
    const double Slab=(*Metric)->GetNumberField(TEXT("slabDepth"))*100;
    double Base=0,Levels=1;Data->TryGetNumberField(TEXT("minLevel"),Base);Data->TryGetNumberField(TEXT("levels"),Levels);
    FEnvelope Value;
    Value.Bounds=FBox(FVector((*Bounds)[0]->AsNumber()*100-HalfWall,(*Bounds)[1]->AsNumber()*100-HalfWall,FMath::Max(-Slab,Base*Floor-Slab)),
                      FVector((*Bounds)[2]->AsNumber()*100+HalfWall,(*Bounds)[3]->AsNumber()*100+HalfWall,(Base+Levels)*Floor-Slab));
    Data->TryGetStringField(TEXT("exteriorFinish"),Value.Finish);
    return &Cache.Add(Actor,MoveTemp(Value));
}
}

void UHeyZedArchitectureHLODSettings::ComputeHLODHash(FHLODHashBuilder& Hash) const
{
    const FString Version=TEXT("SharedBuildingEnvelopes1");
    Hash.HashField(Version,TEXT("Representation"));
    Hash << BoxMesh.LoadSynchronous();Hash << DefaultMaterial.LoadSynchronous();
    TArray<FString> Keys;ExteriorMaterials.GetKeys(Keys);Keys.Sort();
    for(const FString& Key:Keys){Hash.HashField(Key,TEXT("Finish"));Hash << ExteriorMaterials[Key].LoadSynchronous();}
}

TSubclassOf<UHLODBuilderSettings> UHeyZedArchitectureHLOD::GetSettingsClass() const
{
    return UHeyZedArchitectureHLODSettings::StaticClass();
}

TArray<UActorComponent*> UHeyZedArchitectureHLOD::Build(const FHLODBuildContext& Context,const TArray<UActorComponent*>& Sources) const
{
    const auto* Settings=CastChecked<UHeyZedArchitectureHLODSettings>(HLODBuilderSettings);
    UStaticMesh* Box=Settings->BoxMesh.LoadSynchronous();
    UMaterialInterface* Default=Settings->DefaultMaterial.LoadSynchronous();
    if(!Box||!Default)UE_LOG(LogHLODBuilder,Fatal,TEXT("Shared building envelope resources are unavailable"));
    TMap<UMaterialInterface*,UInstancedStaticMeshComponent*> Batches;
    TSet<const AActor*> Seen;
    int32 Count=0;
    auto Add=[&](const FTransform& Transform,UMaterialInterface* Material)
    {
        if(!Material)Material=Default;
        auto*& Component=Batches.FindOrAdd(Material);
        if(!Component)
        {
            Component=NewObject<UHLODInstancedStaticMeshComponent>();
            Component->SetStaticMesh(Box);Component->SetMaterial(0,Material);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCanEverAffectNavigation(false);
            Component->ComponentTags.Add(EnvelopeTag);
        }
        Component->AddInstance(Transform);++Count;
    };
    for(UActorComponent* Source:Sources)
    {
        if(const FEnvelope* Value=Envelope(Source->GetOwner()))
        {
            if(Seen.Contains(Source->GetOwner()))continue;
            Seen.Add(Source->GetOwner());
            const FVector Size=Value->Bounds.GetSize();
            if(Size.X<=0||Size.Y<=0||Size.Z<=0)continue;
            const auto* Material=Settings->ExteriorMaterials.Find(Value->Finish);
            Add(FTransform(FQuat::Identity,Value->Bounds.GetCenter(),Size/100),Material?Material->LoadSynchronous():Default);
        }
        else if(auto* Instances=Cast<UInstancedStaticMeshComponent>(Source);Instances&&Instances->ComponentHasTag(EnvelopeTag)&&Instances->GetStaticMesh()==Box)
        {
            for(int32 Index=0;Index<Instances->GetInstanceCount();++Index)
            {
                FTransform Transform;
                if(Instances->GetInstanceTransform(Index,Transform,true))Add(Transform,Instances->GetMaterial(0));
            }
        }
        else UE_LOG(LogHLODBuilder,Fatal,TEXT("Unexpected source for shared building envelopes: %s"),*Source->GetPathName());
    }
    TArray<UActorComponent*> Result;
    for(auto& Pair:Batches){Pair.Value->UpdateBounds();Result.Add(Pair.Value);}
    UE_LOG(LogHLODBuilder,Display,TEXT("Shared building envelopes: %d boxes, %d material batches, one reused 12-triangle mesh; no proxy reduction or atlas baking"),Count,Result.Num());
    return Result;
}
