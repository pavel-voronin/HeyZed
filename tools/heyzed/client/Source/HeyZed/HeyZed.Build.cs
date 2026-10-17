using UnrealBuildTool;
public class HeyZed : ModuleRules
{
    public HeyZed(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "Sockets", "Networking", "Json", "JsonUtilities", "ProceduralMeshComponent", "RenderCore", "MeshDescription", "StaticMeshDescription", "ImageCore" });
        PublicDependencyModuleNames.AddRange(new[] { "AnimGraphRuntime", "PoseSearch", "BlendStack", "AnimationWarpingRuntime", "RHI", "IKRig", "MoviePlayer" });
        if (Target.Platform == UnrealTargetPlatform.Win64) PrivateDependencyModuleNames.Add("D3D12RHI");
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new[] { "AssetRegistry", "SkeletalMeshUtilitiesCommon", "TargetPlatform", "IKRig", "IKRigEditor", "UnrealEd", "MeshUtilities", "AnimGraph", "BlueprintGraph", "KismetCompiler", "PoseSearchEditor" });
    }
}
