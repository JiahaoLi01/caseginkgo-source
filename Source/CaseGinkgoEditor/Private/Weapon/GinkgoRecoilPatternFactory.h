#pragma once

#include "CoreMinimal.h"
#include "EditorReimportHandler.h"
#include "Factories/Factory.h"
#include "GinkgoRecoilPatternFactory.generated.h"

/**
 * @class UGinkgoRecoilPatternFactory : 导入后坐模板
 * 网页工具（Tools/RecoilEditor）导出的 .recoil 拖进内容浏览器就成 UGinkgoRecoilPattern，右键 Reimport 读回同一个文件。
 * 命令行走 AssetImportTask（Scripts/Assets/import_recoil.py）。
 * 为什么不写插件（2026-11-05，你说的）：编辑在网页里，引擎这边只要读文件，一个工厂就够，旧插件 RecoilAssetsImporter 的 Slate 编辑器不要了
 */
UCLASS()
class UGinkgoRecoilPatternFactory : public UFactory, public FReimportHandler
{
	GENERATED_BODY()

public:
	UGinkgoRecoilPatternFactory();

	//~ Begin UFactory
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	//~ End UFactory

	//~ Begin FReimportHandler
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	//~ End FReimportHandler
};
