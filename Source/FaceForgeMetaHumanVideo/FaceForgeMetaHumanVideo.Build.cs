using UnrealBuildTool;

public class FaceForgeMetaHumanVideo : ModuleRules
{
	public FaceForgeMetaHumanVideo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"FaceForge",   // the clip, the layers, the curve types this delivers into
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"AssetTools",
				"AssetRegistry",
				"AudioEditor",   // USoundFactory, for importing the take's WAV master
				"Json",
				"Projects",

				// The mono-video solve itself. MetaHumanPerformance is the pipeline asset;
				// MetaHumanCoreTech carries FFrameAnimationData, the per-frame result type.
				// MetaHumanPerformance.h's own includes drag in the CaptureData headers, which is
				// why both CaptureData modules are named here rather than only the core one.
				"MetaHumanPerformance",
				"MetaHumanCoreTech",
				"CaptureDataCore",
				"CaptureDataEditor",
				"ImgMedia",

				// The one tool this module publishes. See MetaHumanVideoToolset.h for why it lives
				// here rather than in a sidecar plugin, for now.
				"ToolsetRegistry",
			}
			);
	}
}
