#pragma once

#include "Modules/ModuleManager.h"

/**
 * Registers the in-engine audio-driven animation solver with FaceForge.
 *
 * The whole module is one registration and its undo. Deleting this plugin removes a solver and changes
 * nothing else - FaceForge never learns its name.
 */
class FFaceForgeMetaHumanModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	TSharedPtr<class FMetaHumanFaceProvider> Provider;

	TSharedPtr<class FMetaHumanVocabularyConverter> Converter;
};
