#include "MetaHumanVocabularyConverter.h"

#include "FaceForge.h"

#include "GuiToRawControlsUtils.h"

const FName FMetaHumanVocabularyConverter::ConverterId(TEXT("MetaHumanBoardToRaw"));

bool FMetaHumanVocabularyConverter::Convert(
	const FFaceCurveSamples& In, FFaceCurveSamples& Out, FString& OutError) const
{
	if (In.Vocabulary != EFaceVocabulary::MetaHumanBoard)
	{
		OutError = FString::Printf(
			TEXT("Expected face-board controls but was given %s."), *FaceForge::ToString(In.Vocabulary));
		return false;
	}

	FString InError;
	if (!In.Validate(InError))
	{
		OutError = FString::Printf(TEXT("Source curves are not usable: %s"), *InError);
		return false;
	}

	// Convert frame by frame, because the engine's utility works on one frame's control map. The raw
	// control set is the same for every frame, so the names are taken once from the first result and
	// then reused - both to keep the layout stable and so that a frame which somehow converted to a
	// different set is caught rather than silently reshaping the block.
	TArray<TMap<FString, float>> RawFrames;
	RawFrames.Reserve(In.NumFrames);

	TMap<FString, float> GuiFrame;
	GuiFrame.Reserve(In.NumCurves());

	for (int32 Frame = 0; Frame < In.NumFrames; ++Frame)
	{
		GuiFrame.Reset();

		for (int32 CurveIdx = 0; CurveIdx < In.NumCurves(); ++CurveIdx)
		{
			GuiFrame.Add(In.CurveNames[CurveIdx].ToString(), In.GetValue(Frame, CurveIdx));
		}

		RawFrames.Add(GuiToRawControlsUtils::ConvertGuiToRawControls(GuiFrame));
	}

	if (RawFrames.Num() == 0 || RawFrames[0].Num() == 0)
	{
		OutError = TEXT("The engine's GUI-to-raw conversion returned nothing.");
		return false;
	}

	TArray<FString> RawNames;
	RawFrames[0].GetKeys(RawNames);

	// Sorted so the curve order is deterministic rather than following a hash map's iteration order.
	// Without this, two bakes of identical input produce assets that differ only in layout - which is
	// harmless until somebody diffs them.
	RawNames.Sort();

	Out = FFaceCurveSamples();
	Out.Vocabulary = EFaceVocabulary::MetaHumanRaw;
	Out.Fps = In.Fps;
	Out.CurveNames.Reserve(RawNames.Num());

	for (const FString& Name : RawNames)
	{
		Out.CurveNames.Add(FName(*Name));
	}

	Out.Allocate(In.NumFrames);

	for (int32 Frame = 0; Frame < RawFrames.Num(); ++Frame)
	{
		const TMap<FString, float>& RawFrame = RawFrames[Frame];

		for (int32 CurveIdx = 0; CurveIdx < RawNames.Num(); ++CurveIdx)
		{
			if (const float* Value = RawFrame.Find(RawNames[CurveIdx]))
			{
				Out.SetValue(Frame, CurveIdx, *Value);
			}
		}
	}

	UE_LOG(LogFaceForge, Log, TEXT("Converted %d board control(s) to %d raw control(s) over %d frame(s)."),
		In.NumCurves(), Out.NumCurves(), Out.NumFrames);

	return true;
}
