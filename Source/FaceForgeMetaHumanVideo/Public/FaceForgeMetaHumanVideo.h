// The video half of the MetaHuman adapter: footage in, a clip layer out.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

FACEFORGEMETAHUMANVIDEO_API DECLARE_LOG_CATEGORY_EXTERN(LogFaceForgeVideo, Log, All);

/**
 * A separate module from the audio provider on purpose. The audio solve depends on Speech2Face and
 * loads for everyone; the video solve drags in MetaHumanPerformance, CaptureData and ImgMedia, and
 * a project that only ever solves audio should not pay for any of it. One plugin, two modules, the
 * same seam the engine itself uses.
 */
class FFaceForgeMetaHumanVideoModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
