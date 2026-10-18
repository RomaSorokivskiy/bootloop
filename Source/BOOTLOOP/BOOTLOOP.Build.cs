using UnrealBuildTool;

public class BOOTLOOP : ModuleRules
{
    public BOOTLOOP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AudioMixer" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "UMG", "Slate", "SlateCore", "AudioSynesthesiaCore", "AudioAnalyzer" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(new[] { "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib", "bcrypt.lib" });
        }
    }
}
