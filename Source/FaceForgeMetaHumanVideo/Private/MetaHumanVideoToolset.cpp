#include "MetaHumanVideoToolset.h"

#include "FaceForgeMetaHumanVideo.h"
#include "MetaHumanVideoSolve.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

UToolCallAsyncResultString* UMetaHumanVideoToolset::SolveVideoTakeToLayer(
	const FString& BankPath, FName ClipId, FName LayerId, const FString& TakeDir, float Weight)
{
	FVideoSolveRequest Request;
	Request.BankPath = BankPath;
	Request.ClipId = ClipId;
	Request.LayerId = LayerId;
	Request.TakeDir = TakeDir;
	Request.Weight = Weight;

	UToolCallAsyncResultString* Async = NewObject<UToolCallAsyncResultString>();

	if (Request.TakeDir.Equals(TEXT("latest"), ESearchCase::IgnoreCase))
	{
		// A soft convention, not a dependency: PerformanceForge files takes there, but any folder
		// holding frames/ and source_audio.wav works, whoever made it.
		const FString Root = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("PerformanceForge/Takes"));

		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Root / TEXT("*")), false, true);
		Folders.Sort();

		if (Folders.Num() == 0)
		{
			Async->SetError(FString::Printf(TEXT("No takes under '%s'."), *Root));
			return Async;
		}

		// Take ids end in a timestamp, so lexical order is chronological order.
		Request.TakeDir = Root / Folders.Last();
	}

	FString Error;
	const bool bStarted = FMetaHumanVideoSolver::Solve(Request,
		[Async](bool bSuccess, const FString& Message)
		{
			if (bSuccess)
			{
				Async->SetValue(Message);
			}
			else
			{
				Async->SetError(Message);
			}
		},
		Error);

	if (!bStarted)
	{
		Async->SetError(Error);
	}

	return Async;
}
