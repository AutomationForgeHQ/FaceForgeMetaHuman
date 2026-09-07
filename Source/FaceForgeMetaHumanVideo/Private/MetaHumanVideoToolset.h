// The video solve, as a tool an agent can call.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "MetaHumanVideoToolset.generated.h"

/**
 * Solving a captured video take into a face clip's layer - the one thing this adapter does that
 * `IFaceProvider` has no word for yet.
 *
 * **An adapter earns a toolset when it has operations that are only about itself** - the way
 * FaceForgeACE ships one for hosting Audio2Face: its container, its GPU, and why the local runner
 * refuses to start. This adapter has nothing of the kind - it runs in-engine and has no machine to
 * manage - and an adapter reached through the core's own abstraction needs no second way to be
 * called.
 *
 * This tool exists only because the abstraction is audio-shaped: a provider turns sound into
 * curves, and nothing in it describes turning *pictures* into them.
 *
 * So this is not waiting for a surface to grow. It is waiting for **the video solve to graduate
 * into `IFaceProvider`** - after which FaceForge's own toolset covers it and this class is deleted
 * rather than moved.
 */
UCLASS()
class UMetaHumanVideoToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/**
	 * Solve a recorded video take through the MetaHuman monocular pipeline into a face clip layer.
	 *
	 * The performance-capture bridge: the take's frames become full-face acting curves in a named
	 * layer, kept whole beside whatever other layers the clip holds. Merge Face Clip Layers is the
	 * separate, cheap second step, and baking is unchanged.
	 *
	 * Minutes of local GPU/CPU work for seconds of footage; one solve at a time. Every intermediate
	 * asset - image sequence, imported audio, footage data, the performance asset with the full
	 * per-frame solve - is saved under the FaceForge output path for inspection.
	 *
	 * @param BankPath Content path of a face bank.
	 * @param ClipId Which clip the layer lands on.
	 * @param LayerId Name for the layer, e.g. "Video". Replaces a layer of the same name.
	 * @param TakeDir Absolute path of a take folder (frames/ and source_audio.wav inside), or
	 *        "latest" for the newest folder under the project's PerformanceForge/Takes.
	 * @param Weight How strongly this layer overrides what is under it at merge, 0 to 1.
	 */
	UFUNCTION(meta = (AICallable), Category = "FaceForge|Video")
	static UToolCallAsyncResultString* SolveVideoTakeToLayer(
		const FString& BankPath, FName ClipId, FName LayerId, const FString& TakeDir, float Weight);
};
