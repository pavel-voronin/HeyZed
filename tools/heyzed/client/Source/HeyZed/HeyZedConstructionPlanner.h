#pragma once
#include "CoreMinimal.h"
class FJsonObject;
struct FHeyZedConstructionPlacement
{
    TSharedPtr<FJsonObject> Recipe;
    FVector Position;
    float Yaw;
    TArray<FName> Materials;
    FString Door;
    FVector Scale;
    FString RecipeText;
};


struct FHeyZedConstructionCatalog
{
    TMap<FString,FString> Definitions,SourceBindings;
    TMap<FString,FName> SurfaceBindings;
    TSet<FName> Materials;
};
struct FHeyZedConstructionSnapshot
{
    TSharedPtr<const FHeyZedConstructionCatalog,ESPMode::ThreadSafe> Catalog;
    TMap<FString,FString> Cells;
    TSet<FString> OwnedFloors,Interior;
    double Floor=0,Thickness=0,Slab=0;
};
struct FHeyZedConstructionCatalogView
{
    explicit FHeyZedConstructionCatalogView(const FHeyZedConstructionCatalog& Data)
        :Definitions(Data.Definitions),SourceBindings(Data.SourceBindings),SurfaceBindings(Data.SurfaceBindings),Materials(Data.Materials){}
    const TMap<FString,FString>& Definitions;
    const TMap<FString,FString>& SourceBindings;
    const TMap<FString,FName>& SurfaceBindings;
    const TSet<FName>& Materials;
    TMap<FString,TSharedPtr<FJsonObject>> ParsedDefinitions,ParsedSources;
    TSharedPtr<FJsonObject> Definition(const FString& Name);
    TSharedPtr<FJsonObject> Source(const FString& Name);
};
class FHeyZedConstructionPlanner
{
public:
    explicit FHeyZedConstructionPlanner(const FHeyZedConstructionSnapshot& Snapshot);
    TArray<FHeyZedConstructionPlacement> Plan(const FString& Address);
private:
    FHeyZedConstructionCatalogView Library;
    FHeyZedConstructionCatalogView* Resources;
    TMap<FString,TSharedPtr<FJsonObject>> Cells;
    TSet<FString> OwnedFloors,Interior;
    double Floor,Thickness,Slab;
    TArray<FHeyZedConstructionPlacement> CollectedParts;
    static FString Key(int32 X,int32 Y,int32 Z);
    bool HasSlabAbove(int32 X,int32 Y,int32 Z) const;
    TSharedRef<FJsonObject> SlabRecipe(int32 X,int32 Y,int32 Z);
    bool RebuildTyped(const FString& Address,int32 X,int32 Y,int32 Z,bool Save);
    void GatherParts(const FString& Address,bool Save);
    void Place(const FString& Address,const TSharedRef<FJsonObject>& Recipe,const FVector& Position,float Yaw,const TArray<FName>& Materials,bool Save,const FString& Door=FString(),const FVector& Scale=FVector::OneVector);
};
