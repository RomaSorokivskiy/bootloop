using UnrealBuildTool;

public class BOOTLOOPTarget : TargetRules
{
    public BOOTLOOPTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BOOTLOOP");
    }
}
