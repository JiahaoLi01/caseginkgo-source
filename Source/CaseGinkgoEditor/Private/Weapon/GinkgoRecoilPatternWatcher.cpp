#include "GinkgoRecoilPatternWatcher.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "DirectoryWatcherModule.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Weapon/GinkgoRecoilPattern.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoRecoilWatcher, Log, All);

void FGinkgoRecoilPatternWatcher::Start()
{
	if (!GIsEditor || IsRunningCommandlet())
	{
		return;
	}
	Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/RecoilEditor/patterns"));
	if (!FPaths::DirectoryExists(Directory))
	{
		return;
	}
	FDirectoryWatcherModule& Module = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if (IDirectoryWatcher* Watcher = Module.Get())
	{
		Watcher->RegisterDirectoryChangedCallback_Handle(Directory, IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FGinkgoRecoilPatternWatcher::OnDirectoryChanged), WatchHandle);
	}
}

void FGinkgoRecoilPatternWatcher::Stop()
{
	if (!WatchHandle.IsValid())
	{
		return;
	}
	if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			Watcher->UnregisterDirectoryChangedCallback_Handle(Directory, WatchHandle);
		}
	}
	WatchHandle.Reset();
}

void FGinkgoRecoilPatternWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	TSet<FString> Changed;
	for (const FFileChangeData& Change : Changes)
	{
		// 浏览器存文件是写好临时文件再改名，在这里是 Added 或 Modified；扩展名以外的（临时文件）不看
		if (Change.Action != FFileChangeData::FCA_Removed && FPaths::GetExtension(Change.Filename) == TEXT("recoil"))
		{
			Changed.Add(FPaths::ConvertRelativePathToFull(Change.Filename));
		}
	}
	if (Changed.IsEmpty())
	{
		return;
	}

	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByClass(UGinkgoRecoilPattern::StaticClass()->GetClassPathName(), Assets);
	for (const FString& Filename : Changed)
	{
		bool bFound = false;
		for (const FAssetData& Asset : Assets)
		{
			UGinkgoRecoilPattern* Pattern = Cast<UGinkgoRecoilPattern>(Asset.GetAsset());
			if (!Pattern || !Pattern->AssetImportData || !FPaths::IsSamePath(Pattern->AssetImportData->GetFirstFilename(), Filename))
			{
				continue;
			}
			bFound = true;
			// 一次保存可能来几个事件：内容没变的不重读
			const FAssetImportInfo& Source = Pattern->AssetImportData->GetSourceData();
			if (!Source.SourceFiles.IsEmpty() && Source.SourceFiles[0].FileHash == FMD5Hash::HashFile(*Filename))
			{
				continue;
			}
			FReimportManager::Instance()->Reimport(Pattern, /*bAskForNewFileIfMissing=*/false, /*bShowNotification=*/true);
		}
		if (!bFound)
		{
			UE_LOG(LogGinkgoRecoilWatcher, Display, TEXT("%s 还不是哪个模板的来源：拖进内容浏览器导入一次"), *Filename);
		}
	}
}
