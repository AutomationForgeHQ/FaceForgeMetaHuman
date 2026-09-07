#include "MetaHumanVideoSolve.h"
#include "MetaHumanVideoSolveTask.h"

#include "FaceForgeMetaHumanVideo.h"

#include "FaceClip.h"
#include "FaceCurves.h"
#include "FaceForgeSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "CaptureData.h"
#include "Dom/JsonObject.h"
#include "Factories/SoundFactory.h"
#include "FrameAnimationData.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ImgMediaSource.h"
#include "MetaHumanPerformance.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace MetaHumanVideoPrivate
{
	/** The one solve in flight. The pipeline saturates the machine; a queue would be a lie. */
	static TStrongObjectPtr<UMetaHumanVideoSolveTask> ActiveTask;

	/** SHA1 of a UTF-8 string, rendered as hex. Same construction as the clip hashes. */
	FString HashString(const FString& In)
	{
		const FTCHARToUTF8 Utf8(*In);
		FSHA1 Sha;
		Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Sha.Final();
		FSHAHash Hash;
		Sha.GetHash(Hash.Hash);
		return Hash.ToString();
	}

	void SaveAssetPackage(UObject* Asset)
	{
		if (!Asset)
		{
			return;
		}

		UPackage* Package = Asset->GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs);
	}

	/** Read the measured frame rate out of the take's own manifest. Zero when it cannot be read. */
	float ReadMeasuredFps(const FString& TakeDir)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *(TakeDir / TEXT("take_manifest.json"))))
		{
			return 0.f;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return 0.f;
		}

		const TSharedPtr<FJsonObject>* Video = nullptr;
		if (!Root->TryGetObjectField(TEXT("video"), Video))
		{
			return 0.f;
		}

		double Fps = 0.0;
		(*Video)->TryGetNumberField(TEXT("measured_fps"), Fps);
		return static_cast<float>(Fps);
	}

	template <typename T>
	T* CreateAsset(const FString& PackagePath, const FString& AssetName)
	{
		UPackage* Package = CreatePackage(*(PackagePath / AssetName));
		if (!Package)
		{
			return nullptr;
		}

		T* Asset = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone);
		if (Asset)
		{
			FAssetRegistryModule& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			AssetRegistry.Get().AssetCreated(Asset);
		}
		return Asset;
	}
}

bool FMetaHumanVideoSolver::IsRunning()
{
	return MetaHumanVideoPrivate::ActiveTask.IsValid();
}

bool FMetaHumanVideoSolver::Solve(
	const FVideoSolveRequest& Request, TFunction<void(bool, FString)> OnComplete, FString& OutError)
{
	using namespace MetaHumanVideoPrivate;

	if (IsRunning())
	{
		OutError = TEXT("A video solve is already running. One at a time: the pipeline saturates the machine.");
		return false;
	}

	// ---------------------------------------------------------------------------------------------
	// Validate the take before creating anything.
	// ---------------------------------------------------------------------------------------------

	const FString FramesDir = Request.TakeDir / TEXT("frames");
	const FString AudioPath = Request.TakeDir / TEXT("source_audio.wav");

	TArray<FString> FrameFiles;
	IFileManager::Get().FindFiles(FrameFiles, *(FramesDir / TEXT("frame_*.jpg")), true, false);
	if (FrameFiles.Num() == 0)
	{
		IFileManager::Get().FindFiles(FrameFiles, *(FramesDir / TEXT("frame_*.jpeg")), true, false);
	}

	if (FrameFiles.Num() < 2)
	{
		OutError = FString::Printf(
			TEXT("'%s' holds %d frame(s). A video solve needs a real sequence - was this a voice-only take?"),
			*FramesDir, FrameFiles.Num());
		return false;
	}

	if (!IFileManager::Get().FileExists(*AudioPath))
	{
		OutError = FString::Printf(TEXT("No audio master at '%s'."), *AudioPath);
		return false;
	}

	if (Request.LayerId.IsNone())
	{
		OutError = TEXT("A layer needs a name. None is how the top-level solve is addressed.");
		return false;
	}

	// The clip is checked now, not after minutes of solving. Graduation applies to layers exactly
	// as to clips: a captured or edited clip is a person's work.
	UFaceBank* Bank = LoadObject<UFaceBank>(nullptr, *Request.BankPath);
	if (!Bank)
	{
		OutError = FString::Printf(TEXT("No face bank at '%s'."), *Request.BankPath);
		return false;
	}

	FFaceClip* Clip = Bank->FindClip(Request.ClipId);
	if (!Clip)
	{
		OutError = FString::Printf(TEXT("Bank '%s' has no clip '%s'."),
			*Bank->GetName(), *Request.ClipId.ToString());
		return false;
	}

	if (!Clip->IsOwnedByPipeline())
	{
		OutError = FString::Printf(
			TEXT("Left alone: clip '%s' is not pipeline-owned. Call RevertToGenerated first if you mean it."),
			*Request.ClipId.ToString());
		return false;
	}

	float MeasuredFps = ReadMeasuredFps(Request.TakeDir);
	if (MeasuredFps <= 1.f)
	{
		// No manifest, or an unmeasured rate. 30 is what a webcam almost certainly was; the manifest
		// existing at all means the take finalised, so this is a fallback, not the normal path.
		UE_LOG(LogFaceForgeVideo, Warning,
			TEXT("The take's manifest gives no usable frame rate; assuming 30. Timing may drift."));
		MeasuredFps = 30.f;
	}

	// ---------------------------------------------------------------------------------------------
	// Build the assets the engine pipeline wants, all real and all saved.
	// ---------------------------------------------------------------------------------------------

	const FString TakeId = FPaths::GetCleanFilename(Request.TakeDir);
	const UFaceForgeSettings* Settings = GetDefault<UFaceForgeSettings>();
	const FString PackagePath = (Settings ? Settings->DefaultOutputPath : TEXT("/Game/_Generated/Face"))
		/ TEXT("Video") / TakeId;

	// The pipeline addresses frames by sequential index parsed out of the filename - the capture's
	// tick-stamped names overflow that parse into INT32_MAX and every frame "fails to find file",
	// measured the hard way. So the solve stages a derived sequence: the same frames, sorted by
	// their ticks (zero-padded, so lexical order is time order), copied under sequential names.
	// The take keeps its truthful names; frames_seq is derived data, safe to delete any time.
	const FString StagedDir = Request.TakeDir / TEXT("frames_seq");
	{
		IFileManager& FileManager = IFileManager::Get();
		FileManager.DeleteDirectory(*StagedDir, /*RequireExists=*/false, /*Tree=*/true);
		if (!FileManager.MakeDirectory(*StagedDir, /*Tree=*/true))
		{
			OutError = FString::Printf(TEXT("Could not create '%s'."), *StagedDir);
			return false;
		}

		FrameFiles.Sort();
		for (int32 Index = 0; Index < FrameFiles.Num(); ++Index)
		{
			const FString Staged = StagedDir / FString::Printf(TEXT("frame_%08d.jpg"), Index);
			if (FileManager.Copy(*Staged, *(FramesDir / FrameFiles[Index])) != COPY_OK)
			{
				OutError = FString::Printf(TEXT("Could not stage frame %d of %d into '%s'."),
					Index, FrameFiles.Num(), *StagedDir);
				return false;
			}
		}
	}

	UImgMediaSource* Sequence = CreateAsset<UImgMediaSource>(PackagePath, TEXT("IMG_") + TakeId);
	if (!Sequence)
	{
		OutError = TEXT("Could not create the image sequence asset.");
		return false;
	}

	Sequence->SetSequencePath(StagedDir);

	// The measured rate as a rational. The pipeline rejects footage whose rate has a zero
	// numerator, and a webcam's true rate is never a clean integer - 1000 denominator keeps three
	// decimals of what was actually measured.
	Sequence->FrameRateOverride = FFrameRate(FMath::RoundToInt32(MeasuredFps * 1000.f), 1000);

	// The take's WAV master imported beside the footage. The mono pipeline reads audio for the
	// tongue solve, and the same asset is what a voice-conversion pass will start from later.
	USoundWave* Audio = nullptr;
	{
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = AudioPath;
		Task->DestinationPath = PackagePath;
		Task->DestinationName = TEXT("SW_") + TakeId;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;

		USoundFactory* Factory = NewObject<USoundFactory>();
		Factory->bAutoCreateCue = false;
		Task->Factory = Factory;

		AssetTools.Get().ImportAssetTasks({ Task });

		for (UObject* Object : Task->GetObjects())
		{
			if ((Audio = Cast<USoundWave>(Object)) != nullptr)
			{
				break;
			}
		}
	}

	if (!Audio)
	{
		OutError = FString::Printf(TEXT("The WAV at '%s' did not import as a sound wave."), *AudioPath);
		return false;
	}

	UFootageCaptureData* Footage = CreateAsset<UFootageCaptureData>(PackagePath, TEXT("FCD_") + TakeId);
	if (!Footage)
	{
		OutError = TEXT("Could not create the footage capture data asset.");
		return false;
	}

	Footage->ImageSequences = { Sequence };
	Footage->AudioTracks = { Audio };
	Footage->Metadata.FrameRate = MeasuredFps;
	Footage->Metadata.DeviceClass = EFootageDeviceClass::Unspecified;
	Footage->Metadata.DeviceModelName = TEXT("PerformanceForge webcam take");

	UMetaHumanPerformance* Performance =
		CreateAsset<UMetaHumanPerformance>(PackagePath, TEXT("MHP_") + TakeId);
	if (!Performance)
	{
		OutError = TEXT("Could not create the performance asset.");
		return false;
	}

	Performance->SetInputType(EDataInputType::MonoFootage);
	Performance->SetFootageCaptureData(Footage);

	if (Performance->FootageCaptureData == nullptr)
	{
		// The setter rejects footage it considers invalid and logs why; carry that forward.
		OutError = TEXT("The pipeline rejected the footage - most likely the frame rate. "
		                "See LogMetaHumanPerformance above for its own reason.");
		return false;
	}

	SaveAssetPackage(Sequence);
	SaveAssetPackage(Audio);
	SaveAssetPackage(Footage);

	// ---------------------------------------------------------------------------------------------
	// Run it.
	// ---------------------------------------------------------------------------------------------

	UMetaHumanVideoSolveTask* SolveTask = NewObject<UMetaHumanVideoSolveTask>();
	SolveTask->Request = Request;
	SolveTask->Performance = Performance;
	SolveTask->ImportedAudio = Audio;
	SolveTask->TakeFrameCount = FrameFiles.Num();
	SolveTask->StartSeconds = FPlatformTime::Seconds();

	ActiveTask.Reset(SolveTask);
	SolveTask->Run(MoveTemp(OnComplete));
	return true;
}

// -------------------------------------------------------------------------------------------------
// UMetaHumanVideoSolveTask
// -------------------------------------------------------------------------------------------------

void UMetaHumanVideoSolveTask::Run(TFunction<void(bool, FString)> InOnComplete)
{
	OnComplete = MoveTemp(InOnComplete);

	Performance->OnProcessingFinishedDynamic.AddDynamic(this, &UMetaHumanVideoSolveTask::OnPipelineFinished);

	UE_LOG(LogFaceForgeVideo, Log, TEXT("Solving %d frame(s) of '%s' through the monocular pipeline."),
		TakeFrameCount, *Request.TakeDir);

	const EStartPipelineErrorType StartError = Performance->StartPipeline(/*bInIsScriptedProcessing=*/true);
	if (StartError != EStartPipelineErrorType::None)
	{
		Performance->OnProcessingFinishedDynamic.RemoveDynamic(this, &UMetaHumanVideoSolveTask::OnPipelineFinished);
		Finish(false, FString::Printf(
			TEXT("StartPipeline refused: %s. The Output Log's MetaHuman categories say why."),
			StartError == EStartPipelineErrorType::NoFrames ? TEXT("no frames") : TEXT("disabled")));
	}
}

void UMetaHumanVideoSolveTask::OnPipelineFinished()
{
	using namespace MetaHumanVideoPrivate;

	Performance->OnProcessingFinishedDynamic.RemoveDynamic(this, &UMetaHumanVideoSolveTask::OnPipelineFinished);

	const float WallClock = static_cast<float>(FPlatformTime::Seconds() - StartSeconds);

	const TArray<FFrameAnimationData> Frames = Performance->GetAnimationData();
	if (Frames.Num() == 0)
	{
		Finish(false, TEXT("The pipeline finished but produced no animation frames. A cancelled or "
		                   "failed run looks like this; the Output Log's MetaHuman categories say which."));
		return;
	}

	// The curve names come from the first frame that actually carries face data - the solver's own
	// vocabulary, not a list assumed here. Order is made deterministic by sorting.
	int32 FirstSolved = INDEX_NONE;
	for (int32 Index = 0; Index < Frames.Num(); ++Index)
	{
		if (Frames[Index].AnimationData.Num() > 0)
		{
			FirstSolved = Index;
			break;
		}
	}

	if (FirstSolved == INDEX_NONE)
	{
		Finish(false, TEXT("Every frame came back without face data. The footage may not contain a "
		                   "trackable face, or the tracking models are missing."));
		return;
	}

	TArray<FName> CurveNames;
	for (const TPair<FString, float>& Pair : Frames[FirstSolved].AnimationData)
	{
		CurveNames.Add(FName(*Pair.Key));
	}
	CurveNames.Sort(FNameLexicalLess());

	// Which control set this actually is. The audio solver emits the 81 board GUI controls; the
	// monocular pipeline emits the ~251 raw RigLogic controls - measured on the first real solve
	// (251 curves), and the two are different vocabularies, not renamings. Getting this label wrong
	// is exactly the silent-zero failure the vocabulary system exists to refuse, so it is derived
	// from the names rather than assumed: raw controls are 'CTRL_expressions_*', board controls
	// carry an axis suffix like 'CTRL_C_jaw.ty'.
	int32 RawStyleNames = 0;
	for (const FName Name : CurveNames)
	{
		if (Name.ToString().StartsWith(TEXT("CTRL_expressions")))
		{
			++RawStyleNames;
		}
	}
	const bool bLooksRaw = RawStyleNames * 2 > CurveNames.Num();

	UE_LOG(LogFaceForgeVideo, Log,
		TEXT("Solver vocabulary: %d curve(s), %d raw-style name(s) -> %s. First: %s, last: %s."),
		CurveNames.Num(), RawStyleNames, bLooksRaw ? TEXT("MetaHumanRaw") : TEXT("MetaHumanBoard"),
		CurveNames.Num() > 0 ? *CurveNames[0].ToString() : TEXT("-"),
		CurveNames.Num() > 0 ? *CurveNames.Last().ToString() : TEXT("-"));

	FFaceCurveSamples Curves;
	Curves.Vocabulary = bLooksRaw ? EFaceVocabulary::MetaHumanRaw : EFaceVocabulary::MetaHumanBoard;
	Curves.CurveNames = CurveNames;
	Curves.Fps = static_cast<float>(Performance->GetFrameRate().AsDecimal());
	Curves.Allocate(Frames.Num());

	FFaceHeadPoseSamples HeadPose;
	HeadPose.Fps = Curves.Fps;
	HeadPose.Frames.Reserve(Frames.Num());

	int32 UnsolvedFrames = 0;
	for (int32 Frame = 0; Frame < Frames.Num(); ++Frame)
	{
		const FFrameAnimationData& Data = Frames[Frame];
		if (Data.AnimationData.Num() == 0)
		{
			++UnsolvedFrames;
		}

		for (int32 Curve = 0; Curve < CurveNames.Num(); ++Curve)
		{
			if (const float* Value = Data.AnimationData.Find(CurveNames[Curve].ToString()))
			{
				Curves.SetValue(Frame, Curve, *Value);
			}
		}

		HeadPose.Frames.Add(Data.Pose);
	}

	FString ValidationError;
	if (!Curves.Validate(ValidationError))
	{
		Finish(false, FString::Printf(TEXT("The solved curves failed validation: %s"), *ValidationError));
		return;
	}

	// ---------------------------------------------------------------------------------------------
	// Deliver into the layer.
	// ---------------------------------------------------------------------------------------------

	UFaceBank* Bank = LoadObject<UFaceBank>(nullptr, *Request.BankPath);
	FFaceClip* Clip = Bank ? Bank->FindClip(Request.ClipId) : nullptr;
	if (!Clip)
	{
		Finish(false, TEXT("The bank or clip vanished while the solve ran."));
		return;
	}

	FFaceClipLayer Layer;
	Layer.LayerId = Request.LayerId;
	Layer.ProviderId = TEXT("MetaHumanVideo");
	Layer.Mask = EFaceSolveMask::FullFace;
	Layer.Weight = Request.Weight;
	Layer.SourceDescription = FString::Printf(TEXT("video %s (%d frames)"),
		*FPaths::GetCleanFilename(Request.TakeDir), TakeFrameCount);
	Layer.SolveHash = HashString(FString::Printf(TEXT("video|%s|%d|%.3f"),
		*FPaths::GetCleanFilename(Request.TakeDir), TakeFrameCount, Curves.Fps));
	Layer.SolvedWithModels = { TEXT("MetaHumanMonocular") };
	Layer.SolvedAt = FDateTime::UtcNow();
	Layer.Curves = MoveTemp(Curves);
	Layer.HeadPose = MoveTemp(HeadPose);

	Clip->SetLayer(Layer);
	Bank->MarkPackageDirty();
	SaveAssetPackage(Bank);

	// The performance asset keeps the full solve - contours, per-frame data - and is the thing to
	// open when a face looks wrong. Saved last; it is by far the largest.
	SaveAssetPackage(Performance);

	Finish(true, FString::Printf(
		TEXT("Solved %d frame(s) in %.1fs (%d unsolved) into layer '%s' of clip '%s': %d curves at %.2f fps."),
		Frames.Num(), WallClock, UnsolvedFrames, *Request.LayerId.ToString(),
		*Request.ClipId.ToString(), Layer.Curves.NumCurves(), Layer.Curves.Fps));
}

void UMetaHumanVideoSolveTask::Finish(bool bSuccess, const FString& Message)
{
	if (bSuccess)
	{
		UE_LOG(LogFaceForgeVideo, Log, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogFaceForgeVideo, Error, TEXT("%s"), *Message);
	}

	TFunction<void(bool, FString)> Complete = MoveTemp(OnComplete);
	MetaHumanVideoPrivate::ActiveTask.Reset();

	if (Complete)
	{
		Complete(bSuccess, Message);
	}
}
