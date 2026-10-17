#pragma once
#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "StaticMeshCompiler.h"
#include "DistanceFieldAtlas.h"
#include "MeshCardBuild.h"

inline void FinishHeyZedMesh(UStaticMesh* Mesh)
{
    FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
    // Thin construction solids and aperture edges must also exist in the
    // ray tracing representation used by local lights and Lumen.
    auto& Nanite=Mesh->GetNaniteSettings();
    if (Mesh->GetPathName().StartsWith(TEXT("/Game/HeyZed/Architecture/Meshes/")) && Nanite.bEnabled &&
        (Nanite.FallbackTarget!=ENaniteFallbackTarget::RelativeError || Nanite.FallbackRelativeError!=0 ||
         Nanite.FallbackPercentTriangles!=1 || !Mesh->bSupportRayTracing))
    {
        Nanite.FallbackTarget=ENaniteFallbackTarget::RelativeError;
        Nanite.FallbackRelativeError=0; Nanite.FallbackPercentTriangles=1;
        Mesh->bSupportRayTracing=true;
        Mesh->Build(false);
        FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
        Mesh->MarkPackageDirty();
    }
    if (GDistanceFieldAsyncQueue) GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Mesh,false);
    if (GCardRepresentationAsyncQueue) GCardRepresentationAsyncQueue->BlockUntilBuildComplete(Mesh,false);
}
#endif
