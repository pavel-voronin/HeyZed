using UnrealBuildTool;
public class HeyZedTarget : TargetRules
{
    public HeyZedTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        WindowsPlatform.WindowsSdkVersion = "10.0.26100.0";
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("HeyZed");
    }
}
