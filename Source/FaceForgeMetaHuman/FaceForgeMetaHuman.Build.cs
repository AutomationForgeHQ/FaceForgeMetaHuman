using UnrealBuildTool;

public class FaceForgeMetaHuman : ModuleRules
{
	public FaceForgeMetaHuman(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"FaceForge",   // the capability this provider plugs into
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"Projects",

				// The engine's solver, and the chain of modules its public header drags in.
				//
				// Speech2Face.h includes AudioDrivenAnimationConfig.h -> FrameAnimationData.h and
				// AudioDrivenAnimationMood.h (both MetaHumanCoreTech) -> SpeechAnimationSolverTypes.h
				// (SpeechAnimationSolver, in the hidden StreamingADA plugin, which the .uplugin enables).
				// MetaHumanSpeech2Face lists the last two as *private* dependencies, so their headers are
				// not on our include path by inheritance and have to be named here.
				"MetaHumanSpeech2Face",
				"MetaHumanCoreTech",
				"SpeechAnimationSolver",

				// AudioDrivenAnimationMood.h declares a Slate widget and, under WITH_EDITOR, includes
				// PropertyHandle.h. Including a solver header should not require a UI dependency, but it
				// does, and discovering that at link time is worse than writing it down.
				"Slate",
				"SlateCore",
				"PropertyEditor",
			}
			);
	}
}
