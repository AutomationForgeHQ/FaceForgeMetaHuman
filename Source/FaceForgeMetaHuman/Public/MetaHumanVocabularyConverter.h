// Face-board GUI controls to RigLogic raw controls, using the engine's own conversion.

#pragma once

#include "CoreMinimal.h"
#include "IFaceVocabularyConverter.h"

/**
 * Converts the solver's 81 face-board GUI controls into the ~276 RigLogic raw controls a MetaHuman
 * face rig actually reads.
 *
 * **This is the step whose absence makes a perfectly good face montage animate nothing.** The solver
 * emits `CTRL_C_jaw.ty`; the rig reads `CTRL_expressions_jawOpen`. Both are "MetaHuman controls", the
 * skeleton registers only the second kind, and nothing in the engine complains when you hand it the
 * first - so the failure is a character who simply does not move.
 *
 * The conversion is `GuiToRawControlsUtils::ConvertGuiToRawControls`, the same call Epic's own
 * `FSpeechToAnimNode` makes between solving and exporting. It is:
 *
 * - **Universal, not per-character.** It takes a `TMap` and returns a `TMap`; no DNA, no rig instance,
 *   no identity. The expression logic it encodes belongs to the shared MetaHuman archetype rig, which
 *   is the same reason a face animation authored on one MetaHuman plays correctly on another.
 * - **Not expressible as a mapping asset.** It is not weighted sums - it is the rig's combination
 *   logic. Hence a converter rather than a `UFaceCurveMapping`.
 */
class FMetaHumanVocabularyConverter : public IFaceVocabularyConverter
{
public:

	static const FName ConverterId;

	virtual FName GetConverterId() const override { return ConverterId; }

	virtual EFaceVocabulary GetSourceVocabulary() const override { return EFaceVocabulary::MetaHumanBoard; }
	virtual EFaceVocabulary GetTargetVocabulary() const override { return EFaceVocabulary::MetaHumanRaw; }

	virtual bool Convert(const FFaceCurveSamples& In, FFaceCurveSamples& Out, FString& OutError) const override;
};
