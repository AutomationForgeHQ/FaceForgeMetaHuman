// UE 5.8's own audio-driven animation solver, behind FaceForge's provider interface.

#pragma once

#include "CoreMinimal.h"
#include "IFaceProvider.h"

/**
 * The engine's offline audio-to-face solver.
 *
 * Free, local and offline. Two ONNX models ship with the MetaHuman plugin - a Whisper-derived audio
 * encoder and an animation decoder, together about 310 MB - and run through NNE on the CPU. There is
 * no account, no key, no network call and no per-use cost, which inverts every economic instinct the
 * speech pipeline teaches: nothing here is worth being frugal about except wall-clock.
 *
 * **Nothing about this provider requires a MetaHuman.** The solver is a pure function from audio to
 * named float curves; the only MetaHuman-specific thing about it is the vocabulary those names are in,
 * and translating that vocabulary is FaceForge's job rather than this provider's.
 */
class FMetaHumanFaceProvider : public IFaceProvider
{
public:

	static const FName ProviderId;

	virtual ~FMetaHumanFaceProvider() override;

	virtual FName GetProviderId() const override { return ProviderId; }
	virtual FString GetDisplayName() const override { return TEXT("MetaHuman Audio-Driven Animation (in-engine)"); }
	virtual FFaceProviderCaps GetCaps() const override;
	virtual bool IsAvailable(FString& OutReason) const override;
	virtual void Solve(const FFaceSolveRequest& Request, FOnFaceSolved OnComplete) override;
	virtual void ReleaseResources() override;

private:

	/**
	 * The loaded solver, created lazily and kept between clips.
	 *
	 * Kept rather than recreated per clip because model loading dominates the cost of a single short
	 * line - the same reasoning that keeps a rented GPU running between generations.
	 *
	 * Creation must happen on the game thread: the engine's implementation asserts on it. Solving does
	 * not, and is where all the time goes, so that is what moves off-thread.
	 */
	TSharedPtr<class FSpeech2Face> Solver;

	/** Guards Solver against a second solve arriving while one is running. */
	mutable FCriticalSection SolverLock;

	/** True once we have measured a solve, so the caps estimate stops being a guess. */
	float MeasuredThroughput = 0.f;

	/** Create the solver if it is not up. Game thread only. */
	bool EnsureSolver(FString& OutError);
};
