// The in-flight state of one video solve, alive from StartPipeline to the layer write.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MetaHumanVideoSolve.h"
#include "MetaHumanVideoSolveTask.generated.h"

class UFaceBank;
class UMetaHumanPerformance;
class USoundWave;

/**
 * A UObject because the pipeline's completion is a dynamic delegate, and a dynamic delegate binds
 * to a UFUNCTION on a UObject. It also pins everything the solve is using - the performance asset
 * above all - against garbage collection for the duration.
 */
UCLASS()
class UMetaHumanVideoSolveTask : public UObject
{
	GENERATED_BODY()

public:

	void Run(TFunction<void(bool, FString)> InOnComplete);

	UFUNCTION()
	void OnPipelineFinished();

	FVideoSolveRequest Request;

	UPROPERTY()
	TObjectPtr<UMetaHumanPerformance> Performance;

	UPROPERTY()
	TObjectPtr<USoundWave> ImportedAudio;

	/** Frames on disk when the solve started, for the layer's provenance and hash. */
	int32 TakeFrameCount = 0;

	double StartSeconds = 0.0;

private:

	void Finish(bool bSuccess, const FString& Message);

	TFunction<void(bool, FString)> OnComplete;
};
