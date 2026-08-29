#include "FaceForgeMetaHuman.h"

#include "FaceForge.h"
#include "MetaHumanFaceProvider.h"
#include "MetaHumanVocabularyConverter.h"

void FFaceForgeMetaHumanModule::StartupModule()
{
	// GetPtr loads FaceForge rather than looking it up. A .uplugin dependency guarantees FaceForge is
	// enabled, not that its module started first, so a lookup here works on some runs and returns null
	// on others - and the failure mode is a solver that silently never appears.
	if (FFaceForgeModule* FaceForge = FFaceForgeModule::GetPtr())
	{
		Provider = MakeShared<FMetaHumanFaceProvider>();
		FaceForge->RegisterProvider(Provider.ToSharedRef());

		// Registered beside the provider because it is useless without it and useless to anything else:
		// the conversion exists precisely to bridge this solver's output to the rig it was built for.
		Converter = MakeShared<FMetaHumanVocabularyConverter>();
		FaceForge->RegisterVocabularyConverter(Converter.ToSharedRef());
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("FaceForgeMetaHuman could not load the FaceForge module, so the in-engine face solver is "
			     "not available. Check that the FaceForge plugin is enabled."));
	}
}

void FFaceForgeMetaHumanModule::ShutdownModule()
{
	// GetPtrIfLoaded, never GetPtr: loading a module in order to tell it we are going away would be
	// worse than doing nothing.
	if (FFaceForgeModule* FaceForge = FFaceForgeModule::GetPtrIfLoaded())
	{
		FaceForge->UnregisterProvider(FMetaHumanFaceProvider::ProviderId);
		FaceForge->UnregisterVocabularyConverter(FMetaHumanVocabularyConverter::ConverterId);
	}

	Provider.Reset();
	Converter.Reset();
}

IMPLEMENT_MODULE(FFaceForgeMetaHumanModule, FaceForgeMetaHuman)
