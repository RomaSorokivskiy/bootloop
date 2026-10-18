using UnrealBuildTool;

public class BOOTLOOP : ModuleRules
{
    public BOOTLOOP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // UI translation units have independent local palettes/helpers. Keep
        // their anonymous namespaces separate in Editor and packaged targets.
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AudioMixer" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "UMG", "Slate", "SlateCore", "ApplicationCore", "AudioSynesthesiaCore", "AudioAnalyzer" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(new[] { "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib", "bcrypt.lib" });
        }
    }
}
