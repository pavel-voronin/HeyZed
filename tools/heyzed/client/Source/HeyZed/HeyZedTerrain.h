#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "InstanceDataTypes.h"
#include "HeyZedTerrain.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;
class UHierarchicalInstancedStaticMeshComponent;
class FJsonObject;
struct FHeyZedGroundLayer;

USTRUCT(BlueprintType)
struct FHeyZedGroundShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Material;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector2D> Vertices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector4 Boundary=FVector4(0,0,0,0);
};

USTRUCT(BlueprintType)
struct FHeyZedGroundTile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Parts;
};

USTRUCT(BlueprintType)
struct FHeyZedSurfaceStyle
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Diffuse;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Normal;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Roughness;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Height;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Tint=FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Width=2;
};

UCLASS(BlueprintType)
class UHeyZedGroundLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, TObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, FHeyZedGroundShape> Overlays;
    UPROPERTY() TMap<FName,TObjectPtr<UStaticMesh>> OverlayMeshes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Recipe;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName,FHeyZedGroundTile> Tiles;
    void Resolve(const TArray<FHeyZedGroundLayer>& Layers,TArray<FName>& Parts,FName& Base) const;
    bool GrassAt(const TArray<FName>& Parts,FVector2D Point,FName Base) const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName,FHeyZedSurfaceStyle> Surfaces;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> GrassMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UStaticMesh> GrassMesh;
    static bool IsAsphalt(FName Name);
    static bool IsSoftSurface(FName Name);
    UFUNCTION(BlueprintCallable, Category="heyzed") bool PrepareGrass();
    UFUNCTION(BlueprintCallable, Category="heyzed") bool PrepareMeshes();
};

struct FHeyZedGroundLayer
{
    FName Name, Material;
    FString Edges;
    bool Overlay = false;
};
struct FHeyZedPreparedCell
{
    FIntPoint Address;
    TArray<FHeyZedGroundLayer> Layers;
    FString Signature;
    int64 Version = -1;
    uint64 Generation = 1;
};
struct FHeyZedPreparedGround
{
    int64 Epoch = 0, Revision = 0;
    FIntPoint Center = FIntPoint::ZeroValue;
    TArray<FHeyZedPreparedCell> Cells;
    bool bBaked = false;
    double DecodeMs = 0;
    static TSharedPtr<FHeyZedPreparedGround> Decode(const FString& Text);
    static TSharedPtr<FHeyZedPreparedGround> Prepare(const TSharedRef<FJsonObject>& Snapshot);
};

USTRUCT()
struct FHeyZedGroundInstance
{
    GENERATED_BODY()
    FName Key;
    FPrimitiveInstanceId Id;
};
USTRUCT()
struct FHeyZedGroundCell
{
    GENERATED_BODY()
    FString Signature, RenderSignature;
    TArray<FHeyZedGroundLayer> Layers;
    FName Base, Blend;
    TArray<FName> Parts;
    FVector4f Corners=FVector4f(0,0,0,0), Edges=FVector4f(0,0,0,0);
    bool bBlend=false;
    TSet<FString> Unresolved;
    TArray<FHeyZedGroundInstance> Instances;
    TArray<FPrimitiveInstanceId> Grass;
    bool bLive = false;
    int64 SourceEpoch = 0, SourceRevision = -1;
    uint64 Generation = 1;
    int32 Tiles = 0;
};

UCLASS()
class AHeyZedTerrain : public AActor
{
    GENERATED_BODY()
public:
    AHeyZedTerrain();
    virtual void Tick(float Delta) override;
    void BaseLoaded(FIntPoint NativeCell);
    void Apply(const FHeyZedPreparedGround& Snapshot, const FVector& NativeOrigin);
    void SetArchitectureFloor(FIntPoint Address,bool Claimed);
    bool HasSurface(FIntPoint Address) const {const auto* Cell=Cells.Find(Address);return Cell && (!Cell->Instances.IsEmpty() || !Cell->Base.IsNone());}
    int32 Tiles = 0;
    uint64 RebuiltCells = 0, AppliedPatches = 0;
    TSet<FString> Unresolved;
private:
    void BuildCells(const TSet<FIntPoint>& Addresses,const FVector& NativeOrigin);
    TSet<FIntPoint> ArchitectureFloors;
    FVector GroundOrigin=FVector::ZeroVector;
    double NextGrass=0;
    TSet<FIntPoint> DirtyGrass;
    UPROPERTY() TMap<FIntPoint,TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GrassChunks;
    TMap<FString,int32> UnresolvedCounts;
    UPROPERTY() TMap<FName,TObjectPtr<UInstancedStaticMeshComponent>> Meshes;
    UPROPERTY() TMap<FIntPoint,FHeyZedGroundCell> Cells;
    UPROPERTY() TObjectPtr<UHeyZedGroundLibrary> Library;
};
