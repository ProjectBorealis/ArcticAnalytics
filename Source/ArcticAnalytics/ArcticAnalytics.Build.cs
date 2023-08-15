using UnrealBuildTool;
using System.IO;
using System;

namespace UnrealBuildTool.Rules
{
    public class ArcticAnalytics : ModuleRules
    {
        public ArcticAnalytics(ReadOnlyTargetRules Target) : base(Target)
        {
            PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
					"HTTP",
					"Json",
					"JsonUtilities"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Analytics"
                }
            );

            PublicIncludePathModuleNames.AddRange(
                new string[]
                {
                    "Analytics"
                }
            );
        }
    }
}
