using UnrealBuildTool;

public class HeyZedAuthoring : ModuleRules
{
    public HeyZedAuthoring(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "WorldPartitionHLODUtilities", "UnrealEd", "RenderCore", "RHI", "MeshMergeUtilities", "MeshDescription", "StaticMeshDescription", "MaterialUtilities" });
        PrivateDependencyModuleNames.AddRange(new[] { "TraceInsights", "TraceServices", "TraceAnalysis" });
    }
}
