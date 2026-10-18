using UnrealBuildTool;

public class BOOTLOOPEditorTarget : TargetRules
{
    public BOOTLOOPEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("BOOTLOOP");
    }
}
