#include "FaceForgeMetaHumanVideo.h"

#include "MetaHumanVideoSolve.h"
#include "MetaHumanVideoToolset.h"

#include "ToolsetRegistry/UToolsetRegistry.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogFaceForgeVideo);

namespace FaceForgeVideoConsole
{
	/**
	 * The console is the surface until this graduates into the provider interface proper. The
	 * command is deliberately shaped like the layer tools it sits beside: solve into a named layer,
	 * merge separately, bake unchanged.
	 */
	static FAutoConsoleCommand CmdSolveVideo(
		TEXT("FaceForge.SolveVideoToLayer"),
		TEXT("Solve a recorded video take into a face clip layer. Arguments: <BankPath> <ClipId> "
		     "<LayerId> <TakeDir|latest> [Weight]. 'latest' takes the newest folder under "
		     "<Project>/PerformanceForge/Takes."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 4)
			{
				UE_LOG(LogFaceForgeVideo, Error,
					TEXT("Usage: FaceForge.SolveVideoToLayer <BankPath> <ClipId> <LayerId> <TakeDir|latest> [Weight]"));
				return;
			}

			FVideoSolveRequest Request;
			Request.BankPath = Args[0];
			Request.ClipId = FName(*Args[1]);
			Request.LayerId = FName(*Args[2]);
			Request.TakeDir = Args[3];
			Request.Weight = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 1.f;

			if (Request.TakeDir.Equals(TEXT("latest"), ESearchCase::IgnoreCase))
			{
				// A soft convention, not a dependency: PerformanceForge files takes there, but any
				// folder holding frames/ and source_audio.wav works, whoever made it.
				const FString Root = FPaths::ConvertRelativePathToFull(
					FPaths::ProjectDir() / TEXT("PerformanceForge/Takes"));

				TArray<FString> Folders;
				IFileManager::Get().FindFiles(Folders, *(Root / TEXT("*")), false, true);
				Folders.Sort();

				if (Folders.Num() == 0)
				{
					UE_LOG(LogFaceForgeVideo, Error, TEXT("No takes under '%s'."), *Root);
					return;
				}

				// Take ids end in a timestamp, so lexical order is chronological order.
				Request.TakeDir = Root / Folders.Last();
				UE_LOG(LogFaceForgeVideo, Log, TEXT("latest -> %s"), *Request.TakeDir);
			}

			FString Error;
			const bool bStarted = FMetaHumanVideoSolver::Solve(Request,
				[](bool bSuccess, const FString& Message)
				{
					// Solve already logged; nothing further to do from the console surface.
				},
				Error);

			if (!bStarted)
			{
				UE_LOG(LogFaceForgeVideo, Error, TEXT("%s"), *Error);
			}
		}));
}

void FFaceForgeMetaHumanVideoModule::StartupModule()
{
	UToolsetRegistry::RegisterToolsetClass(UMetaHumanVideoToolset::StaticClass());
}

void FFaceForgeMetaHumanVideoModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FFaceForgeMetaHumanVideoModule, FaceForgeMetaHumanVideo)
