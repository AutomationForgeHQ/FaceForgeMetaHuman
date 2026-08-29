#include "MetaHumanFaceProvider.h"

#include "FaceForge.h"
#include "FaceCurves.h"

#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Speech2Face.h"
#include "AudioDrivenAnimationConfig.h"
#endif

const FName FMetaHumanFaceProvider::ProviderId(TEXT("MetaHumanAudio"));

namespace
{
#if WITH_EDITOR
	/**
	 * Our mood set to the engine's.
	 *
	 * Written as an explicit switch rather than a cast even though the values line up today, because
	 * they line up only because we copied them. An engine update that inserts a mood would turn a cast
	 * into a character who is quietly angry in every scene, and nothing would report it.
	 */
	EAudioDrivenAnimationMood ToEngineMood(EFaceMood Mood)
	{
		switch (Mood)
		{
		case EFaceMood::Neutral:   return EAudioDrivenAnimationMood::Neutral;
		case EFaceMood::Happy:     return EAudioDrivenAnimationMood::Happiness;
		case EFaceMood::Sad:       return EAudioDrivenAnimationMood::Sadness;
		case EFaceMood::Disgust:   return EAudioDrivenAnimationMood::Disgust;
		case EFaceMood::Anger:     return EAudioDrivenAnimationMood::Anger;
		case EFaceMood::Surprise:  return EAudioDrivenAnimationMood::Surprise;
		case EFaceMood::Fear:      return EAudioDrivenAnimationMood::Fear;
		case EFaceMood::Confident: return EAudioDrivenAnimationMood::Confidence;
		case EFaceMood::Excited:   return EAudioDrivenAnimationMood::Excitement;
		case EFaceMood::Bored:     return EAudioDrivenAnimationMood::Boredom;
		case EFaceMood::Playful:   return EAudioDrivenAnimationMood::Playfulness;
		case EFaceMood::Confused:  return EAudioDrivenAnimationMood::Confusion;
		case EFaceMood::AutoDetect:
		default:                   return EAudioDrivenAnimationMood::AutoDetect;
		}
	}
#endif

	/**
	 * Whether a face-board GUI control belongs to the mouth.
	 *
	 * The engine has a mouth-only mask, but it is expressed in RigLogic *raw* control names while the
	 * solver's output is in GUI control names - two different vocabularies, so its list cannot be used
	 * against this data. This is our own equivalent over the GUI names, which is why the jaw and tongue
	 * are included explicitly: a mouth that opens without a jaw is not a mouth.
	 */
	bool IsMouthControl(const FString& ControlName)
	{
		return ControlName.Contains(TEXT("_mouth"))
			|| ControlName.Contains(TEXT("_jaw"))
			|| ControlName.Contains(TEXT("_tongue"));
	}
}

FMetaHumanFaceProvider::~FMetaHumanFaceProvider()
{
	ReleaseResources();
}

FFaceProviderCaps FMetaHumanFaceProvider::GetCaps() const
{
	FFaceProviderCaps Caps;

	Caps.NativeVocabulary          = EFaceVocabulary::MetaHumanBoard;
	Caps.NativeSolveFps            = 50.f;   // solved at 50 internally, then resampled to what you ask
	Caps.bProducesHeadPose         = true;
	Caps.bSupportsBlinkGeneration  = true;
	Caps.bSupportsMood             = true;
	Caps.bSupportsMouthOnlyMask    = true;
	Caps.bIsMetered                = false;  // free, and that changes how it should be used
	Caps.bRequiresNetwork          = false;
	Caps.bIsDeterministic          = true;

	// Zero until a solve has been timed. Guessing here would produce an estimate that reads like a
	// measurement, which is worse than admitting we have not measured yet.
	Caps.SecondsOfAudioPerSecondOfSolve = MeasuredThroughput;

	return Caps;
}

bool FMetaHumanFaceProvider::IsAvailable(FString& OutReason) const
{
#if WITH_EDITOR
	// The models are plugin content, so a missing MetaHuman plugin shows up here rather than as a
	// link error - which is what makes this a message a user can act on.
	const FString EncoderPath = TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AudioEncoder.NNE_AudioDrivenAnimation_AudioEncoder");

	if (!FPackageName::DoesPackageExist(TEXT("/MetaHuman/Speech2Face/NNE_AudioDrivenAnimation_AudioEncoder")))
	{
		OutReason = TEXT("The MetaHuman plugin's Speech2Face model content was not found. Enable the "
		                 "MetaHuman plugin (Edit > Plugins > MetaHuman Animator) and restart the editor.");
		return false;
	}

	return true;
#else
	OutReason = TEXT("This solver is editor-only.");
	return false;
#endif
}

bool FMetaHumanFaceProvider::EnsureSolver(FString& OutError)
{
#if WITH_EDITOR
	check(IsInGameThread());   // the engine's own Init asserts this; failing here is clearer

	if (Solver.IsValid())
	{
		return true;
	}

	TUniquePtr<FSpeech2Face> Created = FSpeech2Face::Create();

	if (!Created.IsValid())
	{
		OutError = TEXT("Could not create the audio-driven animation solver. Its neural models failed to "
		                "load - check the Output Log for LogSpeech2FaceSolver, and check that NNE has a "
		                "runtime available (NNERuntimeORT).");
		return false;
	}

	Solver = MakeShareable(Created.Release());

	UE_LOG(LogFaceForge, Log, TEXT("Loaded the in-engine audio-driven animation models (~310 MB). They "
		"stay resident until FaceForge.ReleaseModels, because loading them dominates the cost of a "
		"short line."));

	return true;
#else
	OutError = TEXT("Editor-only.");
	return false;
#endif
}

void FMetaHumanFaceProvider::ReleaseResources()
{
	FScopeLock Lock(&SolverLock);
	Solver.Reset();
}

void FMetaHumanFaceProvider::Solve(const FFaceSolveRequest& Request, FOnFaceSolved OnComplete)
{
#if WITH_EDITOR
	FFaceSolveResult Failure;

	const USoundWave* Wave = Request.Audio.Get();
	if (!Wave)
	{
		Failure.Error = TEXT("No audio, or it was garbage collected before the solve started.");
		OnComplete(Failure);
		return;
	}

	if (Request.OutputFps <= 0.f)
	{
		Failure.Error = TEXT("Output frame rate must be positive.");
		OnComplete(Failure);
		return;
	}

	if (!Request.bDownmixChannels &&
		(Request.AudioChannelIndex < 0 || Request.AudioChannelIndex >= Wave->NumChannels))
	{
		// The engine asserts on this rather than returning false, and an assert in a batch takes the
		// editor with it. Caught here so it is an error message instead.
		Failure.Error = FString::Printf(
			TEXT("Channel %d was asked for but '%s' has %d channel(s)."),
			Request.AudioChannelIndex, *Wave->GetName(), Wave->NumChannels);
		OnComplete(Failure);
		return;
	}

	FString SolverError;
	if (!EnsureSolver(SolverError))
	{
		Failure.Error = SolverError;
		OnComplete(Failure);
		return;
	}

	// Keep the wave alive for the duration. The solve runs off the game thread and the engine's
	// implementation dereferences the wave; a weak pointer that survives the check and dies during the
	// read would be a crash with no useful callstack.
	TStrongObjectPtr<const USoundWave> KeepAlive(Wave);

	TSharedPtr<FSpeech2Face> LocalSolver = Solver;

	const float StartOffset  = Request.StartOffsetSeconds;
	const bool  bDownmix     = Request.bDownmixChannels;
	const int32 ChannelIndex = Request.AudioChannelIndex;
	const float Fps          = Request.OutputFps;
	const bool  bBlinks      = Request.bGenerateBlinks;
	const bool  bWantHead    = Request.bWantHeadPose;
	const EFaceSolveMask Mask = Request.Mask;
	const EAudioDrivenAnimationMood EngineMood = ToEngineMood(Request.Mood);
	const float MoodIntensity = FMath::Clamp(Request.MoodIntensity, 0.f, 1.f);
	TFunction<bool()> ShouldCancel = Request.ShouldCancel;

	Async(EAsyncExecution::ThreadPool,
		[this, KeepAlive, LocalSolver, StartOffset, bDownmix, ChannelIndex, Fps, bBlinks, bWantHead,
		 Mask, EngineMood, MoodIntensity, ShouldCancel, OnComplete = MoveTemp(OnComplete)]() mutable
		{
			FFaceSolveResult Result;

			const double Started = FPlatformTime::Seconds();

			{
				// One solve at a time. The instance is documented as parallel-safe, but SetMood is
				// instance state - two overlapping solves would silently share a mood.
				FScopeLock Lock(&SolverLock);

				LocalSolver->SetMood(EngineMood);
				LocalSolver->SetMoodIntensity(MoodIntensity);

				const FSpeech2Face::FAudioParams AudioParams(
					KeepAlive.Get(), StartOffset, bDownmix, ChannelIndex);

				TArray<FSpeech2Face::FAnimationFrame> Animation;
				TArray<FSpeech2Face::FAnimationFrame> HeadAnimation;

				const bool bOk = LocalSolver->GenerateFaceAnimation(
					AudioParams, Fps, bBlinks,
					ShouldCancel ? ShouldCancel : TFunction<bool()>([]() { return false; }),
					Animation, HeadAnimation);

				if (ShouldCancel && ShouldCancel())
				{
					Result.bCancelled = true;
				}
				else if (!bOk)
				{
					Result.Error = TEXT("The solver failed. Check the Output Log for LogSpeech2FaceSolver - "
					                    "the usual causes are audio it could not decode and audio with no "
					                    "speech in it.");
				}
				else if (Animation.Num() == 0)
				{
					Result.Error = TEXT("The solver returned no frames.");
				}
				else
				{
					// Read into the canonical board order rather than whatever order the map iterates.
					// A stable, known order is what lets a mapping resolve its terms once instead of by
					// name per frame, and it makes a missing control detectable.
					const TArray<FName>& Controls = FaceForge::GetMetaHumanBoardControls();

					// Pad to the audio's real length, exactly as the engine's own FSpeechToAnimNode does.
					// The solver returns floor(duration x fps) frames, so without this every clip is up
					// to a frame shorter than the line it belongs to - a small, constant, entirely
					// avoidable drift between the mouth and the voice.
					const int32 AudioFrames = FMath::CeilToInt(KeepAlive->GetDuration() * Fps);
					const int32 FrameCount  = FMath::Max(Animation.Num(), AudioFrames);

					Result.Curves.Vocabulary = EFaceVocabulary::MetaHumanBoard;
					Result.Curves.Fps        = Fps;
					Result.Curves.CurveNames = Controls;
					Result.Curves.Allocate(FrameCount);

					int32 MissingControls = 0;

					for (int32 Frame = 0; Frame < Animation.Num(); ++Frame)
					{
						const FSpeech2Face::FAnimationFrame& SourceFrame = Animation[Frame];

						for (int32 ControlIdx = 0; ControlIdx < Controls.Num(); ++ControlIdx)
						{
							const FString ControlName = Controls[ControlIdx].ToString();

							if (Mask == EFaceSolveMask::MouthOnly && !IsMouthControl(ControlName))
							{
								continue;   // left at zero
							}

							if (const float* Found = SourceFrame.Find(ControlName))
							{
								Result.Curves.SetValue(Frame, ControlIdx, *Found);
							}
							else if (Frame == 0)
							{
								++MissingControls;
							}
						}
					}

					if (MissingControls > 0)
					{
						// Not fatal - the missing ones stay at zero - but it means our transcription of
						// the engine's control list has drifted from the engine's, which is worth
						// knowing before somebody spends an afternoon on a face that is subtly inert.
						UE_LOG(LogFaceForge, Warning,
							TEXT("The solver returned no value for %d of the %d control(s) FaceForge "
							     "expects. The engine's control list may have changed in this version."),
							MissingControls, Controls.Num());
					}

					if (bWantHead && HeadAnimation.Num() > 0)
					{
						Result.HeadPose.Fps = Fps;
						Result.HeadPose.Frames.Reserve(HeadAnimation.Num());

						// Both helpers are marked UE_INTERNAL. Used knowingly: they are the only route
						// from the solver's head track to a transform, and reimplementing the control
						// conversion ourselves would be a worse bet than depending on it breaking
						// loudly at compile time if Epic changes it. Head pose is opt-in per clip, so
						// if it does break, nothing else in the pipeline stops working.
						PRAGMA_DISABLE_DEPRECATION_WARNINGS
						for (FSpeech2Face::FAnimationFrame& HeadFrame : HeadAnimation)
						{
							// The head track arrives as GUI controls; the transform helper reads raw
							// ones. Doing only the second would silently produce an identity transform
							// on every frame - a head that is present, correct and perfectly still.
							UE::MetaHuman::ReplaceHeadGuiControlsWithRaw(HeadFrame);
							Result.HeadPose.Frames.Add(
								UE::MetaHuman::GetHeadPoseTransformFromRawControls(HeadFrame));
						}
						PRAGMA_ENABLE_DEPRECATION_WARNINGS
					}

					Result.ModelIds.Add(TEXT("NNE_AudioDrivenAnimation_AudioEncoder"));
					Result.ModelIds.Add(TEXT("NNE_AudioDrivenAnimation_AnimationDecoder"));

					Result.bSuccess = true;
				}
			}

			Result.SolveWallClockSeconds = static_cast<float>(FPlatformTime::Seconds() - Started);

			AsyncTask(ENamedThreads::GameThread,
				[this, Result, OnComplete = MoveTemp(OnComplete)]() mutable
				{
					// Correct the throughput estimate by measurement, so the next batch's estimate is
					// based on this machine rather than on a number somebody typed.
					if (Result.bSuccess && Result.SolveWallClockSeconds > 0.f)
					{
						const float AudioSeconds = Result.Curves.GetDurationSeconds();
						if (AudioSeconds > 0.f)
						{
							const float Observed = AudioSeconds / Result.SolveWallClockSeconds;
							MeasuredThroughput = MeasuredThroughput > 0.f
								? FMath::Lerp(MeasuredThroughput, Observed, 0.3f)
								: Observed;
						}
					}

					OnComplete(Result);
				});
		});
#else
	FFaceSolveResult Failure;
	Failure.Error = TEXT("This solver is editor-only.");
	OnComplete(Failure);
#endif
}
