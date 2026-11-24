#include "GinkgoRecoilPatternFactory.h"

#include "EditorFramework/AssetImportData.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Weapon/GinkgoRecoilPattern.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoRecoilImport, Log, All);

namespace GinkgoRecoilImport
{
	/** 读文件填进模板，记下来源；读不成时模板不变 */
	bool LoadInto(UGinkgoRecoilPattern& Pattern, const FString& Filename, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Filename))
		{
			OutError = FString::Printf(TEXT("读不到 %s"), *Filename);
			return false;
		}
		Pattern.Modify();
		if (!Pattern.ImportFromJson(Text, OutError))
		{
			return false;
		}
		Pattern.AssetImportData->Update(Filename);
		return true;
	}
}

UGinkgoRecoilPatternFactory::UGinkgoRecoilPatternFactory()
{
	SupportedClass = UGinkgoRecoilPattern::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	Formats.Add(TEXT("recoil;Ginkgo recoil pattern"));
}

UObject* UGinkgoRecoilPatternFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, const FName InName, const EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UGinkgoRecoilPattern* Pattern = NewObject<UGinkgoRecoilPattern>(InParent, InClass, InName, Flags | RF_Transactional);
	FString Error;
	if (!GinkgoRecoilImport::LoadInto(*Pattern, Filename, Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("后坐模板 %s：%s"), *Filename, *Error);
		return nullptr;
	}
	UE_LOG(LogGinkgoRecoilImport, Display, TEXT("%s：%d 发，从第 %d 发循环，来自 %s"), *Pattern->GetPathName(), Pattern->Shots.Num(), Pattern->LoopFrom, *Filename);
	return Pattern;
}

bool UGinkgoRecoilPatternFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	const UGinkgoRecoilPattern* Pattern = Cast<UGinkgoRecoilPattern>(Obj);
	if (!Pattern || !Pattern->AssetImportData)
	{
		return false;
	}
	Pattern->AssetImportData->ExtractFilenames(OutFilenames);
	return true;
}

void UGinkgoRecoilPatternFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	if (UGinkgoRecoilPattern* Pattern = Cast<UGinkgoRecoilPattern>(Obj); Pattern && NewReimportPaths.Num() == 1)
	{
		Pattern->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UGinkgoRecoilPatternFactory::Reimport(UObject* Obj)
{
	UGinkgoRecoilPattern* Pattern = Cast<UGinkgoRecoilPattern>(Obj);
	if (!Pattern || !Pattern->AssetImportData)
	{
		return EReimportResult::Failed;
	}
	const FString Filename = Pattern->AssetImportData->GetFirstFilename();
	FString Error;
	if (!GinkgoRecoilImport::LoadInto(*Pattern, Filename, Error))
	{
		UE_LOG(LogGinkgoRecoilImport, Error, TEXT("%s：%s"), *Pattern->GetPathName(), *Error);
		return EReimportResult::Failed;
	}
	UE_LOG(LogGinkgoRecoilImport, Display, TEXT("%s：重新读了 %s，%d 发"), *Pattern->GetPathName(), *Filename, Pattern->Shots.Num());
	Pattern->MarkPackageDirty();
	return EReimportResult::Succeeded;
}
