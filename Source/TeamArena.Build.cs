namespace UnrealBuildTool.Rules
{
    public class TeamArena : ModuleRules
    {
        public TeamArena(TargetInfo Target)
        {
            PrivateIncludePaths.Add("TeamArena/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealTournament",
					"Http",
					"InputCore",
					"Slate",
					"SlateCore",
					"Json",
			        "JsonUtilities",

				}
			);
        }
    }
}
