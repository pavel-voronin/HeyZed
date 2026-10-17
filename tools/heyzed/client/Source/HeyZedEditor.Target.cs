using UnrealBuildTool;
public class HeyZedEditorTarget : TargetRules
{
    public HeyZedEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        WindowsPlatform.WindowsSdkVersion = "10.0.26100.0";
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("HeyZed");
        ExtraModuleNames.Add("HeyZedAuthoring");
    }
}
