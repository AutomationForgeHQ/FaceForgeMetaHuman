// One video solve: a PerformanceForge take through MetaHuman Animator, into a face clip layer.

#pragma once

#include "CoreMinimal.h"

class UFaceBank;
class UMetaHumanPerformance;

/** What a video solve needs. Nothing here is looked up later; validation happens up front. */
struct FVideoSolveRequest
{
	/** Absolute path of a take folder: frames/, source_audio.wav, take_manifest.json. */
	FString TakeDir;

	/** Content path of the face bank holding the clip. */
	FString BankPath;

	FName ClipId;

	/** Layer the curves land in, e.g. "Video". Replaces a layer of the same name. */
	FName LayerId;

	/** How strongly this layer overrides what is under it at merge, 0..1. */
	float Weight = 1.f;
};

/**
 * Runs Epic's monocular pipeline over a recorded take and writes the result into a clip layer.
 *
 * The flow is the one the MetaHuman Animator UI performs, minus the UI: an image sequence and a
 * sound wave wrapped into a UFootageCaptureData, a UMetaHumanPerformance pointed at it with
 * InputType = MonoFootage, StartPipeline, and GetAnimationData read back on completion. Every
 * asset created on the way is a real asset under the FaceForge output path, saved and inspectable,
 * because a solve whose intermediates are transient cannot be debugged when it produces a face
 * that does not move.
 *
 * Asynchronous: StartPipeline runs the pipeline on worker threads and the completion delegate
 * fires back on the game thread. OnComplete always runs on the game thread.
 */
class FMetaHumanVideoSolver
{
public:

	/** Begin a solve. Refuses immediately (false, reason) when the take or the clip is unusable. */
	static bool Solve(const FVideoSolveRequest& Request, TFunction<void(bool, FString)> OnComplete, FString& OutError);

	/** True while a solve is in flight. One at a time: the pipeline saturates the machine. */
	static bool IsRunning();
};
